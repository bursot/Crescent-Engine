#include "SceneCommands.hpp"
#include "SceneSerializer.hpp"
#include "../Core/Engine.hpp"
#include "../Renderer/Renderer.hpp"
#include "../Rendering/Texture.hpp"
#include "../Animation/Skeleton.hpp"
#include "../Animation/AnimationClip.hpp"
#include "../Components/SkinnedMeshRenderer.hpp"
#include "../Components/InstancedMeshRenderer.hpp"
#include "../Components/Animator.hpp"
#include "../Components/PrimitiveMesh.hpp"
#include "../Components/ModelMeshReference.hpp"
#include "../Components/HLODProxy.hpp"
#include "../Assets/AssetDatabase.hpp"
#include "../ECS/Transform.hpp"
#include "../Physics/PhysicsWorld.hpp"
#include "../Rendering/stb_image.h"
#include "../Rendering/stb_image_write.h"
#include "../Rendering/tinyexr.h"
#include <assimp/config.h>
#include <assimp/Importer.hpp>
#include <assimp/GltfMaterial.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/quaternion.h>
#include <assimp/scene.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#if __has_include("../../../ThirdParty/meshoptimizer/src/meshoptimizer.h")
#define CRESCENT_HAS_MESHOPTIMIZER 1
#include "../../../ThirdParty/meshoptimizer/src/meshoptimizer.h"
#include "../../../ThirdParty/meshoptimizer/src/simplifier.cpp"
#else
#define CRESCENT_HAS_MESHOPTIMIZER 0
#endif

#ifndef aiTextureType_GLTF_METALLIC_ROUGHNESS
#define aiTextureType_GLTF_METALLIC_ROUGHNESS aiTextureType_UNKNOWN
#endif

namespace Crescent {
namespace {

void ConfigurePrimitiveStaticLighting(MeshRenderer* renderer) {
    if (!renderer) {
        return;
    }
    MeshRenderer::StaticLightingData staticLighting = renderer->getStaticLighting();
    staticLighting.staticGeometry = true;
    staticLighting.contributeGI = true;
    staticLighting.receiveGI = true;
    renderer->setStaticLighting(staticLighting);
}

struct TextureCandidate {
    std::string baseNameLower;
    std::string normalizedName;
    std::string fullPath;
    std::vector<std::string> tokens;
};

struct ImportContext {
    Scene* scene = nullptr;
    const aiScene* aiScene = nullptr;
    TextureLoader* textureLoader = nullptr;
    std::string baseDir;
    std::string sourcePath;
    SceneCommands::ModelImportOptions options;
    std::vector<std::shared_ptr<Mesh>> meshes;
    std::vector<std::shared_ptr<Material>> materials;
    std::shared_ptr<Skeleton> skeleton;
    std::vector<std::shared_ptr<AnimationClip>> animations;
    std::vector<TextureCandidate> textureCandidates;
    bool textureIndexBuilt = false;
};

struct MeshCacheEntry {
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Material> material;
};

struct HLODBucketBuildResult {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

static HLODBucketBuildResult BuildCompactHLODBucket(const std::vector<Vertex>& sourceVertices,
                                                    const std::vector<uint32_t>& sourceIndices) {
    HLODBucketBuildResult result;
    if (sourceVertices.empty() || sourceIndices.empty()) {
        return result;
    }

    std::unordered_map<uint32_t, uint32_t> remap;
    remap.reserve(sourceIndices.size());
    result.indices.reserve(sourceIndices.size());

    for (uint32_t index : sourceIndices) {
        if (index >= sourceVertices.size()) {
            continue;
        }
        auto [it, inserted] = remap.emplace(index, static_cast<uint32_t>(result.vertices.size()));
        if (inserted) {
            result.vertices.push_back(sourceVertices[index]);
        }
        result.indices.push_back(it->second);
    }

    return result;
}

#if CRESCENT_HAS_MESHOPTIMIZER
static size_t RoundIndexCountToTriangleMultiple(size_t value) {
    return (value / 3) * 3;
}

static float ComputeHLODRatio(size_t triangleCount, size_t sourceCount) {
    float ratio = 0.5f;
    if (triangleCount >= 512) ratio = 0.42f;
    if (triangleCount >= 2048) ratio = 0.3f;
    if (triangleCount >= 8192) ratio = 0.2f;
    if (sourceCount >= 8) ratio *= 0.9f;
    if (sourceCount >= 16) ratio *= 0.85f;
    return std::max(0.12f, std::min(0.85f, ratio));
}

static bool SimplifyHLODBucketInPlace(HLODBucketBuildResult& bucket, size_t sourceCount) {
    if (bucket.vertices.empty() || bucket.indices.size() < 192 || (bucket.indices.size() % 3) != 0) {
        return false;
    }

    const size_t triangleCount = bucket.indices.size() / 3;
    const float ratio = ComputeHLODRatio(triangleCount, sourceCount);
    const size_t targetIndexCount = std::max<size_t>(96, RoundIndexCountToTriangleMultiple(static_cast<size_t>(std::round(bucket.indices.size() * ratio))));
    if (targetIndexCount >= bucket.indices.size() || targetIndexCount < 3) {
        return false;
    }

    std::vector<float> attributes;
    attributes.reserve(bucket.vertices.size() * 17);
    for (const Vertex& v : bucket.vertices) {
        attributes.push_back(v.normal.x);
        attributes.push_back(v.normal.y);
        attributes.push_back(v.normal.z);
        attributes.push_back(v.texCoord.x);
        attributes.push_back(v.texCoord.y);
        attributes.push_back(v.texCoord1.x);
        attributes.push_back(v.texCoord1.y);
        attributes.push_back(v.tangent.x);
        attributes.push_back(v.tangent.y);
        attributes.push_back(v.tangent.z);
        attributes.push_back(v.bitangent.x);
        attributes.push_back(v.bitangent.y);
        attributes.push_back(v.bitangent.z);
        attributes.push_back(v.color.x);
        attributes.push_back(v.color.y);
        attributes.push_back(v.color.z);
        attributes.push_back(v.color.w);
    }

    constexpr float kWeights[17] = {
        0.35f, 0.35f, 0.35f,
        0.75f, 0.75f,
        0.35f, 0.35f,
        0.2f, 0.2f, 0.2f,
        0.1f, 0.1f, 0.1f,
        0.05f, 0.05f, 0.05f, 0.05f
    };

    std::vector<uint32_t> simplified(bucket.indices.size());
    float resultError = 0.0f;
    size_t simplifiedCount = meshopt_simplifyWithAttributes(simplified.data(),
                                                            bucket.indices.data(),
                                                            bucket.indices.size(),
                                                            &bucket.vertices[0].position.x,
                                                            bucket.vertices.size(),
                                                            sizeof(Vertex),
                                                            attributes.data(),
                                                            sizeof(float) * 17,
                                                            kWeights,
                                                            17,
                                                            nullptr,
                                                            targetIndexCount,
                                                            0.02f,
                                                            meshopt_SimplifyLockBorder,
                                                            &resultError);
    if (simplifiedCount == 0 || simplifiedCount >= bucket.indices.size()) {
        return false;
    }

    simplified.resize(RoundIndexCountToTriangleMultiple(simplifiedCount));
    if (simplified.size() < 3) {
        return false;
    }

    std::vector<uint32_t> cacheOptimized(simplified.size());
    meshopt_optimizeVertexCache(cacheOptimized.data(), simplified.data(), simplified.size(), bucket.vertices.size());

    std::vector<uint32_t> fetchRemap(bucket.vertices.size());
    size_t compactVertexCount = meshopt_optimizeVertexFetchRemap(fetchRemap.data(),
                                                                 cacheOptimized.data(),
                                                                 cacheOptimized.size(),
                                                                 bucket.vertices.size());
    if (compactVertexCount == 0) {
        return false;
    }

    std::vector<Vertex> compactVertices(compactVertexCount);
    meshopt_remapVertexBuffer(compactVertices.data(),
                              bucket.vertices.data(),
                              bucket.vertices.size(),
                              sizeof(Vertex),
                              fetchRemap.data());

    std::vector<uint32_t> compactIndices(cacheOptimized.size());
    meshopt_remapIndexBuffer(compactIndices.data(),
                             cacheOptimized.data(),
                             cacheOptimized.size(),
                             fetchRemap.data());

    bucket.vertices = std::move(compactVertices);
    bucket.indices = std::move(compactIndices);
    return true;
}
#endif

struct ModelCacheEntry {
    std::unordered_map<int, MeshCacheEntry> meshesByIndex;
    std::unordered_map<int, MeshCacheEntry> mergedByMaterial;
    std::shared_ptr<Skeleton> skeleton;
    std::vector<std::shared_ptr<AnimationClip>> animations;
};

struct BakedDirectLight {
    Light::Type type = Light::Type::Directional;
    Light::Mobility mobility = Light::Mobility::Movable;
    Math::Vector3 positionWS = Math::Vector3::Zero;
    Math::Vector3 directionWS = Math::Vector3::Down;
    Math::Vector3 color = Math::Vector3::One;
    float intensity = 0.0f;
    float range = 0.0f;
    float cosInner = 1.0f;
    float cosOuter = 1.0f;
    float sourceRadius = 0.0f;
    bool castShadows = true;
    float shadowBias = 0.0005f;
    float shadowNormalBias = 0.001f;
    float shadowDistance = 200.0f;
    int shadowmaskChannel = -1;
};

struct SurfaceSample {
    bool valid = false;
    Math::Vector3 positionWS = Math::Vector3::Zero;
    Math::Vector3 normalWS = Math::Vector3::Up;
    Math::Vector2 uv = Math::Vector2::Zero;
    int materialIndex = 0;
    float distanceSq = std::numeric_limits<float>::max();
};

struct EmissiveTriangleSurface {
    Entity* entity = nullptr;
    MeshRenderer* renderer = nullptr;
    Math::Vector3 p0 = Math::Vector3::Zero;
    Math::Vector3 p1 = Math::Vector3::Zero;
    Math::Vector3 p2 = Math::Vector3::Zero;
    Math::Vector3 n0 = Math::Vector3::Up;
    Math::Vector3 n1 = Math::Vector3::Up;
    Math::Vector3 n2 = Math::Vector3::Up;
    Math::Vector2 uv0 = Math::Vector2::Zero;
    Math::Vector2 uv1 = Math::Vector2::Zero;
    Math::Vector2 uv2 = Math::Vector2::Zero;
    int materialIndex = 0;
    bool twoSided = false;
    float area = 0.0f;
    float weight = 0.0f;
    float cumulativeWeight = 0.0f;
};

struct EmissiveLightingEstimate {
    Math::Vector3 irradiance = Math::Vector3::Zero;
    Math::Vector3 weightedDirection = Math::Vector3::Zero;
    float directionWeight = 0.0f;
    float referenceNoL = 0.0f;
};

struct EmissiveSurfaceSample {
    const EmissiveTriangleSurface* surface = nullptr;
    Math::Vector3 positionWS = Math::Vector3::Zero;
    Math::Vector3 normalWS = Math::Vector3::Up;
    Math::Vector2 uv = Math::Vector2::Zero;
    Math::Vector3 emission = Math::Vector3::Zero;
    float pdfArea = 0.0f;
    bool twoSided = false;
};

struct StaticLightingLayoutCandidate {
    Entity* entity = nullptr;
    MeshRenderer* renderer = nullptr;
    std::shared_ptr<Mesh> mesh;
    float surfaceAreaWS = 0.0f;
    int requestedInnerResolution = 0;
    bool generatedFallbackUVs = false;
};

struct StaticLightingBakeCandidate {
    Entity* entity = nullptr;
    MeshRenderer* renderer = nullptr;
    std::shared_ptr<Mesh> mesh;
    Math::Matrix4x4 worldMatrix = Math::Matrix4x4::Identity;
    Math::Matrix4x4 normalMatrix = Math::Matrix4x4::Identity;
    MeshRenderer::StaticLightingData staticLighting;
};

struct ShelfAtlasState {
    int atlasIndex = 0;
    int width = 0;
    int height = 0;
    int cursorX = 0;
    int cursorY = 0;
    int rowHeight = 0;
};

struct LightmapChart {
    int axis = 2;
    int axisSign = 1;
    std::vector<uint32_t> triangleIndices;
    std::unordered_map<uint32_t, Math::Vector2> projectedUVs;
    Math::Vector2 boundsMin = Math::Vector2::Zero;
    Math::Vector2 boundsMax = Math::Vector2::One;
    Math::Vector2 packedOffset = Math::Vector2::Zero;
};

static std::shared_ptr<Mesh> CloneMeshGeometry(const std::shared_ptr<Mesh>& source) {
    if (!source) {
        return nullptr;
    }
    auto clone = std::make_shared<Mesh>();
    clone->setName(source->getName());
    clone->setVertices(source->getVertices());
    clone->setIndices(source->getIndices());
    clone->setSubmeshes(source->getSubmeshes());
    clone->setSkinWeights(source->getSkinWeights());
    clone->setDoubleSided(source->isDoubleSided());
    return clone;
}

static Math::Vector3 ClampColor(const Math::Vector3& value, float maxValue) {
    return Math::Vector3(
        Math::Clamp(value.x, 0.0f, maxValue),
        Math::Clamp(value.y, 0.0f, maxValue),
        Math::Clamp(value.z, 0.0f, maxValue)
    );
}

static Math::Vector3 MultiplyColor(const Math::Vector3& a, const Math::Vector3& b) {
    return Math::Vector3(a.x * b.x, a.y * b.y, a.z * b.z);
}

static Math::Vector3 SRGBToLinear(const Math::Vector3& color) {
    return Math::Vector3(
        std::pow(Math::Clamp(color.x, 0.0f, 1.0f), 2.2f),
        std::pow(Math::Clamp(color.y, 0.0f, 1.0f), 2.2f),
        std::pow(Math::Clamp(color.z, 0.0f, 1.0f), 2.2f)
    );
}

static bool WriteEXRImage(const std::string& path,
                          int width,
                          int height,
                          const std::vector<float>& rgbaPixels,
                          bool saveAsHalf = true) {
    if (path.empty() || width <= 0 || height <= 0) {
        return false;
    }
    if (rgbaPixels.size() != static_cast<size_t>(width) * static_cast<size_t>(height) * 4u) {
        return false;
    }
    const char* err = nullptr;
    int result = SaveEXR(rgbaPixels.data(), width, height, 4, saveAsHalf ? 1 : 0, path.c_str(), &err);
    if (result != TINYEXR_SUCCESS) {
        std::cerr << "[StaticLighting] Failed to write EXR: " << path;
        if (err) {
            std::cerr << " reason: " << err;
            FreeEXRErrorMessage(err);
        }
        std::cerr << std::endl;
        return false;
    }
    if (err) {
        FreeEXRErrorMessage(err);
    }
    return true;
}

static uint32_t HashUint(uint32_t value) {
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

static float HashToUnitFloat(uint32_t value) {
    return static_cast<float>(HashUint(value) & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

static Math::Vector2 Random2D(uint32_t seedA, uint32_t seedB) {
    return Math::Vector2(HashToUnitFloat(seedA), HashToUnitFloat(seedB));
}

struct CPUImageCacheEntry {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;
};

static const CPUImageCacheEntry* LoadCPUImage(const std::string& path) {
    static std::unordered_map<std::string, CPUImageCacheEntry> cache;
    auto it = cache.find(path);
    if (it != cache.end()) {
        return it->second.rgba.empty() ? nullptr : &it->second;
    }

    CPUImageCacheEntry entry;
    if (!path.empty() && path.rfind("builtin://", 0) != 0) {
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (pixels && width > 0 && height > 0) {
            entry.width = width;
            entry.height = height;
            entry.rgba.assign(pixels, pixels + static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
        }
        if (pixels) {
            stbi_image_free(pixels);
        }
    }

    auto [insertedIt, _] = cache.emplace(path, std::move(entry));
    return insertedIt->second.rgba.empty() ? nullptr : &insertedIt->second;
}

static Math::Vector3 SampleCPUImageRGB(const CPUImageCacheEntry* image,
                                       const Math::Vector2& uv,
                                       bool srgb) {
    if (!image || image->width <= 0 || image->height <= 0 || image->rgba.empty()) {
        return Math::Vector3::One;
    }

    float u = uv.x - std::floor(uv.x);
    float v = uv.y - std::floor(uv.y);
    float x = u * static_cast<float>(image->width - 1);
    float y = (1.0f - v) * static_cast<float>(image->height - 1);
    int x0 = std::max(0, std::min(image->width - 1, static_cast<int>(std::floor(x))));
    int y0 = std::max(0, std::min(image->height - 1, static_cast<int>(std::floor(y))));
    int x1 = std::max(0, std::min(image->width - 1, x0 + 1));
    int y1 = std::max(0, std::min(image->height - 1, y0 + 1));
    float tx = x - static_cast<float>(x0);
    float ty = y - static_cast<float>(y0);

    auto readPixel = [&](int px, int py) {
        size_t offset = (static_cast<size_t>(py) * static_cast<size_t>(image->width) + static_cast<size_t>(px)) * 4u;
        Math::Vector3 color(
            static_cast<float>(image->rgba[offset + 0]) / 255.0f,
            static_cast<float>(image->rgba[offset + 1]) / 255.0f,
            static_cast<float>(image->rgba[offset + 2]) / 255.0f
        );
        return srgb ? SRGBToLinear(color) : color;
    };

    Math::Vector3 c00 = readPixel(x0, y0);
    Math::Vector3 c10 = readPixel(x1, y0);
    Math::Vector3 c01 = readPixel(x0, y1);
    Math::Vector3 c11 = readPixel(x1, y1);
    Math::Vector3 c0 = c00 * (1.0f - tx) + c10 * tx;
    Math::Vector3 c1 = c01 * (1.0f - tx) + c11 * tx;
    return c0 * (1.0f - ty) + c1 * ty;
}

static Math::Vector2 TransformMaterialUV(const Material& material, const Math::Vector2& uv) {
    Math::Vector2 tiledUV(uv.x * material.getUVTiling().x, uv.y * material.getUVTiling().y);
    return tiledUV + material.getUVOffset();
}

static float ComputeLuminance(const Math::Vector3& color) {
    return color.x * 0.2126f + color.y * 0.7152f + color.z * 0.0722f;
}

static float TriangleArea(const Math::Vector3& a, const Math::Vector3& b, const Math::Vector3& c) {
    return 0.5f * (b - a).cross(c - a).length();
}

static float TriangleUVArea(const Math::Vector2& a, const Math::Vector2& b, const Math::Vector2& c) {
    Math::Vector2 ab = b - a;
    Math::Vector2 ac = c - a;
    return std::abs(ab.x * ac.y - ab.y * ac.x) * 0.5f;
}

static bool HasUsableLightmapUVs(const Mesh& mesh) {
    const auto& vertices = mesh.getVertices();
    const auto& indices = mesh.getIndices();
    if (vertices.empty() || indices.size() < 3) {
        return false;
    }

    Math::Vector2 uvMin(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    Math::Vector2 uvMax(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());
    float accumulatedUVArea = 0.0f;

    for (const Vertex& vertex : vertices) {
        uvMin = Math::Vector2::Min(uvMin, vertex.texCoord1);
        uvMax = Math::Vector2::Max(uvMax, vertex.texCoord1);
    }

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
            continue;
        }
        accumulatedUVArea += TriangleUVArea(vertices[i0].texCoord1, vertices[i1].texCoord1, vertices[i2].texCoord1);
    }

    Math::Vector2 uvExtent = uvMax - uvMin;
    if (uvExtent.lengthSquared() <= 1e-8f || accumulatedUVArea <= 1e-7f) {
        return false;
    }

    // Lightmap UVs must not stack large portions of the mesh on top of each other.
    // Primitive/material UVs often reuse the full 0..1 tile per face, which is fine
    // for albedo tiling but invalid for lightmaps. If the summed triangle UV area is
    // much larger than the occupied UV bounds, treat the channel as overlapped and
    // force a dedicated unwrap.
    float occupiedBoundsArea = std::max(uvExtent.x * uvExtent.y, 1e-6f);
    float overlapRatio = accumulatedUVArea / occupiedBoundsArea;
    if (overlapRatio > 1.25f) {
        return false;
    }

    return true;
}

static std::shared_ptr<Mesh> CreateNormalizedLightmapMesh(const std::shared_ptr<Mesh>& source) {
    if (!source) {
        return nullptr;
    }

    const auto& sourceVertices = source->getVertices();
    if (sourceVertices.empty()) {
        return source;
    }

    Math::Vector2 uvMin(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    Math::Vector2 uvMax(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());
    for (const Vertex& vertex : sourceVertices) {
        uvMin = Math::Vector2::Min(uvMin, vertex.texCoord1);
        uvMax = Math::Vector2::Max(uvMax, vertex.texCoord1);
    }

    Math::Vector2 uvExtent = uvMax - uvMin;
    if (uvExtent.x <= 1e-6f || uvExtent.y <= 1e-6f) {
        return source;
    }

    bool alreadyNormalized = uvMin.x >= -0.001f && uvMin.y >= -0.001f &&
                             uvMax.x <= 1.001f && uvMax.y <= 1.001f;
    if (alreadyNormalized) {
        return source;
    }

    auto clone = CloneMeshGeometry(source);
    auto vertices = clone->getVertices();
    for (auto& vertex : vertices) {
        vertex.texCoord1.x = (vertex.texCoord1.x - uvMin.x) / uvExtent.x;
        vertex.texCoord1.y = (vertex.texCoord1.y - uvMin.y) / uvExtent.y;
    }
    clone->setVertices(vertices);
    return clone;
}

static uint64_t MakeEdgeKey(uint32_t a, uint32_t b) {
    uint32_t lo = std::min(a, b);
    uint32_t hi = std::max(a, b);
    return (static_cast<uint64_t>(lo) << 32) | static_cast<uint64_t>(hi);
}

static int DominantProjectionAxis(const Math::Vector3& normal) {
    Math::Vector3 absN(std::abs(normal.x), std::abs(normal.y), std::abs(normal.z));
    if (absN.x >= absN.y && absN.x >= absN.z) {
        return 0;
    }
    if (absN.y >= absN.z) {
        return 1;
    }
    return 2;
}

static int DominantProjectionSign(const Math::Vector3& normal, int axis) {
    switch (axis) {
        case 0: return normal.x >= 0.0f ? 1 : -1;
        case 1: return normal.y >= 0.0f ? 1 : -1;
        default: return normal.z >= 0.0f ? 1 : -1;
    }
}

static Math::Vector2 ProjectLightmapUV(const Math::Vector3& position, int axis, int sign) {
    switch (axis) {
        case 0:
            return sign >= 0 ? Math::Vector2(position.z, position.y) : Math::Vector2(-position.z, position.y);
        case 1:
            return sign >= 0 ? Math::Vector2(position.x, position.z) : Math::Vector2(position.x, -position.z);
        default:
            return sign >= 0 ? Math::Vector2(position.x, position.y) : Math::Vector2(-position.x, position.y);
    }
}

static std::shared_ptr<Mesh> CreateChartedLightmapMesh(const std::shared_ptr<Mesh>& source) {
    if (!source) {
        return nullptr;
    }

    const auto& sourceVertices = source->getVertices();
    const auto& sourceIndices = source->getIndices();
    const auto& sourceSubmeshes = source->getSubmeshes();
    if (sourceVertices.empty() || sourceIndices.size() < 3) {
        return source;
    }

    const size_t triangleCount = sourceIndices.size() / 3;
    std::vector<Math::Vector3> triangleNormals(triangleCount, Math::Vector3::Up);
    std::vector<int> triangleAxes(triangleCount, 2);
    std::vector<int> triangleSigns(triangleCount, 1);
    std::unordered_map<uint64_t, std::vector<uint32_t>> edgeToTriangles;
    edgeToTriangles.reserve(triangleCount * 3);

    for (size_t tri = 0; tri < triangleCount; ++tri) {
        uint32_t i0 = sourceIndices[tri * 3 + 0];
        uint32_t i1 = sourceIndices[tri * 3 + 1];
        uint32_t i2 = sourceIndices[tri * 3 + 2];
        if (i0 >= sourceVertices.size() || i1 >= sourceVertices.size() || i2 >= sourceVertices.size()) {
            continue;
        }

        const Math::Vector3& p0 = sourceVertices[i0].position;
        const Math::Vector3& p1 = sourceVertices[i1].position;
        const Math::Vector3& p2 = sourceVertices[i2].position;
        Math::Vector3 faceNormal = (p1 - p0).cross(p2 - p0);
        if (faceNormal.lengthSquared() <= Math::EPSILON) {
            faceNormal = sourceVertices[i0].normal + sourceVertices[i1].normal + sourceVertices[i2].normal;
        }
        if (faceNormal.lengthSquared() <= Math::EPSILON) {
            faceNormal = Math::Vector3::Up;
        }
        faceNormal.normalize();

        triangleNormals[tri] = faceNormal;
        triangleAxes[tri] = DominantProjectionAxis(faceNormal);
        triangleSigns[tri] = DominantProjectionSign(faceNormal, triangleAxes[tri]);

        edgeToTriangles[MakeEdgeKey(i0, i1)].push_back(static_cast<uint32_t>(tri));
        edgeToTriangles[MakeEdgeKey(i1, i2)].push_back(static_cast<uint32_t>(tri));
        edgeToTriangles[MakeEdgeKey(i2, i0)].push_back(static_cast<uint32_t>(tri));
    }

    std::vector<int> triangleChart(triangleCount, -1);
    std::vector<LightmapChart> charts;
    charts.reserve(triangleCount);
    constexpr float kChartNormalDotThreshold = 0.9063f; // ~25 degrees

    for (size_t tri = 0; tri < triangleCount; ++tri) {
        if (triangleChart[tri] >= 0) {
            continue;
        }

        LightmapChart chart;
        chart.axis = triangleAxes[tri];
        chart.axisSign = triangleSigns[tri];

        std::vector<uint32_t> stack = {static_cast<uint32_t>(tri)};
        triangleChart[tri] = static_cast<int>(charts.size());

        while (!stack.empty()) {
            uint32_t current = stack.back();
            stack.pop_back();
            chart.triangleIndices.push_back(current);

            uint32_t baseIndex = current * 3;
            uint32_t triVerts[3] = {
                sourceIndices[baseIndex + 0],
                sourceIndices[baseIndex + 1],
                sourceIndices[baseIndex + 2]
            };

            for (int edge = 0; edge < 3; ++edge) {
                uint64_t edgeKey = MakeEdgeKey(triVerts[edge], triVerts[(edge + 1) % 3]);
                auto edgeIt = edgeToTriangles.find(edgeKey);
                if (edgeIt == edgeToTriangles.end()) {
                    continue;
                }
                for (uint32_t neighbor : edgeIt->second) {
                    if (triangleChart[neighbor] >= 0) {
                        continue;
                    }
                    if (triangleAxes[neighbor] != chart.axis || triangleSigns[neighbor] != chart.axisSign) {
                        continue;
                    }
                    if (triangleNormals[current].dot(triangleNormals[neighbor]) < kChartNormalDotThreshold) {
                        continue;
                    }
                    triangleChart[neighbor] = static_cast<int>(charts.size());
                    stack.push_back(neighbor);
                }
            }
        }

        Math::Vector2 chartMin(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
        Math::Vector2 chartMax(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());
        for (uint32_t triIndex : chart.triangleIndices) {
            uint32_t baseIndex = triIndex * 3;
            for (int corner = 0; corner < 3; ++corner) {
                uint32_t srcIndex = sourceIndices[baseIndex + corner];
                auto projectedIt = chart.projectedUVs.find(srcIndex);
                if (projectedIt == chart.projectedUVs.end()) {
                    Math::Vector2 projected = ProjectLightmapUV(sourceVertices[srcIndex].position, chart.axis, chart.axisSign);
                    projectedIt = chart.projectedUVs.emplace(srcIndex, projected).first;
                }
                chartMin = Math::Vector2::Min(chartMin, projectedIt->second);
                chartMax = Math::Vector2::Max(chartMax, projectedIt->second);
            }
        }
        chart.boundsMin = chartMin;
        chart.boundsMax = chartMax;
        charts.push_back(std::move(chart));
    }

    std::vector<size_t> chartOrder(charts.size());
    for (size_t i = 0; i < charts.size(); ++i) {
        chartOrder[i] = i;
    }
    std::sort(chartOrder.begin(), chartOrder.end(), [&](size_t a, size_t b) {
        Math::Vector2 sizeA = charts[a].boundsMax - charts[a].boundsMin;
        Math::Vector2 sizeB = charts[b].boundsMax - charts[b].boundsMin;
        float areaA = sizeA.x * sizeA.y;
        float areaB = sizeB.x * sizeB.y;
        return areaA > areaB;
    });

    float packedCursorX = 0.0f;
    float packedCursorY = 0.0f;
    float packedRowHeight = 0.0f;
    float packedWidthLimit = 0.0f;
    for (size_t chartIndex : chartOrder) {
        const auto& chart = charts[chartIndex];
        Math::Vector2 size = chart.boundsMax - chart.boundsMin;
        packedWidthLimit += std::max(size.x, 0.001f) * std::max(size.y, 0.001f);
    }
    packedWidthLimit = std::max(std::sqrt(std::max(packedWidthLimit, 0.0001f)), 1.0f);

    float packedPageWidth = 0.0f;
    float packedPageHeight = 0.0f;
    for (size_t chartIndex : chartOrder) {
        auto& chart = charts[chartIndex];
        Math::Vector2 size = chart.boundsMax - chart.boundsMin;
        size.x = std::max(size.x, 0.001f);
        size.y = std::max(size.y, 0.001f);
        float chartPadding = std::max(std::max(size.x, size.y) * 0.05f, 0.01f);
        if (packedCursorX > 0.0f && packedCursorX + size.x + chartPadding > packedWidthLimit) {
            packedCursorX = 0.0f;
            packedCursorY += packedRowHeight;
            packedRowHeight = 0.0f;
        }
        chart.packedOffset = Math::Vector2(packedCursorX + chartPadding * 0.5f,
                                           packedCursorY + chartPadding * 0.5f);
        packedCursorX += size.x + chartPadding;
        packedRowHeight = std::max(packedRowHeight, size.y + chartPadding);
        packedPageWidth = std::max(packedPageWidth, packedCursorX);
        packedPageHeight = std::max(packedPageHeight, packedCursorY + packedRowHeight);
    }

    packedPageWidth = std::max(packedPageWidth, 1.0f);
    packedPageHeight = std::max(packedPageHeight, 1.0f);

    auto clone = std::make_shared<Mesh>();
    clone->setName(source->getName());
    clone->setDoubleSided(source->isDoubleSided());

    std::vector<Vertex> newVertices;
    std::vector<uint32_t> newIndices;
    std::vector<Submesh> newSubmeshes;
    std::unordered_map<uint64_t, uint32_t> remappedVertices;
    newVertices.reserve(sourceVertices.size() + triangleCount * 2);
    newIndices.reserve(sourceIndices.size());
    const std::vector<Submesh> iterationSubmeshes = sourceSubmeshes.empty()
        ? std::vector<Submesh>{Submesh(0, static_cast<uint32_t>(sourceIndices.size()), 0)}
        : sourceSubmeshes;
    newSubmeshes.reserve(iterationSubmeshes.size());

    for (const Submesh& submesh : iterationSubmeshes) {
        uint32_t submeshStart = static_cast<uint32_t>(newIndices.size());
        const uint32_t submeshEnd = submesh.indexStart + submesh.indexCount;
        for (uint32_t indexOffset = submesh.indexStart; indexOffset + 2 < submeshEnd; indexOffset += 3) {
            uint32_t triIndex = indexOffset / 3;
            if (triIndex >= triangleChart.size() || triangleChart[triIndex] < 0) {
                continue;
            }

            const LightmapChart& chart = charts[triangleChart[triIndex]];

            for (int corner = 0; corner < 3; ++corner) {
                uint32_t srcIndex = sourceIndices[indexOffset + corner];
                uint64_t remapKey = (static_cast<uint64_t>(triangleChart[triIndex]) << 32) | static_cast<uint64_t>(srcIndex);
                auto remapIt = remappedVertices.find(remapKey);
                if (remapIt == remappedVertices.end()) {
                    Vertex vertex = sourceVertices[srcIndex];
                    Math::Vector2 projected = chart.projectedUVs.at(srcIndex);
                    Math::Vector2 local = projected - chart.boundsMin + chart.packedOffset;
                    vertex.texCoord1 = Math::Vector2(local.x / packedPageWidth, local.y / packedPageHeight);
                    uint32_t newIndex = static_cast<uint32_t>(newVertices.size());
                    newVertices.push_back(vertex);
                    remappedVertices.emplace(remapKey, newIndex);
                    newIndices.push_back(newIndex);
                } else {
                    newIndices.push_back(remapIt->second);
                }
            }
        }
        newSubmeshes.emplace_back(submeshStart,
                                  static_cast<uint32_t>(newIndices.size()) - submeshStart,
                                  submesh.materialIndex);
    }

    if (newSubmeshes.empty() && !newIndices.empty()) {
        newSubmeshes.emplace_back(0, static_cast<uint32_t>(newIndices.size()), 0);
    }

    clone->setVertices(newVertices);
    clone->setIndices(newIndices);
    clone->setSubmeshes(newSubmeshes);
    clone->setSkinWeights(source->getSkinWeights());
    return clone;
}

static std::shared_ptr<Mesh> CreateFallbackLightmapMesh(const std::shared_ptr<Mesh>& source) {
    return CreateChartedLightmapMesh(source);
}

static float ComputeWorldSurfaceArea(const Mesh& mesh, const Transform& transform) {
    const auto& vertices = mesh.getVertices();
    const auto& indices = mesh.getIndices();
    if (vertices.empty() || indices.size() < 3) {
        return 0.0f;
    }

    Math::Matrix4x4 world = transform.getWorldMatrix();
    float area = 0.0f;
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
            continue;
        }
        Math::Vector3 p0 = world.transformPoint(vertices[i0].position);
        Math::Vector3 p1 = world.transformPoint(vertices[i1].position);
        Math::Vector3 p2 = world.transformPoint(vertices[i2].position);
        area += TriangleArea(p0, p1, p2);
    }
    return area;
}

static int RoundUpToMultiple(int value, int multiple) {
    if (multiple <= 1) {
        return value;
    }
    return ((value + multiple - 1) / multiple) * multiple;
}

static int EstimateLightmapInnerResolution(float surfaceAreaWS, const SceneStaticLightingSettings& settings) {
    float texels = std::sqrt(std::max(surfaceAreaWS, 0.0001f)) * std::max(settings.texelsPerUnit, 1.0f);
    int resolution = static_cast<int>(std::ceil(texels));
    resolution = std::max(32, RoundUpToMultiple(resolution, 4));
    int maxInner = std::max(32, settings.atlasSize - settings.unwrapPadding * 2);
    return std::min(resolution, maxInner);
}

static bool TryPackShelfRect(ShelfAtlasState& atlas, int rectWidth, int rectHeight, int& outX, int& outY) {
    if (rectWidth > atlas.width || rectHeight > atlas.height) {
        return false;
    }

    if (atlas.cursorX + rectWidth > atlas.width) {
        atlas.cursorX = 0;
        atlas.cursorY += atlas.rowHeight;
        atlas.rowHeight = 0;
    }
    if (atlas.cursorY + rectHeight > atlas.height) {
        return false;
    }

    outX = atlas.cursorX;
    outY = atlas.cursorY;
    atlas.cursorX += rectWidth;
    atlas.rowHeight = std::max(atlas.rowHeight, rectHeight);
    return true;
}

static bool CanPackStaticLightingCandidates(const std::vector<StaticLightingLayoutCandidate>& candidates,
                                            int atlasSize,
                                            int padding,
                                            int maxAtlasCount,
                                            float resolutionScale) {
    std::vector<ShelfAtlasState> atlases;
    atlases.reserve(std::max(maxAtlasCount, 1));

    for (const auto& candidate : candidates) {
        int scaledInnerResolution = static_cast<int>(std::round(static_cast<float>(candidate.requestedInnerResolution) * resolutionScale));
        int innerResolution = std::min(std::max(1, scaledInnerResolution), atlasSize - padding * 2);
        int slotResolution = std::min(atlasSize, innerResolution + padding * 2);
        if (slotResolution <= padding * 2) {
            return false;
        }

        int packedX = 0;
        int packedY = 0;
        bool packed = false;
        for (auto& atlas : atlases) {
            if (TryPackShelfRect(atlas, slotResolution, slotResolution, packedX, packedY)) {
                packed = true;
                break;
            }
        }

        if (packed) {
            continue;
        }

        if (static_cast<int>(atlases.size()) >= std::max(maxAtlasCount, 1)) {
            return false;
        }

        ShelfAtlasState atlas;
        atlas.atlasIndex = static_cast<int>(atlases.size());
        atlas.width = atlasSize;
        atlas.height = atlasSize;
        if (!TryPackShelfRect(atlas, slotResolution, slotResolution, packedX, packedY)) {
            return false;
        }
        atlases.push_back(atlas);
    }

    return true;
}

static float ResolveStaticLightingResolutionScale(const std::vector<StaticLightingLayoutCandidate>& candidates,
                                                  int atlasSize,
                                                  int padding,
                                                  int maxAtlasCount) {
    if (candidates.empty()) {
        return 1.0f;
    }

    constexpr float kMaxResolutionBoost = 4.0f;
    float low = 1.0f;
    float high = kMaxResolutionBoost;

    if (!CanPackStaticLightingCandidates(candidates, atlasSize, padding, maxAtlasCount, low)) {
        return 1.0f;
    }
    if (CanPackStaticLightingCandidates(candidates, atlasSize, padding, maxAtlasCount, high)) {
        return high;
    }

    for (int iteration = 0; iteration < 12; ++iteration) {
        float mid = (low + high) * 0.5f;
        if (CanPackStaticLightingCandidates(candidates, atlasSize, padding, maxAtlasCount, mid)) {
            low = mid;
        } else {
            high = mid;
        }
    }

    return low;
}

static Math::Vector2 TransformLightmapUVToAtlas(const Math::Vector2& uv, const Math::Vector4& scaleOffset) {
    return Math::Vector2(
        uv.x * scaleOffset.x + scaleOffset.z,
        uv.y * scaleOffset.y + scaleOffset.w
    );
}

static bool ComputeBarycentrics(const Math::Vector2& p,
                                const Math::Vector2& a,
                                const Math::Vector2& b,
                                const Math::Vector2& c,
                                Math::Vector3& outBary) {
    Math::Vector2 v0 = b - a;
    Math::Vector2 v1 = c - a;
    Math::Vector2 v2 = p - a;

    float denom = v0.x * v1.y - v1.x * v0.y;
    if (std::abs(denom) <= 1e-10f) {
        return false;
    }

    float invDenom = 1.0f / denom;
    float v = (v2.x * v1.y - v1.x * v2.y) * invDenom;
    float w = (v0.x * v2.y - v2.x * v0.y) * invDenom;
    float u = 1.0f - v - w;
    outBary = Math::Vector3(u, v, w);
    constexpr float kEpsilon = -0.0001f;
    return u >= kEpsilon && v >= kEpsilon && w >= kEpsilon;
}

static Math::Vector3 ClosestPointOnTriangle(const Math::Vector3& p,
                                            const Math::Vector3& a,
                                            const Math::Vector3& b,
                                            const Math::Vector3& c,
                                            Math::Vector3& outBary) {
    Math::Vector3 ab = b - a;
    Math::Vector3 ac = c - a;
    Math::Vector3 ap = p - a;
    float d1 = ab.dot(ap);
    float d2 = ac.dot(ap);
    if (d1 <= 0.0f && d2 <= 0.0f) {
        outBary = Math::Vector3(1.0f, 0.0f, 0.0f);
        return a;
    }

    Math::Vector3 bp = p - b;
    float d3 = ab.dot(bp);
    float d4 = ac.dot(bp);
    if (d3 >= 0.0f && d4 <= d3) {
        outBary = Math::Vector3(0.0f, 1.0f, 0.0f);
        return b;
    }

    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        float v = d1 / std::max(d1 - d3, Math::EPSILON);
        outBary = Math::Vector3(1.0f - v, v, 0.0f);
        return a + ab * v;
    }

    Math::Vector3 cp = p - c;
    float d5 = ab.dot(cp);
    float d6 = ac.dot(cp);
    if (d6 >= 0.0f && d5 <= d6) {
        outBary = Math::Vector3(0.0f, 0.0f, 1.0f);
        return c;
    }

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        float w = d2 / std::max(d2 - d6, Math::EPSILON);
        outBary = Math::Vector3(1.0f - w, 0.0f, w);
        return a + ac * w;
    }

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        float denom = std::max((d4 - d3) + (d5 - d6), Math::EPSILON);
        float w = (d4 - d3) / denom;
        outBary = Math::Vector3(0.0f, 1.0f - w, w);
        return b + (c - b) * w;
    }

    float denom = 1.0f / std::max(va + vb + vc, Math::EPSILON);
    float v = vb * denom;
    float w = vc * denom;
    outBary = Math::Vector3(1.0f - v - w, v, w);
    return a + ab * v + ac * w;
}

static int ResolveTriangleMaterialIndex(const Mesh& mesh, size_t triangleFirstIndex) {
    const auto& submeshes = mesh.getSubmeshes();
    for (const Submesh& submesh : submeshes) {
        if (triangleFirstIndex >= submesh.indexStart && triangleFirstIndex < static_cast<size_t>(submesh.indexStart + submesh.indexCount)) {
            return static_cast<int>(submesh.materialIndex);
        }
    }
    return 0;
}

static bool FindClosestSurfaceSample(Entity* entity,
                                     const Math::Vector3& pointWS,
                                     SurfaceSample& outSample) {
    outSample = SurfaceSample();
    if (!entity) {
        return false;
    }

    MeshRenderer* renderer = entity->getComponent<MeshRenderer>();
    if (!renderer) {
        return false;
    }
    std::shared_ptr<Mesh> mesh = renderer->getMesh();
    if (!mesh || mesh->getVertices().empty() || mesh->getIndices().size() < 3) {
        return false;
    }

    const auto& vertices = mesh->getVertices();
    const auto& indices = mesh->getIndices();
    Math::Matrix4x4 worldMatrix = entity->getTransform()->getWorldMatrix();
    Math::Matrix4x4 normalMatrix = worldMatrix.normalMatrix();

    for (size_t tri = 0; tri + 2 < indices.size(); tri += 3) {
        uint32_t i0 = indices[tri + 0];
        uint32_t i1 = indices[tri + 1];
        uint32_t i2 = indices[tri + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
            continue;
        }

        const Vertex& v0 = vertices[i0];
        const Vertex& v1 = vertices[i1];
        const Vertex& v2 = vertices[i2];
        Math::Vector3 p0 = worldMatrix.transformPoint(v0.position);
        Math::Vector3 p1 = worldMatrix.transformPoint(v1.position);
        Math::Vector3 p2 = worldMatrix.transformPoint(v2.position);

        Math::Vector3 bary;
        Math::Vector3 closest = ClosestPointOnTriangle(pointWS, p0, p1, p2, bary);
        float distanceSq = (closest - pointWS).lengthSquared();
        if (distanceSq >= outSample.distanceSq) {
            continue;
        }

        Math::Vector3 localNormal = (v0.normal * bary.x + v1.normal * bary.y + v2.normal * bary.z).normalized();
        Math::Vector3 worldNormal = normalMatrix.transformDirection(localNormal).normalized();
        if (worldNormal.lengthSquared() <= Math::EPSILON) {
            worldNormal = (p1 - p0).cross(p2 - p0).normalized();
        }

        outSample.valid = true;
        outSample.positionWS = closest;
        outSample.normalWS = worldNormal.lengthSquared() > Math::EPSILON ? worldNormal : Math::Vector3::Up;
        outSample.uv = v0.texCoord * bary.x + v1.texCoord * bary.y + v2.texCoord * bary.z;
        outSample.materialIndex = ResolveTriangleMaterialIndex(*mesh, tri);
        outSample.distanceSq = distanceSq;
    }

    return outSample.valid;
}

static void ResolveMaterialSample(std::shared_ptr<Material> material,
                                  const Math::Vector2& uv,
                                  Math::Vector3& outAlbedo,
                                  Math::Vector3& outEmission,
                                  float& outAO) {
    outAlbedo = Math::Vector3(0.7f, 0.7f, 0.7f);
    outEmission = Math::Vector3::Zero;
    outAO = 1.0f;
    if (!material) {
        return;
    }

    Math::Vector2 tiledUV = TransformMaterialUV(*material, uv);
    const Math::Vector4& albedoValue = material->getAlbedo();
    outAlbedo = ClampColor(Math::Vector3(albedoValue.x, albedoValue.y, albedoValue.z), 1.0f);
    if (std::shared_ptr<Texture2D> albedoTexture = material->getAlbedoTexture()) {
        outAlbedo = MultiplyColor(outAlbedo, ClampColor(SampleCPUImageRGB(LoadCPUImage(albedoTexture->getPath()), tiledUV, true), 1.0f));
    }

    outEmission = ClampColor(material->getEmission() * material->getEmissionStrength(), 16.0f);
    if (std::shared_ptr<Texture2D> emissionTexture = material->getEmissionTexture()) {
        outEmission = MultiplyColor(outEmission, ClampColor(SampleCPUImageRGB(LoadCPUImage(emissionTexture->getPath()), tiledUV, true), 8.0f));
    }

    outAO = Math::Clamp(material->getAO(), 0.05f, 1.0f);
    if (std::shared_ptr<Texture2D> aoTexture = material->getAOTexture()) {
        Math::Vector3 aoSample = SampleCPUImageRGB(LoadCPUImage(aoTexture->getPath()), tiledUV, false);
        outAO *= Math::Clamp(aoSample.x, 0.05f, 1.0f);
    }
}

static void BuildBakeTangentBasis(const Math::Vector3& geometryNormalWS,
                                  const Math::Vector3& interpolatedTangentWS,
                                  const Math::Vector3& interpolatedBitangentWS,
                                  Math::Vector3& outTangentWS,
                                  Math::Vector3& outBitangentWS) {
    Math::Vector3 normalWS = geometryNormalWS.normalized();
    if (normalWS.lengthSquared() <= Math::EPSILON) {
        normalWS = Math::Vector3::Up;
    }

    Math::Vector3 tangentWS = interpolatedTangentWS - normalWS * interpolatedTangentWS.dot(normalWS);
    if (tangentWS.lengthSquared() <= Math::EPSILON) {
        Math::Vector3 reference = (std::abs(normalWS.y) < 0.999f) ? Math::Vector3::Up : Math::Vector3::Right;
        tangentWS = normalWS.cross(reference).normalized();
        if (tangentWS.lengthSquared() <= Math::EPSILON) {
            tangentWS = normalWS.cross(Math::Vector3::Forward).normalized();
        }
    } else {
        tangentWS.normalize();
    }

    float handedness = 1.0f;
    if (interpolatedBitangentWS.lengthSquared() > Math::EPSILON) {
        handedness = (normalWS.cross(tangentWS).dot(interpolatedBitangentWS) < 0.0f) ? -1.0f : 1.0f;
    }

    Math::Vector3 bitangentWS = normalWS.cross(tangentWS).normalized() * handedness;
    if (bitangentWS.lengthSquared() <= Math::EPSILON) {
        bitangentWS = tangentWS.cross(normalWS).normalized();
    }

    outTangentWS = tangentWS.lengthSquared() > Math::EPSILON ? tangentWS : Math::Vector3::Right;
    outBitangentWS = bitangentWS.lengthSquared() > Math::EPSILON ? bitangentWS : Math::Vector3::Forward;
}

static Math::Vector3 ResolveBakeShadingNormal(std::shared_ptr<Material> material,
                                              const Math::Vector2& uv,
                                              const Math::Vector3& geometryNormalWS,
                                              const Math::Vector3& interpolatedTangentWS,
                                              const Math::Vector3& interpolatedBitangentWS) {
    Math::Vector3 normalWS = geometryNormalWS.normalized();
    if (normalWS.lengthSquared() <= Math::EPSILON) {
        return Math::Vector3::Up;
    }

    if (!material) {
        return normalWS;
    }

    std::shared_ptr<Texture2D> normalTexture = material->getNormalTexture();
    if (!normalTexture) {
        return normalWS;
    }

    const CPUImageCacheEntry* image = LoadCPUImage(normalTexture->getPath());
    if (!image) {
        return normalWS;
    }

    Math::Vector3 tangentWS;
    Math::Vector3 bitangentWS;
    BuildBakeTangentBasis(normalWS, interpolatedTangentWS, interpolatedBitangentWS, tangentWS, bitangentWS);

    Math::Vector2 tiledUV = TransformMaterialUV(*material, uv);
    Math::Vector3 tangentNormal = SampleCPUImageRGB(image, tiledUV, false) * 2.0f - Math::Vector3::One;
    tangentNormal.x *= material->getNormalScale();
    tangentNormal.y *= material->getNormalScale();
    if (tangentNormal.lengthSquared() <= Math::EPSILON) {
        return normalWS;
    }
    tangentNormal.normalize();

    Math::Vector3 shadingNormalWS = (tangentWS * tangentNormal.x)
        + (bitangentWS * tangentNormal.y)
        + (normalWS * tangentNormal.z);
    shadingNormalWS.normalize();
    return shadingNormalWS.lengthSquared() > Math::EPSILON ? shadingNormalWS : normalWS;
}

static bool IsEmitterTwoSided(const std::shared_ptr<Mesh>& mesh,
                              const std::shared_ptr<Material>& material) {
    if (material && (material->isTwoSided() || material->getCullMode() == Material::CullMode::Off)) {
        return true;
    }
    return mesh && mesh->isDoubleSided();
}

static std::vector<EmissiveTriangleSurface> BuildEmissiveTriangleSurfaces(Scene* scene) {
    std::vector<EmissiveTriangleSurface> surfaces;
    if (!scene) {
        return surfaces;
    }

    float cumulativeWeight = 0.0f;
    for (const auto& entityPtr : scene->getAllEntities()) {
        Entity* entity = entityPtr.get();
        if (!entity || !entity->isActiveInHierarchy() || entity->isEditorOnly()) {
            continue;
        }

        MeshRenderer* renderer = entity->getComponent<MeshRenderer>();
        if (!renderer || entity->getComponent<SkinnedMeshRenderer>()) {
            continue;
        }

        const auto& staticLighting = renderer->getStaticLighting();
        if (!staticLighting.staticGeometry || !staticLighting.contributeGI) {
            continue;
        }

        std::shared_ptr<Mesh> mesh = renderer->getMesh();
        if (!mesh || mesh->getVertices().empty() || mesh->getIndices().size() < 3) {
            continue;
        }

        const auto& vertices = mesh->getVertices();
        const auto& indices = mesh->getIndices();
        Math::Matrix4x4 worldMatrix = entity->getTransform()->getWorldMatrix();
        Math::Matrix4x4 normalMatrix = worldMatrix.normalMatrix();

        for (size_t tri = 0; tri + 2 < indices.size(); tri += 3) {
            uint32_t i0 = indices[tri + 0];
            uint32_t i1 = indices[tri + 1];
            uint32_t i2 = indices[tri + 2];
            if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
                continue;
            }

            int materialIndex = ResolveTriangleMaterialIndex(*mesh, tri);
            std::shared_ptr<Material> material = renderer->getMaterial(static_cast<uint32_t>(std::max(materialIndex, 0)));
            if (!material || material->getEmissionStrength() <= 0.0f) {
                continue;
            }

            const Vertex& v0 = vertices[i0];
            const Vertex& v1 = vertices[i1];
            const Vertex& v2 = vertices[i2];

            Math::Vector3 p0 = worldMatrix.transformPoint(v0.position);
            Math::Vector3 p1 = worldMatrix.transformPoint(v1.position);
            Math::Vector3 p2 = worldMatrix.transformPoint(v2.position);
            float area = TriangleArea(p0, p1, p2);
            if (area <= 1e-5f) {
                continue;
            }

            Math::Vector2 centroidUV = (v0.texCoord + v1.texCoord + v2.texCoord) / 3.0f;
            Math::Vector3 centroidAlbedo;
            Math::Vector3 centroidEmission;
            float centroidAO = 1.0f;
            ResolveMaterialSample(material, centroidUV, centroidAlbedo, centroidEmission, centroidAO);
            float luminance = ComputeLuminance(centroidEmission);
            if (luminance <= 0.01f) {
                continue;
            }

            Math::Vector3 n0 = normalMatrix.transformDirection(v0.normal).normalized();
            Math::Vector3 n1 = normalMatrix.transformDirection(v1.normal).normalized();
            Math::Vector3 n2 = normalMatrix.transformDirection(v2.normal).normalized();
            Math::Vector3 triangleNormal = (p1 - p0).cross(p2 - p0).normalized();
            if (n0.lengthSquared() <= Math::EPSILON) n0 = triangleNormal;
            if (n1.lengthSquared() <= Math::EPSILON) n1 = triangleNormal;
            if (n2.lengthSquared() <= Math::EPSILON) n2 = triangleNormal;

            EmissiveTriangleSurface surface;
            surface.entity = entity;
            surface.renderer = renderer;
            surface.p0 = p0;
            surface.p1 = p1;
            surface.p2 = p2;
            surface.n0 = n0.lengthSquared() > Math::EPSILON ? n0 : Math::Vector3::Up;
            surface.n1 = n1.lengthSquared() > Math::EPSILON ? n1 : Math::Vector3::Up;
            surface.n2 = n2.lengthSquared() > Math::EPSILON ? n2 : Math::Vector3::Up;
            surface.uv0 = v0.texCoord;
            surface.uv1 = v1.texCoord;
            surface.uv2 = v2.texCoord;
            surface.materialIndex = materialIndex;
            surface.twoSided = IsEmitterTwoSided(mesh, material);
            surface.area = area;
            surface.weight = std::max(area * luminance, 1e-5f);
            cumulativeWeight += surface.weight;
            surface.cumulativeWeight = cumulativeWeight;
            surfaces.push_back(surface);
        }
    }

    return surfaces;
}

static bool SampleEmissiveTriangleSurface(const std::vector<EmissiveTriangleSurface>& surfaces,
                                          uint32_t sampleSeed,
                                          int sampleIndex,
                                          EmissiveSurfaceSample& outSample) {
    outSample = EmissiveSurfaceSample();
    if (surfaces.empty()) {
        return false;
    }

    float totalWeight = std::max(surfaces.back().cumulativeWeight, 1e-5f);
    float selection = HashToUnitFloat(sampleSeed + static_cast<uint32_t>(sampleIndex * 92821 + 17)) * totalWeight;
    auto it = std::lower_bound(
        surfaces.begin(),
        surfaces.end(),
        selection,
        [](const EmissiveTriangleSurface& surface, float value) {
            return surface.cumulativeWeight < value;
        }
    );
    if (it == surfaces.end()) {
        it = surfaces.end() - 1;
    }

    Math::Vector2 random = Random2D(
        sampleSeed + static_cast<uint32_t>(sampleIndex * 2 + 101),
        sampleSeed + static_cast<uint32_t>(sampleIndex * 2 + 102)
    );
    float sqrtU = std::sqrt(Math::Clamp(random.x, 0.0f, 1.0f));
    float bary0 = 1.0f - sqrtU;
    float bary1 = sqrtU * (1.0f - random.y);
    float bary2 = 1.0f - bary0 - bary1;

    Math::Vector3 positionWS = it->p0 * bary0 + it->p1 * bary1 + it->p2 * bary2;
    Math::Vector3 normalWS = (it->n0 * bary0 + it->n1 * bary1 + it->n2 * bary2).normalized();
    if (normalWS.lengthSquared() <= Math::EPSILON) {
        normalWS = (it->p1 - it->p0).cross(it->p2 - it->p0).normalized();
    }
    Math::Vector2 uv = it->uv0 * bary0 + it->uv1 * bary1 + it->uv2 * bary2;

    std::shared_ptr<Material> material = it->renderer
        ? it->renderer->getMaterial(static_cast<uint32_t>(std::max(it->materialIndex, 0)))
        : nullptr;
    Math::Vector3 sampledAlbedo;
    Math::Vector3 sampledEmission;
    float sampledAO = 1.0f;
    ResolveMaterialSample(material, uv, sampledAlbedo, sampledEmission, sampledAO);
    if (ComputeLuminance(sampledEmission) <= 0.001f) {
        return false;
    }

    outSample.surface = &(*it);
    outSample.positionWS = positionWS;
    outSample.normalWS = normalWS.lengthSquared() > Math::EPSILON ? normalWS : Math::Vector3::Up;
    outSample.uv = uv;
    outSample.emission = sampledEmission;
    outSample.pdfArea = std::max((it->weight / totalWeight) / std::max(it->area, 1e-5f), 1e-5f);
    outSample.twoSided = it->twoSided;
    return true;
}

static EmissiveLightingEstimate EstimateEmissiveSurfaceLighting(Scene* scene,
                                                                const std::vector<EmissiveTriangleSurface>& surfaces,
                                                                const Math::Vector3& positionWS,
                                                                const Math::Vector3& normalWS,
                                                                uint32_t sampleSeed,
                                                                int sampleCount) {
    EmissiveLightingEstimate estimate;
    if (!scene || surfaces.empty()) {
        return estimate;
    }

    PhysicsWorld* physicsWorld = scene->getPhysicsWorld();
    sampleCount = std::max(1, std::min(16, sampleCount));
    Math::Vector3 origin = positionWS + normalWS * 0.0035f;

    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
        EmissiveSurfaceSample sample;
        if (!SampleEmissiveTriangleSurface(surfaces, sampleSeed, sampleIndex, sample)) {
            continue;
        }

        Math::Vector3 toEmitter = sample.positionWS - origin;
        float distanceSq = toEmitter.lengthSquared();
        if (distanceSq <= 1e-4f) {
            continue;
        }
        float distance = std::sqrt(distanceSq);
        Math::Vector3 lightDir = toEmitter / distance;
        float nDotL = std::max(normalWS.dot(lightDir), 0.0f);
        if (nDotL <= 0.0f) {
            continue;
        }

        float emitterCos = sample.twoSided
            ? std::abs(sample.normalWS.dot(-lightDir))
            : std::max(sample.normalWS.dot(-lightDir), 0.0f);
        if (emitterCos <= 0.0f) {
            continue;
        }

        if (physicsWorld) {
            constexpr float kHitTolerance = 0.01f;
            PhysicsRaycastHit hit;
            if (physicsWorld->raycast(origin,
                                      lightDir,
                                      std::max(0.0f, distance - kHitTolerance),
                                      hit,
                                      PhysicsWorld::kAllLayersMask,
                                      false,
                                      nullptr)) {
                continue;
            }
        }

        float geometry = (nDotL * emitterCos) / std::max(distanceSq, 0.01f);
        Math::Vector3 contribution = sample.emission * (geometry / std::max(sample.pdfArea, 1e-5f));
        contribution = ClampColor(contribution, 32.0f);
        estimate.irradiance += contribution;

        float weight = std::max(ComputeLuminance(contribution), 0.0f);
        estimate.weightedDirection += lightDir * weight;
        estimate.directionWeight += weight;
        estimate.referenceNoL += nDotL * weight;
    }

    float invSampleCount = 1.0f / static_cast<float>(sampleCount);
    estimate.irradiance *= invSampleCount;
    estimate.weightedDirection *= invSampleCount;
    estimate.directionWeight *= invSampleCount;
    estimate.referenceNoL *= invSampleCount;
    return estimate;
}

static Math::Vector3 BakeLightContribution(const BakedDirectLight& light,
                                           const Math::Vector3& positionWS,
                                           const Math::Vector3& normalWS) {
    Math::Vector3 L = Math::Vector3::Zero;
    float attenuation = 1.0f;

    switch (light.type) {
        case Light::Type::Directional:
            L = (-light.directionWS).normalized();
            attenuation = 1.0f;
            break;
        case Light::Type::Point:
        case Light::Type::AreaRect:
        case Light::Type::AreaDisk:
        case Light::Type::EmissiveMesh: {
            Math::Vector3 toLight = light.positionWS - positionWS;
            float distance = toLight.length();
            if (distance <= Math::EPSILON || (light.range > 0.0f && distance > light.range)) {
                return Math::Vector3::Zero;
            }
            L = toLight / distance;
            float rangeNorm = (light.range > 0.0f) ? Math::Clamp(distance / light.range, 0.0f, 1.0f) : 0.0f;
            float smoothFalloff = std::pow(std::max(0.0f, 1.0f - std::pow(rangeNorm, 4.0f)), 2.0f);
            attenuation = smoothFalloff / std::max(distance * distance, 0.001f);
            if (light.type == Light::Type::AreaRect ||
                light.type == Light::Type::AreaDisk ||
                light.type == Light::Type::EmissiveMesh) {
                attenuation *= 1.0f / std::max(distance, 0.1f);
            }
            break;
        }
        case Light::Type::Spot: {
            Math::Vector3 toLight = light.positionWS - positionWS;
            float distance = toLight.length();
            if (distance <= Math::EPSILON || (light.range > 0.0f && distance > light.range)) {
                return Math::Vector3::Zero;
            }
            L = toLight / distance;
            float rangeNorm = (light.range > 0.0f) ? Math::Clamp(distance / light.range, 0.0f, 1.0f) : 0.0f;
            float smoothFalloff = std::pow(std::max(0.0f, 1.0f - std::pow(rangeNorm, 4.0f)), 2.0f);
            attenuation = smoothFalloff / std::max(distance * distance, 0.001f);
            float cosTheta = light.directionWS.normalized().dot(-L);
            if (cosTheta <= light.cosOuter) {
                return Math::Vector3::Zero;
            }
            float denom = std::max(light.cosInner - light.cosOuter, 0.0001f);
            float spotAttenuation = Math::Clamp((cosTheta - light.cosOuter) / denom, 0.0f, 1.0f);
            attenuation *= spotAttenuation;
            break;
        }
    }

    float nDotL = std::max(normalWS.dot(L), 0.0f);
    if (nDotL <= 0.0f || attenuation <= 0.0f) {
        return Math::Vector3::Zero;
    }

    float scaledIntensity = light.intensity;
    switch (light.type) {
        case Light::Type::Directional:
            break;
        case Light::Type::Point:
            scaledIntensity = light.intensity / (4.0f * Math::PI);
            break;
        case Light::Type::Spot: {
            float solidAngle = 2.0f * Math::PI * (1.0f - light.cosOuter);
            scaledIntensity = (solidAngle > Math::EPSILON) ? (light.intensity / solidAngle) : light.intensity;
            break;
        }
        case Light::Type::AreaRect:
        case Light::Type::AreaDisk:
        case Light::Type::EmissiveMesh:
            break;
    }
    return light.color * (scaledIntensity * attenuation * nDotL);
}

static bool ComputeIncidentLightDirection(const BakedDirectLight& light,
                                          const Math::Vector3& positionWS,
                                          const Math::Vector3& normalWS,
                                          Math::Vector3& outLightDir,
                                          float& outReferenceNdotL) {
    switch (light.type) {
        case Light::Type::Directional:
            outLightDir = (-light.directionWS).normalized();
            break;
        case Light::Type::Point:
        case Light::Type::Spot:
        case Light::Type::AreaRect:
        case Light::Type::AreaDisk:
        case Light::Type::EmissiveMesh: {
            Math::Vector3 toLight = light.positionWS - positionWS;
            float distance = toLight.length();
            if (distance <= Math::EPSILON) {
                return false;
            }
            outLightDir = toLight / distance;
            break;
        }
    }

    if (outLightDir.lengthSquared() <= Math::EPSILON) {
        return false;
    }

    outReferenceNdotL = std::max(normalWS.dot(outLightDir), 0.0f);
    return outReferenceNdotL > 0.0f;
}

static bool IsDirectLightVisible(Scene* scene,
                                 const BakedDirectLight& light,
                                 const Math::Vector3& positionWS,
                                 const Math::Vector3& normalWS) {
    if (!scene || !light.castShadows) {
        return true;
    }

    PhysicsWorld* physicsWorld = scene->getPhysicsWorld();
    if (!physicsWorld) {
        return true;
    }

    constexpr float kMinOriginBias = 0.0025f;
    constexpr float kMinHitTolerance = 0.01f;

    float normalBias = std::max(kMinOriginBias, light.shadowNormalBias * 8.0f);
    float hitTolerance = std::max(kMinHitTolerance, light.shadowBias * 32.0f);
    Math::Vector3 origin = positionWS + normalWS * normalBias;

    Math::Vector3 rayDirection = Math::Vector3::Zero;
    float maxDistance = 0.0f;

    switch (light.type) {
        case Light::Type::Directional: {
            rayDirection = (-light.directionWS).normalized();
            if (rayDirection.lengthSquared() <= Math::EPSILON) {
                return true;
            }
            maxDistance = std::max(light.shadowDistance, 200.0f);
            break;
        }
        case Light::Type::Point:
        case Light::Type::Spot:
        case Light::Type::AreaRect:
        case Light::Type::AreaDisk:
        case Light::Type::EmissiveMesh: {
            Math::Vector3 toLight = light.positionWS - origin;
            float distance = toLight.length();
            if (distance <= Math::EPSILON) {
                return true;
            }
            rayDirection = toLight / distance;
            maxDistance = std::max(0.0f, distance - hitTolerance);
            break;
        }
    }

    if (maxDistance <= Math::EPSILON) {
        return true;
    }

    PhysicsRaycastHit hit;
    if (!physicsWorld->raycast(origin,
                               rayDirection,
                               maxDistance,
                               hit,
                               PhysicsWorld::kAllLayersMask,
                               false,
                               nullptr)) {
        return true;
    }

    return hit.distance <= hitTolerance;
}

static Math::Vector3 SampleBakeEnvironmentRadiance(const SceneEnvironmentSettings& environment,
                                                   const Math::Vector3& directionWS) {
    float up = Math::Clamp(directionWS.y * 0.5f + 0.5f, 0.0f, 1.0f);
    Math::Vector3 ambient = environment.ambientColor * std::max(environment.ambientIntensity, 0.0f);
    Math::Vector3 skyTint = MultiplyColor(environment.tint, Math::Vector3(0.55f, 0.65f, 0.8f));
    Math::Vector3 horizonTint = MultiplyColor(environment.tint, Math::Vector3(0.45f, 0.47f, 0.5f));
    Math::Vector3 groundTint = MultiplyColor(environment.tint, Math::Vector3(0.14f, 0.12f, 0.1f));
    float skyWeight = std::pow(up, 0.35f);
    float groundWeight = std::pow(1.0f - up, 1.75f);
    Math::Vector3 sky = (skyTint * skyWeight + horizonTint * (1.0f - skyWeight)) * std::max(environment.skyIntensity, 0.0f);
    Math::Vector3 ground = groundTint * (groundWeight * 0.35f);
    Math::Vector3 ibl = sky * std::max(environment.iblIntensity, 0.0f);
    return ClampColor(ambient + ibl + ground, 8.0f);
}

static void BuildOrthonormalBasis(const Math::Vector3& normal,
                                  Math::Vector3& outTangent,
                                  Math::Vector3& outBitangent) {
    Math::Vector3 reference = (std::abs(normal.y) < 0.999f) ? Math::Vector3::Up : Math::Vector3::Right;
    outTangent = normal.cross(reference).normalized();
    if (outTangent.lengthSquared() <= Math::EPSILON) {
        reference = Math::Vector3::Forward;
        outTangent = normal.cross(reference).normalized();
    }
    outBitangent = outTangent.cross(normal).normalized();
}

static Math::Vector3 SampleCosineHemisphere(const Math::Vector3& normalWS, const Math::Vector2& random) {
    float r = std::sqrt(Math::Clamp(random.x, 0.0f, 1.0f));
    float phi = 2.0f * Math::PI * random.y;
    float x = r * std::cos(phi);
    float y = r * std::sin(phi);
    float z = std::sqrt(Math::Clamp(1.0f - random.x, 0.0f, 1.0f));

    Math::Vector3 tangent;
    Math::Vector3 bitangent;
    BuildOrthonormalBasis(normalWS, tangent, bitangent);
    Math::Vector3 sampleDirection = tangent * x + bitangent * y + normalWS * z;
    return sampleDirection.normalized();
}

static void ResolveIndirectSurfaceProperties(Entity* entity,
                                             const Math::Vector3& pointWS,
                                             Math::Vector3& outAlbedo,
                                             Math::Vector3& outEmission,
                                             float& outAO,
                                             bool& outContributeGI) {
    outAlbedo = Math::Vector3(0.7f, 0.7f, 0.7f);
    outEmission = Math::Vector3::Zero;
    outAO = 1.0f;
    outContributeGI = true;

    if (!entity) {
        return;
    }

    MeshRenderer* renderer = entity->getComponent<MeshRenderer>();
    if (!renderer) {
        return;
    }

    const auto& staticLighting = renderer->getStaticLighting();
    outContributeGI = staticLighting.contributeGI;
    SurfaceSample surface;
    if (FindClosestSurfaceSample(entity, pointWS, surface)) {
        ResolveMaterialSample(renderer->getMaterial(static_cast<uint32_t>(std::max(surface.materialIndex, 0))),
                              surface.uv,
                              outAlbedo,
                              outEmission,
                              outAO);
    } else if (std::shared_ptr<Material> material = renderer->getMaterial()) {
        ResolveMaterialSample(material, Math::Vector2::Zero, outAlbedo, outEmission, outAO);
    }
}

static int ComputeAdaptiveIndirectSampleCount(const SceneSettings& bakeSettings,
                                              const Math::Vector3& directLighting,
                                              const Math::Vector3& surfaceEmission,
                                              float ao) {
    int baseSamples = std::max(1, std::min(48, bakeSettings.staticLighting.samplesPerTexel / 32));
    float directComplexity = Math::Clamp(ComputeLuminance(directLighting) / 1.5f, 0.0f, 1.0f);
    float emissiveComplexity = Math::Clamp(ComputeLuminance(surfaceEmission) / 2.0f, 0.0f, 1.0f);
    float occlusionComplexity = 1.0f - Math::Clamp(ao, 0.0f, 1.0f);
    float complexity = Math::Clamp(directComplexity * 0.5f + emissiveComplexity * 0.3f + occlusionComplexity * 0.2f, 0.0f, 1.0f);
    float multiplier = 0.7f + complexity * 1.7f;
    return std::max(1, std::min(64, static_cast<int>(std::round(static_cast<float>(baseSamples) * multiplier))));
}

static Math::Vector3 EstimateIndirectLighting(Scene* scene,
                                              const SceneSettings& bakeSettings,
                                              const std::vector<BakedDirectLight>& bakedLights,
                                              const std::vector<EmissiveTriangleSurface>& emissiveSurfaces,
                                              const Math::Vector3& positionWS,
                                              const Math::Vector3& normalWS,
                                              uint32_t sampleSeed,
                                              int sampleCount) {
    if (!scene || bakeSettings.staticLighting.indirectBounces <= 0 || bakeSettings.staticLighting.samplesPerTexel <= 0) {
        return Math::Vector3::Zero;
    }

    PhysicsWorld* physicsWorld = scene->getPhysicsWorld();
    if (!physicsWorld) {
        return Math::Vector3::Zero;
    }

    sampleCount = std::max(1, std::min(64, sampleCount));
    const int bounceCount = std::max(1, std::min(4, bakeSettings.staticLighting.indirectBounces));
    const Math::Vector3 environmentFallback = SampleBakeEnvironmentRadiance(bakeSettings.environment, normalWS);

    Math::Vector3 accumulatedIndirect = Math::Vector3::Zero;
    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
        Math::Vector3 throughput = Math::Vector3::One;
        Math::Vector3 bounceOrigin = positionWS + normalWS * 0.005f;
        Math::Vector3 bounceNormal = normalWS;
        Math::Vector3 bounceDirection = SampleCosineHemisphere(
            bounceNormal,
            Random2D(sampleSeed + static_cast<uint32_t>(sampleIndex * 2 + 1),
                     sampleSeed + static_cast<uint32_t>(sampleIndex * 2 + 2))
        );

        for (int bounceIndex = 0; bounceIndex < bounceCount; ++bounceIndex) {
            PhysicsRaycastHit hit;
            bool hitScene = physicsWorld->raycast(bounceOrigin,
                                                  bounceDirection,
                                                  512.0f,
                                                  hit,
                                                  PhysicsWorld::kAllLayersMask,
                                                  false,
                                                  nullptr);
            if (!hitScene || !hit.hit) {
                accumulatedIndirect += MultiplyColor(throughput, SampleBakeEnvironmentRadiance(bakeSettings.environment, bounceDirection));
                break;
            }

            Math::Vector3 hitNormal = hit.normal.normalized();
            if (hitNormal.lengthSquared() <= Math::EPSILON) {
                hitNormal = (-bounceDirection).normalized();
            } else if (hitNormal.dot(bounceDirection) > 0.0f) {
                hitNormal = -hitNormal;
            }

            Math::Vector3 surfaceAlbedo;
            Math::Vector3 surfaceEmission;
            float surfaceAO = 1.0f;
            bool contributeGI = true;
            ResolveIndirectSurfaceProperties(hit.entity, hit.point, surfaceAlbedo, surfaceEmission, surfaceAO, contributeGI);
            if (!contributeGI) {
                break;
            }

            Math::Vector3 directBounce = Math::Vector3::Zero;
            Math::Vector3 shadingPoint = hit.point + hitNormal * 0.0035f;
            for (const BakedDirectLight& light : bakedLights) {
                Math::Vector3 contribution = BakeLightContribution(light, shadingPoint, hitNormal);
                if (contribution.lengthSquared() <= Math::EPSILON) {
                    continue;
                }
                if (!IsDirectLightVisible(scene, light, shadingPoint, hitNormal)) {
                    continue;
                }
                directBounce += contribution;
            }
            if (!emissiveSurfaces.empty()) {
                EmissiveLightingEstimate emissiveBounce = EstimateEmissiveSurfaceLighting(
                    scene,
                    emissiveSurfaces,
                    shadingPoint,
                    hitNormal,
                    sampleSeed + static_cast<uint32_t>(sampleIndex * 271 + bounceIndex * 3571 + 911),
                    std::max(1, std::min(4, sampleCount / 4))
                );
                directBounce += emissiveBounce.irradiance;
            }

            Math::Vector3 lambert = surfaceAlbedo * (surfaceAO / Math::PI);
            accumulatedIndirect += MultiplyColor(throughput, MultiplyColor(lambert, directBounce + surfaceEmission));

            if (bounceIndex == bounceCount - 1) {
                accumulatedIndirect += MultiplyColor(throughput, MultiplyColor(lambert, environmentFallback * 0.35f));
                break;
            }

            throughput = MultiplyColor(throughput, surfaceAlbedo * (surfaceAO * 0.82f));
            float rrProbability = Math::Clamp(std::max(throughput.x, std::max(throughput.y, throughput.z)), 0.1f, 0.95f);
            if (bounceIndex > 0 && HashToUnitFloat(sampleSeed + static_cast<uint32_t>(sampleIndex * 17 + bounceIndex * 131)) > rrProbability) {
                break;
            }
            if (bounceIndex > 0) {
                throughput /= rrProbability;
            }

            bounceOrigin = hit.point + hitNormal * 0.005f;
            bounceNormal = hitNormal;
            bounceDirection = SampleCosineHemisphere(
                bounceNormal,
                Random2D(sampleSeed + static_cast<uint32_t>((sampleIndex + 1) * 193 + bounceIndex * 37),
                         sampleSeed + static_cast<uint32_t>((sampleIndex + 1) * 389 + bounceIndex * 53))
            );
        }
    }

    return ClampColor(accumulatedIndirect / static_cast<float>(sampleCount), 12.0f);
}

struct ProbeVolumeFileHeader {
    char magic[4];
    uint32_t version = 3u;
    uint32_t countX = 0u;
    uint32_t countY = 0u;
    uint32_t countZ = 0u;
    float boundsMin[3] = {0.0f, 0.0f, 0.0f};
    float boundsMax[3] = {0.0f, 0.0f, 0.0f};
};

struct ProbeVolumeFileRecord {
    float ambientCube[6][4] = {};
    float specularCube[6][4] = {};
    float positionAndValidity[4] = {};
    float visibility[2][4] = {};
};

static std::string SanitizeLightingArtifactStem(std::string value) {
    if (value.empty()) {
        value = "Scene";
    }
    for (char& c : value) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
            c = '_';
        }
    }
    return value;
}

static bool ComputeStaticGeometryBounds(Scene* scene,
                                        Math::Vector3& outMin,
                                        Math::Vector3& outMax) {
    outMin = Math::Vector3(std::numeric_limits<float>::max());
    outMax = Math::Vector3(std::numeric_limits<float>::lowest());
    bool hasBounds = false;

    if (!scene) {
        return false;
    }

    for (const auto& entityPtr : scene->getAllEntities()) {
        Entity* entity = entityPtr.get();
        if (!entity || !entity->isActiveInHierarchy() || entity->isEditorOnly()) {
            continue;
        }

        MeshRenderer* renderer = entity->getComponent<MeshRenderer>();
        if (!renderer || entity->getComponent<SkinnedMeshRenderer>()) {
            continue;
        }

        const auto& staticLighting = renderer->getStaticLighting();
        if (!staticLighting.staticGeometry) {
            continue;
        }

        std::shared_ptr<Mesh> mesh = renderer->getMesh();
        if (!mesh || mesh->getVertices().empty() || mesh->getIndices().size() < 3) {
            continue;
        }

        outMin = Math::Vector3::Min(outMin, renderer->getBoundsMin());
        outMax = Math::Vector3::Max(outMax, renderer->getBoundsMax());
        hasBounds = true;
    }

    if (!hasBounds) {
        return false;
    }

    Math::Vector3 extent = outMax - outMin;
    Math::Vector3 margin = Math::Vector3::Max(extent * 0.05f, Math::Vector3(0.75f, 0.75f, 0.75f));
    outMin -= margin;
    outMax += margin;
    return true;
}

static Math::Vector3 ComputeProbeGridPosition(const Math::Vector3& boundsMin,
                                              const Math::Vector3& boundsMax,
                                              int x,
                                              int y,
                                              int z,
                                              int countX,
                                              int countY,
                                              int countZ) {
    float tx = (countX > 1) ? static_cast<float>(x) / static_cast<float>(countX - 1) : 0.5f;
    float ty = (countY > 1) ? static_cast<float>(y) / static_cast<float>(countY - 1) : 0.5f;
    float tz = (countZ > 1) ? static_cast<float>(z) / static_cast<float>(countZ - 1) : 0.5f;
    return Math::Vector3(
        Math::Lerp(boundsMin.x, boundsMax.x, tx),
        Math::Lerp(boundsMin.y, boundsMax.y, ty),
        Math::Lerp(boundsMin.z, boundsMax.z, tz)
    );
}

static float TraceProbeVisibilityDistance(Scene* scene,
                                          const Math::Vector3& origin,
                                          const Math::Vector3& direction,
                                          float maxDistance) {
    PhysicsWorld* physicsWorld = scene ? scene->getPhysicsWorld() : nullptr;
    if (!physicsWorld || maxDistance <= Math::EPSILON) {
        return maxDistance;
    }

    PhysicsRaycastHit hit;
    Math::Vector3 rayDirection = direction.normalized();
    if (rayDirection.lengthSquared() <= Math::EPSILON) {
        return maxDistance;
    }

    Math::Vector3 rayOrigin = origin + rayDirection * 0.02f;
    if (!physicsWorld->raycast(rayOrigin,
                               rayDirection,
                               maxDistance,
                               hit,
                               PhysicsWorld::kAllLayersMask,
                               false,
                               nullptr)) {
        return maxDistance;
    }

    return Math::Clamp(hit.distance, 0.02f, maxDistance);
}

static Math::Vector3 RelocateProbePosition(Scene* scene,
                                           const Math::Vector3& candidatePosition,
                                           const Math::Vector3& boundsMin,
                                           const Math::Vector3& boundsMax,
                                           float clearanceDistance,
                                           float traceDistance,
                                           float& outValidity) {
    static const Math::Vector3 probeDirections[6] = {
        Math::Vector3::Right,
        -Math::Vector3::Right,
        Math::Vector3::Up,
        -Math::Vector3::Up,
        Math::Vector3::Forward,
        -Math::Vector3::Forward
    };

    Math::Vector3 position = Math::Vector3::Clamp(candidatePosition, boundsMin, boundsMax);
    outValidity = 1.0f;

    PhysicsWorld* physicsWorld = scene ? scene->getPhysicsWorld() : nullptr;
    if (!physicsWorld) {
        return position;
    }

    clearanceDistance = std::max(clearanceDistance, 0.08f);
    traceDistance = std::max(traceDistance, clearanceDistance * 2.0f);

    for (int iteration = 0; iteration < 4; ++iteration) {
        Math::Vector3 push = Math::Vector3::Zero;
        float visibilityDistances[6] = {};
        float openness = 0.0f;

        for (int directionIndex = 0; directionIndex < 6; ++directionIndex) {
            float hitDistance = TraceProbeVisibilityDistance(scene, position, probeDirections[directionIndex], traceDistance);
            visibilityDistances[directionIndex] = hitDistance;
            openness += Math::Clamp(hitDistance / traceDistance, 0.0f, 1.0f);
            if (hitDistance < clearanceDistance) {
                push -= probeDirections[directionIndex] * (clearanceDistance - hitDistance);
            }
        }

        outValidity = Math::Clamp(openness / 6.0f, 0.08f, 1.0f);

        std::vector<PhysicsOverlapHit> overlaps;
        bool embeddedInGeometry = physicsWorld->overlapSphere(position,
                                                              clearanceDistance * 0.35f,
                                                              overlaps,
                                                              PhysicsWorld::kAllLayersMask,
                                                              false,
                                                              nullptr) > 0;
        if (!embeddedInGeometry && push.lengthSquared() <= Math::EPSILON) {
            break;
        }

        if (push.lengthSquared() <= Math::EPSILON) {
            int bestDirection = 0;
            for (int directionIndex = 1; directionIndex < 6; ++directionIndex) {
                if (visibilityDistances[directionIndex] > visibilityDistances[bestDirection]) {
                    bestDirection = directionIndex;
                }
            }
            push = probeDirections[bestDirection] * clearanceDistance;
            outValidity *= 0.75f;
        }

        float pushLength = std::min(clearanceDistance * 0.75f, std::max(push.length(), 0.02f));
        position = Math::Vector3::Clamp(position + push.normalized() * pushLength, boundsMin, boundsMax);
    }

    return position;
}

static Math::Vector3 EvaluateBakedLightingAtPoint(Scene* scene,
                                                  const SceneSettings& bakeSettings,
                                                  const std::vector<BakedDirectLight>& bakedLights,
                                                  const std::vector<EmissiveTriangleSurface>& emissiveSurfaces,
                                                  const Math::Vector3& positionWS,
                                                  const Math::Vector3& normalWS,
                                                  uint32_t sampleSeed,
                                                  int indirectSamples) {
    Math::Vector3 direct = Math::Vector3::Zero;
    for (const BakedDirectLight& light : bakedLights) {
        Math::Vector3 contribution = BakeLightContribution(light, positionWS, normalWS);
        if (contribution.lengthSquared() <= Math::EPSILON) {
            continue;
        }
        if (!IsDirectLightVisible(scene, light, positionWS, normalWS)) {
            continue;
        }
        direct += contribution;
    }

    if (!emissiveSurfaces.empty()) {
        EmissiveLightingEstimate emissive = EstimateEmissiveSurfaceLighting(
            scene,
            emissiveSurfaces,
            positionWS,
            normalWS,
            sampleSeed ^ 0x9e3779b9u,
            std::max(1, std::min(8, indirectSamples / 12))
        );
        direct += emissive.irradiance;
    }

    Math::Vector3 indirect = EstimateIndirectLighting(
        scene,
        bakeSettings,
        bakedLights,
        emissiveSurfaces,
        positionWS,
        normalWS,
        sampleSeed,
        indirectSamples
    );
    return ClampColor(direct + indirect, 16.0f);
}

static Math::Vector3 EvaluateSpecularProbeLightingAtPoint(Scene* scene,
                                                          const SceneSettings& bakeSettings,
                                                          const std::vector<BakedDirectLight>& bakedLights,
                                                          const std::vector<EmissiveTriangleSurface>& emissiveSurfaces,
                                                          const Math::Vector3& positionWS,
                                                          const Math::Vector3& sampleDirectionWS,
                                                          uint32_t sampleSeed,
                                                          int indirectSamples) {
    Math::Vector3 directionWS = sampleDirectionWS.normalized();
    if (directionWS.lengthSquared() <= Math::EPSILON) {
        return Math::Vector3::Zero;
    }

    Math::Vector3 radiance = SampleBakeEnvironmentRadiance(bakeSettings.environment, directionWS);
    for (const BakedDirectLight& light : bakedLights) {
        Math::Vector3 incidentDir = Math::Vector3::Zero;
        float referenceNoL = 0.0f;
        if (!ComputeIncidentLightDirection(light, positionWS, directionWS, incidentDir, referenceNoL)) {
            continue;
        }
        if (!IsDirectLightVisible(scene, light, positionWS, directionWS)) {
            continue;
        }

        Math::Vector3 directionalContribution = BakeLightContribution(light, positionWS, directionWS);
        if (directionalContribution.lengthSquared() <= Math::EPSILON) {
            continue;
        }

        float alignment = Math::Clamp(directionWS.dot(incidentDir), 0.0f, 1.0f);
        float lobe = 0.15f + 0.85f * alignment * alignment;
        radiance += directionalContribution * lobe * 1.35f;
    }

    if (!emissiveSurfaces.empty()) {
        EmissiveLightingEstimate emissive = EstimateEmissiveSurfaceLighting(
            scene,
            emissiveSurfaces,
            positionWS,
            directionWS,
            sampleSeed ^ 0x6a09e667u,
            std::max(1, std::min(8, indirectSamples / 10))
        );
        radiance += emissive.irradiance;
    }

    Math::Vector3 indirect = EstimateIndirectLighting(
        scene,
        bakeSettings,
        bakedLights,
        emissiveSurfaces,
        positionWS,
        directionWS,
        sampleSeed ^ 0xbb67ae85u,
        std::max(1, indirectSamples / 2)
    );
    radiance += indirect * 0.65f;
    return ClampColor(radiance, 24.0f);
}

static std::string BuildProbeVolumeArtifactPath(Scene* scene,
                                                const SceneSettings& bakeSettings,
                                                const std::string& scenePath) {
    std::filesystem::path outputDir = bakeSettings.staticLighting.outputDirectory.empty()
        ? std::filesystem::path("Library/BakedLighting")
        : std::filesystem::path(bakeSettings.staticLighting.outputDirectory);
    if (outputDir.is_relative()) {
        outputDir = std::filesystem::current_path() / outputDir;
    }

    std::string sceneStem = !scenePath.empty()
        ? std::filesystem::path(scenePath).stem().string()
        : (scene ? scene->getName() : std::string("Scene"));
    sceneStem = SanitizeLightingArtifactStem(sceneStem);
    return (outputDir / (sceneStem + "_probes.bin")).lexically_normal().string();
}

static bool BakeProbeVolume(Scene* scene,
                            const std::string& scenePath,
                            const SceneSettings& bakeSettings,
                            const std::vector<BakedDirectLight>& bakedLights,
                            const std::vector<EmissiveTriangleSurface>& emissiveSurfaces) {
    if (!scene || !bakeSettings.staticLighting.probeVolume) {
        return false;
    }

    int countX = std::max(1, std::min(32, bakeSettings.staticLighting.probeCountX));
    int countY = std::max(1, std::min(16, bakeSettings.staticLighting.probeCountY));
    int countZ = std::max(1, std::min(32, bakeSettings.staticLighting.probeCountZ));
    int probeSamples = std::max(8, std::min(256, bakeSettings.staticLighting.probeSamples));

    Math::Vector3 boundsMin;
    Math::Vector3 boundsMax;
    if (!ComputeStaticGeometryBounds(scene, boundsMin, boundsMax)) {
        return false;
    }

    Math::Vector3 extent = Math::Vector3::Max(boundsMax - boundsMin, Math::Vector3(0.5f, 0.5f, 0.5f));
    auto axisSpacing = [](float axisExtent, int count) -> float {
        return (count > 1) ? (axisExtent / static_cast<float>(count - 1)) : axisExtent;
    };
    float spacingX = axisSpacing(extent.x, countX);
    float spacingY = axisSpacing(extent.y, countY);
    float spacingZ = axisSpacing(extent.z, countZ);
    float minSpacing = std::max(0.25f, std::min(spacingX, std::min(spacingY, spacingZ)));
    float maxSpacing = std::max(spacingX, std::max(spacingY, spacingZ));
    float relocationClearance = std::max(0.12f, minSpacing * 0.3f);
    float visibilityDistance = std::max(1.5f, maxSpacing * 1.75f);

    std::vector<ProbeVolumeFileRecord> records(static_cast<size_t>(countX) * static_cast<size_t>(countY) * static_cast<size_t>(countZ));
    const Math::Vector3 probeNormals[6] = {
        Math::Vector3::Right,
        -Math::Vector3::Right,
        Math::Vector3::Up,
        -Math::Vector3::Up,
        Math::Vector3::Forward,
        -Math::Vector3::Forward
    };

    size_t probeIndex = 0;
    for (int z = 0; z < countZ; ++z) {
        for (int y = 0; y < countY; ++y) {
            for (int x = 0; x < countX; ++x) {
                Math::Vector3 gridPositionWS = ComputeProbeGridPosition(boundsMin, boundsMax, x, y, z, countX, countY, countZ);
                float probeValidity = 1.0f;
                Math::Vector3 positionWS = RelocateProbePosition(scene,
                                                                 gridPositionWS,
                                                                 boundsMin,
                                                                 boundsMax,
                                                                 relocationClearance,
                                                                 visibilityDistance,
                                                                 probeValidity);
                ProbeVolumeFileRecord& record = records[probeIndex++];
                record.positionAndValidity[0] = positionWS.x;
                record.positionAndValidity[1] = positionWS.y;
                record.positionAndValidity[2] = positionWS.z;
                record.positionAndValidity[3] = probeValidity;

                float probeVisibility[6] = {};
                for (int faceIndex = 0; faceIndex < 6; ++faceIndex) {
                    probeVisibility[faceIndex] = TraceProbeVisibilityDistance(scene,
                                                                              positionWS,
                                                                              probeNormals[faceIndex],
                                                                              visibilityDistance);
                    Math::Vector3 lighting = EvaluateBakedLightingAtPoint(
                        scene,
                        bakeSettings,
                        bakedLights,
                        emissiveSurfaces,
                        positionWS + probeNormals[faceIndex] * 0.02f,
                        probeNormals[faceIndex],
                        static_cast<uint32_t>((x + 1) * 73856093u)
                            ^ static_cast<uint32_t>((y + 1) * 19349663u)
                            ^ static_cast<uint32_t>((z + 1) * 83492791u)
                            ^ static_cast<uint32_t>((faceIndex + 1) * 2654435761u),
                        probeSamples
                    );
                    record.ambientCube[faceIndex][0] = lighting.x;
                    record.ambientCube[faceIndex][1] = lighting.y;
                    record.ambientCube[faceIndex][2] = lighting.z;
                    record.ambientCube[faceIndex][3] = 1.0f;

                    Math::Vector3 specularLighting = EvaluateSpecularProbeLightingAtPoint(
                        scene,
                        bakeSettings,
                        bakedLights,
                        emissiveSurfaces,
                        positionWS + probeNormals[faceIndex] * 0.02f,
                        probeNormals[faceIndex],
                        static_cast<uint32_t>((x + 1) * 2166136261u)
                            ^ static_cast<uint32_t>((y + 1) * 16777619u)
                            ^ static_cast<uint32_t>((z + 1) * 709607u)
                            ^ static_cast<uint32_t>((faceIndex + 1) * 40503u),
                        probeSamples
                    );
                    record.specularCube[faceIndex][0] = specularLighting.x;
                    record.specularCube[faceIndex][1] = specularLighting.y;
                    record.specularCube[faceIndex][2] = specularLighting.z;
                    record.specularCube[faceIndex][3] = 1.0f;
                }
                record.visibility[0][0] = probeVisibility[0];
                record.visibility[0][1] = probeVisibility[1];
                record.visibility[0][2] = probeVisibility[2];
                record.visibility[0][3] = probeVisibility[3];
                record.visibility[1][0] = probeVisibility[4];
                record.visibility[1][1] = probeVisibility[5];
                record.visibility[1][2] = visibilityDistance;
                record.visibility[1][3] = 0.0f;
            }
        }
    }

    std::string outputPath = BuildProbeVolumeArtifactPath(scene, bakeSettings, scenePath);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(outputPath).parent_path(), ec);

    ProbeVolumeFileHeader header{};
    header.magic[0] = 'C';
    header.magic[1] = 'P';
    header.magic[2] = 'R';
    header.magic[3] = 'B';
    header.version = 3u;
    header.countX = static_cast<uint32_t>(countX);
    header.countY = static_cast<uint32_t>(countY);
    header.countZ = static_cast<uint32_t>(countZ);
    header.boundsMin[0] = boundsMin.x;
    header.boundsMin[1] = boundsMin.y;
    header.boundsMin[2] = boundsMin.z;
    header.boundsMax[0] = boundsMax.x;
    header.boundsMax[1] = boundsMax.y;
    header.boundsMax[2] = boundsMax.z;

    std::ofstream out(outputPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(reinterpret_cast<const char*>(records.data()), static_cast<std::streamsize>(records.size() * sizeof(ProbeVolumeFileRecord)));
    if (!out.good()) {
        return false;
    }

    SceneSettings updatedSettings = scene->getSettings();
    updatedSettings.staticLighting.probeVolume = true;
    updatedSettings.staticLighting.probeCountX = countX;
    updatedSettings.staticLighting.probeCountY = countY;
    updatedSettings.staticLighting.probeCountZ = countZ;
    updatedSettings.staticLighting.probeSamples = probeSamples;
    updatedSettings.staticLighting.probeBoundsMin = boundsMin;
    updatedSettings.staticLighting.probeBoundsMax = boundsMax;
    updatedSettings.staticLighting.probeDataPath = outputPath;
    scene->setSettings(updatedSettings);
    return true;
}

static void DenoiseIndirectAtlas(const std::vector<float>& guideDirectLighting,
                                 const std::vector<uint16_t>& coverage,
                                 int width,
                                 int height,
                                 std::vector<float>& indirectLighting) {
    if (width <= 0 || height <= 0 || indirectLighting.empty() || guideDirectLighting.size() != indirectLighting.size()) {
        return;
    }

    std::vector<float> ping = indirectLighting;
    std::vector<float> pong(indirectLighting.size(), 0.0f);
    const int stepWidths[3] = {1, 2, 4};
    const float spatialWeights[3][3] = {
        {1.0f / 16.0f, 1.0f / 8.0f, 1.0f / 16.0f},
        {1.0f / 8.0f, 1.0f / 4.0f, 1.0f / 8.0f},
        {1.0f / 16.0f, 1.0f / 8.0f, 1.0f / 16.0f}
    };

    auto loadVec3 = [](const std::vector<float>& buffer, size_t pixelIndex) {
        return Math::Vector3(buffer[pixelIndex * 3 + 0], buffer[pixelIndex * 3 + 1], buffer[pixelIndex * 3 + 2]);
    };
    auto storeVec3 = [](std::vector<float>& buffer, size_t pixelIndex, const Math::Vector3& value) {
        buffer[pixelIndex * 3 + 0] = value.x;
        buffer[pixelIndex * 3 + 1] = value.y;
        buffer[pixelIndex * 3 + 2] = value.z;
    };

    for (int pass = 0; pass < 3; ++pass) {
        int step = stepWidths[pass];
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                size_t pixelIndex = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
                if (coverage[pixelIndex] == 0u) {
                    storeVec3(pong, pixelIndex, Math::Vector3::Zero);
                    continue;
                }

                Math::Vector3 centerIndirect = loadVec3(ping, pixelIndex);
                float centerGuide = ComputeLuminance(loadVec3(guideDirectLighting, pixelIndex));
                Math::Vector3 filtered = Math::Vector3::Zero;
                float totalWeight = 0.0f;

                for (int oy = -1; oy <= 1; ++oy) {
                    int sampleY = y + oy * step;
                    if (sampleY < 0 || sampleY >= height) {
                        continue;
                    }
                    for (int ox = -1; ox <= 1; ++ox) {
                        int sampleX = x + ox * step;
                        if (sampleX < 0 || sampleX >= width) {
                            continue;
                        }

                        size_t sampleIndex = static_cast<size_t>(sampleY) * static_cast<size_t>(width) + static_cast<size_t>(sampleX);
                        if (coverage[sampleIndex] == 0u) {
                            continue;
                        }

                        Math::Vector3 sampleIndirect = loadVec3(ping, sampleIndex);
                        float sampleGuide = ComputeLuminance(loadVec3(guideDirectLighting, sampleIndex));
                        float guideDelta = std::abs(sampleGuide - centerGuide);
                        float colorDelta = (sampleIndirect - centerIndirect).length();
                        float coverageSimilarity = std::abs(static_cast<float>(coverage[sampleIndex]) - static_cast<float>(coverage[pixelIndex]));
                        float weight = spatialWeights[oy + 1][ox + 1]
                            * std::exp(-guideDelta * (2.8f / static_cast<float>(step)))
                            * std::exp(-colorDelta * (2.0f / static_cast<float>(step)))
                            * std::exp(-coverageSimilarity * 0.08f);
                        filtered += sampleIndirect * weight;
                        totalWeight += weight;
                    }
                }

                if (totalWeight > Math::EPSILON) {
                    filtered /= totalWeight;
                } else {
                    filtered = centerIndirect;
                }
                storeVec3(pong, pixelIndex, filtered);
            }
        }
        ping.swap(pong);
    }

    indirectLighting = std::move(ping);
}

static ModelCacheEntry BuildModelCache(const std::string& path, const SceneCommands::ModelImportOptions& options) {
    Scene temp("ModelCache");
    SceneCommands::importModel(&temp, path, options, "ModelCache");

    ModelCacheEntry entry;
    for (const auto& entityPtr : temp.getAllEntities()) {
        Entity* entity = entityPtr.get();
        if (!entity) {
            continue;
        }
        ModelMeshReference* reference = entity->getComponent<ModelMeshReference>();
        if (!reference) {
            continue;
        }
        MeshRenderer* renderer = entity->getComponent<MeshRenderer>();
        if (!renderer) {
            continue;
        }
        MeshCacheEntry meshEntry;
        meshEntry.mesh = renderer->getMesh();
        meshEntry.material = renderer->getMaterial(0);
        if (reference->isMerged()) {
            entry.mergedByMaterial[reference->getMaterialIndex()] = meshEntry;
        } else {
            entry.meshesByIndex[reference->getMeshIndex()] = meshEntry;
        }

        if (reference->isSkinned()) {
            SkinnedMeshRenderer* skinned = entity->getComponent<SkinnedMeshRenderer>();
            if (skinned && !entry.skeleton) {
                entry.skeleton = skinned->getSkeleton();
                entry.animations = skinned->getAnimationClips();
            }
        }
    }
    return entry;
}

static bool ReplaceMaterialTexture(Material* material,
                                   const std::string& path,
                                   const std::shared_ptr<Texture2D>& replacement,
                                   const std::shared_ptr<Texture2D>& terrainControlReplacement = nullptr) {
    if (!material || path.empty() || !replacement) {
        return false;
    }
    bool updated = false;
    auto swapIfMatch = [&](const std::shared_ptr<Texture2D>& current,
                           const std::function<void(std::shared_ptr<Texture2D>)>& setter,
                           const std::shared_ptr<Texture2D>& slotReplacement = nullptr) {
        if (current && current->getPath() == path) {
            setter(slotReplacement ? slotReplacement : replacement);
            updated = true;
        }
    };

    swapIfMatch(material->getAlbedoTexture(), [&](std::shared_ptr<Texture2D> tex) { material->setAlbedoTexture(tex); });
    swapIfMatch(material->getNormalTexture(), [&](std::shared_ptr<Texture2D> tex) { material->setNormalTexture(tex); });
    swapIfMatch(material->getMetallicTexture(), [&](std::shared_ptr<Texture2D> tex) { material->setMetallicTexture(tex); });
    swapIfMatch(material->getRoughnessTexture(), [&](std::shared_ptr<Texture2D> tex) { material->setRoughnessTexture(tex); });
    swapIfMatch(material->getAOTexture(), [&](std::shared_ptr<Texture2D> tex) { material->setAOTexture(tex); });
    swapIfMatch(material->getEmissionTexture(), [&](std::shared_ptr<Texture2D> tex) { material->setEmissionTexture(tex); });
    swapIfMatch(material->getORMTexture(), [&](std::shared_ptr<Texture2D> tex) { material->setORMTexture(tex); });
    swapIfMatch(material->getHeightTexture(), [&](std::shared_ptr<Texture2D> tex) { material->setHeightTexture(tex); });
    swapIfMatch(material->getOpacityTexture(), [&](std::shared_ptr<Texture2D> tex) { material->setOpacityTexture(tex); });
    swapIfMatch(material->getTerrainControlTexture(), [&](std::shared_ptr<Texture2D> tex) { material->setTerrainControlTexture(tex); }, terrainControlReplacement);
    swapIfMatch(material->getTerrainLayer0Texture(), [&](std::shared_ptr<Texture2D> tex) { material->setTerrainLayer0Texture(tex); });
    swapIfMatch(material->getTerrainLayer1Texture(), [&](std::shared_ptr<Texture2D> tex) { material->setTerrainLayer1Texture(tex); });
    swapIfMatch(material->getTerrainLayer2Texture(), [&](std::shared_ptr<Texture2D> tex) { material->setTerrainLayer2Texture(tex); });
    swapIfMatch(material->getTerrainLayer0NormalTexture(), [&](std::shared_ptr<Texture2D> tex) { material->setTerrainLayer0NormalTexture(tex); });
    swapIfMatch(material->getTerrainLayer1NormalTexture(), [&](std::shared_ptr<Texture2D> tex) { material->setTerrainLayer1NormalTexture(tex); });
    swapIfMatch(material->getTerrainLayer2NormalTexture(), [&](std::shared_ptr<Texture2D> tex) { material->setTerrainLayer2NormalTexture(tex); });
    swapIfMatch(material->getTerrainLayer0ORMTexture(), [&](std::shared_ptr<Texture2D> tex) { material->setTerrainLayer0ORMTexture(tex); });
    swapIfMatch(material->getTerrainLayer1ORMTexture(), [&](std::shared_ptr<Texture2D> tex) { material->setTerrainLayer1ORMTexture(tex); });
    swapIfMatch(material->getTerrainLayer2ORMTexture(), [&](std::shared_ptr<Texture2D> tex) { material->setTerrainLayer2ORMTexture(tex); });
    return updated;
}

static std::string SafeName(const aiString& name, const std::string& fallback) {
    if (name.length > 0) {
        return std::string(name.C_Str());
    }
    return fallback;
}

static std::string ToLower(const std::string& value) {
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lowered;
}

static std::string NormalizeKey(const std::string& value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (unsigned char c : value) {
        if (std::isalnum(c)) {
            normalized.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    return normalized;
}

static std::vector<std::string> SplitTokens(const std::string& value) {
    std::vector<std::string> tokens;
    std::string current;
    for (unsigned char c : value) {
        if (std::isalnum(c)) {
            current.push_back(static_cast<char>(std::tolower(c)));
        } else if (!current.empty()) {
            tokens.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

static bool IsTextureExtension(const std::string& ext) {
    if (ext.empty()) {
        return false;
    }
    static const std::unordered_set<std::string> kTextureExts = {
        ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".dds", ".exr", ".hdr", ".tif", ".tiff", ".ktx", ".ktx2"
    };
    return kTextureExts.count(ext) > 0;
}

static void BuildTextureIndex(ImportContext& context) {
    if (context.textureIndexBuilt) {
        return;
    }
    context.textureIndexBuilt = true;
    if (context.baseDir.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(context.baseDir, ec);
    if (ec) {
        return;
    }
    for (const auto& entry : it) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const std::filesystem::path& path = entry.path();
        std::string ext = ToLower(path.extension().string());
        if (!IsTextureExtension(ext)) {
            continue;
        }
        std::string baseName = path.stem().string();
        std::string baseLower = ToLower(baseName);
        TextureCandidate candidate;
        candidate.baseNameLower = baseLower;
        candidate.normalizedName = NormalizeKey(baseLower);
        candidate.fullPath = path.string();
        candidate.tokens = SplitTokens(baseLower);
        context.textureCandidates.push_back(std::move(candidate));
    }
}

static Math::Matrix4x4 ToMatrix(const aiMatrix4x4& matrix) {
    return Math::Matrix4x4(
        matrix.a1, matrix.a2, matrix.a3, matrix.a4,
        matrix.b1, matrix.b2, matrix.b3, matrix.b4,
        matrix.c1, matrix.c2, matrix.c3, matrix.c4,
        matrix.d1, matrix.d2, matrix.d3, matrix.d4
    );
}

static aiMatrix4x4 GetNodeWorldTransform(const aiNode* node) {
    aiMatrix4x4 result = node ? node->mTransformation : aiMatrix4x4();
    const aiNode* parent = node ? node->mParent : nullptr;
    while (parent) {
        result = parent->mTransformation * result;
        parent = parent->mParent;
    }
    return result;
}

static bool ShouldImportLOD(const std::string& name, bool onlyLOD0) {
    if (!onlyLOD0) {
        return true;
    }
    if (name.empty()) {
        return true;
    }
    std::string lowered = ToLower(name);
    auto lodPos = lowered.find("lod");
    if (lodPos == std::string::npos) {
        return true;
    }
    if (lowered.find("lod0") != std::string::npos) {
        return true;
    }
    if (lowered.find("lod_0") != std::string::npos) {
        return true;
    }
    if (lowered.find("lod 0") != std::string::npos) {
        return true;
    }
    if (lowered.find("lod00") != std::string::npos) {
        return true;
    }
    return false;
}

static std::string ResolveTexturePath(const std::string& baseDir, const aiString& texturePath) {
    if (texturePath.length == 0) {
        return "";
    }
    std::string rawPath = texturePath.C_Str();
    if (rawPath.empty() || rawPath[0] == '*') {
        return "";
    }
    std::filesystem::path path(rawPath);
    if (path.is_relative()) {
        path = std::filesystem::path(baseDir) / path;
    }
    return path.lexically_normal().string();
}

static std::string ExtractEmbeddedChannelOrder(const aiTexture* texture) {
    if (!texture) {
        return "bgra";
    }
    std::string hint(texture->achFormatHint);
    std::string order;
    order.reserve(4);
    for (unsigned char c : hint) {
        if (!std::isalpha(c)) {
            break;
        }
        if (order.size() < 4) {
            order.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    if (order.empty()) {
        return "bgra";
    }
    return order;
}

static bool BuildEmbeddedRGBA(const aiTexture* texture, std::vector<unsigned char>& rgba) {
    if (!texture || texture->mHeight == 0 || !texture->pcData) {
        return false;
    }
    size_t texelCount = static_cast<size_t>(texture->mWidth) * static_cast<size_t>(texture->mHeight);
    rgba.resize(texelCount * 4);

    std::string order = ExtractEmbeddedChannelOrder(texture);
    auto channelIndex = [&](char channel) -> int {
        size_t pos = order.find(channel);
        if (pos == std::string::npos) {
            return -1;
        }
        return static_cast<int>(pos);
    };
    int rIndex = channelIndex('r');
    int gIndex = channelIndex('g');
    int bIndex = channelIndex('b');
    int aIndex = channelIndex('a');

    const unsigned char* src = reinterpret_cast<const unsigned char*>(texture->pcData);
    for (size_t i = 0; i < texelCount; ++i) {
        const unsigned char* px = src + i * 4;
        rgba[i * 4 + 0] = rIndex >= 0 ? px[rIndex] : 0;
        rgba[i * 4 + 1] = gIndex >= 0 ? px[gIndex] : 0;
        rgba[i * 4 + 2] = bIndex >= 0 ? px[bIndex] : 0;
        rgba[i * 4 + 3] = aIndex >= 0 ? px[aIndex] : 255;
    }
    return true;
}

static std::shared_ptr<Texture2D> LoadEmbeddedTexture(ImportContext& context,
                                                      const aiTexture* texture,
                                                      bool srgb,
                                                      bool normalMap,
                                                      const std::string& cacheKey) {
    if (!texture || !context.textureLoader) {
        return nullptr;
    }
    if (texture->mHeight == 0) {
        const unsigned char* data = reinterpret_cast<const unsigned char*>(texture->pcData);
        size_t size = static_cast<size_t>(texture->mWidth);
        return context.textureLoader->loadTextureFromMemory(data, size, srgb, true, cacheKey, normalMap);
    }
    
    std::vector<unsigned char> rgba;
    if (!BuildEmbeddedRGBA(texture, rgba)) {
        return nullptr;
    }
    return context.textureLoader->createTextureFromRGBA8(cacheKey,
                                                         rgba.data(),
                                                         static_cast<int>(texture->mWidth),
                                                         static_cast<int>(texture->mHeight),
                                                         srgb,
                                                         true,
                                                         normalMap);
}

static std::shared_ptr<Texture2D> LoadMaterialTexture(ImportContext& context,
                                                      const aiMaterial* material,
                                                      aiTextureType type,
                                                      bool srgb,
                                                      bool normalMap) {
    if (!material || !context.textureLoader) {
        return nullptr;
    }
    if (material->GetTextureCount(type) == 0) {
        return nullptr;
    }
    aiString texturePath;
    if (material->GetTexture(type, 0, &texturePath) != AI_SUCCESS) {
        return nullptr;
    }
    std::string rawPath = texturePath.C_Str();
    if (!rawPath.empty() && rawPath[0] == '*') {
        if (!context.aiScene) {
            return nullptr;
        }
        int index = std::atoi(rawPath.c_str() + 1);
        if (index < 0 || index >= static_cast<int>(context.aiScene->mNumTextures)) {
            return nullptr;
        }
        const aiTexture* embedded = context.aiScene->mTextures[index];
        std::string key = context.sourcePath + "#embedded:" + std::to_string(index);
        if (embedded && embedded->achFormatHint[0] != '\0') {
            key += ".";
            key += embedded->achFormatHint;
        }
        return LoadEmbeddedTexture(context, embedded, srgb, normalMap, key);
    }
    std::string resolved = ResolveTexturePath(context.baseDir, texturePath);
    if (resolved.empty()) {
        return nullptr;
    }
    std::error_code ec;
    if (!std::filesystem::exists(resolved, ec)) {
        std::cerr << "[ModelImporter] Missing texture: " << resolved << std::endl;
        return nullptr;
    }
    return context.textureLoader->loadTexture(resolved, srgb, true, normalMap);
}

static bool HasAnyToken(const TextureCandidate& candidate, const std::vector<std::string>& needles) {
    for (const auto& needle : needles) {
        if (std::find(candidate.tokens.begin(), candidate.tokens.end(), needle) != candidate.tokens.end()) {
            return true;
        }
        if (candidate.baseNameLower.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

static std::shared_ptr<Texture2D> FindFallbackTexture(ImportContext& context,
                                                      const std::string& materialName,
                                                      const std::vector<std::string>& channelTokens,
                                                      bool srgb,
                                                      bool normalMap) {
    if (!context.textureLoader || context.baseDir.empty()) {
        return nullptr;
    }
    BuildTextureIndex(context);
    if (context.textureCandidates.empty()) {
        return nullptr;
    }
    std::string materialKey = NormalizeKey(materialName);
    const TextureCandidate* bestMaterialMatch = nullptr;
    const TextureCandidate* firstMatch = nullptr;
    size_t matchCount = 0;
    size_t materialMatchCount = 0;
    for (const auto& candidate : context.textureCandidates) {
        if (!HasAnyToken(candidate, channelTokens)) {
            continue;
        }
        ++matchCount;
        if (!firstMatch) {
            firstMatch = &candidate;
        }
        if (!materialKey.empty() && candidate.normalizedName.find(materialKey) != std::string::npos) {
            ++materialMatchCount;
            if (!bestMaterialMatch || candidate.baseNameLower.size() < bestMaterialMatch->baseNameLower.size()) {
                bestMaterialMatch = &candidate;
            }
        }
    }
    const TextureCandidate* chosen = nullptr;
    if (materialMatchCount == 1 && bestMaterialMatch) {
        chosen = bestMaterialMatch;
    } else if (materialMatchCount == 0 && matchCount == 1) {
        chosen = firstMatch;
    } else if (materialMatchCount > 1 && bestMaterialMatch) {
        chosen = bestMaterialMatch;
    }
    if (!chosen) {
        return nullptr;
    }
    return context.textureLoader->loadTexture(chosen->fullPath, srgb, true, normalMap);
}

static std::shared_ptr<Material> BuildMaterial(ImportContext& context, const aiMaterial* material) {
    auto result = Material::CreateDefault();
    if (!material) {
        return result;
    }

    std::string sourceExt = std::filesystem::path(context.sourcePath).extension().string();
    std::transform(sourceExt.begin(), sourceExt.end(), sourceExt.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    const bool isGltfSource = (sourceExt == ".gltf" || sourceExt == ".glb");
    if (isGltfSource) {
        // glTF 2.0 defaults: metallicFactor = 1.0, roughnessFactor = 1.0.
        // Assimp may omit these keys entirely when they are not authored explicitly.
        result->setMetallic(1.0f);
        result->setRoughness(1.0f);
    }
    
    static const std::vector<std::string> kAlbedoTokens = {
        "albedo", "basecolor", "base_color", "basecolour", "base_colour", "diffuse", "color", "colour"
    };
    static const std::vector<std::string> kNormalTokens = {
        "normal", "nrm"
    };
    static const std::vector<std::string> kMetallicTokens = {
        "metallic", "metalness", "metal"
    };
    static const std::vector<std::string> kRoughnessTokens = {
        "roughness", "rough", "rgh"
    };
    static const std::vector<std::string> kAOTokens = {
        "ao", "occlusion", "ambientocclusion", "ambient_occlusion"
    };
    static const std::vector<std::string> kORMTokens = {
        "orm", "metallicroughness", "metallic_roughness", "metalroughness", "metal_roughness",
        "occlusionroughnessmetallic", "occlusion_roughness_metallic", "mra", "rma"
    };
    static const std::vector<std::string> kEmissionTokens = {
        "emissive", "emission", "emit", "glow"
    };
    static const std::vector<std::string> kHeightTokens = {
        "height", "displace", "displacement", "bump", "parallax"
    };
    static const std::vector<std::string> kOpacityTokens = {
        "opacity", "alpha", "mask", "transparency", "cutout"
    };
    
    aiString name;
    if (material->Get(AI_MATKEY_NAME, name) == AI_SUCCESS && name.length > 0) {
        result->setName(name.C_Str());
    }
    std::string materialName = result->getName();
    
    aiColor4D baseColor;
    if (material->Get(AI_MATKEY_BASE_COLOR, baseColor) == AI_SUCCESS ||
        material->Get(AI_MATKEY_COLOR_DIFFUSE, baseColor) == AI_SUCCESS) {
        result->setAlbedo(Math::Vector4(baseColor.r, baseColor.g, baseColor.b, baseColor.a));
    }
    
    float opacity = 1.0f;
    if (material->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS) {
        result->setAlpha(opacity);
    }
    
    aiColor3D emissiveColor(0.0f, 0.0f, 0.0f);
    if (material->Get(AI_MATKEY_COLOR_EMISSIVE, emissiveColor) == AI_SUCCESS) {
        result->setEmission(Math::Vector3(emissiveColor.r, emissiveColor.g, emissiveColor.b));
        if (emissiveColor.r != 0.0f || emissiveColor.g != 0.0f || emissiveColor.b != 0.0f) {
            result->setEmissionStrength(1.0f);
        }
    }
    
    bool roughnessSet = false;
    float metallic = isGltfSource ? 1.0f : 0.0f;
    if (material->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS) {
        result->setMetallic(metallic);
    }
    float roughness = isGltfSource ? 1.0f : 0.0f;
    if (material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS) {
        result->setRoughness(roughness);
        roughnessSet = true;
    } else if (isGltfSource) {
        roughnessSet = true;
    }
    if (!roughnessSet) {
        float shininess = 0.0f;
        if (material->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS && shininess > 0.0f) {
            float derived = 1.0f - std::min(shininess / 256.0f, 1.0f);
            result->setRoughness(derived);
        }
    }
    
    int twoSided = 0;
    if (material->Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS) {
        result->setTwoSided(twoSided != 0);
    }
    
    aiString alphaMode;
    if (material->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == AI_SUCCESS && alphaMode.length > 0) {
        std::string mode = alphaMode.C_Str();
        if (mode == "BLEND") {
            result->setRenderMode(Material::RenderMode::Transparent);
        } else if (mode == "MASK") {
            result->setRenderMode(Material::RenderMode::Cutout);
            float cutoff = 0.5f;
            if (material->Get(AI_MATKEY_GLTF_ALPHACUTOFF, cutoff) == AI_SUCCESS) {
                result->setAlphaCutoff(cutoff);
            }
        }
    }
    if (result->getAlpha() < 0.999f && result->getRenderMode() == Material::RenderMode::Opaque) {
        result->setRenderMode(Material::RenderMode::Transparent);
    }
    
    auto albedoTex = LoadMaterialTexture(context, material, aiTextureType_BASE_COLOR, true, true);
    if (!albedoTex) {
        albedoTex = LoadMaterialTexture(context, material, aiTextureType_DIFFUSE, true, true);
    }
    if (!albedoTex) {
        albedoTex = FindFallbackTexture(context, materialName, kAlbedoTokens, true, true);
    }
    if (albedoTex) {
        result->setAlbedoTexture(albedoTex);
    }
    
    auto normalTex = LoadMaterialTexture(context, material, aiTextureType_NORMALS, false, true);
    auto heightTex = LoadMaterialTexture(context, material, aiTextureType_HEIGHT, false, false);
    if (!normalTex && heightTex) {
        normalTex = heightTex;
        heightTex.reset();
    }
    if (!normalTex) {
        normalTex = FindFallbackTexture(context, materialName, kNormalTokens, false, true);
    }
    if (normalTex) {
        result->setNormalTexture(normalTex);
    }
    if (!heightTex) {
        heightTex = FindFallbackTexture(context, materialName, kHeightTokens, false, false);
    }
    if (heightTex) {
        result->setHeightTexture(heightTex);
    }

    auto opacityTex = LoadMaterialTexture(context, material, aiTextureType_OPACITY, false, false);
    if (!opacityTex) {
        opacityTex = FindFallbackTexture(context, materialName, kOpacityTokens, false, false);
    }
    if (opacityTex) {
        result->setOpacityTexture(opacityTex);
        if (result->getRenderMode() == Material::RenderMode::Opaque) {
            result->setRenderMode(Material::RenderMode::Cutout);
            if (result->getAlphaCutoff() <= 0.0f) {
                result->setAlphaCutoff(0.4f);
            }
        }
    }
    
    auto metallicTex = LoadMaterialTexture(context, material, aiTextureType_METALNESS, false, false);
    if (!metallicTex) {
        metallicTex = FindFallbackTexture(context, materialName, kMetallicTokens, false, false);
    }
    if (metallicTex) {
        result->setMetallicTexture(metallicTex);
    }
    
    auto roughnessTex = LoadMaterialTexture(context, material, aiTextureType_DIFFUSE_ROUGHNESS, false, false);
    if (!roughnessTex) {
        roughnessTex = FindFallbackTexture(context, materialName, kRoughnessTokens, false, false);
    }
    if (roughnessTex) {
        result->setRoughnessTexture(roughnessTex);
    }
    
    auto aoTex = LoadMaterialTexture(context, material, aiTextureType_AMBIENT_OCCLUSION, false, false);
    if (!aoTex) {
        aoTex = FindFallbackTexture(context, materialName, kAOTokens, false, false);
    }
    if (aoTex) {
        result->setAOTexture(aoTex);
    }
    
    auto ormTex = LoadMaterialTexture(context, material, aiTextureType_GLTF_METALLIC_ROUGHNESS, false, false);
    if (!ormTex) {
        ormTex = FindFallbackTexture(context, materialName, kORMTokens, false, false);
    }
    if (ormTex) {
        result->setORMTexture(ormTex);
        if (result->getMetallicTexture() == ormTex) {
            result->setMetallicTexture(nullptr);
        }
        if (result->getRoughnessTexture() == ormTex) {
            result->setRoughnessTexture(nullptr);
        }
    }
    
    auto emissionTex = LoadMaterialTexture(context, material, aiTextureType_EMISSIVE, true, false);
    if (!emissionTex) {
        emissionTex = FindFallbackTexture(context, materialName, kEmissionTokens, true, false);
    }
    if (emissionTex) {
        result->setEmissionTexture(emissionTex);
        Math::Vector3 emission = result->getEmission();
        if (emission.x <= 0.0001f && emission.y <= 0.0001f && emission.z <= 0.0001f) {
            result->setEmission(Math::Vector3(1.0f, 1.0f, 1.0f));
        }
        if (result->getEmissionStrength() <= 0.0f) {
            result->setEmissionStrength(1.0f);
        }
    }
    
    return result;
}

static const aiNode* FindNodeByName(const aiNode* node, const std::string& name) {
    if (!node) {
        return nullptr;
    }
    if (node->mName.length > 0 && name == node->mName.C_Str()) {
        return node;
    }
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        const aiNode* found = FindNodeByName(node->mChildren[i], name);
        if (found) {
            return found;
        }
    }
    return nullptr;
}

struct NodeNameCache {
    std::unordered_map<const aiNode*, std::string> names;
    uint32_t counter = 0;

    const std::string& get(const aiNode* node) {
        auto it = names.find(node);
        if (it != names.end()) {
            return it->second;
        }
        std::string name;
        if (node && node->mName.length > 0) {
            name = node->mName.C_Str();
        } else {
            name = "Node_" + std::to_string(counter++);
        }
        auto inserted = names.emplace(node, name);
        return inserted.first->second;
    }
};

static bool NodeHasSkinnedMesh(const aiNode* node,
                               const std::vector<bool>& meshIsSkinned,
                               std::unordered_map<const aiNode*, bool>& cache) {
    if (!node) {
        return false;
    }
    auto it = cache.find(node);
    if (it != cache.end()) {
        return it->second;
    }
    bool hasSkinned = false;
    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
        unsigned int meshIndex = node->mMeshes[i];
        if (meshIndex < meshIsSkinned.size() && meshIsSkinned[meshIndex]) {
            hasSkinned = true;
            break;
        }
    }
    if (!hasSkinned) {
        for (unsigned int i = 0; i < node->mNumChildren; ++i) {
            if (NodeHasSkinnedMesh(node->mChildren[i], meshIsSkinned, cache)) {
                hasSkinned = true;
                break;
            }
        }
    }
    cache[node] = hasSkinned;
    return hasSkinned;
}

static void BuildSkeletonRecursive(const aiNode* node,
                                   const std::unordered_set<const aiNode*>& requiredNodes,
                                   const std::unordered_map<std::string, aiMatrix4x4>& inverseBindByName,
                                   NodeNameCache& nameCache,
                                   Skeleton& skeleton,
                                   int parentIndex) {
    if (!node) {
        return;
    }
    int nextParent = parentIndex;
    if (requiredNodes.find(node) != requiredNodes.end()) {
        const std::string& nodeName = nameCache.get(node);
        Math::Matrix4x4 localBind = ToMatrix(node->mTransformation);
        Math::Matrix4x4 inverseBind = Math::Matrix4x4::Identity;
        auto invIt = inverseBindByName.find(nodeName);
        if (invIt != inverseBindByName.end()) {
            inverseBind = ToMatrix(invIt->second);
        }
        uint32_t index = skeleton.addBone(nodeName, parentIndex, localBind, inverseBind);
        nextParent = static_cast<int>(index);
    }
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        BuildSkeletonRecursive(node->mChildren[i], requiredNodes, inverseBindByName, nameCache, skeleton, nextParent);
    }
}

static std::shared_ptr<Skeleton> BuildSkeleton(const aiScene* scene) {
    if (!scene || !scene->mRootNode) {
        return nullptr;
    }
    std::unordered_set<std::string> boneNames;
    std::unordered_map<std::string, aiMatrix4x4> inverseBindByName;
    for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
        const aiMesh* mesh = scene->mMeshes[i];
        if (!mesh || mesh->mNumBones == 0) {
            continue;
        }
        for (unsigned int b = 0; b < mesh->mNumBones; ++b) {
            const aiBone* bone = mesh->mBones[b];
            if (!bone || bone->mName.length == 0) {
                continue;
            }
            std::string name = bone->mName.C_Str();
            boneNames.insert(name);
            if (inverseBindByName.find(name) == inverseBindByName.end()) {
                inverseBindByName[name] = bone->mOffsetMatrix;
            }
        }
    }
    if (boneNames.empty()) {
        return nullptr;
    }

    std::unordered_set<const aiNode*> requiredNodes;
    for (const auto& name : boneNames) {
        const aiNode* boneNode = FindNodeByName(scene->mRootNode, name);
        const aiNode* node = boneNode;
        while (node) {
            requiredNodes.insert(node);
            node = node->mParent;
        }
    }

    auto skeleton = std::make_shared<Skeleton>();
    Math::Matrix4x4 rootTransform = ToMatrix(scene->mRootNode->mTransformation);
    skeleton->setGlobalInverse(rootTransform.inversed());
    NodeNameCache nameCache;
    BuildSkeletonRecursive(scene->mRootNode, requiredNodes, inverseBindByName, nameCache, *skeleton, -1);
    return skeleton;
}

static void DecomposeTRS(const Math::Matrix4x4& matrix,
                         Math::Vector3& outPos,
                         Math::Quaternion& outRot,
                         Math::Vector3& outScale) {
    outPos = Math::Vector3(matrix.m[12], matrix.m[13], matrix.m[14]);

    Math::Vector3 col0(matrix.m[0], matrix.m[1], matrix.m[2]);
    Math::Vector3 col1(matrix.m[4], matrix.m[5], matrix.m[6]);
    Math::Vector3 col2(matrix.m[8], matrix.m[9], matrix.m[10]);

    outScale = Math::Vector3(col0.length(), col1.length(), col2.length());
    if (outScale.x <= 0.0f) outScale.x = 1.0f;
    if (outScale.y <= 0.0f) outScale.y = 1.0f;
    if (outScale.z <= 0.0f) outScale.z = 1.0f;

    Math::Vector3 r0 = col0 / outScale.x;
    Math::Vector3 r1 = col1 / outScale.y;
    Math::Vector3 r2 = col2 / outScale.z;

    const float m00 = r0.x;
    const float m01 = r1.x;
    const float m02 = r2.x;
    const float m10 = r0.y;
    const float m11 = r1.y;
    const float m12 = r2.y;
    const float m20 = r0.z;
    const float m21 = r1.z;
    const float m22 = r2.z;

    const float trace = m00 + m11 + m22;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        outRot.w = 0.25f * s;
        outRot.x = (m21 - m12) / s;
        outRot.y = (m02 - m20) / s;
        outRot.z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        outRot.w = (m21 - m12) / s;
        outRot.x = 0.25f * s;
        outRot.y = (m01 + m10) / s;
        outRot.z = (m02 + m20) / s;
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        outRot.w = (m02 - m20) / s;
        outRot.x = (m01 + m10) / s;
        outRot.y = 0.25f * s;
        outRot.z = (m12 + m21) / s;
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        outRot.w = (m10 - m01) / s;
        outRot.x = (m02 + m20) / s;
        outRot.y = (m12 + m21) / s;
        outRot.z = 0.25f * s;
    }
    outRot.normalize();
}

struct AnimationImportCompatibilityReport {
    bool compatible = true;
    size_t animatedBoneCount = 0;
    size_t matchedAnimatedBoneCount = 0;
    size_t sourceBoneCount = 0;
    size_t matchedSourceBoneCount = 0;
    size_t hierarchyMismatchCount = 0;
    float averagePositionDeltaRatio = 0.0f;
    float averageRotationDeltaDegrees = 0.0f;
};

static AnimationImportCompatibilityReport EvaluateAnimationCompatibility(const aiScene* scene,
                                                                         const Skeleton& targetSkeleton) {
    AnimationImportCompatibilityReport report;
    if (!scene || scene->mNumAnimations == 0 || targetSkeleton.getBoneCount() == 0) {
        report.compatible = false;
        return report;
    }

    std::unordered_set<std::string> animatedBoneNames;
    for (unsigned int i = 0; i < scene->mNumAnimations; ++i) {
        const aiAnimation* anim = scene->mAnimations[i];
        if (!anim) {
            continue;
        }
        for (unsigned int c = 0; c < anim->mNumChannels; ++c) {
            const aiNodeAnim* channel = anim->mChannels[c];
            if (!channel || channel->mNodeName.length == 0) {
                continue;
            }
            animatedBoneNames.insert(channel->mNodeName.C_Str());
        }
    }

    report.animatedBoneCount = animatedBoneNames.size();
    for (const std::string& boneName : animatedBoneNames) {
        if (targetSkeleton.getBoneIndex(boneName) >= 0) {
            ++report.matchedAnimatedBoneCount;
        }
    }

    if (report.animatedBoneCount == 0) {
        report.compatible = false;
        return report;
    }

    const float animatedCoverage =
        static_cast<float>(report.matchedAnimatedBoneCount) / static_cast<float>(report.animatedBoneCount);
    if (animatedCoverage < 0.75f || report.matchedAnimatedBoneCount < std::min<size_t>(report.animatedBoneCount, 3)) {
        report.compatible = false;
        return report;
    }

    std::shared_ptr<Skeleton> sourceSkeleton = BuildSkeleton(scene);
    if (!sourceSkeleton || sourceSkeleton->getBoneCount() == 0) {
        return report;
    }

    report.sourceBoneCount = sourceSkeleton->getBoneCount();
    const auto& sourceBones = sourceSkeleton->getBones();
    const auto& targetBones = targetSkeleton.getBones();

    float totalPositionDeltaRatio = 0.0f;
    float totalRotationDeltaDegrees = 0.0f;
    size_t comparedBindCount = 0;

    for (const Bone& sourceBone : sourceBones) {
        const int targetIndex = targetSkeleton.getBoneIndex(sourceBone.name);
        if (targetIndex < 0 || targetIndex >= static_cast<int>(targetBones.size())) {
            continue;
        }

        ++report.matchedSourceBoneCount;
        const Bone& targetBone = targetBones[static_cast<size_t>(targetIndex)];

        const std::string sourceParentName =
            (sourceBone.parentIndex >= 0 && sourceBone.parentIndex < static_cast<int>(sourceBones.size()))
                ? sourceBones[static_cast<size_t>(sourceBone.parentIndex)].name
                : "";
        const std::string targetParentName =
            (targetBone.parentIndex >= 0 && targetBone.parentIndex < static_cast<int>(targetBones.size()))
                ? targetBones[static_cast<size_t>(targetBone.parentIndex)].name
                : "";
        if (sourceParentName != targetParentName) {
            ++report.hierarchyMismatchCount;
        }

        Math::Vector3 sourcePos;
        Math::Quaternion sourceRot;
        Math::Vector3 sourceScale;
        DecomposeTRS(sourceBone.localBind, sourcePos, sourceRot, sourceScale);

        Math::Vector3 targetPos;
        Math::Quaternion targetRot;
        Math::Vector3 targetScale;
        DecomposeTRS(targetBone.localBind, targetPos, targetRot, targetScale);

        const float referenceLength = std::max(0.01f, std::max(sourcePos.length(), targetPos.length()));
        totalPositionDeltaRatio += (sourcePos - targetPos).length() / referenceLength;

        float rotationDot = std::abs(sourceRot.dot(targetRot));
        rotationDot = std::clamp(rotationDot, -1.0f, 1.0f);
        const float rotationDeltaDegrees = std::acos(rotationDot) * 2.0f * Math::RAD_TO_DEG;
        totalRotationDeltaDegrees += rotationDeltaDegrees;

        ++comparedBindCount;
    }

    if (comparedBindCount > 0) {
        report.averagePositionDeltaRatio = totalPositionDeltaRatio / static_cast<float>(comparedBindCount);
        report.averageRotationDeltaDegrees = totalRotationDeltaDegrees / static_cast<float>(comparedBindCount);
    }

    if (report.sourceBoneCount == 0) {
        return report;
    }

    const float sourceCoverage =
        static_cast<float>(report.matchedSourceBoneCount) / static_cast<float>(report.sourceBoneCount);
    const float hierarchyMismatchRatio = (report.matchedSourceBoneCount > 0)
        ? static_cast<float>(report.hierarchyMismatchCount) / static_cast<float>(report.matchedSourceBoneCount)
        : 1.0f;

    if (sourceCoverage < 0.75f) {
        report.compatible = false;
    }
    if (report.matchedSourceBoneCount >= 6 && hierarchyMismatchRatio > 0.2f) {
        report.compatible = false;
    }
    if (comparedBindCount >= 6 &&
        report.averagePositionDeltaRatio > 0.6f &&
        report.averageRotationDeltaDegrees > 50.0f) {
        report.compatible = false;
    }

    return report;
}

static std::string ToLowerAnimString(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

static bool IsGenericAnimationName(const std::string& value) {
    if (value.empty()) {
        return true;
    }
    std::string lowered = ToLowerAnimString(value);
    return lowered == "mixamo.com" ||
           lowered == "default take" ||
           lowered == "take 001" ||
           lowered == "take001" ||
           lowered == "armature|mixamo.com|layer0" ||
           lowered.rfind("animation_", 0) == 0;
}

static std::string BuildAnimationClipName(const aiAnimation* anim,
                                          unsigned int index,
                                          unsigned int animationCount,
                                          const std::string& sourceNameHint) {
    std::string name = (anim && anim->mName.length > 0) ? anim->mName.C_Str() : "";
    if (IsGenericAnimationName(name)) {
        name.clear();
    }
    if (!name.empty()) {
        return name;
    }
    if (!sourceNameHint.empty()) {
        if (animationCount <= 1) {
            return sourceNameHint;
        }
        return sourceNameHint + "_" + std::to_string(index);
    }
    return "Animation_" + std::to_string(index);
}

static std::vector<std::shared_ptr<AnimationClip>> BuildAnimations(const aiScene* scene,
                                                                   const Skeleton& skeleton,
                                                                   const std::string& sourceNameHint = "") {
    std::vector<std::shared_ptr<AnimationClip>> clips;
    if (!scene || scene->mNumAnimations == 0) {
        return clips;
    }
    clips.reserve(scene->mNumAnimations);
    for (unsigned int i = 0; i < scene->mNumAnimations; ++i) {
        const aiAnimation* anim = scene->mAnimations[i];
        if (!anim) {
            continue;
        }
        auto clip = std::make_shared<AnimationClip>();
        clip->setName(BuildAnimationClipName(anim, i, scene->mNumAnimations, sourceNameHint));
        clip->setDurationTicks(static_cast<float>(anim->mDuration));
        float ticksPerSecond = static_cast<float>(anim->mTicksPerSecond);
        if (ticksPerSecond <= 0.0f) {
            ticksPerSecond = 25.0f;
        }
        clip->setTicksPerSecond(ticksPerSecond);

        for (unsigned int c = 0; c < anim->mNumChannels; ++c) {
            const aiNodeAnim* channel = anim->mChannels[c];
            if (!channel || channel->mNodeName.length == 0) {
                continue;
            }
            std::string boneName = channel->mNodeName.C_Str();
            int boneIndex = skeleton.getBoneIndex(boneName);
            if (boneIndex < 0) {
                continue;
            }
            AnimationChannel channelData;
            channelData.boneName = boneName;
            channelData.boneIndex = boneIndex;

            channelData.positionKeys.reserve(channel->mNumPositionKeys);
            for (unsigned int k = 0; k < channel->mNumPositionKeys; ++k) {
                const aiVector3D& v = channel->mPositionKeys[k].mValue;
                VectorKeyframe key;
                key.time = static_cast<float>(channel->mPositionKeys[k].mTime);
                key.value = Math::Vector3(v.x, v.y, v.z);
                channelData.positionKeys.push_back(key);
            }

            channelData.rotationKeys.reserve(channel->mNumRotationKeys);
            for (unsigned int k = 0; k < channel->mNumRotationKeys; ++k) {
                const aiQuaternion& q = channel->mRotationKeys[k].mValue;
                QuaternionKeyframe key;
                key.time = static_cast<float>(channel->mRotationKeys[k].mTime);
                key.value = Math::Quaternion(q.x, q.y, q.z, q.w);
                channelData.rotationKeys.push_back(key);
            }

            channelData.scaleKeys.reserve(channel->mNumScalingKeys);
            for (unsigned int k = 0; k < channel->mNumScalingKeys; ++k) {
                const aiVector3D& s = channel->mScalingKeys[k].mValue;
                VectorKeyframe key;
                key.time = static_cast<float>(channel->mScalingKeys[k].mTime);
                key.value = Math::Vector3(s.x, s.y, s.z);
                channelData.scaleKeys.push_back(key);
            }

            clip->addChannel(channelData);
        }

        clips.push_back(clip);
    }
    return clips;
}

static void AddBoneWeight(SkinWeight& weight, uint32_t boneIndex, float value) {
    if (value <= 0.0f) {
        return;
    }
    int slot = -1;
    for (int i = 0; i < 4; ++i) {
        if (weight.weights[i] <= 0.0f) {
            slot = i;
            break;
        }
    }
    if (slot == -1) {
        float minWeight = weight.weights[0];
        int minIndex = 0;
        for (int i = 1; i < 4; ++i) {
            if (weight.weights[i] < minWeight) {
                minWeight = weight.weights[i];
                minIndex = i;
            }
        }
        if (value <= minWeight) {
            return;
        }
        slot = minIndex;
    }
    weight.indices[slot] = boneIndex;
    weight.weights[slot] = value;
}

static void NormalizeWeights(SkinWeight& weight) {
    float sum = 0.0f;
    for (float v : weight.weights) {
        sum += v;
    }
    if (sum <= 0.0f) {
        return;
    }
    for (float& v : weight.weights) {
        v /= sum;
    }
}

static void ApplySkinWeights(const aiMesh* mesh, Mesh& result, const Skeleton& skeleton) {
    if (!mesh || !mesh->HasBones()) {
        return;
    }
    std::vector<SkinWeight> weights(mesh->mNumVertices);
    for (unsigned int b = 0; b < mesh->mNumBones; ++b) {
        const aiBone* bone = mesh->mBones[b];
        if (!bone || bone->mName.length == 0) {
            continue;
        }
        int boneIndex = skeleton.getBoneIndex(bone->mName.C_Str());
        if (boneIndex < 0) {
            continue;
        }
        for (unsigned int w = 0; w < bone->mNumWeights; ++w) {
            const aiVertexWeight& vw = bone->mWeights[w];
            if (vw.mVertexId >= weights.size()) {
                continue;
            }
            AddBoneWeight(weights[vw.mVertexId], static_cast<uint32_t>(boneIndex), vw.mWeight);
        }
    }
    for (auto& weight : weights) {
        NormalizeWeights(weight);
    }
    result.setSkinWeights(weights);
}

static std::shared_ptr<Mesh> BuildMesh(const aiMesh* mesh, const Skeleton* skeleton) {
    auto result = std::make_shared<Mesh>();
    if (!mesh) {
        return result;
    }
    
    if (mesh->mName.length > 0) {
        result->setName(mesh->mName.C_Str());
    }
    
    std::vector<Vertex> vertices;
    vertices.reserve(mesh->mNumVertices);
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        Vertex vertex;
        vertex.position = Math::Vector3(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z);
        
        if (mesh->HasNormals()) {
            vertex.normal = Math::Vector3(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);
        } else {
            vertex.normal = Math::Vector3::Up;
        }
        
        if (mesh->HasTextureCoords(0) && mesh->mTextureCoords[0]) {
            vertex.texCoord = Math::Vector2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y);
        } else {
            vertex.texCoord = Math::Vector2::Zero;
        }
        if (mesh->HasTextureCoords(1) && mesh->mTextureCoords[1]) {
            vertex.texCoord1 = Math::Vector2(mesh->mTextureCoords[1][i].x, mesh->mTextureCoords[1][i].y);
        } else {
            vertex.texCoord1 = vertex.texCoord;
        }
        
        if (mesh->HasTangentsAndBitangents()) {
            vertex.tangent = Math::Vector3(mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z);
            vertex.bitangent = Math::Vector3(mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z);
        } else {
            vertex.tangent = Math::Vector3::Zero;
            vertex.bitangent = Math::Vector3::Zero;
        }
        
        if (mesh->HasVertexColors(0) && mesh->mColors[0]) {
            vertex.color = Math::Vector4(mesh->mColors[0][i].r, mesh->mColors[0][i].g, mesh->mColors[0][i].b, mesh->mColors[0][i].a);
        } else {
            vertex.color = Math::Vector4::One;
        }
        
        vertices.push_back(vertex);
    }
    
    std::vector<uint32_t> indices;
    indices.reserve(mesh->mNumFaces * 3);
    for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
        const aiFace& face = mesh->mFaces[i];
        if (face.mNumIndices != 3) {
            continue;
        }
        indices.push_back(face.mIndices[0]);
        indices.push_back(face.mIndices[1]);
        indices.push_back(face.mIndices[2]);
    }
    
    result->setVertices(vertices);
    result->setIndices(indices);
    
    if (!mesh->HasNormals()) {
        result->calculateNormals();
    }
    if (!mesh->HasTangentsAndBitangents() && mesh->HasTextureCoords(0)) {
        result->calculateTangents();
    }

    if (skeleton && mesh->HasBones()) {
        ApplySkinWeights(mesh, *result, *skeleton);
    }
    
    return result;
}

struct MergedStaticMesh {
    unsigned int materialIndex = 0;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

static void AppendTransformedMesh(const Mesh& source,
                                  const Math::Matrix4x4& transform,
                                  MergedStaticMesh& target) {
    const auto& verts = source.getVertices();
    const auto& indices = source.getIndices();
    if (verts.empty() || indices.empty()) {
        return;
    }

    Math::Matrix4x4 normalMatrix = transform.normalMatrix();
    uint32_t indexOffset = static_cast<uint32_t>(target.vertices.size());
    target.vertices.reserve(target.vertices.size() + verts.size());
    target.indices.reserve(target.indices.size() + indices.size());

    for (const auto& v : verts) {
        Vertex out = v;
        out.position = transform.transformPoint(v.position);
        out.normal = normalMatrix.transformDirection(v.normal).normalized();
        out.tangent = normalMatrix.transformDirection(v.tangent).normalized();
        out.bitangent = normalMatrix.transformDirection(v.bitangent).normalized();
        target.vertices.push_back(out);
    }

    for (uint32_t idx : indices) {
        target.indices.push_back(indexOffset + idx);
    }
}

static void BuildStaticMergeRecursive(const aiNode* node,
                                      const Math::Matrix4x4& parentWorld,
                                      const Math::Matrix4x4& rootInverse,
                                      ImportContext& context,
                                      const std::vector<bool>& meshIsSkinned,
                                      std::unordered_map<unsigned int, MergedStaticMesh>& merged) {
    if (!node) {
        return;
    }

    Math::Matrix4x4 local = ToMatrix(node->mTransformation);
    Math::Matrix4x4 nodeWorld = parentWorld * local;
    Math::Matrix4x4 toRootLocal = rootInverse * nodeWorld;

    std::string nodeName = SafeName(node->mName, "Node");
    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
        unsigned int meshIndex = node->mMeshes[i];
        if (meshIndex >= context.meshes.size()) {
            continue;
        }
        if (meshIndex < meshIsSkinned.size() && meshIsSkinned[meshIndex]) {
            continue;
        }
        std::shared_ptr<Mesh> mesh = context.meshes[meshIndex];
        if (!mesh) {
            continue;
        }
        std::string meshName = mesh->getName();
        if (meshName.empty() || meshName == "Mesh") {
            meshName = nodeName + "_Mesh";
            if (node->mNumMeshes > 1) {
                meshName += "_" + std::to_string(i);
            }
        }
        std::string combinedName = nodeName + " " + meshName;
        if (!ShouldImportLOD(combinedName, context.options.onlyLOD0)) {
            continue;
        }

        unsigned int materialIndex = 0;
        if (context.aiScene && meshIndex < context.aiScene->mNumMeshes) {
            materialIndex = context.aiScene->mMeshes[meshIndex]->mMaterialIndex;
        }
        auto& target = merged[materialIndex];
        target.materialIndex = materialIndex;
        AppendTransformedMesh(*mesh, toRootLocal, target);
    }

    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        BuildStaticMergeRecursive(node->mChildren[i], nodeWorld, rootInverse, context, meshIsSkinned, merged);
    }
}

static void ApplyNodeTransform(Transform* transform, const aiMatrix4x4& matrix) {
    if (!transform) {
        return;
    }
    aiVector3D scaling;
    aiQuaternion rotation;
    aiVector3D position;
    matrix.Decompose(scaling, rotation, position);
    transform->setLocalPosition(Math::Vector3(position.x, position.y, position.z));
    transform->setLocalRotation(Math::Quaternion(rotation.x, rotation.y, rotation.z, rotation.w));
    transform->setLocalScale(Math::Vector3(scaling.x, scaling.y, scaling.z));
}

static void ImportNodeRecursive(const aiNode* node,
                                ImportContext& context,
                                Entity* parent,
                                Entity* existing,
                                const std::vector<bool>* meshIsSkinned,
                                std::unordered_map<const aiNode*, bool>* skinnedNodeCache) {
    if (!node || !context.scene) {
        return;
    }

    if (context.options.mergeStaticMeshes && meshIsSkinned && skinnedNodeCache) {
        bool keepNode = existing != nullptr ||
                        NodeHasSkinnedMesh(node, *meshIsSkinned, *skinnedNodeCache);
        if (!keepNode) {
            return;
        }
    }
    
    std::string nodeName = SafeName(node->mName, "Node");
    Entity* nodeEntity = existing ? existing : context.scene->createEntity(nodeName);
    if (!existing && parent) {
        nodeEntity->getTransform()->setParent(parent->getTransform(), false);
    }
    ApplyNodeTransform(nodeEntity->getTransform(), node->mTransformation);
    
    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
        unsigned int meshIndex = node->mMeshes[i];
        if (meshIndex >= context.meshes.size()) {
            continue;
        }
        if (context.options.mergeStaticMeshes && meshIsSkinned &&
            meshIndex < meshIsSkinned->size() && !(*meshIsSkinned)[meshIndex]) {
            continue;
        }
        std::shared_ptr<Mesh> mesh = context.meshes[meshIndex];
        unsigned int materialIndex = 0;
        std::shared_ptr<Material> material = Material::CreateDefault();
        if (context.aiScene && meshIndex < context.aiScene->mNumMeshes) {
            materialIndex = context.aiScene->mMeshes[meshIndex]->mMaterialIndex;
            if (materialIndex < context.materials.size()) {
                material = context.materials[materialIndex];
            }
        }
        
        std::string meshName = mesh->getName();
        if (meshName.empty() || meshName == "Mesh") {
            meshName = nodeName + "_Mesh";
            if (node->mNumMeshes > 1) {
                meshName += "_" + std::to_string(i);
            }
        }
        std::string combinedName = nodeName + " " + meshName;
        if (!ShouldImportLOD(combinedName, context.options.onlyLOD0)) {
            continue;
        }
        Entity* meshEntity = context.scene->createEntity(meshName);
        meshEntity->getTransform()->setParent(nodeEntity->getTransform(), false);
        MeshRenderer* renderer = meshEntity->addComponent<MeshRenderer>();
        renderer->setMesh(mesh);
        renderer->setMaterial(material);
        if (mesh->hasSkinWeights() && context.skeleton) {
            SkinnedMeshRenderer* skinned = meshEntity->addComponent<SkinnedMeshRenderer>();
            skinned->setMesh(mesh);
            skinned->setMaterial(material);
            skinned->setSkeleton(context.skeleton);
            if (!context.animations.empty()) {
                skinned->setAnimationClips(context.animations);
                skinned->setPlaying(true);
            }
        }
        
        ModelMeshReference* reference = meshEntity->addComponent<ModelMeshReference>();
        reference->setSourcePath(context.sourcePath);
        reference->setSourceGuid(AssetDatabase::getInstance().registerAsset(context.sourcePath, "model"));
        reference->setMeshIndex(static_cast<int>(meshIndex));
        reference->setMaterialIndex(static_cast<int>(materialIndex));
        reference->setMeshName(meshName);
        reference->setSkinned(mesh->hasSkinWeights());
        reference->setImportOptions(context.options);
    }
    
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        ImportNodeRecursive(node->mChildren[i], context, nodeEntity, nullptr, meshIsSkinned, skinnedNodeCache);
    }
}

} // namespace

Entity* SceneCommands::createCube(Scene* scene, const std::string& name) {
    if (!scene) return nullptr;
    
    Entity* entity = scene->createEntity(name);
    
    // Add MeshRenderer
    MeshRenderer* renderer = entity->addComponent<MeshRenderer>();
    renderer->setMesh(Mesh::CreateCube(1.0f));
    renderer->setMaterial(Material::CreateDefault());
    ConfigurePrimitiveStaticLighting(renderer);
    entity->addComponent<PrimitiveMesh>()->setType(PrimitiveType::Cube);
    
    return entity;
}

Entity* SceneCommands::createSphere(Scene* scene, const std::string& name) {
    if (!scene) return nullptr;
    
    Entity* entity = scene->createEntity(name);
    
    MeshRenderer* renderer = entity->addComponent<MeshRenderer>();
    renderer->setMesh(Mesh::CreateSphere(0.5f, 32, 16));
    renderer->setMaterial(Material::CreateDefault());
    ConfigurePrimitiveStaticLighting(renderer);
    entity->addComponent<PrimitiveMesh>()->setType(PrimitiveType::Sphere);
    
    return entity;
}

Entity* SceneCommands::createPlane(Scene* scene, const std::string& name) {
    if (!scene) return nullptr;
    
    Entity* entity = scene->createEntity(name);
    
    MeshRenderer* renderer = entity->addComponent<MeshRenderer>();
    renderer->setMesh(Mesh::CreatePlane(10.0f, 10.0f, 1, 1));
    renderer->setMaterial(Material::CreateDefault());
    ConfigurePrimitiveStaticLighting(renderer);
    entity->addComponent<PrimitiveMesh>()->setType(PrimitiveType::Plane);
    
    return entity;
}

Entity* SceneCommands::createCylinder(Scene* scene, const std::string& name) {
    if (!scene) return nullptr;
    
    Entity* entity = scene->createEntity(name);
    
    MeshRenderer* renderer = entity->addComponent<MeshRenderer>();
    renderer->setMesh(Mesh::CreateCylinder(0.5f, 1.0f, 32));
    renderer->setMaterial(Material::CreateDefault());
    ConfigurePrimitiveStaticLighting(renderer);
    entity->addComponent<PrimitiveMesh>()->setType(PrimitiveType::Cylinder);
    
    return entity;
}

Entity* SceneCommands::createCone(Scene* scene, const std::string& name) {
    if (!scene) return nullptr;
    
    Entity* entity = scene->createEntity(name);
    
    MeshRenderer* renderer = entity->addComponent<MeshRenderer>();
    renderer->setMesh(Mesh::CreateCone(0.5f, 1.0f, 32));
    renderer->setMaterial(Material::CreateDefault());
    ConfigurePrimitiveStaticLighting(renderer);
    entity->addComponent<PrimitiveMesh>()->setType(PrimitiveType::Cone);
    
    return entity;
}

Entity* SceneCommands::createTorus(Scene* scene, const std::string& name) {
    if (!scene) return nullptr;
    
    Entity* entity = scene->createEntity(name);
    
    MeshRenderer* renderer = entity->addComponent<MeshRenderer>();
    renderer->setMesh(Mesh::CreateTorus(0.75f, 0.25f, 32, 16));
    renderer->setMaterial(Material::CreateDefault());
    ConfigurePrimitiveStaticLighting(renderer);
    entity->addComponent<PrimitiveMesh>()->setType(PrimitiveType::Torus);
    
    return entity;
}

Entity* SceneCommands::createCapsule(Scene* scene, const std::string& name) {
    if (!scene) return nullptr;
    
    Entity* entity = scene->createEntity(name);
    
    MeshRenderer* renderer = entity->addComponent<MeshRenderer>();
    renderer->setMesh(Mesh::CreateCapsule(0.5f, 2.0f, 16));
    renderer->setMaterial(Material::CreateDefault());
    ConfigurePrimitiveStaticLighting(renderer);
    entity->addComponent<PrimitiveMesh>()->setType(PrimitiveType::Capsule);
    
    return entity;
}

Entity* SceneCommands::createDirectionalLight(Scene* scene, const std::string& name) {
    if (!scene) return nullptr;
    
    Entity* entity = scene->createEntity(name);
    
    Light* light = entity->addComponent<Light>();
    light->setType(Light::Type::Directional);
    light->setColor(Math::Vector3(1.0f, 1.0f, 1.0f));
    light->setIntensity(1.0f);
    
    // Rotate to look down
    entity->getTransform()->setRotation(
        Math::Quaternion::FromEulerAngles(Math::Vector3(-45.0f * Math::DEG_TO_RAD, 45.0f * Math::DEG_TO_RAD, 0))
    );
    
    return entity;
}

Entity* SceneCommands::createPointLight(Scene* scene, const std::string& name) {
    if (!scene) return nullptr;
    
    Entity* entity = scene->createEntity(name);
    
    Light* light = entity->addComponent<Light>();
    light->setType(Light::Type::Point);
    light->setColor(Math::Vector3(1.0f, 1.0f, 1.0f));
    light->setIntensity(800.0f);
    light->setRange(15.0f);
    // Horror / practical-light workflows rely on local-light occlusion,
    // so keep shadows enabled out of the box with a conservative default.
    light->setCastShadows(true);
    light->setShadowMapResolution(512);
    
    entity->getTransform()->setPosition(Math::Vector3(0, 3, 0));
    
    return entity;
}

Entity* SceneCommands::createSpotLight(Scene* scene, const std::string& name) {
    if (!scene) return nullptr;
    
    Entity* entity = scene->createEntity(name);
    
    Light* light = entity->addComponent<Light>();
    light->setType(Light::Type::Spot);
    light->setColor(Math::Vector3(1.0f, 1.0f, 1.0f));
    light->setIntensity(1200.0f);
    light->setRange(20.0f);
    light->setSpotAngle(45.0f);
    light->setCastShadows(true);
    light->setShadowMapResolution(512);
    
    entity->getTransform()->setPosition(Math::Vector3(0, 5, 0));
    entity->getTransform()->setRotation(
        Math::Quaternion::FromEulerAngles(Math::Vector3(-90.0f * Math::DEG_TO_RAD, 0, 0))
    );
    
    return entity;
}

Entity* SceneCommands::createDecal(Scene* scene, const std::string& name) {
    if (!scene) return nullptr;

    Entity* entity = scene->createEntity(name);
    Decal* decal = entity->addComponent<Decal>();
    decal->setOpacity(1.0f);
    decal->setEdgeSoftness(0.05f);

    entity->getTransform()->setLocalScale(Math::Vector3(1.0f, 1.0f, 1.0f));
    return entity;
}

Entity* SceneCommands::createCamera(Scene* scene, const std::string& name) {
    if (!scene) return nullptr;
    
    Entity* entity = scene->createEntity(name);
    
    Camera* camera = entity->addComponent<Camera>();
    camera->setFieldOfView(60.0f * Math::DEG_TO_RAD);
    camera->setClearColor(Math::Vector4(0.1f, 0.1f, 0.15f, 1.0f));
    
    entity->getTransform()->setPosition(Math::Vector3(0, 2, 5));
    entity->getTransform()->setRotation(
        Math::Quaternion::FromEulerAngles(Math::Vector3(-15.0f * Math::DEG_TO_RAD, 0, 0))
    );
    
    return entity;
}

Entity* SceneCommands::createEmpty(Scene* scene, const std::string& name) {
    if (!scene) return nullptr;
    
    Entity* entity = scene->createEntity(name);
    return entity;
}

Entity* SceneCommands::importModel(Scene* scene, const std::string& path, const ModelImportOptions& options, const std::string& name) {
    if (!scene) return nullptr;
    if (path.empty()) {
        std::cerr << "[ModelImporter] Empty model path" << std::endl;
        return nullptr;
    }
    
    Assimp::Importer importer;
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
    if (options.scale > 0.0f && options.scale != 1.0f) {
        importer.SetPropertyFloat(AI_CONFIG_GLOBAL_SCALE_FACTOR_KEY, options.scale);
    }
    unsigned int flags = aiProcess_Triangulate |
                         aiProcess_GenSmoothNormals |
                         aiProcess_CalcTangentSpace |
                         aiProcess_JoinIdenticalVertices |
                         aiProcess_ImproveCacheLocality |
                         aiProcess_OptimizeMeshes |
                         aiProcess_OptimizeGraph |
                         aiProcess_GlobalScale;
    if (options.flipUVs) {
        flags |= aiProcess_FlipUVs;
    }
    
    const aiScene* aiScene = importer.ReadFile(path, flags);
    if (!aiScene || !aiScene->mRootNode) {
        std::cerr << "[ModelImporter] Failed to read: " << path << " (" << importer.GetErrorString() << ")" << std::endl;
        return nullptr;
    }
    
    ImportContext context;
    context.scene = scene;
    context.aiScene = aiScene;
    context.baseDir = std::filesystem::path(path).parent_path().string();
    context.sourcePath = path;
    context.options = options;
    if (context.baseDir.empty()) {
        context.baseDir = ".";
    }

    std::string guid = AssetDatabase::getInstance().registerAsset(path, "model");
    if (!guid.empty()) {
        AssetDatabase::getInstance().updateModelImportSettings(guid, options);
    }
    
    Renderer* renderer = Engine::getInstance().getRenderer();
    context.textureLoader = renderer ? renderer->getTextureLoader() : nullptr;
    if (!context.textureLoader) {
        std::cerr << "[ModelImporter] Texture loader unavailable; importing without textures" << std::endl;
    }

    context.skeleton = BuildSkeleton(aiScene);
    if (context.skeleton) {
        context.animations = BuildAnimations(aiScene, *context.skeleton, std::filesystem::path(path).stem().string());
    }
    
    context.meshes.reserve(aiScene->mNumMeshes);
    std::vector<bool> meshIsSkinned;
    meshIsSkinned.reserve(aiScene->mNumMeshes);
    for (unsigned int i = 0; i < aiScene->mNumMeshes; ++i) {
        aiMesh* aiMeshPtr = aiScene->mMeshes[i];
        context.meshes.push_back(BuildMesh(aiMeshPtr, context.skeleton.get()));
        meshIsSkinned.push_back(aiMeshPtr && aiMeshPtr->HasBones());
    }
    
    context.materials.reserve(aiScene->mNumMaterials);
    for (unsigned int i = 0; i < aiScene->mNumMaterials; ++i) {
        context.materials.push_back(BuildMaterial(context, aiScene->mMaterials[i]));
    }
    if (context.materials.empty()) {
        context.materials.push_back(Material::CreateDefault());
    }
    
    std::string rootName = name;
    if (rootName.empty()) {
        rootName = std::filesystem::path(path).stem().string();
        if (rootName.empty()) {
            rootName = SafeName(aiScene->mRootNode->mName, "Model");
        }
    }
    
    Entity* root = scene->createEntity(rootName);
    std::unordered_map<const aiNode*, bool> skinnedNodeCache;
    ImportNodeRecursive(aiScene->mRootNode, context, nullptr, root, &meshIsSkinned, &skinnedNodeCache);

    if (context.skeleton && !context.animations.empty()) {
        Animator* animator = root->addComponent<Animator>();
        std::vector<AnimatorState> states;
        states.reserve(context.animations.size());
        for (size_t clipIndex = 0; clipIndex < context.animations.size(); ++clipIndex) {
            std::string clipName = context.animations[clipIndex]
                ? context.animations[clipIndex]->getName()
                : "";
            if (clipName.empty()) {
                clipName = "Clip " + std::to_string(clipIndex);
            }
            AnimatorState state;
            state.name = clipName;
            state.clipIndex = static_cast<int>(clipIndex);
            state.speed = 1.0f;
            states.push_back(state);
        }
        animator->setStates(states);
        animator->setDefaultBlendDuration(0.25f);
        animator->setAutoPlay(true);
    }

    if (context.options.mergeStaticMeshes) {
        std::unordered_map<unsigned int, MergedStaticMesh> merged;
        Math::Matrix4x4 rootWorld = ToMatrix(GetNodeWorldTransform(aiScene->mRootNode));
        Math::Matrix4x4 rootInverse = rootWorld.inversed();
        BuildStaticMergeRecursive(aiScene->mRootNode,
                                  Math::Matrix4x4::Identity,
                                  rootInverse,
                                  context,
                                  meshIsSkinned,
                                  merged);

        for (auto& entry : merged) {
            MergedStaticMesh& mergedMesh = entry.second;
            if (mergedMesh.vertices.empty() || mergedMesh.indices.empty()) {
                continue;
            }
            auto combined = std::make_shared<Mesh>();
            combined->setName(rootName + "_Static");
            combined->setVertices(mergedMesh.vertices);
            combined->setIndices(mergedMesh.indices);

            std::shared_ptr<Material> material = Material::CreateDefault();
            if (mergedMesh.materialIndex < context.materials.size()) {
                material = context.materials[mergedMesh.materialIndex];
            }

            std::string entityName = rootName + " Static";
            if (material && !material->getName().empty()) {
                entityName = rootName + " " + material->getName();
            }
            Entity* mergedEntity = scene->createEntity(entityName);
            mergedEntity->getTransform()->setParent(root->getTransform(), false);
            MeshRenderer* renderer = mergedEntity->addComponent<MeshRenderer>();
            renderer->setMesh(combined);
            renderer->setMaterial(material);
            ModelMeshReference* reference = mergedEntity->addComponent<ModelMeshReference>();
            reference->setSourcePath(context.sourcePath);
            reference->setSourceGuid(AssetDatabase::getInstance().registerAsset(context.sourcePath, "model"));
            reference->setMeshIndex(-1);
            reference->setMaterialIndex(static_cast<int>(mergedMesh.materialIndex));
            reference->setMeshName(combined->getName());
            reference->setSkinned(false);
            reference->setMerged(true);
            reference->setImportOptions(context.options);
        }
    }
    
    std::cout << "[ModelImporter] Imported " << context.meshes.size() << " mesh(es) from " << path << std::endl;
    if (!guid.empty()) {
        AssetDatabase::getInstance().recordImportForGuid(guid);
    }
    return root;
}

std::vector<std::shared_ptr<AnimationClip>> SceneCommands::importAnimationClipsForSkeleton(
    const std::string& path,
    const Skeleton& skeleton,
    const ModelImportOptions& options) {
    if (path.empty() || skeleton.getBoneCount() == 0) {
        return {};
    }

    Assimp::Importer importer;
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
    if (options.scale > 0.0f && options.scale != 1.0f) {
        importer.SetPropertyFloat(AI_CONFIG_GLOBAL_SCALE_FACTOR_KEY, options.scale);
    }

    const unsigned int flags = aiProcess_GlobalScale | aiProcess_ValidateDataStructure;
    const aiScene* aiScene = importer.ReadFile(path, flags);
    if (!aiScene || aiScene->mNumAnimations == 0) {
        std::cerr << "[AnimationImporter] Failed to read clips: " << path
                  << " (" << importer.GetErrorString() << ")" << std::endl;
        return {};
    }

    const AnimationImportCompatibilityReport compatibility = EvaluateAnimationCompatibility(aiScene, skeleton);
    if (!compatibility.compatible) {
        std::cerr << "[AnimationImporter] Rejected incompatible animation source: " << path
                  << " | animated matches " << compatibility.matchedAnimatedBoneCount << "/"
                  << compatibility.animatedBoneCount
                  << " | skeleton matches " << compatibility.matchedSourceBoneCount << "/"
                  << compatibility.sourceBoneCount
                  << " | hierarchy mismatches " << compatibility.hierarchyMismatchCount
                  << " | avg bind pos ratio " << compatibility.averagePositionDeltaRatio
                  << " | avg bind rot deg " << compatibility.averageRotationDeltaDegrees
                  << std::endl;
        return {};
    }

    std::string sourceName = std::filesystem::path(path).stem().string();
    return BuildAnimations(aiScene, skeleton, sourceName);
}

bool SceneCommands::reimportModelAsset(Scene* scene, const std::string& guid) {
    if (!scene || guid.empty()) {
        return false;
    }
    AssetDatabase& db = AssetDatabase::getInstance();
    AssetRecord record;
    if (!db.getRecordForGuid(guid, record)) {
        return false;
    }
    if (record.type != "model") {
        return false;
    }
    std::string path = db.getPathForGuid(guid);
    if (path.empty()) {
        return false;
    }

    ModelCacheEntry cache = BuildModelCache(path, record.modelSettings);
    bool updated = false;

    for (const auto& entityPtr : scene->getAllEntities()) {
        Entity* entity = entityPtr.get();
        if (!entity) {
            continue;
        }
        ModelMeshReference* reference = entity->getComponent<ModelMeshReference>();
        if (!reference || reference->getSourceGuid() != guid) {
            continue;
        }

        MeshCacheEntry entry;
        bool found = false;
        if (reference->isMerged()) {
            auto it = cache.mergedByMaterial.find(reference->getMaterialIndex());
            if (it != cache.mergedByMaterial.end()) {
                entry = it->second;
                found = true;
            }
        } else {
            auto it = cache.meshesByIndex.find(reference->getMeshIndex());
            if (it != cache.meshesByIndex.end()) {
                entry = it->second;
                found = true;
            }
        }

        if (!found) {
            continue;
        }

        if (auto* renderer = entity->getComponent<MeshRenderer>()) {
            renderer->setMesh(entry.mesh);
            renderer->setMaterial(entry.material);
        }

        if (auto* skinned = entity->getComponent<SkinnedMeshRenderer>()) {
            skinned->setMesh(entry.mesh);
            skinned->setMaterial(entry.material);
            if (cache.skeleton) {
                skinned->setSkeleton(cache.skeleton);
            }
            if (!cache.animations.empty()) {
                skinned->setAnimationClips(cache.animations);
            }
        }

        reference->setSourcePath(path);
        reference->setImportOptions(record.modelSettings);
        updated = true;
    }

    if (updated) {
        AssetDatabase::getInstance().recordImportForGuid(guid);
    }
    return updated;
}

bool SceneCommands::reimportTextureAsset(Scene* scene, const std::string& guid) {
    if (!scene || guid.empty()) {
        return false;
    }
    AssetDatabase& db = AssetDatabase::getInstance();
    AssetRecord record;
    if (!db.getRecordForGuid(guid, record)) {
        return false;
    }
    if (record.type != "texture") {
        return false;
    }
    std::string path = db.getPathForGuid(guid);
    if (path.empty()) {
        return false;
    }

    Renderer* renderer = Engine::getInstance().getRenderer();
    if (!renderer) {
        return false;
    }
    TextureLoader* loader = renderer->getTextureLoader();
    if (!loader) {
        return false;
    }

    bool usesAsTerrainControl = false;
    for (const auto& entityPtr : scene->getAllEntities()) {
        Entity* entity = entityPtr.get();
        if (!entity) {
            continue;
        }

        auto scanMaterials = [&](auto* rendererComp) {
            if (!rendererComp) {
                return;
            }
            for (const auto& material : rendererComp->getMaterials()) {
                if (material && material->getTerrainControlTexture() &&
                    material->getTerrainControlTexture()->getPath() == path) {
                    usesAsTerrainControl = true;
                    return;
                }
            }
        };

        scanMaterials(entity->getComponent<MeshRenderer>());
        if (!usesAsTerrainControl) {
            scanMaterials(entity->getComponent<SkinnedMeshRenderer>());
        }
        if (!usesAsTerrainControl) {
            scanMaterials(entity->getComponent<InstancedMeshRenderer>());
        }
        if (usesAsTerrainControl) {
            break;
        }
    }

    loader->invalidateTexture(path);
    bool srgb = record.textureSettings.srgb;
    if (record.textureSettings.normalMap) {
        srgb = false;
    }
    auto texture = loader->loadTexture(path, srgb, record.textureSettings.flipY, record.textureSettings.normalMap);
    if (!texture) {
        return false;
    }

    std::shared_ptr<Texture2D> terrainControlTexture;
    if (usesAsTerrainControl) {
        terrainControlTexture = loader->loadTextureUncompressed(path, false, false);
        if (!terrainControlTexture) {
            return false;
        }
    }

    bool updated = false;
    for (const auto& entityPtr : scene->getAllEntities()) {
        Entity* entity = entityPtr.get();
        if (!entity) {
            continue;
        }

        if (auto* rendererComp = entity->getComponent<MeshRenderer>()) {
            for (const auto& material : rendererComp->getMaterials()) {
                updated |= ReplaceMaterialTexture(material.get(), path, texture, terrainControlTexture);
            }
        }

        if (auto* skinned = entity->getComponent<SkinnedMeshRenderer>()) {
            for (const auto& material : skinned->getMaterials()) {
                updated |= ReplaceMaterialTexture(material.get(), path, texture, terrainControlTexture);
            }
        }

        if (auto* instanced = entity->getComponent<InstancedMeshRenderer>()) {
            for (const auto& material : instanced->getMaterials()) {
                updated |= ReplaceMaterialTexture(material.get(), path, texture, terrainControlTexture);
            }
        }
    }

    if (updated) {
        AssetDatabase::getInstance().recordImportForGuid(guid);
    }
    return updated;
}

bool SceneCommands::reimportHdriAsset(Scene* scene, const std::string& guid) {
    if (!scene || guid.empty()) {
        return false;
    }
    AssetDatabase& db = AssetDatabase::getInstance();
    AssetRecord record;
    if (!db.getRecordForGuid(guid, record)) {
        return false;
    }
    if (record.type != "hdri") {
        return false;
    }
    std::string path = db.getPathForGuid(guid);
    if (path.empty()) {
        return false;
    }

    Renderer* renderer = Engine::getInstance().getRenderer();
    if (!renderer) {
        return false;
    }
    if (renderer->getEnvironmentPath() != path) {
        AssetDatabase::getInstance().recordImportForGuid(guid);
        return true;
    }
    bool loaded = renderer->loadEnvironmentMap(path);
    if (loaded) {
        AssetDatabase::getInstance().recordImportForGuid(guid);
    }
    return loaded;
}

void SceneCommands::destroyEntitiesByUUID(Scene* scene, const std::vector<std::string>& uuids) {
    if (!scene) return;
    
    for (const auto& uuidStr : uuids) {
        Entity* entity = getEntityByUUID(scene, uuidStr);
        if (entity) {
            std::cout << "[SceneCommands] Destroying entity: " << entity->getName() 
                      << " (UUID: " << uuidStr << ")" << std::endl;
            scene->destroyEntity(entity);
        } else {
            std::cout << "[SceneCommands] Entity not found for UUID: " << uuidStr << std::endl;
        }
    }
}

std::vector<Entity*> SceneCommands::getAllEntities(Scene* scene) {
    if (!scene) return {};
    
    std::vector<Entity*> entities;
    const auto& allEntities = scene->getAllEntities();
    
    for (const auto& entityPtr : allEntities) {
        entities.push_back(entityPtr.get());
    }
    
    return entities;
}

Entity* SceneCommands::getEntityByUUID(Scene* scene, const std::string& uuidStr) {
    if (!scene) return nullptr;
    
    // Parse UUID from string (simple implementation)
    // Format: "0000000000000001" (16 hex digits)
    try {
        uint64_t uuidValue = std::stoull(uuidStr, nullptr, 16);
        UUID uuid(uuidValue);
        return scene->findEntity(uuid);
    } catch (...) {
        std::cerr << "Failed to parse UUID: " << uuidStr << std::endl;
        return nullptr;
    }
}

bool SceneCommands::setParent(Scene* scene, const std::string& childUuid, const std::string& parentUuid) {
    if (!scene) {
        return false;
    }
    Entity* child = getEntityByUUID(scene, childUuid);
    if (!child) {
        return false;
    }
    Transform* childTransform = child->getTransform();
    if (!childTransform) {
        return false;
    }

    Transform* newParent = nullptr;
    if (!parentUuid.empty()) {
        Entity* parent = getEntityByUUID(scene, parentUuid);
        if (!parent) {
            return false;
        }
        Transform* parentTransform = parent->getTransform();
        if (!parentTransform) {
            return false;
        }
        if (parentTransform == childTransform || parentTransform->isChildOf(childTransform)) {
            return false;
        }
        newParent = parentTransform;
    }

    childTransform->setParent(newParent, true);
    return true;
}

Entity* SceneCommands::buildHLOD(Scene* scene, const std::vector<std::string>& uuids, float lodStart, float lodEnd) {
    if (!scene || uuids.empty()) {
        return nullptr;
    }

    std::vector<Entity*> sources;
    sources.reserve(uuids.size());
    for (const auto& uuid : uuids) {
        if (auto* entity = getEntityByUUID(scene, uuid)) {
            sources.push_back(entity);
        }
    }
    if (sources.empty()) {
        return nullptr;
    }

    std::vector<Vertex> mergedVertices;
    std::vector<uint32_t> mergedIndices;
    std::vector<Submesh> mergedSubmeshes;
    std::vector<std::shared_ptr<Material>> materials;
    std::unordered_map<const Material*, uint32_t> materialMap;

    struct Bucket {
        uint32_t materialIndex = 0;
        std::vector<uint32_t> indices;
    };
    std::vector<Bucket> buckets;

    auto getMaterialIndex = [&](const std::shared_ptr<Material>& mat) -> uint32_t {
        const Material* key = mat.get();
        auto it = materialMap.find(key);
        if (it != materialMap.end()) {
            return it->second;
        }
        uint32_t idx = static_cast<uint32_t>(materials.size());
        materials.push_back(mat ? mat : Material::CreateDefault());
        materialMap[key] = idx;
        buckets.push_back(Bucket{idx, {}});
        return idx;
    };

    for (Entity* entity : sources) {
        if (!entity || !entity->isActiveInHierarchy()) {
            continue;
        }
        if (entity->getComponent<SkinnedMeshRenderer>() || entity->getComponent<InstancedMeshRenderer>()) {
            continue;
        }
        MeshRenderer* renderer = entity->getComponent<MeshRenderer>();
        if (!renderer || !renderer->isEnabled()) {
            continue;
        }
        std::shared_ptr<Mesh> mesh = renderer->getMesh();
        if (!mesh) {
            continue;
        }

        const auto& vertices = mesh->getVertices();
        const auto& indices = mesh->getIndices();
        if (vertices.empty() || indices.empty()) {
            continue;
        }

        Math::Matrix4x4 world = entity->getTransform()->getWorldMatrix();
        Math::Matrix4x4 normalMatrix = world.normalMatrix();

        uint32_t baseVertex = static_cast<uint32_t>(mergedVertices.size());
        mergedVertices.reserve(mergedVertices.size() + vertices.size());
        for (const auto& v : vertices) {
            Vertex out = v;
            out.position = world.transformPoint(v.position);
            out.normal = normalMatrix.transformDirection(v.normal);
            out.normal.normalize();
            out.tangent = normalMatrix.transformDirection(v.tangent);
            out.tangent.normalize();
            out.bitangent = normalMatrix.transformDirection(v.bitangent);
            out.bitangent.normalize();
            mergedVertices.push_back(out);
        }

        const auto& submeshes = mesh->getSubmeshes();
        if (submeshes.empty()) {
            auto mat = renderer->getMaterial(0);
            uint32_t matIndex = getMaterialIndex(mat);
            Bucket& bucket = buckets[matIndex];
            bucket.indices.reserve(bucket.indices.size() + indices.size());
            for (uint32_t idx : indices) {
                bucket.indices.push_back(baseVertex + idx);
            }
        } else {
            for (const auto& sub : submeshes) {
                auto mat = renderer->getMaterial(sub.materialIndex);
                uint32_t matIndex = getMaterialIndex(mat);
                Bucket& bucket = buckets[matIndex];
                uint32_t end = sub.indexStart + sub.indexCount;
                bucket.indices.reserve(bucket.indices.size() + sub.indexCount);
                for (uint32_t i = sub.indexStart; i < end; ++i) {
                    if (i >= indices.size()) break;
                    bucket.indices.push_back(baseVertex + indices[i]);
                }
            }
        }
    }

    if (mergedVertices.empty() || buckets.empty()) {
        return nullptr;
    }

    mergedIndices.clear();
    mergedSubmeshes.clear();

    uint32_t indexStart = 0;
    std::vector<Vertex> hlodVertices;
    std::vector<uint32_t> hlodIndices;
    for (const auto& bucket : buckets) {
        if (bucket.indices.empty()) {
            continue;
        }

        HLODBucketBuildResult bucketMesh = BuildCompactHLODBucket(mergedVertices, bucket.indices);
        if (bucketMesh.vertices.empty() || bucketMesh.indices.empty()) {
            continue;
        }

#if CRESCENT_HAS_MESHOPTIMIZER
        SimplifyHLODBucketInPlace(bucketMesh, sources.size());
#endif

        const uint32_t baseVertex = static_cast<uint32_t>(hlodVertices.size());
        hlodVertices.insert(hlodVertices.end(), bucketMesh.vertices.begin(), bucketMesh.vertices.end());

        mergedSubmeshes.emplace_back(indexStart, static_cast<uint32_t>(bucketMesh.indices.size()), bucket.materialIndex);
        for (uint32_t index : bucketMesh.indices) {
            hlodIndices.push_back(baseVertex + index);
        }
        indexStart += static_cast<uint32_t>(bucketMesh.indices.size());
    }

    if (hlodVertices.empty() || hlodIndices.empty()) {
        return nullptr;
    }

    auto mergedMesh = std::make_shared<Mesh>();
    mergedMesh->setName("HLOD_Mesh");
    mergedMesh->setVertices(hlodVertices);
    mergedMesh->setIndices(hlodIndices);
    if (!mergedSubmeshes.empty()) {
        mergedMesh->setSubmeshes(mergedSubmeshes);
    }

    Math::Vector3 boundsSize = mergedMesh->getBoundsSize();
    float size = std::max(0.1f, std::max(boundsSize.x, std::max(boundsSize.y, boundsSize.z)));
    float autoStart = std::max(size * 4.0f, 30.0f);
    float autoEnd = std::max(autoStart * 1.2f, autoStart + size * 2.0f);
    if (lodStart < 0.0f) lodStart = autoStart;
    if (lodEnd < 0.0f) lodEnd = autoEnd;

    Entity* hlodEntity = scene->createEntity("HLOD_Proxy");
    if (!hlodEntity) {
        return nullptr;
    }
    hlodEntity->getTransform()->setLocalPosition(Math::Vector3::Zero);
    hlodEntity->getTransform()->setLocalRotation(Math::Quaternion::Identity);
    hlodEntity->getTransform()->setLocalScale(Math::Vector3(1.0f, 1.0f, 1.0f));

    auto* mr = hlodEntity->addComponent<MeshRenderer>();
    mr->setMesh(mergedMesh);
    for (uint32_t i = 0; i < materials.size(); ++i) {
        mr->setMaterial(i, materials[i] ? materials[i] : Material::CreateDefault());
    }
    mr->setCastShadows(true);
    mr->setReceiveShadows(true);

    auto* proxy = hlodEntity->addComponent<HLODProxy>();
    proxy->setSourceUuids(uuids);
    proxy->setLodStart(lodStart);
    proxy->setLodEnd(lodEnd);
    proxy->setEnabled(true);

    return hlodEntity;
}

SceneCommands::StaticLightingLayoutStats SceneCommands::buildStaticLightingLayout(Scene* scene, const std::string& scenePath) {
    StaticLightingLayoutStats stats;
    if (!scene) {
        return stats;
    }

    SceneSettings settings = scene->getSettings();
    SceneStaticLightingSettings& staticLightingSettings = settings.staticLighting;
    staticLightingSettings.enabled = true;
    staticLightingSettings.lastBakeHash.clear();
    scene->setSettings(settings);

    std::vector<StaticLightingLayoutCandidate> candidates;
    candidates.reserve(scene->getEntityCount());

    for (const auto& entityPtr : scene->getAllEntities()) {
        Entity* entity = entityPtr.get();
        if (!entity || !entity->isActiveInHierarchy() || entity->isEditorOnly()) {
            continue;
        }

        auto* renderer = entity->getComponent<MeshRenderer>();
        if (!renderer || entity->getComponent<SkinnedMeshRenderer>()) {
            continue;
        }

        std::shared_ptr<Mesh> mesh = renderer->getMesh();
        if (!mesh || mesh->getVertices().empty() || mesh->getIndices().size() < 3) {
            continue;
        }

        MeshRenderer::StaticLightingData metadata = renderer->getStaticLighting();
        metadata.lightmapUVChannel = 1;
        metadata.lightmapIndex = -1;
        metadata.lightmapScaleOffset = Math::Vector4(1.0f, 1.0f, 0.0f, 0.0f);
        metadata.lightmapPath.clear();
        metadata.directionalLightmapPath.clear();
        metadata.shadowmaskPath.clear();

        if (!metadata.staticGeometry) {
            renderer->setStaticLighting(metadata);
            continue;
        }
        stats.staticGeometryRendererCount += 1;

        bool generatedFallback = false;
        std::shared_ptr<Mesh> layoutMesh = mesh;
        if (!HasUsableLightmapUVs(*layoutMesh)) {
            if (!staticLightingSettings.autoUnwrap) {
                renderer->setStaticLighting(metadata);
                stats.skippedRendererCount += 1;
                continue;
            }
            layoutMesh = CreateFallbackLightmapMesh(layoutMesh);
            generatedFallback = true;
        }
        layoutMesh = CreateNormalizedLightmapMesh(layoutMesh);
        if (!layoutMesh || !HasUsableLightmapUVs(*layoutMesh)) {
            renderer->setStaticLighting(metadata);
            stats.skippedRendererCount += 1;
            continue;
        }

        if (layoutMesh != mesh) {
            renderer->setMesh(layoutMesh);
        }

        float surfaceAreaWS = ComputeWorldSurfaceArea(*layoutMesh, *entity->getTransform());
        if (surfaceAreaWS <= 0.0001f) {
            renderer->setStaticLighting(metadata);
            stats.skippedRendererCount += 1;
            continue;
        }

        StaticLightingLayoutCandidate candidate;
        candidate.entity = entity;
        candidate.renderer = renderer;
        candidate.mesh = layoutMesh;
        candidate.surfaceAreaWS = surfaceAreaWS;
        candidate.requestedInnerResolution = EstimateLightmapInnerResolution(surfaceAreaWS, staticLightingSettings);
        candidate.generatedFallbackUVs = generatedFallback;
        candidates.push_back(candidate);
    }

    std::sort(candidates.begin(), candidates.end(), [](const StaticLightingLayoutCandidate& a,
                                                       const StaticLightingLayoutCandidate& b) {
        if (a.requestedInnerResolution != b.requestedInnerResolution) {
            return a.requestedInnerResolution > b.requestedInnerResolution;
        }
        return a.surfaceAreaWS > b.surfaceAreaWS;
    });

    std::vector<ShelfAtlasState> atlases;
    atlases.reserve(std::max(staticLightingSettings.maxAtlasCount, 1));
    const int atlasSize = std::max(256, staticLightingSettings.atlasSize);
    const int padding = std::max(1, staticLightingSettings.unwrapPadding);
    const float resolutionScale = ResolveStaticLightingResolutionScale(
        candidates,
        atlasSize,
        padding,
        staticLightingSettings.maxAtlasCount
    );

    for (auto& candidate : candidates) {
        int scaledInnerResolution = static_cast<int>(std::round(static_cast<float>(candidate.requestedInnerResolution) * resolutionScale));
        int innerResolution = std::min(std::max(1, scaledInnerResolution), atlasSize - padding * 2);
        int slotResolution = std::min(atlasSize, innerResolution + padding * 2);
        if (slotResolution <= padding * 2) {
            stats.skippedRendererCount += 1;
            continue;
        }

        int packedX = 0;
        int packedY = 0;
        int assignedAtlasIndex = -1;

        for (auto& atlas : atlases) {
            if (TryPackShelfRect(atlas, slotResolution, slotResolution, packedX, packedY)) {
                assignedAtlasIndex = atlas.atlasIndex;
                break;
            }
        }

        if (assignedAtlasIndex < 0) {
            if (static_cast<int>(atlases.size()) >= std::max(1, staticLightingSettings.maxAtlasCount)) {
                MeshRenderer::StaticLightingData metadata = candidate.renderer->getStaticLighting();
                metadata.lightmapIndex = -1;
                metadata.lightmapScaleOffset = Math::Vector4(1.0f, 1.0f, 0.0f, 0.0f);
                candidate.renderer->setStaticLighting(metadata);
                stats.skippedRendererCount += 1;
                continue;
            }

            ShelfAtlasState atlas;
            atlas.atlasIndex = static_cast<int>(atlases.size());
            atlas.width = atlasSize;
            atlas.height = atlasSize;
            if (!TryPackShelfRect(atlas, slotResolution, slotResolution, packedX, packedY)) {
                stats.skippedRendererCount += 1;
                continue;
            }
            assignedAtlasIndex = atlas.atlasIndex;
            atlases.push_back(atlas);
        }

        float atlasSizeF = static_cast<float>(atlasSize);
        float scale = static_cast<float>(innerResolution) / atlasSizeF;
        float offsetX = static_cast<float>(packedX + padding) / atlasSizeF;
        float offsetY = static_cast<float>(packedY + padding) / atlasSizeF;

        MeshRenderer::StaticLightingData metadata = candidate.renderer->getStaticLighting();
        metadata.lightmapIndex = assignedAtlasIndex;
        metadata.lightmapUVChannel = 1;
        metadata.lightmapScaleOffset = Math::Vector4(scale, scale, offsetX, offsetY);
        metadata.lightmapPath.clear();
        metadata.directionalLightmapPath.clear();
        metadata.shadowmaskPath.clear();
        candidate.renderer->setStaticLighting(metadata);

        stats.rendererCount += 1;
        if (candidate.generatedFallbackUVs) {
            stats.generatedUVRendererCount += 1;
        } else {
            stats.reusedUVRendererCount += 1;
        }
    }

    stats.atlasCount = static_cast<int>(atlases.size());
    if (stats.rendererCount > 0) {
        SceneSerializer::SaveStaticLightingManifest(scene, scenePath);
    }
    return stats;
}

SceneCommands::StaticLightmapBakeStats SceneCommands::bakeStaticLightmaps(Scene* scene, const std::string& scenePath) {
    StaticLightmapBakeStats stats;
    if (!scene) {
        return stats;
    }

    std::vector<Light*> stationaryShadowmaskLights;
    stationaryShadowmaskLights.reserve(4);
    int markedStaticLightCount = 0;
    for (const auto& entityPtr : scene->getAllEntities()) {
        Entity* entity = entityPtr.get();
        if (!entity) {
            continue;
        }

        Light* light = entity->getComponent<Light>();
        if (!light) {
            continue;
        }

        light->setShadowmaskChannel(-1);
        if (!entity->isActiveInHierarchy() || entity->isEditorOnly() || !light->getContributeToStaticBake()) {
            continue;
        }
        markedStaticLightCount += 1;

        if (light->getMobility() == Light::Mobility::Stationary && stationaryShadowmaskLights.size() < 4u) {
            stationaryShadowmaskLights.push_back(light);
        }
    }
    for (size_t index = 0; index < stationaryShadowmaskLights.size(); ++index) {
        stationaryShadowmaskLights[index]->setShadowmaskChannel(static_cast<int>(index));
    }

    SceneSettings bakeSettings = scene->getSettings();
    bakeSettings.staticLighting.enabled = true;
    bakeSettings.staticLighting.directionalLightmaps = bakeSettings.staticLighting.bakeDirectLighting;
    bakeSettings.staticLighting.shadowmask = !stationaryShadowmaskLights.empty();
    scene->setSettings(bakeSettings);
    stats.bakedLightCount = markedStaticLightCount;

    StaticLightingLayoutStats layoutStats = buildStaticLightingLayout(scene, scenePath);
    stats.staticGeometryRendererCount = layoutStats.staticGeometryRendererCount;
    stats.layoutRendererCount = layoutStats.rendererCount;
    stats.layoutSkippedRendererCount = layoutStats.skippedRendererCount;
    stats.generatedUVRendererCount = layoutStats.generatedUVRendererCount;
    stats.reusedUVRendererCount = layoutStats.reusedUVRendererCount;
    StaticLightingManifest manifest = SceneSerializer::BuildStaticLightingManifest(scene, scenePath);
    if (manifest.atlases.empty()) {
        return stats;
    }

    std::vector<BakedDirectLight> bakedLights;
    bakedLights.reserve(8);
    for (const auto& entityPtr : scene->getAllEntities()) {
        Entity* entity = entityPtr.get();
        if (!entity || !entity->isActiveInHierarchy() || entity->isEditorOnly()) {
            continue;
        }

        Light* light = entity->getComponent<Light>();
        if (!light || !light->getContributeToStaticBake()) {
            continue;
        }

        BakedDirectLight baked;
        baked.type = light->getType();
        baked.mobility = light->getMobility();
        baked.positionWS = entity->getTransform()->getPosition();
        baked.directionWS = entity->getTransform()->forward().normalized();
        baked.color = ClampColor(light->getEffectiveColor(), 8.0f);
        baked.intensity = std::max(0.0f, light->getIntensity());
        baked.range = std::max(0.0f, light->getRange());
        baked.sourceRadius = std::max(0.0f, light->getSourceRadius());
        baked.castShadows = light->getCastShadows();
        baked.shadowBias = std::max(0.0f, light->getShadowBias());
        baked.shadowNormalBias = std::max(0.0f, light->getShadowNormalBias());
        baked.shadowDistance = std::max(light->getShadowFarPlane(), light->getRange());
        baked.shadowmaskChannel = light->getShadowmaskChannel();
        float outerRadians = light->getSpotAngle() * Math::DEG_TO_RAD * 0.5f;
        float innerRadians = light->getInnerSpotAngle() * Math::DEG_TO_RAD * 0.5f;
        baked.cosOuter = std::cos(outerRadians);
        baked.cosInner = std::cos(innerRadians);
        bakedLights.push_back(baked);
    }
    std::vector<EmissiveTriangleSurface> emissiveSurfaces = BuildEmissiveTriangleSurfaces(scene);
    stats.bakedLightCount = static_cast<int>(bakedLights.size()) + (emissiveSurfaces.empty() ? 0 : 1);
    if (bakedLights.empty() && emissiveSurfaces.empty()) {
        return stats;
    }

    std::unordered_map<int, std::vector<StaticLightingBakeCandidate>> atlasCandidates;
    atlasCandidates.reserve(manifest.atlases.size());
    for (const auto& entityPtr : scene->getAllEntities()) {
        Entity* entity = entityPtr.get();
        if (!entity || !entity->isActiveInHierarchy() || entity->isEditorOnly()) {
            continue;
        }

        MeshRenderer* renderer = entity->getComponent<MeshRenderer>();
        if (!renderer || entity->getComponent<SkinnedMeshRenderer>()) {
            continue;
        }

        const auto& staticLighting = renderer->getStaticLighting();
        if (!staticLighting.staticGeometry || staticLighting.lightmapIndex < 0 || staticLighting.lightmapUVChannel != 1) {
            continue;
        }

        std::shared_ptr<Mesh> mesh = renderer->getMesh();
        if (!mesh || mesh->getVertices().empty() || mesh->getIndices().size() < 3) {
            continue;
        }

        StaticLightingBakeCandidate candidate;
        candidate.entity = entity;
        candidate.renderer = renderer;
        candidate.mesh = mesh;
        candidate.worldMatrix = entity->getTransform()->getWorldMatrix();
        candidate.normalMatrix = candidate.worldMatrix.normalMatrix();
        candidate.staticLighting = staticLighting;
        atlasCandidates[staticLighting.lightmapIndex].push_back(candidate);
        stats.bakedRendererCount += 1;
    }

    for (auto& atlasRecord : manifest.atlases) {
        const int atlasIndex = atlasRecord.index;
        const int width = std::max(1, atlasRecord.width);
        const int height = std::max(1, atlasRecord.height);
        std::vector<float> atlasDirectLighting(static_cast<size_t>(width) * static_cast<size_t>(height) * 3u, 0.0f);
        std::vector<float> atlasIndirectLighting(static_cast<size_t>(width) * static_cast<size_t>(height) * 3u, 0.0f);
        std::vector<float> atlasDirectional(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0.0f);
        std::vector<float> atlasShadowmask(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0.0f);
        std::vector<uint16_t> atlasCoverage(static_cast<size_t>(width) * static_cast<size_t>(height), 0u);
        std::vector<uint16_t> atlasShadowmaskCoverage(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0u);

        auto atlasIt = atlasCandidates.find(atlasIndex);
        if (atlasIt != atlasCandidates.end()) {
            for (const auto& candidate : atlasIt->second) {
                const auto& vertices = candidate.mesh->getVertices();
                const auto& indices = candidate.mesh->getIndices();
                for (size_t tri = 0; tri + 2 < indices.size(); tri += 3) {
                    uint32_t i0 = indices[tri + 0];
                    uint32_t i1 = indices[tri + 1];
                    uint32_t i2 = indices[tri + 2];
                    if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
                        continue;
                    }

                    const Vertex& v0 = vertices[i0];
                    const Vertex& v1 = vertices[i1];
                    const Vertex& v2 = vertices[i2];
                    const int materialIndex = ResolveTriangleMaterialIndex(*candidate.mesh, tri);
                    std::shared_ptr<Material> material = candidate.renderer->getMaterial(static_cast<uint32_t>(std::max(materialIndex, 0)));

                    Math::Vector2 uv0 = TransformLightmapUVToAtlas(v0.texCoord1, candidate.staticLighting.lightmapScaleOffset);
                    Math::Vector2 uv1 = TransformLightmapUVToAtlas(v1.texCoord1, candidate.staticLighting.lightmapScaleOffset);
                    Math::Vector2 uv2 = TransformLightmapUVToAtlas(v2.texCoord1, candidate.staticLighting.lightmapScaleOffset);

                    Math::Vector2 p0(uv0.x * static_cast<float>(width), uv0.y * static_cast<float>(height));
                    Math::Vector2 p1(uv1.x * static_cast<float>(width), uv1.y * static_cast<float>(height));
                    Math::Vector2 p2(uv2.x * static_cast<float>(width), uv2.y * static_cast<float>(height));

                    float minXf = std::floor(std::min(p0.x, std::min(p1.x, p2.x)));
                    float minYf = std::floor(std::min(p0.y, std::min(p1.y, p2.y)));
                    float maxXf = std::ceil(std::max(p0.x, std::max(p1.x, p2.x)));
                    float maxYf = std::ceil(std::max(p0.y, std::max(p1.y, p2.y)));

                    int minX = std::max(0, static_cast<int>(minXf));
                    int minY = std::max(0, static_cast<int>(minYf));
                    int maxX = std::min(width - 1, static_cast<int>(maxXf));
                    int maxY = std::min(height - 1, static_cast<int>(maxYf));

                    for (int y = minY; y <= maxY; ++y) {
                        for (int x = minX; x <= maxX; ++x) {
                            Math::Vector2 sampleUV((static_cast<float>(x) + 0.5f) / static_cast<float>(width),
                                                   (static_cast<float>(y) + 0.5f) / static_cast<float>(height));
                            Math::Vector3 bary;
                            if (!ComputeBarycentrics(sampleUV, uv0, uv1, uv2, bary)) {
                                continue;
                            }

                            Math::Vector3 localPos = v0.position * bary.x + v1.position * bary.y + v2.position * bary.z;
                            Math::Vector2 surfaceUV = v0.texCoord * bary.x + v1.texCoord * bary.y + v2.texCoord * bary.z;
                            Math::Vector3 localNormal = (v0.normal * bary.x + v1.normal * bary.y + v2.normal * bary.z).normalized();
                            if (localNormal.lengthSquared() <= Math::EPSILON) {
                                localNormal = Math::Vector3::Up;
                            }
                            Math::Vector3 localTangent = v0.tangent * bary.x + v1.tangent * bary.y + v2.tangent * bary.z;
                            Math::Vector3 localBitangent = v0.bitangent * bary.x + v1.bitangent * bary.y + v2.bitangent * bary.z;

                            Math::Vector3 positionWS = candidate.worldMatrix.transformPoint(localPos);
                            Math::Vector3 geometryNormalWS = candidate.normalMatrix.transformDirection(localNormal).normalized();
                            if (geometryNormalWS.lengthSquared() <= Math::EPSILON) {
                                geometryNormalWS = candidate.worldMatrix.transformDirection(localNormal).normalized();
                            }
                            if (geometryNormalWS.lengthSquared() <= Math::EPSILON) {
                                geometryNormalWS = Math::Vector3::Up;
                            }
                            Math::Vector3 tangentWS = candidate.normalMatrix.transformDirection(localTangent).normalized();
                            Math::Vector3 bitangentWS = candidate.normalMatrix.transformDirection(localBitangent).normalized();
                            Math::Vector3 shadingNormalWS = ResolveBakeShadingNormal(
                                material,
                                surfaceUV,
                                geometryNormalWS,
                                tangentWS,
                                bitangentWS
                            );

                            Math::Vector3 accumulated = Math::Vector3::Zero;
                            Math::Vector3 indirect = Math::Vector3::Zero;
                            Math::Vector3 dominantDirection = Math::Vector3::Zero;
                            float dominantWeight = 0.0f;
                            float referenceNoL = 0.0f;
                            for (const BakedDirectLight& light : bakedLights) {
                                Math::Vector3 contribution = BakeLightContribution(light, positionWS, shadingNormalWS);
                                if (contribution.lengthSquared() <= Math::EPSILON) {
                                    continue;
                                }
                                bool visible = IsDirectLightVisible(scene, light, positionWS, geometryNormalWS);
                                if (light.mobility == Light::Mobility::Stationary && light.shadowmaskChannel >= 0) {
                                    size_t shadowmaskIndex = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u
                                        + static_cast<size_t>(light.shadowmaskChannel);
                                    atlasShadowmask[shadowmaskIndex] += visible ? 1.0f : 0.0f;
                                    atlasShadowmaskCoverage[shadowmaskIndex] += 1u;
                                }
                                if (!visible) {
                                    continue;
                                }
                                if (light.mobility == Light::Mobility::Stationary) {
                                    continue;
                                }
                                accumulated += contribution;

                                Math::Vector3 lightDir = Math::Vector3::Zero;
                                float sampleNoL = 0.0f;
                                if (ComputeIncidentLightDirection(light, positionWS, shadingNormalWS, lightDir, sampleNoL)) {
                                    float weight = std::max(ComputeLuminance(contribution), 0.0f);
                                    dominantDirection += lightDir * weight;
                                    dominantWeight += weight;
                                    referenceNoL += sampleNoL * weight;
                                }
                            }
                            if (!emissiveSurfaces.empty()) {
                                EmissiveLightingEstimate emissiveDirect = EstimateEmissiveSurfaceLighting(
                                    scene,
                                    emissiveSurfaces,
                                    positionWS,
                                    shadingNormalWS,
                                    static_cast<uint32_t>((atlasIndex + 1) * 2166136261u)
                                        ^ static_cast<uint32_t>((x + 1) * 16777619u)
                                        ^ static_cast<uint32_t>((y + 1) * 374761393u)
                                        ^ static_cast<uint32_t>(tri * 668265263u),
                                    std::max(1, std::min(8, bakeSettings.staticLighting.samplesPerTexel / 32))
                                );
                                accumulated += emissiveDirect.irradiance;
                                dominantDirection += emissiveDirect.weightedDirection;
                                dominantWeight += emissiveDirect.directionWeight;
                                referenceNoL += emissiveDirect.referenceNoL;
                            }
                            accumulated = ClampColor(accumulated, 16.0f);
                            if (candidate.staticLighting.receiveGI && bakeSettings.staticLighting.indirectBounces > 0) {
                                Math::Vector3 primaryAlbedo = Math::Vector3::One;
                                Math::Vector3 primaryEmission = Math::Vector3::Zero;
                                float primaryAO = 1.0f;
                                bool primaryContributeGI = true;
                                ResolveIndirectSurfaceProperties(candidate.entity,
                                                                 positionWS,
                                                                 primaryAlbedo,
                                                                 primaryEmission,
                                                                 primaryAO,
                                                                 primaryContributeGI);
                                uint32_t sampleSeed = static_cast<uint32_t>((atlasIndex + 1) * 73856093)
                                    ^ static_cast<uint32_t>((x + 1) * 19349663)
                                    ^ static_cast<uint32_t>((y + 1) * 83492791)
                                    ^ static_cast<uint32_t>(tri * 2654435761u);
                                int adaptiveSampleCount = ComputeAdaptiveIndirectSampleCount(
                                    bakeSettings,
                                    accumulated,
                                    primaryEmission,
                                    primaryAO
                                );
                                indirect = EstimateIndirectLighting(
                                    scene,
                                    bakeSettings,
                                    bakedLights,
                                    emissiveSurfaces,
                                    positionWS,
                                    geometryNormalWS,
                                    sampleSeed,
                                    adaptiveSampleCount
                                );
                            }

                            size_t pixelIndex = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
                            atlasDirectLighting[pixelIndex * 3 + 0] += accumulated.x;
                            atlasDirectLighting[pixelIndex * 3 + 1] += accumulated.y;
                            atlasDirectLighting[pixelIndex * 3 + 2] += accumulated.z;
                            atlasIndirectLighting[pixelIndex * 3 + 0] += indirect.x;
                            atlasIndirectLighting[pixelIndex * 3 + 1] += indirect.y;
                            atlasIndirectLighting[pixelIndex * 3 + 2] += indirect.z;
                            if (dominantWeight > Math::EPSILON) {
                                Math::Vector3 encodedDominant = dominantDirection / dominantWeight;
                                float encodedReference = referenceNoL / dominantWeight;
                                atlasDirectional[pixelIndex * 4 + 0] += encodedDominant.x;
                                atlasDirectional[pixelIndex * 4 + 1] += encodedDominant.y;
                                atlasDirectional[pixelIndex * 4 + 2] += encodedDominant.z;
                                atlasDirectional[pixelIndex * 4 + 3] += encodedReference;
                            }
                            atlasCoverage[pixelIndex] += 1;
                        }
                    }
                }
            }
        }

        std::vector<float> atlasPixelsHDR(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0.0f);
        std::vector<unsigned char> directionalPixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0u);
        std::vector<unsigned char> shadowmaskPixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 255u);
        for (size_t pixelIndex = 0; pixelIndex < atlasCoverage.size(); ++pixelIndex) {
            uint16_t coverage = atlasCoverage[pixelIndex];
            if (coverage == 0u) {
                continue;
            }
            float invCoverage = 1.0f / static_cast<float>(coverage);
            atlasDirectLighting[pixelIndex * 3 + 0] *= invCoverage;
            atlasDirectLighting[pixelIndex * 3 + 1] *= invCoverage;
            atlasDirectLighting[pixelIndex * 3 + 2] *= invCoverage;
            atlasIndirectLighting[pixelIndex * 3 + 0] *= invCoverage;
            atlasIndirectLighting[pixelIndex * 3 + 1] *= invCoverage;
            atlasIndirectLighting[pixelIndex * 3 + 2] *= invCoverage;
            atlasDirectional[pixelIndex * 4 + 0] *= invCoverage;
            atlasDirectional[pixelIndex * 4 + 1] *= invCoverage;
            atlasDirectional[pixelIndex * 4 + 2] *= invCoverage;
            atlasDirectional[pixelIndex * 4 + 3] *= invCoverage;
        }
        if (bakeSettings.staticLighting.denoise && bakeSettings.staticLighting.indirectBounces > 0) {
            DenoiseIndirectAtlas(atlasDirectLighting, atlasCoverage, width, height, atlasIndirectLighting);
        }
        for (size_t pixelIndex = 0; pixelIndex < atlasCoverage.size(); ++pixelIndex) {
            uint16_t coverage = atlasCoverage[pixelIndex];
            if (coverage == 0) {
                continue;
            }
            float r = bakeSettings.staticLighting.bakeDirectLighting
                ? std::max(atlasDirectLighting[pixelIndex * 3 + 0] + atlasIndirectLighting[pixelIndex * 3 + 0], 0.0f)
                : std::max(atlasIndirectLighting[pixelIndex * 3 + 0], 0.0f);
            float g = bakeSettings.staticLighting.bakeDirectLighting
                ? std::max(atlasDirectLighting[pixelIndex * 3 + 1] + atlasIndirectLighting[pixelIndex * 3 + 1], 0.0f)
                : std::max(atlasIndirectLighting[pixelIndex * 3 + 1], 0.0f);
            float b = bakeSettings.staticLighting.bakeDirectLighting
                ? std::max(atlasDirectLighting[pixelIndex * 3 + 2] + atlasIndirectLighting[pixelIndex * 3 + 2], 0.0f)
                : std::max(atlasIndirectLighting[pixelIndex * 3 + 2], 0.0f);

            atlasPixelsHDR[pixelIndex * 4 + 0] = r;
            atlasPixelsHDR[pixelIndex * 4 + 1] = g;
            atlasPixelsHDR[pixelIndex * 4 + 2] = b;
            atlasPixelsHDR[pixelIndex * 4 + 3] = 1.0f;

            Math::Vector3 directionalVec(atlasDirectional[pixelIndex * 4 + 0],
                                         atlasDirectional[pixelIndex * 4 + 1],
                                         atlasDirectional[pixelIndex * 4 + 2]);
            directionalVec = Math::Vector3::Clamp(directionalVec,
                                                  Math::Vector3(-1.0f, -1.0f, -1.0f),
                                                  Math::Vector3(1.0f, 1.0f, 1.0f));
            float encodedReference = Math::Clamp(atlasDirectional[pixelIndex * 4 + 3], 0.0f, 1.0f);
            directionalPixels[pixelIndex * 4 + 0] = static_cast<unsigned char>(Math::Clamp(directionalVec.x * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f);
            directionalPixels[pixelIndex * 4 + 1] = static_cast<unsigned char>(Math::Clamp(directionalVec.y * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f);
            directionalPixels[pixelIndex * 4 + 2] = static_cast<unsigned char>(Math::Clamp(directionalVec.z * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f);
            directionalPixels[pixelIndex * 4 + 3] = static_cast<unsigned char>(encodedReference * 255.0f);
            for (size_t channel = 0; channel < 4u; ++channel) {
                size_t shadowmaskIndex = pixelIndex * 4u + channel;
                uint16_t shadowmaskCoverage = atlasShadowmaskCoverage[shadowmaskIndex];
                if (shadowmaskCoverage == 0u) {
                    continue;
                }
                float visibility = atlasShadowmask[shadowmaskIndex] / static_cast<float>(shadowmaskCoverage);
                shadowmaskPixels[shadowmaskIndex] = static_cast<unsigned char>(Math::Clamp(visibility, 0.0f, 1.0f) * 255.0f);
            }
            stats.bakedTexelCount += 1;
        }

        std::string lightmapPath = atlasRecord.expectedLightmapPath;
        if (lightmapPath.empty()) {
            continue;
        }
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(lightmapPath).parent_path(), ec);
        if (!WriteEXRImage(lightmapPath, width, height, atlasPixelsHDR, true)) {
            continue;
        }

        atlasRecord.lightmapPath = lightmapPath;
        std::string directionalPath = atlasRecord.expectedDirectionalLightmapPath;
        if (!directionalPath.empty()) {
            std::error_code directionalEc;
            std::filesystem::create_directories(std::filesystem::path(directionalPath).parent_path(), directionalEc);
            if (stbi_write_png(directionalPath.c_str(), width, height, 4, directionalPixels.data(), width * 4) != 0) {
                atlasRecord.directionalLightmapPath = directionalPath;
            }
        }
        std::string shadowmaskPath = atlasRecord.expectedShadowmaskPath;
        if (!shadowmaskPath.empty()) {
            std::error_code shadowmaskEc;
            std::filesystem::create_directories(std::filesystem::path(shadowmaskPath).parent_path(), shadowmaskEc);
            if (stbi_write_png(shadowmaskPath.c_str(), width, height, 4, shadowmaskPixels.data(), width * 4) != 0) {
                atlasRecord.shadowmaskPath = shadowmaskPath;
            }
        }
        stats.atlasCount += 1;

        if (atlasIt != atlasCandidates.end()) {
            for (const auto& candidate : atlasIt->second) {
                MeshRenderer::StaticLightingData metadata = candidate.renderer->getStaticLighting();
                metadata.lightmapPath = lightmapPath;
                metadata.directionalLightmapPath = atlasRecord.directionalLightmapPath;
                metadata.shadowmaskPath = atlasRecord.shadowmaskPath;
                candidate.renderer->setStaticLighting(metadata);
            }
        }
    }

        if (stats.atlasCount > 0) {
            BakeProbeVolume(scene, scenePath, bakeSettings, bakedLights, emissiveSurfaces);
            SceneSettings updatedSettings = scene->getSettings();
            updatedSettings.staticLighting.enabled = true;
            updatedSettings.staticLighting.directionalLightmaps = bakeSettings.staticLighting.bakeDirectLighting;
            updatedSettings.staticLighting.localReflectionProbes = bakeSettings.staticLighting.localReflectionProbes;
            updatedSettings.staticLighting.shadowmask = !stationaryShadowmaskLights.empty();
            if (bakeSettings.staticLighting.bakeDirectLighting) {
                updatedSettings.staticLighting.lastBakeHash = !stationaryShadowmaskLights.empty()
                    ? "lightmap_direct_indirect_shadowmask_hdrlightmap_probereflections_v20"
                    : "lightmap_direct_indirect_hdrlightmap_probereflections_v20";
            } else {
                updatedSettings.staticLighting.lastBakeHash = !stationaryShadowmaskLights.empty()
                    ? "lightmap_indirect_shadowmask_runtime_direct_hdrlightmap_probereflections_v20"
                    : "lightmap_indirect_runtime_direct_hdrlightmap_probereflections_v20";
            }
            scene->setSettings(updatedSettings);
            SceneSerializer::SaveStaticLightingManifest(scene, scenePath);
        }

    return stats;
}


} // namespace Crescent
