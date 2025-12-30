#pragma once

#include "../Math/Vector3.hpp"
#include <string>

namespace Crescent {

struct SceneEnvironmentSettings {
    std::string skyboxPath = "Builtin Sky";
    int skyMode = 0; // 0 = Procedural, 1 = HDRI
    float exposureEV = 0.0f;
    float iblIntensity = 1.0f;
    float skyIntensity = 1.0f;
    float saturation = 1.0f;
    float contrast = 1.0f;
    float blurLevel = 0.0f;
    Math::Vector3 tint = Math::Vector3(1.0f);
    Math::Vector3 rotation = Math::Vector3(0.0f);
    bool skyboxVisible = true;
    bool autoSunColor = true;
};

struct SceneFogSettings {
    bool enabled = false;
    Math::Vector3 color = Math::Vector3(0.6f, 0.6f, 0.65f);
    float density = 0.02f;
    float startDistance = 0.0f;
    float endDistance = 200.0f;
    bool heightFog = false;
    float height = 0.0f;
    float heightFalloff = 0.2f;
    float volumetricScattering = 1.0f;
    float volumetricAnisotropy = 0.4f;
    float volumetricHistoryWeight = 0.9f;
    int volumetricQuality = 1; // 0 = Low, 1 = Medium, 2 = High
};

struct ScenePostProcessSettings {
    bool enabled = true;
    bool bloom = false;
    float bloomIntensity = 0.8f;
    float bloomThreshold = 1.0f;
    bool toneMapping = true;
    int toneMappingMode = 1; // 0 = None, 1 = Filmic, 2 = ACES
    bool colorGrading = false;
    float colorGradingIntensity = 1.0f;
    std::string colorGradingLUT;
    float saturation = 1.0f;
    float contrast = 1.0f;
    bool vignette = false;
    float vignetteIntensity = 0.3f;
    bool filmGrain = false;
    float filmGrainIntensity = 0.15f;
    bool ssao = false;
    float ssaoRadius = 0.5f;
    float ssaoStrength = 1.0f;
    bool ssr = false;
    float ssrMaxRoughness = 0.6f;
    float ssrThickness = 0.1f;
    bool taa = false;
    float taaSharpness = 0.5f;
    bool fxaa = false;
    bool motionBlur = false;
    float motionBlurStrength = 0.5f;
    bool depthOfField = false;
    float dofFocusDistance = 5.0f;
    float dofAperture = 2.8f;
};

struct SceneQualitySettings {
    bool overrideProject = false;
    int shadowQuality = 2;
    int shadowResolution = 1024;
    int msaaSamples = 1;
    int anisotropy = 8;
    float renderScale = 1.0f;
    float lodBias = 0.0f;
    int textureQuality = 2;
};

struct SceneSettings {
    SceneEnvironmentSettings environment;
    SceneFogSettings fog;
    ScenePostProcessSettings postProcess;
    SceneQualitySettings quality;
};

} // namespace Crescent
