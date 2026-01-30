#include "ShadowRenderPass.hpp"
#include "LightingSystem.hpp"
#include "../Scene/Scene.hpp"
#include "../Components/Camera.hpp"
#include "../Components/MeshRenderer.hpp"
#include "../Components/SkinnedMeshRenderer.hpp"
#include "../Components/HLODProxy.hpp"
#include "../Rendering/Mesh.hpp"
#include "../Rendering/Material.hpp"
#include "../Core/Time.hpp"
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <algorithm>
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
    inline MTL::VertexDescriptor* buildShadowVertexDescriptor(bool skinned, bool includeUV) {
        MTL::VertexDescriptor* vd = MTL::VertexDescriptor::alloc()->init();
        vd->attributes()->object(0)->setFormat(MTL::VertexFormatFloat3);
        vd->attributes()->object(0)->setOffset(0);
        vd->attributes()->object(0)->setBufferIndex(0);
        if (includeUV) {
            vd->attributes()->object(2)->setFormat(MTL::VertexFormatFloat2);
            vd->attributes()->object(2)->setOffset(24);
            vd->attributes()->object(2)->setBufferIndex(0);
        }
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

    struct DrawIndexedIndirectArgs {
        uint32_t indexCount;
        uint32_t instanceCount;
        uint32_t indexStart;
        int32_t baseVertex;
        uint32_t baseInstance;
    };

    struct InstanceDataCPU {
        Math::Matrix4x4 modelMatrix;
        Math::Matrix4x4 normalMatrix;
    };

    struct InstanceCullParamsCPU {
        Math::Vector4 frustumPlanes[6];
        Math::Vector4 boundsCenterRadius;
        uint32_t inputOffset;
        uint32_t outputOffset;
        uint32_t instanceCount;
        uint32_t pad;
        Math::Vector2 screenSize;
        uint32_t hzbMipCount;
        uint32_t pad2;
    };

    struct ShadowAlphaParamsCPU {
        Math::Vector4 albedo;
        Math::Vector4 uvTilingOffset;
        Math::Vector4 alphaParams; // x cutoff, y hasAlbedoTex, z alphaClip, w unused
    };

    struct ShadowFoliageParamsCPU {
        Math::Vector4 foliageParams0;
        Math::Vector4 foliageParams1;
        Math::Vector4 foliageParams2;
        Math::Vector4 foliageParams3;
        Math::Vector4 cameraTime;
        Math::Vector4 boundsCenter;
        Math::Vector4 boundsSize;
        Math::Vector4 flags;
    };

    static std::array<Math::Vector4, 6> extractFrustumPlanes(const Math::Matrix4x4& m) {
        auto row = [&](int r) {
            return Math::Vector4(m(r, 0), m(r, 1), m(r, 2), m(r, 3));
        };
        Math::Vector4 r0 = row(0);
        Math::Vector4 r1 = row(1);
        Math::Vector4 r2 = row(2);
        Math::Vector4 r3 = row(3);
        std::array<Math::Vector4, 6> planes = {
            r3 + r0,
            r3 - r0,
            r3 + r1,
            r3 - r1,
            r3 + r2,
            r3 - r2
        };
        for (auto& p : planes) {
            float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
            if (len > 0.0f) {
                p.x /= len;
                p.y /= len;
                p.z /= len;
                p.w /= len;
            }
        }
        return planes;
    }

    inline bool IsCutoutMaterial(const std::shared_ptr<Material>& material) {
        return material && material->getRenderMode() == Material::RenderMode::Cutout;
    }

    inline MTL::CullMode ResolveCullMode(const std::shared_ptr<Material>& material) {
        if (!material) {
            return MTL::CullModeBack;
        }
        if (material->isTwoSided() || material->getCullMode() == Material::CullMode::Off) {
            return MTL::CullModeNone;
        }
        if (material->getCullMode() == Material::CullMode::Front) {
            return MTL::CullModeFront;
        }
        return MTL::CullModeBack;
    }

    inline void BindShadowAlpha(MTL::RenderCommandEncoder* enc,
                                const std::shared_ptr<Material>& material,
                                MTL::SamplerState* sampler) {
        if (!material || !enc) {
            return;
        }
        ShadowAlphaParamsCPU params{};
        params.albedo = material->getAlbedo();
        Math::Vector2 tiling = material->getUVTiling();
        Math::Vector2 offset = material->getUVOffset();
        params.uvTilingOffset = Math::Vector4(tiling.x, tiling.y, offset.x, offset.y);
        bool hasAlbedo = material->getAlbedoTexture() != nullptr;
        params.alphaParams = Math::Vector4(
            material->getAlphaCutoff(),
            hasAlbedo ? 1.0f : 0.0f,
            material->getRenderMode() == Material::RenderMode::Cutout ? 1.0f : 0.0f,
            0.0f
        );
        enc->setFragmentBytes(&params, sizeof(ShadowAlphaParamsCPU), 0);
        auto albedoTex = material->getAlbedoTexture();
        enc->setFragmentTexture(albedoTex ? albedoTex->getHandle() : nullptr, 0);
        if (sampler) {
            enc->setFragmentSamplerState(sampler, 0);
        }
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
    , m_dirPipelineCutout(nullptr)
    , m_spotPipelineCutout(nullptr)
    , m_pointPipelineCutout(nullptr)
    , m_areaPipelineCutout(nullptr)
    , m_dirPipelineSkinned(nullptr)
    , m_spotPipelineSkinned(nullptr)
    , m_pointPipelineSkinned(nullptr)
    , m_areaPipelineSkinned(nullptr)
    , m_dirPipelineSkinnedCutout(nullptr)
    , m_spotPipelineSkinnedCutout(nullptr)
    , m_pointPipelineSkinnedCutout(nullptr)
    , m_areaPipelineSkinnedCutout(nullptr)
    , m_dirPipelineInstanced(nullptr)
    , m_spotPipelineInstanced(nullptr)
    , m_pointPipelineInstanced(nullptr)
    , m_areaPipelineInstanced(nullptr)
    , m_dirPipelineInstancedCutout(nullptr)
    , m_spotPipelineInstancedCutout(nullptr)
    , m_pointPipelineInstancedCutout(nullptr)
    , m_areaPipelineInstancedCutout(nullptr)
    , m_instanceCullPipeline(nullptr)
    , m_instanceIndirectPipeline(nullptr)
    , m_instanceCullBuffer(nullptr)
    , m_instanceCountBuffer(nullptr)
    , m_instanceIndirectBuffer(nullptr)
    , m_instanceCullCapacity(0)
    , m_instanceCountCapacity(0)
    , m_instanceIndirectCapacity(0)
    , m_skinningBuffer(nullptr)
    , m_alphaSampler(nullptr)
    , m_skinningBufferCapacity(0)
    , m_skinningBufferOffset(0)
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
    if (m_alphaSampler) {
        m_alphaSampler->release();
        m_alphaSampler = nullptr;
    }
    MTL::SamplerDescriptor* alphaDesc = MTL::SamplerDescriptor::alloc()->init();
    alphaDesc->setMinFilter(MTL::SamplerMinMagFilterLinear);
    alphaDesc->setMagFilter(MTL::SamplerMinMagFilterLinear);
    alphaDesc->setMipFilter(MTL::SamplerMipFilterNearest);
    alphaDesc->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
    alphaDesc->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
    m_alphaSampler = m_device->newSamplerState(alphaDesc);
    alphaDesc->release();
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
    if (m_dirPipelineCutout) { m_dirPipelineCutout->release(); m_dirPipelineCutout = nullptr; }
    if (m_spotPipelineCutout) { m_spotPipelineCutout->release(); m_spotPipelineCutout = nullptr; }
    if (m_pointPipelineCutout) { m_pointPipelineCutout->release(); m_pointPipelineCutout = nullptr; }
    if (m_areaPipelineCutout) { m_areaPipelineCutout->release(); m_areaPipelineCutout = nullptr; }
    if (m_dirPipelineSkinned) { m_dirPipelineSkinned->release(); m_dirPipelineSkinned = nullptr; }
    if (m_spotPipelineSkinned) { m_spotPipelineSkinned->release(); m_spotPipelineSkinned = nullptr; }
    if (m_pointPipelineSkinned) { m_pointPipelineSkinned->release(); m_pointPipelineSkinned = nullptr; }
    if (m_areaPipelineSkinned) { m_areaPipelineSkinned->release(); m_areaPipelineSkinned = nullptr; }
    if (m_dirPipelineSkinnedCutout) { m_dirPipelineSkinnedCutout->release(); m_dirPipelineSkinnedCutout = nullptr; }
    if (m_spotPipelineSkinnedCutout) { m_spotPipelineSkinnedCutout->release(); m_spotPipelineSkinnedCutout = nullptr; }
    if (m_pointPipelineSkinnedCutout) { m_pointPipelineSkinnedCutout->release(); m_pointPipelineSkinnedCutout = nullptr; }
    if (m_areaPipelineSkinnedCutout) { m_areaPipelineSkinnedCutout->release(); m_areaPipelineSkinnedCutout = nullptr; }
    if (m_dirPipelineInstanced) { m_dirPipelineInstanced->release(); m_dirPipelineInstanced = nullptr; }
    if (m_spotPipelineInstanced) { m_spotPipelineInstanced->release(); m_spotPipelineInstanced = nullptr; }
    if (m_pointPipelineInstanced) { m_pointPipelineInstanced->release(); m_pointPipelineInstanced = nullptr; }
    if (m_areaPipelineInstanced) { m_areaPipelineInstanced->release(); m_areaPipelineInstanced = nullptr; }
    if (m_dirPipelineInstancedCutout) { m_dirPipelineInstancedCutout->release(); m_dirPipelineInstancedCutout = nullptr; }
    if (m_spotPipelineInstancedCutout) { m_spotPipelineInstancedCutout->release(); m_spotPipelineInstancedCutout = nullptr; }
    if (m_pointPipelineInstancedCutout) { m_pointPipelineInstancedCutout->release(); m_pointPipelineInstancedCutout = nullptr; }
    if (m_areaPipelineInstancedCutout) { m_areaPipelineInstancedCutout->release(); m_areaPipelineInstancedCutout = nullptr; }
    if (m_instanceCullPipeline) { m_instanceCullPipeline->release(); m_instanceCullPipeline = nullptr; }
    if (m_instanceIndirectPipeline) { m_instanceIndirectPipeline->release(); m_instanceIndirectPipeline = nullptr; }
    if (m_instanceCullBuffer) { m_instanceCullBuffer->release(); m_instanceCullBuffer = nullptr; m_instanceCullCapacity = 0; }
    if (m_instanceCountBuffer) { m_instanceCountBuffer->release(); m_instanceCountBuffer = nullptr; m_instanceCountCapacity = 0; }
    if (m_instanceIndirectBuffer) { m_instanceIndirectBuffer->release(); m_instanceIndirectBuffer = nullptr; m_instanceIndirectCapacity = 0; }
    if (m_skinningBuffer) { m_skinningBuffer->release(); m_skinningBuffer = nullptr; }
    if (m_alphaSampler) { m_alphaSampler->release(); m_alphaSampler = nullptr; }
    m_skinningBufferCapacity = 0;
    m_skinningBufferOffset = 0;
}

void ShadowRenderPass::buildDepthState() {
    MTL::DepthStencilDescriptor* depthDesc = MTL::DepthStencilDescriptor::alloc()->init();
    depthDesc->setDepthCompareFunction(MTL::CompareFunctionLessEqual);
    depthDesc->setDepthWriteEnabled(true);
    m_depthState = m_device->newDepthStencilState(depthDesc);
    depthDesc->release();
}

bool ShadowRenderPass::allocateSkinningSlice(size_t bytes, size_t& outOffset) {
    constexpr size_t kAlignment = 256;
    size_t alignedOffset = (m_skinningBufferOffset + (kAlignment - 1)) & ~(kAlignment - 1);
    size_t required = alignedOffset + bytes;
    if (!m_skinningBuffer || required > m_skinningBufferCapacity) {
        size_t newCapacity = std::max(required, m_skinningBufferCapacity > 0 ? m_skinningBufferCapacity * 2 : required);
        if (m_skinningBuffer) {
            m_skinningBuffer->release();
        }
        m_skinningBuffer = m_device->newBuffer(newCapacity, MTL::ResourceStorageModeShared);
        m_skinningBufferCapacity = m_skinningBuffer ? m_skinningBuffer->length() : 0;
        alignedOffset = 0;
        required = bytes;
    }
    if (!m_skinningBuffer || m_skinningBufferCapacity < required) {
        return false;
    }
    outOffset = alignedOffset;
    m_skinningBufferOffset = alignedOffset + bytes;
    return true;
}

void ShadowRenderPass::buildPipelines() {
    NS::Error* error = nullptr;
    MTL::Library* lib = m_device->newDefaultLibrary();
    if (!lib) {
        std::cerr << "ShadowRenderPass: missing default Metal library\n";
        return;
    }
    
    auto buildPipeline = [&](const char* vsName, MTL::RenderPipelineState** out, bool skinned, bool includeUV, const char* fsName) {
        MTL::Function* vs = lib->newFunction(NS::String::string(vsName, NS::UTF8StringEncoding));
        if (!vs) { std::cerr << "ShadowRenderPass: missing shader " << vsName << "\n"; return; }
        MTL::Function* fs = nullptr;
        if (fsName) {
            fs = lib->newFunction(NS::String::string(fsName, NS::UTF8StringEncoding));
            if (!fs) {
                std::cerr << "ShadowRenderPass: missing shader " << fsName << "\n";
                vs->release();
                return;
            }
        }
        
        MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
        desc->setVertexFunction(vs);
        desc->setFragmentFunction(fs); // depth-only unless alpha cutout
        MTL::VertexDescriptor* vd = buildShadowVertexDescriptor(skinned, includeUV);
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
        if (fs) {
            fs->release();
        }
    };
    
    buildPipeline("shadow_dir_vertex", &m_dirPipeline, false, false, nullptr);
    buildPipeline("shadow_spot_vertex", &m_spotPipeline, false, false, nullptr);
    buildPipeline("shadow_point_vertex", &m_pointPipeline, false, false, nullptr);
    buildPipeline("shadow_area_vertex", &m_areaPipeline, false, false, nullptr);
    buildPipeline("shadow_dir_vertex_skinned", &m_dirPipelineSkinned, true, false, nullptr);
    buildPipeline("shadow_spot_vertex_skinned", &m_spotPipelineSkinned, true, false, nullptr);
    buildPipeline("shadow_point_vertex_skinned", &m_pointPipelineSkinned, true, false, nullptr);
    buildPipeline("shadow_area_vertex_skinned", &m_areaPipelineSkinned, true, false, nullptr);
    buildPipeline("shadow_dir_vertex_instanced", &m_dirPipelineInstanced, false, false, nullptr);
    buildPipeline("shadow_spot_vertex_instanced", &m_spotPipelineInstanced, false, false, nullptr);
    buildPipeline("shadow_point_vertex_instanced", &m_pointPipelineInstanced, false, false, nullptr);
    buildPipeline("shadow_area_vertex_instanced", &m_areaPipelineInstanced, false, false, nullptr);

    buildPipeline("shadow_dir_vertex_cutout", &m_dirPipelineCutout, false, true, "shadow_alpha_fragment");
    buildPipeline("shadow_spot_vertex_cutout", &m_spotPipelineCutout, false, true, "shadow_alpha_fragment");
    buildPipeline("shadow_point_vertex_cutout", &m_pointPipelineCutout, false, true, "shadow_alpha_fragment");
    buildPipeline("shadow_area_vertex_cutout", &m_areaPipelineCutout, false, true, "shadow_alpha_fragment");
    buildPipeline("shadow_dir_vertex_cutout_skinned", &m_dirPipelineSkinnedCutout, true, true, "shadow_alpha_fragment");
    buildPipeline("shadow_spot_vertex_cutout_skinned", &m_spotPipelineSkinnedCutout, true, true, "shadow_alpha_fragment");
    buildPipeline("shadow_point_vertex_cutout_skinned", &m_pointPipelineSkinnedCutout, true, true, "shadow_alpha_fragment");
    buildPipeline("shadow_area_vertex_cutout_skinned", &m_areaPipelineSkinnedCutout, true, true, "shadow_alpha_fragment");
    buildPipeline("shadow_dir_vertex_cutout_instanced", &m_dirPipelineInstancedCutout, false, true, "shadow_alpha_fragment");
    buildPipeline("shadow_spot_vertex_cutout_instanced", &m_spotPipelineInstancedCutout, false, true, "shadow_alpha_fragment");
    buildPipeline("shadow_point_vertex_cutout_instanced", &m_pointPipelineInstancedCutout, false, true, "shadow_alpha_fragment");
    buildPipeline("shadow_area_vertex_cutout_instanced", &m_areaPipelineInstancedCutout, false, true, "shadow_alpha_fragment");

    MTL::Function* cullFn = lib->newFunction(NS::String::string("instance_cull", NS::UTF8StringEncoding));
    MTL::Function* indirectFn = lib->newFunction(NS::String::string("instance_build_indirect", NS::UTF8StringEncoding));
    if (cullFn && indirectFn) {
        NS::Error* computeErr = nullptr;
        m_instanceCullPipeline = m_device->newComputePipelineState(cullFn, &computeErr);
        if (!m_instanceCullPipeline && computeErr) {
            std::cerr << "ShadowRenderPass: instance cull compute error " << computeErr->localizedDescription()->utf8String() << "\n";
        }
        computeErr = nullptr;
        m_instanceIndirectPipeline = m_device->newComputePipelineState(indirectFn, &computeErr);
        if (!m_instanceIndirectPipeline && computeErr) {
            std::cerr << "ShadowRenderPass: instance indirect compute error " << computeErr->localizedDescription()->utf8String() << "\n";
        }
    } else {
        std::cerr << "ShadowRenderPass: missing instance cull compute shaders\n";
    }
    if (cullFn) cullFn->release();
    if (indirectFn) indirectFn->release();
    
    lib->release();
}

void ShadowRenderPass::execute(MTL::CommandBuffer* cmdBuffer,
                               Scene* scene,
                               Camera* camera,
                               const LightingSystem& lighting,
                               const std::vector<InstancedShadowDraw>& instancedDraws) {
    if (!cmdBuffer || !scene || !camera || !m_shadowAtlas) {
        return;
    }

    m_cameraPosition = camera->getEntity()->getTransform()->getPosition();
    m_timeSeconds = Time::time();
    m_skinningBufferOffset = 0;

    m_hlodHidden.clear();
    m_hlodActiveProxies.clear();
    {
        const auto& hlodEntities = scene->getAllEntities();
        for (const auto& entityPtr : hlodEntities) {
            Entity* entity = entityPtr.get();
            if (!entity || !entity->isActiveInHierarchy()) {
                continue;
            }
            HLODProxy* proxy = entity->getComponent<HLODProxy>();
            if (!proxy || !proxy->isEnabled()) {
                continue;
            }
            MeshRenderer* mr = entity->getComponent<MeshRenderer>();
            if (!mr || !mr->isEnabled() || !mr->getMesh()) {
                continue;
            }
            Math::Vector3 center = mr->getBoundsCenter();
            float dist = (center - m_cameraPosition).length();
            if (dist < proxy->getLodStart()) {
                continue;
            }
            std::string proxyId = entity->getUUID().toString();
            m_hlodActiveProxies.insert(proxyId);
            for (const auto& src : proxy->getSourceUuids()) {
                m_hlodHidden.insert(src);
            }
        }
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

    if (!instancedDraws.empty()) {
        const auto& cascades = lighting.getCascades();
        if (!cascades.empty()) {
            for (size_t i = 0; i < cascades.size(); ++i) {
                const auto& slice = cascades[i];
                if (!slice.atlas.valid || !m_dirPipelineInstanced) {
                    continue;
                }
                ShadowGPUData tempShadow{};
                tempShadow.viewProj = slice.viewProj;
                ShadowAtlasTile tile = slice.atlas;
                renderInstancedRange(cmdBuffer, tempShadow, tile, m_dirPipelineInstanced, m_dirPipelineInstancedCutout, instancedDraws);
            }
        }

        // Render instanced local shadows
        const auto& lights = lighting.getGPULights();
        const auto& shadows = lighting.getGPUShadows();
        for (size_t i = 0; i < lights.size(); ++i) {
            const LightGPUData& lgpu = lights[i];
            int shadowIdx = static_cast<int>(lgpu.shadowCookie.x);
            if (shadowIdx < 0 || shadowIdx >= static_cast<int>(shadows.size())) {
                continue;
            }
            int type = static_cast<int>(lgpu.directionType.w);
            if (type == 1) {
                continue;
            }
            const ShadowGPUData& s = shadows[shadowIdx];
            ShadowAtlasTile tile{};
            tile.valid = true;
            tile.x = static_cast<uint32_t>(s.atlasUV.x * m_atlasResolution);
            tile.y = static_cast<uint32_t>(s.atlasUV.y * m_atlasResolution);
            tile.size = static_cast<uint32_t>(s.atlasUV.z * m_atlasResolution);
            tile.layer = static_cast<uint32_t>(s.depthRange.w);

            MTL::RenderPipelineState* pipelineInstanced = nullptr;
            switch (type) {
                case 2: pipelineInstanced = m_spotPipelineInstanced; break;
                case 3:
                case 4: pipelineInstanced = m_areaPipelineInstanced; break;
                default: pipelineInstanced = nullptr; break;
            }
            if (!pipelineInstanced) {
                continue;
            }
            MTL::RenderPipelineState* pipelineInstancedCutout = nullptr;
            switch (type) {
                case 2: pipelineInstancedCutout = m_spotPipelineInstancedCutout; break;
                case 3:
                case 4: pipelineInstancedCutout = m_areaPipelineInstancedCutout; break;
                default: pipelineInstancedCutout = nullptr; break;
            }
            renderInstancedRange(cmdBuffer, s, tile, pipelineInstanced, pipelineInstancedCutout, instancedDraws);
        }

        // Render instanced point shadows
        if (m_pointPipelineInstanced) {
            const auto& prepared = lighting.getPreparedLights();
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

                for (int face = 0; face < 6; ++face) {
                    Math::Vector3 lightPos = prepared[i].positionWS;
                    Math::Matrix4x4 view = Math::Matrix4x4::LookAt(lightPos, lightPos + faceDirs[face], faceUps[face]);
                    Math::Matrix4x4 proj = Math::Matrix4x4::Perspective(Math::HALF_PI, 1.0f, s.depthRange.x, s.depthRange.y);
                    Math::Matrix4x4 vp = proj * view;
                    renderInstancedCubeFace(cmdBuffer, cubeTex, cubeIndex * 6 + face, res, vp, m_pointPipelineInstanced, m_pointPipelineInstancedCutout, instancedDraws);
                }
            }
        }
    }
}

void ShadowRenderPass::setExtraHiddenEntities(const std::unordered_set<std::string>& hidden) {
    m_extraHidden = hidden;
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
            if (shouldSkipEntity(e)) continue;
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

            std::shared_ptr<Material> material = mr->getMaterial(0);
            bool isCutout = IsCutoutMaterial(material);
            enc->setCullMode(ResolveCullMode(material));
            MTL::RenderPipelineState* desiredPipeline = useSkinned
                ? (isCutout && m_dirPipelineSkinnedCutout ? m_dirPipelineSkinnedCutout : m_dirPipelineSkinned)
                : (isCutout && m_dirPipelineCutout ? m_dirPipelineCutout : m_dirPipeline);
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
                size_t bufferOffset = 0;
                if (allocateSkinningSlice(bytes, bufferOffset)) {
                    std::memcpy(static_cast<char*>(m_skinningBuffer->contents()) + bufferOffset,
                                boneMatrices.data(),
                                bytes);
                    enc->setVertexBuffer(m_skinningBuffer, bufferOffset, 2);
                }
            }
            if (isCutout && (desiredPipeline == m_dirPipelineCutout || desiredPipeline == m_dirPipelineSkinnedCutout)) {
                BindShadowAlpha(enc, material, m_alphaSampler);
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
        renderLightRange(cmdBuffer, scene, s, dummyTile, pipeline, pipelineSkinned,
                         pipeline == m_spotPipeline ? m_spotPipelineCutout : m_areaPipelineCutout,
                         pipeline == m_spotPipeline ? m_spotPipelineSkinnedCutout : m_areaPipelineSkinnedCutout);
    }
}

void ShadowRenderPass::renderLightRange(MTL::CommandBuffer* cmdBuffer,
                                        Scene* scene,
                                        const ShadowGPUData& shadow,
                                        const ShadowAtlasTile& tile,
                                        MTL::RenderPipelineState* pipeline,
                                        MTL::RenderPipelineState* pipelineSkinned,
                                        MTL::RenderPipelineState* pipelineCutout,
                                        MTL::RenderPipelineState* pipelineSkinnedCutout) {
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
            if (shouldSkipEntity(e)) continue;
            MeshRenderer* mr = e->getComponent<MeshRenderer>();
            if (!mr || !mr->isEnabled() || !mr->getCastShadows()) continue;
            std::shared_ptr<Mesh> mesh = mr->getMesh();
            if (!mesh || !mesh->isUploaded()) continue;

            SkinnedMeshRenderer* skinned = e->getComponent<SkinnedMeshRenderer>();
            bool wantsSkin = skinned && skinned->isEnabled() && mesh->hasSkinWeights() && !skinned->getBoneMatrices().empty();
            MTL::Buffer* skinBuffer = static_cast<MTL::Buffer*>(mesh->getSkinWeightBuffer());
            bool useSkinned = wantsSkin && skinBuffer && pipelineSkinned;
            std::shared_ptr<Material> material = mr->getMaterial(0);
            bool isCutout = IsCutoutMaterial(material);
            enc->setCullMode(ResolveCullMode(material));
            MTL::RenderPipelineState* desiredPipeline = useSkinned
                ? (isCutout && pipelineSkinnedCutout ? pipelineSkinnedCutout : pipelineSkinned)
                : (isCutout && pipelineCutout ? pipelineCutout : pipeline);
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
                size_t bufferOffset = 0;
                if (allocateSkinningSlice(bytes, bufferOffset)) {
                    std::memcpy(static_cast<char*>(m_skinningBuffer->contents()) + bufferOffset,
                                boneMatrices.data(),
                                bytes);
                    enc->setVertexBuffer(m_skinningBuffer, bufferOffset, 2);
                }
            }
            if (isCutout && (desiredPipeline == pipelineCutout || desiredPipeline == pipelineSkinnedCutout)) {
                BindShadowAlpha(enc, material, m_alphaSampler);
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

bool ShadowRenderPass::shouldSkipEntity(Entity* entity) const {
    if (!entity) {
        return true;
    }
    std::string id = entity->getUUID().toString();
    if (m_extraHidden.find(id) != m_extraHidden.end()) {
        return true;
    }
    if (m_hlodHidden.find(id) != m_hlodHidden.end()) {
        return true;
    }
    if (entity->getComponent<HLODProxy>()) {
        return m_hlodActiveProxies.find(id) == m_hlodActiveProxies.end();
    }
    return false;
}

void ShadowRenderPass::renderInstancedRange(MTL::CommandBuffer* cmdBuffer,
                                            const ShadowGPUData& shadow,
                                            const ShadowAtlasTile& tile,
                                            MTL::RenderPipelineState* pipeline,
                                            MTL::RenderPipelineState* pipelineCutout,
                                            const std::vector<InstancedShadowDraw>& instancedDraws) {
    if (!tile.valid || !pipeline || instancedDraws.empty()) {
        return;
    }

    auto buildFoliageParams = [&](const InstancedShadowDraw& draw) {
        ShadowFoliageParamsCPU params{};
        if (draw.material) {
            Math::Vector3 windDir = draw.material->getWindDirection();
            params.foliageParams0 = Math::Vector4(
                draw.material->getWindStrength(),
                draw.material->getWindSpeed(),
                draw.material->getWindScale(),
                draw.material->getWindGust()
            );
            params.foliageParams1 = Math::Vector4(
                draw.material->getLodFadeStart(),
                draw.material->getLodFadeEnd(),
                draw.material->getBillboardStart(),
                draw.material->getBillboardEnd()
            );
            params.foliageParams2 = Math::Vector4(
                draw.material->getWindEnabled() ? 1.0f : 0.0f,
                draw.material->getLodFadeEnabled() ? 1.0f : 0.0f,
                draw.material->getBillboardEnabled() ? 1.0f : 0.0f,
                draw.material->getDitherEnabled() ? 1.0f : 0.0f
            );
            params.foliageParams3 = Math::Vector4(windDir.x, windDir.y, windDir.z, 0.0f);
        } else {
            params.foliageParams0 = Math::Vector4(0.0f);
            params.foliageParams1 = Math::Vector4(0.0f);
            params.foliageParams2 = Math::Vector4(0.0f);
            params.foliageParams3 = Math::Vector4(0.0f);
        }
        params.cameraTime = Math::Vector4(m_cameraPosition.x, m_cameraPosition.y, m_cameraPosition.z, m_timeSeconds);
        params.boundsCenter = Math::Vector4(draw.boundsCenter.x, draw.boundsCenter.y, draw.boundsCenter.z, 0.0f);
        params.boundsSize = Math::Vector4(draw.boundsSize.x, draw.boundsSize.y, draw.boundsSize.z, 0.0f);
        params.flags = Math::Vector4(draw.isBillboard ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
        return params;
    };

    if (!m_instanceCullPipeline || !m_instanceIndirectPipeline) {
        // Fall back to non-culled draw path
        MTL::RenderPassDescriptor* rp = MTL::RenderPassDescriptor::alloc()->init();
        rp->depthAttachment()->setTexture(m_shadowAtlas);
        rp->depthAttachment()->setSlice(tile.layer);
        rp->depthAttachment()->setLoadAction(MTL::LoadActionLoad);
        rp->depthAttachment()->setStoreAction(MTL::StoreActionStore);
        rp->depthAttachment()->setClearDepth(1.0);

        MTL::RenderCommandEncoder* enc = cmdBuffer->renderCommandEncoder(rp);
        enc->setDepthStencilState(m_depthState);
        enc->setViewport({double(tile.x), double(tile.y), double(tile.size), double(tile.size), 0.0, 1.0});
        enc->setRenderPipelineState(pipeline);
        MTL::RenderPipelineState* currentPipeline = pipeline;
        enc->setVertexBytes(&shadow.viewProj, sizeof(Math::Matrix4x4), 1);

        for (const auto& draw : instancedDraws) {
            if (!draw.mesh || draw.instanceCount == 0 || !draw.instanceBuffer) {
                continue;
            }
            bool isCutout = IsCutoutMaterial(draw.material);
            if (isCutout && pipelineCutout) {
                if (currentPipeline != pipelineCutout) {
                    enc->setRenderPipelineState(pipelineCutout);
                    currentPipeline = pipelineCutout;
                }
                BindShadowAlpha(enc, draw.material, m_alphaSampler);
            } else if (currentPipeline != pipeline) {
                enc->setRenderPipelineState(pipeline);
                currentPipeline = pipeline;
            }
            enc->setCullMode(ResolveCullMode(draw.material));
            MTL::Buffer* vertexBuffer = static_cast<MTL::Buffer*>(draw.mesh->getVertexBuffer());
            MTL::Buffer* indexBuffer = static_cast<MTL::Buffer*>(draw.mesh->getIndexBuffer());
            if (!vertexBuffer || !indexBuffer) {
                continue;
            }
            enc->setVertexBuffer(vertexBuffer, 0, 0);
            enc->setVertexBuffer(draw.instanceBuffer, draw.instanceOffset, 2);
            ShadowFoliageParamsCPU foliage = buildFoliageParams(draw);
            enc->setVertexBytes(&foliage, sizeof(ShadowFoliageParamsCPU), 3);
            enc->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle,
                                       draw.mesh->getIndices().size(),
                                       MTL::IndexTypeUInt32,
                                       indexBuffer,
                                       0,
                                       draw.instanceCount);
        }

        enc->endEncoding();
        rp->release();
        return;
    }

    const size_t drawCount = instancedDraws.size();
    if (drawCount == 0) {
        return;
    }

    size_t totalInstances = 0;
    for (const auto& draw : instancedDraws) {
        totalInstances += draw.instanceCount;
    }
    size_t neededCullBytes = totalInstances * sizeof(InstanceDataCPU);
    if (!m_instanceCullBuffer || m_instanceCullCapacity < neededCullBytes) {
        size_t newCapacity = std::max(neededCullBytes, m_instanceCullCapacity > 0 ? m_instanceCullCapacity * 2 : neededCullBytes);
        if (m_instanceCullBuffer) {
            m_instanceCullBuffer->release();
        }
        m_instanceCullBuffer = m_device->newBuffer(newCapacity, MTL::ResourceStorageModeShared);
        m_instanceCullCapacity = m_instanceCullBuffer ? m_instanceCullBuffer->length() : 0;
    }

    size_t counterBytes = std::max<size_t>(drawCount * sizeof(uint32_t), 256);
    if (!m_instanceCountBuffer || m_instanceCountCapacity < counterBytes) {
        if (m_instanceCountBuffer) {
            m_instanceCountBuffer->release();
        }
        m_instanceCountBuffer = m_device->newBuffer(counterBytes, MTL::ResourceStorageModeShared);
        m_instanceCountCapacity = m_instanceCountBuffer ? m_instanceCountBuffer->length() : 0;
    }

    size_t indirectBytes = std::max<size_t>(drawCount * sizeof(DrawIndexedIndirectArgs), 256);
    if (!m_instanceIndirectBuffer || m_instanceIndirectCapacity < indirectBytes) {
        if (m_instanceIndirectBuffer) {
            m_instanceIndirectBuffer->release();
        }
        m_instanceIndirectBuffer = m_device->newBuffer(indirectBytes, MTL::ResourceStorageModeShared);
        m_instanceIndirectCapacity = m_instanceIndirectBuffer ? m_instanceIndirectBuffer->length() : 0;
    }

    if (!m_instanceCullBuffer || !m_instanceCountBuffer || !m_instanceIndirectBuffer) {
        return;
    }

    std::memset(m_instanceCountBuffer->contents(), 0, drawCount * sizeof(uint32_t));
    auto* args = static_cast<DrawIndexedIndirectArgs*>(m_instanceIndirectBuffer->contents());
    for (size_t i = 0; i < drawCount; ++i) {
        args[i].indexCount = instancedDraws[i].mesh ? instancedDraws[i].mesh->getIndices().size() : 0;
        args[i].instanceCount = 0;
        args[i].indexStart = 0;
        args[i].baseVertex = 0;
        args[i].baseInstance = 0;
    }

    auto planes = extractFrustumPlanes(shadow.viewProj);

    MTL::ComputeCommandEncoder* cullEncoder = cmdBuffer->computeCommandEncoder();
    cullEncoder->setComputePipelineState(m_instanceCullPipeline);

    uint32_t outputOffset = 0;
    for (size_t i = 0; i < drawCount; ++i) {
        const auto& draw = instancedDraws[i];
        if (!draw.mesh || draw.instanceCount == 0 || !draw.instanceBuffer) {
            continue;
        }

        Math::Vector3 meshCenter = draw.boundsCenter;
        Math::Vector3 meshSize = draw.boundsSize;
        float baseRadius = 0.5f * meshSize.length() * 0.85f;

        InstanceCullParamsCPU params{};
        for (int p = 0; p < 6; ++p) {
            params.frustumPlanes[p] = planes[p];
        }
        params.boundsCenterRadius = Math::Vector4(meshCenter.x, meshCenter.y, meshCenter.z, baseRadius);
        params.inputOffset = static_cast<uint32_t>(draw.instanceOffset / sizeof(InstanceDataCPU));
        params.outputOffset = outputOffset;
        params.instanceCount = draw.instanceCount;

        cullEncoder->setBuffer(draw.instanceBuffer, 0, 0);
        cullEncoder->setBuffer(m_instanceCullBuffer, 0, 1);
        cullEncoder->setBuffer(m_instanceCountBuffer, i * sizeof(uint32_t), 2);
        cullEncoder->setBytes(&params, sizeof(InstanceCullParamsCPU), 3);

        const uint32_t threads = 64;
        const uint32_t grid = (draw.instanceCount + threads - 1) / threads;
        cullEncoder->dispatchThreads(MTL::Size(grid * threads, 1, 1), MTL::Size(threads, 1, 1));

        outputOffset += draw.instanceCount;
    }
    cullEncoder->endEncoding();

    MTL::ComputeCommandEncoder* indirectEncoder = cmdBuffer->computeCommandEncoder();
    indirectEncoder->setComputePipelineState(m_instanceIndirectPipeline);
    indirectEncoder->setBuffer(m_instanceCountBuffer, 0, 0);
    indirectEncoder->setBuffer(m_instanceIndirectBuffer, 0, 1);
    const uint32_t threads = 64;
    const uint32_t grid = (drawCount + threads - 1) / threads;
    indirectEncoder->dispatchThreads(MTL::Size(grid * threads, 1, 1), MTL::Size(threads, 1, 1));
    indirectEncoder->endEncoding();

    MTL::RenderPassDescriptor* rp = MTL::RenderPassDescriptor::alloc()->init();
    rp->depthAttachment()->setTexture(m_shadowAtlas);
    rp->depthAttachment()->setSlice(tile.layer);
    rp->depthAttachment()->setLoadAction(MTL::LoadActionLoad);
    rp->depthAttachment()->setStoreAction(MTL::StoreActionStore);
    rp->depthAttachment()->setClearDepth(1.0);

    MTL::RenderCommandEncoder* enc = cmdBuffer->renderCommandEncoder(rp);
    enc->setDepthStencilState(m_depthState);
    enc->setViewport({double(tile.x), double(tile.y), double(tile.size), double(tile.size), 0.0, 1.0});
    enc->setRenderPipelineState(pipeline);
    MTL::RenderPipelineState* currentPipeline = pipeline;
    enc->setVertexBytes(&shadow.viewProj, sizeof(Math::Matrix4x4), 1);

    outputOffset = 0;
    for (size_t i = 0; i < drawCount; ++i) {
        const auto& draw = instancedDraws[i];
        if (!draw.mesh || draw.instanceCount == 0) {
            continue;
        }
        bool isCutout = IsCutoutMaterial(draw.material);
        if (isCutout && pipelineCutout) {
            if (currentPipeline != pipelineCutout) {
                enc->setRenderPipelineState(pipelineCutout);
                currentPipeline = pipelineCutout;
            }
            BindShadowAlpha(enc, draw.material, m_alphaSampler);
        } else if (currentPipeline != pipeline) {
            enc->setRenderPipelineState(pipeline);
            currentPipeline = pipeline;
        }
        enc->setCullMode(ResolveCullMode(draw.material));
        MTL::Buffer* vertexBuffer = static_cast<MTL::Buffer*>(draw.mesh->getVertexBuffer());
        MTL::Buffer* indexBuffer = static_cast<MTL::Buffer*>(draw.mesh->getIndexBuffer());
        if (!vertexBuffer || !indexBuffer) {
            continue;
        }
        enc->setVertexBuffer(vertexBuffer, 0, 0);
        enc->setVertexBuffer(m_instanceCullBuffer, outputOffset * sizeof(InstanceDataCPU), 2);
        ShadowFoliageParamsCPU foliage = buildFoliageParams(draw);
        enc->setVertexBytes(&foliage, sizeof(ShadowFoliageParamsCPU), 3);
        enc->drawIndexedPrimitives(
            MTL::PrimitiveTypeTriangle,
            MTL::IndexTypeUInt32,
            indexBuffer,
            0,
            m_instanceIndirectBuffer,
            i * sizeof(DrawIndexedIndirectArgs)
        );
        outputOffset += draw.instanceCount;
    }

    enc->endEncoding();
    rp->release();
}

void ShadowRenderPass::renderInstancedCubeFace(MTL::CommandBuffer* cmdBuffer,
                                               MTL::Texture* target,
                                               uint32_t slice,
                                               uint32_t resolution,
                                               const Math::Matrix4x4& viewProj,
                                               MTL::RenderPipelineState* pipeline,
                                               MTL::RenderPipelineState* pipelineCutout,
                                               const std::vector<InstancedShadowDraw>& instancedDraws) {
    if (!target || !pipeline || instancedDraws.empty()) {
        return;
    }

    auto buildFoliageParams = [&](const InstancedShadowDraw& draw) {
        ShadowFoliageParamsCPU params{};
        if (draw.material) {
            Math::Vector3 windDir = draw.material->getWindDirection();
            params.foliageParams0 = Math::Vector4(
                draw.material->getWindStrength(),
                draw.material->getWindSpeed(),
                draw.material->getWindScale(),
                draw.material->getWindGust()
            );
            params.foliageParams1 = Math::Vector4(
                draw.material->getLodFadeStart(),
                draw.material->getLodFadeEnd(),
                draw.material->getBillboardStart(),
                draw.material->getBillboardEnd()
            );
            params.foliageParams2 = Math::Vector4(
                draw.material->getWindEnabled() ? 1.0f : 0.0f,
                draw.material->getLodFadeEnabled() ? 1.0f : 0.0f,
                draw.material->getBillboardEnabled() ? 1.0f : 0.0f,
                draw.material->getDitherEnabled() ? 1.0f : 0.0f
            );
            params.foliageParams3 = Math::Vector4(windDir.x, windDir.y, windDir.z, 0.0f);
        } else {
            params.foliageParams0 = Math::Vector4(0.0f);
            params.foliageParams1 = Math::Vector4(0.0f);
            params.foliageParams2 = Math::Vector4(0.0f);
            params.foliageParams3 = Math::Vector4(0.0f);
        }
        params.cameraTime = Math::Vector4(m_cameraPosition.x, m_cameraPosition.y, m_cameraPosition.z, m_timeSeconds);
        params.boundsCenter = Math::Vector4(draw.boundsCenter.x, draw.boundsCenter.y, draw.boundsCenter.z, 0.0f);
        params.boundsSize = Math::Vector4(draw.boundsSize.x, draw.boundsSize.y, draw.boundsSize.z, 0.0f);
        params.flags = Math::Vector4(draw.isBillboard ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
        return params;
    };

    if (!m_instanceCullPipeline || !m_instanceIndirectPipeline) {
        MTL::RenderPassDescriptor* rp = MTL::RenderPassDescriptor::alloc()->init();
        rp->depthAttachment()->setTexture(target);
        rp->depthAttachment()->setSlice(slice);
        rp->depthAttachment()->setLoadAction(MTL::LoadActionLoad);
        rp->depthAttachment()->setStoreAction(MTL::StoreActionStore);

        MTL::RenderCommandEncoder* enc = cmdBuffer->renderCommandEncoder(rp);
        enc->setDepthStencilState(m_depthState);
        enc->setViewport({0.0, 0.0, (double)resolution, (double)resolution, 0.0, 1.0});
        enc->setRenderPipelineState(pipeline);
        MTL::RenderPipelineState* currentPipeline = pipeline;
        enc->setVertexBytes(&viewProj, sizeof(Math::Matrix4x4), 1);

        for (const auto& draw : instancedDraws) {
            if (!draw.mesh || draw.instanceCount == 0 || !draw.instanceBuffer) {
                continue;
            }
            bool isCutout = IsCutoutMaterial(draw.material);
            if (isCutout && pipelineCutout) {
                if (currentPipeline != pipelineCutout) {
                    enc->setRenderPipelineState(pipelineCutout);
                    currentPipeline = pipelineCutout;
                }
                BindShadowAlpha(enc, draw.material, m_alphaSampler);
            } else if (currentPipeline != pipeline) {
                enc->setRenderPipelineState(pipeline);
                currentPipeline = pipeline;
            }
            enc->setCullMode(ResolveCullMode(draw.material));
            MTL::Buffer* vertexBuffer = static_cast<MTL::Buffer*>(draw.mesh->getVertexBuffer());
            MTL::Buffer* indexBuffer = static_cast<MTL::Buffer*>(draw.mesh->getIndexBuffer());
            if (!vertexBuffer || !indexBuffer) {
                continue;
            }
            enc->setVertexBuffer(vertexBuffer, 0, 0);
            enc->setVertexBuffer(draw.instanceBuffer, draw.instanceOffset, 2);
            ShadowFoliageParamsCPU foliage = buildFoliageParams(draw);
            enc->setVertexBytes(&foliage, sizeof(ShadowFoliageParamsCPU), 3);
            enc->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle,
                                       draw.mesh->getIndices().size(),
                                       MTL::IndexTypeUInt32,
                                       indexBuffer,
                                       0,
                                       draw.instanceCount);
        }

        enc->endEncoding();
        rp->release();
        return;
    }

    const size_t drawCount = instancedDraws.size();
    size_t totalInstances = 0;
    for (const auto& draw : instancedDraws) {
        totalInstances += draw.instanceCount;
    }
    size_t neededCullBytes = totalInstances * sizeof(InstanceDataCPU);
    if (!m_instanceCullBuffer || m_instanceCullCapacity < neededCullBytes) {
        size_t newCapacity = std::max(neededCullBytes, m_instanceCullCapacity > 0 ? m_instanceCullCapacity * 2 : neededCullBytes);
        if (m_instanceCullBuffer) {
            m_instanceCullBuffer->release();
        }
        m_instanceCullBuffer = m_device->newBuffer(newCapacity, MTL::ResourceStorageModeShared);
        m_instanceCullCapacity = m_instanceCullBuffer ? m_instanceCullBuffer->length() : 0;
    }

    size_t counterBytes = std::max<size_t>(drawCount * sizeof(uint32_t), 256);
    if (!m_instanceCountBuffer || m_instanceCountCapacity < counterBytes) {
        if (m_instanceCountBuffer) {
            m_instanceCountBuffer->release();
        }
        m_instanceCountBuffer = m_device->newBuffer(counterBytes, MTL::ResourceStorageModeShared);
        m_instanceCountCapacity = m_instanceCountBuffer ? m_instanceCountBuffer->length() : 0;
    }

    size_t indirectBytes = std::max<size_t>(drawCount * sizeof(DrawIndexedIndirectArgs), 256);
    if (!m_instanceIndirectBuffer || m_instanceIndirectCapacity < indirectBytes) {
        if (m_instanceIndirectBuffer) {
            m_instanceIndirectBuffer->release();
        }
        m_instanceIndirectBuffer = m_device->newBuffer(indirectBytes, MTL::ResourceStorageModeShared);
        m_instanceIndirectCapacity = m_instanceIndirectBuffer ? m_instanceIndirectBuffer->length() : 0;
    }

    if (!m_instanceCullBuffer || !m_instanceCountBuffer || !m_instanceIndirectBuffer) {
        return;
    }

    std::memset(m_instanceCountBuffer->contents(), 0, drawCount * sizeof(uint32_t));
    auto* args = static_cast<DrawIndexedIndirectArgs*>(m_instanceIndirectBuffer->contents());
    for (size_t i = 0; i < drawCount; ++i) {
        args[i].indexCount = instancedDraws[i].mesh ? instancedDraws[i].mesh->getIndices().size() : 0;
        args[i].instanceCount = 0;
        args[i].indexStart = 0;
        args[i].baseVertex = 0;
        args[i].baseInstance = 0;
    }

    auto planes = extractFrustumPlanes(viewProj);

    MTL::ComputeCommandEncoder* cullEncoder = cmdBuffer->computeCommandEncoder();
    cullEncoder->setComputePipelineState(m_instanceCullPipeline);

    uint32_t outputOffset = 0;
    for (size_t i = 0; i < drawCount; ++i) {
        const auto& draw = instancedDraws[i];
        if (!draw.mesh || draw.instanceCount == 0 || !draw.instanceBuffer) {
            continue;
        }

        Math::Vector3 meshCenter = draw.boundsCenter;
        Math::Vector3 meshSize = draw.boundsSize;
        float baseRadius = 0.5f * meshSize.length() * 0.85f;

        InstanceCullParamsCPU params{};
        for (int p = 0; p < 6; ++p) {
            params.frustumPlanes[p] = planes[p];
        }
        params.boundsCenterRadius = Math::Vector4(meshCenter.x, meshCenter.y, meshCenter.z, baseRadius);
        params.inputOffset = static_cast<uint32_t>(draw.instanceOffset / sizeof(InstanceDataCPU));
        params.outputOffset = outputOffset;
        params.instanceCount = draw.instanceCount;

        cullEncoder->setBuffer(draw.instanceBuffer, 0, 0);
        cullEncoder->setBuffer(m_instanceCullBuffer, 0, 1);
        cullEncoder->setBuffer(m_instanceCountBuffer, i * sizeof(uint32_t), 2);
        cullEncoder->setBytes(&params, sizeof(InstanceCullParamsCPU), 3);

        const uint32_t threads = 64;
        const uint32_t grid = (draw.instanceCount + threads - 1) / threads;
        cullEncoder->dispatchThreads(MTL::Size(grid * threads, 1, 1), MTL::Size(threads, 1, 1));

        outputOffset += draw.instanceCount;
    }
    cullEncoder->endEncoding();

    MTL::ComputeCommandEncoder* indirectEncoder = cmdBuffer->computeCommandEncoder();
    indirectEncoder->setComputePipelineState(m_instanceIndirectPipeline);
    indirectEncoder->setBuffer(m_instanceCountBuffer, 0, 0);
    indirectEncoder->setBuffer(m_instanceIndirectBuffer, 0, 1);
    const uint32_t threads = 64;
    const uint32_t grid = (drawCount + threads - 1) / threads;
    indirectEncoder->dispatchThreads(MTL::Size(grid * threads, 1, 1), MTL::Size(threads, 1, 1));
    indirectEncoder->endEncoding();

    MTL::RenderPassDescriptor* rp = MTL::RenderPassDescriptor::alloc()->init();
    rp->depthAttachment()->setTexture(target);
    rp->depthAttachment()->setSlice(slice);
    rp->depthAttachment()->setLoadAction(MTL::LoadActionLoad);
    rp->depthAttachment()->setStoreAction(MTL::StoreActionStore);

    MTL::RenderCommandEncoder* enc = cmdBuffer->renderCommandEncoder(rp);
    enc->setDepthStencilState(m_depthState);
    enc->setViewport({0.0, 0.0, (double)resolution, (double)resolution, 0.0, 1.0});
    enc->setRenderPipelineState(pipeline);
    MTL::RenderPipelineState* currentPipeline = pipeline;
    enc->setVertexBytes(&viewProj, sizeof(Math::Matrix4x4), 1);

    outputOffset = 0;
    for (size_t i = 0; i < drawCount; ++i) {
        const auto& draw = instancedDraws[i];
        if (!draw.mesh || draw.instanceCount == 0) {
            continue;
        }
        bool isCutout = IsCutoutMaterial(draw.material);
        if (isCutout && pipelineCutout) {
            if (currentPipeline != pipelineCutout) {
                enc->setRenderPipelineState(pipelineCutout);
                currentPipeline = pipelineCutout;
            }
            BindShadowAlpha(enc, draw.material, m_alphaSampler);
        } else if (currentPipeline != pipeline) {
            enc->setRenderPipelineState(pipeline);
            currentPipeline = pipeline;
        }
        enc->setCullMode(ResolveCullMode(draw.material));
        MTL::Buffer* vertexBuffer = static_cast<MTL::Buffer*>(draw.mesh->getVertexBuffer());
        MTL::Buffer* indexBuffer = static_cast<MTL::Buffer*>(draw.mesh->getIndexBuffer());
        if (!vertexBuffer || !indexBuffer) {
            continue;
        }
        enc->setVertexBuffer(vertexBuffer, 0, 0);
        enc->setVertexBuffer(m_instanceCullBuffer, outputOffset * sizeof(InstanceDataCPU), 2);
        ShadowFoliageParamsCPU foliage = buildFoliageParams(draw);
        enc->setVertexBytes(&foliage, sizeof(ShadowFoliageParamsCPU), 3);
        enc->drawIndexedPrimitives(
            MTL::PrimitiveTypeTriangle,
            MTL::IndexTypeUInt32,
            indexBuffer,
            0,
            m_instanceIndirectBuffer,
            i * sizeof(DrawIndexedIndirectArgs)
        );
        outputOffset += draw.instanceCount;
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
            if (shouldSkipEntity(e)) continue;
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
                std::shared_ptr<Material> material = mr->getMaterial(0);
                bool isCutout = IsCutoutMaterial(material);
                enc->setCullMode(ResolveCullMode(material));
                MTL::RenderPipelineState* desiredPipeline = useSkinned
                    ? (isCutout && m_pointPipelineSkinnedCutout ? m_pointPipelineSkinnedCutout : m_pointPipelineSkinned)
                    : (isCutout && m_pointPipelineCutout ? m_pointPipelineCutout : m_pointPipeline);
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
                    size_t bufferOffset = 0;
                    if (allocateSkinningSlice(bytes, bufferOffset)) {
                        std::memcpy(static_cast<char*>(m_skinningBuffer->contents()) + bufferOffset,
                                    boneMatrices.data(),
                                    bytes);
                        enc->setVertexBuffer(m_skinningBuffer, bufferOffset, 2);
                    }
                }
                if (isCutout && (desiredPipeline == m_pointPipelineCutout || desiredPipeline == m_pointPipelineSkinnedCutout)) {
                    BindShadowAlpha(enc, material, m_alphaSampler);
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
