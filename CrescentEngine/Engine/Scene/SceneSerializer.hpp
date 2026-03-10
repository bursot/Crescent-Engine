#pragma once

#include "Scene.hpp"
#include <string>
#include <vector>

namespace Crescent {

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
};

} // namespace Crescent
