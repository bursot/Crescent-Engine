#pragma once

#include "../Math/Vector3.hpp"
#include <string>

namespace Crescent {

struct SceneEnvironmentSettings {
    std::string skyboxPath = "Builtin Sky";
    std::string cookedSkyboxPath;
    int skyMode = 0; // 0 = Procedural, 1 = HDRI
    float exposureEV = 0.0f;
    float iblIntensity = 1.0f;
    float skyIntensity = 1.0f;
    float ambientIntensity = 0.25f;
    float saturation = 1.0f;
    float contrast = 1.0f;
    float blurLevel = 0.0f;
    Math::Vector3 ambientColor = Math::Vector3(1.0f);
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
    int shadowDebugMode = 0; // 0 Off, 1 Raw, 2 Directional, 3 Point, 4 Cascade Index, 5 Point Face
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
    bool taaSpecularStability = true;
    float taaSpecularStabilityStrength = 1.0f;
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
    int upscaler = 0; // 0 = Off, 1 = MetalFX Temporal
};

struct SceneStaticLightingSettings {
    bool enabled = false;
    int mode = 1; // 0 = Disabled, 1 = Full Lightmap Bake, 2 = Shadowmask
    int atlasSize = 2048;
    int maxAtlasCount = 8;
    float texelsPerUnit = 32.0f;
    int samplesPerTexel = 256;
    int indirectBounces = 3;
    bool denoise = true;
    bool bakeDirectLighting = false;
    bool directionalLightmaps = false;
    bool shadowmask = false;
    bool probeVolume = true;
    bool localReflectionProbes = true;
    bool reflectionProbeBoxProjection = true;
    bool reflectionProbeOcclusion = true;
    int probeCountX = 8;
    int probeCountY = 4;
    int probeCountZ = 8;
    int probeSamples = 96;
    float reflectionProbeIntensity = 1.0f;
    float reflectionProbeBlendSharpness = 3.0f;
    float reflectionProbeFilterStrength = 1.0f;
    float specularOcclusionStrength = 1.0f;
    int reflectionProbeMaxBlendCount = 4;
    Math::Vector3 probeBoundsMin = Math::Vector3(-5.0f, 0.0f, -5.0f);
    Math::Vector3 probeBoundsMax = Math::Vector3(5.0f, 5.0f, 5.0f);
    bool autoUnwrap = true;
    int unwrapPadding = 4;
    std::string outputDirectory = "Library/BakedLighting";
    std::string bakeManifestPath;
    std::string probeDataPath;
    std::string lastBakeHash;
};

struct SceneSettings {
    SceneEnvironmentSettings environment;
    SceneFogSettings fog;
    ScenePostProcessSettings postProcess;
    SceneQualitySettings quality;
    SceneStaticLightingSettings staticLighting;
};

} // namespace Crescent
