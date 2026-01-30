#include "Renderer.hpp"
#include "../Rendering/Mesh.hpp"
#include "../Rendering/Material.hpp"
#include "../Rendering/Texture.hpp"
#include "../Rendering/stb_image_write.h"
#include "../Components/Camera.hpp"
#include "../Components/Light.hpp"
#include "../Components/MeshRenderer.hpp"
#include "../Components/SkinnedMeshRenderer.hpp"
#include "../Components/InstancedMeshRenderer.hpp"
#include "../Components/Decal.hpp"
#include "../Components/HLODProxy.hpp"
#include "../Scene/Scene.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Core/Time.hpp"
#include "../ECS/Entity.hpp"
#include "../ECS/Transform.hpp"
#include "../Assets/AssetDatabase.hpp"
#include "DebugRenderer.hpp"
#include "../IBL/IBLGenerator.hpp"
#include "LightingSystem.hpp"
#include "ShadowRenderPass.hpp"
#include "ClusteredLightingPass.hpp"
#include <algorithm>
#include <cmath>
#include <array>
#include <random>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cctype>
#include <unordered_set>
#include <limits>

#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include <iostream>
#include <chrono>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <utility>

namespace Crescent {

static float HaltonSequence(uint32_t index, uint32_t base) {
    float f = 1.0f;
    float r = 0.0f;
    while (index > 0) {
        f /= static_cast<float>(base);
        r += f * static_cast<float>(index % base);
        index /= base;
    }
    return r;
}

namespace {
struct CubeLUTData {
    int size = 0;
    std::vector<Math::Vector3> values;
};

std::string ToUpper(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return value;
}

bool ParseCubeLUT(const std::string& path, CubeLUTData& out) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string token;
        if (!(ss >> token)) {
            continue;
        }
        if (!token.empty() && token[0] == '#') {
            continue;
        }
        std::string key = ToUpper(token);
        if (key == "TITLE") {
            continue;
        }
        if (key == "DOMAIN_MIN" || key == "DOMAIN_MAX") {
            continue;
        }
        if (key == "LUT_3D_SIZE") {
            int size = 0;
            if (ss >> size && size > 1) {
                out.size = size;
                out.values.reserve(static_cast<size_t>(size) * size * size);
            }
            continue;
        }

        std::stringstream lineStream(line);
        float r = 0.0f, g = 0.0f, b = 0.0f;
        if (lineStream >> r >> g >> b) {
            out.values.emplace_back(r, g, b);
        }
    }

    if (out.values.empty()) {
        return false;
    }
    if (out.size <= 1) {
        int guess = static_cast<int>(std::round(std::cbrt(static_cast<double>(out.values.size()))));
        if (guess > 1 && guess * guess * guess == static_cast<int>(out.values.size())) {
            out.size = guess;
        } else {
            return false;
        }
    }
    if (static_cast<int>(out.values.size()) != out.size * out.size * out.size) {
        return false;
    }
    return true;
}

std::shared_ptr<Texture2D> CreateLUTTextureFromCube(TextureLoader* loader, const std::string& path) {
    if (!loader) {
        return nullptr;
    }
    CubeLUTData lut;
    if (!ParseCubeLUT(path, lut)) {
        return nullptr;
    }

    const int size = lut.size;
    const int width = size * size;
    const int height = size;
    std::vector<unsigned char> data(static_cast<size_t>(width) * height * 4);
    for (int b = 0; b < size; ++b) {
        for (int g = 0; g < size; ++g) {
            for (int r = 0; r < size; ++r) {
                int idx = (b * size * size) + (g * size) + r;
                Math::Vector3 c = lut.values[static_cast<size_t>(idx)];
                c.x = Math::Clamp(c.x, 0.0f, 1.0f);
                c.y = Math::Clamp(c.y, 0.0f, 1.0f);
                c.z = Math::Clamp(c.z, 0.0f, 1.0f);
                int x = r + b * size;
                int y = g;
                size_t offset = (static_cast<size_t>(y) * width + x) * 4;
                data[offset + 0] = static_cast<unsigned char>(std::round(c.x * 255.0f));
                data[offset + 1] = static_cast<unsigned char>(std::round(c.y * 255.0f));
                data[offset + 2] = static_cast<unsigned char>(std::round(c.z * 255.0f));
                data[offset + 3] = 255;
            }
        }
    }

    return loader->createTextureFromRGBA8(path, data.data(), width, height, false, false);
}
} // namespace

// Uniform structures matching Metal shader
struct ModelUniforms {
    Math::Matrix4x4 modelMatrix;
    Math::Matrix4x4 normalMatrix;
};

struct CameraUniforms {
    Math::Matrix4x4 viewMatrix;
    Math::Matrix4x4 projectionMatrix;
    Math::Matrix4x4 projectionMatrixNoJitter;
    Math::Matrix4x4 projectionMatrixNoJitterInverse;
    Math::Matrix4x4 viewProjectionMatrix;
    Math::Matrix4x4 viewMatrixInverse;
    Math::Matrix4x4 projectionMatrixInverse;
    Math::Vector4 cameraPositionTime;
};

struct SSAOParamsGPU {
    float radius;
    float bias;
    float intensity;
    float power;
    Math::Vector2 noiseScale;
    Math::Vector2 texelSize;
};

struct SSAOBlurParamsGPU {
    Math::Vector2 texelSize;
    float depthSharpness;
    float normalSharpness;
};

struct SSRParamsGPU {
    Math::Vector4 settings0; // texelSize.xy, thickness, maxSteps
    Math::Vector4 settings1; // maxDistance, maxRoughness, fadeStart, fadeEnd
};

struct DecalParamsGPU {
    Math::Vector4 colorOpacity;   // rgb + opacity
    Math::Vector4 uvTilingOffset; // xy tiling, zw offset
    Math::Vector4 edgeParams;     // softness, padding
    Math::Vector4 mapFlags;       // hasAlbedo, hasNormal, hasOrm, hasMask
    Math::Matrix4x4 modelMatrix;
    Math::Matrix4x4 invModel;
};

struct BloomPrefilterParamsGPU {
    float threshold;
    float knee;
    float pad0;
    float pad1;
};

struct BloomDownsampleParamsGPU {
    Math::Vector2 texelSize;
    float pad0;
    float pad1;
};

struct BloomUpsampleParamsGPU {
    Math::Vector2 texelSize;
    float radius;
    float pad0;
};

struct BloomCombineParamsGPU {
    float intensity;
    float pad0;
    float pad1;
    float pad2;
};

struct PostProcessParamsGPU {
    Math::Vector4 params0; // vignetteIntensity, grainIntensity, grainScale, time
    Math::Vector4 params1; // gradingIntensity, toneMapping, padding, padding
};

struct TAAParamsGPU {
    Math::Matrix4x4 prevViewProjection;
    Math::Vector4 params0; // texelSize.xy, feedback, historyValid
    Math::Vector4 params1; // sharpness, padding
};

struct MotionBlurParamsGPU {
    Math::Matrix4x4 prevViewProjection;
    Math::Matrix4x4 currViewProjection;
    Math::Vector4 params0; // texelSize.xy, strength, padding
};

struct DofParamsGPU {
    Math::Vector4 params0; // texelSize.xy, focusDistance, aperture
    Math::Vector4 params1; // maxBlur, padding
};

struct FogParamsGPU {
    Math::Vector4 fogColorDensity; // rgb, density
    Math::Vector4 distanceParams;  // start, end, height, heightFalloff
    Math::Vector4 sunDirIntensity; // xyz dir, intensity
    Math::Vector4 sunColor;        // rgb, padding
    Math::Vector4 volumeParams;    // near, far, sliceCount, historyWeight
    Math::Vector4 misc;            // heightFogEnabled, anisotropy, historyValid, padding
    Math::Vector4 shadowParams;    // shadowIndex, cascadeCount, enabled, strength
};

struct VelocityUniformsGPU {
    Math::Matrix4x4 prevModelMatrix;
    Math::Matrix4x4 currViewProjection;
    Math::Matrix4x4 prevViewProjection;
};

struct MaterialUniformsGPU {
    Math::Vector4 albedo;          // 16 bytes
    Math::Vector4 properties;      // 16 bytes (metallic, roughness, ao, normalScale)
    Math::Vector4 emission;        // 16 bytes (emission.xyz, emissionStrength)
    Math::Vector4 uvTilingOffset;  // 16 bytes (tiling.xy, offset.xy)
    Math::Vector4 textureFlags;    // 16 bytes (albedo, normal, metallic, roughness)
    Math::Vector4 textureFlags2;   // 16 bytes (ao, emission, height, invertHeight)
    Math::Vector4 textureFlags3;   // 16 bytes (packedORM, alphaClip, alphaCutoff, unused)
    Math::Vector4 heightParams;    // 16 bytes (scale, minLayers, maxLayers, receiveShadows)
    Math::Vector4 foliageParams0;  // 16 bytes (windStrength, windSpeed, windScale, windGust)
    Math::Vector4 foliageParams1;  // 16 bytes (lodStart, lodEnd, billboardStart, billboardEnd)
    Math::Vector4 foliageParams2;  // 16 bytes (windEnabled, lodEnabled, billboardEnabled, ditherEnabled)
    Math::Vector4 foliageParams3;  // 16 bytes (windDir.xyz, padding)
    Math::Vector4 impostorParams0; // 16 bytes (enabled, rows, cols, padding)
};  // Total = 208 bytes

struct MeshUniformsGPU {
    Math::Vector4 boundsCenter;
    Math::Vector4 boundsSize;
    Math::Vector4 flags;
};

struct LightDataGPU {
    Math::Vector4 direction;       // 16 bytes (direction.xyz, intensity)
    Math::Vector4 color;           // 16 bytes (color.xyz, padding)
};  // Total = 32 bytes

struct EnvironmentUniformsGPU {
    Math::Vector4 exposureIntensity; // exposure EV, ibl intensity, sky intensity, blur
    Math::Vector4 colorControl;      // tint rgb, saturation
    Math::Vector4 toneControl;       // contrast, unused, skyboxVisible flag, padding
    Math::Vector4 skyParams;         // skyMode, reserved
    Math::Vector4 rot0;
    Math::Vector4 rot1;
    Math::Vector4 rot2;
}; // 112 bytes

struct ClusterParams {
    Math::Matrix4x4 projection;
    Math::Matrix4x4 projectionInv;
    uint32_t clusterX;
    uint32_t clusterY;
    uint32_t clusterZ;
    uint32_t lightCount;
    uint32_t maxLightsPerCluster;
    float screenWidth;
    float screenHeight;
    float nearPlane;
    float farPlane;
    float _pad[3]; // pad to 16-byte multiple to match Metal constant buffer
};

struct InstanceDataGPU {
    Math::Matrix4x4 modelMatrix;
    Math::Matrix4x4 normalMatrix;
};

struct InstanceCullParams {
    Math::Vector4 frustumPlanes[6];
    Math::Vector4 boundsCenterRadius;
    uint32_t inputOffset;
    uint32_t outputOffset;
    uint32_t instanceCount;
    uint32_t _pad;
    Math::Vector2 screenSize;
    uint32_t hzbMipCount;
    uint32_t _pad2;
};

struct DrawIndexedIndirectArgs {
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t indexStart;
    int32_t baseVertex;
    uint32_t baseInstance;
};

static std::array<Math::Vector4, 6> ExtractFrustumPlanes(const Math::Matrix4x4& m) {
    auto row = [&](int r) {
        return Math::Vector4(m(r, 0), m(r, 1), m(r, 2), m(r, 3));
    };
    Math::Vector4 r0 = row(0);
    Math::Vector4 r1 = row(1);
    Math::Vector4 r2 = row(2);
    Math::Vector4 r3 = row(3);

    std::array<Math::Vector4, 6> planes = {
        r3 + r0, // left
        r3 - r0, // right
        r3 + r1, // bottom
        r3 - r1, // top
        r3 + r2, // near
        r3 - r2  // far
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

static constexpr float kCullTightness = 0.85f;

static bool IsSphereInFrustum(const std::array<Math::Vector4, 6>& planes,
                              const Math::Vector3& center,
                              float radius) {
    for (const auto& p : planes) {
        float dist = p.x * center.x + p.y * center.y + p.z * center.z + p.w;
        if (dist < -radius) {
            return false;
        }
    }
    return true;
}

static bool SphereInFrustum(const std::array<Math::Vector4, 6>& planes,
                            const Math::Vector3& center,
                            float radius) {
    for (const auto& p : planes) {
        float d = p.x * center.x + p.y * center.y + p.z * center.z + p.w;
        if (d < -radius) {
            return false;
        }
    }
    return true;
}

Renderer::Renderer()
    : m_device(nullptr)
    , m_commandQueue(nullptr)
    , m_library(nullptr)
    , m_metalLayer(nullptr)
    , m_depthStencilState(nullptr)
    , m_skyboxDepthState(nullptr)
    , m_depthReadState(nullptr)
    , m_prepassPipelineState(nullptr)
    , m_prepassPipelineSkinned(nullptr)
    , m_prepassPipelineInstanced(nullptr)
    , m_instanceCullPipeline(nullptr)
    , m_instanceCullHzbPipeline(nullptr)
    , m_instanceIndirectPipeline(nullptr)
    , m_hzbInitPipeline(nullptr)
    , m_hzbDownsamplePipeline(nullptr)
    , m_velocityPipelineState(nullptr)
    , m_velocityPipelineSkinned(nullptr)
    , m_ssaoPipelineState(nullptr)
    , m_ssaoBlurPipelineState(nullptr)
    , m_impostorBakePipeline(nullptr)
    , m_ssrPipelineState(nullptr)
    , m_decalPipelineState(nullptr)
    , m_bloomPrefilterPipelineState(nullptr)
    , m_bloomDownsamplePipelineState(nullptr)
    , m_bloomUpsamplePipelineState(nullptr)
    , m_bloomCombinePipelineState(nullptr)
    , m_taaPipelineState(nullptr)
    , m_dofPipelineState(nullptr)
    , m_fogPipelineState(nullptr)
    , m_fogVolumePipelineState(nullptr)
    , m_motionBlurPipelineState(nullptr)
    , m_depthTexture(nullptr)
    , m_msaaDepthTexture(nullptr)
    , m_hzbTexture(nullptr)
    , m_hzbMipViews()
    , m_hzbMipCount(0)
    , m_normalTexture(nullptr)
    , m_ssaoTexture(nullptr)
    , m_ssaoBlurTexture(nullptr)
    , m_ssaoNoiseTexture(nullptr)
    , m_velocityTexture(nullptr)
    , m_dofTexture(nullptr)
    , m_fogTexture(nullptr)
    , m_fogVolumeTexture(nullptr)
    , m_fogVolumeHistoryTexture(nullptr)
    , m_fogVolumeWidth(0)
    , m_fogVolumeHeight(0)
    , m_fogVolumeDepth(0)
    , m_fogVolumeQuality(1)
    , m_fogVolumeHistoryValid(false)
    , m_fogHistoryInitialized(false)
    , m_prevFogCameraPos(Math::Vector3(0.0f))
    , m_prevFogCameraForward(Math::Vector3(0.0f, 0.0f, -1.0f))
    , m_prevFogSunDir(Math::Vector3(0.0f, -1.0f, 0.0f))
    , m_prevFogSunIntensity(0.0f)
    , m_postColorTexture(nullptr)
    , m_decalAlbedoTexture(nullptr)
    , m_decalNormalTexture(nullptr)
    , m_decalOrmTexture(nullptr)
    , m_motionBlurTexture(nullptr)
    , m_bloomMipTextures()
    , m_bloomMipCount(0)
    , m_taaHistoryTexture(nullptr)
    , m_taaCurrentTexture(nullptr)
    , m_colorTexture(nullptr)
    , m_msaaColorTexture(nullptr)
    , m_modelUniformBuffer(nullptr)
    , m_cameraUniformBuffer(nullptr)
    , m_materialUniformBuffer(nullptr)
    , m_lightUniformBuffer(nullptr)
    , m_environmentUniformBuffer(nullptr)
    , m_lightGPUBuffer(nullptr)
    , m_shadowGPUBuffer(nullptr)
    , m_lightCountBuffer(nullptr)
    , m_clusterHeaderBuffer(nullptr)
    , m_clusterIndexBuffer(nullptr)
    , m_clusterParamsBuffer(nullptr)
    , m_skinningBuffer(nullptr)
    , m_prevSkinningBuffer(nullptr)
    , m_skinningBufferCapacity(0)
    , m_prevSkinningBufferCapacity(0)
    , m_instanceBuffer(nullptr)
    , m_instanceBufferCapacity(0)
    , m_instanceBufferOffset(0)
    , m_instanceCullBuffer(nullptr)
    , m_instanceCountBuffer(nullptr)
    , m_instanceIndirectBuffer(nullptr)
    , m_instanceCullCapacity(0)
    , m_instanceCountCapacity(0)
    , m_instanceIndirectCapacity(0)
    , m_samplerState(nullptr)
    , m_shadowSampler(nullptr)
    , m_linearClampSampler(nullptr)
    , m_pointClampSampler(nullptr)
    , m_debugLibrary(nullptr)
    , m_debugLinePipelineState(nullptr)
    , m_debugGridPipelineState(nullptr)
    , m_debugRenderer(nullptr)
    , m_skyboxPipelineState(nullptr)
    , m_blitPipelineState(nullptr)
    , m_skyboxVertexBuffer(nullptr)
    , m_skyboxIndexBuffer(nullptr)
    , m_skyboxIndexCount(0)
    , m_iblCubemap(nullptr)
    , m_iblPrefiltered(nullptr)
    , m_iblIrradiance(nullptr)
    , m_iblBRDFLUT(nullptr)
    , m_hasIBL(false)
    , m_sceneColorFormat(static_cast<int>(MTL::PixelFormatBGRA8Unorm))
    , m_outputHDR(false)
    , m_prevViewProjection()
    , m_prevViewProjectionNoJitter()
    , m_taaHistoryValid(false)
    , m_motionHistoryValid(false)
    , m_taaFrameIndex(0)
    , m_frameIndex(0)
    , m_qualitySettings()
    , m_renderTargetWidth(0)
    , m_renderTargetHeight(0)
    , m_msaaSamples(1)
    , m_viewportWidth(1920.0f)
    , m_viewportHeight(1080.0f)
    , m_isInitialized(false)
    , m_debugDrawShadowAtlas(false)
    , m_debugDrawCascades(false)
    , m_debugDrawPointFrusta(false)
    , m_activePool(RenderTargetPool::Scene) {
    m_lightingSystem = std::make_unique<LightingSystem>();
    m_shadowPass = std::make_unique<ShadowRenderPass>();
    m_clusterPass = std::make_unique<ClusteredLightingPass>();
    m_sceneTargets.sceneColorFormat = m_sceneColorFormat;
    m_gameTargets.sceneColorFormat = m_sceneColorFormat;
    storeRenderTargetState(m_sceneTargets);
}

Renderer::~Renderer() {
    shutdown();
}

bool Renderer::initialize() {
    if (m_isInitialized) {
        return true;
    }
    
    std::cout << "Initializing Metal Renderer..." << std::endl;
    
    // Get default Metal device
    m_device = MTL::CreateSystemDefaultDevice();
    if (!m_device) {
        std::cerr << "Failed to create Metal device!" << std::endl;
        return false;
    }
    
    std::cout << "Metal Device: " << m_device->name()->utf8String() << std::endl;
    
    // Create command queue
    m_commandQueue = m_device->newCommandQueue();
    if (!m_commandQueue) {
        std::cerr << "Failed to create command queue!" << std::endl;
        return false;
    }
    
    // Load shader library
    m_library = m_device->newDefaultLibrary();
    
    if (!m_library) {
        std::cerr << "Failed to load shader library!" << std::endl;
        std::cerr << "Make sure PBR.metal is added to Xcode project!" << std::endl;
        return false;
    }
    
    std::cout << "Shader library loaded" << std::endl;
    
    // Create uniform buffers
    m_modelUniformBuffer = m_device->newBuffer(sizeof(ModelUniforms), MTL::ResourceStorageModeShared);
    m_cameraUniformBuffer = m_device->newBuffer(sizeof(CameraUniforms), MTL::ResourceStorageModeShared);
    m_materialUniformBuffer = m_device->newBuffer(sizeof(MaterialUniformsGPU), MTL::ResourceStorageModeShared);
    m_lightUniformBuffer = m_device->newBuffer(sizeof(LightDataGPU), MTL::ResourceStorageModeShared);
    m_environmentUniformBuffer = m_device->newBuffer(sizeof(EnvironmentUniformsGPU), MTL::ResourceStorageModeShared);
    
    rebuildSamplerState(8);
    
    // Shadow sampler (comparison, clamp)
    MTL::SamplerDescriptor* shadowDesc = MTL::SamplerDescriptor::alloc()->init();
    shadowDesc->setMinFilter(MTL::SamplerMinMagFilterLinear);
    shadowDesc->setMagFilter(MTL::SamplerMinMagFilterLinear);
    shadowDesc->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
    shadowDesc->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
    shadowDesc->setCompareFunction(MTL::CompareFunctionLessEqual);
    shadowDesc->setLodMinClamp(0);
    shadowDesc->setLodMaxClamp(16);
    m_shadowSampler = m_device->newSamplerState(shadowDesc);
    shadowDesc->release();

    MTL::SamplerDescriptor* clampDesc = MTL::SamplerDescriptor::alloc()->init();
    clampDesc->setMinFilter(MTL::SamplerMinMagFilterLinear);
    clampDesc->setMagFilter(MTL::SamplerMinMagFilterLinear);
    clampDesc->setMipFilter(MTL::SamplerMipFilterNearest);
    clampDesc->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
    clampDesc->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
    m_linearClampSampler = m_device->newSamplerState(clampDesc);
    clampDesc->release();

    MTL::SamplerDescriptor* pointDesc = MTL::SamplerDescriptor::alloc()->init();
    pointDesc->setMinFilter(MTL::SamplerMinMagFilterNearest);
    pointDesc->setMagFilter(MTL::SamplerMinMagFilterNearest);
    pointDesc->setMipFilter(MTL::SamplerMipFilterNearest);
    pointDesc->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
    pointDesc->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
    m_pointClampSampler = m_device->newSamplerState(pointDesc);
    pointDesc->release();
    
    // Texture loader and built-in defaults
    m_textureLoader = std::make_unique<TextureLoader>(m_device, m_commandQueue);
    m_defaultWhiteTexture = m_textureLoader->createSolidTexture(1.0f, 1.0f, 1.0f, 1.0f, true);
    m_defaultNormalTexture = m_textureLoader->createFlatNormalTexture();
    m_defaultBlackTexture = m_textureLoader->createSolidTexture(0.0f, 0.0f, 0.0f, 1.0f, false);
    m_defaultHeightTexture = m_textureLoader->createSolidTexture(0.5f, 0.5f, 0.5f, 1.0f, false); // mid-height
    m_defaultEnvironmentTexture = m_textureLoader->createSolidTexture(0.18f, 0.2f, 0.26f, 1.0f, false);
    m_environmentTexture = m_defaultEnvironmentTexture;
    m_billboardMesh = Mesh::CreatePlane(1.0f, 1.0f, 1, 1);
    if (m_billboardMesh) {
        m_billboardMesh->setName("BillboardQuad");
    }
    if (m_textureLoader) {
        const int lutSize = 32;
        const int width = lutSize * lutSize;
        const int height = lutSize;
        std::vector<unsigned char> data(width * height * 4);
        for (int b = 0; b < lutSize; ++b) {
            for (int g = 0; g < lutSize; ++g) {
                for (int r = 0; r < lutSize; ++r) {
                    float fr = static_cast<float>(r) / static_cast<float>(lutSize - 1);
                    float fg = static_cast<float>(g) / static_cast<float>(lutSize - 1);
                    float fb = static_cast<float>(b) / static_cast<float>(lutSize - 1);
                    int x = r + b * lutSize;
                    int y = g;
                    int idx = (y * width + x) * 4;
                    data[idx + 0] = static_cast<unsigned char>(std::round(fr * 255.0f));
                    data[idx + 1] = static_cast<unsigned char>(std::round(fg * 255.0f));
                    data[idx + 2] = static_cast<unsigned char>(std::round(fb * 255.0f));
                    data[idx + 3] = 255;
                }
            }
        }
        m_colorGradingNeutralLUT = m_textureLoader->createTextureFromRGBA8("neutral_lut_32", data.data(), width, height, false, false);
        m_colorGradingLUT = m_colorGradingNeutralLUT;
        m_colorGradingLUTPath.clear();
    }
    
    // Build pipelines and depth stencil states
    buildPipelines();
    buildDepthStencilStates();
    buildEnvironmentPipeline();
    buildBlitPipeline();
    buildPrepassPipeline();
    buildInstanceCullingPipeline();
    buildHZBPipelines();
    buildVelocityPipelines();
    buildSSAOPipelines();
    buildSSAONoiseTexture();
    buildImpostorPipeline();
    buildSSRPipeline();
    buildDecalPipeline();
    buildMotionBlurPipeline();
    buildBloomPipelines();
    buildDOFPipeline();
    buildFogPipeline();
    buildFogVolumePipeline();
    buildTAAPipeline();
    
    // Initialize debug renderer
    m_debugRenderer = std::make_unique<DebugRenderer>();
    if (!m_debugRenderer->initialize(m_device)) {
        std::cerr << "Failed to initialize DebugRenderer!" << std::endl;
        return false;
    }
    
    // Build debug pipelines
    buildDebugPipelines();
    
    // Initialize IBL Generator
    m_iblGenerator = std::make_unique<IBLGenerator>();
    if (!m_iblGenerator->initialize(m_device, m_commandQueue)) {
        std::cerr << "Warning: IBL Generator failed to initialize - using fallback IBL" << std::endl;
    } else {
        m_iblBRDFLUT = m_iblGenerator->getBRDFLUT();
    }
    
    // Initialize shadow rendering
    if (m_shadowPass) {
        if (!m_shadowPass->initialize(m_device, 4096, 1)) {
            std::cerr << "Warning: ShadowRenderPass failed to initialize" << std::endl;
        }
    }
    if (m_lightingSystem) {
        m_lightingSystem->configureShadowAtlas(4096, 1);
    }
    if (m_clusterPass) {
        if (!m_clusterPass->initialize(m_device)) {
            std::cerr << "Warning: ClusteredLightingPass failed to initialize" << std::endl;
        }
    }
    
    resetEnvironment();
    
    m_isInitialized = true;
    std::cout << "Metal Renderer initialized successfully!" << std::endl;
    
    return true;
}

void Renderer::buildPipelines() {
    // We'll build pipelines on-demand when needed
    // This allows for different vertex formats
}

void Renderer::buildDepthStencilStates() {
    if (m_depthStencilState) {
        m_depthStencilState->release();
        m_depthStencilState = nullptr;
    }
    if (m_depthReadState) {
        m_depthReadState->release();
        m_depthReadState = nullptr;
    }

    MTL::DepthStencilDescriptor* depthDescriptor = MTL::DepthStencilDescriptor::alloc()->init();
    
    // Use standard depth: near -> 0, far -> 1
    depthDescriptor->setDepthCompareFunction(MTL::CompareFunctionLessEqual);
    depthDescriptor->setDepthWriteEnabled(true);
    
    m_depthStencilState = m_device->newDepthStencilState(depthDescriptor);
    depthDescriptor->release();

    MTL::DepthStencilDescriptor* readDescriptor = MTL::DepthStencilDescriptor::alloc()->init();
    readDescriptor->setDepthCompareFunction(MTL::CompareFunctionLessEqual);
    readDescriptor->setDepthWriteEnabled(false);
    m_depthReadState = m_device->newDepthStencilState(readDescriptor);
    readDescriptor->release();
}

void Renderer::buildDebugPipelines() {
    std::cout << "Building debug pipelines..." << std::endl;
    NS::Error* error = nullptr;
    
    // Use main library for debug shaders
    if (m_debugLinePipelineState) {
        m_debugLinePipelineState->release();
        m_debugLinePipelineState = nullptr;
    }
    if (m_debugGridPipelineState) {
        m_debugGridPipelineState->release();
        m_debugGridPipelineState = nullptr;
    }
    if (m_debugLibrary) {
        m_debugLibrary->release();
        m_debugLibrary = nullptr;
    }
    
    m_debugLibrary = m_library;
    m_debugLibrary->retain();
    
    // Debug line pipeline
    std::cout << "Loading debugLineVertexShader..." << std::endl;
    MTL::Function* debugLineVertexFunc = m_debugLibrary->newFunction(
        NS::String::string("debugLineVertexShader", NS::UTF8StringEncoding)
    );
    MTL::Function* debugLineFragmentFunc = m_debugLibrary->newFunction(
        NS::String::string("debugLineFragmentShader", NS::UTF8StringEncoding)
    );
    
    if (!debugLineVertexFunc) {
        std::cerr << "ERROR: debugLineVertexShader NOT FOUND in library!" << std::endl;
    }
    if (!debugLineFragmentFunc) {
        std::cerr << "ERROR: debugLineFragmentShader NOT FOUND in library!" << std::endl;
    }
    
    if (debugLineVertexFunc && debugLineFragmentFunc) {
        MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
        desc->setVertexFunction(debugLineVertexFunc);
        desc->setFragmentFunction(debugLineFragmentFunc);
        desc->setSampleCount(m_msaaSamples);
        
        // Configure vertex descriptor for debug vertices
        MTL::VertexDescriptor* vertexDesc = MTL::VertexDescriptor::alloc()->init();
        
        // Position (attribute 0) - float3
        vertexDesc->attributes()->object(0)->setFormat(MTL::VertexFormatFloat3);
        vertexDesc->attributes()->object(0)->setOffset(0);
        vertexDesc->attributes()->object(0)->setBufferIndex(0);
        
        // Color (attribute 1) - float4
        vertexDesc->attributes()->object(1)->setFormat(MTL::VertexFormatFloat4);
        vertexDesc->attributes()->object(1)->setOffset(12);
        vertexDesc->attributes()->object(1)->setBufferIndex(0);
        
        // Vertex buffer layout (stride = 3*float + 4*float = 28 bytes)
        vertexDesc->layouts()->object(0)->setStride(28);
        vertexDesc->layouts()->object(0)->setStepFunction(MTL::VertexStepFunctionPerVertex);
        
        desc->setVertexDescriptor(vertexDesc);
        
        desc->colorAttachments()->object(0)->setPixelFormat(static_cast<MTL::PixelFormat>(m_sceneColorFormat));
        desc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);
        
        auto colorAttachment = desc->colorAttachments()->object(0);
        colorAttachment->setBlendingEnabled(true);
        colorAttachment->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
        colorAttachment->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
        
        m_debugLinePipelineState = m_device->newRenderPipelineState(desc, &error);
        
        if (error) {
            std::cerr << "ERROR creating debug line pipeline: " << error->localizedDescription()->utf8String() << std::endl;
        }
        
        vertexDesc->release();
        desc->release();
        debugLineVertexFunc->release();
        debugLineFragmentFunc->release();
        
        if (m_debugLinePipelineState) {
            std::cout << "Debug line pipeline created" << std::endl;
        }
    }
    
    // Grid pipeline
    std::cout << "Loading gridVertexShader..." << std::endl;
    MTL::Function* gridVertexFunc = m_debugLibrary->newFunction(
        NS::String::string("gridVertexShader", NS::UTF8StringEncoding)
    );
    MTL::Function* gridFragmentFunc = m_debugLibrary->newFunction(
        NS::String::string("gridFragmentShader", NS::UTF8StringEncoding)
    );
    
    if (!gridVertexFunc) {
        std::cerr << "ERROR: gridVertexShader NOT FOUND in library!" << std::endl;
    }
    if (!gridFragmentFunc) {
        std::cerr << "ERROR: gridFragmentShader NOT FOUND in library!" << std::endl;
    }
    
    if (gridVertexFunc && gridFragmentFunc) {
        MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
        desc->setVertexFunction(gridVertexFunc);
        desc->setFragmentFunction(gridFragmentFunc);
        desc->setSampleCount(m_msaaSamples);
        
        // Configure vertex descriptor for debug vertices
        MTL::VertexDescriptor* vertexDesc = MTL::VertexDescriptor::alloc()->init();
        
        // Position (attribute 0) - float3
        vertexDesc->attributes()->object(0)->setFormat(MTL::VertexFormatFloat3);
        vertexDesc->attributes()->object(0)->setOffset(0);
        vertexDesc->attributes()->object(0)->setBufferIndex(0);
        
        // Color (attribute 1) - float4
        vertexDesc->attributes()->object(1)->setFormat(MTL::VertexFormatFloat4);
        vertexDesc->attributes()->object(1)->setOffset(12);
        vertexDesc->attributes()->object(1)->setBufferIndex(0);
        
        // Vertex buffer layout (stride = 3*float + 4*float = 28 bytes)
        vertexDesc->layouts()->object(0)->setStride(28);
        vertexDesc->layouts()->object(0)->setStepFunction(MTL::VertexStepFunctionPerVertex);
        
        desc->setVertexDescriptor(vertexDesc);
        
        desc->colorAttachments()->object(0)->setPixelFormat(static_cast<MTL::PixelFormat>(m_sceneColorFormat));
        desc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);
        
        auto colorAttachment = desc->colorAttachments()->object(0);
        colorAttachment->setBlendingEnabled(true);
        colorAttachment->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
        colorAttachment->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
        
        m_debugGridPipelineState = m_device->newRenderPipelineState(desc, &error);
        
        if (error) {
            std::cerr << "ERROR creating grid pipeline: " << error->localizedDescription()->utf8String() << std::endl;
        }
        
        vertexDesc->release();
        desc->release();
        gridVertexFunc->release();
        gridFragmentFunc->release();
        
        if (m_debugGridPipelineState) {
            std::cout << "Grid pipeline created" << std::endl;
        }
    }
}

void Renderer::buildEnvironmentPipeline() {
    if (!m_device || !m_library) {
        return;
    }
    
    // Full-screen triangle - no vertex buffer needed, shader generates positions
    m_skyboxIndexCount = 3;
    
    // Release old vertex buffer if exists (no longer needed)
    if (m_skyboxVertexBuffer) {
        m_skyboxVertexBuffer->release();
        m_skyboxVertexBuffer = nullptr;
    }
    
    // Depth state for skybox (no writes, always pass)
    if (!m_skyboxDepthState) {
        MTL::DepthStencilDescriptor* skyboxDepthDesc = MTL::DepthStencilDescriptor::alloc()->init();
        skyboxDepthDesc->setDepthCompareFunction(MTL::CompareFunctionLessEqual);
        skyboxDepthDesc->setDepthWriteEnabled(false);
        m_skyboxDepthState = m_device->newDepthStencilState(skyboxDepthDesc);
        skyboxDepthDesc->release();
    }
    
    // Skybox pipeline
    NS::Error* error = nullptr;
    MTL::Function* skyboxVertexFunc = m_library->newFunction(
        NS::String::string("skybox_vertex", NS::UTF8StringEncoding)
    );
    MTL::Function* skyboxFragmentFunc = m_library->newFunction(
        NS::String::string("skybox_fragment", NS::UTF8StringEncoding)
    );
    
    if (!skyboxVertexFunc || !skyboxFragmentFunc) {
        std::cerr << "Skybox shaders not found in library!" << std::endl;
        if (skyboxVertexFunc) skyboxVertexFunc->release();
        if (skyboxFragmentFunc) skyboxFragmentFunc->release();
        return;
    }
    
    if (m_skyboxPipelineState) {
        m_skyboxPipelineState->release();
        m_skyboxPipelineState = nullptr;
    }
    
    MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
    desc->setVertexFunction(skyboxVertexFunc);
    desc->setFragmentFunction(skyboxFragmentFunc);
    desc->setSampleCount(m_msaaSamples);
    
    // No vertex descriptor needed - shader reads directly from buffer using vertex_id
    
    desc->colorAttachments()->object(0)->setPixelFormat(static_cast<MTL::PixelFormat>(m_sceneColorFormat));
    desc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);
    
    m_skyboxPipelineState = m_device->newRenderPipelineState(desc, &error);
    if (error) {
        std::cerr << "Failed to create skybox pipeline: " << error->localizedDescription()->utf8String() << std::endl;
    }
    
    desc->release();
    skyboxVertexFunc->release();
    skyboxFragmentFunc->release();
}

void Renderer::buildBlitPipeline() {
    if (!m_device || !m_library) {
        return;
    }
    
    if (m_blitPipelineState) {
        m_blitPipelineState->release();
        m_blitPipelineState = nullptr;
    }
    NS::Error* error = nullptr;
    MTL::Function* blitVertex = m_library->newFunction(
        NS::String::string("blit_vertex", NS::UTF8StringEncoding)
    );
    MTL::Function* blitFragment = m_library->newFunction(
        NS::String::string("blit_fragment", NS::UTF8StringEncoding)
    );
    
    if (!blitVertex || !blitFragment) {
        std::cerr << "Blit shaders not found in library!" << std::endl;
        if (blitVertex) blitVertex->release();
        if (blitFragment) blitFragment->release();
        return;
    }
    
    MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
    desc->setVertexFunction(blitVertex);
    desc->setFragmentFunction(blitFragment);
    desc->setSampleCount(1);
    desc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    desc->setDepthAttachmentPixelFormat(MTL::PixelFormatInvalid);
    
    m_blitPipelineState = m_device->newRenderPipelineState(desc, &error);
    if (error) {
        std::cerr << "Failed to create blit pipeline: " << error->localizedDescription()->utf8String() << std::endl;
    }
    
    desc->release();
    blitVertex->release();
    blitFragment->release();
}

void Renderer::buildPrepassPipeline() {
    if (!m_device || !m_library) {
        return;
    }

    auto buildPipeline = [&](const char* vertexName, bool skinned, MTL::RenderPipelineState*& outState) {
        if (outState) {
            outState->release();
            outState = nullptr;
        }

        NS::String* vsName = NS::String::string(vertexName, NS::UTF8StringEncoding);
        NS::String* fsName = NS::String::string("fragment_prepass", NS::UTF8StringEncoding);
        MTL::Function* vertexFunction = m_library->newFunction(vsName);
        MTL::Function* fragmentFunction = m_library->newFunction(fsName);
        if (!vertexFunction || !fragmentFunction) {
            std::cerr << "Missing prepass shader functions: " << vertexName << " / fragment_prepass\n";
            if (vertexFunction) vertexFunction->release();
            if (fragmentFunction) fragmentFunction->release();
            return;
        }

        MTL::RenderPipelineDescriptor* descriptor = MTL::RenderPipelineDescriptor::alloc()->init();
        descriptor->setVertexFunction(vertexFunction);
        descriptor->setFragmentFunction(fragmentFunction);
        descriptor->setSampleCount(1);

        MTL::VertexDescriptor* vertexDescriptor = MTL::VertexDescriptor::alloc()->init();
        vertexDescriptor->attributes()->object(0)->setFormat(MTL::VertexFormatFloat3);
        vertexDescriptor->attributes()->object(0)->setOffset(0);
        vertexDescriptor->attributes()->object(0)->setBufferIndex(0);

        vertexDescriptor->attributes()->object(1)->setFormat(MTL::VertexFormatFloat3);
        vertexDescriptor->attributes()->object(1)->setOffset(12);
        vertexDescriptor->attributes()->object(1)->setBufferIndex(0);

        vertexDescriptor->attributes()->object(2)->setFormat(MTL::VertexFormatFloat2);
        vertexDescriptor->attributes()->object(2)->setOffset(24);
        vertexDescriptor->attributes()->object(2)->setBufferIndex(0);

        vertexDescriptor->attributes()->object(3)->setFormat(MTL::VertexFormatFloat3);
        vertexDescriptor->attributes()->object(3)->setOffset(32);
        vertexDescriptor->attributes()->object(3)->setBufferIndex(0);

        vertexDescriptor->attributes()->object(4)->setFormat(MTL::VertexFormatFloat3);
        vertexDescriptor->attributes()->object(4)->setOffset(44);
        vertexDescriptor->attributes()->object(4)->setBufferIndex(0);

        vertexDescriptor->attributes()->object(5)->setFormat(MTL::VertexFormatFloat4);
        vertexDescriptor->attributes()->object(5)->setOffset(56);
        vertexDescriptor->attributes()->object(5)->setBufferIndex(0);

        vertexDescriptor->layouts()->object(0)->setStride(72);
        vertexDescriptor->layouts()->object(0)->setStepFunction(MTL::VertexStepFunctionPerVertex);

        if (skinned) {
            vertexDescriptor->attributes()->object(6)->setFormat(MTL::VertexFormatUInt4);
            vertexDescriptor->attributes()->object(6)->setOffset(0);
            vertexDescriptor->attributes()->object(6)->setBufferIndex(4);

            vertexDescriptor->attributes()->object(7)->setFormat(MTL::VertexFormatFloat4);
            vertexDescriptor->attributes()->object(7)->setOffset(sizeof(uint32_t) * 4);
            vertexDescriptor->attributes()->object(7)->setBufferIndex(4);

            vertexDescriptor->layouts()->object(4)->setStride(sizeof(uint32_t) * 4 + sizeof(float) * 4);
            vertexDescriptor->layouts()->object(4)->setStepFunction(MTL::VertexStepFunctionPerVertex);
        }

        descriptor->setVertexDescriptor(vertexDescriptor);
        descriptor->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatRGBA16Float);
        descriptor->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);

        NS::Error* error = nullptr;
        outState = m_device->newRenderPipelineState(descriptor, &error);
        if (!outState) {
            std::cerr << "Failed to create prepass pipeline state" << std::endl;
            if (error) {
                std::cerr << "Error: " << error->localizedDescription()->utf8String() << std::endl;
            }
        }

        vertexDescriptor->release();
        descriptor->release();
        vertexFunction->release();
        fragmentFunction->release();
    };

    buildPipeline("vertex_prepass", false, m_prepassPipelineState);
    buildPipeline("vertex_prepass_skinned", true, m_prepassPipelineSkinned);
    buildPipeline("vertex_prepass_instanced", false, m_prepassPipelineInstanced);
}

void Renderer::buildInstanceCullingPipeline() {
    if (!m_device || !m_library) {
        return;
    }

    if (m_instanceCullPipeline) {
        m_instanceCullPipeline->release();
        m_instanceCullPipeline = nullptr;
    }
    if (m_instanceCullHzbPipeline) {
        m_instanceCullHzbPipeline->release();
        m_instanceCullHzbPipeline = nullptr;
    }
    if (m_instanceIndirectPipeline) {
        m_instanceIndirectPipeline->release();
        m_instanceIndirectPipeline = nullptr;
    }

    NS::Error* error = nullptr;
    MTL::Function* cullFn = m_library->newFunction(NS::String::string("instance_cull", NS::UTF8StringEncoding));
    MTL::Function* cullHzbFn = m_library->newFunction(NS::String::string("instance_cull_hzb", NS::UTF8StringEncoding));
    MTL::Function* indirectFn = m_library->newFunction(NS::String::string("instance_build_indirect", NS::UTF8StringEncoding));
    if (!cullFn || !indirectFn) {
        std::cerr << "Missing instance culling shaders (instance_cull / instance_build_indirect)\n";
        if (cullFn) cullFn->release();
        if (cullHzbFn) cullHzbFn->release();
        if (indirectFn) indirectFn->release();
        return;
    }

    m_instanceCullPipeline = m_device->newComputePipelineState(cullFn, &error);
    if (!m_instanceCullPipeline && error) {
        std::cerr << "Failed to create instance cull pipeline: " << error->localizedDescription()->utf8String() << std::endl;
    }
    error = nullptr;
    if (cullHzbFn) {
        m_instanceCullHzbPipeline = m_device->newComputePipelineState(cullHzbFn, &error);
        if (!m_instanceCullHzbPipeline && error) {
            std::cerr << "Failed to create instance HZB cull pipeline: " << error->localizedDescription()->utf8String() << std::endl;
        }
    }
    error = nullptr;
    m_instanceIndirectPipeline = m_device->newComputePipelineState(indirectFn, &error);
    if (!m_instanceIndirectPipeline && error) {
        std::cerr << "Failed to create instance indirect pipeline: " << error->localizedDescription()->utf8String() << std::endl;
    }

    cullFn->release();
    if (cullHzbFn) {
        cullHzbFn->release();
    }
    indirectFn->release();
}

void Renderer::buildHZBPipelines() {
    if (!m_device || !m_library) {
        return;
    }

    if (m_hzbInitPipeline) {
        m_hzbInitPipeline->release();
        m_hzbInitPipeline = nullptr;
    }
    if (m_hzbDownsamplePipeline) {
        m_hzbDownsamplePipeline->release();
        m_hzbDownsamplePipeline = nullptr;
    }

    NS::Error* error = nullptr;
    MTL::Function* initFn = m_library->newFunction(NS::String::string("hzb_init", NS::UTF8StringEncoding));
    MTL::Function* downFn = m_library->newFunction(NS::String::string("hzb_downsample", NS::UTF8StringEncoding));
    if (!initFn || !downFn) {
        std::cerr << "Missing HZB shaders (hzb_init / hzb_downsample)\n";
        if (initFn) initFn->release();
        if (downFn) downFn->release();
        return;
    }

    m_hzbInitPipeline = m_device->newComputePipelineState(initFn, &error);
    if (!m_hzbInitPipeline && error) {
        std::cerr << "Failed to create HZB init pipeline: " << error->localizedDescription()->utf8String() << std::endl;
    }
    error = nullptr;
    m_hzbDownsamplePipeline = m_device->newComputePipelineState(downFn, &error);
    if (!m_hzbDownsamplePipeline && error) {
        std::cerr << "Failed to create HZB downsample pipeline: " << error->localizedDescription()->utf8String() << std::endl;
    }

    initFn->release();
    downFn->release();
}

void Renderer::buildVelocityPipelines() {
    if (!m_device || !m_library) {
        return;
    }

    auto buildPipeline = [&](const char* vertexName, bool skinned, MTL::RenderPipelineState*& outState) {
        if (outState) {
            outState->release();
            outState = nullptr;
        }

        NS::String* vsName = NS::String::string(vertexName, NS::UTF8StringEncoding);
        NS::String* fsName = NS::String::string("fragment_velocity", NS::UTF8StringEncoding);
        MTL::Function* vertexFunction = m_library->newFunction(vsName);
        MTL::Function* fragmentFunction = m_library->newFunction(fsName);
        if (!vertexFunction || !fragmentFunction) {
            std::cerr << "Missing velocity shader functions: " << vertexName << " / fragment_velocity\n";
            if (vertexFunction) vertexFunction->release();
            if (fragmentFunction) fragmentFunction->release();
            return;
        }

        MTL::RenderPipelineDescriptor* descriptor = MTL::RenderPipelineDescriptor::alloc()->init();
        descriptor->setVertexFunction(vertexFunction);
        descriptor->setFragmentFunction(fragmentFunction);
        descriptor->setSampleCount(1);

        MTL::VertexDescriptor* vertexDescriptor = MTL::VertexDescriptor::alloc()->init();
        vertexDescriptor->attributes()->object(0)->setFormat(MTL::VertexFormatFloat3);
        vertexDescriptor->attributes()->object(0)->setOffset(0);
        vertexDescriptor->attributes()->object(0)->setBufferIndex(0);

        vertexDescriptor->attributes()->object(1)->setFormat(MTL::VertexFormatFloat3);
        vertexDescriptor->attributes()->object(1)->setOffset(12);
        vertexDescriptor->attributes()->object(1)->setBufferIndex(0);

        vertexDescriptor->attributes()->object(2)->setFormat(MTL::VertexFormatFloat2);
        vertexDescriptor->attributes()->object(2)->setOffset(24);
        vertexDescriptor->attributes()->object(2)->setBufferIndex(0);

        vertexDescriptor->attributes()->object(3)->setFormat(MTL::VertexFormatFloat3);
        vertexDescriptor->attributes()->object(3)->setOffset(32);
        vertexDescriptor->attributes()->object(3)->setBufferIndex(0);

        vertexDescriptor->attributes()->object(4)->setFormat(MTL::VertexFormatFloat3);
        vertexDescriptor->attributes()->object(4)->setOffset(44);
        vertexDescriptor->attributes()->object(4)->setBufferIndex(0);

        vertexDescriptor->attributes()->object(5)->setFormat(MTL::VertexFormatFloat4);
        vertexDescriptor->attributes()->object(5)->setOffset(56);
        vertexDescriptor->attributes()->object(5)->setBufferIndex(0);

        vertexDescriptor->layouts()->object(0)->setStride(72);
        vertexDescriptor->layouts()->object(0)->setStepFunction(MTL::VertexStepFunctionPerVertex);

        if (skinned) {
            vertexDescriptor->attributes()->object(6)->setFormat(MTL::VertexFormatUInt4);
            vertexDescriptor->attributes()->object(6)->setOffset(0);
            vertexDescriptor->attributes()->object(6)->setBufferIndex(4);

            vertexDescriptor->attributes()->object(7)->setFormat(MTL::VertexFormatFloat4);
            vertexDescriptor->attributes()->object(7)->setOffset(sizeof(uint32_t) * 4);
            vertexDescriptor->attributes()->object(7)->setBufferIndex(4);

            vertexDescriptor->layouts()->object(4)->setStride(sizeof(uint32_t) * 4 + sizeof(float) * 4);
            vertexDescriptor->layouts()->object(4)->setStepFunction(MTL::VertexStepFunctionPerVertex);
        }

        descriptor->setVertexDescriptor(vertexDescriptor);
        descriptor->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatRG16Float);
        descriptor->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);

        NS::Error* error = nullptr;
        outState = m_device->newRenderPipelineState(descriptor, &error);
        if (!outState) {
            std::cerr << "Failed to create velocity pipeline state" << std::endl;
            if (error) {
                std::cerr << "Error: " << error->localizedDescription()->utf8String() << std::endl;
            }
        }

        vertexDescriptor->release();
        descriptor->release();
        vertexFunction->release();
        fragmentFunction->release();
    };

    buildPipeline("vertex_velocity", false, m_velocityPipelineState);
    buildPipeline("vertex_velocity_skinned", true, m_velocityPipelineSkinned);
}

void Renderer::buildSSAOPipelines() {
    if (!m_device || !m_library) {
        return;
    }

    auto buildPipeline = [&](const char* fragmentName, MTL::RenderPipelineState*& outState) {
        if (outState) {
            outState->release();
            outState = nullptr;
        }

        NS::String* vsName = NS::String::string("blit_vertex", NS::UTF8StringEncoding);
        NS::String* fsName = NS::String::string(fragmentName, NS::UTF8StringEncoding);
        MTL::Function* vertexFunction = m_library->newFunction(vsName);
        MTL::Function* fragmentFunction = m_library->newFunction(fsName);
        if (!vertexFunction || !fragmentFunction) {
            std::cerr << "Missing SSAO shader functions: blit_vertex / " << fragmentName << "\n";
            if (vertexFunction) vertexFunction->release();
            if (fragmentFunction) fragmentFunction->release();
            return;
        }

        MTL::RenderPipelineDescriptor* descriptor = MTL::RenderPipelineDescriptor::alloc()->init();
        descriptor->setVertexFunction(vertexFunction);
        descriptor->setFragmentFunction(fragmentFunction);
        descriptor->setSampleCount(1);
        descriptor->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatR8Unorm);
        descriptor->setDepthAttachmentPixelFormat(MTL::PixelFormatInvalid);

        NS::Error* error = nullptr;
        outState = m_device->newRenderPipelineState(descriptor, &error);
        if (!outState) {
            std::cerr << "Failed to create SSAO pipeline state" << std::endl;
            if (error) {
                std::cerr << "Error: " << error->localizedDescription()->utf8String() << std::endl;
            }
        }

        descriptor->release();
        vertexFunction->release();
        fragmentFunction->release();
    };

    buildPipeline("ssao_fragment", m_ssaoPipelineState);
    buildPipeline("ssao_blur_fragment", m_ssaoBlurPipelineState);
}

void Renderer::buildSSAONoiseTexture() {
    if (!m_device) {
        return;
    }
    if (m_ssaoNoiseTexture) {
        m_ssaoNoiseTexture->release();
        m_ssaoNoiseTexture = nullptr;
    }

    constexpr uint32_t noiseSize = 4;
    std::array<uint8_t, noiseSize * noiseSize * 4> data{};
    std::mt19937 rng(1337);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    for (size_t i = 0; i < noiseSize * noiseSize; ++i) {
        float x = dist(rng) * 2.0f - 1.0f;
        float y = dist(rng) * 2.0f - 1.0f;
        Math::Vector3 v(x, y, 0.0f);
        v = v.length() > 0.0f ? v.normalized() : Math::Vector3(1.0f, 0.0f, 0.0f);
        data[i * 4 + 0] = static_cast<uint8_t>((v.x * 0.5f + 0.5f) * 255.0f);
        data[i * 4 + 1] = static_cast<uint8_t>((v.y * 0.5f + 0.5f) * 255.0f);
        data[i * 4 + 2] = 0;
        data[i * 4 + 3] = 255;
    }

    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
    desc->setTextureType(MTL::TextureType2D);
    desc->setWidth(noiseSize);
    desc->setHeight(noiseSize);
    desc->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
    desc->setUsage(MTL::TextureUsageShaderRead);
    desc->setStorageMode(MTL::StorageModeShared);
    m_ssaoNoiseTexture = m_device->newTexture(desc);
    desc->release();

    if (m_ssaoNoiseTexture) {
        MTL::Region region = MTL::Region::Make2D(0, 0, noiseSize, noiseSize);
        m_ssaoNoiseTexture->replaceRegion(region, 0, data.data(), noiseSize * 4);
    }
}

void Renderer::buildImpostorPipeline() {
    if (!m_device || !m_library) {
        return;
    }
    if (m_impostorBakePipeline) {
        m_impostorBakePipeline->release();
        m_impostorBakePipeline = nullptr;
    }

    NS::String* vsName = NS::String::string("impostor_bake_vertex", NS::UTF8StringEncoding);
    NS::String* fsName = NS::String::string("impostor_bake_fragment", NS::UTF8StringEncoding);
    MTL::Function* vertexFunction = m_library->newFunction(vsName);
    MTL::Function* fragmentFunction = m_library->newFunction(fsName);
    if (!vertexFunction || !fragmentFunction) {
        std::cerr << "Missing impostor bake shader functions\n";
        if (vertexFunction) vertexFunction->release();
        if (fragmentFunction) fragmentFunction->release();
        return;
    }

    MTL::RenderPipelineDescriptor* descriptor = MTL::RenderPipelineDescriptor::alloc()->init();
    descriptor->setVertexFunction(vertexFunction);
    descriptor->setFragmentFunction(fragmentFunction);
    descriptor->setSampleCount(1);
    descriptor->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
    descriptor->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);

    NS::Error* error = nullptr;
    m_impostorBakePipeline = m_device->newRenderPipelineState(descriptor, &error);
    if (!m_impostorBakePipeline) {
        std::cerr << "Failed to create impostor bake pipeline state" << std::endl;
        if (error) {
            std::cerr << "Error: " << error->localizedDescription()->utf8String() << std::endl;
        }
    }

    descriptor->release();
    vertexFunction->release();
    fragmentFunction->release();
}

void Renderer::buildSSRPipeline() {
    if (!m_device || !m_library) {
        return;
    }

    if (m_ssrPipelineState) {
        m_ssrPipelineState->release();
        m_ssrPipelineState = nullptr;
    }
    if (m_impostorBakePipeline) {
        m_impostorBakePipeline->release();
        m_impostorBakePipeline = nullptr;
    }
    if (m_decalPipelineState) {
        m_decalPipelineState->release();
        m_decalPipelineState = nullptr;
    }

    NS::String* vsName = NS::String::string("blit_vertex", NS::UTF8StringEncoding);
    NS::String* fsName = NS::String::string("ssr_fragment", NS::UTF8StringEncoding);
    MTL::Function* vertexFunction = m_library->newFunction(vsName);
    MTL::Function* fragmentFunction = m_library->newFunction(fsName);
    if (!vertexFunction || !fragmentFunction) {
        std::cerr << "Missing SSR shader functions: blit_vertex / ssr_fragment\n";
        if (vertexFunction) vertexFunction->release();
        if (fragmentFunction) fragmentFunction->release();
        return;
    }

    MTL::RenderPipelineDescriptor* descriptor = MTL::RenderPipelineDescriptor::alloc()->init();
    descriptor->setVertexFunction(vertexFunction);
    descriptor->setFragmentFunction(fragmentFunction);
    descriptor->setSampleCount(1);
    descriptor->colorAttachments()->object(0)->setPixelFormat(
        static_cast<MTL::PixelFormat>(m_sceneColorFormat)
    );
    descriptor->setDepthAttachmentPixelFormat(MTL::PixelFormatInvalid);

    NS::Error* error = nullptr;
    m_ssrPipelineState = m_device->newRenderPipelineState(descriptor, &error);
    if (!m_ssrPipelineState) {
        std::cerr << "Failed to create SSR pipeline state" << std::endl;
        if (error) {
            std::cerr << "Error: " << error->localizedDescription()->utf8String() << std::endl;
        }
    }

    descriptor->release();
    vertexFunction->release();
    fragmentFunction->release();
}

void Renderer::buildDecalPipeline() {
    if (!m_device || !m_library) {
        return;
    }

    if (m_decalPipelineState) {
        m_decalPipelineState->release();
        m_decalPipelineState = nullptr;
    }

    NS::String* vsName = NS::String::string("blit_vertex", NS::UTF8StringEncoding);
    NS::String* fsName = NS::String::string("decal_fragment", NS::UTF8StringEncoding);
    MTL::Function* vertexFunction = m_library->newFunction(vsName);
    MTL::Function* fragmentFunction = m_library->newFunction(fsName);
    if (!vertexFunction || !fragmentFunction) {
        std::cerr << "Missing Decal shader functions: blit_vertex / decal_fragment\n";
        if (vertexFunction) vertexFunction->release();
        if (fragmentFunction) fragmentFunction->release();
        return;
    }

    MTL::RenderPipelineDescriptor* descriptor = MTL::RenderPipelineDescriptor::alloc()->init();
    descriptor->setVertexFunction(vertexFunction);
    descriptor->setFragmentFunction(fragmentFunction);
    descriptor->setSampleCount(1);
    descriptor->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatRGBA16Float);
    descriptor->colorAttachments()->object(1)->setPixelFormat(MTL::PixelFormatRGBA16Float);
    descriptor->colorAttachments()->object(2)->setPixelFormat(MTL::PixelFormatRGBA16Float);
    descriptor->setDepthAttachmentPixelFormat(MTL::PixelFormatInvalid);

    for (int i = 0; i < 3; ++i) {
        auto color = descriptor->colorAttachments()->object(i);
        color->setBlendingEnabled(true);
        color->setRgbBlendOperation(MTL::BlendOperationAdd);
        color->setAlphaBlendOperation(MTL::BlendOperationAdd);
        color->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
        color->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
        color->setSourceAlphaBlendFactor(MTL::BlendFactorOne);
        color->setDestinationAlphaBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
    }

    NS::Error* error = nullptr;
    m_decalPipelineState = m_device->newRenderPipelineState(descriptor, &error);
    if (!m_decalPipelineState) {
        std::cerr << "Failed to create Decal pipeline state" << std::endl;
        if (error) {
            std::cerr << "Error: " << error->localizedDescription()->utf8String() << std::endl;
        }
    }

    descriptor->release();
    vertexFunction->release();
    fragmentFunction->release();
}

void Renderer::buildDOFPipeline() {
    if (!m_device || !m_library) {
        return;
    }

    if (m_dofPipelineState) {
        m_dofPipelineState->release();
        m_dofPipelineState = nullptr;
    }

    NS::String* vsName = NS::String::string("blit_vertex", NS::UTF8StringEncoding);
    NS::String* fsName = NS::String::string("dof_fragment", NS::UTF8StringEncoding);
    MTL::Function* vertexFunction = m_library->newFunction(vsName);
    MTL::Function* fragmentFunction = m_library->newFunction(fsName);
    if (!vertexFunction || !fragmentFunction) {
        std::cerr << "Missing DOF shader functions: blit_vertex / dof_fragment\n";
        if (vertexFunction) vertexFunction->release();
        if (fragmentFunction) fragmentFunction->release();
        return;
    }

    MTL::RenderPipelineDescriptor* descriptor = MTL::RenderPipelineDescriptor::alloc()->init();
    descriptor->setVertexFunction(vertexFunction);
    descriptor->setFragmentFunction(fragmentFunction);
    descriptor->setSampleCount(1);
    descriptor->colorAttachments()->object(0)->setPixelFormat(
        static_cast<MTL::PixelFormat>(m_sceneColorFormat)
    );
    descriptor->setDepthAttachmentPixelFormat(MTL::PixelFormatInvalid);

    NS::Error* error = nullptr;
    m_dofPipelineState = m_device->newRenderPipelineState(descriptor, &error);
    if (!m_dofPipelineState) {
        std::cerr << "Failed to create DOF pipeline state" << std::endl;
        if (error) {
            std::cerr << "Error: " << error->localizedDescription()->utf8String() << std::endl;
        }
    }

    descriptor->release();
    vertexFunction->release();
    fragmentFunction->release();
}

void Renderer::buildFogPipeline() {
    if (!m_device || !m_library) {
        return;
    }

    if (m_fogPipelineState) {
        m_fogPipelineState->release();
        m_fogPipelineState = nullptr;
    }
    if (m_fogVolumePipelineState) {
        m_fogVolumePipelineState->release();
        m_fogVolumePipelineState = nullptr;
    }
    if (m_fogVolumePipelineState) {
        m_fogVolumePipelineState->release();
        m_fogVolumePipelineState = nullptr;
    }

    NS::String* vsName = NS::String::string("blit_vertex", NS::UTF8StringEncoding);
    NS::String* fsName = NS::String::string("fog_fragment", NS::UTF8StringEncoding);
    MTL::Function* vertexFunction = m_library->newFunction(vsName);
    MTL::Function* fragmentFunction = m_library->newFunction(fsName);
    if (!vertexFunction || !fragmentFunction) {
        std::cerr << "Missing fog shader functions: blit_vertex / fog_fragment\n";
        if (vertexFunction) vertexFunction->release();
        if (fragmentFunction) fragmentFunction->release();
        return;
    }

    MTL::RenderPipelineDescriptor* descriptor = MTL::RenderPipelineDescriptor::alloc()->init();
    descriptor->setVertexFunction(vertexFunction);
    descriptor->setFragmentFunction(fragmentFunction);
    descriptor->setSampleCount(1);
    descriptor->colorAttachments()->object(0)->setPixelFormat(
        static_cast<MTL::PixelFormat>(m_sceneColorFormat)
    );
    descriptor->setDepthAttachmentPixelFormat(MTL::PixelFormatInvalid);

    NS::Error* error = nullptr;
    m_fogPipelineState = m_device->newRenderPipelineState(descriptor, &error);
    if (!m_fogPipelineState) {
        std::cerr << "Failed to create fog pipeline state" << std::endl;
        if (error) {
            std::cerr << "Error: " << error->localizedDescription()->utf8String() << std::endl;
        }
    }

    descriptor->release();
    vertexFunction->release();
    fragmentFunction->release();
}

void Renderer::buildFogVolumePipeline() {
    if (!m_device || !m_library) {
        return;
    }

    if (m_fogVolumePipelineState) {
        m_fogVolumePipelineState->release();
        m_fogVolumePipelineState = nullptr;
    }

    NS::String* csName = NS::String::string("fog_volume_build", NS::UTF8StringEncoding);
    MTL::Function* computeFunction = m_library->newFunction(csName);
    if (!computeFunction) {
        std::cerr << "Missing fog volume shader function: fog_volume_build\n";
        return;
    }

    NS::Error* error = nullptr;
    m_fogVolumePipelineState = m_device->newComputePipelineState(computeFunction, &error);
    computeFunction->release();
    if (!m_fogVolumePipelineState) {
        std::cerr << "Failed to create fog volume pipeline state" << std::endl;
        if (error) {
            std::cerr << "Error: " << error->localizedDescription()->utf8String() << std::endl;
        }
    }
}

void Renderer::buildMotionBlurPipeline() {
    if (!m_device || !m_library) {
        return;
    }

    if (m_motionBlurPipelineState) {
        m_motionBlurPipelineState->release();
        m_motionBlurPipelineState = nullptr;
    }
    if (m_instanceCullPipeline) {
        m_instanceCullPipeline->release();
        m_instanceCullPipeline = nullptr;
    }
    if (m_instanceCullHzbPipeline) {
        m_instanceCullHzbPipeline->release();
        m_instanceCullHzbPipeline = nullptr;
    }
    if (m_instanceIndirectPipeline) {
        m_instanceIndirectPipeline->release();
        m_instanceIndirectPipeline = nullptr;
    }
    if (m_hzbInitPipeline) {
        m_hzbInitPipeline->release();
        m_hzbInitPipeline = nullptr;
    }
    if (m_hzbDownsamplePipeline) {
        m_hzbDownsamplePipeline->release();
        m_hzbDownsamplePipeline = nullptr;
    }

    NS::String* vsName = NS::String::string("blit_vertex", NS::UTF8StringEncoding);
    NS::String* fsName = NS::String::string("motion_blur_fragment", NS::UTF8StringEncoding);
    MTL::Function* vertexFunction = m_library->newFunction(vsName);
    MTL::Function* fragmentFunction = m_library->newFunction(fsName);
    if (!vertexFunction || !fragmentFunction) {
        std::cerr << "Missing motion blur shader functions: blit_vertex / motion_blur_fragment\n";
        if (vertexFunction) vertexFunction->release();
        if (fragmentFunction) fragmentFunction->release();
        return;
    }

    MTL::RenderPipelineDescriptor* descriptor = MTL::RenderPipelineDescriptor::alloc()->init();
    descriptor->setVertexFunction(vertexFunction);
    descriptor->setFragmentFunction(fragmentFunction);
    descriptor->setSampleCount(1);
    descriptor->colorAttachments()->object(0)->setPixelFormat(
        static_cast<MTL::PixelFormat>(m_sceneColorFormat)
    );
    descriptor->setDepthAttachmentPixelFormat(MTL::PixelFormatInvalid);

    NS::Error* error = nullptr;
    m_motionBlurPipelineState = m_device->newRenderPipelineState(descriptor, &error);
    if (!m_motionBlurPipelineState) {
        std::cerr << "Failed to create motion blur pipeline state" << std::endl;
        if (error) {
            std::cerr << "Error: " << error->localizedDescription()->utf8String() << std::endl;
        }
    }

    descriptor->release();
    vertexFunction->release();
    fragmentFunction->release();
}

void Renderer::buildBloomPipelines() {
    if (!m_device || !m_library) {
        return;
    }

    auto buildPipeline = [&](const char* fragmentName,
                             MTL::PixelFormat format,
                             bool blendAdditive,
                             MTL::RenderPipelineState*& outState) {
        if (outState) {
            outState->release();
            outState = nullptr;
        }

        NS::String* vsName = NS::String::string("blit_vertex", NS::UTF8StringEncoding);
        NS::String* fsName = NS::String::string(fragmentName, NS::UTF8StringEncoding);
        MTL::Function* vertexFunction = m_library->newFunction(vsName);
        MTL::Function* fragmentFunction = m_library->newFunction(fsName);
        if (!vertexFunction || !fragmentFunction) {
            std::cerr << "Missing bloom shader functions: blit_vertex / " << fragmentName << "\n";
            if (vertexFunction) vertexFunction->release();
            if (fragmentFunction) fragmentFunction->release();
            return;
        }

        MTL::RenderPipelineDescriptor* descriptor = MTL::RenderPipelineDescriptor::alloc()->init();
        descriptor->setVertexFunction(vertexFunction);
        descriptor->setFragmentFunction(fragmentFunction);
        descriptor->setSampleCount(1);
        auto colorAttachment = descriptor->colorAttachments()->object(0);
        colorAttachment->setPixelFormat(format);
        if (blendAdditive) {
            colorAttachment->setBlendingEnabled(true);
            colorAttachment->setSourceRGBBlendFactor(MTL::BlendFactorOne);
            colorAttachment->setDestinationRGBBlendFactor(MTL::BlendFactorOne);
            colorAttachment->setRgbBlendOperation(MTL::BlendOperationAdd);
            colorAttachment->setSourceAlphaBlendFactor(MTL::BlendFactorOne);
            colorAttachment->setDestinationAlphaBlendFactor(MTL::BlendFactorOne);
            colorAttachment->setAlphaBlendOperation(MTL::BlendOperationAdd);
        }
        descriptor->setDepthAttachmentPixelFormat(MTL::PixelFormatInvalid);

        NS::Error* error = nullptr;
        outState = m_device->newRenderPipelineState(descriptor, &error);
        if (!outState) {
            std::cerr << "Failed to create bloom pipeline state" << std::endl;
            if (error) {
                std::cerr << "Error: " << error->localizedDescription()->utf8String() << std::endl;
            }
        }

        descriptor->release();
        vertexFunction->release();
        fragmentFunction->release();
    };

    buildPipeline("bloom_prefilter_fragment", MTL::PixelFormatRGBA16Float, false, m_bloomPrefilterPipelineState);
    buildPipeline("bloom_downsample_fragment", MTL::PixelFormatRGBA16Float, false, m_bloomDownsamplePipelineState);
    buildPipeline("bloom_upsample_fragment", MTL::PixelFormatRGBA16Float, true, m_bloomUpsamplePipelineState);
    buildPipeline("bloom_combine_fragment", MTL::PixelFormatBGRA8Unorm, false, m_bloomCombinePipelineState);
}

void Renderer::buildTAAPipeline() {
    if (!m_device || !m_library) {
        return;
    }

    if (m_taaPipelineState) {
        m_taaPipelineState->release();
        m_taaPipelineState = nullptr;
    }

    NS::String* vsName = NS::String::string("blit_vertex", NS::UTF8StringEncoding);
    NS::String* fsName = NS::String::string("taa_fragment", NS::UTF8StringEncoding);
    MTL::Function* vertexFunction = m_library->newFunction(vsName);
    MTL::Function* fragmentFunction = m_library->newFunction(fsName);
    if (!vertexFunction || !fragmentFunction) {
        std::cerr << "Missing TAA shader functions: blit_vertex / taa_fragment\n";
        if (vertexFunction) vertexFunction->release();
        if (fragmentFunction) fragmentFunction->release();
        return;
    }

    MTL::RenderPipelineDescriptor* descriptor = MTL::RenderPipelineDescriptor::alloc()->init();
    descriptor->setVertexFunction(vertexFunction);
    descriptor->setFragmentFunction(fragmentFunction);
    descriptor->setSampleCount(1);
    descriptor->colorAttachments()->object(0)->setPixelFormat(
        static_cast<MTL::PixelFormat>(m_sceneColorFormat)
    );
    descriptor->setDepthAttachmentPixelFormat(MTL::PixelFormatInvalid);

    NS::Error* error = nullptr;
    m_taaPipelineState = m_device->newRenderPipelineState(descriptor, &error);
    if (!m_taaPipelineState) {
        std::cerr << "Failed to create TAA pipeline state" << std::endl;
        if (error) {
            std::cerr << "Error: " << error->localizedDescription()->utf8String() << std::endl;
        }
    }

    descriptor->release();
    vertexFunction->release();
    fragmentFunction->release();
}

MTL::RenderPipelineState* Renderer::getPipelineState(const PipelineStateKey& key) {
    // Check cache
    auto it = m_pipelineStates.find(key);
    if (it != m_pipelineStates.end()) {
        return it->second;
    }

    if (key.isInstanced && key.isSkinned) {
        std::cerr << "Invalid pipeline key: instanced + skinned not supported\n";
        return nullptr;
    }
    
    // Create new pipeline state
    MTL::RenderPipelineDescriptor* descriptor = MTL::RenderPipelineDescriptor::alloc()->init();
    
    // Load shaders
    const char* vertexName = key.isInstanced ? "vertex_main_instanced"
        : (key.isSkinned ? "vertex_skinned" : "vertex_main");
    NS::String* vertexFunctionName = NS::String::string(vertexName, NS::UTF8StringEncoding);
    NS::String* fragmentFunctionName = NS::String::string("fragment_main", NS::UTF8StringEncoding);
    
    MTL::Function* vertexFunction = m_library->newFunction(vertexFunctionName);
    MTL::Function* fragmentFunction = m_library->newFunction(fragmentFunctionName);
    
    descriptor->setVertexFunction(vertexFunction);
    descriptor->setFragmentFunction(fragmentFunction);
    descriptor->setSampleCount(std::max<uint8_t>(1, key.sampleCount));
    
    // Configure vertex descriptor
    MTL::VertexDescriptor* vertexDescriptor = MTL::VertexDescriptor::alloc()->init();
    
    // Position (attribute 0)
    vertexDescriptor->attributes()->object(0)->setFormat(MTL::VertexFormatFloat3);
    vertexDescriptor->attributes()->object(0)->setOffset(0);
    vertexDescriptor->attributes()->object(0)->setBufferIndex(0);
    
    // Normal (attribute 1)
    vertexDescriptor->attributes()->object(1)->setFormat(MTL::VertexFormatFloat3);
    vertexDescriptor->attributes()->object(1)->setOffset(12);
    vertexDescriptor->attributes()->object(1)->setBufferIndex(0);
    
    // TexCoord (attribute 2)
    vertexDescriptor->attributes()->object(2)->setFormat(MTL::VertexFormatFloat2);
    vertexDescriptor->attributes()->object(2)->setOffset(24);
    vertexDescriptor->attributes()->object(2)->setBufferIndex(0);
    
    // Tangent (attribute 3)
    vertexDescriptor->attributes()->object(3)->setFormat(MTL::VertexFormatFloat3);
    vertexDescriptor->attributes()->object(3)->setOffset(32);
    vertexDescriptor->attributes()->object(3)->setBufferIndex(0);
    
    // Bitangent (attribute 4)
    vertexDescriptor->attributes()->object(4)->setFormat(MTL::VertexFormatFloat3);
    vertexDescriptor->attributes()->object(4)->setOffset(44);
    vertexDescriptor->attributes()->object(4)->setBufferIndex(0);
    
    // Color (attribute 5)
    vertexDescriptor->attributes()->object(5)->setFormat(MTL::VertexFormatFloat4);
    vertexDescriptor->attributes()->object(5)->setOffset(56);
    vertexDescriptor->attributes()->object(5)->setBufferIndex(0);
    
    // Vertex buffer layout (stride = sizeof(Vertex))
    vertexDescriptor->layouts()->object(0)->setStride(72);
    vertexDescriptor->layouts()->object(0)->setStepFunction(MTL::VertexStepFunctionPerVertex);

    if (key.isSkinned) {
        // Bone indices (attribute 6) - uint4
        vertexDescriptor->attributes()->object(6)->setFormat(MTL::VertexFormatUInt4);
        vertexDescriptor->attributes()->object(6)->setOffset(0);
        vertexDescriptor->attributes()->object(6)->setBufferIndex(4);

        // Bone weights (attribute 7) - float4
        vertexDescriptor->attributes()->object(7)->setFormat(MTL::VertexFormatFloat4);
        vertexDescriptor->attributes()->object(7)->setOffset(sizeof(uint32_t) * 4);
        vertexDescriptor->attributes()->object(7)->setBufferIndex(4);

        vertexDescriptor->layouts()->object(4)->setStride(sizeof(uint32_t) * 4 + sizeof(float) * 4);
        vertexDescriptor->layouts()->object(4)->setStepFunction(MTL::VertexStepFunctionPerVertex);
    }
    
    descriptor->setVertexDescriptor(vertexDescriptor);
    
    if (key.alphaToCoverage && key.sampleCount > 1) {
        descriptor->setAlphaToCoverageEnabled(true);
    }

    // Color attachment
    descriptor->colorAttachments()->object(0)->setPixelFormat(key.hdrTarget ? MTL::PixelFormatRGBA16Float : MTL::PixelFormatBGRA8Unorm);
    
    if (key.isTransparent) {
        // Enable blending for transparency
        descriptor->colorAttachments()->object(0)->setBlendingEnabled(true);
        descriptor->colorAttachments()->object(0)->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
        descriptor->colorAttachments()->object(0)->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
        descriptor->colorAttachments()->object(0)->setRgbBlendOperation(MTL::BlendOperationAdd);
        descriptor->colorAttachments()->object(0)->setSourceAlphaBlendFactor(MTL::BlendFactorOne);
        descriptor->colorAttachments()->object(0)->setDestinationAlphaBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
        descriptor->colorAttachments()->object(0)->setAlphaBlendOperation(MTL::BlendOperationAdd);
    }
    
    // Depth attachment
    descriptor->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);
    
    // Create pipeline state
    NS::Error* error = nullptr;
    MTL::RenderPipelineState* pipelineState = m_device->newRenderPipelineState(descriptor, &error);
    
    if (!pipelineState) {
        std::cerr << "Failed to create pipeline state!" << std::endl;
        if (error) {
            std::cerr << "Error: " << error->localizedDescription()->utf8String() << std::endl;
        }
    } else {
        std::cout << "Pipeline state created" << std::endl;
    }
    
    // Cache it only on success
    if (pipelineState) {
        m_pipelineStates[key] = pipelineState;
    }
    
    // Cleanup
    vertexDescriptor->release();
    descriptor->release();
    vertexFunction->release();
    fragmentFunction->release();
    
    return pipelineState;
}

void Renderer::setMetalLayer(void* layer) {
    setMetalLayer(layer, true);
}

void Renderer::setMetalLayer(void* layer, bool applySize) {
    m_metalLayer = static_cast<CA::MetalLayer*>(layer);
    
    if (m_metalLayer) {
        m_metalLayer->setDevice(m_device);
        m_metalLayer->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
        
        if (applySize) {
            // Get initial size
            auto drawableSize = m_metalLayer->drawableSize();
            float width = static_cast<float>(drawableSize.width);
            float height = static_cast<float>(drawableSize.height);
            
            std::cout << "Metal layer set with size: " << width << "x" << height << std::endl;
            
            // Only resize if we have valid dimensions
            if (width > 0 && height > 0) {
                resize(width, height);
            } else {
                std::cout << "Warning: Metal layer has invalid size, using defaults" << std::endl;
                m_viewportWidth = 1920.0f;
                m_viewportHeight = 1080.0f;
            }
        }
    }
}

Renderer::RenderTargetState& Renderer::getRenderTargetState(RenderTargetPool pool) {
    return pool == RenderTargetPool::Scene ? m_sceneTargets : m_gameTargets;
}

void Renderer::storeRenderTargetState(RenderTargetState& state) {
    state.depthTexture = m_depthTexture;
    state.msaaDepthTexture = m_msaaDepthTexture;
    state.hzbTexture = m_hzbTexture;
    state.hzbMipViews = m_hzbMipViews;
    state.hzbMipCount = m_hzbMipCount;
    state.normalTexture = m_normalTexture;
    state.ssaoTexture = m_ssaoTexture;
    state.ssaoBlurTexture = m_ssaoBlurTexture;
    state.ssaoNoiseTexture = m_ssaoNoiseTexture;
    state.velocityTexture = m_velocityTexture;
    state.dofTexture = m_dofTexture;
    state.fogTexture = m_fogTexture;
    state.fogVolumeTexture = m_fogVolumeTexture;
    state.fogVolumeHistoryTexture = m_fogVolumeHistoryTexture;
    state.fogVolumeWidth = m_fogVolumeWidth;
    state.fogVolumeHeight = m_fogVolumeHeight;
    state.fogVolumeDepth = m_fogVolumeDepth;
    state.fogVolumeQuality = m_fogVolumeQuality;
    state.fogVolumeHistoryValid = m_fogVolumeHistoryValid;
    state.fogHistoryInitialized = m_fogHistoryInitialized;
    state.prevFogCameraPos = m_prevFogCameraPos;
    state.prevFogCameraForward = m_prevFogCameraForward;
    state.prevFogSunDir = m_prevFogSunDir;
    state.prevFogSunIntensity = m_prevFogSunIntensity;
    state.postColorTexture = m_postColorTexture;
    state.decalAlbedoTexture = m_decalAlbedoTexture;
    state.decalNormalTexture = m_decalNormalTexture;
    state.decalOrmTexture = m_decalOrmTexture;
    state.motionBlurTexture = m_motionBlurTexture;
    state.bloomMipTextures = m_bloomMipTextures;
    state.bloomMipCount = m_bloomMipCount;
    state.taaHistoryTexture = m_taaHistoryTexture;
    state.taaCurrentTexture = m_taaCurrentTexture;
    state.colorTexture = m_colorTexture;
    state.msaaColorTexture = m_msaaColorTexture;
    state.sceneColorFormat = m_sceneColorFormat;
    state.taaHistoryValid = m_taaHistoryValid;
    state.motionHistoryValid = m_motionHistoryValid;
    state.taaFrameIndex = m_taaFrameIndex;
    state.prevViewProjection = m_prevViewProjection;
    state.prevViewProjectionNoJitter = m_prevViewProjectionNoJitter;
    state.renderTargetWidth = m_renderTargetWidth;
    state.renderTargetHeight = m_renderTargetHeight;
    state.msaaSamples = m_msaaSamples;
}

void Renderer::loadRenderTargetState(const RenderTargetState& state) {
    m_depthTexture = state.depthTexture;
    m_msaaDepthTexture = state.msaaDepthTexture;
    m_hzbTexture = state.hzbTexture;
    m_hzbMipViews = state.hzbMipViews;
    m_hzbMipCount = state.hzbMipCount;
    m_normalTexture = state.normalTexture;
    m_ssaoTexture = state.ssaoTexture;
    m_ssaoBlurTexture = state.ssaoBlurTexture;
    m_ssaoNoiseTexture = state.ssaoNoiseTexture;
    m_velocityTexture = state.velocityTexture;
    m_dofTexture = state.dofTexture;
    m_fogTexture = state.fogTexture;
    m_fogVolumeTexture = state.fogVolumeTexture;
    m_fogVolumeHistoryTexture = state.fogVolumeHistoryTexture;
    m_fogVolumeWidth = state.fogVolumeWidth;
    m_fogVolumeHeight = state.fogVolumeHeight;
    m_fogVolumeDepth = state.fogVolumeDepth;
    m_fogVolumeQuality = state.fogVolumeQuality;
    m_fogVolumeHistoryValid = state.fogVolumeHistoryValid;
    m_fogHistoryInitialized = state.fogHistoryInitialized;
    m_prevFogCameraPos = state.prevFogCameraPos;
    m_prevFogCameraForward = state.prevFogCameraForward;
    m_prevFogSunDir = state.prevFogSunDir;
    m_prevFogSunIntensity = state.prevFogSunIntensity;
    m_postColorTexture = state.postColorTexture;
    m_decalAlbedoTexture = state.decalAlbedoTexture;
    m_decalNormalTexture = state.decalNormalTexture;
    m_decalOrmTexture = state.decalOrmTexture;
    m_motionBlurTexture = state.motionBlurTexture;
    m_bloomMipTextures = state.bloomMipTextures;
    m_bloomMipCount = state.bloomMipCount;
    m_taaHistoryTexture = state.taaHistoryTexture;
    m_taaCurrentTexture = state.taaCurrentTexture;
    m_colorTexture = state.colorTexture;
    m_msaaColorTexture = state.msaaColorTexture;
    m_sceneColorFormat = state.sceneColorFormat;
    m_taaHistoryValid = state.taaHistoryValid;
    m_motionHistoryValid = state.motionHistoryValid;
    m_taaFrameIndex = state.taaFrameIndex;
    m_prevViewProjection = state.prevViewProjection;
    m_prevViewProjectionNoJitter = state.prevViewProjectionNoJitter;
    m_renderTargetWidth = state.renderTargetWidth;
    m_renderTargetHeight = state.renderTargetHeight;
    m_msaaSamples = state.msaaSamples;
}

void Renderer::releaseRenderTargetState(RenderTargetState& state) {
    if (state.depthTexture) {
        state.depthTexture->release();
        state.depthTexture = nullptr;
    }
    if (state.msaaDepthTexture) {
        state.msaaDepthTexture->release();
        state.msaaDepthTexture = nullptr;
    }
    for (MTL::Texture* tex : state.hzbMipViews) {
        if (tex) {
            tex->release();
        }
    }
    state.hzbMipViews.clear();
    if (state.hzbTexture) {
        state.hzbTexture->release();
        state.hzbTexture = nullptr;
    }
    state.hzbMipCount = 0;
    if (state.colorTexture) {
        state.colorTexture->release();
        state.colorTexture = nullptr;
    }
    if (state.msaaColorTexture) {
        state.msaaColorTexture->release();
        state.msaaColorTexture = nullptr;
    }
    if (state.postColorTexture) {
        state.postColorTexture->release();
        state.postColorTexture = nullptr;
    }
    if (state.decalAlbedoTexture) {
        state.decalAlbedoTexture->release();
        state.decalAlbedoTexture = nullptr;
    }
    if (state.decalNormalTexture) {
        state.decalNormalTexture->release();
        state.decalNormalTexture = nullptr;
    }
    if (state.decalOrmTexture) {
        state.decalOrmTexture->release();
        state.decalOrmTexture = nullptr;
    }
    if (state.motionBlurTexture) {
        state.motionBlurTexture->release();
        state.motionBlurTexture = nullptr;
    }
    if (state.taaHistoryTexture) {
        state.taaHistoryTexture->release();
        state.taaHistoryTexture = nullptr;
    }
    if (state.taaCurrentTexture) {
        state.taaCurrentTexture->release();
        state.taaCurrentTexture = nullptr;
    }
    if (state.normalTexture) {
        state.normalTexture->release();
        state.normalTexture = nullptr;
    }
    if (state.ssaoTexture) {
        state.ssaoTexture->release();
        state.ssaoTexture = nullptr;
    }
    if (state.ssaoBlurTexture) {
        state.ssaoBlurTexture->release();
        state.ssaoBlurTexture = nullptr;
    }
    if (state.ssaoNoiseTexture) {
        state.ssaoNoiseTexture->release();
        state.ssaoNoiseTexture = nullptr;
    }
    if (state.velocityTexture) {
        state.velocityTexture->release();
        state.velocityTexture = nullptr;
    }
    if (state.dofTexture) {
        state.dofTexture->release();
        state.dofTexture = nullptr;
    }
    if (state.fogTexture) {
        state.fogTexture->release();
        state.fogTexture = nullptr;
    }
    if (state.fogVolumeTexture) {
        state.fogVolumeTexture->release();
        state.fogVolumeTexture = nullptr;
    }
    if (state.fogVolumeHistoryTexture) {
        state.fogVolumeHistoryTexture->release();
        state.fogVolumeHistoryTexture = nullptr;
    }
    for (MTL::Texture* tex : state.bloomMipTextures) {
        if (tex) {
            tex->release();
        }
    }
    state.bloomMipTextures.clear();
    state.bloomMipCount = 0;
    state.renderTargetWidth = 0;
    state.renderTargetHeight = 0;
    state.msaaSamples = 1;
    state.taaHistoryValid = false;
    state.motionHistoryValid = false;
    state.taaFrameIndex = 0;
    state.fogVolumeWidth = 0;
    state.fogVolumeHeight = 0;
    state.fogVolumeDepth = 0;
    state.fogVolumeHistoryValid = false;
    state.fogHistoryInitialized = false;
    state.prevFogCameraPos = Math::Vector3(0.0f);
    state.prevFogCameraForward = Math::Vector3(0.0f, 0.0f, -1.0f);
    state.prevFogSunDir = Math::Vector3(0.0f, -1.0f, 0.0f);
    state.prevFogSunIntensity = 0.0f;
}

void Renderer::invalidateRenderTargetState(RenderTargetState& state, uint32_t msaaSamples) {
    state.renderTargetWidth = 0;
    state.renderTargetHeight = 0;
    state.msaaSamples = msaaSamples;
    state.taaHistoryValid = false;
    state.motionHistoryValid = false;
    state.taaFrameIndex = 0;
    state.fogVolumeHistoryValid = false;
    state.fogHistoryInitialized = false;
    state.fogVolumeWidth = 0;
    state.fogVolumeHeight = 0;
    state.fogVolumeDepth = 0;
    state.hzbMipCount = 0;
}

void Renderer::setRenderTargetPool(RenderTargetPool pool) {
    if (pool == m_activePool) {
        return;
    }
    storeRenderTargetState(getRenderTargetState(m_activePool));
    loadRenderTargetState(getRenderTargetState(pool));
    m_activePool = pool;
}

void Renderer::resize(float width, float height) {
    // Validate dimensions
    if (width <= 0 || height <= 0) {
        std::cerr << "Invalid viewport dimensions: " << width << "x" << height << std::endl;
        return;
    }

    std::cout << "Resizing viewport to: " << width << "x" << height << std::endl;
    setViewportSize(width, height, true);
}

void Renderer::setViewportSize(float width, float height, bool updateTargets) {
    if (width <= 0 || height <= 0) {
        return;
    }

    bool sizeChanged = (m_viewportWidth != width) || (m_viewportHeight != height);
    if (!sizeChanged && !updateTargets) {
        return;
    }

    m_viewportWidth = width;
    m_viewportHeight = height;
    
    if (!updateTargets) {
        return;
    }
    float scale = std::max(0.5f, std::min(2.0f, m_qualitySettings.renderScale));
    uint32_t renderWidth = static_cast<uint32_t>(std::max(1.0f, std::round(width * scale)));
    uint32_t renderHeight = static_cast<uint32_t>(std::max(1.0f, std::round(height * scale)));
    uint32_t targetWidth = std::max(renderWidth, m_renderTargetWidth);
    uint32_t targetHeight = std::max(renderHeight, m_renderTargetHeight);
    ensureRenderTargets(targetWidth, targetHeight, m_qualitySettings.msaaSamples, m_sceneColorFormat);
}

void Renderer::rebuildSamplerState(int anisotropy) {
    if (!m_device) {
        return;
    }
    if (m_samplerState) {
        m_samplerState->release();
        m_samplerState = nullptr;
    }
    
    int clamped = std::max(1, std::min(16, anisotropy));
    MTL::SamplerDescriptor* samplerDesc = MTL::SamplerDescriptor::alloc()->init();
    samplerDesc->setMinFilter(MTL::SamplerMinMagFilterLinear);
    samplerDesc->setMagFilter(MTL::SamplerMinMagFilterLinear);
    samplerDesc->setSAddressMode(MTL::SamplerAddressModeRepeat);
    samplerDesc->setTAddressMode(MTL::SamplerAddressModeRepeat);
    samplerDesc->setMipFilter(MTL::SamplerMipFilterLinear);
    samplerDesc->setMaxAnisotropy(clamped);
    m_samplerState = m_device->newSamplerState(samplerDesc);
    samplerDesc->release();
}

void Renderer::clearPipelineCache() {
    for (auto& pair : m_pipelineStates) {
        if (pair.second) {
            pair.second->release();
        }
    }
    m_pipelineStates.clear();
}

uint32_t Renderer::resolveSampleCount(uint32_t requested) const {
    uint32_t clamped = std::max(1u, std::min(8u, requested));
    if (!m_device) {
        return clamped;
    }
    const uint32_t candidates[] = {8, 4, 2, 1};
    for (uint32_t sample : candidates) {
        if (clamped >= sample && m_device->supportsTextureSampleCount(sample)) {
            return sample;
        }
    }
    return 1;
}

void Renderer::ensureRenderTargets(uint32_t width, uint32_t height, uint32_t msaaSamples, int colorFormat) {
    if (!m_device || width == 0 || height == 0) {
        return;
    }
    
    MTL::PixelFormat format = static_cast<MTL::PixelFormat>(colorFormat);
    uint32_t clampedSamples = resolveSampleCount(msaaSamples);
    bool sizeChanged = width != m_renderTargetWidth || height != m_renderTargetHeight;
    bool samplesChanged = clampedSamples != m_msaaSamples;
    bool formatChanged = colorFormat != m_sceneColorFormat;
    
    if (!sizeChanged && !samplesChanged && !formatChanged && m_colorTexture && m_depthTexture && m_normalTexture
        && m_ssaoTexture && m_ssaoBlurTexture && m_velocityTexture && m_dofTexture && m_fogTexture && m_postColorTexture
        && m_decalAlbedoTexture && m_decalNormalTexture && m_decalOrmTexture && m_motionBlurTexture
        && !m_bloomMipTextures.empty() && m_taaHistoryTexture && m_taaCurrentTexture
        && m_hzbTexture && !m_hzbMipViews.empty()) {
        return;
    }
    
    m_renderTargetWidth = width;
    m_renderTargetHeight = height;
    m_msaaSamples = clampedSamples;
    m_sceneColorFormat = colorFormat;
    m_taaHistoryValid = false;
    m_motionHistoryValid = false;
    
    if (m_colorTexture) {
        m_colorTexture->release();
        m_colorTexture = nullptr;
    }
    if (m_msaaColorTexture) {
        m_msaaColorTexture->release();
        m_msaaColorTexture = nullptr;
    }
    if (m_postColorTexture) {
        m_postColorTexture->release();
        m_postColorTexture = nullptr;
    }
    if (m_decalAlbedoTexture) {
        m_decalAlbedoTexture->release();
        m_decalAlbedoTexture = nullptr;
    }
    if (m_decalNormalTexture) {
        m_decalNormalTexture->release();
        m_decalNormalTexture = nullptr;
    }
    if (m_decalOrmTexture) {
        m_decalOrmTexture->release();
        m_decalOrmTexture = nullptr;
    }
    if (m_motionBlurTexture) {
        m_motionBlurTexture->release();
        m_motionBlurTexture = nullptr;
    }
    if (m_taaHistoryTexture) {
        m_taaHistoryTexture->release();
        m_taaHistoryTexture = nullptr;
    }
    if (m_taaCurrentTexture) {
        m_taaCurrentTexture->release();
        m_taaCurrentTexture = nullptr;
    }
    for (MTL::Texture* tex : m_bloomMipTextures) {
        if (tex) {
            tex->release();
        }
    }
    m_bloomMipTextures.clear();
    m_bloomMipCount = 0;
    if (m_postColorTexture) {
        m_postColorTexture->release();
        m_postColorTexture = nullptr;
    }
    if (m_decalAlbedoTexture) {
        m_decalAlbedoTexture->release();
        m_decalAlbedoTexture = nullptr;
    }
    if (m_decalNormalTexture) {
        m_decalNormalTexture->release();
        m_decalNormalTexture = nullptr;
    }
    if (m_decalOrmTexture) {
        m_decalOrmTexture->release();
        m_decalOrmTexture = nullptr;
    }
    if (m_motionBlurTexture) {
        m_motionBlurTexture->release();
        m_motionBlurTexture = nullptr;
    }
    if (m_taaHistoryTexture) {
        m_taaHistoryTexture->release();
        m_taaHistoryTexture = nullptr;
    }
    if (m_taaCurrentTexture) {
        m_taaCurrentTexture->release();
        m_taaCurrentTexture = nullptr;
    }
    for (MTL::Texture* tex : m_bloomMipTextures) {
        if (tex) {
            tex->release();
        }
    }
    m_bloomMipTextures.clear();
    m_bloomMipCount = 0;
    if (m_depthTexture) {
        m_depthTexture->release();
        m_depthTexture = nullptr;
    }
    if (m_msaaDepthTexture) {
        m_msaaDepthTexture->release();
        m_msaaDepthTexture = nullptr;
    }
    for (MTL::Texture* tex : m_hzbMipViews) {
        if (tex) {
            tex->release();
        }
    }
    m_hzbMipViews.clear();
    if (m_hzbTexture) {
        m_hzbTexture->release();
        m_hzbTexture = nullptr;
    }
    m_hzbMipCount = 0;
    if (m_normalTexture) {
        m_normalTexture->release();
        m_normalTexture = nullptr;
    }
    if (m_ssaoTexture) {
        m_ssaoTexture->release();
        m_ssaoTexture = nullptr;
    }
    if (m_ssaoBlurTexture) {
        m_ssaoBlurTexture->release();
        m_ssaoBlurTexture = nullptr;
    }
    if (m_ssaoNoiseTexture) {
        m_ssaoNoiseTexture->release();
        m_ssaoNoiseTexture = nullptr;
    }
    if (m_velocityTexture) {
        m_velocityTexture->release();
        m_velocityTexture = nullptr;
    }
    if (m_dofTexture) {
        m_dofTexture->release();
        m_dofTexture = nullptr;
    }
    if (m_fogTexture) {
        m_fogTexture->release();
        m_fogTexture = nullptr;
    }
    if (m_fogVolumeTexture) {
        m_fogVolumeTexture->release();
        m_fogVolumeTexture = nullptr;
    }
    if (m_fogVolumeHistoryTexture) {
        m_fogVolumeHistoryTexture->release();
        m_fogVolumeHistoryTexture = nullptr;
    }
    if (m_fogVolumeHistoryTexture) {
        m_fogVolumeHistoryTexture->release();
        m_fogVolumeHistoryTexture = nullptr;
    }
    MTL::TextureDescriptor* colorDesc = MTL::TextureDescriptor::alloc()->init();
    colorDesc->setTextureType(MTL::TextureType2D);
    colorDesc->setWidth(width);
    colorDesc->setHeight(height);
    colorDesc->setPixelFormat(format);
    colorDesc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
    colorDesc->setStorageMode(MTL::StorageModePrivate);
    m_colorTexture = m_device->newTexture(colorDesc);
    colorDesc->release();

    MTL::TextureDescriptor* postDesc = MTL::TextureDescriptor::alloc()->init();
    postDesc->setTextureType(MTL::TextureType2D);
    postDesc->setWidth(width);
    postDesc->setHeight(height);
    postDesc->setPixelFormat(format);
    postDesc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
    postDesc->setStorageMode(MTL::StorageModePrivate);
    m_postColorTexture = m_device->newTexture(postDesc);
    postDesc->release();

    MTL::TextureDescriptor* decalDesc = MTL::TextureDescriptor::alloc()->init();
    decalDesc->setTextureType(MTL::TextureType2D);
    decalDesc->setWidth(width);
    decalDesc->setHeight(height);
    decalDesc->setPixelFormat(MTL::PixelFormatRGBA16Float);
    decalDesc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
    decalDesc->setStorageMode(MTL::StorageModePrivate);
    m_decalAlbedoTexture = m_device->newTexture(decalDesc);
    m_decalNormalTexture = m_device->newTexture(decalDesc);
    m_decalOrmTexture = m_device->newTexture(decalDesc);
    decalDesc->release();

    MTL::TextureDescriptor* motionDesc = MTL::TextureDescriptor::alloc()->init();
    motionDesc->setTextureType(MTL::TextureType2D);
    motionDesc->setWidth(width);
    motionDesc->setHeight(height);
    motionDesc->setPixelFormat(format);
    motionDesc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
    motionDesc->setStorageMode(MTL::StorageModePrivate);
    m_motionBlurTexture = m_device->newTexture(motionDesc);
    motionDesc->release();

    MTL::TextureDescriptor* taaDesc = MTL::TextureDescriptor::alloc()->init();
    taaDesc->setTextureType(MTL::TextureType2D);
    taaDesc->setWidth(width);
    taaDesc->setHeight(height);
    taaDesc->setPixelFormat(format);
    taaDesc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
    taaDesc->setStorageMode(MTL::StorageModePrivate);
    m_taaHistoryTexture = m_device->newTexture(taaDesc);
    m_taaCurrentTexture = m_device->newTexture(taaDesc);
    taaDesc->release();
    
    MTL::TextureDescriptor* depthDesc = MTL::TextureDescriptor::alloc()->init();
    depthDesc->setTextureType(MTL::TextureType2D);
    depthDesc->setWidth(width);
    depthDesc->setHeight(height);
    depthDesc->setPixelFormat(MTL::PixelFormatDepth32Float);
    depthDesc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
    depthDesc->setStorageMode(MTL::StorageModePrivate);
    m_depthTexture = m_device->newTexture(depthDesc);
    depthDesc->release();

    auto calcHzbMipCount = [](uint32_t w, uint32_t h) -> uint32_t {
        uint32_t levels = 1;
        while (w > 1 || h > 1) {
            w = std::max(1u, w / 2);
            h = std::max(1u, h / 2);
            levels++;
        }
        return levels;
    };
    m_hzbMipCount = calcHzbMipCount(width, height);
    MTL::TextureDescriptor* hzbDesc = MTL::TextureDescriptor::alloc()->init();
    hzbDesc->setTextureType(MTL::TextureType2D);
    hzbDesc->setWidth(width);
    hzbDesc->setHeight(height);
    hzbDesc->setPixelFormat(MTL::PixelFormatR32Float);
    hzbDesc->setUsage(MTL::TextureUsageShaderRead | MTL::TextureUsageShaderWrite);
    hzbDesc->setStorageMode(MTL::StorageModePrivate);
    hzbDesc->setMipmapLevelCount(m_hzbMipCount);
    m_hzbTexture = m_device->newTexture(hzbDesc);
    hzbDesc->release();
    m_hzbMipViews.clear();
    if (m_hzbTexture) {
        m_hzbMipViews.reserve(m_hzbMipCount);
        for (uint32_t mip = 0; mip < m_hzbMipCount; ++mip) {
            NS::Range mipRange = NS::Range::Make(mip, 1);
            NS::Range sliceRange = NS::Range::Make(0, 1);
            MTL::Texture* view = m_hzbTexture->newTextureView(
                MTL::PixelFormatR32Float,
                MTL::TextureType2D,
                mipRange,
                sliceRange
            );
            if (view) {
                m_hzbMipViews.push_back(view);
            }
        }
    }

    MTL::TextureDescriptor* normalDesc = MTL::TextureDescriptor::alloc()->init();
    normalDesc->setTextureType(MTL::TextureType2D);
    normalDesc->setWidth(width);
    normalDesc->setHeight(height);
    normalDesc->setPixelFormat(MTL::PixelFormatRGBA16Float);
    normalDesc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
    normalDesc->setStorageMode(MTL::StorageModePrivate);
    m_normalTexture = m_device->newTexture(normalDesc);
    normalDesc->release();

    MTL::TextureDescriptor* velocityDesc = MTL::TextureDescriptor::alloc()->init();
    velocityDesc->setTextureType(MTL::TextureType2D);
    velocityDesc->setWidth(width);
    velocityDesc->setHeight(height);
    velocityDesc->setPixelFormat(MTL::PixelFormatRG16Float);
    velocityDesc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
    velocityDesc->setStorageMode(MTL::StorageModePrivate);
    m_velocityTexture = m_device->newTexture(velocityDesc);
    velocityDesc->release();

    MTL::TextureDescriptor* dofDesc = MTL::TextureDescriptor::alloc()->init();
    dofDesc->setTextureType(MTL::TextureType2D);
    dofDesc->setWidth(width);
    dofDesc->setHeight(height);
    dofDesc->setPixelFormat(format);
    dofDesc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
    dofDesc->setStorageMode(MTL::StorageModePrivate);
    m_dofTexture = m_device->newTexture(dofDesc);
    dofDesc->release();

    MTL::TextureDescriptor* fogDesc = MTL::TextureDescriptor::alloc()->init();
    fogDesc->setTextureType(MTL::TextureType2D);
    fogDesc->setWidth(width);
    fogDesc->setHeight(height);
    fogDesc->setPixelFormat(format);
    fogDesc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
    fogDesc->setStorageMode(MTL::StorageModePrivate);
    m_fogTexture = m_device->newTexture(fogDesc);
    fogDesc->release();

    MTL::TextureDescriptor* ssaoDesc = MTL::TextureDescriptor::alloc()->init();
    ssaoDesc->setTextureType(MTL::TextureType2D);
    ssaoDesc->setWidth(width);
    ssaoDesc->setHeight(height);
    ssaoDesc->setPixelFormat(MTL::PixelFormatR8Unorm);
    ssaoDesc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
    ssaoDesc->setStorageMode(MTL::StorageModePrivate);
    m_ssaoTexture = m_device->newTexture(ssaoDesc);
    m_ssaoBlurTexture = m_device->newTexture(ssaoDesc);
    ssaoDesc->release();

    uint32_t bloomWidth = std::max(1u, width / 2);
    uint32_t bloomHeight = std::max(1u, height / 2);
    auto calcMipCount = [](uint32_t w, uint32_t h) -> uint32_t {
        uint32_t levels = 1;
        while (levels < 6 && (w > 1 || h > 1)) {
            w = std::max(1u, w / 2);
            h = std::max(1u, h / 2);
            levels++;
        }
        return levels;
    };
    m_bloomMipCount = calcMipCount(bloomWidth, bloomHeight);
    m_bloomMipTextures.reserve(m_bloomMipCount);
    for (uint32_t i = 0; i < m_bloomMipCount; ++i) {
        MTL::TextureDescriptor* bloomDesc = MTL::TextureDescriptor::alloc()->init();
        bloomDesc->setTextureType(MTL::TextureType2D);
        bloomDesc->setWidth(bloomWidth);
        bloomDesc->setHeight(bloomHeight);
        bloomDesc->setPixelFormat(MTL::PixelFormatRGBA16Float);
        bloomDesc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
        bloomDesc->setStorageMode(MTL::StorageModePrivate);
        MTL::Texture* mipTex = m_device->newTexture(bloomDesc);
        bloomDesc->release();
        m_bloomMipTextures.push_back(mipTex);
        bloomWidth = std::max(1u, bloomWidth / 2);
        bloomHeight = std::max(1u, bloomHeight / 2);
    }
    
    if (clampedSamples > 1) {
        MTL::TextureDescriptor* msaaColor = MTL::TextureDescriptor::alloc()->init();
        msaaColor->setTextureType(MTL::TextureType2DMultisample);
        msaaColor->setWidth(width);
        msaaColor->setHeight(height);
        msaaColor->setPixelFormat(format);
        msaaColor->setSampleCount(clampedSamples);
        msaaColor->setUsage(MTL::TextureUsageRenderTarget);
        msaaColor->setStorageMode(MTL::StorageModePrivate);
        m_msaaColorTexture = m_device->newTexture(msaaColor);
        msaaColor->release();
        
        MTL::TextureDescriptor* msaaDepth = MTL::TextureDescriptor::alloc()->init();
        msaaDepth->setTextureType(MTL::TextureType2DMultisample);
        msaaDepth->setWidth(width);
        msaaDepth->setHeight(height);
        msaaDepth->setPixelFormat(MTL::PixelFormatDepth32Float);
        msaaDepth->setSampleCount(clampedSamples);
        msaaDepth->setUsage(MTL::TextureUsageRenderTarget);
        msaaDepth->setStorageMode(MTL::StorageModePrivate);
        m_msaaDepthTexture = m_device->newTexture(msaaDepth);
        msaaDepth->release();
    }

    if (!m_ssaoNoiseTexture) {
        buildSSAONoiseTexture();
    }

    if (formatChanged) {
        buildEnvironmentPipeline();
        buildDebugPipelines();
        buildSSRPipeline();
        buildDecalPipeline();
        buildMotionBlurPipeline();
        buildFogPipeline();
        buildFogVolumePipeline();
        buildDOFPipeline();
        buildTAAPipeline();
    }
}

void Renderer::ensureFogVolume(uint32_t width, uint32_t height, int quality) {
    if (!m_device || width == 0 || height == 0) {
        return;
    }

    int clampedQuality = std::max(0, std::min(2, quality));
    float scale = 0.25f;
    uint32_t depth = 64;
    if (clampedQuality == 0) {
        scale = 0.2f;
        depth = 32;
    } else if (clampedQuality == 2) {
        scale = 0.5f;
        depth = 96;
    }

    uint32_t desiredWidth = std::max(1u, static_cast<uint32_t>(std::round(width * scale)));
    uint32_t desiredHeight = std::max(1u, static_cast<uint32_t>(std::round(height * scale)));

    bool sizeChanged = desiredWidth != m_fogVolumeWidth
        || desiredHeight != m_fogVolumeHeight
        || depth != m_fogVolumeDepth
        || clampedQuality != m_fogVolumeQuality;

    if (!sizeChanged && m_fogVolumeTexture && m_fogVolumeHistoryTexture) {
        return;
    }

    if (m_fogVolumeTexture) {
        m_fogVolumeTexture->release();
        m_fogVolumeTexture = nullptr;
    }
    if (m_fogVolumeHistoryTexture) {
        m_fogVolumeHistoryTexture->release();
        m_fogVolumeHistoryTexture = nullptr;
    }

    m_fogVolumeWidth = desiredWidth;
    m_fogVolumeHeight = desiredHeight;
    m_fogVolumeDepth = depth;
    m_fogVolumeQuality = clampedQuality;
    m_fogVolumeHistoryValid = false;
    m_fogHistoryInitialized = false;

    MTL::TextureDescriptor* fogVolumeDesc = MTL::TextureDescriptor::alloc()->init();
    fogVolumeDesc->setTextureType(MTL::TextureType3D);
    fogVolumeDesc->setWidth(m_fogVolumeWidth);
    fogVolumeDesc->setHeight(m_fogVolumeHeight);
    fogVolumeDesc->setDepth(m_fogVolumeDepth);
    fogVolumeDesc->setPixelFormat(MTL::PixelFormatRGBA16Float);
    fogVolumeDesc->setUsage(MTL::TextureUsageShaderRead | MTL::TextureUsageShaderWrite);
    fogVolumeDesc->setStorageMode(MTL::StorageModePrivate);
    m_fogVolumeTexture = m_device->newTexture(fogVolumeDesc);
    m_fogVolumeHistoryTexture = m_device->newTexture(fogVolumeDesc);
    fogVolumeDesc->release();
}

void Renderer::applyQualitySettings(const SceneQualitySettings& quality) {
    int shadowResolution = std::max(256, std::min(8192, quality.shadowResolution));
    uint32_t msaaSamples = resolveSampleCount(std::max(1, std::min(8, quality.msaaSamples)));
    float renderScale = std::max(0.5f, std::min(2.0f, quality.renderScale));
    SceneQualitySettings clamped = quality;
    clamped.shadowResolution = shadowResolution;
    clamped.msaaSamples = static_cast<int>(msaaSamples);
    clamped.anisotropy = std::max(1, std::min(16, quality.anisotropy));
    clamped.renderScale = renderScale;
    
    const bool shadowResolutionChanged = clamped.shadowResolution != m_qualitySettings.shadowResolution;
    const bool anisotropyChanged = quality.anisotropy != m_qualitySettings.anisotropy;
    const bool msaaChanged = msaaSamples != m_msaaSamples;
    const bool renderScaleChanged = std::abs(renderScale - m_qualitySettings.renderScale) > 0.001f;
    
    m_qualitySettings = clamped;
    
    if (!m_device) {
        return;
    }
    
    if (anisotropyChanged) {
        rebuildSamplerState(clamped.anisotropy);
    }
    
    if (msaaChanged) {
        m_msaaSamples = msaaSamples;
        clearPipelineCache();
        buildEnvironmentPipeline();
        buildDebugPipelines();
        m_renderTargetWidth = 0;
        m_renderTargetHeight = 0;
    }
    
    if (renderScaleChanged) {
        m_renderTargetWidth = 0;
        m_renderTargetHeight = 0;
    }

    if (msaaChanged || renderScaleChanged) {
        storeRenderTargetState(getRenderTargetState(m_activePool));
        invalidateRenderTargetState(m_sceneTargets, msaaSamples);
        invalidateRenderTargetState(m_gameTargets, msaaSamples);
        loadRenderTargetState(getRenderTargetState(m_activePool));
    }
    
    if (shadowResolutionChanged) {
        if (m_shadowPass) {
            m_shadowPass->shutdown();
            if (!m_shadowPass->initialize(m_device, clamped.shadowResolution, 1)) {
                std::cerr << "Warning: ShadowRenderPass failed to reinitialize\n";
            }
        }
        if (m_lightingSystem) {
            m_lightingSystem->configureShadowAtlas(clamped.shadowResolution, 1);
        }
    }
}

void Renderer::uploadMesh(Mesh* mesh) {
    if (!mesh || mesh->isUploaded()) {
        return;
    }
    
    const auto& vertices = mesh->getVertices();
    const auto& indices = mesh->getIndices();
    
    if (vertices.empty() || indices.empty()) {
        std::cerr << "Cannot upload mesh: empty vertices or indices" << std::endl;
        return;
    }
    
    // Create vertex buffer
    size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
    MTL::Buffer* vertexBuffer = m_device->newBuffer(vertices.data(), vertexBufferSize, MTL::ResourceStorageModeShared);
    mesh->setVertexBuffer(vertexBuffer);
    
    // Create index buffer
    size_t indexBufferSize = indices.size() * sizeof(uint32_t);
    MTL::Buffer* indexBuffer = m_device->newBuffer(indices.data(), indexBufferSize, MTL::ResourceStorageModeShared);
    mesh->setIndexBuffer(indexBuffer);

    if (mesh->hasSkinWeights()) {
        const auto& skinWeights = mesh->getSkinWeights();
        if (skinWeights.size() == vertices.size()) {
            struct SkinWeightGPU {
                uint32_t indices[4];
                float weights[4];
            };
            std::vector<SkinWeightGPU> packed;
            packed.resize(skinWeights.size());
            for (size_t i = 0; i < skinWeights.size(); ++i) {
                for (int j = 0; j < 4; ++j) {
                    packed[i].indices[j] = skinWeights[i].indices[j];
                    packed[i].weights[j] = skinWeights[i].weights[j];
                }
            }
            size_t skinBufferSize = packed.size() * sizeof(SkinWeightGPU);
            MTL::Buffer* skinBuffer = m_device->newBuffer(packed.data(), skinBufferSize, MTL::ResourceStorageModeShared);
            mesh->setSkinWeightBuffer(skinBuffer);
        } else {
            std::cerr << "Skin weights size mismatch for mesh: " << mesh->getName() << std::endl;
        }
    }
    
    mesh->setUploaded(true);
}

void Renderer::destroyMesh(Mesh* mesh) {
    if (!mesh || !mesh->isUploaded()) {
        return;
    }
    
    MTL::Buffer* vertexBuffer = static_cast<MTL::Buffer*>(mesh->getVertexBuffer());
    MTL::Buffer* indexBuffer = static_cast<MTL::Buffer*>(mesh->getIndexBuffer());
    
    if (vertexBuffer) {
        vertexBuffer->release();
        mesh->setVertexBuffer(nullptr);
    }
    
    if (indexBuffer) {
        indexBuffer->release();
        mesh->setIndexBuffer(nullptr);
    }

    MTL::Buffer* skinBuffer = static_cast<MTL::Buffer*>(mesh->getSkinWeightBuffer());
    if (skinBuffer) {
        skinBuffer->release();
        mesh->setSkinWeightBuffer(nullptr);
    }
    
    mesh->setUploaded(false);
}

void Renderer::render() {
    if (!m_isInitialized || !m_metalLayer) {
        return;
    }
    
    auto startTime = std::chrono::high_resolution_clock::now();
    m_stats.reset();
    
    // Get active scene
    Scene* activeScene = SceneManager::getInstance().getActiveScene();
    if (!activeScene) {
        return;
    }
    
    renderScene(activeScene);
    
    auto endTime = std::chrono::high_resolution_clock::now();
    m_stats.frameTime = std::chrono::duration<float, std::milli>(endTime - startTime).count();
}

void Renderer::renderScene(Scene* scene) {
    RenderOptions options;
    renderScene(scene, nullptr, options);
}

void Renderer::renderScene(Scene* scene, Camera* cameraOverride, const RenderOptions& options) {
    if (!scene) return;

    // Get main camera
    Camera* camera = cameraOverride ? cameraOverride : Camera::getMainCamera();
    if (!camera) {
        static bool loggedOnce = false;
        if (!loggedOnce) {
            std::cerr << "No main camera found!" << std::endl;
            loggedOnce = true;
        }
        return;
    }

    struct StatsScope {
        Renderer* renderer;
        std::chrono::high_resolution_clock::time_point start;
        explicit StatsScope(Renderer* r)
        : renderer(r)
        , start(std::chrono::high_resolution_clock::now()) {
            if (renderer) {
                renderer->m_stats.reset();
            }
        }
        ~StatsScope() {
            if (!renderer) return;
            auto endTime = std::chrono::high_resolution_clock::now();
            renderer->m_stats.frameTime =
                std::chrono::duration<float, std::milli>(endTime - start).count();
        }
    } statsScope(this);

    if (m_viewportHeight > 0.0f) {
        float aspectRatio = m_viewportWidth / m_viewportHeight;
        camera->setAspectRatio(aspectRatio);
    }
    
    float renderScale = std::max(0.5f, std::min(2.0f, m_qualitySettings.renderScale));
    const auto& post = scene->getSettings().postProcess;
    const auto& fog = scene->getSettings().fog;
    bool allowTemporal = options.allowTemporal;
    bool bloomEnabled = post.enabled && post.bloom;
    bool toneMappingEnabled = post.enabled && post.toneMapping;
    bool colorGradingEnabled = post.enabled && post.colorGrading;
    bool taaEnabled = allowTemporal && post.enabled && post.taa;
    bool ssrEnabled = post.enabled && post.ssr;
    bool motionBlurEnabled = allowTemporal && post.enabled && post.motionBlur;
    bool dofEnabled = post.enabled && post.depthOfField;
    bool fogEnabled = fog.enabled;
    if (!fogEnabled) {
        m_fogVolumeHistoryValid = false;
        m_fogHistoryInitialized = false;
    }
    bool useOffscreen = bloomEnabled || toneMappingEnabled || colorGradingEnabled
        || taaEnabled || ssrEnabled || motionBlurEnabled || dofEnabled || fogEnabled
        || std::abs(renderScale - 1.0f) > 0.001f;
    bool hdrPost = bloomEnabled || toneMappingEnabled || colorGradingEnabled;
    int desiredColorFormat = hdrPost ? static_cast<int>(MTL::PixelFormatRGBA16Float)
                                     : static_cast<int>(MTL::PixelFormatBGRA8Unorm);
    m_outputHDR = hdrPost;
    uint32_t renderWidth = static_cast<uint32_t>(std::max(1.0f, std::round(m_viewportWidth * renderScale)));
    uint32_t renderHeight = static_cast<uint32_t>(std::max(1.0f, std::round(m_viewportHeight * renderScale)));
    uint32_t targetWidth = std::max(renderWidth, m_renderTargetWidth);
    uint32_t targetHeight = std::max(renderHeight, m_renderTargetHeight);
    ensureRenderTargets(targetWidth, targetHeight, m_qualitySettings.msaaSamples, desiredColorFormat);
    if (fogEnabled) {
        ensureFogVolume(renderWidth, renderHeight, fog.volumetricQuality);
    }
    bool useMSAA = m_msaaSamples > 1;
    bool resolveToDrawable = useMSAA && !useOffscreen;
    if ((useOffscreen || useMSAA) && !m_colorTexture) {
        std::cerr << "Render targets missing, skipping frame\n";
        return;
    }
    if (useMSAA && !m_msaaColorTexture) {
        std::cerr << "MSAA color target missing, skipping frame\n";
        return;
    }

    Math::Matrix4x4 viewMatrix = camera->getViewMatrix();
    Math::Matrix4x4 projectionMatrix = camera->getProjectionMatrix();
    Math::Matrix4x4 projectionMatrixNoJitter = projectionMatrix;
    Math::Matrix4x4 viewProjectionNoJitter = projectionMatrix * viewMatrix;
    const auto frustumPlanes = ExtractFrustumPlanes(viewProjectionNoJitter);
    if (taaEnabled) {
        uint32_t jitterIndex = ++m_taaFrameIndex;
        float jitterX = HaltonSequence(jitterIndex, 2) - 0.5f;
        float jitterY = HaltonSequence(jitterIndex, 3) - 0.5f;
        float ndcX = (jitterX * 2.0f) / std::max(1.0f, static_cast<float>(renderWidth));
        float ndcY = (jitterY * 2.0f) / std::max(1.0f, static_cast<float>(renderHeight));
        projectionMatrix(0, 2) += ndcX;
        projectionMatrix(1, 2) += ndcY;
    } else if (options.updateHistory) {
        m_taaFrameIndex = 0;
        m_taaHistoryValid = false;
    }
    Math::Matrix4x4 viewProjection = projectionMatrix * viewMatrix;
    
    // Get drawable
    CA::MetalDrawable* drawable = m_metalLayer->nextDrawable();
    if (!drawable) {
        return;
    }
    if (resolveToDrawable) {
        MTL::Texture* drawableTex = drawable->texture();
        if (!drawableTex || drawableTex->width() != renderWidth || drawableTex->height() != renderHeight) {
            resolveToDrawable = false;
        }
    }
    
    // Create command buffer
    MTL::CommandBuffer* commandBuffer = m_commandQueue->commandBuffer();

    size_t skinningOffset = 0;
    size_t prevSkinningOffset = 0;
    m_instanceBufferOffset = 0;
    auto allocateSkinningSlice = [&](MTL::Buffer*& buffer,
                                     size_t& capacity,
                                     size_t& offset,
                                     size_t bytes,
                                     size_t& outOffset) -> bool {
        constexpr size_t kAlignment = 256;
        size_t alignedOffset = (offset + (kAlignment - 1)) & ~(kAlignment - 1);
        size_t required = alignedOffset + bytes;
        if (!buffer || required > capacity) {
            size_t newCapacity = std::max(required, capacity > 0 ? capacity * 2 : required);
            if (buffer) {
                buffer->release();
            }
            buffer = m_device->newBuffer(newCapacity, MTL::ResourceStorageModeShared);
            capacity = buffer ? buffer->length() : 0;
            alignedOffset = 0;
            required = bytes;
        }
        if (!buffer || capacity < required) {
            return false;
        }
        outOffset = alignedOffset;
        offset = alignedOffset + bytes;
        return true;
    };

    auto allocateInstanceSlice = [&](size_t bytes, size_t& outOffset) -> bool {
        constexpr size_t kAlignment = 256;
        size_t alignedOffset = (m_instanceBufferOffset + (kAlignment - 1)) & ~(kAlignment - 1);
        size_t required = alignedOffset + bytes;
        if (!m_instanceBuffer || required > m_instanceBufferCapacity) {
            size_t newCapacity = std::max(required, m_instanceBufferCapacity > 0 ? m_instanceBufferCapacity * 2 : required);
            if (m_instanceBuffer) {
                m_instanceBuffer->release();
            }
            m_instanceBuffer = m_device->newBuffer(newCapacity, MTL::ResourceStorageModeShared);
            m_instanceBufferCapacity = m_instanceBuffer ? m_instanceBuffer->length() : 0;
            alignedOffset = 0;
            required = bytes;
        }
        if (!m_instanceBuffer || m_instanceBufferCapacity < required) {
            return false;
        }
        outOffset = alignedOffset;
        m_instanceBufferOffset = alignedOffset + bytes;
        return true;
    };
    
    // Prepare lighting once so both shadow and clustered passes use fresh data
    if (m_lightingSystem) {
        m_lightingSystem->beginFrame(scene, camera, renderWidth, renderHeight);
        m_lightingSystem->setDebugDrawAtlas(m_debugDrawShadowAtlas);
        
        const auto& gpuLights = m_lightingSystem->getGPULights();
        const auto& gpuShadows = m_lightingSystem->getGPUShadows();
        
        auto ensureBuffer = [&](MTL::Buffer*& buf, size_t bytes, const void* src) {
            if (!buf || buf->length() < bytes) {
                if (buf) { buf->release(); }
                buf = m_device->newBuffer(bytes, MTL::ResourceStorageModeShared);
            }
            if (buf && src) {
                std::memcpy(buf->contents(), src, bytes);
            }
        };
        
        if (!gpuLights.empty()) {
            ensureBuffer(m_lightGPUBuffer, gpuLights.size() * sizeof(LightGPUData), gpuLights.data());
        } else {
            // Keep a tiny dummy buffer bound to satisfy shader validation
            ensureBuffer(m_lightGPUBuffer, sizeof(LightGPUData), nullptr);
            if (m_lightGPUBuffer) {
                std::memset(m_lightGPUBuffer->contents(), 0, m_lightGPUBuffer->length());
            }
        }
        if (!gpuShadows.empty()) {
            ensureBuffer(m_shadowGPUBuffer, gpuShadows.size() * sizeof(ShadowGPUData), gpuShadows.data());
        } else {
            // Bind a dummy shadow buffer so fragment binding slot 5 is always valid
            ensureBuffer(m_shadowGPUBuffer, sizeof(ShadowGPUData), nullptr);
            if (m_shadowGPUBuffer) {
                std::memset(m_shadowGPUBuffer->contents(), 0, m_shadowGPUBuffer->length());
            }
        }
        uint32_t count = static_cast<uint32_t>(gpuLights.size());
        ensureBuffer(m_lightCountBuffer, sizeof(uint32_t), &count);
        
        ClusterParams clusterParams{};
        clusterParams.projection = projectionMatrix;
        clusterParams.projectionInv = clusterParams.projection.inversed();
        clusterParams.clusterX = m_clusterPass ? m_clusterPass->getClusterX() : 16;
        clusterParams.clusterY = m_clusterPass ? m_clusterPass->getClusterY() : 9;
        clusterParams.clusterZ = m_clusterPass ? m_clusterPass->getClusterZ() : 24;
        clusterParams.lightCount = static_cast<uint32_t>(m_lightingSystem->getVisibleLightCount());
        clusterParams.maxLightsPerCluster = 64;
        clusterParams.screenWidth = static_cast<float>(renderWidth);
        clusterParams.screenHeight = static_cast<float>(renderHeight);
        clusterParams.nearPlane = camera->getNearClip();
        clusterParams.farPlane = camera->getFarClip();
        ensureBuffer(m_clusterParamsBuffer, sizeof(ClusterParams), &clusterParams);
    }
    
    // Build clustered light lists
    if (m_clusterPass && m_lightGPUBuffer) {
        m_clusterPass->dispatch(
            commandBuffer,
            *m_lightingSystem,
            camera->getProjectionMatrix(),
            camera->getNearClip(),
            camera->getFarClip(),
            renderWidth,
            renderHeight,
            m_lightGPUBuffer
        );
        m_clusterHeaderBuffer = m_clusterPass->getClusterHeaders();
        m_clusterIndexBuffer = m_clusterPass->getClusterIndices();
    }

    MTL::Viewport viewport = {
        0.0, 0.0,
        static_cast<double>(renderWidth), static_cast<double>(renderHeight),
        0.0, 1.0
    };
    
    // Collect lights
    std::vector<Light*> lights;
    Light* mainLight = Light::getMainLight();
    if (mainLight) {
        lights.push_back(mainLight);
    }
    
    // Setup camera uniforms
    CameraUniforms* cameraUniforms = static_cast<CameraUniforms*>(m_cameraUniformBuffer->contents());
    cameraUniforms->viewMatrix = viewMatrix;
    cameraUniforms->projectionMatrix = projectionMatrix;
    cameraUniforms->projectionMatrixNoJitter = projectionMatrixNoJitter;
    cameraUniforms->projectionMatrixNoJitterInverse = projectionMatrixNoJitter.inversed();
    cameraUniforms->viewProjectionMatrix = viewProjection;
    cameraUniforms->viewMatrixInverse = cameraUniforms->viewMatrix.inversed();
    cameraUniforms->projectionMatrixInverse = cameraUniforms->projectionMatrix.inversed();
    Math::Vector3 camPos = camera->getEntity()->getTransform()->getPosition();
    cameraUniforms->cameraPositionTime = Math::Vector4(camPos.x, camPos.y, camPos.z, Time::time());

    std::unordered_set<std::string> hlodHidden;
    std::unordered_set<std::string> hlodActiveProxies;
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
            float dist = (center - camPos).length();
            bool active = dist >= proxy->getLodStart();
            if (!active) {
                continue;
            }
            std::string proxyId = entity->getUUID().toString();
            hlodActiveProxies.insert(proxyId);
            for (const auto& src : proxy->getSourceUuids()) {
                hlodHidden.insert(src);
            }
        }
    }

    auto shouldSkipEntity = [&](Entity* entity) -> bool {
        if (!entity) {
            return true;
        }
        std::string id = entity->getUUID().toString();
        if (hlodHidden.find(id) != hlodHidden.end()) {
            return true;
        }
        if (auto* proxy = entity->getComponent<HLODProxy>()) {
            return hlodActiveProxies.find(id) == hlodActiveProxies.end();
        }
        return false;
    };

    // Ensure meshes are uploaded before any pass uses them
    const auto& entitiesPre = scene->getAllEntities();
    for (const auto& entPtr : entitiesPre) {
        if (shouldSkipEntity(entPtr.get())) {
            continue;
        }
        MeshRenderer* mr = entPtr->getComponent<MeshRenderer>();
        if (!mr || !mr->isEnabled()) continue;
        std::shared_ptr<Mesh> mesh = mr->getMesh();
        if (mesh && !mesh->isUploaded()) {
            uploadMesh(mesh.get());
        }
    }

    auto resolveCullMode = [](Material* material) -> MTL::CullMode {
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
    };
    
    struct InstancedBatchKey {
        Mesh* mesh = nullptr;
        Mesh* sourceMesh = nullptr;
        Material* material = nullptr;
        bool isTransparent = false;
        bool receiveShadows = true;
        bool castShadows = true;

        bool operator==(const InstancedBatchKey& other) const {
            return mesh == other.mesh &&
                   sourceMesh == other.sourceMesh &&
                   material == other.material &&
                   isTransparent == other.isTransparent &&
                   receiveShadows == other.receiveShadows &&
                   castShadows == other.castShadows;
        }
    };

    struct InstancedBatchKeyHash {
        size_t operator()(const InstancedBatchKey& key) const {
            size_t h = reinterpret_cast<size_t>(key.mesh);
            h ^= reinterpret_cast<size_t>(key.sourceMesh) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            h ^= reinterpret_cast<size_t>(key.material) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            h ^= static_cast<size_t>(key.isTransparent) << 1;
            h ^= static_cast<size_t>(key.receiveShadows) << 2;
            h ^= static_cast<size_t>(key.castShadows) << 3;
            return h;
        }
    };

    struct InstancedBatch {
        InstancedBatchKey key;
        std::shared_ptr<Material> material;
        std::vector<InstanceDataGPU> instances;
        Math::Vector3 boundsCenter;
        Math::Vector3 boundsSize;
    };

    struct InstancedDraw {
        Mesh* mesh = nullptr;
        std::shared_ptr<Material> material;
        bool isTransparent = false;
        bool receiveShadows = true;
        size_t instanceOffset = 0;
        uint32_t instanceCount = 0;
        Math::Vector3 boundsCenter;
        Math::Vector3 boundsSize;
        bool isBillboard = false;
    };

    struct InstanceBatchGPU {
        Mesh* mesh = nullptr;
        Mesh* sourceMesh = nullptr;
        std::shared_ptr<Material> material;
        bool isTransparent = false;
        bool receiveShadows = true;
        bool castShadows = true;
        uint32_t inputOffset = 0;
        uint32_t inputCount = 0;
        uint32_t outputOffset = 0;
        Math::Vector3 boundsCenter;
        Math::Vector3 boundsSize;
        bool isBillboard = false;
    };

    std::unordered_map<InstancedBatchKey, InstancedBatch, InstancedBatchKeyHash> instancedVisible;
    std::unordered_map<InstancedBatchKey, InstancedBatch, InstancedBatchKeyHash> instancedShadow;
    std::vector<InstancedDraw> instancedDraws;
    std::vector<InstancedShadowDraw> instancedShadowDraws;
    std::vector<InstanceBatchGPU> instancedBatches;
    std::unordered_set<Entity*> gpuCulledStatics;
    std::unordered_set<std::string> gpuCulledStaticIds;
    std::shared_ptr<Mesh> billboardMesh = m_billboardMesh;
    if (billboardMesh && !billboardMesh->isUploaded()) {
        uploadMesh(billboardMesh.get());
    }
    Math::Vector3 cameraPos = camera->getEntity()->getTransform()->getPosition();

    {
        const auto& instancedEntities = scene->getAllEntities();
        for (const auto& entityPtr : instancedEntities) {
            Entity* entity = entityPtr.get();
            if (!entity || !entity->isActiveInHierarchy()) {
                continue;
            }
            InstancedMeshRenderer* instanced = entity->getComponent<InstancedMeshRenderer>();
            if (!instanced || !instanced->isEnabled()) {
                continue;
            }
            std::shared_ptr<Mesh> mesh = instanced->getMesh();
            if (!mesh) {
                continue;
            }
            if (!mesh->isUploaded()) {
                uploadMesh(mesh.get());
            }

            std::shared_ptr<Material> material = instanced->getMaterial(0);
            bool isTransparent = false;
            if (material) {
                isTransparent = material->getRenderMode() == Material::RenderMode::Transparent
                    || material->getAlpha() < 0.999f;
            }

            Math::Vector3 meshCenter = mesh->getBoundsCenter();
            Math::Vector3 meshSize = mesh->getBoundsSize();
            bool billboardEnabled = material && material->getBillboardEnabled() && billboardMesh;
            float billboardStart = material ? material->getBillboardStart() : 0.0f;
            float billboardEnd = material ? material->getBillboardEnd() : 0.0f;
            bool billboardRangeValid = billboardEnabled && billboardEnd > billboardStart + 0.01f;

            const auto& instanceTransforms = instanced->getInstances();
            if (instanceTransforms.empty()) {
                continue;
            }

            Math::Matrix4x4 parentMatrix = entity->getTransform()->getWorldMatrix();

            for (const auto& local : instanceTransforms) {
                Math::Matrix4x4 world = parentMatrix * local;

                InstanceDataGPU data{};
                data.modelMatrix = world;
                data.normalMatrix = world.inversed().transposed();

                if (instanced->getCastShadows()) {
                    auto addShadowInstance = [&](Mesh* drawMesh, Mesh* sourceMesh) {
                        InstancedBatchKey key{};
                        key.mesh = drawMesh;
                        key.sourceMesh = sourceMesh;
                        key.material = material.get();
                        key.isTransparent = isTransparent;
                        key.receiveShadows = instanced->getReceiveShadows();
                        key.castShadows = instanced->getCastShadows();
                        auto& batch = instancedShadow[key];
                        batch.key = key;
                        batch.material = material;
                        batch.boundsCenter = meshCenter;
                        batch.boundsSize = meshSize;
                        batch.instances.push_back(data);
                    };

                    if (billboardRangeValid) {
                        Math::Vector3 worldCenter = world.transformPoint(meshCenter);
                        float dist = (worldCenter - cameraPos).length();
                        if (dist <= billboardStart) {
                            addShadowInstance(mesh.get(), mesh.get());
                        } else if (dist >= billboardEnd) {
                            addShadowInstance(billboardMesh.get(), mesh.get());
                        } else {
                            addShadowInstance(mesh.get(), mesh.get());
                            addShadowInstance(billboardMesh.get(), mesh.get());
                        }
                    } else {
                        addShadowInstance(mesh.get(), mesh.get());
                    }
                }
                auto addVisibleInstance = [&](Mesh* drawMesh, Mesh* sourceMesh) {
                    InstancedBatchKey key{};
                    key.mesh = drawMesh;
                    key.sourceMesh = sourceMesh;
                    key.material = material.get();
                    key.isTransparent = isTransparent;
                    key.receiveShadows = instanced->getReceiveShadows();
                    key.castShadows = instanced->getCastShadows();
                    auto& batch = instancedVisible[key];
                    batch.key = key;
                    batch.material = material;
                    batch.boundsCenter = meshCenter;
                    batch.boundsSize = meshSize;
                    batch.instances.push_back(data);
                };

                if (billboardRangeValid) {
                    Math::Vector3 worldCenter = world.transformPoint(meshCenter);
                    float dist = (worldCenter - cameraPos).length();
                    if (dist <= billboardStart) {
                        addVisibleInstance(mesh.get(), mesh.get());
                    } else if (dist >= billboardEnd) {
                        addVisibleInstance(billboardMesh.get(), mesh.get());
                    } else {
                        addVisibleInstance(mesh.get(), mesh.get());
                        addVisibleInstance(billboardMesh.get(), mesh.get());
                    }
                } else {
                    addVisibleInstance(mesh.get(), mesh.get());
                }
            }
        }
    }

    {
        const auto& staticEntities = scene->getAllEntities();
        for (const auto& entityPtr : staticEntities) {
            Entity* entity = entityPtr.get();
            if (!entity || !entity->isActiveInHierarchy()) {
                continue;
            }
            if (shouldSkipEntity(entity)) {
                continue;
            }

            if (auto* instanced = entity->getComponent<InstancedMeshRenderer>()) {
                if (instanced->isEnabled()) {
                    continue;
                }
            }

            MeshRenderer* mr = entity->getComponent<MeshRenderer>();
            if (!mr || !mr->isEnabled()) {
                continue;
            }
            std::shared_ptr<Mesh> mesh = mr->getMesh();
            if (!mesh) {
                continue;
            }

            SkinnedMeshRenderer* skinned = entity->getComponent<SkinnedMeshRenderer>();
            bool wantsSkin = skinned && skinned->isEnabled() && mesh->hasSkinWeights() && !skinned->getBoneMatrices().empty();
            if (wantsSkin) {
                continue;
            }

            if (!mesh->isUploaded()) {
                uploadMesh(mesh.get());
            }

            std::shared_ptr<Material> material = mr->getMaterial(0);
            bool isTransparent = false;
            if (material) {
                isTransparent = material->getRenderMode() == Material::RenderMode::Transparent
                    || material->getAlpha() < 0.999f;
            }

            Math::Vector3 meshCenter = mesh->getBoundsCenter();
            Math::Vector3 meshSize = mesh->getBoundsSize();
            bool billboardEnabled = material && material->getBillboardEnabled() && billboardMesh;
            float billboardStart = material ? material->getBillboardStart() : 0.0f;
            float billboardEnd = material ? material->getBillboardEnd() : 0.0f;
            bool billboardRangeValid = billboardEnabled && billboardEnd > billboardStart + 0.01f;

            Math::Matrix4x4 world = entity->getTransform()->getWorldMatrix();
            Math::Vector3 worldCenter = world.transformPoint(meshCenter);
            Math::Vector3 scale = entity->getTransform()->getScale();
            float maxScale = std::max(std::abs(scale.x), std::max(std::abs(scale.y), std::abs(scale.z)));
            float radius = 0.5f * std::sqrt(meshSize.x * meshSize.x
                                            + meshSize.y * meshSize.y
                                            + meshSize.z * meshSize.z) * maxScale;
            if (radius <= 0.0f) {
                radius = 0.001f;
            }
            radius *= kCullTightness;
            if (!IsSphereInFrustum(frustumPlanes, worldCenter, radius)) {
                continue;
            }

            InstanceDataGPU data{};
            data.modelMatrix = world;
            data.normalMatrix = world.inversed().transposed();

            auto addVisibleInstance = [&](Mesh* drawMesh, Mesh* sourceMesh) {
                InstancedBatchKey key{};
                key.mesh = drawMesh;
                key.sourceMesh = sourceMesh;
                key.material = material.get();
                key.isTransparent = isTransparent;
                key.receiveShadows = mr->getReceiveShadows();
                key.castShadows = mr->getCastShadows();
                auto& batch = instancedVisible[key];
                batch.key = key;
                batch.material = material;
                batch.boundsCenter = meshCenter;
                batch.boundsSize = meshSize;
                batch.instances.push_back(data);
            };

            auto addShadowInstance = [&](Mesh* drawMesh, Mesh* sourceMesh) {
                InstancedBatchKey key{};
                key.mesh = drawMesh;
                key.sourceMesh = sourceMesh;
                key.material = material.get();
                key.isTransparent = isTransparent;
                key.receiveShadows = mr->getReceiveShadows();
                key.castShadows = mr->getCastShadows();
                auto& batch = instancedShadow[key];
                batch.key = key;
                batch.material = material;
                batch.boundsCenter = meshCenter;
                batch.boundsSize = meshSize;
                batch.instances.push_back(data);
            };

            if (billboardRangeValid) {
                float dist = (worldCenter - cameraPos).length();
                if (dist <= billboardStart) {
                    addVisibleInstance(mesh.get(), mesh.get());
                    if (mr->getCastShadows()) {
                        addShadowInstance(mesh.get(), mesh.get());
                    }
                } else if (dist >= billboardEnd) {
                    addVisibleInstance(billboardMesh.get(), mesh.get());
                    if (mr->getCastShadows()) {
                        addShadowInstance(billboardMesh.get(), mesh.get());
                    }
                } else {
                    addVisibleInstance(mesh.get(), mesh.get());
                    addVisibleInstance(billboardMesh.get(), mesh.get());
                    if (mr->getCastShadows()) {
                        addShadowInstance(mesh.get(), mesh.get());
                        addShadowInstance(billboardMesh.get(), mesh.get());
                    }
                }
            } else {
                addVisibleInstance(mesh.get(), mesh.get());
                if (mr->getCastShadows()) {
                    addShadowInstance(mesh.get(), mesh.get());
                }
            }

            gpuCulledStatics.insert(entity);
            gpuCulledStaticIds.insert(entity->getUUID().toString());
        }
    }

    auto align256 = [](size_t value) {
        constexpr size_t kAlignment = 256;
        return (value + (kAlignment - 1)) & ~(kAlignment - 1);
    };

    uint32_t totalInputCount = 0;
    instancedBatches.reserve(instancedVisible.size());
    for (auto& entry : instancedVisible) {
        auto& batch = entry.second;
        if (batch.instances.empty()) {
            continue;
        }
        InstanceBatchGPU gpu{};
        gpu.mesh = batch.key.mesh;
        gpu.sourceMesh = batch.key.sourceMesh;
        gpu.material = batch.material;
        gpu.isTransparent = batch.key.isTransparent;
        gpu.receiveShadows = batch.key.receiveShadows;
        gpu.castShadows = batch.key.castShadows;
        gpu.inputOffset = totalInputCount;
        gpu.inputCount = static_cast<uint32_t>(batch.instances.size());
        gpu.outputOffset = totalInputCount;
        gpu.boundsCenter = batch.boundsCenter;
        gpu.boundsSize = batch.boundsSize;
        gpu.isBillboard = billboardMesh && batch.key.mesh == billboardMesh.get();
        totalInputCount += gpu.inputCount;
        instancedBatches.push_back(gpu);
    }
    m_stats.instanceInput = totalInputCount;

    size_t totalInputBytes = totalInputCount * sizeof(InstanceDataGPU);
    if (totalInputBytes > 0) {
        if (!m_instanceBuffer || m_instanceBufferCapacity < totalInputBytes) {
            size_t newCapacity = std::max(totalInputBytes, m_instanceBufferCapacity > 0 ? m_instanceBufferCapacity * 2 : totalInputBytes);
            if (m_instanceBuffer) {
                m_instanceBuffer->release();
            }
            m_instanceBuffer = m_device->newBuffer(newCapacity, MTL::ResourceStorageModeShared);
            m_instanceBufferCapacity = m_instanceBuffer ? m_instanceBuffer->length() : 0;
        }
    }

    if (m_instanceBuffer && totalInputCount > 0) {
        for (auto& entry : instancedVisible) {
            auto& batch = entry.second;
            if (batch.instances.empty()) {
                continue;
            }
            uint32_t offset = 0;
            for (const auto& gpu : instancedBatches) {
                if (gpu.mesh == batch.key.mesh && gpu.material.get() == batch.material.get() &&
                    gpu.sourceMesh == batch.key.sourceMesh &&
                    gpu.isTransparent == batch.key.isTransparent &&
                    gpu.receiveShadows == batch.key.receiveShadows &&
                    gpu.castShadows == batch.key.castShadows) {
                    offset = gpu.inputOffset;
                    break;
                }
            }
            size_t bytes = batch.instances.size() * sizeof(InstanceDataGPU);
            std::memcpy(static_cast<uint8_t*>(m_instanceBuffer->contents()) + offset * sizeof(InstanceDataGPU),
                        batch.instances.data(),
                        bytes);
        }
    }

    for (auto& entry : instancedShadow) {
        auto& batch = entry.second;
        if (batch.instances.empty()) {
            continue;
        }
        uint32_t offset = 0;
        for (const auto& gpu : instancedBatches) {
            if (gpu.mesh == batch.key.mesh && gpu.material.get() == batch.material.get() &&
                gpu.sourceMesh == batch.key.sourceMesh &&
                gpu.isTransparent == batch.key.isTransparent &&
                gpu.receiveShadows == batch.key.receiveShadows &&
                gpu.castShadows == batch.key.castShadows) {
                offset = gpu.inputOffset;
                break;
            }
        }
        InstancedShadowDraw draw{};
        draw.mesh = batch.key.mesh;
        draw.instanceBuffer = m_instanceBuffer;
        draw.instanceOffset = offset * sizeof(InstanceDataGPU);
        draw.instanceCount = static_cast<uint32_t>(batch.instances.size());
        draw.material = batch.material;
        draw.boundsCenter = batch.boundsCenter;
        draw.boundsSize = batch.boundsSize;
        draw.isBillboard = billboardMesh && batch.key.mesh == billboardMesh.get();
        instancedShadowDraws.push_back(draw);
    }

    // Render shadow maps first
    if (m_shadowPass && m_lightingSystem) {
        m_shadowPass->setExtraHiddenEntities(gpuCulledStaticIds);
        m_shadowPass->execute(commandBuffer, scene, camera, *m_lightingSystem, instancedShadowDraws);
    }

    const bool useGpuInstanceCulling = m_instanceCullPipeline && m_instanceIndirectPipeline && totalInputCount > 0;
    auto resetInstanceCullingBuffers = [&]() {
        if (m_instanceCountBuffer) {
            std::memset(m_instanceCountBuffer->contents(), 0, instancedBatches.size() * sizeof(uint32_t));
        }
        if (m_instanceIndirectBuffer) {
            auto* args = static_cast<DrawIndexedIndirectArgs*>(m_instanceIndirectBuffer->contents());
            for (size_t i = 0; i < instancedBatches.size(); ++i) {
                args[i].indexCount = instancedBatches[i].mesh ? instancedBatches[i].mesh->getIndices().size() : 0;
                args[i].instanceCount = 0;
                args[i].indexStart = 0;
                args[i].baseVertex = 0;
                args[i].baseInstance = 0;
            }
        }
    };
    if (useGpuInstanceCulling && m_instanceCountBuffer && !instancedBatches.empty()) {
        const uint32_t* counts = static_cast<const uint32_t*>(m_instanceCountBuffer->contents());
        uint64_t visibleSum = 0;
        for (size_t i = 0; i < instancedBatches.size(); ++i) {
            visibleSum += counts[i];
        }
        m_stats.instanceVisible = static_cast<uint32_t>(
            std::min<uint64_t>(visibleSum, std::numeric_limits<uint32_t>::max()));
    } else {
        m_stats.instanceVisible = m_stats.instanceInput;
    }

    auto dispatchInstanceCulling = [&](MTL::ComputePipelineState* cullPipeline, bool useHzb) {
        if (!cullPipeline || !m_instanceCullBuffer || !m_instanceCountBuffer || !m_instanceIndirectBuffer) {
            return;
        }

        resetInstanceCullingBuffers();

        MTL::ComputeCommandEncoder* cullEncoder = commandBuffer->computeCommandEncoder();
        cullEncoder->setComputePipelineState(cullPipeline);
        cullEncoder->setBuffer(m_instanceBuffer, 0, 0);
        cullEncoder->setBuffer(m_instanceCullBuffer, 0, 1);
        if (useHzb && m_hzbTexture) {
            cullEncoder->setTexture(m_hzbTexture, 0);
            cullEncoder->setBuffer(m_cameraUniformBuffer, 0, 4);
        }

        const auto frustumPlanes = ExtractFrustumPlanes(viewProjectionNoJitter);
        Math::Vector2 screenSize(
            static_cast<float>(m_depthTexture ? m_depthTexture->width() : targetWidth),
            static_cast<float>(m_depthTexture ? m_depthTexture->height() : targetHeight)
        );
        uint32_t hzbMipCount = std::max(1u, m_hzbMipCount);

        for (size_t i = 0; i < instancedBatches.size(); ++i) {
            const auto& batch = instancedBatches[i];
            Math::Vector3 meshCenter = batch.boundsCenter;
            Math::Vector3 meshSize = batch.boundsSize;
            float baseRadius = 0.5f * meshSize.length() * kCullTightness;

            InstanceCullParams params{};
            for (int p = 0; p < 6; ++p) {
                params.frustumPlanes[p] = frustumPlanes[p];
            }
            params.boundsCenterRadius = Math::Vector4(meshCenter.x, meshCenter.y, meshCenter.z, baseRadius);
            params.inputOffset = batch.inputOffset;
            params.outputOffset = batch.outputOffset;
            params.instanceCount = batch.inputCount;
            params.screenSize = screenSize;
            params.hzbMipCount = hzbMipCount;

            cullEncoder->setBuffer(m_instanceCountBuffer, i * sizeof(uint32_t), 2);
            cullEncoder->setBytes(&params, sizeof(InstanceCullParams), 3);

            const uint32_t threads = 64;
            const uint32_t grid = (batch.inputCount + threads - 1) / threads;
            MTL::Size tgSize = MTL::Size(threads, 1, 1);
            MTL::Size gridSize = MTL::Size(grid * threads, 1, 1);
            cullEncoder->dispatchThreads(gridSize, tgSize);
        }
        cullEncoder->endEncoding();

        MTL::ComputeCommandEncoder* indirectEncoder = commandBuffer->computeCommandEncoder();
        indirectEncoder->setComputePipelineState(m_instanceIndirectPipeline);
        indirectEncoder->setBuffer(m_instanceCountBuffer, 0, 0);
        indirectEncoder->setBuffer(m_instanceIndirectBuffer, 0, 1);
        const uint32_t batchCount = static_cast<uint32_t>(instancedBatches.size());
        if (batchCount > 0) {
            const uint32_t threads = 64;
            const uint32_t grid = (batchCount + threads - 1) / threads;
            MTL::Size tgSize = MTL::Size(threads, 1, 1);
            MTL::Size gridSize = MTL::Size(grid * threads, 1, 1);
            indirectEncoder->dispatchThreads(gridSize, tgSize);
        }
        indirectEncoder->endEncoding();
    };

    if (useGpuInstanceCulling) {
        size_t outputBytes = totalInputBytes;
        if (!m_instanceCullBuffer || m_instanceCullCapacity < outputBytes) {
            size_t newCapacity = std::max(outputBytes, m_instanceCullCapacity > 0 ? m_instanceCullCapacity * 2 : outputBytes);
            if (m_instanceCullBuffer) {
                m_instanceCullBuffer->release();
            }
            m_instanceCullBuffer = m_device->newBuffer(newCapacity, MTL::ResourceStorageModeShared);
            m_instanceCullCapacity = m_instanceCullBuffer ? m_instanceCullBuffer->length() : 0;
        }

        size_t countBytes = instancedBatches.size() * sizeof(uint32_t);
        if (!m_instanceCountBuffer || m_instanceCountCapacity < countBytes) {
            if (m_instanceCountBuffer) {
                m_instanceCountBuffer->release();
            }
            m_instanceCountBuffer = m_device->newBuffer(std::max<size_t>(countBytes, 256), MTL::ResourceStorageModeShared);
            m_instanceCountCapacity = m_instanceCountBuffer ? m_instanceCountBuffer->length() : 0;
        }

        size_t indirectBytes = instancedBatches.size() * sizeof(DrawIndexedIndirectArgs);
        if (!m_instanceIndirectBuffer || m_instanceIndirectCapacity < indirectBytes) {
            if (m_instanceIndirectBuffer) {
                m_instanceIndirectBuffer->release();
            }
            m_instanceIndirectBuffer = m_device->newBuffer(std::max<size_t>(indirectBytes, 256), MTL::ResourceStorageModeShared);
            m_instanceIndirectCapacity = m_instanceIndirectBuffer ? m_instanceIndirectBuffer->length() : 0;
        }

        dispatchInstanceCulling(m_instanceCullPipeline, false);
    }
    if (!useGpuInstanceCulling) {
        for (const auto& batch : instancedBatches) {
            if (batch.inputCount == 0) {
                continue;
            }
            InstancedDraw draw{};
            draw.mesh = batch.mesh;
            draw.material = batch.material;
            draw.isTransparent = batch.isTransparent;
            draw.receiveShadows = batch.receiveShadows;
            draw.instanceOffset = batch.inputOffset * sizeof(InstanceDataGPU);
            draw.instanceCount = batch.inputCount;
            draw.boundsCenter = batch.boundsCenter;
            draw.boundsSize = batch.boundsSize;
            draw.isBillboard = batch.isBillboard;
            instancedDraws.push_back(draw);
        }
    }

    // Setup light uniforms
    LightDataGPU* lightData = static_cast<LightDataGPU*>(m_lightUniformBuffer->contents());
    if (mainLight) {
        Math::Vector3 dir = mainLight->getDirection();
        lightData->direction = Math::Vector4(dir.x, dir.y, dir.z, mainLight->getIntensity());
        Math::Vector3 col = mainLight->getColor();
        if (m_environmentSettings.autoSunColor) {
            Math::Vector3 sunDir = Math::Vector3(-dir.x, -dir.y, -dir.z);
            float sunHeight = std::max(0.0f, std::min(1.0f, sunDir.y * 0.5f + 0.5f));
            Math::Vector3 sunWarm(1.0f, 0.55f, 0.35f);
            Math::Vector3 sunCool(1.0f, 0.95f, 0.9f);
            Math::Vector3 tint = sunWarm * (1.0f - sunHeight) + sunCool * sunHeight;
            col = Math::Vector3(col.x * tint.x, col.y * tint.y, col.z * tint.z);
        }
        lightData->color = Math::Vector4(col.x, col.y, col.z, 0.0f);
    } else {
        lightData->direction = Math::Vector4(0.0f, 1.0f, 0.0f, 0.0f);
        lightData->color = Math::Vector4(1.0f, 1.0f, 1.0f, 0.0f);
    }
    
    bool runPrepass = m_prepassPipelineState && m_normalTexture && m_depthTexture;
    if (runPrepass) {
        MTL::RenderPassDescriptor* prepass = MTL::RenderPassDescriptor::alloc()->init();
        prepass->colorAttachments()->object(0)->setTexture(m_normalTexture);
        prepass->colorAttachments()->object(0)->setLoadAction(MTL::LoadActionClear);
        prepass->colorAttachments()->object(0)->setStoreAction(MTL::StoreActionStore);
        prepass->colorAttachments()->object(0)->setClearColor(MTL::ClearColor::Make(0.5, 0.5, 1.0, 1.0));
        prepass->depthAttachment()->setTexture(m_depthTexture);
        prepass->depthAttachment()->setLoadAction(MTL::LoadActionClear);
        prepass->depthAttachment()->setStoreAction(MTL::StoreActionStore);
        prepass->depthAttachment()->setClearDepth(1.0);
        
        MTL::RenderCommandEncoder* preEncoder = commandBuffer->renderCommandEncoder(prepass);
        preEncoder->setDepthStencilState(m_depthStencilState);
        preEncoder->setFrontFacingWinding(MTL::WindingCounterClockwise);
        preEncoder->setCullMode(MTL::CullModeBack);
        preEncoder->setViewport(viewport);
        preEncoder->setVertexBuffer(m_cameraUniformBuffer, 0, 2);
        
        const auto& preEntities = scene->getAllEntities();
        for (const auto& entityPtr : preEntities) {
            Entity* entity = entityPtr.get();
            if (!entity->isActiveInHierarchy()) {
                continue;
            }
            if (shouldSkipEntity(entity)) {
                continue;
            }
            if (gpuCulledStatics.find(entity) != gpuCulledStatics.end()) {
                continue;
            }

            MeshRenderer* meshRenderer = entity->getComponent<MeshRenderer>();
            if (!meshRenderer || !meshRenderer->isEnabled()) {
                continue;
            }
            
            std::shared_ptr<Mesh> mesh = meshRenderer->getMesh();
            if (!mesh) {
                continue;
            }

            Math::Vector3 boundsCenter = meshRenderer->getBoundsCenter();
            Math::Vector3 boundsSize = meshRenderer->getBoundsSize();
            float radius = 0.5f * std::sqrt(boundsSize.x * boundsSize.x
                                            + boundsSize.y * boundsSize.y
                                            + boundsSize.z * boundsSize.z);
            if (radius <= 0.0f) {
                radius = 0.001f;
            }
            radius *= kCullTightness;
            if (!IsSphereInFrustum(frustumPlanes, boundsCenter, radius)) {
                continue;
            }

            std::shared_ptr<Material> material = meshRenderer->getMaterial(0);
            if (material) {
                bool isTransparent = material->getRenderMode() == Material::RenderMode::Transparent
                    || material->getAlpha() < 0.999f;
                if (isTransparent) {
                    continue;
                }
            }
            
            MTL::Buffer* vertexBuffer = static_cast<MTL::Buffer*>(mesh->getVertexBuffer());
            MTL::Buffer* indexBuffer = static_cast<MTL::Buffer*>(mesh->getIndexBuffer());
            MTL::Buffer* skinBuffer = static_cast<MTL::Buffer*>(mesh->getSkinWeightBuffer());
            if (!vertexBuffer || !indexBuffer) {
                continue;
            }
            
            SkinnedMeshRenderer* skinned = entity->getComponent<SkinnedMeshRenderer>();
            bool wantsSkin = skinned && skinned->isEnabled() && mesh->hasSkinWeights() && !skinned->getBoneMatrices().empty();
            bool isSkinned = wantsSkin && (skinBuffer != nullptr);
            MTL::RenderPipelineState* pipeline = isSkinned ? m_prepassPipelineSkinned : m_prepassPipelineState;
            if (!pipeline) {
                continue;
            }
            
            preEncoder->setRenderPipelineState(pipeline);
            
            ModelUniforms modelUniforms;
            modelUniforms.modelMatrix = entity->getTransform()->getWorldMatrix();
            modelUniforms.normalMatrix = modelUniforms.modelMatrix.inversed().transposed();
            
            preEncoder->setVertexBuffer(vertexBuffer, 0, 0);
            if (isSkinned) {
                preEncoder->setVertexBuffer(skinBuffer, 0, 4);
            }
            preEncoder->setVertexBytes(&modelUniforms, sizeof(ModelUniforms), 1);
            preEncoder->setVertexBuffer(m_cameraUniformBuffer, 0, 2);
            
            if (isSkinned && skinned) {
                const auto& boneMatrices = skinned->getBoneMatrices();
                if (!boneMatrices.empty()) {
                    size_t bytes = boneMatrices.size() * sizeof(Math::Matrix4x4);
                    size_t bufferOffset = 0;
                    if (allocateSkinningSlice(m_skinningBuffer, m_skinningBufferCapacity, skinningOffset, bytes, bufferOffset)) {
                        std::memcpy(static_cast<uint8_t*>(m_skinningBuffer->contents()) + bufferOffset,
                                    boneMatrices.data(),
                                    bytes);
                        preEncoder->setVertexBuffer(m_skinningBuffer, bufferOffset, 3);
                    }
                }
            }

            MaterialUniformsGPU matUniforms{};
            if (material) {
                matUniforms.albedo = material->getAlbedo();
                matUniforms.properties = Math::Vector4(
                    material->getMetallic(),
                    material->getRoughness(),
                    material->getAO(),
                    material->getNormalScale()
                );
                Math::Vector3 emis = material->getEmission();
                matUniforms.emission = Math::Vector4(
                    emis.x, emis.y, emis.z,
                    material->getEmissionStrength()
                );
                Math::Vector2 tiling = material->getUVTiling();
                Math::Vector2 offset = material->getUVOffset();
                matUniforms.uvTilingOffset = Math::Vector4(tiling.x, tiling.y, offset.x, offset.y);

                bool hasRoughnessTex = material->getRoughnessTexture() != nullptr;
                bool hasORMTex = material->getORMTexture() != nullptr;
                matUniforms.textureFlags = Math::Vector4(
                    material->getAlbedoTexture() ? 1.0f : 0.0f,
                    material->getNormalTexture() ? 1.0f : 0.0f,
                    material->getMetallicTexture() ? 1.0f : 0.0f,
                    hasRoughnessTex ? 1.0f : 0.0f
                );
                matUniforms.textureFlags2 = Math::Vector4(
                    material->getAOTexture() ? 1.0f : 0.0f,
                    material->getEmissionTexture() ? 1.0f : 0.0f,
                    material->getHeightTexture() ? 1.0f : 0.0f,
                    material->getHeightInvert() ? 1.0f : 0.0f
                );
                bool alphaClip = material->getRenderMode() == Material::RenderMode::Cutout;
                float alphaCutoff = material->getAlphaCutoff();
                matUniforms.textureFlags3 = Math::Vector4(
                    hasORMTex ? 1.0f : 0.0f,
                    alphaClip ? 1.0f : 0.0f,
                    alphaCutoff,
                    0.0f
                );
                matUniforms.heightParams = Math::Vector4(
                    material->getHeightScale(),
                    16.0f,
                    48.0f,
                    meshRenderer->getReceiveShadows() ? 1.0f : 0.0f
                );
                Math::Vector3 windDir = material->getWindDirection();
                matUniforms.foliageParams0 = Math::Vector4(
                    material->getWindStrength(),
                    material->getWindSpeed(),
                    material->getWindScale(),
                    material->getWindGust()
                );
                matUniforms.foliageParams1 = Math::Vector4(
                    material->getLodFadeStart(),
                    material->getLodFadeEnd(),
                    material->getBillboardStart(),
                    material->getBillboardEnd()
                );
                matUniforms.foliageParams2 = Math::Vector4(
                    material->getWindEnabled() ? 1.0f : 0.0f,
                    material->getLodFadeEnabled() ? 1.0f : 0.0f,
                    material->getBillboardEnabled() ? 1.0f : 0.0f,
                    material->getDitherEnabled() ? 1.0f : 0.0f
                );
                matUniforms.foliageParams3 = Math::Vector4(
                    windDir.x, windDir.y, windDir.z, 0.0f
                );
                matUniforms.impostorParams0 = Math::Vector4(
                    material->getImpostorEnabled() ? 1.0f : 0.0f,
                    static_cast<float>(material->getImpostorRows()),
                    static_cast<float>(material->getImpostorCols()),
                    0.0f
                );
            } else {
                matUniforms.albedo = Math::Vector4(1.0f);
                matUniforms.properties = Math::Vector4(0.0f, 1.0f, 1.0f, 1.0f);
                matUniforms.emission = Math::Vector4(0.0f);
                matUniforms.uvTilingOffset = Math::Vector4(1.0f, 1.0f, 0.0f, 0.0f);
                matUniforms.textureFlags = Math::Vector4(0.0f);
                matUniforms.textureFlags2 = Math::Vector4(0.0f);
                matUniforms.textureFlags3 = Math::Vector4(0.0f);
                matUniforms.heightParams = Math::Vector4(0.0f);
                matUniforms.foliageParams0 = Math::Vector4(0.0f);
                matUniforms.foliageParams1 = Math::Vector4(0.0f);
                matUniforms.foliageParams2 = Math::Vector4(0.0f);
                matUniforms.foliageParams3 = Math::Vector4(0.0f);
                matUniforms.impostorParams0 = Math::Vector4(0.0f);
            }

            preEncoder->setFragmentBytes(&matUniforms, sizeof(MaterialUniformsGPU), 0);
            if (!isSkinned) {
                MeshUniformsGPU meshUniforms{};
                Math::Vector3 boundsCenter = mesh->getBoundsCenter();
                Math::Vector3 boundsSize = mesh->getBoundsSize();
                meshUniforms.boundsCenter = Math::Vector4(boundsCenter.x, boundsCenter.y, boundsCenter.z, 0.0f);
                meshUniforms.boundsSize = Math::Vector4(boundsSize.x, boundsSize.y, boundsSize.z, 0.0f);
                meshUniforms.flags = Math::Vector4(0.0f);
                preEncoder->setVertexBytes(&matUniforms, sizeof(MaterialUniformsGPU), 3);
                preEncoder->setVertexBytes(&meshUniforms, sizeof(MeshUniformsGPU), 4);
            }
            auto albedoTex = (material && material->getAlbedoTexture()) ? material->getAlbedoTexture() : m_defaultWhiteTexture;
            auto roughnessTex = (material && material->getRoughnessTexture()) ? material->getRoughnessTexture() : m_defaultWhiteTexture;
            auto ormTex = (material && material->getORMTexture()) ? material->getORMTexture() : m_defaultBlackTexture;
            preEncoder->setFragmentTexture(albedoTex ? albedoTex->getHandle() : nullptr, 2);
            preEncoder->setFragmentTexture(roughnessTex ? roughnessTex->getHandle() : nullptr, 0);
            preEncoder->setFragmentTexture(ormTex ? ormTex->getHandle() : nullptr, 1);
            if (m_samplerState) {
                preEncoder->setFragmentSamplerState(m_samplerState, 0);
            }
            preEncoder->setCullMode(resolveCullMode(material.get()));
            
            preEncoder->drawIndexedPrimitives(
                MTL::PrimitiveTypeTriangle,
                mesh->getIndices().size(),
                MTL::IndexTypeUInt32,
                indexBuffer,
                0
            );
        }

        if (m_prepassPipelineInstanced) {
            if (useGpuInstanceCulling && m_instanceCullBuffer && m_instanceIndirectBuffer) {
                for (size_t i = 0; i < instancedBatches.size(); ++i) {
                    const auto& batch = instancedBatches[i];
                    if (!batch.mesh || batch.isTransparent) {
                        continue;
                    }

                    MTL::Buffer* vertexBuffer = static_cast<MTL::Buffer*>(batch.mesh->getVertexBuffer());
                    MTL::Buffer* indexBuffer = static_cast<MTL::Buffer*>(batch.mesh->getIndexBuffer());
                    if (!vertexBuffer || !indexBuffer) {
                        continue;
                    }

                    preEncoder->setRenderPipelineState(m_prepassPipelineInstanced);
                    preEncoder->setVertexBuffer(vertexBuffer, 0, 0);
                    preEncoder->setVertexBuffer(m_instanceCullBuffer, batch.outputOffset * sizeof(InstanceDataGPU), 1);
                    preEncoder->setVertexBuffer(m_cameraUniformBuffer, 0, 2);

                    MaterialUniformsGPU matUniforms{};
                    if (batch.material) {
                        matUniforms.albedo = batch.material->getAlbedo();
                        matUniforms.properties = Math::Vector4(
                            batch.material->getMetallic(),
                            batch.material->getRoughness(),
                            batch.material->getAO(),
                            batch.material->getNormalScale()
                        );
                        Math::Vector3 emis = batch.material->getEmission();
                        matUniforms.emission = Math::Vector4(
                            emis.x, emis.y, emis.z,
                            batch.material->getEmissionStrength()
                        );
                        Math::Vector2 tiling = batch.material->getUVTiling();
                        Math::Vector2 offset = batch.material->getUVOffset();
                        matUniforms.uvTilingOffset = Math::Vector4(tiling.x, tiling.y, offset.x, offset.y);

                        bool hasRoughnessTex = batch.material->getRoughnessTexture() != nullptr;
                        bool hasORMTex = batch.material->getORMTexture() != nullptr;
                        matUniforms.textureFlags = Math::Vector4(
                            batch.material->getAlbedoTexture() ? 1.0f : 0.0f,
                            batch.material->getNormalTexture() ? 1.0f : 0.0f,
                            batch.material->getMetallicTexture() ? 1.0f : 0.0f,
                            hasRoughnessTex ? 1.0f : 0.0f
                        );
                        matUniforms.textureFlags2 = Math::Vector4(
                            batch.material->getAOTexture() ? 1.0f : 0.0f,
                            batch.material->getEmissionTexture() ? 1.0f : 0.0f,
                            batch.material->getHeightTexture() ? 1.0f : 0.0f,
                            batch.material->getHeightInvert() ? 1.0f : 0.0f
                        );
                        bool alphaClip = batch.material->getRenderMode() == Material::RenderMode::Cutout;
                        float alphaCutoff = batch.material->getAlphaCutoff();
                        matUniforms.textureFlags3 = Math::Vector4(
                            hasORMTex ? 1.0f : 0.0f,
                            alphaClip ? 1.0f : 0.0f,
                            alphaCutoff,
                            0.0f
                        );
                        matUniforms.heightParams = Math::Vector4(
                            batch.material->getHeightScale(),
                            16.0f,
                            48.0f,
                            batch.receiveShadows ? 1.0f : 0.0f
                        );
                        Math::Vector3 windDir = batch.material->getWindDirection();
                        matUniforms.foliageParams0 = Math::Vector4(
                            batch.material->getWindStrength(),
                            batch.material->getWindSpeed(),
                            batch.material->getWindScale(),
                            batch.material->getWindGust()
                        );
                        matUniforms.foliageParams1 = Math::Vector4(
                            batch.material->getLodFadeStart(),
                            batch.material->getLodFadeEnd(),
                            batch.material->getBillboardStart(),
                            batch.material->getBillboardEnd()
                        );
                        matUniforms.foliageParams2 = Math::Vector4(
                            batch.material->getWindEnabled() ? 1.0f : 0.0f,
                            batch.material->getLodFadeEnabled() ? 1.0f : 0.0f,
                            batch.material->getBillboardEnabled() ? 1.0f : 0.0f,
                            batch.material->getDitherEnabled() ? 1.0f : 0.0f
                        );
                        matUniforms.foliageParams3 = Math::Vector4(
                            windDir.x, windDir.y, windDir.z, 0.0f
                        );
                        matUniforms.impostorParams0 = Math::Vector4(
                            batch.material->getImpostorEnabled() ? 1.0f : 0.0f,
                            static_cast<float>(batch.material->getImpostorRows()),
                            static_cast<float>(batch.material->getImpostorCols()),
                            0.0f
                        );
                    } else {
                        matUniforms.albedo = Math::Vector4(1.0f);
                        matUniforms.properties = Math::Vector4(0.0f, 1.0f, 1.0f, 1.0f);
                        matUniforms.emission = Math::Vector4(0.0f);
                        matUniforms.uvTilingOffset = Math::Vector4(1.0f, 1.0f, 0.0f, 0.0f);
                        matUniforms.textureFlags = Math::Vector4(0.0f);
                        matUniforms.textureFlags2 = Math::Vector4(0.0f);
                        matUniforms.textureFlags3 = Math::Vector4(0.0f);
                        matUniforms.heightParams = Math::Vector4(0.0f);
                        matUniforms.foliageParams0 = Math::Vector4(0.0f);
                        matUniforms.foliageParams1 = Math::Vector4(0.0f);
                        matUniforms.foliageParams2 = Math::Vector4(0.0f);
                        matUniforms.foliageParams3 = Math::Vector4(0.0f);
                        matUniforms.impostorParams0 = Math::Vector4(0.0f);
                    }

                    preEncoder->setFragmentBytes(&matUniforms, sizeof(MaterialUniformsGPU), 0);
                    MeshUniformsGPU meshUniforms{};
                    meshUniforms.boundsCenter = Math::Vector4(batch.boundsCenter.x, batch.boundsCenter.y, batch.boundsCenter.z, 0.0f);
                    meshUniforms.boundsSize = Math::Vector4(batch.boundsSize.x, batch.boundsSize.y, batch.boundsSize.z, 0.0f);
                    meshUniforms.flags = Math::Vector4(batch.isBillboard ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
                    preEncoder->setVertexBytes(&matUniforms, sizeof(MaterialUniformsGPU), 3);
                    preEncoder->setVertexBytes(&meshUniforms, sizeof(MeshUniformsGPU), 4);
                    auto albedoTex = (batch.material && batch.material->getAlbedoTexture()) ? batch.material->getAlbedoTexture() : m_defaultWhiteTexture;
                    auto roughnessTex = (batch.material && batch.material->getRoughnessTexture()) ? batch.material->getRoughnessTexture() : m_defaultWhiteTexture;
                    auto ormTex = (batch.material && batch.material->getORMTexture()) ? batch.material->getORMTexture() : m_defaultBlackTexture;
                    preEncoder->setFragmentTexture(albedoTex ? albedoTex->getHandle() : nullptr, 2);
                    preEncoder->setFragmentTexture(roughnessTex ? roughnessTex->getHandle() : nullptr, 0);
                    preEncoder->setFragmentTexture(ormTex ? ormTex->getHandle() : nullptr, 1);
                    if (m_samplerState) {
                        preEncoder->setFragmentSamplerState(m_samplerState, 0);
                    }
                    preEncoder->setCullMode(resolveCullMode(batch.material.get()));

                    preEncoder->drawIndexedPrimitives(
                        MTL::PrimitiveTypeTriangle,
                        MTL::IndexTypeUInt32,
                        indexBuffer,
                        0,
                        m_instanceIndirectBuffer,
                        i * sizeof(DrawIndexedIndirectArgs)
                    );
                }
            } else if (m_instanceBuffer) {
                for (const auto& draw : instancedDraws) {
                    if (!draw.mesh || draw.instanceCount == 0 || draw.isTransparent) {
                        continue;
                    }

                    MTL::Buffer* vertexBuffer = static_cast<MTL::Buffer*>(draw.mesh->getVertexBuffer());
                    MTL::Buffer* indexBuffer = static_cast<MTL::Buffer*>(draw.mesh->getIndexBuffer());
                    if (!vertexBuffer || !indexBuffer) {
                        continue;
                    }

                    preEncoder->setRenderPipelineState(m_prepassPipelineInstanced);
                    preEncoder->setVertexBuffer(vertexBuffer, 0, 0);
                    preEncoder->setVertexBuffer(m_instanceBuffer, draw.instanceOffset, 1);
                    preEncoder->setVertexBuffer(m_cameraUniformBuffer, 0, 2);

                    MaterialUniformsGPU matUniforms{};
                    if (draw.material) {
                        matUniforms.albedo = draw.material->getAlbedo();
                        matUniforms.properties = Math::Vector4(
                            draw.material->getMetallic(),
                            draw.material->getRoughness(),
                            draw.material->getAO(),
                            draw.material->getNormalScale()
                        );
                        Math::Vector3 emis = draw.material->getEmission();
                        matUniforms.emission = Math::Vector4(
                            emis.x, emis.y, emis.z,
                            draw.material->getEmissionStrength()
                        );
                        Math::Vector2 tiling = draw.material->getUVTiling();
                        Math::Vector2 offset = draw.material->getUVOffset();
                        matUniforms.uvTilingOffset = Math::Vector4(tiling.x, tiling.y, offset.x, offset.y);

                        bool hasRoughnessTex = draw.material->getRoughnessTexture() != nullptr;
                        bool hasORMTex = draw.material->getORMTexture() != nullptr;
                        matUniforms.textureFlags = Math::Vector4(
                            draw.material->getAlbedoTexture() ? 1.0f : 0.0f,
                            draw.material->getNormalTexture() ? 1.0f : 0.0f,
                            draw.material->getMetallicTexture() ? 1.0f : 0.0f,
                            hasRoughnessTex ? 1.0f : 0.0f
                        );
                        matUniforms.textureFlags2 = Math::Vector4(
                            draw.material->getAOTexture() ? 1.0f : 0.0f,
                            draw.material->getEmissionTexture() ? 1.0f : 0.0f,
                            draw.material->getHeightTexture() ? 1.0f : 0.0f,
                            draw.material->getHeightInvert() ? 1.0f : 0.0f
                        );
                        bool alphaClip = draw.material->getRenderMode() == Material::RenderMode::Cutout;
                        float alphaCutoff = draw.material->getAlphaCutoff();
                        matUniforms.textureFlags3 = Math::Vector4(
                            hasORMTex ? 1.0f : 0.0f,
                            alphaClip ? 1.0f : 0.0f,
                            alphaCutoff,
                            0.0f
                        );
                        matUniforms.heightParams = Math::Vector4(
                            draw.material->getHeightScale(),
                            16.0f,
                            48.0f,
                            draw.receiveShadows ? 1.0f : 0.0f
                        );
                        Math::Vector3 windDir = draw.material->getWindDirection();
                        matUniforms.foliageParams0 = Math::Vector4(
                            draw.material->getWindStrength(),
                            draw.material->getWindSpeed(),
                            draw.material->getWindScale(),
                            draw.material->getWindGust()
                        );
                        matUniforms.foliageParams1 = Math::Vector4(
                            draw.material->getLodFadeStart(),
                            draw.material->getLodFadeEnd(),
                            draw.material->getBillboardStart(),
                            draw.material->getBillboardEnd()
                        );
                        matUniforms.foliageParams2 = Math::Vector4(
                            draw.material->getWindEnabled() ? 1.0f : 0.0f,
                            draw.material->getLodFadeEnabled() ? 1.0f : 0.0f,
                            draw.material->getBillboardEnabled() ? 1.0f : 0.0f,
                            draw.material->getDitherEnabled() ? 1.0f : 0.0f
                        );
                        matUniforms.foliageParams3 = Math::Vector4(
                            windDir.x, windDir.y, windDir.z, 0.0f
                        );
                        matUniforms.impostorParams0 = Math::Vector4(
                            draw.material->getImpostorEnabled() ? 1.0f : 0.0f,
                            static_cast<float>(draw.material->getImpostorRows()),
                            static_cast<float>(draw.material->getImpostorCols()),
                            0.0f
                        );
                    } else {
                        matUniforms.albedo = Math::Vector4(1.0f);
                        matUniforms.properties = Math::Vector4(0.0f, 1.0f, 1.0f, 1.0f);
                        matUniforms.emission = Math::Vector4(0.0f);
                        matUniforms.uvTilingOffset = Math::Vector4(1.0f, 1.0f, 0.0f, 0.0f);
                        matUniforms.textureFlags = Math::Vector4(0.0f);
                        matUniforms.textureFlags2 = Math::Vector4(0.0f);
                        matUniforms.textureFlags3 = Math::Vector4(0.0f);
                        matUniforms.heightParams = Math::Vector4(0.0f);
                        matUniforms.foliageParams0 = Math::Vector4(0.0f);
                        matUniforms.foliageParams1 = Math::Vector4(0.0f);
                        matUniforms.foliageParams2 = Math::Vector4(0.0f);
                        matUniforms.foliageParams3 = Math::Vector4(0.0f);
                        matUniforms.impostorParams0 = Math::Vector4(0.0f);
                    }

                    preEncoder->setFragmentBytes(&matUniforms, sizeof(MaterialUniformsGPU), 0);
                    MeshUniformsGPU meshUniforms{};
                    meshUniforms.boundsCenter = Math::Vector4(draw.boundsCenter.x, draw.boundsCenter.y, draw.boundsCenter.z, 0.0f);
                    meshUniforms.boundsSize = Math::Vector4(draw.boundsSize.x, draw.boundsSize.y, draw.boundsSize.z, 0.0f);
                    meshUniforms.flags = Math::Vector4(draw.isBillboard ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
                    preEncoder->setVertexBytes(&matUniforms, sizeof(MaterialUniformsGPU), 3);
                    preEncoder->setVertexBytes(&meshUniforms, sizeof(MeshUniformsGPU), 4);
                    auto albedoTex = (draw.material && draw.material->getAlbedoTexture()) ? draw.material->getAlbedoTexture() : m_defaultWhiteTexture;
                    auto roughnessTex = (draw.material && draw.material->getRoughnessTexture()) ? draw.material->getRoughnessTexture() : m_defaultWhiteTexture;
                    auto ormTex = (draw.material && draw.material->getORMTexture()) ? draw.material->getORMTexture() : m_defaultBlackTexture;
                    preEncoder->setFragmentTexture(albedoTex ? albedoTex->getHandle() : nullptr, 2);
                    preEncoder->setFragmentTexture(roughnessTex ? roughnessTex->getHandle() : nullptr, 0);
                    preEncoder->setFragmentTexture(ormTex ? ormTex->getHandle() : nullptr, 1);
                    if (m_samplerState) {
                        preEncoder->setFragmentSamplerState(m_samplerState, 0);
                    }
                    preEncoder->setCullMode(resolveCullMode(draw.material.get()));

                    preEncoder->drawIndexedPrimitives(
                        MTL::PrimitiveTypeTriangle,
                        draw.mesh->getIndices().size(),
                        MTL::IndexTypeUInt32,
                        indexBuffer,
                        0,
                        draw.instanceCount
                    );
                }
            }
        }
        
        preEncoder->endEncoding();
        prepass->release();
    }

    bool canBuildHzb = runPrepass && useGpuInstanceCulling && m_instanceCullHzbPipeline && m_hzbTexture
        && m_hzbInitPipeline && m_hzbDownsamplePipeline && !m_hzbMipViews.empty();
    if (canBuildHzb) {
        MTL::ComputeCommandEncoder* hzbInit = commandBuffer->computeCommandEncoder();
        hzbInit->setComputePipelineState(m_hzbInitPipeline);
        hzbInit->setTexture(m_depthTexture, 0);
        hzbInit->setTexture(m_hzbMipViews[0], 1);
        if (m_pointClampSampler) {
            hzbInit->setSamplerState(m_pointClampSampler, 0);
        } else if (m_linearClampSampler) {
            hzbInit->setSamplerState(m_linearClampSampler, 0);
        }
        const uint32_t hzbWidth = m_hzbMipViews[0]->width();
        const uint32_t hzbHeight = m_hzbMipViews[0]->height();
        const uint32_t threads = 8;
        MTL::Size tgSize = MTL::Size(threads, threads, 1);
        MTL::Size gridSize = MTL::Size((hzbWidth + threads - 1) / threads * threads,
                                       (hzbHeight + threads - 1) / threads * threads,
                                       1);
        hzbInit->dispatchThreads(gridSize, tgSize);
        hzbInit->endEncoding();

        for (size_t mip = 1; mip < m_hzbMipViews.size(); ++mip) {
            MTL::Texture* src = m_hzbMipViews[mip - 1];
            MTL::Texture* dst = m_hzbMipViews[mip];
            if (!src || !dst) {
                continue;
            }
            MTL::ComputeCommandEncoder* downEncoder = commandBuffer->computeCommandEncoder();
            downEncoder->setComputePipelineState(m_hzbDownsamplePipeline);
            downEncoder->setTexture(src, 0);
            downEncoder->setTexture(dst, 1);
            const uint32_t mipWidth = dst->width();
            const uint32_t mipHeight = dst->height();
            MTL::Size downGrid = MTL::Size((mipWidth + threads - 1) / threads * threads,
                                           (mipHeight + threads - 1) / threads * threads,
                                           1);
            downEncoder->dispatchThreads(downGrid, tgSize);
            downEncoder->endEncoding();
        }

        if (m_instanceCullHzbPipeline) {
            dispatchInstanceCulling(m_instanceCullHzbPipeline, true);
        }
    }

    bool useDecals = runPrepass && m_decalPipelineState && m_decalAlbedoTexture && m_decalNormalTexture
        && m_decalOrmTexture && m_depthTexture;
    if (useDecals) {
        struct DecalDraw {
            Decal* decal;
            Transform* transform;
        };
        std::vector<DecalDraw> decalDraws;
        decalDraws.reserve(16);
        const auto& decalEntities = scene->getAllEntities();
        for (const auto& entityPtr : decalEntities) {
            Entity* entity = entityPtr.get();
            if (!entity || !entity->isActiveInHierarchy()) {
                continue;
            }
            Decal* decal = entity->getComponent<Decal>();
            if (!decal || !decal->isEnabled()) {
                continue;
            }
            decalDraws.push_back({decal, entity->getTransform()});
        }

        MTL::RenderPassDescriptor* decalPass = MTL::RenderPassDescriptor::alloc()->init();
        auto color0 = decalPass->colorAttachments()->object(0);
        auto color1 = decalPass->colorAttachments()->object(1);
        auto color2 = decalPass->colorAttachments()->object(2);
        color0->setTexture(m_decalAlbedoTexture);
        color1->setTexture(m_decalNormalTexture);
        color2->setTexture(m_decalOrmTexture);
        color0->setLoadAction(MTL::LoadActionClear);
        color1->setLoadAction(MTL::LoadActionClear);
        color2->setLoadAction(MTL::LoadActionClear);
        color0->setStoreAction(MTL::StoreActionStore);
        color1->setStoreAction(MTL::StoreActionStore);
        color2->setStoreAction(MTL::StoreActionStore);
        color0->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 0.0));
        color1->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 0.0));
        color2->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 0.0));

        MTL::RenderCommandEncoder* decalEncoder = commandBuffer->renderCommandEncoder(decalPass);
        decalEncoder->setViewport(viewport);
        decalEncoder->setRenderPipelineState(m_decalPipelineState);
        decalEncoder->setFragmentBuffer(m_cameraUniformBuffer, 0, 0);
        decalEncoder->setFragmentTexture(m_depthTexture, 0);
        if (m_samplerState) {
            decalEncoder->setFragmentSamplerState(m_samplerState, 0);
        } else if (m_linearClampSampler) {
            decalEncoder->setFragmentSamplerState(m_linearClampSampler, 0);
        }

        for (const auto& draw : decalDraws) {
            Decal* decal = draw.decal;
            Transform* transform = draw.transform;
            if (!decal || !transform) {
                continue;
            }

            DecalParamsGPU decalParams{};
            Math::Vector4 tint = decal->getTint();
            decalParams.colorOpacity = Math::Vector4(
                tint.x,
                tint.y,
                tint.z,
                decal->getOpacity()
            );
            Math::Vector2 tiling = decal->getTiling();
            Math::Vector2 offset = decal->getOffset();
            decalParams.uvTilingOffset = Math::Vector4(tiling.x, tiling.y, offset.x, offset.y);
            decalParams.edgeParams = Math::Vector4(decal->getEdgeSoftness(), 0.0f, 0.0f, 0.0f);

            bool hasNormal = decal->getNormalTexture() != nullptr;
            bool hasOrm = decal->getORMTexture() != nullptr;
            bool hasMask = decal->getMaskTexture() != nullptr;
            decalParams.mapFlags = Math::Vector4(1.0f, hasNormal ? 1.0f : 0.0f, hasOrm ? 1.0f : 0.0f, hasMask ? 1.0f : 0.0f);
            decalParams.modelMatrix = transform->getWorldMatrix();
            decalParams.invModel = decalParams.modelMatrix.inversed();

            decalEncoder->setFragmentBytes(&decalParams, sizeof(DecalParamsGPU), 1);

            std::shared_ptr<Texture2D> albedoTex = decal->getAlbedoTexture();
            std::shared_ptr<Texture2D> normalTex = decal->getNormalTexture();
            std::shared_ptr<Texture2D> ormTex = decal->getORMTexture();
            std::shared_ptr<Texture2D> maskTex = decal->getMaskTexture();

            MTL::Texture* albedoHandle = albedoTex ? albedoTex->getHandle() : nullptr;
            MTL::Texture* normalHandle = normalTex ? normalTex->getHandle() : nullptr;
            MTL::Texture* ormHandle = ormTex ? ormTex->getHandle() : nullptr;
            MTL::Texture* maskHandle = maskTex ? maskTex->getHandle() : nullptr;

            if (!albedoHandle && m_defaultWhiteTexture) {
                albedoHandle = m_defaultWhiteTexture->getHandle();
            }
            if (!normalHandle && m_defaultNormalTexture) {
                normalHandle = m_defaultNormalTexture->getHandle();
            }
            if (!ormHandle && m_defaultBlackTexture) {
                ormHandle = m_defaultBlackTexture->getHandle();
            }
            if (!maskHandle && m_defaultWhiteTexture) {
                maskHandle = m_defaultWhiteTexture->getHandle();
            }

            decalEncoder->setFragmentTexture(albedoHandle, 1);
            decalEncoder->setFragmentTexture(normalHandle, 2);
            decalEncoder->setFragmentTexture(ormHandle, 3);
            decalEncoder->setFragmentTexture(maskHandle, 4);

            decalEncoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
        }

        decalEncoder->endEncoding();
        decalPass->release();
    }

    bool runVelocity = (motionBlurEnabled || taaEnabled) && runPrepass && m_velocityPipelineState && m_velocityTexture && m_depthTexture;
    if (runVelocity) {
        MTL::RenderPassDescriptor* velocityPass = MTL::RenderPassDescriptor::alloc()->init();
        velocityPass->colorAttachments()->object(0)->setTexture(m_velocityTexture);
        velocityPass->colorAttachments()->object(0)->setLoadAction(MTL::LoadActionClear);
        velocityPass->colorAttachments()->object(0)->setStoreAction(MTL::StoreActionStore);
        velocityPass->colorAttachments()->object(0)->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
        velocityPass->depthAttachment()->setTexture(m_depthTexture);
        velocityPass->depthAttachment()->setLoadAction(MTL::LoadActionLoad);
        velocityPass->depthAttachment()->setStoreAction(MTL::StoreActionDontCare);

        MTL::RenderCommandEncoder* velEncoder = commandBuffer->renderCommandEncoder(velocityPass);
        velEncoder->setDepthStencilState(m_depthReadState ? m_depthReadState : m_depthStencilState);
        velEncoder->setFrontFacingWinding(MTL::WindingCounterClockwise);
        velEncoder->setCullMode(MTL::CullModeBack);
        velEncoder->setViewport(viewport);

        bool velocityUseJitter = taaEnabled;
        Math::Matrix4x4 currViewProjection = velocityUseJitter ? viewProjection : viewProjectionNoJitter;
        Math::Matrix4x4 prevViewProjection = velocityUseJitter
            ? (m_motionHistoryValid ? m_prevViewProjection : viewProjection)
            : (m_motionHistoryValid ? m_prevViewProjectionNoJitter : viewProjectionNoJitter);

        const auto& velEntities = scene->getAllEntities();
        for (const auto& entityPtr : velEntities) {
            Entity* entity = entityPtr.get();
            if (!entity->isActiveInHierarchy()) {
                continue;
            }
            if (shouldSkipEntity(entity)) {
                continue;
            }

            MeshRenderer* meshRenderer = entity->getComponent<MeshRenderer>();
            if (!meshRenderer || !meshRenderer->isEnabled()) {
                continue;
            }

            std::shared_ptr<Mesh> mesh = meshRenderer->getMesh();
            if (!mesh) {
                continue;
            }

            MTL::Buffer* vertexBuffer = static_cast<MTL::Buffer*>(mesh->getVertexBuffer());
            MTL::Buffer* indexBuffer = static_cast<MTL::Buffer*>(mesh->getIndexBuffer());
            MTL::Buffer* skinBuffer = static_cast<MTL::Buffer*>(mesh->getSkinWeightBuffer());
            if (!vertexBuffer || !indexBuffer) {
                continue;
            }

            SkinnedMeshRenderer* skinned = entity->getComponent<SkinnedMeshRenderer>();
            bool wantsSkin = skinned && skinned->isEnabled() && mesh->hasSkinWeights() && !skinned->getBoneMatrices().empty();
            bool isSkinned = wantsSkin && (skinBuffer != nullptr);
            MTL::RenderPipelineState* pipeline = isSkinned ? m_velocityPipelineSkinned : m_velocityPipelineState;
            if (!pipeline) {
                continue;
            }

            velEncoder->setRenderPipelineState(pipeline);

            ModelUniforms modelUniforms;
            modelUniforms.modelMatrix = entity->getTransform()->getWorldMatrix();
            modelUniforms.normalMatrix = modelUniforms.modelMatrix.inversed().transposed();

            VelocityUniformsGPU velocityUniforms{};
            velocityUniforms.prevModelMatrix = entity->getTransform()->getPreviousWorldMatrix();
            velocityUniforms.currViewProjection = currViewProjection;
            velocityUniforms.prevViewProjection = prevViewProjection;

            velEncoder->setVertexBuffer(vertexBuffer, 0, 0);
            if (isSkinned) {
                velEncoder->setVertexBuffer(skinBuffer, 0, 4);
            }
            velEncoder->setVertexBytes(&modelUniforms, sizeof(ModelUniforms), 1);
            velEncoder->setVertexBytes(&velocityUniforms, sizeof(VelocityUniformsGPU), 5);

            if (isSkinned && skinned) {
                const auto& boneMatrices = skinned->getBoneMatrices();
                if (!boneMatrices.empty()) {
                    size_t bytes = boneMatrices.size() * sizeof(Math::Matrix4x4);
                    size_t bufferOffset = 0;
                    if (allocateSkinningSlice(m_skinningBuffer, m_skinningBufferCapacity, skinningOffset, bytes, bufferOffset)) {
                        std::memcpy(static_cast<uint8_t*>(m_skinningBuffer->contents()) + bufferOffset,
                                    boneMatrices.data(),
                                    bytes);
                        velEncoder->setVertexBuffer(m_skinningBuffer, bufferOffset, 3);
                    }

                    const auto& prevBones = skinned->getPreviousBoneMatrices();
                    const auto& usePrevBones = (prevBones.size() == boneMatrices.size() && !prevBones.empty())
                        ? prevBones
                        : boneMatrices;
                    size_t prevBufferOffset = 0;
                    if (allocateSkinningSlice(m_prevSkinningBuffer, m_prevSkinningBufferCapacity, prevSkinningOffset, bytes, prevBufferOffset)) {
                        std::memcpy(static_cast<uint8_t*>(m_prevSkinningBuffer->contents()) + prevBufferOffset,
                                    usePrevBones.data(),
                                    bytes);
                        velEncoder->setVertexBuffer(m_prevSkinningBuffer, prevBufferOffset, 6);
                    }
                }
            }

            velEncoder->drawIndexedPrimitives(
                MTL::PrimitiveTypeTriangle,
                mesh->getIndices().size(),
                MTL::IndexTypeUInt32,
                indexBuffer,
                0
            );
        }

        velEncoder->endEncoding();
        velocityPass->release();
    }

    bool useSSAO = post.enabled && post.ssao && runPrepass && m_ssaoPipelineState && m_ssaoBlurPipelineState
        && m_ssaoTexture && m_ssaoBlurTexture && m_depthTexture && m_normalTexture && m_ssaoNoiseTexture;
    if (useSSAO) {
        SSAOParamsGPU ssaoParams{};
        ssaoParams.radius = std::max(0.05f, post.ssaoRadius);
        ssaoParams.bias = 0.02f;
        ssaoParams.intensity = std::max(0.0f, std::min(post.ssaoStrength, 1.0f));
        ssaoParams.power = 1.5f;
        ssaoParams.noiseScale = Math::Vector2(renderWidth / 4.0f, renderHeight / 4.0f);
        ssaoParams.texelSize = Math::Vector2(1.0f / renderWidth, 1.0f / renderHeight);

        MTL::RenderPassDescriptor* ssaoPass = MTL::RenderPassDescriptor::alloc()->init();
        ssaoPass->colorAttachments()->object(0)->setTexture(m_ssaoTexture);
        ssaoPass->colorAttachments()->object(0)->setLoadAction(MTL::LoadActionClear);
        ssaoPass->colorAttachments()->object(0)->setStoreAction(MTL::StoreActionStore);
        ssaoPass->colorAttachments()->object(0)->setClearColor(MTL::ClearColor::Make(1.0, 1.0, 1.0, 1.0));

        MTL::RenderCommandEncoder* ssaoEncoder = commandBuffer->renderCommandEncoder(ssaoPass);
        ssaoEncoder->setRenderPipelineState(m_ssaoPipelineState);
        ssaoEncoder->setViewport(viewport);
        ssaoEncoder->setFragmentBuffer(m_cameraUniformBuffer, 0, 0);
        ssaoEncoder->setFragmentBytes(&ssaoParams, sizeof(SSAOParamsGPU), 1);
        ssaoEncoder->setFragmentTexture(m_depthTexture, 0);
        ssaoEncoder->setFragmentTexture(m_normalTexture, 1);
        ssaoEncoder->setFragmentTexture(m_ssaoNoiseTexture, 2);
        if (m_linearClampSampler) {
            ssaoEncoder->setFragmentSamplerState(m_linearClampSampler, 0);
        }
        ssaoEncoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
        ssaoEncoder->endEncoding();
        ssaoPass->release();

        SSAOBlurParamsGPU blurParams{};
        blurParams.texelSize = ssaoParams.texelSize;
        blurParams.depthSharpness = 48.0f;
        blurParams.normalSharpness = 32.0f;

        MTL::RenderPassDescriptor* blurPass = MTL::RenderPassDescriptor::alloc()->init();
        blurPass->colorAttachments()->object(0)->setTexture(m_ssaoBlurTexture);
        blurPass->colorAttachments()->object(0)->setLoadAction(MTL::LoadActionClear);
        blurPass->colorAttachments()->object(0)->setStoreAction(MTL::StoreActionStore);
        blurPass->colorAttachments()->object(0)->setClearColor(MTL::ClearColor::Make(1.0, 1.0, 1.0, 1.0));

        MTL::RenderCommandEncoder* blurEncoder = commandBuffer->renderCommandEncoder(blurPass);
        blurEncoder->setRenderPipelineState(m_ssaoBlurPipelineState);
        blurEncoder->setViewport(viewport);
        blurEncoder->setFragmentBuffer(m_cameraUniformBuffer, 0, 0);
        blurEncoder->setFragmentBytes(&blurParams, sizeof(SSAOBlurParamsGPU), 1);
        blurEncoder->setFragmentTexture(m_ssaoTexture, 0);
        blurEncoder->setFragmentTexture(m_depthTexture, 1);
        blurEncoder->setFragmentTexture(m_normalTexture, 2);
        if (m_linearClampSampler) {
            blurEncoder->setFragmentSamplerState(m_linearClampSampler, 0);
        }
        blurEncoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
        blurEncoder->endEncoding();
        blurPass->release();
    }
    
    // Setup render pass
    MTL::RenderPassDescriptor* renderPass = MTL::RenderPassDescriptor::alloc()->init();
    
    // Color attachment
    if (useMSAA) {
        renderPass->colorAttachments()->object(0)->setTexture(m_msaaColorTexture);
        renderPass->colorAttachments()->object(0)->setResolveTexture(resolveToDrawable ? drawable->texture() : m_colorTexture);
        renderPass->colorAttachments()->object(0)->setStoreAction(MTL::StoreActionMultisampleResolve);
    } else if (useOffscreen) {
        renderPass->colorAttachments()->object(0)->setTexture(m_colorTexture);
        renderPass->colorAttachments()->object(0)->setStoreAction(MTL::StoreActionStore);
    } else {
        renderPass->colorAttachments()->object(0)->setTexture(drawable->texture());
        renderPass->colorAttachments()->object(0)->setStoreAction(MTL::StoreActionStore);
    }
    renderPass->colorAttachments()->object(0)->setLoadAction(MTL::LoadActionClear);
    
    Math::Vector4 clearColor = camera->getClearColor();
    renderPass->colorAttachments()->object(0)->setClearColor(
        MTL::ClearColor::Make(clearColor.x, clearColor.y, clearColor.z, clearColor.w)
    );
    
    // Depth attachment
    MTL::Texture* depthTarget = useMSAA ? m_msaaDepthTexture : m_depthTexture;
    if (!depthTarget) {
        std::cerr << "Failed to create depth texture!" << std::endl;
        renderPass->release();
        return;
    }
    
    renderPass->depthAttachment()->setTexture(depthTarget);
    bool usePrepassDepth = runPrepass && !useMSAA;
    if (usePrepassDepth) {
        renderPass->depthAttachment()->setLoadAction(MTL::LoadActionLoad);
    } else {
        renderPass->depthAttachment()->setLoadAction(MTL::LoadActionClear);
        renderPass->depthAttachment()->setClearDepth(1.0);
    }
    renderPass->depthAttachment()->setStoreAction(MTL::StoreActionDontCare);
    
    // Create render encoder
    MTL::RenderCommandEncoder* encoder = commandBuffer->renderCommandEncoder(renderPass);
    encoder->setDepthStencilState(m_depthStencilState);
    encoder->setFrontFacingWinding(MTL::WindingCounterClockwise);  // ✅ CCW is Metal/OpenGL standard
    encoder->setCullMode(MTL::CullModeBack);  // ✅ Cull back faces
    
    // Set viewport
    encoder->setViewport(viewport);
    
    // Environment uniforms
    updateEnvironmentUniforms();
    
    // Bind environment once for all draws
    if (m_samplerState) {
        encoder->setFragmentSamplerState(m_samplerState, 1);
    }
    if (m_environmentUniformBuffer) {
        encoder->setFragmentBuffer(m_environmentUniformBuffer, 0, 3);
    }
    auto envTexHandle = (m_environmentTexture ? m_environmentTexture : m_defaultEnvironmentTexture);
    encoder->setFragmentTexture(envTexHandle ? envTexHandle->getHandle() : nullptr, 7);
    MTL::Texture* ssaoTexture = (useSSAO && m_ssaoBlurTexture) ? m_ssaoBlurTexture : nullptr;
    if (!ssaoTexture && m_defaultWhiteTexture) {
        ssaoTexture = m_defaultWhiteTexture->getHandle();
    }
    encoder->setFragmentTexture(ssaoTexture, 17);

    MTL::Texture* decalAlbedo = useDecals ? m_decalAlbedoTexture : nullptr;
    MTL::Texture* decalNormal = useDecals ? m_decalNormalTexture : nullptr;
    MTL::Texture* decalOrm = useDecals ? m_decalOrmTexture : nullptr;
    if (!decalAlbedo && m_defaultBlackTexture) {
        decalAlbedo = m_defaultBlackTexture->getHandle();
    }
    if (!decalNormal && m_defaultNormalTexture) {
        decalNormal = m_defaultNormalTexture->getHandle();
    }
    if (!decalOrm && m_defaultBlackTexture) {
        decalOrm = m_defaultBlackTexture->getHandle();
    }
    encoder->setFragmentTexture(decalAlbedo, 18);
    encoder->setFragmentTexture(decalNormal, 19);
    encoder->setFragmentTexture(decalOrm, 20);
    
    // Draw skybox first
    renderSkybox(encoder, camera);
    encoder->setDepthStencilState(m_depthStencilState);
    encoder->setCullMode(MTL::CullModeBack);
    
    // Render all mesh renderers
    const auto& entities = scene->getAllEntities();
    
    static bool firstFrame = true;
    if (firstFrame) {
        std::cout << "Starting render loop with " << entities.size() << " entities" << std::endl;
        firstFrame = false;
    }
    
    int renderedCount = 0;
    for (const auto& entityPtr : entities) {
        Entity* entity = entityPtr.get();
        if (!entity->isActiveInHierarchy()) {
            continue;
        }
        if (shouldSkipEntity(entity)) {
            continue;
        }
        if (gpuCulledStatics.find(entity) != gpuCulledStatics.end()) {
            continue;
        }
        
        MeshRenderer* meshRenderer = entity->getComponent<MeshRenderer>();
        if (!meshRenderer || !meshRenderer->isEnabled()) {
            continue;
        }
        
        // Get mesh
        std::shared_ptr<Mesh> mesh = meshRenderer->getMesh();
        if (!mesh) continue;

        Math::Vector3 boundsCenter = meshRenderer->getBoundsCenter();
        Math::Vector3 boundsSize = meshRenderer->getBoundsSize();
        float radius = 0.5f * std::sqrt(boundsSize.x * boundsSize.x
                                        + boundsSize.y * boundsSize.y
                                        + boundsSize.z * boundsSize.z);
        if (radius <= 0.0f) {
            radius = 0.001f;
        }
        radius *= kCullTightness;
        if (!IsSphereInFrustum(frustumPlanes, boundsCenter, radius)) {
            continue;
        }

        SkinnedMeshRenderer* skinned = entity->getComponent<SkinnedMeshRenderer>();
        bool wantsSkin = skinned && skinned->isEnabled() && mesh->hasSkinWeights() && !skinned->getBoneMatrices().empty();

        // Upload mesh if needed
        if (!mesh->isUploaded()) {
            uploadMesh(mesh.get());
        }
        
        MTL::Buffer* vertexBuffer = static_cast<MTL::Buffer*>(mesh->getVertexBuffer());
        MTL::Buffer* indexBuffer = static_cast<MTL::Buffer*>(mesh->getIndexBuffer());
        MTL::Buffer* skinBuffer = static_cast<MTL::Buffer*>(mesh->getSkinWeightBuffer());
        
        if (!vertexBuffer || !indexBuffer) continue;

        bool isSkinned = wantsSkin && (skinBuffer != nullptr);

        renderMeshRenderer(meshRenderer, camera, lights);
        std::shared_ptr<Material> material = meshRenderer->getMaterial(0);
        bool isTransparent = false;
        if (material) {
            isTransparent = material->getRenderMode() == Material::RenderMode::Transparent
                || material->getAlpha() < 0.999f;
        }
        bool alphaToCoverage = material && material->getRenderMode() == Material::RenderMode::Cutout
            && material->getAlphaToCoverage();
        PipelineStateKey pipelineKey{true, true, true, isTransparent, isSkinned, false, alphaToCoverage, m_outputHDR, static_cast<uint8_t>(m_msaaSamples)};
        MTL::RenderPipelineState* pipelineState = getPipelineState(pipelineKey);
        if (!pipelineState) {
            continue;
        }
        encoder->setRenderPipelineState(pipelineState);
        
        // Setup model uniforms
        ModelUniforms modelUniforms;
        modelUniforms.modelMatrix = entity->getTransform()->getWorldMatrix();
        modelUniforms.normalMatrix = modelUniforms.modelMatrix.inversed().transposed();
        
        // Setup material uniforms
        if (material) {
            MaterialUniformsGPU matUniforms;
            matUniforms.albedo = material->getAlbedo();
            matUniforms.properties = Math::Vector4(
                material->getMetallic(),
                material->getRoughness(),
                material->getAO(),
                material->getNormalScale()
            );
            Math::Vector3 emis = material->getEmission();
            matUniforms.emission = Math::Vector4(
                emis.x, emis.y, emis.z,
                material->getEmissionStrength()
            );
            
            Math::Vector2 tiling = material->getUVTiling();
            Math::Vector2 offset = material->getUVOffset();
            matUniforms.uvTilingOffset = Math::Vector4(tiling.x, tiling.y, offset.x, offset.y);
            
            bool hasAlbedoTex = material->getAlbedoTexture() != nullptr;
            bool hasNormalTex = material->getNormalTexture() != nullptr;
            bool hasMetallicTex = material->getMetallicTexture() != nullptr;
            bool hasRoughnessTex = material->getRoughnessTexture() != nullptr;
            bool hasAOTex = material->getAOTexture() != nullptr;
            bool hasEmissionTex = material->getEmissionTexture() != nullptr;
            bool hasHeightTex = material->getHeightTexture() != nullptr;
            bool hasORMTex = material->getORMTexture() != nullptr;
            bool invertHeight = material->getHeightInvert();
            
            matUniforms.textureFlags = Math::Vector4(
                hasAlbedoTex ? 1.0f : 0.0f,
                hasNormalTex ? 1.0f : 0.0f,
                hasMetallicTex ? 1.0f : 0.0f,
                hasRoughnessTex ? 1.0f : 0.0f
            );
            matUniforms.textureFlags2 = Math::Vector4(
                hasAOTex ? 1.0f : 0.0f,
                hasEmissionTex ? 1.0f : 0.0f,
                hasHeightTex ? 1.0f : 0.0f,
                invertHeight ? 1.0f : 0.0f
            );
            bool alphaClip = material->getRenderMode() == Material::RenderMode::Cutout;
            float alphaCutoff = material->getAlphaCutoff();
            matUniforms.textureFlags3 = Math::Vector4(
                hasORMTex ? 1.0f : 0.0f,
                alphaClip ? 1.0f : 0.0f,
                alphaCutoff,
                0.0f
            );
            matUniforms.heightParams = Math::Vector4(
                material->getHeightScale(),
                16.0f,   // min layers
                48.0f,  // max layers
                meshRenderer->getReceiveShadows() ? 1.0f : 0.0f
            );
            Math::Vector3 windDir = material->getWindDirection();
            matUniforms.foliageParams0 = Math::Vector4(
                material->getWindStrength(),
                material->getWindSpeed(),
                material->getWindScale(),
                material->getWindGust()
            );
            matUniforms.foliageParams1 = Math::Vector4(
                material->getLodFadeStart(),
                material->getLodFadeEnd(),
                material->getBillboardStart(),
                material->getBillboardEnd()
            );
            matUniforms.foliageParams2 = Math::Vector4(
                material->getWindEnabled() ? 1.0f : 0.0f,
                material->getLodFadeEnabled() ? 1.0f : 0.0f,
                material->getBillboardEnabled() ? 1.0f : 0.0f,
                material->getDitherEnabled() ? 1.0f : 0.0f
            );
            matUniforms.foliageParams3 = Math::Vector4(
                windDir.x, windDir.y, windDir.z, 0.0f
            );
            matUniforms.impostorParams0 = Math::Vector4(
                material->getImpostorEnabled() ? 1.0f : 0.0f,
                static_cast<float>(material->getImpostorRows()),
                static_cast<float>(material->getImpostorCols()),
                0.0f
            );
            
            encoder->setFragmentBytes(&matUniforms, sizeof(MaterialUniformsGPU), 1);
            if (!isSkinned) {
                MeshUniformsGPU meshUniforms{};
                Math::Vector3 boundsCenter = mesh->getBoundsCenter();
                Math::Vector3 boundsSize = mesh->getBoundsSize();
                meshUniforms.boundsCenter = Math::Vector4(boundsCenter.x, boundsCenter.y, boundsCenter.z, 0.0f);
                meshUniforms.boundsSize = Math::Vector4(boundsSize.x, boundsSize.y, boundsSize.z, 0.0f);
                meshUniforms.flags = Math::Vector4(0.0f);
                encoder->setVertexBytes(&matUniforms, sizeof(MaterialUniformsGPU), 3);
                encoder->setVertexBytes(&meshUniforms, sizeof(MeshUniformsGPU), 4);
            }
            
            // Bind textures (fall back to engine defaults)
            auto albedoTex = hasAlbedoTex ? material->getAlbedoTexture() : m_defaultWhiteTexture;
            auto normalTex = hasNormalTex ? material->getNormalTexture() : m_defaultNormalTexture;
            auto metallicTex = hasMetallicTex ? material->getMetallicTexture() : m_defaultBlackTexture;
            auto roughnessTex = hasRoughnessTex ? material->getRoughnessTexture() : m_defaultWhiteTexture;
            auto aoTex = hasAOTex ? material->getAOTexture() : m_defaultWhiteTexture;
            auto emissionTex = hasEmissionTex ? material->getEmissionTexture() : m_defaultBlackTexture;
            auto heightTex = hasHeightTex ? material->getHeightTexture() : m_defaultHeightTexture;
            auto ormTex = hasORMTex ? material->getORMTexture() : m_defaultBlackTexture;
            
            encoder->setFragmentTexture(albedoTex ? albedoTex->getHandle() : nullptr, 0);
            encoder->setFragmentTexture(normalTex ? normalTex->getHandle() : nullptr, 1);
            encoder->setFragmentTexture(metallicTex ? metallicTex->getHandle() : nullptr, 2);
            encoder->setFragmentTexture(roughnessTex ? roughnessTex->getHandle() : nullptr, 3);
            encoder->setFragmentTexture(aoTex ? aoTex->getHandle() : nullptr, 4);
            encoder->setFragmentTexture(emissionTex ? emissionTex->getHandle() : nullptr, 5);
            encoder->setFragmentTexture(heightTex ? heightTex->getHandle() : nullptr, 6);
            encoder->setFragmentTexture(ormTex ? ormTex->getHandle() : nullptr, 16);
            
            // IBL textures (slots 8, 9, 10)
            if (m_hasIBL && m_iblIrradiance && m_iblPrefiltered && m_iblBRDFLUT) {
                encoder->setFragmentTexture(m_iblIrradiance, 8);
                encoder->setFragmentTexture(m_iblPrefiltered, 9);
                encoder->setFragmentTexture(m_iblBRDFLUT, 10);
            }
            
            if (m_samplerState) {
                encoder->setFragmentSamplerState(m_samplerState, 0);
            }
        }
        
        // Bind buffers
        encoder->setVertexBuffer(vertexBuffer, 0, 0);
        if (isSkinned) {
            encoder->setVertexBuffer(skinBuffer, 0, 4);
        }
        encoder->setVertexBytes(&modelUniforms, sizeof(ModelUniforms), 1);
        encoder->setVertexBuffer(m_cameraUniformBuffer, 0, 2);
        if (isSkinned && skinned) {
            const auto& boneMatrices = skinned->getBoneMatrices();
            if (!boneMatrices.empty()) {
                size_t bytes = boneMatrices.size() * sizeof(Math::Matrix4x4);
                size_t bufferOffset = 0;
                if (allocateSkinningSlice(m_skinningBuffer, m_skinningBufferCapacity, skinningOffset, bytes, bufferOffset)) {
                    std::memcpy(static_cast<uint8_t*>(m_skinningBuffer->contents()) + bufferOffset,
                                boneMatrices.data(),
                                bytes);
                    encoder->setVertexBuffer(m_skinningBuffer, bufferOffset, 3);
                }
            }
        }
        
        encoder->setFragmentBuffer(m_cameraUniformBuffer, 0, 0);
        encoder->setFragmentBuffer(m_lightUniformBuffer, 0, 2);
        
        // New clustered light/shadow buffers
        if (m_lightGPUBuffer) {
            encoder->setFragmentBuffer(m_lightGPUBuffer, 0, 4);
        }
        if (m_shadowGPUBuffer) {
            encoder->setFragmentBuffer(m_shadowGPUBuffer, 0, 5);
        }
        if (m_lightCountBuffer) {
            encoder->setFragmentBuffer(m_lightCountBuffer, 0, 6);
        } else {
            uint32_t zero = 0;
            encoder->setFragmentBytes(&zero, sizeof(uint32_t), 6);
        }
    if (m_shadowPass) {
        encoder->setFragmentTexture(m_shadowPass->getShadowAtlas(), 11);
        const auto& cubes = m_shadowPass->getPointCubeTextures();
        for (size_t i = 0; i < cubes.size() && i < 4; ++i) {
            if (cubes[i]) {
                encoder->setFragmentTexture(cubes[i], 12 + i);
            }
        }
    }
        if (m_shadowSampler) {
            encoder->setFragmentSamplerState(m_shadowSampler, 2);
        }
        if (m_clusterHeaderBuffer) {
            encoder->setFragmentBuffer(m_clusterHeaderBuffer, 0, 7);
        }
        if (m_clusterIndexBuffer) {
            encoder->setFragmentBuffer(m_clusterIndexBuffer, 0, 8);
        }
        if (m_clusterParamsBuffer) {
            encoder->setFragmentBuffer(m_clusterParamsBuffer, 0, 9);
        }
        encoder->setCullMode(resolveCullMode(material.get()));
        
        // Draw
        encoder->drawIndexedPrimitives(
            MTL::PrimitiveTypeTriangle,
            mesh->getIndices().size(),
            MTL::IndexTypeUInt32,
            indexBuffer,
            0
        );
        
        renderedCount++;
        m_stats.drawCalls++;
        m_stats.triangles += mesh->getIndices().size() / 3;
        m_stats.vertices += mesh->getVertices().size();
    }

    if (useGpuInstanceCulling && m_instanceCullBuffer && m_instanceIndirectBuffer) {
        for (size_t i = 0; i < instancedBatches.size(); ++i) {
            const auto& batch = instancedBatches[i];
            if (!batch.mesh) {
                continue;
            }

            MTL::Buffer* vertexBuffer = static_cast<MTL::Buffer*>(batch.mesh->getVertexBuffer());
            MTL::Buffer* indexBuffer = static_cast<MTL::Buffer*>(batch.mesh->getIndexBuffer());
            if (!vertexBuffer || !indexBuffer) {
                continue;
            }

            bool alphaToCoverage = batch.material && batch.material->getRenderMode() == Material::RenderMode::Cutout
                && batch.material->getAlphaToCoverage();
            PipelineStateKey pipelineKey{true, true, true, batch.isTransparent, false, true, alphaToCoverage, m_outputHDR, static_cast<uint8_t>(m_msaaSamples)};
            MTL::RenderPipelineState* pipelineState = getPipelineState(pipelineKey);
            if (!pipelineState) {
                continue;
            }
            encoder->setRenderPipelineState(pipelineState);

            MaterialUniformsGPU matUniforms{};
            if (batch.material) {
                matUniforms.albedo = batch.material->getAlbedo();
                matUniforms.properties = Math::Vector4(
                    batch.material->getMetallic(),
                    batch.material->getRoughness(),
                    batch.material->getAO(),
                    batch.material->getNormalScale()
                );
                Math::Vector3 emis = batch.material->getEmission();
                matUniforms.emission = Math::Vector4(
                    emis.x, emis.y, emis.z,
                    batch.material->getEmissionStrength()
                );

                Math::Vector2 tiling = batch.material->getUVTiling();
                Math::Vector2 offset = batch.material->getUVOffset();
                matUniforms.uvTilingOffset = Math::Vector4(tiling.x, tiling.y, offset.x, offset.y);

                bool hasAlbedoTex = batch.material->getAlbedoTexture() != nullptr;
                bool hasNormalTex = batch.material->getNormalTexture() != nullptr;
                bool hasMetallicTex = batch.material->getMetallicTexture() != nullptr;
                bool hasRoughnessTex = batch.material->getRoughnessTexture() != nullptr;
                bool hasAOTex = batch.material->getAOTexture() != nullptr;
                bool hasEmissionTex = batch.material->getEmissionTexture() != nullptr;
                bool hasHeightTex = batch.material->getHeightTexture() != nullptr;
                bool hasORMTex = batch.material->getORMTexture() != nullptr;
                bool invertHeight = batch.material->getHeightInvert();

                matUniforms.textureFlags = Math::Vector4(
                    hasAlbedoTex ? 1.0f : 0.0f,
                    hasNormalTex ? 1.0f : 0.0f,
                    hasMetallicTex ? 1.0f : 0.0f,
                    hasRoughnessTex ? 1.0f : 0.0f
                );
                matUniforms.textureFlags2 = Math::Vector4(
                    hasAOTex ? 1.0f : 0.0f,
                    hasEmissionTex ? 1.0f : 0.0f,
                    hasHeightTex ? 1.0f : 0.0f,
                    invertHeight ? 1.0f : 0.0f
                );
                bool alphaClip = batch.material->getRenderMode() == Material::RenderMode::Cutout;
                float alphaCutoff = batch.material->getAlphaCutoff();
                matUniforms.textureFlags3 = Math::Vector4(
                    hasORMTex ? 1.0f : 0.0f,
                    alphaClip ? 1.0f : 0.0f,
                    alphaCutoff,
                    0.0f
                );
                matUniforms.heightParams = Math::Vector4(
                    batch.material->getHeightScale(),
                    16.0f,
                    48.0f,
                    batch.receiveShadows ? 1.0f : 0.0f
                );
                Math::Vector3 windDir = batch.material->getWindDirection();
                matUniforms.foliageParams0 = Math::Vector4(
                    batch.material->getWindStrength(),
                    batch.material->getWindSpeed(),
                    batch.material->getWindScale(),
                    batch.material->getWindGust()
                );
                matUniforms.foliageParams1 = Math::Vector4(
                    batch.material->getLodFadeStart(),
                    batch.material->getLodFadeEnd(),
                    batch.material->getBillboardStart(),
                    batch.material->getBillboardEnd()
                );
                matUniforms.foliageParams2 = Math::Vector4(
                    batch.material->getWindEnabled() ? 1.0f : 0.0f,
                    batch.material->getLodFadeEnabled() ? 1.0f : 0.0f,
                    batch.material->getBillboardEnabled() ? 1.0f : 0.0f,
                    batch.material->getDitherEnabled() ? 1.0f : 0.0f
                );
                matUniforms.foliageParams3 = Math::Vector4(
                    windDir.x, windDir.y, windDir.z, 0.0f
                );

                encoder->setFragmentBytes(&matUniforms, sizeof(MaterialUniformsGPU), 1);
                MeshUniformsGPU meshUniforms{};
                meshUniforms.boundsCenter = Math::Vector4(batch.boundsCenter.x, batch.boundsCenter.y, batch.boundsCenter.z, 0.0f);
                meshUniforms.boundsSize = Math::Vector4(batch.boundsSize.x, batch.boundsSize.y, batch.boundsSize.z, 0.0f);
                meshUniforms.flags = Math::Vector4(batch.isBillboard ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
                encoder->setVertexBytes(&matUniforms, sizeof(MaterialUniformsGPU), 3);
                encoder->setVertexBytes(&meshUniforms, sizeof(MeshUniformsGPU), 4);

                auto albedoTex = hasAlbedoTex ? batch.material->getAlbedoTexture() : m_defaultWhiteTexture;
                auto normalTex = hasNormalTex ? batch.material->getNormalTexture() : m_defaultNormalTexture;
                auto metallicTex = hasMetallicTex ? batch.material->getMetallicTexture() : m_defaultBlackTexture;
                auto roughnessTex = hasRoughnessTex ? batch.material->getRoughnessTexture() : m_defaultWhiteTexture;
                auto aoTex = hasAOTex ? batch.material->getAOTexture() : m_defaultWhiteTexture;
                auto emissionTex = hasEmissionTex ? batch.material->getEmissionTexture() : m_defaultBlackTexture;
                auto heightTex = hasHeightTex ? batch.material->getHeightTexture() : m_defaultHeightTexture;
                auto ormTex = hasORMTex ? batch.material->getORMTexture() : m_defaultBlackTexture;

                encoder->setFragmentTexture(albedoTex ? albedoTex->getHandle() : nullptr, 0);
                encoder->setFragmentTexture(normalTex ? normalTex->getHandle() : nullptr, 1);
                encoder->setFragmentTexture(metallicTex ? metallicTex->getHandle() : nullptr, 2);
                encoder->setFragmentTexture(roughnessTex ? roughnessTex->getHandle() : nullptr, 3);
                encoder->setFragmentTexture(aoTex ? aoTex->getHandle() : nullptr, 4);
                encoder->setFragmentTexture(emissionTex ? emissionTex->getHandle() : nullptr, 5);
                encoder->setFragmentTexture(heightTex ? heightTex->getHandle() : nullptr, 6);
                encoder->setFragmentTexture(ormTex ? ormTex->getHandle() : nullptr, 16);

                if (m_hasIBL && m_iblIrradiance && m_iblPrefiltered && m_iblBRDFLUT) {
                    encoder->setFragmentTexture(m_iblIrradiance, 8);
                    encoder->setFragmentTexture(m_iblPrefiltered, 9);
                    encoder->setFragmentTexture(m_iblBRDFLUT, 10);
                }

                if (m_samplerState) {
                    encoder->setFragmentSamplerState(m_samplerState, 0);
                }
            }

            encoder->setVertexBuffer(vertexBuffer, 0, 0);
            encoder->setVertexBuffer(m_instanceCullBuffer, batch.outputOffset * sizeof(InstanceDataGPU), 1);
            encoder->setVertexBuffer(m_cameraUniformBuffer, 0, 2);

            encoder->setFragmentBuffer(m_cameraUniformBuffer, 0, 0);
            encoder->setFragmentBuffer(m_lightUniformBuffer, 0, 2);

            if (m_lightGPUBuffer) {
                encoder->setFragmentBuffer(m_lightGPUBuffer, 0, 4);
            }
            if (m_shadowGPUBuffer) {
                encoder->setFragmentBuffer(m_shadowGPUBuffer, 0, 5);
            }
            if (m_lightCountBuffer) {
                encoder->setFragmentBuffer(m_lightCountBuffer, 0, 6);
            } else {
                uint32_t zero = 0;
                encoder->setFragmentBytes(&zero, sizeof(uint32_t), 6);
            }
            if (m_shadowPass) {
                encoder->setFragmentTexture(m_shadowPass->getShadowAtlas(), 11);
                const auto& cubes = m_shadowPass->getPointCubeTextures();
                for (size_t j = 0; j < cubes.size() && j < 4; ++j) {
                    if (cubes[j]) {
                        encoder->setFragmentTexture(cubes[j], 12 + j);
                    }
                }
            }
            if (m_shadowSampler) {
                encoder->setFragmentSamplerState(m_shadowSampler, 2);
            }
            if (m_clusterHeaderBuffer) {
                encoder->setFragmentBuffer(m_clusterHeaderBuffer, 0, 7);
            }
            if (m_clusterIndexBuffer) {
                encoder->setFragmentBuffer(m_clusterIndexBuffer, 0, 8);
            }
            if (m_clusterParamsBuffer) {
                encoder->setFragmentBuffer(m_clusterParamsBuffer, 0, 9);
            }
            encoder->setCullMode(resolveCullMode(batch.material.get()));

            encoder->drawIndexedPrimitives(
                MTL::PrimitiveTypeTriangle,
                MTL::IndexTypeUInt32,
                indexBuffer,
                0,
                m_instanceIndirectBuffer,
                i * sizeof(DrawIndexedIndirectArgs)
            );

            renderedCount += static_cast<int>(batch.inputCount);
            m_stats.drawCalls++;
            m_stats.triangles += (batch.mesh->getIndices().size() / 3) * batch.inputCount;
            m_stats.vertices += batch.mesh->getVertices().size() * batch.inputCount;
        }
    } else {
        for (const auto& draw : instancedDraws) {
            if (!draw.mesh || draw.instanceCount == 0) {
                continue;
            }

            MTL::Buffer* vertexBuffer = static_cast<MTL::Buffer*>(draw.mesh->getVertexBuffer());
            MTL::Buffer* indexBuffer = static_cast<MTL::Buffer*>(draw.mesh->getIndexBuffer());
            if (!vertexBuffer || !indexBuffer) {
                continue;
            }

            bool alphaToCoverage = draw.material && draw.material->getRenderMode() == Material::RenderMode::Cutout
                && draw.material->getAlphaToCoverage();
            PipelineStateKey pipelineKey{true, true, true, draw.isTransparent, false, true, alphaToCoverage, m_outputHDR, static_cast<uint8_t>(m_msaaSamples)};
            MTL::RenderPipelineState* pipelineState = getPipelineState(pipelineKey);
            if (!pipelineState) {
                continue;
            }
            encoder->setRenderPipelineState(pipelineState);

            MaterialUniformsGPU matUniforms{};
            if (draw.material) {
                matUniforms.albedo = draw.material->getAlbedo();
                matUniforms.properties = Math::Vector4(
                    draw.material->getMetallic(),
                    draw.material->getRoughness(),
                    draw.material->getAO(),
                    draw.material->getNormalScale()
                );
                Math::Vector3 emis = draw.material->getEmission();
                matUniforms.emission = Math::Vector4(
                    emis.x, emis.y, emis.z,
                    draw.material->getEmissionStrength()
                );

                Math::Vector2 tiling = draw.material->getUVTiling();
                Math::Vector2 offset = draw.material->getUVOffset();
                matUniforms.uvTilingOffset = Math::Vector4(tiling.x, tiling.y, offset.x, offset.y);

                bool hasAlbedoTex = draw.material->getAlbedoTexture() != nullptr;
                bool hasNormalTex = draw.material->getNormalTexture() != nullptr;
                bool hasMetallicTex = draw.material->getMetallicTexture() != nullptr;
                bool hasRoughnessTex = draw.material->getRoughnessTexture() != nullptr;
                bool hasAOTex = draw.material->getAOTexture() != nullptr;
                bool hasEmissionTex = draw.material->getEmissionTexture() != nullptr;
                bool hasHeightTex = draw.material->getHeightTexture() != nullptr;
                bool hasORMTex = draw.material->getORMTexture() != nullptr;
                bool invertHeight = draw.material->getHeightInvert();

                matUniforms.textureFlags = Math::Vector4(
                    hasAlbedoTex ? 1.0f : 0.0f,
                    hasNormalTex ? 1.0f : 0.0f,
                    hasMetallicTex ? 1.0f : 0.0f,
                    hasRoughnessTex ? 1.0f : 0.0f
                );
                matUniforms.textureFlags2 = Math::Vector4(
                    hasAOTex ? 1.0f : 0.0f,
                    hasEmissionTex ? 1.0f : 0.0f,
                    hasHeightTex ? 1.0f : 0.0f,
                    invertHeight ? 1.0f : 0.0f
                );
                bool alphaClip = draw.material->getRenderMode() == Material::RenderMode::Cutout;
                float alphaCutoff = draw.material->getAlphaCutoff();
                matUniforms.textureFlags3 = Math::Vector4(
                    hasORMTex ? 1.0f : 0.0f,
                    alphaClip ? 1.0f : 0.0f,
                    alphaCutoff,
                    0.0f
                );
                matUniforms.heightParams = Math::Vector4(
                    draw.material->getHeightScale(),
                    16.0f,
                    48.0f,
                    draw.receiveShadows ? 1.0f : 0.0f
                );
                Math::Vector3 windDir = draw.material->getWindDirection();
                matUniforms.foliageParams0 = Math::Vector4(
                    draw.material->getWindStrength(),
                    draw.material->getWindSpeed(),
                    draw.material->getWindScale(),
                    draw.material->getWindGust()
                );
                matUniforms.foliageParams1 = Math::Vector4(
                    draw.material->getLodFadeStart(),
                    draw.material->getLodFadeEnd(),
                    draw.material->getBillboardStart(),
                    draw.material->getBillboardEnd()
                );
                matUniforms.foliageParams2 = Math::Vector4(
                    draw.material->getWindEnabled() ? 1.0f : 0.0f,
                    draw.material->getLodFadeEnabled() ? 1.0f : 0.0f,
                    draw.material->getBillboardEnabled() ? 1.0f : 0.0f,
                    draw.material->getDitherEnabled() ? 1.0f : 0.0f
                );
                matUniforms.foliageParams3 = Math::Vector4(
                    windDir.x, windDir.y, windDir.z, 0.0f
                );

                encoder->setFragmentBytes(&matUniforms, sizeof(MaterialUniformsGPU), 1);
                MeshUniformsGPU meshUniforms{};
                meshUniforms.boundsCenter = Math::Vector4(draw.boundsCenter.x, draw.boundsCenter.y, draw.boundsCenter.z, 0.0f);
                meshUniforms.boundsSize = Math::Vector4(draw.boundsSize.x, draw.boundsSize.y, draw.boundsSize.z, 0.0f);
                meshUniforms.flags = Math::Vector4(draw.isBillboard ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
                encoder->setVertexBytes(&matUniforms, sizeof(MaterialUniformsGPU), 3);
                encoder->setVertexBytes(&meshUniforms, sizeof(MeshUniformsGPU), 4);

                auto albedoTex = hasAlbedoTex ? draw.material->getAlbedoTexture() : m_defaultWhiteTexture;
                auto normalTex = hasNormalTex ? draw.material->getNormalTexture() : m_defaultNormalTexture;
                auto metallicTex = hasMetallicTex ? draw.material->getMetallicTexture() : m_defaultBlackTexture;
                auto roughnessTex = hasRoughnessTex ? draw.material->getRoughnessTexture() : m_defaultWhiteTexture;
                auto aoTex = hasAOTex ? draw.material->getAOTexture() : m_defaultWhiteTexture;
                auto emissionTex = hasEmissionTex ? draw.material->getEmissionTexture() : m_defaultBlackTexture;
                auto heightTex = hasHeightTex ? draw.material->getHeightTexture() : m_defaultHeightTexture;
                auto ormTex = hasORMTex ? draw.material->getORMTexture() : m_defaultBlackTexture;

                encoder->setFragmentTexture(albedoTex ? albedoTex->getHandle() : nullptr, 0);
                encoder->setFragmentTexture(normalTex ? normalTex->getHandle() : nullptr, 1);
                encoder->setFragmentTexture(metallicTex ? metallicTex->getHandle() : nullptr, 2);
                encoder->setFragmentTexture(roughnessTex ? roughnessTex->getHandle() : nullptr, 3);
                encoder->setFragmentTexture(aoTex ? aoTex->getHandle() : nullptr, 4);
                encoder->setFragmentTexture(emissionTex ? emissionTex->getHandle() : nullptr, 5);
                encoder->setFragmentTexture(heightTex ? heightTex->getHandle() : nullptr, 6);
                encoder->setFragmentTexture(ormTex ? ormTex->getHandle() : nullptr, 16);

                if (m_hasIBL && m_iblIrradiance && m_iblPrefiltered && m_iblBRDFLUT) {
                    encoder->setFragmentTexture(m_iblIrradiance, 8);
                    encoder->setFragmentTexture(m_iblPrefiltered, 9);
                    encoder->setFragmentTexture(m_iblBRDFLUT, 10);
                }

                if (m_samplerState) {
                    encoder->setFragmentSamplerState(m_samplerState, 0);
                }
            }

            encoder->setVertexBuffer(vertexBuffer, 0, 0);
            encoder->setVertexBuffer(m_instanceBuffer, draw.instanceOffset, 1);
            encoder->setVertexBuffer(m_cameraUniformBuffer, 0, 2);

            encoder->setFragmentBuffer(m_cameraUniformBuffer, 0, 0);
            encoder->setFragmentBuffer(m_lightUniformBuffer, 0, 2);

            if (m_lightGPUBuffer) {
                encoder->setFragmentBuffer(m_lightGPUBuffer, 0, 4);
            }
            if (m_shadowGPUBuffer) {
                encoder->setFragmentBuffer(m_shadowGPUBuffer, 0, 5);
            }
            if (m_lightCountBuffer) {
                encoder->setFragmentBuffer(m_lightCountBuffer, 0, 6);
            } else {
                uint32_t zero = 0;
                encoder->setFragmentBytes(&zero, sizeof(uint32_t), 6);
            }
            if (m_shadowPass) {
                encoder->setFragmentTexture(m_shadowPass->getShadowAtlas(), 11);
                const auto& cubes = m_shadowPass->getPointCubeTextures();
                for (size_t i = 0; i < cubes.size() && i < 4; ++i) {
                    if (cubes[i]) {
                        encoder->setFragmentTexture(cubes[i], 12 + i);
                    }
                }
            }
            if (m_shadowSampler) {
                encoder->setFragmentSamplerState(m_shadowSampler, 2);
            }
            if (m_clusterHeaderBuffer) {
                encoder->setFragmentBuffer(m_clusterHeaderBuffer, 0, 7);
            }
            if (m_clusterIndexBuffer) {
                encoder->setFragmentBuffer(m_clusterIndexBuffer, 0, 8);
            }
            if (m_clusterParamsBuffer) {
                encoder->setFragmentBuffer(m_clusterParamsBuffer, 0, 9);
            }
            encoder->setCullMode(resolveCullMode(draw.material.get()));

            encoder->drawIndexedPrimitives(
                MTL::PrimitiveTypeTriangle,
                draw.mesh->getIndices().size(),
                MTL::IndexTypeUInt32,
                indexBuffer,
                0,
                draw.instanceCount
            );

            renderedCount += static_cast<int>(draw.instanceCount);
            m_stats.drawCalls++;
            m_stats.triangles += (draw.mesh->getIndices().size() / 3) * draw.instanceCount;
            m_stats.vertices += draw.mesh->getVertices().size() * draw.instanceCount;
        }
    }
    
    static int frameCount = 0;
    if (frameCount++ < 3) {
        std::cout << "Frame " << frameCount << ": Rendered " << renderedCount << " meshes" << std::endl;
    }
    
    // === DEBUG RENDERING ===
    // NOTE: Wire/gizmo now drawn by Engine::render() BEFORE this function
    // Only selected entities get wireframe - drawn in Engine, not here!
    static bool debugRenderLogOnce = true;
    if (m_debugRenderer && camera) {
        #ifdef DEBUG
        if (m_lightingSystem && (m_debugDrawCascades || m_debugDrawPointFrusta || m_debugDrawShadowAtlas)) {
            m_lightingSystem->buildLightGizmos(*m_debugRenderer, m_debugDrawCascades);
        }
        #endif
        // Update debug uniforms and buffers
        m_debugRenderer->render(
            nullptr,
            camera->getViewMatrix(),
            camera->getProjectionMatrix(),
            camera->getEntity()->getTransform()->getPosition()
        );
        
        // Render grid (disable depth write, always pass depth test)
        static bool gridLogOnce = true;
        if (m_debugRenderer->isGridEnabled() && m_debugGridPipelineState && m_debugRenderer->getGridVertexCount() > 0) {
            if (gridLogOnce) {
                std::cout << "  Rendering grid with " << m_debugRenderer->getGridVertexCount() << " vertices" << std::endl;
                gridLogOnce = false;
            }
            
            // Create depth stencil state for grid (render behind meshes only)
            MTL::DepthStencilDescriptor* debugDepthDesc = MTL::DepthStencilDescriptor::alloc()->init();
            debugDepthDesc->setDepthCompareFunction(MTL::CompareFunctionLess);  // ✅ Grid only visible where NO mesh
            debugDepthDesc->setDepthWriteEnabled(false);  // Don't write to depth buffer
            MTL::DepthStencilState* debugDepthState = m_device->newDepthStencilState(debugDepthDesc);
            debugDepthDesc->release();
            
            encoder->setRenderPipelineState(m_debugGridPipelineState);
            encoder->setDepthStencilState(debugDepthState);
            encoder->setCullMode(MTL::CullModeNone);  // ✅ CRITICAL: Grid visible from both sides!
            encoder->setVertexBuffer((MTL::Buffer*)m_debugRenderer->getGridBuffer(), 0, 0);
            encoder->setVertexBuffer((MTL::Buffer*)m_debugRenderer->getUniformBuffer(), 0, 1);
            encoder->setFragmentBuffer((MTL::Buffer*)m_debugRenderer->getUniformBuffer(), 0, 1);
            encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), 
                                   NS::UInteger(m_debugRenderer->getGridVertexCount()));
            
            debugDepthState->release();
        } else {
            if (gridLogOnce) {
                std::cout << "  Grid NOT rendered - enabled:" << m_debugRenderer->isGridEnabled() 
                         << " pipeline:" << (m_debugGridPipelineState ? "YES" : "NO")
                         << " vertices:" << m_debugRenderer->getGridVertexCount() << std::endl;
                gridLogOnce = false;
            }
        }
        
        // Render debug lines (disable depth write, always pass depth test)
        if (m_debugRenderer->getLineCount() > 0 && m_debugLinePipelineState) {
            static bool logLineRenderOnce = true;
            if (logLineRenderOnce) {
                std::cout << "  Rendering " << m_debugRenderer->getLineCount() << " debug lines" << std::endl;
                logLineRenderOnce = false;
            }
            
            // Create depth stencil state for debug lines (avoid z-fighting)
            MTL::DepthStencilDescriptor* debugDepthDesc = MTL::DepthStencilDescriptor::alloc()->init();
            debugDepthDesc->setDepthCompareFunction(MTL::CompareFunctionLessEqual);
            debugDepthDesc->setDepthWriteEnabled(false);  // Don't write to depth buffer
            MTL::DepthStencilState* debugDepthState = m_device->newDepthStencilState(debugDepthDesc);
            debugDepthDesc->release();
            
            encoder->setRenderPipelineState(m_debugLinePipelineState);
            encoder->setDepthStencilState(debugDepthState);
            encoder->setDepthBias(-1.0f, -1.0f, -1.0f);
            encoder->setCullMode(MTL::CullModeNone);  // ✅ CRITICAL: Lines visible from all angles!
            encoder->setVertexBuffer((MTL::Buffer*)m_debugRenderer->getLineBuffer(), 0, 0);
            encoder->setVertexBuffer((MTL::Buffer*)m_debugRenderer->getUniformBuffer(), 0, 1);
            encoder->drawPrimitives(MTL::PrimitiveTypeLine, NS::UInteger(0), 
                                   NS::UInteger(m_debugRenderer->getLineCount()));
            encoder->setDepthBias(0.0f, 0.0f, 0.0f);
            
            debugDepthState->release();
        }
    }
    
    encoder->endEncoding();

    MTL::Texture* sceneColorForPost = m_colorTexture;
    bool useSSR = ssrEnabled && runPrepass && m_ssrPipelineState
        && m_postColorTexture && m_colorTexture && m_depthTexture && m_normalTexture;
    if (useSSR) {
        float maxDistance = std::min(100.0f, camera->getFarClip() * 0.5f);
        float thickness = std::max(0.001f, post.ssrThickness);
        SSRParamsGPU ssrParams{};
        ssrParams.settings0 = Math::Vector4(
            1.0f / static_cast<float>(renderWidth),
            1.0f / static_cast<float>(renderHeight),
            thickness,
            64.0f
        );
        ssrParams.settings1 = Math::Vector4(
            maxDistance,
            std::max(0.0f, std::min(1.0f, post.ssrMaxRoughness)),
            0.0f,
            maxDistance
        );

        MTL::RenderPassDescriptor* ssrPass = MTL::RenderPassDescriptor::alloc()->init();
        ssrPass->colorAttachments()->object(0)->setTexture(m_postColorTexture);
        ssrPass->colorAttachments()->object(0)->setLoadAction(MTL::LoadActionClear);
        ssrPass->colorAttachments()->object(0)->setStoreAction(MTL::StoreActionStore);
        ssrPass->colorAttachments()->object(0)->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));

        MTL::RenderCommandEncoder* ssrEncoder = commandBuffer->renderCommandEncoder(ssrPass);
        ssrEncoder->setRenderPipelineState(m_ssrPipelineState);
        ssrEncoder->setViewport(viewport);
        ssrEncoder->setFragmentBuffer(m_cameraUniformBuffer, 0, 0);
        ssrEncoder->setFragmentBytes(&ssrParams, sizeof(SSRParamsGPU), 1);
        ssrEncoder->setFragmentTexture(m_colorTexture, 0);
        ssrEncoder->setFragmentTexture(m_depthTexture, 1);
        ssrEncoder->setFragmentTexture(m_normalTexture, 2);
        if (m_linearClampSampler) {
            ssrEncoder->setFragmentSamplerState(m_linearClampSampler, 0);
        }
        ssrEncoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
        ssrEncoder->endEncoding();
        ssrPass->release();

        sceneColorForPost = m_postColorTexture;
    }

    bool useTAA = taaEnabled && runPrepass && m_taaPipelineState && m_depthTexture
        && m_taaHistoryTexture && m_taaCurrentTexture && sceneColorForPost;
    if (useTAA) {
        TAAParamsGPU taaParams{};
        taaParams.prevViewProjection = m_prevViewProjection;
        float sharpness = std::max(0.0f, std::min(1.0f, post.taaSharpness));
        float feedback = 0.2f - 0.15f * sharpness;
        taaParams.params0 = Math::Vector4(
            1.0f / static_cast<float>(renderWidth),
            1.0f / static_cast<float>(renderHeight),
            feedback,
            m_taaHistoryValid ? 1.0f : 0.0f
        );
        float useVelocity = runVelocity ? 1.0f : 0.0f;
        taaParams.params1 = Math::Vector4(sharpness, useVelocity, 0.01f, 0.2f);

        MTL::RenderPassDescriptor* taaPass = MTL::RenderPassDescriptor::alloc()->init();
        taaPass->colorAttachments()->object(0)->setTexture(m_taaCurrentTexture);
        taaPass->colorAttachments()->object(0)->setLoadAction(MTL::LoadActionClear);
        taaPass->colorAttachments()->object(0)->setStoreAction(MTL::StoreActionStore);
        taaPass->colorAttachments()->object(0)->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));

        MTL::RenderCommandEncoder* taaEncoder = commandBuffer->renderCommandEncoder(taaPass);
        taaEncoder->setRenderPipelineState(m_taaPipelineState);
        taaEncoder->setViewport(viewport);
        taaEncoder->setFragmentBuffer(m_cameraUniformBuffer, 0, 0);
        taaEncoder->setFragmentBytes(&taaParams, sizeof(TAAParamsGPU), 1);
        taaEncoder->setFragmentTexture(sceneColorForPost, 0);
        taaEncoder->setFragmentTexture(m_taaHistoryTexture, 1);
        taaEncoder->setFragmentTexture(m_depthTexture, 2);
        taaEncoder->setFragmentTexture(m_velocityTexture, 3);
        taaEncoder->setFragmentTexture(m_normalTexture, 4);
        if (m_linearClampSampler) {
            taaEncoder->setFragmentSamplerState(m_linearClampSampler, 0);
        }
        taaEncoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
        taaEncoder->endEncoding();
        taaPass->release();

        std::swap(m_taaHistoryTexture, m_taaCurrentTexture);
        sceneColorForPost = m_taaHistoryTexture;
        m_taaHistoryValid = true;
    }

    bool useMotionBlur = motionBlurEnabled && m_motionHistoryValid
        && m_motionBlurPipelineState && m_motionBlurTexture && m_velocityTexture && sceneColorForPost && m_depthTexture;
    if (useMotionBlur) {
        MotionBlurParamsGPU blurParams{};
        blurParams.prevViewProjection = m_prevViewProjectionNoJitter;
        blurParams.currViewProjection = viewProjectionNoJitter;
        float strength = std::max(0.0f, std::min(1.0f, post.motionBlurStrength));
        blurParams.params0 = Math::Vector4(
            1.0f / static_cast<float>(renderWidth),
            1.0f / static_cast<float>(renderHeight),
            strength,
            0.0f
        );

        MTL::RenderPassDescriptor* blurPass = MTL::RenderPassDescriptor::alloc()->init();
        blurPass->colorAttachments()->object(0)->setTexture(m_motionBlurTexture);
        blurPass->colorAttachments()->object(0)->setLoadAction(MTL::LoadActionClear);
        blurPass->colorAttachments()->object(0)->setStoreAction(MTL::StoreActionStore);
        blurPass->colorAttachments()->object(0)->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));

        MTL::RenderCommandEncoder* blurEncoder = commandBuffer->renderCommandEncoder(blurPass);
        blurEncoder->setRenderPipelineState(m_motionBlurPipelineState);
        blurEncoder->setViewport(viewport);
        blurEncoder->setFragmentBuffer(m_cameraUniformBuffer, 0, 0);
        blurEncoder->setFragmentBytes(&blurParams, sizeof(MotionBlurParamsGPU), 1);
        blurEncoder->setFragmentTexture(sceneColorForPost, 0);
        blurEncoder->setFragmentTexture(m_depthTexture, 1);
        blurEncoder->setFragmentTexture(m_velocityTexture, 2);
        if (m_linearClampSampler) {
            blurEncoder->setFragmentSamplerState(m_linearClampSampler, 0);
        }
        blurEncoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
        blurEncoder->endEncoding();
        blurPass->release();

        sceneColorForPost = m_motionBlurTexture;
    }

    FogParamsGPU fogParams{};
    if (fogEnabled) {
        fogParams.fogColorDensity = Math::Vector4(
            fog.color.x,
            fog.color.y,
            fog.color.z,
            std::max(0.0f, fog.density)
        );
        float startDistance = std::max(0.0f, fog.startDistance);
        float endDistance = std::max(startDistance + 0.001f, fog.endDistance);
        fogParams.distanceParams = Math::Vector4(
            startDistance,
            endDistance,
            fog.height,
            std::max(0.0001f, fog.heightFalloff)
        );
        Math::Vector3 sunDir = mainLight ? mainLight->getDirection() : Math::Vector3(0.0f, -1.0f, 0.0f);
        Math::Vector3 sunColor = mainLight ? mainLight->getColor() : Math::Vector3(1.0f, 1.0f, 1.0f);
        float sunIntensity = mainLight ? mainLight->getIntensity() : 0.0f;
        fogParams.sunDirIntensity = Math::Vector4(sunDir.x, sunDir.y, sunDir.z, sunIntensity);
        fogParams.sunColor = Math::Vector4(
            sunColor.x,
            sunColor.y,
            sunColor.z,
            std::max(0.0f, fog.volumetricScattering)
        );
        float nearPlane = std::max(0.001f, camera->getNearClip());
        float farPlane = std::max(nearPlane + 0.001f, camera->getFarClip());
        float sliceCount = (m_fogVolumeDepth > 0) ? static_cast<float>(m_fogVolumeDepth) : 64.0f;
        float historyWeight = std::max(0.0f, std::min(0.98f, fog.volumetricHistoryWeight));
        float historyValid = m_fogVolumeHistoryValid ? 1.0f : 0.0f;
        if (camera->getEntity()) {
            Transform* camTransform = camera->getEntity()->getTransform();
            Math::Vector3 camPos = camTransform->getPosition();
            Math::Vector3 camForward = camTransform->forward().normalized();
            Math::Vector3 sunDirNorm = sunDir.normalized();

            if (!m_fogHistoryInitialized) {
                m_fogHistoryInitialized = true;
                m_prevFogCameraPos = camPos;
                m_prevFogCameraForward = camForward;
                m_prevFogSunDir = sunDirNorm;
                m_prevFogSunIntensity = sunIntensity;
                m_fogVolumeHistoryValid = false;
            }

            float posDelta = (camPos - m_prevFogCameraPos).length();
            float rotDelta = std::acos(Math::Clamp(camForward.dot(m_prevFogCameraForward), -1.0f, 1.0f));
            float sunDelta = std::acos(Math::Clamp(sunDirNorm.dot(m_prevFogSunDir), -1.0f, 1.0f));
            float intensityDelta = std::abs(sunIntensity - m_prevFogSunIntensity);

            if (posDelta > 0.05f || rotDelta > 0.02f || sunDelta > 0.01f || intensityDelta > 0.1f) {
                m_fogVolumeHistoryValid = false;
            }
            historyValid = m_fogVolumeHistoryValid ? 1.0f : 0.0f;
            if (historyValid > 0.5f) {
                float motionScore = posDelta * 8.0f + rotDelta * 5.0f + sunDelta * 3.0f + intensityDelta * 0.05f;
                float historyScale = Math::Clamp(1.0f - motionScore, 0.0f, 1.0f);
                historyWeight *= historyScale;
            } else {
                historyWeight = 0.0f;
            }

            m_prevFogCameraPos = camPos;
            m_prevFogCameraForward = camForward;
            m_prevFogSunDir = sunDirNorm;
            m_prevFogSunIntensity = sunIntensity;
        }

        fogParams.volumeParams = Math::Vector4(
            nearPlane,
            farPlane,
            sliceCount,
            historyWeight
        );
        float heightEnabled = fog.heightFog ? 1.0f : 0.0f;
        float anisotropy = std::max(0.0f, std::min(0.9f, fog.volumetricAnisotropy));
        fogParams.misc = Math::Vector4(heightEnabled, anisotropy, historyValid, 0.0f);

        int shadowIndex = -1;
        int cascadeCount = 0;
        if (mainLight && m_lightingSystem && m_shadowPass && m_shadowPass->getShadowAtlas()) {
            const auto& prepared = m_lightingSystem->getPreparedLights();
            for (const auto& pl : prepared) {
                if (pl.light == mainLight && pl.shadowStart != UINT32_MAX && pl.shadowCount > 0) {
                    shadowIndex = static_cast<int>(pl.shadowStart);
                    cascadeCount = static_cast<int>(pl.shadowCount);
                    break;
                }
            }
        }
        float shadowEnabled = (shadowIndex >= 0 && cascadeCount > 0) ? 1.0f : 0.0f;
        fogParams.shadowParams = Math::Vector4(
            static_cast<float>(shadowIndex),
            static_cast<float>(cascadeCount),
            shadowEnabled,
            1.0f
        );
    }

    bool buildFogVolume = fogEnabled && m_fogVolumePipelineState && m_fogVolumeTexture;
    if (buildFogVolume) {
        MTL::ComputeCommandEncoder* fogCompute = commandBuffer->computeCommandEncoder();
        fogCompute->setComputePipelineState(m_fogVolumePipelineState);
        fogCompute->setTexture(m_fogVolumeTexture, 0);
        fogCompute->setTexture(m_fogVolumeHistoryTexture, 1);
        if (m_shadowPass && m_shadowPass->getShadowAtlas()) {
            fogCompute->setTexture(m_shadowPass->getShadowAtlas(), 2);
        } else if (m_depthTexture) {
            fogCompute->setTexture(m_depthTexture, 2);
        }
        fogCompute->setBuffer(m_cameraUniformBuffer, 0, 0);
        fogCompute->setBytes(&fogParams, sizeof(FogParamsGPU), 1);
        if (m_shadowGPUBuffer) {
            fogCompute->setBuffer(m_shadowGPUBuffer, 0, 2);
        }
        if (m_linearClampSampler) {
            fogCompute->setSamplerState(m_linearClampSampler, 0);
        }
        MTL::Size threadsPerGroup = MTL::Size::Make(4, 4, 4);
        MTL::Size threadgroups = MTL::Size::Make(
            (m_fogVolumeWidth + threadsPerGroup.width - 1) / threadsPerGroup.width,
            (m_fogVolumeHeight + threadsPerGroup.height - 1) / threadsPerGroup.height,
            (m_fogVolumeDepth + threadsPerGroup.depth - 1) / threadsPerGroup.depth
        );
        fogCompute->dispatchThreadgroups(threadgroups, threadsPerGroup);
        fogCompute->endEncoding();
    }

    bool useFog = buildFogVolume && runPrepass && m_fogPipelineState && m_fogTexture
        && sceneColorForPost && m_depthTexture;
    if (useFog) {
        MTL::RenderPassDescriptor* fogPass = MTL::RenderPassDescriptor::alloc()->init();
        fogPass->colorAttachments()->object(0)->setTexture(m_fogTexture);
        fogPass->colorAttachments()->object(0)->setLoadAction(MTL::LoadActionClear);
        fogPass->colorAttachments()->object(0)->setStoreAction(MTL::StoreActionStore);
        fogPass->colorAttachments()->object(0)->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));

        MTL::RenderCommandEncoder* fogEncoder = commandBuffer->renderCommandEncoder(fogPass);
        fogEncoder->setRenderPipelineState(m_fogPipelineState);
        fogEncoder->setViewport(viewport);
        fogEncoder->setFragmentBuffer(m_cameraUniformBuffer, 0, 0);
        fogEncoder->setFragmentBytes(&fogParams, sizeof(FogParamsGPU), 1);
        fogEncoder->setFragmentTexture(sceneColorForPost, 0);
        fogEncoder->setFragmentTexture(m_depthTexture, 1);
        fogEncoder->setFragmentTexture(m_fogVolumeTexture, 2);
        if (m_linearClampSampler) {
            fogEncoder->setFragmentSamplerState(m_linearClampSampler, 0);
        } else if (m_samplerState) {
            fogEncoder->setFragmentSamplerState(m_samplerState, 0);
        }
        fogEncoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
        fogEncoder->endEncoding();
        fogPass->release();

        sceneColorForPost = m_fogTexture;
        m_fogVolumeHistoryValid = true;
        std::swap(m_fogVolumeTexture, m_fogVolumeHistoryTexture);
    }

    bool useDOF = dofEnabled && runPrepass && m_dofPipelineState && m_dofTexture && sceneColorForPost && m_depthTexture;
    if (useDOF) {
        DofParamsGPU dofParams{};
        float focusDistance = std::max(0.01f, post.dofFocusDistance);
        float aperture = std::max(0.1f, post.dofAperture);
        dofParams.params0 = Math::Vector4(
            1.0f / static_cast<float>(renderWidth),
            1.0f / static_cast<float>(renderHeight),
            focusDistance,
            aperture
        );
        dofParams.params1 = Math::Vector4(12.0f, 0.0f, 0.0f, 0.0f);

        MTL::RenderPassDescriptor* dofPass = MTL::RenderPassDescriptor::alloc()->init();
        dofPass->colorAttachments()->object(0)->setTexture(m_dofTexture);
        dofPass->colorAttachments()->object(0)->setLoadAction(MTL::LoadActionClear);
        dofPass->colorAttachments()->object(0)->setStoreAction(MTL::StoreActionStore);
        dofPass->colorAttachments()->object(0)->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));

        MTL::RenderCommandEncoder* dofEncoder = commandBuffer->renderCommandEncoder(dofPass);
        dofEncoder->setRenderPipelineState(m_dofPipelineState);
        dofEncoder->setViewport(viewport);
        dofEncoder->setFragmentBuffer(m_cameraUniformBuffer, 0, 0);
        dofEncoder->setFragmentBytes(&dofParams, sizeof(DofParamsGPU), 1);
        dofEncoder->setFragmentTexture(sceneColorForPost, 0);
        dofEncoder->setFragmentTexture(m_depthTexture, 1);
        if (m_linearClampSampler) {
            dofEncoder->setFragmentSamplerState(m_linearClampSampler, 0);
        }
        dofEncoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
        dofEncoder->endEncoding();
        dofPass->release();

        sceneColorForPost = m_dofTexture;
    }

    bool useBloom = bloomEnabled && m_bloomPrefilterPipelineState && m_bloomDownsamplePipelineState
        && m_bloomUpsamplePipelineState && m_bloomCombinePipelineState
        && !m_bloomMipTextures.empty() && sceneColorForPost;
    if (useBloom) {
        MTL::Texture* mip0 = m_bloomMipTextures[0];
        uint32_t mipWidth = mip0 ? mip0->width() : 0;
        uint32_t mipHeight = mip0 ? mip0->height() : 0;
        if (mip0 && mipWidth > 0 && mipHeight > 0) {
            MTL::Viewport mipViewport = {
                0.0, 0.0,
                static_cast<double>(mipWidth), static_cast<double>(mipHeight),
                0.0, 1.0
            };

            float threshold = std::max(0.0f, post.bloomThreshold);
            float knee = threshold * 0.5f;
            BloomPrefilterParamsGPU preParams{};
            preParams.threshold = threshold;
            preParams.knee = knee;

            MTL::RenderPassDescriptor* prePass = MTL::RenderPassDescriptor::alloc()->init();
            prePass->colorAttachments()->object(0)->setTexture(mip0);
            prePass->colorAttachments()->object(0)->setLoadAction(MTL::LoadActionClear);
            prePass->colorAttachments()->object(0)->setStoreAction(MTL::StoreActionStore);
            prePass->colorAttachments()->object(0)->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));

            MTL::RenderCommandEncoder* preEncoder = commandBuffer->renderCommandEncoder(prePass);
            preEncoder->setRenderPipelineState(m_bloomPrefilterPipelineState);
            preEncoder->setViewport(mipViewport);
            preEncoder->setFragmentBytes(&preParams, sizeof(BloomPrefilterParamsGPU), 0);
            preEncoder->setFragmentTexture(sceneColorForPost, 0);
            if (m_linearClampSampler) {
                preEncoder->setFragmentSamplerState(m_linearClampSampler, 0);
            }
            preEncoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
            preEncoder->endEncoding();
            prePass->release();

            for (size_t i = 1; i < m_bloomMipTextures.size(); ++i) {
                MTL::Texture* src = m_bloomMipTextures[i - 1];
                MTL::Texture* dst = m_bloomMipTextures[i];
                if (!src || !dst) {
                    continue;
                }
                uint32_t dstWidth = dst->width();
                uint32_t dstHeight = dst->height();
                MTL::Viewport downViewport = {
                    0.0, 0.0,
                    static_cast<double>(dstWidth), static_cast<double>(dstHeight),
                    0.0, 1.0
                };

                BloomDownsampleParamsGPU downParams{};
                downParams.texelSize = Math::Vector2(1.0f / src->width(), 1.0f / src->height());

                MTL::RenderPassDescriptor* downPass = MTL::RenderPassDescriptor::alloc()->init();
                downPass->colorAttachments()->object(0)->setTexture(dst);
                downPass->colorAttachments()->object(0)->setLoadAction(MTL::LoadActionClear);
                downPass->colorAttachments()->object(0)->setStoreAction(MTL::StoreActionStore);
                downPass->colorAttachments()->object(0)->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));

                MTL::RenderCommandEncoder* downEncoder = commandBuffer->renderCommandEncoder(downPass);
                downEncoder->setRenderPipelineState(m_bloomDownsamplePipelineState);
                downEncoder->setViewport(downViewport);
                downEncoder->setFragmentBytes(&downParams, sizeof(BloomDownsampleParamsGPU), 0);
                downEncoder->setFragmentTexture(src, 0);
                if (m_linearClampSampler) {
                    downEncoder->setFragmentSamplerState(m_linearClampSampler, 0);
                }
                downEncoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
                downEncoder->endEncoding();
                downPass->release();
            }

            for (size_t i = m_bloomMipTextures.size(); i-- > 1; ) {
                MTL::Texture* src = m_bloomMipTextures[i];
                MTL::Texture* dst = m_bloomMipTextures[i - 1];
                if (!src || !dst) {
                    continue;
                }
                uint32_t dstWidth = dst->width();
                uint32_t dstHeight = dst->height();
                MTL::Viewport upViewport = {
                    0.0, 0.0,
                    static_cast<double>(dstWidth), static_cast<double>(dstHeight),
                    0.0, 1.0
                };

                BloomUpsampleParamsGPU upParams{};
                upParams.texelSize = Math::Vector2(1.0f / src->width(), 1.0f / src->height());
                upParams.radius = 1.0f;

                MTL::RenderPassDescriptor* upPass = MTL::RenderPassDescriptor::alloc()->init();
                upPass->colorAttachments()->object(0)->setTexture(dst);
                upPass->colorAttachments()->object(0)->setLoadAction(MTL::LoadActionLoad);
                upPass->colorAttachments()->object(0)->setStoreAction(MTL::StoreActionStore);

                MTL::RenderCommandEncoder* upEncoder = commandBuffer->renderCommandEncoder(upPass);
                upEncoder->setRenderPipelineState(m_bloomUpsamplePipelineState);
                upEncoder->setViewport(upViewport);
                upEncoder->setFragmentBytes(&upParams, sizeof(BloomUpsampleParamsGPU), 0);
                upEncoder->setFragmentTexture(src, 0);
                if (m_linearClampSampler) {
                    upEncoder->setFragmentSamplerState(m_linearClampSampler, 0);
                }
                upEncoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
                upEncoder->endEncoding();
                upPass->release();
            }
        }
    }

    if (useOffscreen || (useMSAA && !resolveToDrawable)) {
        if (!m_blitPipelineState || !m_colorTexture) {
            std::cerr << "Blit pass skipped: missing pipeline or source texture\n";
        } else {
            MTL::RenderPassDescriptor* blitPass = MTL::RenderPassDescriptor::alloc()->init();
            blitPass->colorAttachments()->object(0)->setTexture(drawable->texture());
            blitPass->colorAttachments()->object(0)->setLoadAction(MTL::LoadActionDontCare);
            blitPass->colorAttachments()->object(0)->setStoreAction(MTL::StoreActionStore);
            
            MTL::RenderCommandEncoder* blitEncoder = commandBuffer->renderCommandEncoder(blitPass);
            float vignetteIntensity = (post.enabled && post.vignette) ? std::max(0.0f, std::min(1.0f, post.vignetteIntensity)) : 0.0f;
            float grainIntensity = (post.enabled && post.filmGrain) ? std::max(0.0f, std::min(1.0f, post.filmGrainIntensity)) : 0.0f;
            float gradingIntensity = (post.enabled && post.colorGrading)
                ? std::max(0.0f, std::min(1.0f, post.colorGradingIntensity))
                : 0.0f;
            float toneMapping = 0.0f;
            if (post.enabled && post.toneMapping) {
                toneMapping = (post.toneMappingMode == 2) ? 2.0f : 1.0f;
            }
            PostProcessParamsGPU postParams{};
            postParams.params0 = Math::Vector4(
                vignetteIntensity,
                grainIntensity,
                160.0f,
                static_cast<float>(m_frameIndex) * 0.013f
            );
            postParams.params1 = Math::Vector4(gradingIntensity, toneMapping, 0.0f, 0.0f);
            bool lutValid = (m_colorGradingLUT != nullptr) || (m_colorGradingNeutralLUT != nullptr);
            MTL::Texture* lutTexture = m_colorGradingLUT ? m_colorGradingLUT->getHandle() : nullptr;
            if (!lutTexture && m_colorGradingNeutralLUT) {
                lutTexture = m_colorGradingNeutralLUT->getHandle();
            }
            if (!lutTexture && m_defaultWhiteTexture) {
                lutTexture = m_defaultWhiteTexture->getHandle();
            }
            if (!lutValid || !lutTexture) {
                postParams.params1.x = 0.0f;
            }

            if (useBloom && m_bloomCombinePipelineState && !m_bloomMipTextures.empty()) {
                BloomCombineParamsGPU combineParams{};
                combineParams.intensity = std::max(0.0f, post.bloomIntensity);
                blitEncoder->setRenderPipelineState(m_bloomCombinePipelineState);
                blitEncoder->setFragmentBytes(&combineParams, sizeof(BloomCombineParamsGPU), 0);
                blitEncoder->setFragmentBytes(&postParams, sizeof(PostProcessParamsGPU), 1);
                blitEncoder->setFragmentTexture(sceneColorForPost, 0);
                blitEncoder->setFragmentTexture(m_bloomMipTextures[0], 1);
                if (lutTexture) {
                    blitEncoder->setFragmentTexture(lutTexture, 2);
                }
            } else {
                blitEncoder->setRenderPipelineState(m_blitPipelineState);
                blitEncoder->setFragmentBytes(&postParams, sizeof(PostProcessParamsGPU), 0);
                blitEncoder->setFragmentTexture(sceneColorForPost, 0);
                if (lutTexture) {
                    blitEncoder->setFragmentTexture(lutTexture, 1);
                }
            }
            if (m_linearClampSampler) {
                blitEncoder->setFragmentSamplerState(m_linearClampSampler, 0);
            } else if (m_samplerState) {
                blitEncoder->setFragmentSamplerState(m_samplerState, 0);
            }
            MTL::Viewport blitViewport = {
                0.0, 0.0,
                m_viewportWidth, m_viewportHeight,
                0.0, 1.0
            };
            blitEncoder->setViewport(blitViewport);
            blitEncoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
            blitEncoder->endEncoding();
            
            blitPass->release();
        }
    }

    if (options.updateHistory) {
        if (taaEnabled && !useTAA) {
            m_taaHistoryValid = false;
        }
        m_prevViewProjection = viewProjection;
        m_prevViewProjectionNoJitter = viewProjectionNoJitter;
        m_motionHistoryValid = true;
        m_frameIndex++;
    }

    // Present
    commandBuffer->presentDrawable(drawable);
    commandBuffer->commit();
    
    // Cleanup
    renderPass->release();
}

std::shared_ptr<Texture2D> Renderer::bakeImpostorAtlas(Mesh* mesh,
                                                       Material* material,
                                                       uint32_t rows,
                                                       uint32_t cols,
                                                       uint32_t tileSize,
                                                       std::string* outPath) {
    if (!m_device || !m_commandQueue || !mesh || rows == 0 || cols == 0 || tileSize == 0) {
        return nullptr;
    }
    if (!m_impostorBakePipeline) {
        buildImpostorPipeline();
    }
    if (!m_impostorBakePipeline) {
        return nullptr;
    }
    if (!mesh->isUploaded()) {
        uploadMesh(mesh);
    }

    MTL::Buffer* vertexBuffer = static_cast<MTL::Buffer*>(mesh->getVertexBuffer());
    MTL::Buffer* indexBuffer = static_cast<MTL::Buffer*>(mesh->getIndexBuffer());
    if (!vertexBuffer || !indexBuffer) {
        return nullptr;
    }

    uint32_t width = cols * tileSize;
    uint32_t height = rows * tileSize;

    MTL::TextureDescriptor* colorDesc = MTL::TextureDescriptor::alloc()->init();
    colorDesc->setTextureType(MTL::TextureType2D);
    colorDesc->setWidth(width);
    colorDesc->setHeight(height);
    colorDesc->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
    colorDesc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
    colorDesc->setStorageMode(MTL::StorageModeShared);
    MTL::Texture* colorTex = m_device->newTexture(colorDesc);
    colorDesc->release();

    MTL::TextureDescriptor* depthDesc = MTL::TextureDescriptor::alloc()->init();
    depthDesc->setTextureType(MTL::TextureType2D);
    depthDesc->setWidth(width);
    depthDesc->setHeight(height);
    depthDesc->setPixelFormat(MTL::PixelFormatDepth32Float);
    depthDesc->setUsage(MTL::TextureUsageRenderTarget);
    depthDesc->setStorageMode(MTL::StorageModeShared);
    MTL::Texture* depthTex = m_device->newTexture(depthDesc);
    depthDesc->release();

    if (!colorTex || !depthTex) {
        if (colorTex) colorTex->release();
        if (depthTex) depthTex->release();
        return nullptr;
    }

    struct BakeParams {
        Math::Matrix4x4 viewProjection;
        Math::Vector4 albedo;
        Math::Vector4 uvTilingOffset;
        Math::Vector4 lightDirHasTex;
    };

    Math::Vector3 boundsCenter = mesh->getBoundsCenter();
    Math::Vector3 boundsSize = mesh->getBoundsSize();
    float radius = std::max(0.1f, std::max(boundsSize.x, std::max(boundsSize.y, boundsSize.z)) * 0.5f);
    float orthoSize = radius * 1.2f;
    float nearZ = 0.01f;
    float farZ = radius * 6.0f;

    MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::alloc()->init();
    pass->colorAttachments()->object(0)->setTexture(colorTex);
    pass->colorAttachments()->object(0)->setLoadAction(MTL::LoadActionClear);
    pass->colorAttachments()->object(0)->setStoreAction(MTL::StoreActionStore);
    pass->colorAttachments()->object(0)->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 0.0));
    pass->depthAttachment()->setTexture(depthTex);
    pass->depthAttachment()->setLoadAction(MTL::LoadActionClear);
    pass->depthAttachment()->setStoreAction(MTL::StoreActionDontCare);
    pass->depthAttachment()->setClearDepth(1.0);

    MTL::CommandBuffer* commandBuffer = m_commandQueue->commandBuffer();
    MTL::RenderCommandEncoder* encoder = commandBuffer->renderCommandEncoder(pass);
    encoder->setRenderPipelineState(m_impostorBakePipeline);
    if (m_depthStencilState) {
        encoder->setDepthStencilState(m_depthStencilState);
    }
    encoder->setCullMode(MTL::CullModeBack);

    BakeParams params{};
    if (material) {
        params.albedo = material->getAlbedo();
        Math::Vector2 tiling = material->getUVTiling();
        Math::Vector2 offset = material->getUVOffset();
        params.uvTilingOffset = Math::Vector4(tiling.x, tiling.y, offset.x, offset.y);
    } else {
        params.albedo = Math::Vector4(1.0f);
        params.uvTilingOffset = Math::Vector4(1.0f, 1.0f, 0.0f, 0.0f);
    }
    params.lightDirHasTex = Math::Vector4(0.3f, 0.8f, 0.4f, material && material->getAlbedoTexture() ? 1.0f : 0.0f);

    auto albedoTex = material && material->getAlbedoTexture() ? material->getAlbedoTexture() : m_defaultWhiteTexture;
    encoder->setFragmentTexture(albedoTex ? albedoTex->getHandle() : nullptr, 0);
    if (m_samplerState) {
        encoder->setFragmentSamplerState(m_samplerState, 0);
    }
    encoder->setVertexBuffer(vertexBuffer, 0, 0);

    MTL::ScissorRect scissor{};
    for (uint32_t row = 0; row < rows; ++row) {
        for (uint32_t col = 0; col < cols; ++col) {
            float azimuth = (static_cast<float>(col) + 0.5f) / static_cast<float>(cols) * Math::TWO_PI - Math::PI;
            float elevation = (static_cast<float>(row) + 0.5f) / static_cast<float>(rows) * Math::PI - Math::HALF_PI;
            float cosEl = std::cos(elevation);
            Math::Vector3 dir(
                std::sin(azimuth) * cosEl,
                std::sin(elevation),
                std::cos(azimuth) * cosEl
            );
            Math::Vector3 cameraPos = boundsCenter + dir * (radius * 2.5f);
            Math::Matrix4x4 view = Math::Matrix4x4::LookAt(cameraPos, boundsCenter, Math::Vector3::Up);
            Math::Matrix4x4 proj = Math::Matrix4x4::Orthographic(-orthoSize, orthoSize, -orthoSize, orthoSize, nearZ, farZ);
            params.viewProjection = proj * view;

            MTL::Viewport vp = {
                static_cast<double>(col * tileSize),
                static_cast<double>(row * tileSize),
                static_cast<double>(tileSize),
                static_cast<double>(tileSize),
                0.0,
                1.0
            };
            encoder->setViewport(vp);
            scissor.x = col * tileSize;
            scissor.y = row * tileSize;
            scissor.width = tileSize;
            scissor.height = tileSize;
            encoder->setScissorRect(scissor);
            encoder->setVertexBytes(&params, sizeof(BakeParams), 1);
            encoder->setFragmentBytes(&params, sizeof(BakeParams), 0);
            encoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle,
                                           mesh->getIndices().size(),
                                           MTL::IndexTypeUInt32,
                                           indexBuffer,
                                           0);
        }
    }

    encoder->endEncoding();
    pass->release();
    commandBuffer->commit();
    commandBuffer->waitUntilCompleted();

    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    MTL::Region region = MTL::Region::Make2D(0, 0, width, height);
    colorTex->getBytes(pixels.data(), width * 4, region, 0);

    std::string outputPath;
    AssetDatabase& db = AssetDatabase::getInstance();
    std::string root = db.getRootPath();
    if (root.empty()) {
        root = std::filesystem::current_path().string();
    }
    std::filesystem::path outDir = std::filesystem::path(root) / "Assets" / "Generated" / "Impostors";
    std::filesystem::create_directories(outDir);
    auto sanitize = [](std::string name) {
        for (char& c : name) {
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) {
                c = '_';
            }
        }
        return name;
    };
    std::string meshName = sanitize(mesh->getName().empty() ? "Mesh" : mesh->getName());
    std::string matName = sanitize(material ? material->getName() : "Material");
    outputPath = (outDir / (meshName + "_" + matName + "_impostor.png")).string();

    stbi_write_png(outputPath.c_str(), static_cast<int>(width), static_cast<int>(height), 4, pixels.data(), static_cast<int>(width * 4));

    if (outPath) {
        *outPath = outputPath;
    }

    if (depthTex) depthTex->release();
    if (colorTex) colorTex->release();

    if (!m_textureLoader) {
        return nullptr;
    }
    return m_textureLoader->loadTexture(outputPath, true, false);
}

void Renderer::renderSkybox(MTL::RenderCommandEncoder* encoder, Camera* camera) {
    if (!encoder || !camera || !m_skyboxPipelineState) {
        return;
    }
    
    if (!m_environmentSettings.skyboxVisible) {
        return;
    }
    
    encoder->setRenderPipelineState(m_skyboxPipelineState);
    if (m_skyboxDepthState) {
        encoder->setDepthStencilState(m_skyboxDepthState);
    }
    encoder->setCullMode(MTL::CullModeNone);
    
    // Fragment shader needs camera uniforms for ray direction calculation
    encoder->setFragmentBuffer(m_cameraUniformBuffer, 0, 1);
    
    // Environment data
    if (m_environmentUniformBuffer) {
        encoder->setFragmentBuffer(m_environmentUniformBuffer, 0, 0);
    }
    if (m_lightUniformBuffer) {
        encoder->setFragmentBuffer(m_lightUniformBuffer, 0, 2);
    }
    auto envTex = (m_environmentTexture ? m_environmentTexture : m_defaultEnvironmentTexture);
    encoder->setFragmentTexture(envTex ? envTex->getHandle() : nullptr, 0);
    if (m_samplerState) {
        encoder->setFragmentSamplerState(m_samplerState, 0);
    }
    
    // Draw full-screen triangle (3 vertices, no buffer)
    encoder->drawPrimitives(
        MTL::PrimitiveTypeTriangle,
        NS::UInteger(0),
        NS::UInteger(3)
    );
}

void Renderer::updateEnvironmentUniforms() {
    if (!m_environmentUniformBuffer) {
        return;
    }
    
    EnvironmentUniformsGPU* env = static_cast<EnvironmentUniformsGPU*>(m_environmentUniformBuffer->contents());
    env->exposureIntensity = Math::Vector4(
        m_environmentSettings.exposureEV,
        m_environmentSettings.iblIntensity,
        m_environmentSettings.skyIntensity,
        Math::Clamp(m_environmentSettings.blurLevel, 0.0f, 12.0f)
    );
    
    env->colorControl = Math::Vector4(
        m_environmentSettings.tint.x,
        m_environmentSettings.tint.y,
        m_environmentSettings.tint.z,
        Math::Clamp(m_environmentSettings.saturation, 0.0f, 4.0f)
    );
    
    env->toneControl = Math::Vector4(
        Math::Clamp(m_environmentSettings.contrast, 0.1f, 3.0f),
        m_hasIBL ? 1.0f : 0.0f,  // hasProperIBL flag
        m_environmentSettings.skyboxVisible ? 1.0f : 0.0f,
        m_outputHDR ? 1.0f : 0.0f
    );
    env->skyParams = Math::Vector4(
        static_cast<float>(m_environmentSettings.skyMode),
        0.0f,
        0.0f,
        0.0f
    );
    
    Math::Vector3 eulerRad(
        m_environmentSettings.rotation.x * Math::DEG_TO_RAD,
        m_environmentSettings.rotation.y * Math::DEG_TO_RAD,
        m_environmentSettings.rotation.z * Math::DEG_TO_RAD
    );
    Math::Quaternion q = Math::Quaternion::FromEulerAngles(eulerRad);
    Math::Matrix4x4 rot = Math::Matrix4x4::Rotate(q);
    env->rot0 = Math::Vector4(rot(0, 0), rot(1, 0), rot(2, 0), 0.0f);
    env->rot1 = Math::Vector4(rot(0, 1), rot(1, 1), rot(2, 1), 0.0f);
    env->rot2 = Math::Vector4(rot(0, 2), rot(1, 2), rot(2, 2), 0.0f);
}

bool Renderer::loadEnvironmentMap(const std::string& path) {
    if (!m_textureLoader) {
        return false;
    }
    
    auto texture = m_textureLoader->loadTexture(path, false, false);
    if (!texture) {
        return false;
    }
    
    m_environmentTexture = texture;
    m_environmentSettings.sourcePath = path;
    
    // Process IBL if generator is available
    if (m_iblGenerator && m_iblGenerator->isInitialized()) {
        std::cout << "Processing IBL for: " << path << std::endl;
        
        // Release old IBL textures
        if (m_iblCubemap) {
            m_iblCubemap->release();
            m_iblCubemap = nullptr;
        }
        if (m_iblPrefiltered) {
            m_iblPrefiltered->release();
            m_iblPrefiltered = nullptr;
        }
        if (m_iblIrradiance) {
            m_iblIrradiance->release();
            m_iblIrradiance = nullptr;
        }
        
        // Get the raw Metal texture handle
        MTL::Texture* equirectTex = static_cast<MTL::Texture*>(texture->getHandle());
        if (equirectTex) {
            auto iblTextures = m_iblGenerator->processEnvironmentMap(equirectTex);
            m_iblCubemap = iblTextures.cubemap;
            m_iblPrefiltered = iblTextures.prefiltered;
            m_iblIrradiance = iblTextures.irradiance;
            m_hasIBL = (m_iblCubemap && m_iblPrefiltered && m_iblIrradiance);
            
            if (m_hasIBL) {
                std::cout << "IBL processing complete - high quality IBL enabled" << std::endl;
            } else {
                std::cout << "IBL processing incomplete - using fallback" << std::endl;
            }
        }
    }
    
    return true;
}

void Renderer::resetEnvironment() {
    // Release IBL textures
    if (m_iblCubemap) {
        m_iblCubemap->release();
        m_iblCubemap = nullptr;
    }
    if (m_iblPrefiltered) {
        m_iblPrefiltered->release();
        m_iblPrefiltered = nullptr;
    }
    if (m_iblIrradiance) {
        m_iblIrradiance->release();
        m_iblIrradiance = nullptr;
    }
    m_hasIBL = false;
    
    m_environmentTexture = m_defaultEnvironmentTexture ? m_defaultEnvironmentTexture : m_defaultWhiteTexture;
    m_environmentSettings = EnvironmentSettings(); // resets to defaults
}

void Renderer::setEnvironmentRotation(const Math::Vector3& eulerDegrees) {
    m_environmentSettings.rotation = eulerDegrees;
}

void Renderer::setEnvironmentExposure(float ev) {
    m_environmentSettings.exposureEV = ev;
}

void Renderer::setEnvironmentIblIntensity(float intensity) {
    m_environmentSettings.iblIntensity = std::max(0.0f, intensity);
}

void Renderer::setEnvironmentSkyIntensity(float intensity) {
    m_environmentSettings.skyIntensity = std::max(0.0f, intensity);
}

void Renderer::setEnvironmentTint(const Math::Vector3& tint) {
    m_environmentSettings.tint = tint;
}

void Renderer::setEnvironmentSaturation(float saturation) {
    m_environmentSettings.saturation = saturation;
}

void Renderer::setEnvironmentContrast(float contrast) {
    m_environmentSettings.contrast = contrast;
}

void Renderer::setEnvironmentBlur(float blur) {
    m_environmentSettings.blurLevel = std::max(0.0f, blur);
}

void Renderer::setEnvironmentSkyboxVisible(bool visible) {
    m_environmentSettings.skyboxVisible = visible;
}

void Renderer::setEnvironmentSkyMode(int mode) {
    m_environmentSettings.skyMode = (mode == 1) ? 1 : 0;
}

void Renderer::setEnvironmentAutoSunColor(bool enabled) {
    m_environmentSettings.autoSunColor = enabled;
}

void Renderer::setColorGradingLUT(const std::string& path) {
    if (path == m_colorGradingLUTPath) {
        return;
    }
    m_colorGradingLUTPath = path;
    if (!m_textureLoader || path.empty()) {
        m_colorGradingLUT = m_colorGradingNeutralLUT;
        return;
    }
    std::shared_ptr<Texture2D> texture;
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".cube") {
        texture = CreateLUTTextureFromCube(m_textureLoader.get(), path);
    } else {
        texture = m_textureLoader->loadTexture(path, false, false);
    }
    if (texture) {
        m_colorGradingLUT = texture;
    } else {
        m_colorGradingLUT = m_colorGradingNeutralLUT;
        m_colorGradingLUTPath.clear();
    }
}

void Renderer::setDebugDrawShadowAtlas(bool enabled) {
    m_debugDrawShadowAtlas = enabled;
}

void Renderer::setDebugDrawCascades(bool enabled) {
    m_debugDrawCascades = enabled;
}

void Renderer::setDebugDrawPointFrusta(bool enabled) {
    m_debugDrawPointFrusta = enabled;
}

void Renderer::renderMeshRenderer(MeshRenderer* renderer, Camera* camera, const std::vector<Light*>& lights) {
    // This is called per mesh renderer - we could do culling here
}

void Renderer::shutdown() {
    // Shutdown IBL generator
    if (m_iblGenerator) {
        m_iblGenerator->shutdown();
        m_iblGenerator.reset();
    }
    
    // Release IBL textures (not owned by generator)
    if (m_iblCubemap) {
        m_iblCubemap->release();
        m_iblCubemap = nullptr;
    }
    if (m_iblPrefiltered) {
        m_iblPrefiltered->release();
        m_iblPrefiltered = nullptr;
    }
    if (m_iblIrradiance) {
        m_iblIrradiance->release();
        m_iblIrradiance = nullptr;
    }
    m_iblBRDFLUT = nullptr; // Owned by generator
    m_hasIBL = false;
    
    // Shutdown debug renderer
    if (m_debugRenderer) {
        m_debugRenderer->shutdown();
        m_debugRenderer.reset();
    }
    
    if (m_shadowPass) {
        m_shadowPass->shutdown();
        m_shadowPass.reset();
    }
    if (m_clusterPass) {
        m_clusterPass->shutdown();
        m_clusterPass.reset();
    }
    
    if (m_debugLinePipelineState) {
        m_debugLinePipelineState->release();
        m_debugLinePipelineState = nullptr;
    }
    
    if (m_debugGridPipelineState) {
        m_debugGridPipelineState->release();
        m_debugGridPipelineState = nullptr;
    }
    
    if (m_skyboxPipelineState) {
        m_skyboxPipelineState->release();
        m_skyboxPipelineState = nullptr;
    }
    if (m_blitPipelineState) {
        m_blitPipelineState->release();
        m_blitPipelineState = nullptr;
    }
    if (m_ssrPipelineState) {
        m_ssrPipelineState->release();
        m_ssrPipelineState = nullptr;
    }
    if (m_bloomPrefilterPipelineState) {
        m_bloomPrefilterPipelineState->release();
        m_bloomPrefilterPipelineState = nullptr;
    }
    if (m_bloomDownsamplePipelineState) {
        m_bloomDownsamplePipelineState->release();
        m_bloomDownsamplePipelineState = nullptr;
    }
    if (m_bloomUpsamplePipelineState) {
        m_bloomUpsamplePipelineState->release();
        m_bloomUpsamplePipelineState = nullptr;
    }
    if (m_bloomCombinePipelineState) {
        m_bloomCombinePipelineState->release();
        m_bloomCombinePipelineState = nullptr;
    }
    if (m_taaPipelineState) {
        m_taaPipelineState->release();
        m_taaPipelineState = nullptr;
    }
    if (m_dofPipelineState) {
        m_dofPipelineState->release();
        m_dofPipelineState = nullptr;
    }
    if (m_fogPipelineState) {
        m_fogPipelineState->release();
        m_fogPipelineState = nullptr;
    }
    if (m_motionBlurPipelineState) {
        m_motionBlurPipelineState->release();
        m_motionBlurPipelineState = nullptr;
    }
    if (m_velocityPipelineState) {
        m_velocityPipelineState->release();
        m_velocityPipelineState = nullptr;
    }
    if (m_velocityPipelineSkinned) {
        m_velocityPipelineSkinned->release();
        m_velocityPipelineSkinned = nullptr;
    }
    
    if (m_debugLibrary && m_debugLibrary != m_library) {
        m_debugLibrary->release();
        m_debugLibrary = nullptr;
    }
    
    if (!m_isInitialized) {
        return;
    }
    
    // Release pipeline states
    for (auto& pair : m_pipelineStates) {
        if (pair.second) {
            pair.second->release();
        }
    }
    m_pipelineStates.clear();
    
    // Release depth stencil state
    if (m_depthStencilState) {
        m_depthStencilState->release();
        m_depthStencilState = nullptr;
    }
    if (m_skyboxDepthState) {
        m_skyboxDepthState->release();
        m_skyboxDepthState = nullptr;
    }
    if (m_depthReadState) {
        m_depthReadState->release();
        m_depthReadState = nullptr;
    }

    storeRenderTargetState(getRenderTargetState(m_activePool));
    releaseRenderTargetState(m_sceneTargets);
    releaseRenderTargetState(m_gameTargets);
    loadRenderTargetState(getRenderTargetState(m_activePool));
    
    // Release uniform buffers
    if (m_modelUniformBuffer) {
        m_modelUniformBuffer->release();
        m_modelUniformBuffer = nullptr;
    }
    if (m_cameraUniformBuffer) {
        m_cameraUniformBuffer->release();
        m_cameraUniformBuffer = nullptr;
    }
    if (m_materialUniformBuffer) {
        m_materialUniformBuffer->release();
        m_materialUniformBuffer = nullptr;
    }
    if (m_lightUniformBuffer) {
        m_lightUniformBuffer->release();
        m_lightUniformBuffer = nullptr;
    }
    if (m_environmentUniformBuffer) {
        m_environmentUniformBuffer->release();
        m_environmentUniformBuffer = nullptr;
    }
    if (m_lightGPUBuffer) {
        m_lightGPUBuffer->release();
        m_lightGPUBuffer = nullptr;
    }
    if (m_shadowGPUBuffer) {
        m_shadowGPUBuffer->release();
        m_shadowGPUBuffer = nullptr;
    }
    if (m_lightCountBuffer) {
        m_lightCountBuffer->release();
        m_lightCountBuffer = nullptr;
    }
    if (m_clusterHeaderBuffer) {
        m_clusterHeaderBuffer->release();
        m_clusterHeaderBuffer = nullptr;
    }
    if (m_clusterIndexBuffer) {
        m_clusterIndexBuffer->release();
        m_clusterIndexBuffer = nullptr;
    }
    if (m_clusterParamsBuffer) {
        m_clusterParamsBuffer->release();
        m_clusterParamsBuffer = nullptr;
    }
    if (m_skinningBuffer) {
        m_skinningBuffer->release();
        m_skinningBuffer = nullptr;
        m_skinningBufferCapacity = 0;
    }
    if (m_prevSkinningBuffer) {
        m_prevSkinningBuffer->release();
        m_prevSkinningBuffer = nullptr;
        m_prevSkinningBufferCapacity = 0;
    }
    if (m_instanceBuffer) {
        m_instanceBuffer->release();
        m_instanceBuffer = nullptr;
        m_instanceBufferCapacity = 0;
        m_instanceBufferOffset = 0;
    }
    if (m_instanceCullBuffer) {
        m_instanceCullBuffer->release();
        m_instanceCullBuffer = nullptr;
        m_instanceCullCapacity = 0;
    }
    if (m_instanceCountBuffer) {
        m_instanceCountBuffer->release();
        m_instanceCountBuffer = nullptr;
        m_instanceCountCapacity = 0;
    }
    if (m_instanceIndirectBuffer) {
        m_instanceIndirectBuffer->release();
        m_instanceIndirectBuffer = nullptr;
        m_instanceIndirectCapacity = 0;
    }
    
    if (m_samplerState) {
        m_samplerState->release();
        m_samplerState = nullptr;
    }
    if (m_shadowSampler) {
        m_shadowSampler->release();
        m_shadowSampler = nullptr;
    }
    if (m_linearClampSampler) {
        m_linearClampSampler->release();
        m_linearClampSampler = nullptr;
    }
    if (m_pointClampSampler) {
        m_pointClampSampler->release();
        m_pointClampSampler = nullptr;
    }
    
    if (m_skyboxVertexBuffer) {
        m_skyboxVertexBuffer->release();
        m_skyboxVertexBuffer = nullptr;
    }
    if (m_skyboxIndexBuffer) {
        m_skyboxIndexBuffer->release();
        m_skyboxIndexBuffer = nullptr;
    }
    
    m_defaultWhiteTexture.reset();
    m_defaultNormalTexture.reset();
    m_defaultBlackTexture.reset();
    m_defaultHeightTexture.reset();
    m_defaultEnvironmentTexture.reset();
    m_environmentTexture.reset();
    m_colorGradingLUT.reset();
    m_colorGradingNeutralLUT.reset();
    m_colorGradingLUTPath.clear();
    m_textureLoader.reset();
    
    // Release library
    if (m_library) {
        m_library->release();
        m_library = nullptr;
    }
    
    // Release command queue
    if (m_commandQueue) {
        m_commandQueue->release();
        m_commandQueue = nullptr;
    }
    
    // Release device
    if (m_device) {
        m_device->release();
        m_device = nullptr;
    }
    
    m_isInitialized = false;
}

void* Renderer::getDevice() const {
    return m_device;
}

} // namespace Crescent
