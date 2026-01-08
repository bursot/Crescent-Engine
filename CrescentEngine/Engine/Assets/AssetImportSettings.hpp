#pragma once

namespace Crescent {

struct ModelImportSettings {
    float scale = 1.0f;
    bool flipUVs = false;
    bool onlyLOD0 = false;
    bool mergeStaticMeshes = false;
};

struct TextureImportSettings {
    bool srgb = true;
    bool generateMipmaps = true;
    bool flipY = false;
    int maxSize = 4096;
    bool normalMap = false;
};

struct HdriImportSettings {
    bool flipY = false;
    int maxSize = 2048;
};

} // namespace Crescent
