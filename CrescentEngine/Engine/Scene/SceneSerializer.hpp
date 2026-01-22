#pragma once

#include "Scene.hpp"
#include <string>
#include <vector>

namespace Crescent {

class SceneSerializer {
public:
    static bool SaveScene(Scene* scene, const std::string& path);
    static bool LoadScene(Scene* scene, const std::string& path);

    static std::string SerializeScene(Scene* scene);
    static std::string SerializeScene(Scene* scene, const std::string& scenePath);
    static std::string SerializeScene(Scene* scene, bool includeEditorOnly);
    static bool DeserializeScene(Scene* scene, const std::string& data);
    static bool DeserializeScene(Scene* scene, const std::string& data, const std::string& scenePath);
    static std::vector<Entity*> DuplicateEntities(Scene* scene, const std::vector<Entity*>& entities);
};

} // namespace Crescent
