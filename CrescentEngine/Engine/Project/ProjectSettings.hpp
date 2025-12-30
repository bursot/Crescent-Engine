#pragma once

#include "../Scene/SceneSettings.hpp"
#include <string>
#include <vector>

namespace Crescent {

struct ProjectSettings {
    std::string defaultRenderProfile = "High";
    std::string buildTarget = "macOS";
    std::vector<std::string> assetPaths = {"Assets"};
    
    struct RenderProfile {
        std::string name = "High";
        SceneQualitySettings quality;
    };
    
    struct QualityPreset {
        std::string name = "High";
        SceneQualitySettings quality;
    };
    
    struct InputBinding {
        std::string action;
        std::string key;
        std::string mouseButton;
        float scale = 1.0f;
        bool invert = false;
    };
    
    std::vector<RenderProfile> renderProfiles;
    std::vector<QualityPreset> qualityPresets;
    std::vector<InputBinding> inputBindings;
};

} // namespace Crescent
