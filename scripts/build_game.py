#!/usr/bin/env python3
import argparse
import hashlib
import json
import plistlib
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

LDR_EXTS = {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".gif", ".tif", ".tiff"}
MODEL_EXTS = {
    ".fbx", ".obj", ".gltf", ".glb", ".dae", ".blend", ".3ds",
    ".stl", ".ply", ".x", ".smd", ".md5mesh", ".md2", ".md3", ".ms3d", ".lwo", ".lws"
}
RUNTIME_RAW_ASSET_EXTS = {".hdr", ".exr", ".wav", ".mp3", ".ogg", ".flac", ".ktx", ".ktx2", ".dds", ".cube", ".cmat"}
KTX2_CACHE_VERSION = "v2a"
ENV_CACHE_VERSION = "v1"
STATIC_LIGHTMAP_COOK_VERSION = "v1"
EMBEDDED_MARKER = "#embedded:"
FNV64_OFFSET = 14695981039346656037
FNV64_PRIME = 1099511628211


def find_project_file(root: Path) -> Optional[Path]:
    if root.is_file() and root.suffix.lower() == ".cproj":
        return root
    candidate = root / "Project.cproj"
    return candidate if candidate.exists() else None


def load_project(project_file: Path) -> dict:
    with project_file.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def list_scene_files(project_root: Path, project_data: dict) -> list[Path]:
    scenes_dir = project_root / project_data.get("scenes", "Scenes")
    if not scenes_dir.exists():
        return []
    return sorted(path for path in scenes_dir.rglob("*.cscene") if path.is_file())


def resolve_startup_scene(project_root: Path, project_data: dict, override: Optional[str]) -> str:
    startup_scene = override or project_data.get("startupScene", "")
    if startup_scene:
        scene_path = project_root / startup_scene
        if not scene_path.exists():
            raise FileNotFoundError(f"Startup scene not found: {startup_scene}")
        return startup_scene

    scenes = list_scene_files(project_root, project_data)
    if len(scenes) == 1:
        return scenes[0].relative_to(project_root).as_posix()
    if not scenes:
        raise FileNotFoundError("No .cscene files found under the project.")
    raise FileNotFoundError("Startup scene is not configured. Set it in Project Settings or pass --startup-scene.")


def scene_has_runtime_camera(scene_path: Path) -> bool:
    with scene_path.open("r", encoding="utf-8") as handle:
        scene_data = json.load(handle)
    for entity in scene_data.get("entities", []):
        if entity.get("editorOnly", False):
            continue
        if not entity.get("active", True):
            continue
        components = entity.get("components", {})
        camera = components.get("Camera")
        if not isinstance(camera, dict):
            continue
        if camera.get("editorCamera", False):
            continue
        return True
    return False


def validate_startup_scene(project_root: Path, startup_scene: str) -> None:
    scene_path = project_root / startup_scene
    if not scene_has_runtime_camera(scene_path):
        raise RuntimeError(
            f"Startup scene '{startup_scene}' does not contain an active non-editor Camera. "
            "Add a gameplay camera entity to the scene before building."
        )


def relative_cooked_scene_path(project_root: Path, scene_path: Path) -> Path:
    return scene_path.relative_to(project_root).with_suffix(".ccscene")


def normalize_environment_key(project_root: Path, source_path: Path, project_data: dict) -> str:
    resolved = source_path.resolve()
    asset_roots = [project_root / relative for relative in (project_data.get("assetPaths") or [project_data.get("assets", "Assets")])]
    for root in asset_roots:
        try:
            return resolved.relative_to(root.resolve()).as_posix()
        except ValueError:
            continue
    try:
        return resolved.relative_to(project_root.resolve()).as_posix()
    except ValueError:
        return resolved.as_posix()


def relative_cooked_environment_path(project_root: Path, source_path: Path, project_data: dict) -> Path:
    key = normalize_environment_key(project_root, source_path, project_data)
    return Path("Library") / "ImportCache" / f"hdri_{stable_hash(key)}_{ENV_CACHE_VERSION}.cenv"


def normalize_static_lightmap_key(project_root: Path, source_path: Path) -> str:
    resolved = source_path.resolve()
    try:
        return resolved.relative_to(project_root.resolve()).as_posix()
    except ValueError:
        return resolved.as_posix()


def relative_cooked_static_lightmap_path(project_root: Path, source_path: Path) -> Path:
    key = normalize_static_lightmap_key(project_root, source_path)
    return Path("Library") / "ImportCache" / f"lightmap_rgbm_{stable_hash(key)}_{STATIC_LIGHTMAP_COOK_VERSION}.ktx2"


def relative_baked_texture_cache_path(source_path: Path) -> Path:
    return Path("Library") / "ImportCache" / f"path_{stable_hash(source_path.resolve().as_posix())}_{KTX2_CACHE_VERSION}.ktx2"


def collect_environment_refs(scene_files: list[Path], project_root: Path, project_data: dict) -> list[Path]:
    refs: set[Path] = set()
    asset_roots = [project_root / relative for relative in (project_data.get("assetPaths") or [project_data.get("assets", "Assets")])]
    for scene_path in scene_files:
        try:
            scene_data = json.loads(scene_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue

        environment = scene_data.get("sceneSettings", {}).get("environment", {})
        if not isinstance(environment, dict):
            continue
        skybox_entry = environment.get("skybox")
        skybox_ref = extract_asset_ref(skybox_entry)
        if not skybox_ref or skybox_ref == "Builtin Sky":
            continue

        candidates = [(project_root / skybox_ref).resolve()]
        for root in asset_roots:
            candidates.append((root / skybox_ref).resolve())

        resolved = next((candidate for candidate in candidates if candidate.exists()), candidates[0])
        refs.add(resolved)

    return sorted(refs)


def bake_source_scenes(player_app: Path,
                       project_file: Path,
                       scene_files: list[Path],
                       baked_root: Path) -> tuple[dict[Path, Path], dict[str, int]]:
    if not scene_files:
        return {}, {
            "bakedSceneCount": 0,
            "bakedAtlasCount": 0,
            "bakedRendererCount": 0,
            "bakedTexelCount": 0,
            "bakedLightCount": 0,
        }

    executable = player_app / "Contents" / "MacOS" / "CrescentPlayer"
    if not executable.exists():
        raise FileNotFoundError(f"Cooker executable not found: {executable}")

    project_root = project_file.parent
    baked_scene_map: dict[Path, Path] = {}
    stats = {
        "bakedSceneCount": 0,
        "bakedAtlasCount": 0,
        "bakedRendererCount": 0,
        "bakedTexelCount": 0,
        "bakedLightCount": 0,
    }

    def scene_bake_stats(scene_path: Path) -> tuple[int, int, int]:
        try:
            scene_data = json.loads(scene_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return 0, 0, 0

        scene_baked_lights = 0
        scene_baked_renderers = 0
        scene_atlas_paths: set[str] = set()
        for entity in scene_data.get("entities", []):
            components = entity.get("components", {})
            light = components.get("Light")
            if isinstance(light, dict) and (
                light.get("contributeToStaticBake", False)
                or light.get("bakeToVertexLighting", False)
            ):
                scene_baked_lights += 1

            renderer = components.get("MeshRenderer")
            if not isinstance(renderer, dict):
                continue
            static_lighting = renderer.get("staticLighting", {})
            if not isinstance(static_lighting, dict):
                continue
            lightmap_path = extract_asset_ref(static_lighting.get("lightmap"))
            if not isinstance(lightmap_path, str) or not lightmap_path:
                continue

            scene_baked_renderers += 1
            scene_atlas_paths.add(lightmap_path)

        return scene_baked_renderers, len(scene_atlas_paths), scene_baked_lights

    for scene_path in scene_files:
        baked_output = baked_root / scene_path.relative_to(project_root)
        baked_output.parent.mkdir(parents=True, exist_ok=True)
        run(
            [
                str(executable),
                "--bake-scene-lighting",
                str(project_file),
                str(scene_path),
                str(baked_output),
            ],
            cwd=project_root,
        )
        stats["bakedSceneCount"] += 1
        baked_renderer_count, baked_atlas_count, baked_light_count = scene_bake_stats(baked_output)
        effective_scene = baked_output
        if baked_renderer_count == 0 and baked_atlas_count == 0:
            source_renderer_count, source_atlas_count, source_light_count = scene_bake_stats(scene_path)
            if source_renderer_count > 0 or source_atlas_count > 0:
                effective_scene = scene_path
                baked_renderer_count = source_renderer_count
                baked_atlas_count = source_atlas_count
                baked_light_count = source_light_count

        baked_scene_map[scene_path] = effective_scene
        stats["bakedRendererCount"] += baked_renderer_count
        stats["bakedAtlasCount"] += baked_atlas_count
        stats["bakedLightCount"] += baked_light_count

    return baked_scene_map, stats


def collect_baked_lighting_artifacts(scene_sources: list[Path], project_root: Path) -> tuple[list[Path], list[Path]]:
    hdr_refs: set[Path] = set()
    ldr_refs: set[Path] = set()
    for scene_path in scene_sources:
        try:
            scene_data = json.loads(scene_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue

        for entity in scene_data.get("entities", []):
            components = entity.get("components", {})
            renderer = components.get("MeshRenderer")
            if not isinstance(renderer, dict):
                continue
            static_lighting = renderer.get("staticLighting")
            if not isinstance(static_lighting, dict):
                continue
            for key in ("lightmap", "directionalLightmap", "shadowmask"):
                artifact_ref = extract_asset_ref(static_lighting.get(key))
                if not artifact_ref:
                    continue
                artifact_path = (project_root / artifact_ref).resolve()
                if not artifact_path.exists():
                    continue
                if artifact_path.suffix.lower() in {".exr", ".hdr"}:
                    hdr_refs.add(artifact_path)
                elif artifact_path.suffix.lower() in LDR_EXTS:
                    ldr_refs.add(artifact_path)

    return sorted(hdr_refs), sorted(ldr_refs)


def cook_static_lightmaps(player_app: Path,
                          repo_root: Path,
                          project_file: Path,
                          baked_scene_sources: list[Path],
                          cooked_root: Path) -> dict[Path, Path]:
    static_lightmaps, ldr_artifacts = collect_baked_lighting_artifacts(baked_scene_sources, project_file.parent)
    if not static_lightmaps and not ldr_artifacts:
        return {}

    executable = player_app / "Contents" / "MacOS" / "CrescentPlayer"
    if not executable.exists():
        raise FileNotFoundError(f"Cooker executable not found: {executable}")

    project_root = project_file.parent
    cooked_map: dict[Path, Path] = {}
    for source_path in static_lightmaps:
        cooked_relative = relative_cooked_static_lightmap_path(project_root, source_path)
        cooked_output = cooked_root / cooked_relative
        cooked_output.parent.mkdir(parents=True, exist_ok=True)
        run(
            [
                str(executable),
                "--cook-static-lightmap",
                str(project_file),
                str(source_path),
                str(cooked_output),
            ],
            cwd=project_root,
        )
        cooked_map[source_path] = cooked_output

    basisu = basisu_path(repo_root)
    if ldr_artifacts:
        if not basisu.exists():
            raise FileNotFoundError(f"basisu binary not found: {basisu}")
        for source_path in ldr_artifacts:
            cooked_relative = relative_baked_texture_cache_path(source_path)
            cooked_output = cooked_root / cooked_relative
            cooked_output.parent.mkdir(parents=True, exist_ok=True)
            run(
                build_basisu_command(
                    basisu,
                    source_path,
                    cooked_output,
                    srgb=False,
                    normal_map=False,
                    generate_mips=True,
                ),
                cwd=project_root,
            )
            cooked_map[source_path] = cooked_output

    return cooked_map


def cook_runtime_environments(player_app: Path,
                              project_file: Path,
                              project_data: dict,
                              environment_files: list[Path],
                              cooked_root: Path) -> dict[Path, Path]:
    if not environment_files:
        return {}

    executable = player_app / "Contents" / "MacOS" / "CrescentPlayer"
    if not executable.exists():
        raise FileNotFoundError(f"Cooker executable not found: {executable}")

    project_root = project_file.parent
    cooked_map: dict[Path, Path] = {}
    for source_path in environment_files:
        cooked_relative = relative_cooked_environment_path(project_root, source_path, project_data)
        cooked_output = cooked_root / cooked_relative
        cooked_output.parent.mkdir(parents=True, exist_ok=True)
        run(
            [
                str(executable),
                "--cook-environment",
                str(project_file),
                str(source_path),
                str(cooked_output),
            ],
            cwd=project_root,
        )
        cooked_map[source_path] = cooked_output
    return cooked_map


def cook_runtime_scenes(player_app: Path,
                        project_file: Path,
                        scene_files: list[Path],
                        source_scene_lookup: Optional[dict[Path, Path]],
                        cooked_root: Path) -> dict[str, str]:
    if not scene_files:
        return {}

    executable = player_app / "Contents" / "MacOS" / "CrescentPlayer"
    if not executable.exists():
        raise FileNotFoundError(f"Cooker executable not found: {executable}")

    project_root = project_file.parent
    cooked_map: dict[str, str] = {}
    for scene_path in scene_files:
        source_scene_path = source_scene_lookup.get(scene_path, scene_path) if source_scene_lookup else scene_path
        cooked_relative = relative_cooked_scene_path(project_root, scene_path)
        cooked_output = cooked_root / cooked_relative
        cooked_output.parent.mkdir(parents=True, exist_ok=True)
        run(
            [
                str(executable),
                "--cook-scene",
                str(project_file),
                str(source_scene_path),
                str(cooked_output),
            ],
            cwd=project_root,
        )
        cooked_map[scene_path.relative_to(project_root).as_posix()] = cooked_relative.as_posix()

    return cooked_map


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(cmd: list[str], cwd: Optional[Path] = None) -> None:
    result = subprocess.run(cmd, cwd=cwd, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"Command failed ({result.returncode}): {' '.join(cmd)}")


def basisu_path(repo_root: Path) -> Path:
    return repo_root / "ThirdParty" / "basisu" / "basisu"


def build_basisu_command(basisu: Path,
                         asset_path: Path,
                         output_path: Path,
                         *,
                         srgb: bool,
                         normal_map: bool,
                         generate_mips: bool) -> list[str]:
    use_uastc = normal_map
    cmd = [
        str(basisu),
        "-ktx2",
        "-ktx2_no_zstandard",
        "-uastc" if use_uastc else "-etc1s",
        "-srgb" if srgb else "-linear",
    ]
    if generate_mips:
        cmd.append("-mipmap")
    cmd += [
        "-quality",
        "128" if use_uastc else "80",
        "-effort",
        "5" if use_uastc else "4",
        "-file",
        str(asset_path),
        "-output_file",
        str(output_path),
        "-no_status_output",
    ]
    return cmd


def build_app(repo_root: Path, configuration: str, derived_data: Path) -> Path:
    run(
        [
            "xcodebuild",
            "-project",
            "CrescentEngine.xcodeproj",
            "-scheme",
            "CrescentPlayer",
            "-configuration",
            configuration,
            "-destination",
            "platform=macOS,arch=arm64",
            "-derivedDataPath",
            str(derived_data),
            "build",
            "CODE_SIGNING_ALLOWED=NO",
        ],
        cwd=repo_root,
    )
    app_path = derived_data / "Build" / "Products" / configuration / "CrescentPlayer.app"
    if not app_path.exists():
        raise FileNotFoundError(f"Built app not found: {app_path}")
    return app_path


def copy_tree_if_exists(src: Path, dst: Path) -> None:
    if not src.exists():
        return
    shutil.copytree(src, dst, dirs_exist_ok=True)


def extract_asset_ref(entry) -> Optional[str]:
    if isinstance(entry, str):
        return entry
    if isinstance(entry, dict):
        path = entry.get("path")
        if isinstance(path, str) and path:
            return path
    return None


def _collect_asset_refs_from_json(value, project_root: Path, out_paths: set[Path]) -> None:
    if isinstance(value, dict):
        path_value = value.get("path")
        if isinstance(path_value, str) and path_value:
            candidate = Path(path_value)
            suffix = candidate.suffix.lower()
            if suffix in LDR_EXTS or suffix in MODEL_EXTS:
                out_paths.add((project_root / candidate).resolve())
        for child in value.values():
            _collect_asset_refs_from_json(child, project_root, out_paths)
        return

    if isinstance(value, list):
        for child in value:
            _collect_asset_refs_from_json(child, project_root, out_paths)
        return

    if isinstance(value, str):
        candidate = Path(value)
        suffix = candidate.suffix.lower()
        if suffix in LDR_EXTS or suffix in MODEL_EXTS:
            out_paths.add((project_root / candidate).resolve())


def collect_scene_asset_refs(scene_files: list[Path], project_root: Path) -> tuple[set[Path], set[Path]]:
    referenced_textures: set[Path] = set()
    referenced_models: set[Path] = set()

    for scene_path in scene_files:
        try:
            scene_data = json.loads(scene_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue

        discovered: set[Path] = set()
        _collect_asset_refs_from_json(scene_data, project_root, discovered)
        for path in discovered:
            suffix = path.suffix.lower()
            if suffix in LDR_EXTS:
                referenced_textures.add(path)
            elif suffix in MODEL_EXTS:
                referenced_models.add(path)

    return referenced_textures, referenced_models


def collect_runtime_raw_texture_refs(scene_files: list[Path], project_root: Path) -> set[Path]:
    keep: set[Path] = set()
    for scene_path in scene_files:
        try:
            scene_data = json.loads(scene_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue

        for entity in scene_data.get("entities", []):
            components = entity.get("components", {})
            mesh_renderer = components.get("MeshRenderer", {})
            materials = mesh_renderer.get("materials", []) if isinstance(mesh_renderer, dict) else []
            for material in materials:
                if not isinstance(material, dict):
                    continue
                textures = material.get("textures", {})
                if not isinstance(textures, dict):
                    continue
                terrain_control = extract_asset_ref(textures.get("terrainControl"))
                if not terrain_control:
                    continue
                keep.add((project_root / terrain_control).resolve())
    return keep


def should_package_asset(path: Path, runtime_raw_texture_refs: set[Path]) -> bool:
    name = path.name
    if name == ".DS_Store":
        return False

    suffix = path.suffix.lower()
    if suffix == ".cmeta":
        return False
    if suffix in RUNTIME_RAW_ASSET_EXTS:
        return True
    if suffix in MODEL_EXTS:
        return False
    if suffix in LDR_EXTS:
        try:
            return path.resolve() in runtime_raw_texture_refs
        except OSError:
            return False
    return False


def copy_packaged_assets(project_root: Path,
                         project_data: dict,
                         game_data_dir: Path,
                         scene_files: list[Path]) -> None:
    assets_relative = Path(project_data.get("assets", "Assets"))
    source_assets_dir = project_root / assets_relative
    packaged_assets_dir = game_data_dir / assets_relative
    if not source_assets_dir.exists():
        return

    runtime_raw_texture_refs = collect_runtime_raw_texture_refs(scene_files, project_root)

    for source_path in source_assets_dir.rglob("*"):
        if not source_path.is_file():
            continue
        if not should_package_asset(source_path, runtime_raw_texture_refs):
            continue

        relative_path = source_path.relative_to(source_assets_dir)
        destination = packaged_assets_dir / relative_path
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source_path, destination)


def copy_packaged_import_cache(project_root: Path, project_data: dict, game_data_dir: Path) -> None:
    source_cache_dir = project_root / project_data.get("library", "Library") / "ImportCache"
    packaged_cache_dir = game_data_dir / "Library" / "ImportCache"
    if not source_cache_dir.exists():
        return

    for source_path in source_cache_dir.rglob("*"):
        if not source_path.is_file():
            continue
        if source_path.suffix.lower() not in {".ktx2", ".cenv"}:
            continue
        relative_path = source_path.relative_to(source_cache_dir)
        destination = packaged_cache_dir / relative_path
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source_path, destination)


def copy_packaged_scenes(project_root: Path,
                         project_data: dict,
                         game_data_dir: Path,
                         cooked_scene_map: dict[str, str],
                         cooked_scenes_root: Path) -> None:
    scenes_relative = Path(project_data.get("scenes", "Scenes"))
    source_scenes_dir = project_root / scenes_relative
    packaged_scenes_dir = game_data_dir / scenes_relative

    if not source_scenes_dir.exists():
        return

    packaged_scenes_dir.mkdir(parents=True, exist_ok=True)

    if not cooked_scene_map:
        copy_tree_if_exists(source_scenes_dir, packaged_scenes_dir)
        return

    for source_path in source_scenes_dir.rglob("*"):
        if not source_path.is_file():
            continue
        if source_path.suffix.lower() == ".cscene":
            continue
        relative_path = source_path.relative_to(source_scenes_dir)
        destination = packaged_scenes_dir / relative_path
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source_path, destination)

    copy_tree_if_exists(cooked_scenes_root / scenes_relative, packaged_scenes_dir)


def patch_info_plist(info_plist_path: Path, product_name: str, bundle_identifier: str) -> None:
    with info_plist_path.open("rb") as handle:
        data = plistlib.load(handle)
    data["CFBundleDisplayName"] = product_name
    data["CFBundleName"] = product_name
    data["CFBundleIdentifier"] = bundle_identifier
    with info_plist_path.open("wb") as handle:
        plistlib.dump(data, handle)


def prune_packaged_runtime_resources(resources_dir: Path) -> None:
    for relative_path in ("basisu", "README_JOLT.md"):
        target = resources_dir / relative_path
        if target.is_symlink() or target.is_file():
            target.unlink()
        elif target.is_dir():
            shutil.rmtree(target)


def maybe_encode_textures(repo_root: Path, project_file: Path) -> None:
    script_path = repo_root / "scripts" / "encode_textures.py"
    if not script_path.exists():
        return
    run([sys.executable, str(script_path), str(project_file)], cwd=repo_root)


def iter_texture_assets(project_file: Path, project_data: dict) -> list[tuple[Path, dict]]:
    project_root = project_file.parent
    assets_dirs = project_data.get("assetPaths") or [project_data.get("assets", "Assets")]
    results: list[tuple[Path, dict]] = []

    for relative_assets_dir in assets_dirs:
        assets_dir = project_root / relative_assets_dir
        if not assets_dir.exists():
            continue
        for meta_path in assets_dir.rglob("*.cmeta"):
            try:
                meta = json.loads(meta_path.read_text(encoding="utf-8"))
            except json.JSONDecodeError:
                continue
            if meta.get("type") != "texture":
                continue
            asset_path = meta_path.with_suffix("")
            if asset_path.suffix.lower() not in LDR_EXTS:
                continue
            if not is_embedded_texture_path(asset_path) and not asset_path.exists():
                continue
            results.append((asset_path, meta))

    return results


def stable_hash(value: str) -> str:
    hash_value = FNV64_OFFSET
    for byte in value.encode("utf-8"):
        hash_value ^= byte
        hash_value = (hash_value * FNV64_PRIME) & 0xFFFFFFFFFFFFFFFF
    return f"{hash_value:016x}"


def is_embedded_texture_path(asset_path: Path) -> bool:
    return EMBEDDED_MARKER in asset_path.as_posix()


def normalize_embedded_key(asset_root: Path, asset_path: Path) -> str:
    key = asset_path.as_posix()
    source_path, suffix = key.split(EMBEDDED_MARKER, 1)
    resolved_source = Path(source_path).resolve()
    try:
        normalized_source = resolved_source.relative_to(asset_root.resolve()).as_posix()
    except ValueError:
        normalized_source = resolved_source.as_posix()
    return f"{normalized_source}{EMBEDDED_MARKER}{suffix}"


def expected_embedded_cache_path(project_root: Path, project_data: dict, asset_root: Path, asset_path: Path) -> Path:
    library_dir = project_root / project_data.get("library", "Library")
    return library_dir / "ImportCache" / f"embedded_{stable_hash(normalize_embedded_key(asset_root, asset_path))}_{KTX2_CACHE_VERSION}.ktx2"


def expected_texture_cache_path(project_root: Path, project_data: dict, guid: str) -> Path:
    library_dir = project_root / project_data.get("library", "Library")
    return library_dir / "ImportCache" / f"{guid}_{KTX2_CACHE_VERSION}.ktx2"


def validate_texture_caches(project_file: Path, project_data: dict) -> None:
    project_root = project_file.parent
    missing: list[str] = []
    stale: list[str] = []
    scene_files = list_scene_files(project_root, project_data)
    referenced_textures, referenced_models = collect_scene_asset_refs(scene_files, project_root)

    assets_dirs = project_data.get("assetPaths") or [project_data.get("assets", "Assets")]
    asset_roots = [(project_root / relative_assets_dir).resolve() for relative_assets_dir in assets_dirs]

    for asset_path, meta in iter_texture_assets(project_file, project_data):
        asset_root = next((root for root in asset_roots if str(asset_path.resolve()).startswith(str(root) + "/") or asset_path.resolve() == root), project_root.resolve())
        if is_embedded_texture_path(asset_path):
            source_model_path = Path(asset_path.as_posix().split(EMBEDDED_MARKER, 1)[0])
            try:
                resolved_source_model = source_model_path.resolve()
            except OSError:
                resolved_source_model = source_model_path
            if referenced_models and resolved_source_model not in referenced_models:
                continue
            if not source_model_path.exists():
                missing.append(f"{asset_path.relative_to(project_root).as_posix()} (missing source model)")
                continue
            cache_path = expected_embedded_cache_path(project_root, project_data, asset_root, asset_path)
            if not cache_path.exists():
                missing.append(f"{asset_path.relative_to(project_root).as_posix()} -> {cache_path.relative_to(project_root).as_posix()}")
                continue
            try:
                if cache_path.stat().st_mtime < source_model_path.stat().st_mtime:
                    stale.append(f"{asset_path.relative_to(project_root).as_posix()} -> {cache_path.relative_to(project_root).as_posix()}")
            except OSError:
                stale.append(f"{asset_path.relative_to(project_root).as_posix()} -> {cache_path.relative_to(project_root).as_posix()}")
            continue

        try:
            resolved_asset_path = asset_path.resolve()
        except OSError:
            resolved_asset_path = asset_path
        if referenced_textures and resolved_asset_path not in referenced_textures:
            continue

        guid = str(meta.get("guid") or "").strip()
        if not guid:
            missing.append(f"{asset_path.relative_to(project_root).as_posix()} (missing guid)")
            continue

        cache_path = expected_texture_cache_path(project_root, project_data, guid)
        if not cache_path.exists():
            missing.append(f"{asset_path.relative_to(project_root).as_posix()} -> {cache_path.relative_to(project_root).as_posix()}")
            continue

        try:
            if cache_path.stat().st_mtime < asset_path.stat().st_mtime:
                stale.append(f"{asset_path.relative_to(project_root).as_posix()} -> {cache_path.relative_to(project_root).as_posix()}")
        except OSError:
            stale.append(f"{asset_path.relative_to(project_root).as_posix()} -> {cache_path.relative_to(project_root).as_posix()}")

    problems: list[str] = []
    if missing:
        problems.append("Missing cooked textures:\n" + "\n".join(f"  - {entry}" for entry in missing[:20]))
    if stale:
        problems.append("Stale cooked textures:\n" + "\n".join(f"  - {entry}" for entry in stale[:20]))
    if problems:
        raise RuntimeError("\n".join(problems))


def build_manifest(game_data_dir: Path, project_data: dict, startup_scene: str) -> dict:
    files = []
    for path in sorted(game_data_dir.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(game_data_dir).as_posix()
        files.append(
            {
                "path": rel,
                "size": path.stat().st_size,
                "sha256": sha256_file(path),
            }
        )

    return {
        "version": 1,
        "generatedAtUTC": datetime.now(timezone.utc).isoformat(),
        "projectName": project_data.get("name", ""),
        "productName": project_data.get("productName") or project_data.get("name", "CrescentGame"),
        "bundleIdentifier": project_data.get("bundleIdentifier", "com.crescentengine.game"),
        "buildTarget": project_data.get("buildTarget", "macOS"),
        "startupScene": startup_scene,
        "files": files,
    }


def package_game(repo_root: Path,
                 project_file: Path,
                 configuration: str,
                 output_dir: Path,
                 startup_scene_override: Optional[str],
                 encode_textures: bool) -> Path:
    project_root = project_file.parent
    project_data = load_project(project_file)
    build_target = project_data.get("buildTarget", "macOS")
    if build_target != "macOS":
        raise RuntimeError(f"Only macOS export is supported right now. Current target: {build_target}")

    startup_scene = resolve_startup_scene(project_root, project_data, startup_scene_override)
    validate_startup_scene(project_root, startup_scene)
    scene_files = list_scene_files(project_root, project_data)
    environment_files = collect_environment_refs(scene_files, project_root, project_data)
    product_name = project_data.get("productName") or project_data.get("name") or "CrescentGame"
    bundle_identifier = project_data.get("bundleIdentifier") or "com.crescentengine.game"
    require_cooked_textures = encode_textures or configuration == "Release"

    if require_cooked_textures:
        maybe_encode_textures(repo_root, project_file)
        validate_texture_caches(project_file, project_data)

    with tempfile.TemporaryDirectory(prefix="crescent-build-") as tmp_dir:
        derived_data = Path(tmp_dir) / "DerivedData"
        built_app = build_app(repo_root, configuration, derived_data)
        cooked_environment_root = Path(tmp_dir) / "CookedEnvironment"
        cooked_environment_map = cook_runtime_environments(
            built_app,
            project_file,
            project_data,
            environment_files,
            cooked_environment_root,
        )
        print(f"Cooked environments: {len(cooked_environment_map)}")
        baked_scenes_root = Path(tmp_dir) / "BakedScenes"
        baked_source_scene_map, baked_stats = bake_source_scenes(
            built_app,
            project_file,
            scene_files,
            baked_scenes_root,
        )
        print(
            "Baked lighting: "
            f"{baked_stats['bakedAtlasCount']} atlases, "
            f"{baked_stats['bakedRendererCount']} renderers, "
            f"{baked_stats['bakedLightCount']} baked lights across "
            f"{baked_stats['bakedSceneCount']} scenes."
        )
        cooked_static_lightmaps_root = Path(tmp_dir) / "CookedStaticLightmaps"
        cooked_static_lightmap_map = cook_static_lightmaps(
            built_app,
            repo_root,
            project_file,
            list(baked_source_scene_map.values()),
            cooked_static_lightmaps_root,
        )
        print(f"Cooked static lightmaps: {len(cooked_static_lightmap_map)}")
        cooked_scenes_root = Path(tmp_dir) / "CookedScenes"
        cooked_scene_map = cook_runtime_scenes(
            built_app,
            project_file,
            scene_files,
            baked_source_scene_map,
            cooked_scenes_root,
        )

        output_dir.mkdir(parents=True, exist_ok=True)
        packaged_app = output_dir / f"{product_name}.app"
        if packaged_app.exists():
            shutil.rmtree(packaged_app)
        shutil.copytree(built_app, packaged_app)
        patch_info_plist(packaged_app / "Contents" / "Info.plist", product_name, bundle_identifier)

        resources_dir = packaged_app / "Contents" / "Resources"
        prune_packaged_runtime_resources(resources_dir)
        game_data_dir = resources_dir / "GameData"
        game_data_dir.mkdir(parents=True, exist_ok=True)

        packaged_project_data = dict(project_data)
        packaged_startup_scene = cooked_scene_map.get(startup_scene, startup_scene)
        packaged_project_data["productName"] = product_name
        packaged_project_data["bundleIdentifier"] = bundle_identifier
        packaged_project_data["startupScene"] = packaged_startup_scene
        packaged_project_data["requireCookedTextures"] = require_cooked_textures
        packaged_project_data["textureCookFormat"] = "KTX2_ASTC4x4"
        packaged_project_data["requireCookedScenes"] = bool(cooked_scene_map)
        packaged_project_data["sceneCookFormat"] = "MessagePackExternalMeshV1" if cooked_scene_map else ""
        packaged_project_data["lightingBakeFormat"] = "LightmapRGBMKTX2DirectionalShadowmaskProbeReflections_V18"
        packaged_project_data["requireCookedEnvironmentIBL"] = bool(cooked_environment_map)
        packaged_project_data["environmentCookFormat"] = "CENV_RGBA16F_V1" if cooked_environment_map else ""
        packaged_project_data["cookedScenes"] = cooked_scene_map
        with (game_data_dir / "Project.cproj").open("w", encoding="utf-8") as handle:
            json.dump(packaged_project_data, handle, indent=2)
            handle.write("\n")
        copy_packaged_assets(project_root, project_data, game_data_dir, scene_files)
        copy_packaged_scenes(project_root, project_data, game_data_dir, cooked_scene_map, cooked_scenes_root)
        copy_tree_if_exists(project_root / project_data.get("settings", "Settings"), game_data_dir / "Settings")
        copy_packaged_import_cache(project_root, project_data, game_data_dir)
        copy_tree_if_exists(
            project_root / project_data.get("library", "Library") / "BakedLighting",
            game_data_dir / "Library" / "BakedLighting",
        )
        copy_tree_if_exists(cooked_static_lightmaps_root / "Library" / "ImportCache", game_data_dir / "Library" / "ImportCache")
        copy_tree_if_exists(cooked_environment_root / "Library" / "ImportCache", game_data_dir / "Library" / "ImportCache")

        manifest = build_manifest(game_data_dir, project_data, packaged_startup_scene)
        manifest_path = game_data_dir / "BuildManifest.json"
        with manifest_path.open("w", encoding="utf-8") as handle:
            json.dump(manifest, handle, indent=2)
            handle.write("\n")

    return packaged_app


def main() -> int:
    parser = argparse.ArgumentParser(description="Build and package a CrescentEngine project as a macOS app.")
    parser.add_argument("project", help="Project root or Project.cproj path")
    parser.add_argument("--configuration", default="Debug", choices=["Debug", "Release"])
    parser.add_argument("--output", default="", help="Output directory. Defaults to <Project>/Build/<Configuration>")
    parser.add_argument("--startup-scene", default="", help="Override startup scene relative to project root")
    parser.add_argument("--encode-textures", action="store_true", help="Run texture encoding before packaging")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    project_input = Path(args.project).resolve()
    project_file = find_project_file(project_input)
    if project_file is None:
        print("Project.cproj not found.", file=sys.stderr)
        return 1

    project_root = project_file.parent
    output_dir = Path(args.output).resolve() if args.output else project_root / "Build" / args.configuration

    try:
        packaged_app = package_game(
            repo_root=repo_root,
            project_file=project_file,
            configuration=args.configuration,
            output_dir=output_dir,
            startup_scene_override=args.startup_scene or None,
            encode_textures=args.encode_textures,
        )
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        return 1

    print(f"Packaged app: {packaged_app}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
