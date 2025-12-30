#pragma once

#include "../Math/Math.hpp"
#include "LightingSystem.hpp"
#include <vector>
#include <memory>
#include <cstddef>

namespace MTL {
    class Device;
    class CommandBuffer;
    class RenderCommandEncoder;
    class Texture;
    class Buffer;
    class DepthStencilState;
    class RenderPipelineState;
}

namespace Crescent {

class Scene;
class Camera;

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
                 const LightingSystem& lighting);
    
    // Atlas texture exposed to main renderer for sampling.
    MTL::Texture* getShadowAtlas() const { return m_shadowAtlas; }
    const std::vector<MTL::Texture*>& getPointCubeTextures() const { return m_pointCubeTextures; }
    
private:
    void buildPipelines();
    void buildDepthState();
    void renderDirectional(MTL::CommandBuffer* cmdBuffer, Scene* scene, const LightingSystem& lighting);
    void renderLocal(MTL::CommandBuffer* cmdBuffer, Scene* scene, const LightingSystem& lighting);
    void renderPointCubes(MTL::CommandBuffer* cmdBuffer, Scene* scene, const LightingSystem& lighting);
    
    void renderLightRange(MTL::CommandBuffer* cmdBuffer,
                          Scene* scene,
                          const ShadowGPUData& shadow,
                          const ShadowAtlasTile& tile,
                          MTL::RenderPipelineState* pipeline,
                          MTL::RenderPipelineState* pipelineSkinned);
    
private:
    MTL::Device* m_device;
    MTL::Texture* m_shadowAtlas;
    std::vector<MTL::Texture*> m_pointCubeTextures; // indexed by resolution tier
    MTL::DepthStencilState* m_depthState;
    MTL::RenderPipelineState* m_dirPipeline;
    MTL::RenderPipelineState* m_spotPipeline;
    MTL::RenderPipelineState* m_pointPipeline;
    MTL::RenderPipelineState* m_areaPipeline;
    MTL::RenderPipelineState* m_dirPipelineSkinned;
    MTL::RenderPipelineState* m_spotPipelineSkinned;
    MTL::RenderPipelineState* m_pointPipelineSkinned;
    MTL::RenderPipelineState* m_areaPipelineSkinned;
    MTL::Buffer* m_skinningBuffer;
    size_t m_skinningBufferCapacity;
    
    uint32_t m_atlasResolution;
    uint32_t m_atlasLayers;
};

} // namespace Crescent
