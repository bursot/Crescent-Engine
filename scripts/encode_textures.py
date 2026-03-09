#!/usr/bin/env python3
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


LDR_EXTS = {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".gif", ".tif", ".tiff"}
KTX2_CACHE_VERSION = "v2a"
EMBEDDED_MARKER = "#embedded:"
FNV64_OFFSET = 14695981039346656037
FNV64_PRIME = 1099511628211


def find_project_file(root: Path) -> Path:
    if root.is_file() and root.name.lower().endswith(".cproj"):
        return root
    candidate = root / "Project.cproj"
    if candidate.exists():
        return candidate
    return Path()


def load_project(project_file: Path) -> dict:
    with project_file.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    return data


def resolve_paths(project_file: Path, data: dict) -> tuple[list[Path], Path]:
    root = project_file.parent
    assets = data.get("assetPaths") or ["Assets"]
    assets_dirs = [root / p for p in assets]
    library_dir = root / data.get("library", "Library")
    return assets_dirs, library_dir


def basisu_path_from_script() -> Path:
    script_dir = Path(__file__).resolve().parent
    return script_dir.parent / "ThirdParty" / "basisu" / "basisu"


def embedded_extractor_binary_from_script() -> Path:
    script_dir = Path(__file__).resolve().parent
    return script_dir / ".build" / "extract_embedded_textures"


def stable_hash(value: str) -> str:
    hash_value = FNV64_OFFSET
    for byte in value.encode("utf-8"):
        hash_value ^= byte
        hash_value = (hash_value * FNV64_PRIME) & 0xFFFFFFFFFFFFFFFF
    return f"{hash_value:016x}"


def normalize_embedded_key(asset_root: Path, asset_path: Path) -> str:
    key = asset_path.as_posix()
    source_path, suffix = key.split(EMBEDDED_MARKER, 1)
    resolved_source = Path(source_path).resolve()
    try:
        normalized_source = resolved_source.relative_to(asset_root.resolve()).as_posix()
    except ValueError:
        normalized_source = resolved_source.as_posix()
    return f"{normalized_source}{EMBEDDED_MARKER}{suffix}"


def embedded_cache_output_path(asset_root: Path, cache_dir: Path, asset_path: Path) -> Path:
    return cache_dir / f"embedded_{stable_hash(normalize_embedded_key(asset_root, asset_path))}_{KTX2_CACHE_VERSION}.ktx2"


def embedded_source_output_path(asset_root: Path, embedded_sources_dir: Path, asset_path: Path) -> Path:
    suffix = asset_path.suffix.lower()
    source_ext = suffix if suffix in LDR_EXTS else ".png"
    return embedded_sources_dir / f"embedded_{stable_hash(normalize_embedded_key(asset_root, asset_path))}{source_ext}"


def build_embedded_extractor(script_dir: Path) -> Path:
    binary_path = embedded_extractor_binary_from_script()
    source_path = script_dir / "extract_embedded_textures.cpp"
    if binary_path.exists() and binary_path.stat().st_mtime >= source_path.stat().st_mtime:
        return binary_path

    binary_path.parent.mkdir(parents=True, exist_ok=True)
    repo_root = script_dir.parent
    cmd = [
        "clang++",
        "-std=c++20",
        "-O2",
        "-I",
        str(repo_root / "ThirdParty" / "assimp" / "include"),
        str(source_path),
        str(repo_root / "ThirdParty" / "assimp-build-release" / "lib" / "libassimp.a"),
        "-lz",
        "-o",
        str(binary_path),
    ]
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"Failed to build embedded extractor:\n{result.stderr}")
    return binary_path


def ensure_embedded_sources(model_path: Path, embedded_sources_dir: Path, script_dir: Path, asset_root: Path) -> None:
    extractor = build_embedded_extractor(script_dir)
    embedded_sources_dir.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(
        [str(extractor), str(model_path), str(embedded_sources_dir), str(asset_root)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(f"Failed to extract embedded textures for {model_path}:\n{result.stderr}")


def should_encode(asset_path: Path, output_path: Path) -> bool:
    if not output_path.exists():
        return True
    try:
        return output_path.stat().st_mtime < asset_path.stat().st_mtime
    except OSError:
        return True


def build_command(basisu: Path,
                  asset_path: Path,
                  output_path: Path,
                  srgb: bool,
                  normal_map: bool,
                  flip_y: bool,
                  generate_mips: bool) -> list[str]:
    use_uastc = normal_map
    cmd = [
        str(basisu),
        "-ktx2",
        "-ktx2_no_zstandard",
        "-uastc" if use_uastc else "-etc1s",
        "-srgb" if srgb else "-linear",
    ]
    if flip_y:
        cmd.append("-y_flip")
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


def main() -> int:
    parser = argparse.ArgumentParser(description="Encode CrescentEngine textures to KTX2 (ASTC-ready).")
    parser.add_argument("project", nargs="?", default=".", help="Project root or Project.cproj path")
    parser.add_argument("--basisu", default=os.environ.get("CRESCENT_BASISU_PATH", ""), help="Path to basisu binary")
    args = parser.parse_args()

    project_arg = Path(args.project).resolve()
    project_file = find_project_file(project_arg)
    if not project_file:
        print("Project.cproj not found. Pass project root or Project.cproj path.", file=sys.stderr)
        return 1

    data = load_project(project_file)
    assets_dirs, library_dir = resolve_paths(project_file, data)
    cache_dir = library_dir / "ImportCache"
    cache_dir.mkdir(parents=True, exist_ok=True)
    embedded_sources_dir = cache_dir / "EmbeddedSources"
    script_dir = Path(__file__).resolve().parent

    basisu = Path(args.basisu) if args.basisu else basisu_path_from_script()
    if not basisu.exists():
        print(f"basisu binary not found: {basisu}", file=sys.stderr)
        return 1

    encoded = 0
    skipped = 0
    extracted_models: set[Path] = set()

    for assets_dir in assets_dirs:
        if not assets_dir.exists():
            continue
        for meta_path in assets_dir.rglob("*.cmeta"):
            try:
                meta = json.loads(meta_path.read_text(encoding="utf-8"))
            except json.JSONDecodeError:
                continue
            if meta.get("type") != "texture":
                continue
            guid = meta.get("guid")
            if not guid:
                continue
            asset_path = meta_path.with_suffix("")
            import_settings = (meta.get("import") or {}).get("texture") or {}
            srgb = bool(import_settings.get("srgb", True))
            normal_map = bool(import_settings.get("normalMap", False))
            flip_y = bool(import_settings.get("flipY", False))
            generate_mips = bool(import_settings.get("generateMipmaps", True))

            source_path = asset_path
            if EMBEDDED_MARKER in asset_path.as_posix():
                output_path = embedded_cache_output_path(assets_dir, cache_dir, asset_path)
                model_source = Path(asset_path.as_posix().split(EMBEDDED_MARKER, 1)[0])
                if not model_source.exists():
                    continue
                if not should_encode(model_source, output_path):
                    skipped += 1
                    continue
                if model_source not in extracted_models:
                    ensure_embedded_sources(model_source, embedded_sources_dir, script_dir, assets_dir)
                    extracted_models.add(model_source)
                source_path = embedded_source_output_path(assets_dir, embedded_sources_dir, asset_path)
                if not source_path.exists():
                    print(f"Missing extracted embedded source: {source_path}", file=sys.stderr)
                    continue
            else:
                if asset_path.suffix.lower() not in LDR_EXTS:
                    continue
                if not asset_path.exists():
                    continue
                output_path = cache_dir / f"{guid}_{KTX2_CACHE_VERSION}.ktx2"
                if not should_encode(asset_path, output_path):
                    skipped += 1
                    continue

            cmd = build_command(basisu, source_path, output_path, srgb, normal_map, flip_y, generate_mips)
            result = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
            if result.returncode != 0:
                print(f"Failed: {asset_path}\n{result.stderr}", file=sys.stderr)
                continue
            encoded += 1

    print(f"Encoded {encoded} texture(s), skipped {skipped}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
