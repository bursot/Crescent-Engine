#pragma once

#include "Scene.hpp"
#include <string>

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
};

} // namespace Crescent
