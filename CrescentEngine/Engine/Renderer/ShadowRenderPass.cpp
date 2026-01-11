#include "ShadowRenderPass.hpp"
#include "LightingSystem.hpp"
#include "../Scene/Scene.hpp"
#include "../Components/Camera.hpp"
#include "../Components/MeshRenderer.hpp"
#include "../Components/SkinnedMeshRenderer.hpp"
#include "../Rendering/Mesh.hpp"
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <iostream>
#include <array>
#include <cstring>

namespace {
constexpr bool kShadowDebug = false;
}

#define SHADOW_DEBUG_LOG(expr) do { if (kShadowDebug) { std::cout << expr << std::endl; } } while (0)

namespace Crescent {

namespace {
    // Vertex layout matches Engine vertex: position/normal/uv/tangent/bitangent/color
    inline MTL::VertexDescriptor* buildShadowVertexDescriptor(bool skinned) {
        MTL::VertexDescriptor* vd = MTL::VertexDescriptor::alloc()->init();
        vd->attributes()->object(0)->setFormat(MTL::VertexFormatFloat3);
        vd->attributes()->object(0)->setOffset(0);
        vd->attributes()->object(0)->setBufferIndex(0);
        vd->layouts()->object(0)->setStride(72);
        vd->layouts()->object(0)->setStepFunction(MTL::VertexStepFunctionPerVertex);
        if (skinned) {
            vd->attributes()->object(6)->setFormat(MTL::VertexFormatUInt4);
            vd->attributes()->object(6)->setOffset(0);
            vd->attributes()->object(6)->setBufferIndex(4);
            vd->attributes()->object(7)->setFormat(MTL::VertexFormatFloat4);
            vd->attributes()->object(7)->setOffset(sizeof(uint32_t) * 4);
            vd->attributes()->object(7)->setBufferIndex(4);
            vd->layouts()->object(4)->setStride(sizeof(uint32_t) * 4 + sizeof(float) * 4);
            vd->layouts()->object(4)->setStepFunction(MTL::VertexStepFunctionPerVertex);
        }
        return vd;
    }
}

ShadowRenderPass::ShadowRenderPass()
    : m_device(nullptr)
    , m_shadowAtlas(nullptr)
    , m_depthState(nullptr)
    , m_dirPipeline(nullptr)
    , m_spotPipeline(nullptr)
    , m_pointPipeline(nullptr)
    , m_areaPipeline(nullptr)
    , m_dirPipelineSkinned(nullptr)
    , m_spotPipelineSkinned(nullptr)
    , m_pointPipelineSkinned(nullptr)
    , m_areaPipelineSkinned(nullptr)
    , m_skinningBuffer(nullptr)
    , m_skinningBufferCapacity(0)
    , m_atlasResolution(4096)
    , m_atlasLayers(1) {
}

ShadowRenderPass::~ShadowRenderPass() {
    shutdown();
}

bool ShadowRenderPass::initialize(MTL::Device* device, uint32_t atlasResolution, uint32_t atlasLayers) {
    m_device = device;
    m_atlasResolution = atlasResolution;
    m_atlasLayers = atlasLayers;
    
    if (!m_device) {
        return false;
    }
    
    // Create atlas texture (depth-only)
    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
    desc->setTextureType(atlasLayers > 1 ? MTL::TextureType2DArray : MTL::TextureType2D);
    desc->setWidth(atlasResolution);
    desc->setHeight(atlasResolution);
    desc->setArrayLength(atlasLayers);
    desc->setPixelFormat(MTL::PixelFormatDepth32Float);
    desc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
    desc->setStorageMode(MTL::StorageModePrivate);
    
    m_shadowAtlas = m_device->newTexture(desc);
    desc->release();
    
    if (!m_shadowAtlas) {
        std::cerr << "Failed to create shadow atlas texture\n";
        return false;
    }
    
    buildDepthState();
    buildPipelines();
    return m_dirPipeline && m_spotPipeline && m_pointPipeline && m_areaPipeline;
}

void ShadowRenderPass::shutdown() {
    if (m_shadowAtlas) { m_shadowAtlas->release(); m_shadowAtlas = nullptr; }
    for (auto& tex : m_pointCubeTextures) {
        if (tex) tex->release();
    }
    m_pointCubeTextures.clear();
    if (m_depthState) { m_depthState->release(); m_depthState = nullptr; }
    if (m_dirPipeline) { m_dirPipeline->release(); m_dirPipeline = nullptr; }
    if (m_spotPipeline) { m_spotPipeline->release(); m_spotPipeline = nullptr; }
    if (m_pointPipeline) { m_pointPipeline->release(); m_pointPipeline = nullptr; }
    if (m_areaPipeline) { m_areaPipeline->release(); m_areaPipeline = nullptr; }
    if (m_dirPipelineSkinned) { m_dirPipelineSkinned->release(); m_dirPipelineSkinned = nullptr; }
    if (m_spotPipelineSkinned) { m_spotPipelineSkinned->release(); m_spotPipelineSkinned = nullptr; }
    if (m_pointPipelineSkinned) { m_pointPipelineSkinned->release(); m_pointPipelineSkinned = nullptr; }
    if (m_areaPipelineSkinned) { m_areaPipelineSkinned->release(); m_areaPipelineSkinned = nullptr; }
    if (m_skinningBuffer) { m_skinningBuffer->release(); m_skinningBuffer = nullptr; }
    m_skinningBufferCapacity = 0;
}

void ShadowRenderPass::buildDepthState() {
    MTL::DepthStencilDescriptor* depthDesc = MTL::DepthStencilDescriptor::alloc()->init();
    depthDesc->setDepthCompareFunction(MTL::CompareFunctionLessEqual);
    depthDesc->setDepthWriteEnabled(true);
    m_depthState = m_device->newDepthStencilState(depthDesc);
    depthDesc->release();
}

void ShadowRenderPass::buildPipelines() {
    NS::Error* error = nullptr;
    MTL::Library* lib = m_device->newDefaultLibrary();
    if (!lib) {
        std::cerr << "ShadowRenderPass: missing default Metal library\n";
        return;
    }
    
    auto buildPipeline = [&](const char* vsName, MTL::RenderPipelineState** out, bool skinned) {
        MTL::Function* vs = lib->newFunction(NS::String::string(vsName, NS::UTF8StringEncoding));
        if (!vs) { std::cerr << "ShadowRenderPass: missing shader " << vsName << "\n"; return; }
        
        MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
        desc->setVertexFunction(vs);
        desc->setFragmentFunction(nullptr); // depth-only
        MTL::VertexDescriptor* vd = buildShadowVertexDescriptor(skinned);
        desc->setVertexDescriptor(vd);
        desc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatInvalid);
        desc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);
        
        *out = m_device->newRenderPipelineState(desc, &error);
        if (error) {
            std::cerr << "ShadowRenderPass: pipeline error " << error->localizedDescription()->utf8String() << "\n";
        }
        vd->release();
        desc->release();
        vs->release();
    };
    
    buildPipeline("shadow_dir_vertex", &m_dirPipeline, false);
    buildPipeline("shadow_spot_vertex", &m_spotPipeline, false);
    buildPipeline("shadow_point_vertex", &m_pointPipeline, false);
    buildPipeline("shadow_area_vertex", &m_areaPipeline, false);
    buildPipeline("shadow_dir_vertex_skinned", &m_dirPipelineSkinned, true);
    buildPipeline("shadow_spot_vertex_skinned", &m_spotPipelineSkinned, true);
    buildPipeline("shadow_point_vertex_skinned", &m_pointPipelineSkinned, true);
    buildPipeline("shadow_area_vertex_skinned", &m_areaPipelineSkinned, true);
    
    lib->release();
}

void ShadowRenderPass::execute(MTL::CommandBuffer* cmdBuffer,
                               Scene* scene,
                               Camera* camera,
                               const LightingSystem& lighting) {
    if (!cmdBuffer || !scene || !camera || !m_shadowAtlas) {
        return;
    }
    
    // Clear atlas once
    {
        MTL::RenderPassDescriptor* clearDesc = MTL::RenderPassDescriptor::alloc()->init();
        clearDesc->depthAttachment()->setTexture(m_shadowAtlas);
        clearDesc->depthAttachment()->setLoadAction(MTL::LoadActionClear);
        clearDesc->depthAttachment()->setStoreAction(MTL::StoreActionStore);
        clearDesc->depthAttachment()->setClearDepth(1.0);
        MTL::RenderCommandEncoder* clearEnc = cmdBuffer->renderCommandEncoder(clearDesc);
        clearEnc->endEncoding();
        clearDesc->release();
    }
    
    renderDirectional(cmdBuffer, scene, lighting);
    renderLocal(cmdBuffer, scene, lighting);
    renderPointCubes(cmdBuffer, scene, lighting);
}

void ShadowRenderPass::renderDirectional(MTL::CommandBuffer* cmdBuffer, Scene* scene, const LightingSystem& lighting) {
    const auto& cascades = lighting.getCascades();
    if (cascades.empty()) {
        SHADOW_DEBUG_LOG("[SHADOW DEBUG] No cascades!");
        return;
    }
    
    SHADOW_DEBUG_LOG("[SHADOW DEBUG] Rendering " << cascades.size() << " cascades");
    
    for (size_t i = 0; i < cascades.size(); ++i) {
        const auto& slice = cascades[i];
        if (!slice.atlas.valid) {
            SHADOW_DEBUG_LOG("[SHADOW DEBUG] Cascade " << i << " atlas invalid!");
            continue;
        }
        
        SHADOW_DEBUG_LOG("[SHADOW DEBUG] Cascade " << i << " atlas: x=" << slice.atlas.x
                         << " y=" << slice.atlas.y << " size=" << slice.atlas.size);
        
        MTL::RenderPassDescriptor* rp = MTL::RenderPassDescriptor::alloc()->init();
        rp->depthAttachment()->setTexture(m_shadowAtlas);
        rp->depthAttachment()->setLevel(0);
        rp->depthAttachment()->setSlice(slice.atlas.layer);
        // Atlas already cleared once per frame; keep other tiles intact.
        rp->depthAttachment()->setLoadAction(MTL::LoadActionLoad);
        rp->depthAttachment()->setStoreAction(MTL::StoreActionStore);
        rp->depthAttachment()->setClearDepth(1.0);
        
        // Set viewport to atlas rect
        MTL::RenderCommandEncoder* enc = cmdBuffer->renderCommandEncoder(rp);
        enc->setDepthStencilState(m_depthState);
        enc->setViewport({double(slice.atlas.x), double(slice.atlas.y), double(slice.atlas.size), double(slice.atlas.size), 0.0, 1.0});
        
        // Iterate meshes
        int meshCount = 0;
        MTL::RenderPipelineState* currentPipeline = nullptr;
        const auto& entities = scene->getAllEntities();
        for (const auto& entPtr : entities) {
            Entity* e = entPtr.get();
            if (!e->isActiveInHierarchy()) continue;
            MeshRenderer* mr = e->getComponent<MeshRenderer>();
            if (!mr || !mr->isEnabled()) continue;
            if (!mr->getCastShadows()) {
                SHADOW_DEBUG_LOG("[SHADOW DEBUG] Entity '" << e->getName() << "' doesn't cast shadows");
                continue;
            }
            std::shared_ptr<Mesh> mesh = mr->getMesh();
            if (!mesh) {
                SHADOW_DEBUG_LOG("[SHADOW DEBUG] Entity '" << e->getName() << "' has no mesh");
                continue;
            }
            if (!mesh->isUploaded()) {
                SHADOW_DEBUG_LOG("[SHADOW DEBUG] Entity '" << e->getName() << "' mesh not uploaded");
                continue;
            }
            
            meshCount++;

            SkinnedMeshRenderer* skinned = e->getComponent<SkinnedMeshRenderer>();
            bool wantsSkin = skinned && skinned->isEnabled() && mesh->hasSkinWeights() && !skinned->getBoneMatrices().empty();
            MTL::Buffer* skinBuffer = static_cast<MTL::Buffer*>(mesh->getSkinWeightBuffer());
            bool useSkinned = wantsSkin && skinBuffer && m_dirPipelineSkinned;

            MTL::RenderPipelineState* desiredPipeline = useSkinned ? m_dirPipelineSkinned : m_dirPipeline;
            if (!desiredPipeline) {
                continue;
            }
            if (desiredPipeline != currentPipeline) {
                enc->setRenderPipelineState(desiredPipeline);
                currentPipeline = desiredPipeline;
            }
            
            Math::Matrix4x4 model = e->getTransform()->getWorldMatrix();
            Math::Matrix4x4 mvp = slice.viewProj * model;
            enc->setVertexBuffer(static_cast<MTL::Buffer*>(mesh->getVertexBuffer()), 0, 0);
            if (useSkinned) {
                enc->setVertexBuffer(skinBuffer, 0, 4);
                const auto& boneMatrices = skinned->getBoneMatrices();
                size_t bytes = boneMatrices.size() * sizeof(Math::Matrix4x4);
                if (!m_skinningBuffer || m_skinningBufferCapacity < bytes) {
                    if (m_skinningBuffer) {
                        m_skinningBuffer->release();
                    }
                    m_skinningBuffer = m_device->newBuffer(bytes, MTL::ResourceStorageModeShared);
                    m_skinningBufferCapacity = m_skinningBuffer ? m_skinningBuffer->length() : 0;
                }
                if (m_skinningBuffer) {
                    std::memcpy(m_skinningBuffer->contents(), boneMatrices.data(), bytes);
                    enc->setVertexBuffer(m_skinningBuffer, 0, 2);
                }
            }
            enc->setVertexBytes(&mvp, sizeof(Math::Matrix4x4), 1);
            enc->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle,
                                       mesh->getIndices().size(),
                                       MTL::IndexTypeUInt32,
                                       static_cast<MTL::Buffer*>(mesh->getIndexBuffer()),
                                       0);
        }
        
        SHADOW_DEBUG_LOG("[SHADOW DEBUG] Cascade " << i << " rendered " << meshCount << " meshes");
        
        enc->endEncoding();
        rp->release();
    }
}

void ShadowRenderPass::renderLocal(MTL::CommandBuffer* cmdBuffer, Scene* scene, const LightingSystem& lighting) {
    const auto& lights = lighting.getGPULights();
    const auto& shadows = lighting.getGPUShadows();
    const auto& tiles = lighting.getShadowAtlas().getTiles();
    (void)tiles; // reserved for debugging
    
    SHADOW_DEBUG_LOG("[SHADOW DEBUG] renderLocal: " << lights.size() << " lights, " << shadows.size() << " shadows");
    
    for (size_t i = 0; i < lights.size(); ++i) {
        const LightGPUData& lgpu = lights[i];
        int shadowIdx = static_cast<int>(lgpu.shadowCookie.x);
        if (shadowIdx < 0 || shadowIdx >= static_cast<int>(shadows.size())) {
            continue;
        }
        int type = static_cast<int>(lgpu.directionType.w);
        if (type == 1) {
            continue; // point handled in renderPointCubes
        }
        
        SHADOW_DEBUG_LOG("[SHADOW DEBUG] Light " << i << " type=" << type << " shadowIdx=" << shadowIdx);
        
        MTL::RenderPipelineState* pipeline = nullptr;
        switch (type) {
            case 2: pipeline = m_spotPipeline; break;
            case 3:
            case 4: pipeline = m_areaPipeline; break;
            default: pipeline = nullptr; break;
        }
        if (!pipeline) continue;
        
        const ShadowGPUData& s = shadows[shadowIdx];
        ShadowAtlasTile dummyTile{};
        dummyTile.valid = true;
        dummyTile.x = static_cast<uint32_t>(s.atlasUV.x * m_atlasResolution);
        dummyTile.y = static_cast<uint32_t>(s.atlasUV.y * m_atlasResolution);
        dummyTile.size = static_cast<uint32_t>(s.atlasUV.z * m_atlasResolution);
        dummyTile.layer = static_cast<uint32_t>(s.depthRange.w);
        MTL::RenderPipelineState* pipelineSkinned = nullptr;
        switch (type) {
            case 2: pipelineSkinned = m_spotPipelineSkinned; break;
            case 3:
            case 4: pipelineSkinned = m_areaPipelineSkinned; break;
            default: pipelineSkinned = nullptr; break;
        }
        renderLightRange(cmdBuffer, scene, s, dummyTile, pipeline, pipelineSkinned);
    }
}

void ShadowRenderPass::renderLightRange(MTL::CommandBuffer* cmdBuffer,
                                        Scene* scene,
                                        const ShadowGPUData& shadow,
                                        const ShadowAtlasTile& tile,
                                        MTL::RenderPipelineState* pipeline,
                                        MTL::RenderPipelineState* pipelineSkinned) {
    if (!tile.valid || !pipeline) {
        SHADOW_DEBUG_LOG("[SHADOW DEBUG] renderLightRange: tile invalid or no pipeline");
        return;
    }
    
    SHADOW_DEBUG_LOG("[SHADOW DEBUG] renderLightRange: tile x=" << tile.x << " y=" << tile.y << " size=" << tile.size);
    
    MTL::RenderPassDescriptor* rp = MTL::RenderPassDescriptor::alloc()->init();
    rp->depthAttachment()->setTexture(m_shadowAtlas);
    rp->depthAttachment()->setSlice(tile.layer);
    // Atlas already cleared once per frame; keep other tiles intact.
    rp->depthAttachment()->setLoadAction(MTL::LoadActionLoad);
    rp->depthAttachment()->setStoreAction(MTL::StoreActionStore);
    rp->depthAttachment()->setClearDepth(1.0);
    
    MTL::RenderCommandEncoder* enc = cmdBuffer->renderCommandEncoder(rp);
    enc->setDepthStencilState(m_depthState);
    enc->setViewport({double(tile.x), double(tile.y), double(tile.size), double(tile.size), 0.0, 1.0});
    
        const auto& entities = scene->getAllEntities();
        MTL::RenderPipelineState* currentPipeline = nullptr;
        for (const auto& entPtr : entities) {
            Entity* e = entPtr.get();
            if (!e->isActiveInHierarchy()) continue;
            MeshRenderer* mr = e->getComponent<MeshRenderer>();
            if (!mr || !mr->isEnabled() || !mr->getCastShadows()) continue;
            std::shared_ptr<Mesh> mesh = mr->getMesh();
            if (!mesh || !mesh->isUploaded()) continue;

            SkinnedMeshRenderer* skinned = e->getComponent<SkinnedMeshRenderer>();
            bool wantsSkin = skinned && skinned->isEnabled() && mesh->hasSkinWeights() && !skinned->getBoneMatrices().empty();
            MTL::Buffer* skinBuffer = static_cast<MTL::Buffer*>(mesh->getSkinWeightBuffer());
            bool useSkinned = wantsSkin && skinBuffer && pipelineSkinned;
            MTL::RenderPipelineState* desiredPipeline = useSkinned ? pipelineSkinned : pipeline;
            if (!desiredPipeline) {
                continue;
            }
            if (desiredPipeline != currentPipeline) {
                enc->setRenderPipelineState(desiredPipeline);
                currentPipeline = desiredPipeline;
            }
        
        Math::Matrix4x4 model = e->getTransform()->getWorldMatrix();
        Math::Matrix4x4 mvp = shadow.viewProj * model;
        enc->setVertexBuffer(static_cast<MTL::Buffer*>(mesh->getVertexBuffer()), 0, 0);
        if (useSkinned) {
            enc->setVertexBuffer(skinBuffer, 0, 4);
            const auto& boneMatrices = skinned->getBoneMatrices();
            size_t bytes = boneMatrices.size() * sizeof(Math::Matrix4x4);
            if (!m_skinningBuffer || m_skinningBufferCapacity < bytes) {
                if (m_skinningBuffer) {
                    m_skinningBuffer->release();
                }
                m_skinningBuffer = m_device->newBuffer(bytes, MTL::ResourceStorageModeShared);
                m_skinningBufferCapacity = m_skinningBuffer ? m_skinningBuffer->length() : 0;
            }
            if (m_skinningBuffer) {
                std::memcpy(m_skinningBuffer->contents(), boneMatrices.data(), bytes);
                enc->setVertexBuffer(m_skinningBuffer, 0, 2);
            }
        }
        enc->setVertexBytes(&mvp, sizeof(Math::Matrix4x4), 1);
        enc->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle,
                                   mesh->getIndices().size(),
                                   MTL::IndexTypeUInt32,
                                   static_cast<MTL::Buffer*>(mesh->getIndexBuffer()),
                                   0);
    }
    
    enc->endEncoding();
    rp->release();
}

void ShadowRenderPass::renderPointCubes(MTL::CommandBuffer* cmdBuffer, Scene* scene, const LightingSystem& lighting) {
    if (!m_pointPipeline) return;
    const auto& lights = lighting.getGPULights();
    const auto& prepared = lighting.getPreparedLights();
    const auto& shadows = lighting.getGPUShadows();
    if (lights.empty() || shadows.empty()) return;
    
    // Count point lights per tier and track max index per tier
    std::array<uint32_t,4> tierCounts = {0,0,0,0};
    std::array<uint32_t,4> tierMaxCube = {0,0,0,0};
    for (size_t i = 0; i < lights.size(); ++i) {
        const auto& L = lights[i];
        if ((int)round(L.directionType.w) == 1 && L.shadowCookie.x >= 0) {
            int shadowIdx = (int)round(L.shadowCookie.x);
            if (shadowIdx >= 0 && shadowIdx < (int)shadows.size()) {
                int tier = (int)round(shadows[shadowIdx].depthRange.w);
                tier = std::max(0, std::min(3, tier));
                tierCounts[tier]++;
                tierMaxCube[tier] = std::max<uint32_t>(tierMaxCube[tier], (uint32_t)shadows[shadowIdx].depthRange.z);
            }
        }
    }
    
    // Allocate cube array per tier if needed
    static const uint32_t presets[4] = {256, 512, 1024, 2048};
    for (int tier = 0; tier < 4; ++tier) {
        if (tierCounts[tier] == 0) continue;
        uint32_t res = presets[tier];
        uint32_t neededCubes = tierMaxCube[tier] + 1;
        if (tier >= (int)m_pointCubeTextures.size()) {
            m_pointCubeTextures.resize(4, nullptr);
        }
        MTL::Texture* tex = m_pointCubeTextures[tier];
        if (!tex || tex->width() != res || tex->arrayLength() < neededCubes) {
            if (tex) { tex->release(); }
            MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
            desc->setTextureType(MTL::TextureTypeCubeArray);
            desc->setWidth(res);
            desc->setHeight(res);
            desc->setArrayLength(neededCubes);
            desc->setPixelFormat(MTL::PixelFormatDepth32Float);
            desc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
            desc->setStorageMode(MTL::StorageModePrivate);
            tex = m_device->newTexture(desc);
            desc->release();
            m_pointCubeTextures[tier] = tex;
        }
    }
    
    static const Math::Vector3 faceDirs[6] = {
        Math::Vector3(1,0,0), Math::Vector3(-1,0,0),
        Math::Vector3(0,1,0), Math::Vector3(0,-1,0),
        Math::Vector3(0,0,1), Math::Vector3(0,0,-1)
    };
    static const Math::Vector3 faceUps[6] = {
        Math::Vector3(0,1,0), Math::Vector3(0,1,0),
        Math::Vector3(0,0,1), Math::Vector3(0,0,-1),
        Math::Vector3(0,1,0), Math::Vector3(0,1,0)
    };
    
    for (size_t i = 0; i < prepared.size(); ++i) {
        if (!prepared[i].light || prepared[i].light->getType() != Light::Type::Point) continue;
        const auto& L = lights[i];
        if ((int)round(L.directionType.w) != 1 || L.shadowCookie.x < 0) continue;
        int shadowIdx = (int)round(L.shadowCookie.x);
        if (shadowIdx < 0 || shadowIdx >= (int)shadows.size()) continue;
        const ShadowGPUData& s = shadows[shadowIdx];
        uint32_t res = (uint32_t)std::max(1.0f, s.atlasUV.x);
        int tier = (int)round(s.depthRange.w);
        tier = std::max(0, std::min(3, tier));
        MTL::Texture* cubeTex = (tier < (int)m_pointCubeTextures.size()) ? m_pointCubeTextures[tier] : nullptr;
        if (!cubeTex) continue;
        uint32_t cubeIndex = (uint32_t)std::max(0.0f, s.depthRange.z);
        
        // Clear all faces for this cube
        for (int face = 0; face < 6; ++face) {
            MTL::RenderPassDescriptor* rp = MTL::RenderPassDescriptor::alloc()->init();
            rp->depthAttachment()->setTexture(cubeTex);
            // For cube arrays, slices are laid out as 6 faces per cube
            rp->depthAttachment()->setSlice(cubeIndex * 6 + face);
            rp->depthAttachment()->setLoadAction(MTL::LoadActionClear);
            rp->depthAttachment()->setStoreAction(MTL::StoreActionStore);
            rp->depthAttachment()->setClearDepth(1.0);
            
            MTL::RenderCommandEncoder* enc = cmdBuffer->renderCommandEncoder(rp);
            enc->setDepthStencilState(m_depthState);
            enc->setViewport({0.0, 0.0, (double)res, (double)res, 0.0, 1.0});
            MTL::RenderPipelineState* currentPipeline = nullptr;
            
            Math::Vector3 lightPos = prepared[i].positionWS;
            Math::Matrix4x4 view = Math::Matrix4x4::LookAt(lightPos, lightPos + faceDirs[face], faceUps[face]);
            // Cubemap face FOV must be 90 degrees (HALF_PI), not 180!
            Math::Matrix4x4 proj = Math::Matrix4x4::Perspective(Math::HALF_PI, 1.0f, s.depthRange.x, s.depthRange.y);
            Math::Matrix4x4 vp = proj * view;
            
        const auto& entities = scene->getAllEntities();
        for (const auto& entPtr : entities) {
            Entity* e = entPtr.get();
            if (!e->isActiveInHierarchy()) continue;
            MeshRenderer* mr = e->getComponent<MeshRenderer>();
            if (!mr || !mr->isEnabled() || !mr->getCastShadows()) continue;
            std::shared_ptr<Mesh> mesh = mr->getMesh();
            if (!mesh || !mesh->isUploaded()) continue;
                
                Math::Matrix4x4 model = e->getTransform()->getWorldMatrix();
                Math::Matrix4x4 mvp = vp * model;
                enc->setVertexBuffer(static_cast<MTL::Buffer*>(mesh->getVertexBuffer()), 0, 0);
                SkinnedMeshRenderer* skinned = e->getComponent<SkinnedMeshRenderer>();
                bool wantsSkin = skinned && skinned->isEnabled() && mesh->hasSkinWeights() && !skinned->getBoneMatrices().empty();
                MTL::Buffer* skinBuffer = static_cast<MTL::Buffer*>(mesh->getSkinWeightBuffer());
                bool useSkinned = wantsSkin && skinBuffer && m_pointPipelineSkinned;
                MTL::RenderPipelineState* desiredPipeline = useSkinned ? m_pointPipelineSkinned : m_pointPipeline;
                if (!desiredPipeline) {
                    continue;
                }
                if (desiredPipeline != currentPipeline) {
                    enc->setRenderPipelineState(desiredPipeline);
                    currentPipeline = desiredPipeline;
                }
                if (useSkinned) {
                    enc->setVertexBuffer(skinBuffer, 0, 4);
                    const auto& boneMatrices = skinned->getBoneMatrices();
                    size_t bytes = boneMatrices.size() * sizeof(Math::Matrix4x4);
                    if (!m_skinningBuffer || m_skinningBufferCapacity < bytes) {
                        if (m_skinningBuffer) {
                            m_skinningBuffer->release();
                        }
                        m_skinningBuffer = m_device->newBuffer(bytes, MTL::ResourceStorageModeShared);
                        m_skinningBufferCapacity = m_skinningBuffer ? m_skinningBuffer->length() : 0;
                    }
                    if (m_skinningBuffer) {
                        std::memcpy(m_skinningBuffer->contents(), boneMatrices.data(), bytes);
                        enc->setVertexBuffer(m_skinningBuffer, 0, 2);
                    }
                }
                enc->setVertexBytes(&mvp, sizeof(Math::Matrix4x4), 1);
                enc->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle,
                                           mesh->getIndices().size(),
                                           MTL::IndexTypeUInt32,
                                           static_cast<MTL::Buffer*>(mesh->getIndexBuffer()),
                                           0);
            }
            
            enc->endEncoding();
            rp->release();
        }
        
    }
}

} // namespace Crescent
