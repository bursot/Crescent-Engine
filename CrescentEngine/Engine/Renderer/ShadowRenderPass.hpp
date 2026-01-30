#pragma once

#include "../Math/Math.hpp"
#include "LightingSystem.hpp"
#include <vector>
#include <memory>
#include <cstddef>
#include <unordered_set>
#include <string>

namespace MTL {
    class Device;
    class CommandBuffer;
    class RenderCommandEncoder;
    class Texture;
    class Buffer;
    class DepthStencilState;
    class RenderPipelineState;
    class ComputePipelineState;
    class SamplerState;
}

namespace Crescent {

class Scene;
class Camera;
class Mesh;
class Material;

struct InstancedShadowDraw {
    Mesh* mesh = nullptr;
    MTL::Buffer* instanceBuffer = nullptr;
    size_t instanceOffset = 0;
    uint32_t instanceCount = 0;
    std::shared_ptr<Material> material;
    Math::Vector3 boundsCenter = Math::Vector3::Zero;
    Math::Vector3 boundsSize = Math::Vector3::Zero;
    bool isBillboard = false;
};

// Handles rendering shadow maps into atlas textures using LightingSystem prepared data.
class ShadowRenderPass {
public:
    ShadowRenderPass();
    ~ShadowRenderPass();
    
    bool initialize(MTL::Device* device, uint32_t atlasResolution, uint32_t atlasLayers = 1);
    void shutdown();
    
    // Runs shadow rendering for the frame. Expects LightingSystem::beginFrame already called.
    void execute(MTL::CommandBuffer* cmdBuffer,
                 Scene* scene,
                 Camera* camera,
                 const LightingSystem& lighting,
                 const std::vector<InstancedShadowDraw>& instancedDraws);

    void setExtraHiddenEntities(const std::unordered_set<std::string>& hidden);
    
    // Atlas texture exposed to main renderer for sampling.
    MTL::Texture* getShadowAtlas() const { return m_shadowAtlas; }
    const std::vector<MTL::Texture*>& getPointCubeTextures() const { return m_pointCubeTextures; }
    
private:
    void buildPipelines();
    void buildDepthState();
    bool allocateSkinningSlice(size_t bytes, size_t& outOffset);
    void renderDirectional(MTL::CommandBuffer* cmdBuffer, Scene* scene, const LightingSystem& lighting);
    void renderLocal(MTL::CommandBuffer* cmdBuffer, Scene* scene, const LightingSystem& lighting);
    void renderPointCubes(MTL::CommandBuffer* cmdBuffer, Scene* scene, const LightingSystem& lighting);
    void renderInstancedRange(MTL::CommandBuffer* cmdBuffer,
                              const ShadowGPUData& shadow,
                              const ShadowAtlasTile& tile,
                              MTL::RenderPipelineState* pipeline,
                              MTL::RenderPipelineState* pipelineCutout,
                              const std::vector<InstancedShadowDraw>& instancedDraws);
    void renderInstancedCubeFace(MTL::CommandBuffer* cmdBuffer,
                                 MTL::Texture* target,
                                 uint32_t slice,
                                 uint32_t resolution,
                                 const Math::Matrix4x4& viewProj,
                                 MTL::RenderPipelineState* pipeline,
                                 MTL::RenderPipelineState* pipelineCutout,
                                 const std::vector<InstancedShadowDraw>& instancedDraws);
    
    void renderLightRange(MTL::CommandBuffer* cmdBuffer,
                          Scene* scene,
                          const ShadowGPUData& shadow,
                          const ShadowAtlasTile& tile,
                          MTL::RenderPipelineState* pipeline,
                          MTL::RenderPipelineState* pipelineSkinned,
                          MTL::RenderPipelineState* pipelineCutout,
                          MTL::RenderPipelineState* pipelineSkinnedCutout);
    bool shouldSkipEntity(Entity* entity) const;
    
private:
    MTL::Device* m_device;
    MTL::Texture* m_shadowAtlas;
    std::vector<MTL::Texture*> m_pointCubeTextures; // indexed by resolution tier
    MTL::DepthStencilState* m_depthState;
    MTL::RenderPipelineState* m_dirPipeline;
    MTL::RenderPipelineState* m_spotPipeline;
    MTL::RenderPipelineState* m_pointPipeline;
    MTL::RenderPipelineState* m_areaPipeline;
    MTL::RenderPipelineState* m_dirPipelineCutout;
    MTL::RenderPipelineState* m_spotPipelineCutout;
    MTL::RenderPipelineState* m_pointPipelineCutout;
    MTL::RenderPipelineState* m_areaPipelineCutout;
    MTL::RenderPipelineState* m_dirPipelineSkinned;
    MTL::RenderPipelineState* m_spotPipelineSkinned;
    MTL::RenderPipelineState* m_pointPipelineSkinned;
    MTL::RenderPipelineState* m_areaPipelineSkinned;
    MTL::RenderPipelineState* m_dirPipelineSkinnedCutout;
    MTL::RenderPipelineState* m_spotPipelineSkinnedCutout;
    MTL::RenderPipelineState* m_pointPipelineSkinnedCutout;
    MTL::RenderPipelineState* m_areaPipelineSkinnedCutout;
    MTL::RenderPipelineState* m_dirPipelineInstanced;
    MTL::RenderPipelineState* m_spotPipelineInstanced;
    MTL::RenderPipelineState* m_pointPipelineInstanced;
    MTL::RenderPipelineState* m_areaPipelineInstanced;
    MTL::RenderPipelineState* m_dirPipelineInstancedCutout;
    MTL::RenderPipelineState* m_spotPipelineInstancedCutout;
    MTL::RenderPipelineState* m_pointPipelineInstancedCutout;
    MTL::RenderPipelineState* m_areaPipelineInstancedCutout;
    MTL::ComputePipelineState* m_instanceCullPipeline;
    MTL::ComputePipelineState* m_instanceIndirectPipeline;
    MTL::Buffer* m_instanceCullBuffer;
    MTL::Buffer* m_instanceCountBuffer;
    MTL::Buffer* m_instanceIndirectBuffer;
    size_t m_instanceCullCapacity;
    size_t m_instanceCountCapacity;
    size_t m_instanceIndirectCapacity;
    MTL::Buffer* m_skinningBuffer;
    MTL::SamplerState* m_alphaSampler;
    size_t m_skinningBufferCapacity;
    size_t m_skinningBufferOffset;

    std::unordered_set<std::string> m_hlodHidden;
    std::unordered_set<std::string> m_hlodActiveProxies;
    std::unordered_set<std::string> m_extraHidden;
    
    uint32_t m_atlasResolution;
    uint32_t m_atlasLayers;

    Math::Vector3 m_cameraPosition;
    float m_timeSeconds;
};

} // namespace Crescent
