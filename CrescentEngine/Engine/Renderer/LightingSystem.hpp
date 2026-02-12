#pragma once

#include "../Math/Math.hpp"
#include "../Components/Light.hpp"
#include <vector>
#include <cstdint>
#include <array>

namespace Crescent {

class Scene;
class Camera;
class DebugRenderer;
class Entity;

// GPU-friendly packed light data
struct LightGPUData {
    Math::Vector4 positionRange;   // xyz = view-space position (dir uses world center), w = invRange
    Math::Vector4 directionType;   // xyz = view-space direction, w = type (int)
    Math::Vector4 colorIntensity;  // rgb = linear color, w = intensity
    Math::Vector4 misc;            // x = cosInner, y = cosOuter, z = areaX, w = areaY
    Math::Vector4 shadowCookie;    // x = shadowIdx, y = cookieIdx, z = iesIdx, w = flags (soft/contact/volumetric)
};

// GPU shadow entry per cascade/light
struct ShadowGPUData {
    Math::Matrix4x4 viewProj;
    Math::Vector4 atlasUV;   // xy = offset (0-1), zw = scale (0-1)
    Math::Vector4 params;    // x = bias, y = normalBias, z = penumbra, w = type (0=cascade idx, 1=local, 2=point face, 3=point cube)
    Math::Vector4 depthRange; // x = near, y = far, z = cubeIndex or texelWorldSize, w = atlas layer or tier index
};

struct ShadowAtlasTile {
    bool valid = false;
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t size = 0;
    uint32_t layer = 0;
};

struct CascadedSlice {
    Math::Matrix4x4 view;
    Math::Matrix4x4 proj;
    Math::Matrix4x4 viewProj;
    ShadowAtlasTile atlas;
    Light* owner = nullptr;
    uint8_t cascadeIndex = 0;
    uint32_t resolution = 1024;
    float texelWorldSize = 0.0f;
    float depthSpan = 0.0f;
    float splitNear = 0.0f;
    float splitFar = 0.0f;
};

struct PreparedLight {
    Light* light = nullptr;
    Math::Vector3 positionWS;
    Math::Vector3 directionWS;
    Math::Vector3 positionVS;
    Math::Vector3 directionVS;
    float range = 0.0f;
    uint32_t shadowStart = UINT32_MAX;
    uint32_t shadowCount = 0;
};

// Simple frame-reset atlas allocator. Easily replaceable with bin-pack.
class ShadowAtlas {
public:
    ShadowAtlas(uint32_t resolution = 4096, uint32_t layers = 1);
    
    void reset();
    ShadowAtlasTile allocate(uint32_t size);
    uint32_t getResolution() const { return m_resolution; }
    uint32_t getLayerCount() const { return m_layers; }
    const std::vector<ShadowAtlasTile>& getTiles() const { return m_tiles; }
    
private:
    uint32_t m_resolution;
    uint32_t m_layers;
    uint32_t m_cursorX;
    uint32_t m_cursorY;
    uint32_t m_rowHeight;
    std::vector<ShadowAtlasTile> m_tiles;
};

class LightingSystem {
public:
    LightingSystem();
    
    void configureShadowAtlas(uint32_t resolution, uint32_t layers = 1);
    
    void beginFrame(Scene* scene, Camera* camera, uint32_t viewportWidth, uint32_t viewportHeight);
    
    // Accessors
    const std::vector<LightGPUData>& getGPULights() const { return m_gpuLights; }
    const std::vector<ShadowGPUData>& getGPUShadows() const { return m_gpuShadows; }
    const std::vector<CascadedSlice>& getCascades() const { return m_cascades; }
    const ShadowAtlas& getShadowAtlas() const { return m_shadowAtlas; }
    uint32_t getVisibleLightCount() const { return static_cast<uint32_t>(m_preparedLights.size()); }
    const std::vector<PreparedLight>& getPreparedLights() const { return m_preparedLights; }
    const std::array<uint32_t, 4>& getPointCubeCounts() const { return m_pointCubeCounts; }
    
    // Editor gizmos
    void buildLightGizmos(DebugRenderer& debug, bool drawCascades = true) const;
    void setDebugDrawAtlas(bool enabled) { m_debugDrawAtlas = enabled; }
    bool getDebugDrawAtlas() const { return m_debugDrawAtlas; }
    
private:
    void gatherLights(Scene* scene, Camera* camera);
    void buildDirectionalCascades(const PreparedLight& light, Camera* camera);
    void allocateShadows();
    void fillGPUBuffers();
    
    // Helpers
    std::array<Math::Vector3, 8> computeFrustumCornersWS(Camera* camera, float nearPlane, float farPlane) const;
    void drawSpotGizmo(DebugRenderer& debug, const PreparedLight& light) const;
    void drawAreaGizmo(DebugRenderer& debug, const PreparedLight& light) const;
    
private:
    ShadowAtlas m_shadowAtlas;
    uint32_t m_viewportWidth;
    uint32_t m_viewportHeight;
    bool m_debugDrawAtlas;
    std::array<uint32_t, 4> m_pointCubeCounts;
    
    std::vector<PreparedLight> m_preparedLights;
    std::vector<CascadedSlice> m_cascades;
    std::vector<LightGPUData> m_gpuLights;
    std::vector<ShadowGPUData> m_gpuShadows;
};

} // namespace Crescent
