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


def find_project_file(root: Path) -> Path:
    if root.is_file() and root.suffix.lower() == ".cproj":
        return root
    candidate = root / "Project.cproj"
    return candidate if candidate.exists() else Path()


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


def patch_info_plist(info_plist_path: Path, product_name: str, bundle_identifier: str) -> None:
    with info_plist_path.open("rb") as handle:
        data = plistlib.load(handle)
    data["CFBundleDisplayName"] = product_name
    data["CFBundleName"] = product_name
    data["CFBundleIdentifier"] = bundle_identifier
    with info_plist_path.open("wb") as handle:
        plistlib.dump(data, handle)


def maybe_encode_textures(repo_root: Path, project_file: Path) -> None:
    script_path = repo_root / "scripts" / "encode_textures.py"
    if not script_path.exists():
        return
    run([sys.executable, str(script_path), str(project_file)], cwd=repo_root)


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
    product_name = project_data.get("productName") or project_data.get("name") or "CrescentGame"
    bundle_identifier = project_data.get("bundleIdentifier") or "com.crescentengine.game"

    if encode_textures:
        maybe_encode_textures(repo_root, project_file)

    with tempfile.TemporaryDirectory(prefix="crescent-build-") as tmp_dir:
        derived_data = Path(tmp_dir) / "DerivedData"
        built_app = build_app(repo_root, configuration, derived_data)

        output_dir.mkdir(parents=True, exist_ok=True)
        packaged_app = output_dir / f"{product_name}.app"
        if packaged_app.exists():
            shutil.rmtree(packaged_app)
        shutil.copytree(built_app, packaged_app)

    patch_info_plist(packaged_app / "Contents" / "Info.plist", product_name, bundle_identifier)

    resources_dir = packaged_app / "Contents" / "Resources"
    game_data_dir = resources_dir / "GameData"
    game_data_dir.mkdir(parents=True, exist_ok=True)

    packaged_project_data = dict(project_data)
    packaged_project_data["productName"] = product_name
    packaged_project_data["bundleIdentifier"] = bundle_identifier
    packaged_project_data["startupScene"] = startup_scene
    with (game_data_dir / "Project.cproj").open("w", encoding="utf-8") as handle:
        json.dump(packaged_project_data, handle, indent=2)
        handle.write("\n")
    copy_tree_if_exists(project_root / project_data.get("assets", "Assets"), game_data_dir / "Assets")
    copy_tree_if_exists(project_root / project_data.get("scenes", "Scenes"), game_data_dir / "Scenes")
    copy_tree_if_exists(project_root / project_data.get("settings", "Settings"), game_data_dir / "Settings")
    copy_tree_if_exists(project_root / project_data.get("library", "Library") / "ImportCache",
                        game_data_dir / "Library" / "ImportCache")

    manifest = build_manifest(game_data_dir, project_data, startup_scene)
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
    if not project_file:
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
