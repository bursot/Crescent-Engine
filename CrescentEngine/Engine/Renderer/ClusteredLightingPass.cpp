#include "ClusteredLightingPass.hpp"
#include <Metal/Metal.hpp>
#include <iostream>
#include <cstring>

namespace Crescent {

namespace {
    struct ClusterHeader {
        uint32_t offset;
        uint32_t count;
        uint32_t _pad0;
        uint32_t _pad1;
    };
}

ClusteredLightingPass::ClusteredLightingPass()
    : m_device(nullptr)
    , m_pipeline(nullptr)
    , m_clusterHeaders(nullptr)
    , m_clusterIndices(nullptr)
    , m_clusterX(16)
    , m_clusterY(9)
    , m_clusterZ(24)
    , m_clusterCount(0)
    , m_maxLightsPerCluster(64) {
}

ClusteredLightingPass::~ClusteredLightingPass() {
    shutdown();
}

bool ClusteredLightingPass::initialize(MTL::Device* device, uint32_t clusterX, uint32_t clusterY, uint32_t clusterZ) {
    m_device = device;
    setGrid(clusterX, clusterY, clusterZ, m_maxLightsPerCluster);
    
    NS::Error* error = nullptr;
    MTL::Library* lib = m_device->newDefaultLibrary();
    if (!lib) {
        std::cerr << "ClusteredLightingPass: missing default Metal library\n";
        return false;
    }
    
    MTL::Function* func = lib->newFunction(NS::String::string("cluster_build", NS::UTF8StringEncoding));
    if (!func) {
        std::cerr << "ClusteredLightingPass: missing cluster_build shader\n";
        lib->release();
        return false;
    }
    
    m_pipeline = m_device->newComputePipelineState(func, &error);
    func->release();
    lib->release();
    
    if (error) {
        std::cerr << "ClusteredLightingPass: pipeline error " << error->localizedDescription()->utf8String() << "\n";
        return false;
    }
    
    return m_pipeline != nullptr;
}

void ClusteredLightingPass::setGrid(uint32_t clusterX, uint32_t clusterY, uint32_t clusterZ, uint32_t maxLightsPerCluster) {
    m_clusterX = clusterX;
    m_clusterY = clusterY;
    m_clusterZ = clusterZ;
    m_maxLightsPerCluster = maxLightsPerCluster;
    m_clusterCount = m_clusterX * m_clusterY * m_clusterZ;
    allocateBuffers();
}

void ClusteredLightingPass::allocateBuffers() {
    if (!m_device) return;
    if (m_clusterHeaders) { m_clusterHeaders->release(); m_clusterHeaders = nullptr; }
    if (m_clusterIndices) { m_clusterIndices->release(); m_clusterIndices = nullptr; }
    
    size_t headersSize = sizeof(ClusterHeader) * m_clusterCount;
    size_t indicesSize = sizeof(uint32_t) * m_clusterCount * m_maxLightsPerCluster;
    
    m_clusterHeaders = m_device->newBuffer(headersSize, MTL::ResourceStorageModeShared);
    m_clusterIndices = m_device->newBuffer(indicesSize, MTL::ResourceStorageModeShared);
}

void ClusteredLightingPass::shutdown() {
    if (m_clusterHeaders) { m_clusterHeaders->release(); m_clusterHeaders = nullptr; }
    if (m_clusterIndices) { m_clusterIndices->release(); m_clusterIndices = nullptr; }
    if (m_pipeline) { m_pipeline->release(); m_pipeline = nullptr; }
}

void ClusteredLightingPass::dispatch(MTL::CommandBuffer* cmdBuffer,
                                     const LightingSystem& lighting,
                                     const Math::Matrix4x4& projection,
                                     float nearPlane,
                                     float farPlane,
                                     uint32_t viewportWidth,
                                     uint32_t viewportHeight,
                                     MTL::Buffer* lightBuffer) {
    if (!cmdBuffer || !m_pipeline || !lightBuffer) return;
    
    MTL::ComputeCommandEncoder* enc = cmdBuffer->computeCommandEncoder();
    enc->setComputePipelineState(m_pipeline);
    
    enc->setBuffer(lightBuffer, 0, 0);
    enc->setBuffer(m_clusterHeaders, 0, 1);
    enc->setBuffer(m_clusterIndices, 0, 2);
    // Overflow counter at buffer 3
    static const uint32_t zero = 0;
    MTL::Buffer* overflow = m_device->newBuffer(sizeof(uint32_t), MTL::ResourceStorageModeShared);
    memcpy(overflow->contents(), &zero, sizeof(uint32_t));
    enc->setBuffer(overflow, 0, 3);
    
    // Upload constants
    struct Params {
        Math::Matrix4x4 projection;
        Math::Matrix4x4 projectionInv;
        uint32_t clusterX, clusterY, clusterZ;
        uint32_t lightCount;
        uint32_t maxLightsPerCluster;
        float screenWidth;
        float screenHeight;
        float nearPlane;
        float farPlane;
        float _pad[3]; // pad to 16-byte multiple (Metal expects 176 bytes)
    } params;
    params.projection = projection;
    params.projectionInv = projection.inversed();
    params.clusterX = m_clusterX;
    params.clusterY = m_clusterY;
    params.clusterZ = m_clusterZ;
    params.lightCount = static_cast<uint32_t>(lighting.getVisibleLightCount());
    params.maxLightsPerCluster = m_maxLightsPerCluster;
    params.screenWidth = static_cast<float>(viewportWidth);
    params.screenHeight = static_cast<float>(viewportHeight);
    params.nearPlane = nearPlane;
    params.farPlane = farPlane;
    
    enc->setBytes(&params, sizeof(Params), 4);
    
    MTL::Size tgSize = MTL::Size::Make(8, 8, 1);
    MTL::Size grid = MTL::Size::Make((m_clusterX + 7) / 8, (m_clusterY + 7) / 8, m_clusterZ);
    enc->dispatchThreadgroups(grid, tgSize);
    
    enc->endEncoding();
    
    // Diagnostics
    uint32_t* overflowPtr = reinterpret_cast<uint32_t*>(overflow->contents());
    if (overflowPtr && *overflowPtr > 0) {
        std::cerr << "ClusteredLightingPass: overflowed lights in clusters: " << *overflowPtr << std::endl;
    }
    overflow->release();
}

} // namespace Crescent
