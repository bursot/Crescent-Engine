#pragma once

#include <memory>
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>
#include "../Math/Math.hpp"
#include "../Scene/SceneSettings.hpp"

// Forward declarations to avoid including metal-cpp in header
namespace MTL {
    class Device;
    class CommandQueue;
    class RenderPipelineState;
    class ComputePipelineState;
    class RenderCommandEncoder;
    class Buffer;
    class Library;
    class DepthStencilState;
    class SamplerState;
    class Texture;
}

namespace CA {
    class MetalLayer;
}

namespace Crescent {

// Forward declarations
class Mesh;
class Material;
class Camera;
class Light;
class MeshRenderer;
class SkinnedMeshRenderer;
class Scene;
class DebugRenderer;
class Texture2D;
class TextureLoader;
class IBLGenerator;
class LightingSystem;
class ShadowRenderPass;
class ClusteredLightingPass;

// GPU Buffer wrapper
struct GPUBuffer {
    void* buffer;  // MTL::Buffer*
    size_t size;
    
    GPUBuffer() : buffer(nullptr), size(0) {}
};

// Pipeline state cache key
struct PipelineStateKey {
    bool hasNormals;
    bool hasTexCoords;
    bool hasTangents;
    bool isTransparent;
    bool isSkinned;
    bool hdrTarget;
    uint8_t sampleCount;
    
    bool operator==(const PipelineStateKey& other) const {
        return hasNormals == other.hasNormals &&
               hasTexCoords == other.hasTexCoords &&
               hasTangents == other.hasTangents &&
               isTransparent == other.isTransparent &&
               isSkinned == other.isSkinned &&
               hdrTarget == other.hdrTarget &&
               sampleCount == other.sampleCount;
    }
};

// Hash function for pipeline key
struct PipelineStateKeyHash {
    size_t operator()(const PipelineStateKey& key) const {
        return (key.hasNormals ? 1 : 0) |
               (key.hasTexCoords ? 2 : 0) |
               (key.hasTangents ? 4 : 0) |
               (key.isTransparent ? 8 : 0) |
               (key.isSkinned ? 16 : 0) |
               (key.hdrTarget ? 32 : 0) |
               (static_cast<size_t>(key.sampleCount) << 7);
    }
};

class Renderer {
public:
    enum class RenderTargetPool {
        Scene,
        Game
    };

    Renderer();
    ~Renderer();
    
    // Initialize the renderer
    bool initialize();
    
    // Set the Metal layer from Swift (as void* to avoid type conflicts)
    void setMetalLayer(void* layer);
    void setMetalLayer(void* layer, bool applySize);
    
    // Shutdown renderer
    void shutdown();
    
    // Render a frame
    void render();
    
    // Render a specific scene
    void renderScene(Scene* scene);
    struct RenderOptions {
        bool allowTemporal = true;
        bool updateHistory = true;
    };
    void renderScene(Scene* scene, Camera* cameraOverride, const RenderOptions& options);

    void setRenderTargetPool(RenderTargetPool pool);
    
    // Get Metal device (return as void* to avoid type conflicts)
    void* getDevice() const;
    
    // Resize viewport
    void resize(float width, float height);
    void setViewportSize(float width, float height, bool updateTargets);
    void applyQualitySettings(const SceneQualitySettings& quality);
    
    // Mesh management
    void uploadMesh(Mesh* mesh);
    void destroyMesh(Mesh* mesh);
    
    // Debug renderer access
    DebugRenderer* getDebugRenderer() { return m_debugRenderer.get(); }
    
    float getViewportWidth() const { return m_viewportWidth; }
    float getViewportHeight() const { return m_viewportHeight; }
    
    TextureLoader* getTextureLoader() const { return m_textureLoader.get(); }
    
    // Quality controls (shadow atlas, anisotropy, etc.)
    
    // Statistics
    struct RenderStats {
        uint32_t drawCalls;
        uint32_t triangles;
        uint32_t vertices;
        float frameTime;
        
        void reset() {
            drawCalls = 0;
            triangles = 0;
            vertices = 0;
            frameTime = 0.0f;
        }
    };
    
    const RenderStats& getStats() const { return m_stats; }

    // Environment controls
    struct EnvironmentSettings {
        float exposureEV = 0.0f;
        float iblIntensity = 1.0f;
        float skyIntensity = 1.0f;
        float saturation = 1.0f;
        float contrast = 1.0f;
        float blurLevel = 0.0f;
        Math::Vector3 tint = Math::Vector3(1.0f);
        Math::Vector3 rotation = Math::Vector3(0.0f); // Degrees
        bool skyboxVisible = true;
        std::string sourcePath = "Builtin Sky";
        int skyMode = 0; // 0 = Procedural, 1 = HDRI
        bool autoSunColor = true;
    };
    
    EnvironmentSettings& getEnvironmentSettings() { return m_environmentSettings; }
    const EnvironmentSettings& getEnvironmentSettings() const { return m_environmentSettings; }
    
    bool loadEnvironmentMap(const std::string& path);
    void resetEnvironment();
    void setEnvironmentRotation(const Math::Vector3& eulerDegrees);
    void setEnvironmentExposure(float ev);
    void setEnvironmentIblIntensity(float intensity);
    void setEnvironmentSkyIntensity(float intensity);
    void setEnvironmentTint(const Math::Vector3& tint);
    void setEnvironmentSaturation(float saturation);
    void setEnvironmentContrast(float contrast);
    void setEnvironmentBlur(float blur);
    void setEnvironmentSkyboxVisible(bool visible);
    void setEnvironmentSkyMode(int mode);
    void setEnvironmentAutoSunColor(bool enabled);
    void setColorGradingLUT(const std::string& path);
    std::string getEnvironmentPath() const { return m_environmentSettings.sourcePath; }

    // Debug toggles (editor-only use)
    void setDebugDrawShadowAtlas(bool enabled);
    void setDebugDrawCascades(bool enabled);
    void setDebugDrawPointFrusta(bool enabled);
    
private:
    void buildPipelines();
    void buildDepthStencilStates();
    void buildDebugPipelines();
    void buildEnvironmentPipeline();
    void buildBlitPipeline();
    void buildPrepassPipeline();
    void buildVelocityPipelines();
    void buildSSAOPipelines();
    void buildSSAONoiseTexture();
    void buildBloomPipelines();
    void buildSSRPipeline();
    void buildDecalPipeline();
    void buildDOFPipeline();
    void buildFogPipeline();
    void buildFogVolumePipeline();
    void buildTAAPipeline();
    void buildMotionBlurPipeline();
    void updateEnvironmentUniforms();
    void renderSkybox(MTL::RenderCommandEncoder* encoder, Camera* camera);
    void rebuildSamplerState(int anisotropy);
    void ensureRenderTargets(uint32_t width, uint32_t height, uint32_t msaaSamples, int colorFormat);
    void ensureFogVolume(uint32_t width, uint32_t height, int quality);
    void clearPipelineCache();
    uint32_t resolveSampleCount(uint32_t requested) const;
    
    MTL::RenderPipelineState* getPipelineState(const PipelineStateKey& key);
    
    void renderMeshRenderer(MeshRenderer* renderer, Camera* camera, const std::vector<Light*>& lights);
    void renderDebugGeometry(Camera* camera);
    
    void setupUniforms(Camera* camera, Light* light);
    
private:
    struct RenderTargetState {
        MTL::Texture* depthTexture = nullptr;
        MTL::Texture* msaaDepthTexture = nullptr;
        MTL::Texture* normalTexture = nullptr;
        MTL::Texture* ssaoTexture = nullptr;
        MTL::Texture* ssaoBlurTexture = nullptr;
        MTL::Texture* ssaoNoiseTexture = nullptr;
        MTL::Texture* velocityTexture = nullptr;
        MTL::Texture* dofTexture = nullptr;
        MTL::Texture* fogTexture = nullptr;
        MTL::Texture* fogVolumeTexture = nullptr;
        MTL::Texture* fogVolumeHistoryTexture = nullptr;
        uint32_t fogVolumeWidth = 0;
        uint32_t fogVolumeHeight = 0;
        uint32_t fogVolumeDepth = 0;
        int fogVolumeQuality = 1;
        bool fogVolumeHistoryValid = false;
        bool fogHistoryInitialized = false;
        Math::Vector3 prevFogCameraPos = Math::Vector3(0.0f);
        Math::Vector3 prevFogCameraForward = Math::Vector3(0.0f, 0.0f, -1.0f);
        Math::Vector3 prevFogSunDir = Math::Vector3(0.0f, -1.0f, 0.0f);
        float prevFogSunIntensity = 0.0f;
        MTL::Texture* postColorTexture = nullptr;
        MTL::Texture* decalAlbedoTexture = nullptr;
        MTL::Texture* decalNormalTexture = nullptr;
        MTL::Texture* decalOrmTexture = nullptr;
        MTL::Texture* motionBlurTexture = nullptr;
        std::vector<MTL::Texture*> bloomMipTextures;
        uint32_t bloomMipCount = 0;
        MTL::Texture* taaHistoryTexture = nullptr;
        MTL::Texture* taaCurrentTexture = nullptr;
        MTL::Texture* colorTexture = nullptr;
        MTL::Texture* msaaColorTexture = nullptr;
        int sceneColorFormat = 0;
        bool taaHistoryValid = false;
        bool motionHistoryValid = false;
        uint32_t taaFrameIndex = 0;
        Math::Matrix4x4 prevViewProjection;
        Math::Matrix4x4 prevViewProjectionNoJitter;
        uint32_t renderTargetWidth = 0;
        uint32_t renderTargetHeight = 0;
        uint32_t msaaSamples = 1;
    };

    RenderTargetState& getRenderTargetState(RenderTargetPool pool);
    void storeRenderTargetState(RenderTargetState& state);
    void loadRenderTargetState(const RenderTargetState& state);
    void releaseRenderTargetState(RenderTargetState& state);
    void invalidateRenderTargetState(RenderTargetState& state, uint32_t msaaSamples);

    // Metal objects
    MTL::Device* m_device;
    MTL::CommandQueue* m_commandQueue;
    MTL::Library* m_library;
    MTL::Library* m_debugLibrary;
    CA::MetalLayer* m_metalLayer;
    
    // Pipeline states (cached)
    std::unordered_map<PipelineStateKey, MTL::RenderPipelineState*, PipelineStateKeyHash> m_pipelineStates;
    
    // Debug pipeline states
    MTL::RenderPipelineState* m_debugLinePipelineState;
    MTL::RenderPipelineState* m_debugGridPipelineState;
    
    // Depth stencil states
    MTL::DepthStencilState* m_depthStencilState;
    MTL::DepthStencilState* m_skyboxDepthState;
    MTL::DepthStencilState* m_depthReadState;

    // Prepass pipeline states
    MTL::RenderPipelineState* m_prepassPipelineState;
    MTL::RenderPipelineState* m_prepassPipelineSkinned;
    MTL::RenderPipelineState* m_velocityPipelineState;
    MTL::RenderPipelineState* m_velocityPipelineSkinned;
    MTL::RenderPipelineState* m_ssaoPipelineState;
    MTL::RenderPipelineState* m_ssaoBlurPipelineState;
    MTL::RenderPipelineState* m_ssrPipelineState;
    MTL::RenderPipelineState* m_decalPipelineState;
    MTL::RenderPipelineState* m_bloomPrefilterPipelineState;
    MTL::RenderPipelineState* m_bloomDownsamplePipelineState;
    MTL::RenderPipelineState* m_bloomUpsamplePipelineState;
    MTL::RenderPipelineState* m_bloomCombinePipelineState;
    MTL::RenderPipelineState* m_taaPipelineState;
    MTL::RenderPipelineState* m_dofPipelineState;
    MTL::RenderPipelineState* m_fogPipelineState;
    MTL::ComputePipelineState* m_fogVolumePipelineState;
    MTL::RenderPipelineState* m_motionBlurPipelineState;
    
    // Depth texture
    MTL::Texture* m_depthTexture;
    MTL::Texture* m_msaaDepthTexture;
    MTL::Texture* m_normalTexture;
    MTL::Texture* m_ssaoTexture;
    MTL::Texture* m_ssaoBlurTexture;
    MTL::Texture* m_ssaoNoiseTexture;
    MTL::Texture* m_velocityTexture;
    MTL::Texture* m_dofTexture;
    MTL::Texture* m_fogTexture;
    MTL::Texture* m_fogVolumeTexture;
    MTL::Texture* m_fogVolumeHistoryTexture;
    uint32_t m_fogVolumeWidth;
    uint32_t m_fogVolumeHeight;
    uint32_t m_fogVolumeDepth;
    int m_fogVolumeQuality;
    bool m_fogVolumeHistoryValid;
    bool m_fogHistoryInitialized;
    Math::Vector3 m_prevFogCameraPos;
    Math::Vector3 m_prevFogCameraForward;
    Math::Vector3 m_prevFogSunDir;
    float m_prevFogSunIntensity;
    MTL::Texture* m_postColorTexture;
    MTL::Texture* m_decalAlbedoTexture;
    MTL::Texture* m_decalNormalTexture;
    MTL::Texture* m_decalOrmTexture;
    MTL::Texture* m_motionBlurTexture;
    std::vector<MTL::Texture*> m_bloomMipTextures;
    uint32_t m_bloomMipCount;
    MTL::Texture* m_taaHistoryTexture;
    MTL::Texture* m_taaCurrentTexture;
    
    // Offscreen color targets
    MTL::Texture* m_colorTexture;
    MTL::Texture* m_msaaColorTexture;
    
    // Uniform buffers
    MTL::Buffer* m_modelUniformBuffer;
    MTL::Buffer* m_cameraUniformBuffer;
    MTL::Buffer* m_materialUniformBuffer;
    MTL::Buffer* m_lightUniformBuffer;
    MTL::Buffer* m_environmentUniformBuffer;
    MTL::Buffer* m_lightGPUBuffer;
    MTL::Buffer* m_shadowGPUBuffer;
    MTL::Buffer* m_lightCountBuffer;
    MTL::Buffer* m_clusterHeaderBuffer;
    MTL::Buffer* m_clusterIndexBuffer;
    MTL::Buffer* m_clusterParamsBuffer;
    MTL::Buffer* m_skinningBuffer;
    MTL::Buffer* m_prevSkinningBuffer;
    size_t m_skinningBufferCapacity;
    size_t m_prevSkinningBufferCapacity;
    
    // Sampling and textures
    MTL::SamplerState* m_samplerState;
    MTL::SamplerState* m_shadowSampler;
    MTL::SamplerState* m_linearClampSampler;
    std::unique_ptr<TextureLoader> m_textureLoader;
    std::shared_ptr<Texture2D> m_defaultWhiteTexture;
    std::shared_ptr<Texture2D> m_defaultNormalTexture;
    std::shared_ptr<Texture2D> m_defaultBlackTexture;
    std::shared_ptr<Texture2D> m_defaultHeightTexture;
    std::shared_ptr<Texture2D> m_defaultEnvironmentTexture;
    std::shared_ptr<Texture2D> m_environmentTexture;
    std::shared_ptr<Texture2D> m_colorGradingLUT;
    std::shared_ptr<Texture2D> m_colorGradingNeutralLUT;
    std::string m_colorGradingLUTPath;
    
    // Environment rendering
    MTL::RenderPipelineState* m_skyboxPipelineState;
    MTL::RenderPipelineState* m_blitPipelineState;
    MTL::Buffer* m_skyboxVertexBuffer;
    MTL::Buffer* m_skyboxIndexBuffer;
    uint32_t m_skyboxIndexCount;
    EnvironmentSettings m_environmentSettings;
    
    // IBL (Image-Based Lighting) system
    std::unique_ptr<IBLGenerator> m_iblGenerator;
    MTL::Texture* m_iblCubemap;           // Environment cubemap
    MTL::Texture* m_iblPrefiltered;       // Prefiltered specular
    MTL::Texture* m_iblIrradiance;        // Irradiance diffuse
    MTL::Texture* m_iblBRDFLUT;           // BRDF lookup table
    bool m_hasIBL;                        // True if IBL textures are ready

    int m_sceneColorFormat;
    bool m_outputHDR;
    Math::Matrix4x4 m_prevViewProjection;
    Math::Matrix4x4 m_prevViewProjectionNoJitter;
    bool m_taaHistoryValid;
    bool m_motionHistoryValid;
    uint32_t m_taaFrameIndex;
    uint32_t m_frameIndex;

    // Active quality settings
    SceneQualitySettings m_qualitySettings;
    uint32_t m_renderTargetWidth;
    uint32_t m_renderTargetHeight;
    uint32_t m_msaaSamples;
    
    // Lighting and shadows
    std::unique_ptr<LightingSystem> m_lightingSystem;
    std::unique_ptr<ShadowRenderPass> m_shadowPass;
    std::unique_ptr<ClusteredLightingPass> m_clusterPass;
    bool m_debugDrawShadowAtlas;
    bool m_debugDrawCascades;
    bool m_debugDrawPointFrusta;
    
    // Debug renderer
    std::unique_ptr<DebugRenderer> m_debugRenderer;
    
    // Viewport dimensions
    float m_viewportWidth;
    float m_viewportHeight;
    
    // Statistics
    RenderStats m_stats;

    bool m_isInitialized;

    RenderTargetState m_sceneTargets;
    RenderTargetState m_gameTargets;
    RenderTargetPool m_activePool;
};

} // namespace Crescent
