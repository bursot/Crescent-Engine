#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS_DIR="$ROOT_DIR/ThirdParty"

JOLT_TAG="v5.5.0"
ASSIMP_TAG="v5.4.3"
BASISU_TAG="master"
ASTC_TAG="main"

clone_repo() {
  local url="$1"
  local dir="$2"
  local tag="$3"

  if [ -d "$dir/.git" ]; then
    git -C "$dir" fetch --tags --prune
    git -C "$dir" checkout "$tag"
  else
    git clone --depth 1 --branch "$tag" "$url" "$dir"
  fi
}

build_cmake() {
  local src="$1"
  local build="$2"
  local config="$3"
  shift 3

  cmake -S "$src" -B "$build" -DCMAKE_BUILD_TYPE="$config" "$@"
  cmake --build "$build" --config "$config" --parallel
}

mkdir -p "$DEPS_DIR"

clone_repo "https://github.com/jrouwe/JoltPhysics.git" "$DEPS_DIR/JoltPhysics" "$JOLT_TAG"
clone_repo "https://github.com/assimp/assimp.git" "$DEPS_DIR/assimp" "$ASSIMP_TAG"
clone_repo "https://github.com/BinomialLLC/basis_universal.git" "$DEPS_DIR/basisu" "$BASISU_TAG"
clone_repo "https://github.com/ARM-software/astc-encoder.git" "$DEPS_DIR/astc-encoder" "$ASTC_TAG"

build_cmake "$DEPS_DIR/JoltPhysics/Build" "$DEPS_DIR/jolt-build-debug" Debug \
  -DJPH_BUILD_SHARED_LIBRARY=OFF \
  -DJPH_BUILD_EXAMPLES=OFF \
  -DJPH_BUILD_TESTS=OFF

build_cmake "$DEPS_DIR/JoltPhysics/Build" "$DEPS_DIR/jolt-build-release" Release \
  -DJPH_BUILD_SHARED_LIBRARY=OFF \
  -DJPH_BUILD_EXAMPLES=OFF \
  -DJPH_BUILD_TESTS=OFF

build_cmake "$DEPS_DIR/assimp" "$DEPS_DIR/assimp-build-debug" Debug \
  -DASSIMP_BUILD_ASSIMP_TOOLS=OFF \
  -DASSIMP_BUILD_TESTS=OFF \
  -DASSIMP_BUILD_ZLIB=OFF \
  -DASSIMP_INJECT_DEBUG_POSTFIX=OFF \
  -DBUILD_SHARED_LIBS=OFF

build_cmake "$DEPS_DIR/assimp" "$DEPS_DIR/assimp-build-release" Release \
  -DASSIMP_BUILD_ASSIMP_TOOLS=OFF \
  -DASSIMP_BUILD_TESTS=OFF \
  -DASSIMP_BUILD_ZLIB=OFF \
  -DASSIMP_INJECT_DEBUG_POSTFIX=OFF \
  -DBUILD_SHARED_LIBS=OFF

ASSIMP_CONFIG_SRC="$DEPS_DIR/assimp-build-release/include/assimp/config.h"
ASSIMP_CONFIG_DST="$DEPS_DIR/assimp/include/assimp/config.h"
if [ -f "$ASSIMP_CONFIG_SRC" ]; then
  cp "$ASSIMP_CONFIG_SRC" "$ASSIMP_CONFIG_DST"
fi

echo "Dependencies are ready."
