#pragma once

#include "../Math/Math.hpp"
#include "LightingSystem.hpp"
#include <memory>

namespace MTL {
    class Device;
    class CommandBuffer;
    class ComputePipelineState;
    class Buffer;
}

namespace Crescent {

// Builds cluster headers + light index lists for Forward+/clustered lighting.
class ClusteredLightingPass {
public:
    ClusteredLightingPass();
    ~ClusteredLightingPass();
    
    bool initialize(MTL::Device* device, uint32_t clusterX = 16, uint32_t clusterY = 9, uint32_t clusterZ = 24);
    void shutdown();
    
    void setGrid(uint32_t clusterX, uint32_t clusterY, uint32_t clusterZ, uint32_t maxLightsPerCluster = 64);
    
    void dispatch(MTL::CommandBuffer* cmdBuffer,
                  const LightingSystem& lighting,
                  const Math::Matrix4x4& projection,
                  float nearPlane,
                  float farPlane,
                  uint32_t viewportWidth,
                  uint32_t viewportHeight,
                  MTL::Buffer* lightBuffer);
    
    MTL::Buffer* getClusterHeaders() const { return m_clusterHeaders; }
    MTL::Buffer* getClusterIndices() const { return m_clusterIndices; }
    uint32_t getClusterCount() const { return m_clusterCount; }
    uint32_t getClusterZ() const { return m_clusterZ; }
    uint32_t getClusterX() const { return m_clusterX; }
    uint32_t getClusterY() const { return m_clusterY; }
    uint32_t getMaxLightsPerCluster() const { return m_maxLightsPerCluster; }
    
private:
    void allocateBuffers();
    
private:
    MTL::Device* m_device;
    MTL::ComputePipelineState* m_pipeline;
    MTL::Buffer* m_clusterHeaders; // offset/count per cluster
    MTL::Buffer* m_clusterIndices; // flat light indices
    uint32_t m_clusterX;
    uint32_t m_clusterY;
    uint32_t m_clusterZ;
    uint32_t m_clusterCount;
    uint32_t m_maxLightsPerCluster;
};

} // namespace Crescent
