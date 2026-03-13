#pragma once

#include "Scene.hpp"
#include <string>
#include <vector>

namespace Crescent {

struct StaticLightingAtlasRecord {
    int index = -1;
    int width = 0;
    int height = 0;
    int rendererCount = 0;
    std::string lightmapPath;
    std::string directionalLightmapPath;
    std::string shadowmaskPath;
    std::string expectedLightmapPath;
    std::string expectedDirectionalLightmapPath;
    std::string expectedShadowmaskPath;
};

struct StaticLightingRendererRecord {
    std::string entityUUID;
    std::string entityName;
    bool staticGeometry = false;
    bool contributeGI = true;
    bool receiveGI = true;
    int lightmapIndex = -1;
    int lightmapUVChannel = 1;
    Math::Vector4 lightmapScaleOffset = Math::Vector4(1.0f, 1.0f, 0.0f, 0.0f);
    std::string lightmapPath;
    std::string directionalLightmapPath;
    std::string shadowmaskPath;
};

struct StaticLightingManifest {
    int version = 1;
    std::string sceneName;
    std::string scenePath;
    std::string generatedAtUTC;
    std::string bakeHash;
    SceneStaticLightingSettings settings;
    std::vector<StaticLightingAtlasRecord> atlases;
    std::vector<StaticLightingRendererRecord> renderers;
};

class SceneSerializer {
public:
    static bool SaveScene(Scene* scene, const std::string& path);
    static bool SaveCookedRuntimeScene(Scene* scene, const std::string& path, bool includeEditorOnly = false);
    static bool LoadScene(Scene* scene, const std::string& path);

    static std::string SerializeScene(Scene* scene);
    static std::string SerializeScene(Scene* scene, const std::string& scenePath);
    static std::string SerializeScene(Scene* scene, bool includeEditorOnly);
    static std::string SerializeCookedRuntimeScene(Scene* scene, bool includeEditorOnly = false);
    static std::vector<uint8_t> SerializeCookedRuntimeSceneBinary(Scene* scene, bool includeEditorOnly = false);
    static bool DeserializeScene(Scene* scene, const std::string& data);
    static bool DeserializeScene(Scene* scene, const std::string& data, const std::string& scenePath);
    static bool DeserializeSceneBinary(Scene* scene, const std::vector<uint8_t>& data, const std::string& scenePath = "");
    static std::vector<Entity*> DuplicateEntities(Scene* scene, const std::vector<Entity*>& entities);
    static std::string ResolveStaticLightingManifestPath(Scene* scene, const std::string& scenePath = "");
    static StaticLightingManifest BuildStaticLightingManifest(Scene* scene, const std::string& scenePath = "");
    static bool SaveStaticLightingManifest(Scene* scene, const std::string& scenePath = "");
    static bool LoadStaticLightingManifest(const std::string& manifestPath, StaticLightingManifest& outManifest);
};

} // namespace Crescent
