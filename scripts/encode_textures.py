#!/usr/bin/env python3
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


LDR_EXTS = {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".gif", ".tif", ".tiff"}


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
    cmd = [
        str(basisu),
        "-ktx2",
        "-ktx2_no_zstandard",
        "-uastc" if normal_map else "-etc1s",
        "-srgb" if srgb else "-linear",
    ]
    if flip_y:
        cmd.append("-y_flip")
    if generate_mips:
        cmd.append("-mipmap")
    cmd += [
        "-quality",
        "50" if normal_map else "80",
        "-effort",
        "4",
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

    basisu = Path(args.basisu) if args.basisu else basisu_path_from_script()
    if not basisu.exists():
        print(f"basisu binary not found: {basisu}", file=sys.stderr)
        return 1

    encoded = 0
    skipped = 0

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
            if asset_path.suffix.lower() not in LDR_EXTS:
                continue
            import_settings = (meta.get("import") or {}).get("texture") or {}
            srgb = bool(import_settings.get("srgb", True))
            normal_map = bool(import_settings.get("normalMap", False))
            flip_y = bool(import_settings.get("flipY", False))
            generate_mips = bool(import_settings.get("generateMipmaps", True))

            output_path = cache_dir / f"{guid}.ktx2"
            if not should_encode(asset_path, output_path):
                skipped += 1
                continue

            cmd = build_command(basisu, asset_path, output_path, srgb, normal_map, flip_y, generate_mips)
            result = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
            if result.returncode != 0:
                print(f"Failed: {asset_path}\n{result.stderr}", file=sys.stderr)
                continue
            encoded += 1

    print(f"Encoded {encoded} texture(s), skipped {skipped}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
