#include "SceneCommands.hpp"
#include "../Core/Engine.hpp"
#include "../Renderer/Renderer.hpp"
#include "../Rendering/Texture.hpp"
#include "../Animation/Skeleton.hpp"
#include "../Animation/AnimationClip.hpp"
#include "../Components/SkinnedMeshRenderer.hpp"
#include "../Components/Animator.hpp"
#include "../Components/PrimitiveMesh.hpp"
#include "../Components/ModelMeshReference.hpp"
#include "../Assets/AssetDatabase.hpp"
#include <assimp/config.h>
#include <assimp/Importer.hpp>
#include <assimp/GltfMaterial.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/quaternion.h>
#include <assimp/scene.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

#ifndef aiTextureType_GLTF_METALLIC_ROUGHNESS
#define aiTextureType_GLTF_METALLIC_ROUGHNESS aiTextureType_UNKNOWN
#endif

namespace Crescent {
namespace {

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

struct ModelCacheEntry {
    std::unordered_map<int, MeshCacheEntry> meshesByIndex;
    std::unordered_map<int, MeshCacheEntry> mergedByMaterial;
    std::shared_ptr<Skeleton> skeleton;
    std::vector<std::shared_ptr<AnimationClip>> animations;
};

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
                                   const std::shared_ptr<Texture2D>& replacement) {
    if (!material || path.empty() || !replacement) {
        return false;
    }
    bool updated = false;
    auto swapIfMatch = [&](const std::shared_ptr<Texture2D>& current,
                           const std::function<void(std::shared_ptr<Texture2D>)>& setter) {
        if (current && current->getPath() == path) {
            setter(replacement);
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

static std::shared_ptr<Texture2D> LoadEmbeddedTexture(ImportContext& context, const aiTexture* texture, bool srgb, const std::string& cacheKey) {
    if (!texture || !context.textureLoader) {
        return nullptr;
    }
    if (texture->mHeight == 0) {
        const unsigned char* data = reinterpret_cast<const unsigned char*>(texture->pcData);
        size_t size = static_cast<size_t>(texture->mWidth);
        return context.textureLoader->loadTextureFromMemory(data, size, srgb, true, cacheKey);
    }
    
    std::vector<unsigned char> rgba;
    if (!BuildEmbeddedRGBA(texture, rgba)) {
        return nullptr;
    }
    return context.textureLoader->createTextureFromRGBA8(cacheKey, rgba.data(), static_cast<int>(texture->mWidth), static_cast<int>(texture->mHeight), srgb, true);
}

static std::shared_ptr<Texture2D> LoadMaterialTexture(ImportContext& context, const aiMaterial* material, aiTextureType type, bool srgb) {
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
        return LoadEmbeddedTexture(context, embedded, srgb, key);
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
    return context.textureLoader->loadTexture(resolved, srgb, true);
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
                                                      bool srgb) {
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
    return context.textureLoader->loadTexture(chosen->fullPath, srgb, true);
}

static std::shared_ptr<Material> BuildMaterial(ImportContext& context, const aiMaterial* material) {
    auto result = Material::CreateDefault();
    if (!material) {
        return result;
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
        "orm", "metallicroughness", "metallic_roughness", "occlusionroughnessmetallic", "occlusion_roughness_metallic", "mra", "rma"
    };
    static const std::vector<std::string> kEmissionTokens = {
        "emissive", "emission", "emit", "glow"
    };
    static const std::vector<std::string> kHeightTokens = {
        "height", "displace", "displacement", "bump", "parallax"
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
    float metallic = 0.0f;
    if (material->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS) {
        result->setMetallic(metallic);
    }
    float roughness = 0.0f;
    if (material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS) {
        result->setRoughness(roughness);
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
    
    auto albedoTex = LoadMaterialTexture(context, material, aiTextureType_BASE_COLOR, true);
    if (!albedoTex) {
        albedoTex = LoadMaterialTexture(context, material, aiTextureType_DIFFUSE, true);
    }
    if (!albedoTex) {
        albedoTex = FindFallbackTexture(context, materialName, kAlbedoTokens, true);
    }
    if (albedoTex) {
        result->setAlbedoTexture(albedoTex);
    }
    
    auto normalTex = LoadMaterialTexture(context, material, aiTextureType_NORMALS, false);
    auto heightTex = LoadMaterialTexture(context, material, aiTextureType_HEIGHT, false);
    if (!normalTex && heightTex) {
        normalTex = heightTex;
        heightTex.reset();
    }
    if (!normalTex) {
        normalTex = FindFallbackTexture(context, materialName, kNormalTokens, false);
    }
    if (normalTex) {
        result->setNormalTexture(normalTex);
    }
    if (!heightTex) {
        heightTex = FindFallbackTexture(context, materialName, kHeightTokens, false);
    }
    if (heightTex) {
        result->setHeightTexture(heightTex);
    }
    
    auto metallicTex = LoadMaterialTexture(context, material, aiTextureType_METALNESS, false);
    if (!metallicTex) {
        metallicTex = FindFallbackTexture(context, materialName, kMetallicTokens, false);
    }
    if (metallicTex) {
        result->setMetallicTexture(metallicTex);
    }
    
    auto roughnessTex = LoadMaterialTexture(context, material, aiTextureType_DIFFUSE_ROUGHNESS, false);
    if (!roughnessTex) {
        roughnessTex = FindFallbackTexture(context, materialName, kRoughnessTokens, false);
    }
    if (roughnessTex) {
        result->setRoughnessTexture(roughnessTex);
    }
    
    auto aoTex = LoadMaterialTexture(context, material, aiTextureType_AMBIENT_OCCLUSION, false);
    if (!aoTex) {
        aoTex = FindFallbackTexture(context, materialName, kAOTokens, false);
    }
    if (aoTex) {
        result->setAOTexture(aoTex);
    }
    
    auto ormTex = LoadMaterialTexture(context, material, aiTextureType_GLTF_METALLIC_ROUGHNESS, false);
    if (!ormTex) {
        ormTex = FindFallbackTexture(context, materialName, kORMTokens, false);
    }
    if (ormTex) {
        result->setORMTexture(ormTex);
    }
    
    auto emissionTex = LoadMaterialTexture(context, material, aiTextureType_EMISSIVE, true);
    if (!emissionTex) {
        emissionTex = FindFallbackTexture(context, materialName, kEmissionTokens, true);
    }
    if (emissionTex) {
        result->setEmissionTexture(emissionTex);
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

static std::vector<std::shared_ptr<AnimationClip>> BuildAnimations(const aiScene* scene, const Skeleton& skeleton) {
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
        std::string name = (anim->mName.length > 0) ? anim->mName.C_Str() : ("Animation_" + std::to_string(i));
        clip->setName(name);
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

    Math::Matrix4x4 normalMatrix = transform.inversed().transposed();
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
    entity->addComponent<PrimitiveMesh>()->setType(PrimitiveType::Cube);
    
    return entity;
}

Entity* SceneCommands::createSphere(Scene* scene, const std::string& name) {
    if (!scene) return nullptr;
    
    Entity* entity = scene->createEntity(name);
    
    MeshRenderer* renderer = entity->addComponent<MeshRenderer>();
    renderer->setMesh(Mesh::CreateSphere(0.5f, 32, 16));
    renderer->setMaterial(Material::CreateDefault());
    entity->addComponent<PrimitiveMesh>()->setType(PrimitiveType::Sphere);
    
    return entity;
}

Entity* SceneCommands::createPlane(Scene* scene, const std::string& name) {
    if (!scene) return nullptr;
    
    Entity* entity = scene->createEntity(name);
    
    MeshRenderer* renderer = entity->addComponent<MeshRenderer>();
    renderer->setMesh(Mesh::CreatePlane(10.0f, 10.0f, 1, 1));
    renderer->setMaterial(Material::CreateDefault());
    entity->addComponent<PrimitiveMesh>()->setType(PrimitiveType::Plane);
    
    return entity;
}

Entity* SceneCommands::createCylinder(Scene* scene, const std::string& name) {
    if (!scene) return nullptr;
    
    Entity* entity = scene->createEntity(name);
    
    MeshRenderer* renderer = entity->addComponent<MeshRenderer>();
    renderer->setMesh(Mesh::CreateCylinder(0.5f, 1.0f, 32));
    renderer->setMaterial(Material::CreateDefault());
    entity->addComponent<PrimitiveMesh>()->setType(PrimitiveType::Cylinder);
    
    return entity;
}

Entity* SceneCommands::createCone(Scene* scene, const std::string& name) {
    if (!scene) return nullptr;
    
    Entity* entity = scene->createEntity(name);
    
    MeshRenderer* renderer = entity->addComponent<MeshRenderer>();
    renderer->setMesh(Mesh::CreateCone(0.5f, 1.0f, 32));
    renderer->setMaterial(Material::CreateDefault());
    entity->addComponent<PrimitiveMesh>()->setType(PrimitiveType::Cone);
    
    return entity;
}

Entity* SceneCommands::createTorus(Scene* scene, const std::string& name) {
    if (!scene) return nullptr;
    
    Entity* entity = scene->createEntity(name);
    
    MeshRenderer* renderer = entity->addComponent<MeshRenderer>();
    renderer->setMesh(Mesh::CreateTorus(0.75f, 0.25f, 32, 16));
    renderer->setMaterial(Material::CreateDefault());
    entity->addComponent<PrimitiveMesh>()->setType(PrimitiveType::Torus);
    
    return entity;
}

Entity* SceneCommands::createCapsule(Scene* scene, const std::string& name) {
    if (!scene) return nullptr;
    
    Entity* entity = scene->createEntity(name);
    
    MeshRenderer* renderer = entity->addComponent<MeshRenderer>();
    renderer->setMesh(Mesh::CreateCapsule(0.5f, 2.0f, 16));
    renderer->setMaterial(Material::CreateDefault());
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
    light->setIntensity(10.0f);
    light->setRange(10.0f);
    
    entity->getTransform()->setPosition(Math::Vector3(0, 3, 0));
    
    return entity;
}

Entity* SceneCommands::createSpotLight(Scene* scene, const std::string& name) {
    if (!scene) return nullptr;
    
    Entity* entity = scene->createEntity(name);
    
    Light* light = entity->addComponent<Light>();
    light->setType(Light::Type::Spot);
    light->setColor(Math::Vector3(1.0f, 1.0f, 1.0f));
    light->setIntensity(10.0f);
    light->setRange(10.0f);
    light->setSpotAngle(45.0f);
    
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
        context.animations = BuildAnimations(aiScene, *context.skeleton);
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

    loader->invalidateTexture(path);
    bool srgb = record.textureSettings.srgb;
    if (record.textureSettings.normalMap) {
        srgb = false;
    }
    auto texture = loader->loadTexture(path, srgb, record.textureSettings.flipY);
    if (!texture) {
        return false;
    }

    bool updated = false;
    for (const auto& entityPtr : scene->getAllEntities()) {
        Entity* entity = entityPtr.get();
        if (!entity) {
            continue;
        }

        if (auto* rendererComp = entity->getComponent<MeshRenderer>()) {
            for (const auto& material : rendererComp->getMaterials()) {
                updated |= ReplaceMaterialTexture(material.get(), path, texture);
            }
        }

        if (auto* skinned = entity->getComponent<SkinnedMeshRenderer>()) {
            for (const auto& material : skinned->getMaterials()) {
                updated |= ReplaceMaterialTexture(material.get(), path, texture);
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

} // namespace Crescent
