
// Read types are sync, write types are async. The latest editor was a little bit slow because most calls were sync.
// We can optimize further later if needed. So If anyone want to contribute, feel free to do so. :) 

#import "CrescentEngineBridge.h"
#include "../Engine/Core/Engine.hpp"
#include "../Engine/Core/SelectionSystem.hpp"
#include "../Engine/Renderer/Renderer.hpp"
#include "../Engine/Scene/SceneManager.hpp"
#include "../Engine/Scene/SceneCommands.hpp"
#include "../Engine/Scene/SceneSerializer.hpp"
#include "../Engine/Scene/Scene.hpp"
#include "../Engine/Components/MeshRenderer.hpp"
#include "../Engine/Components/SkinnedMeshRenderer.hpp"
#include "../Engine/Components/InstancedMeshRenderer.hpp"
#include "../Engine/Components/PrimitiveMesh.hpp"
#include "../Engine/Components/Animator.hpp"
#include "../Engine/Components/IKConstraint.hpp"
#include "../Engine/Components/Rigidbody.hpp"
#include "../Engine/Components/PhysicsCollider.hpp"
#include "../Engine/Components/CharacterController.hpp"
#include "../Engine/Components/FirstPersonController.hpp"
#include "../Engine/Components/ThirdPersonController.hpp"
#include "../Engine/Components/BoneAttachment.hpp"
#include "../Engine/Components/Health.hpp"
#include "../Engine/Components/AudioSource.hpp"
#include "../Engine/ECS/Transform.hpp"
#include "../Engine/Assets/AssetDatabase.hpp"
#include "../Engine/Animation/AnimationClip.hpp"
#include "../Engine/Project/Project.hpp"
#include "../Engine/Components/CameraController.hpp"
#include "../Engine/Components/Camera.hpp"
#include "../Engine/Components/Light.hpp"
#include "../Engine/Components/Decal.hpp"
#include <algorithm>
#include <cmath>
#include "../Engine/Physics/PhysicsWorld.hpp"
#include "../Engine/Rendering/Material.hpp"
#include "../Engine/Rendering/Texture.hpp"
#include "../Engine/Rendering/stb_image.h"
#include "../Engine/Rendering/stb_image_write.h"
#include "../../../ThirdParty/nlohmann/json.hpp"
#include <atomic>
#include <dispatch/dispatch.h>
#include <iostream>
#include <iomanip>
#include <mutex>
#include <fstream>
#include <string>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <unordered_map>
#include <vector>
#include <cstring>
#include "../../../ThirdParty/miniaudio/miniaudio.h"

using namespace Crescent;
static const void* kEngineQueueKey = &kEngineQueueKey;

struct MaterialBinding {
    Entity* entity = nullptr;
    MeshRenderer* renderer = nullptr;
    SkinnedMeshRenderer* skinned = nullptr;
    InstancedMeshRenderer* instanced = nullptr;
    std::shared_ptr<Material> material;
};

static MaterialBinding GetMaterialBindingForEntityUUID(const std::string& entityUUID) {
    MaterialBinding binding;
    Scene* scene = SceneManager::getInstance().getActiveScene();
    if (!scene) return binding;

    Entity* entity = SceneCommands::getEntityByUUID(scene, entityUUID);
    if (!entity) return binding;

    binding.entity = entity;
    binding.renderer = entity->getComponent<MeshRenderer>();
    binding.skinned = entity->getComponent<SkinnedMeshRenderer>();
    binding.instanced = entity->getComponent<InstancedMeshRenderer>();

    if (binding.renderer) {
        binding.material = binding.renderer->getMaterial(0);
    }
    if (!binding.material && binding.skinned) {
        binding.material = binding.skinned->getMaterial(0);
    }
    if (!binding.material && binding.instanced) {
        binding.material = binding.instanced->getMaterial(0);
    }
    return binding;
}

static bool IsMaterialShared(Scene* scene,
                             const std::shared_ptr<Material>& material,
                             const Entity* owner) {
    if (!scene || !material) {
        return false;
    }
    for (const auto& entry : scene->getAllEntities()) {
        const Entity* entity = entry.get();
        if (!entity || entity == owner) {
            continue;
        }
        if (const auto* renderer = entity->getComponent<MeshRenderer>()) {
            const auto& materials = renderer->getMaterials();
            for (const auto& current : materials) {
                if (current == material) {
                    return true;
                }
            }
        }
        if (const auto* skinned = entity->getComponent<SkinnedMeshRenderer>()) {
            const auto& materials = skinned->getMaterials();
            for (const auto& current : materials) {
                if (current == material) {
                    return true;
                }
            }
        }
        if (const auto* instanced = entity->getComponent<InstancedMeshRenderer>()) {
            const auto& materials = instanced->getMaterials();
            for (const auto& current : materials) {
                if (current == material) {
                    return true;
                }
            }
        }
    }
    return false;
}

static std::shared_ptr<Material> CloneMaterial(const std::shared_ptr<Material>& source) {
    if (!source) {
        return nullptr;
    }
    auto clone = std::make_shared<Material>(*source);
    if (!source->getName().empty()) {
        clone->setName(source->getName() + " (Instance)");
    }
    return clone;
}

static std::string SanitizeFilename(std::string name) {
    if (name.empty()) {
        return "Material";
    }
    for (char& c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            continue;
        }
        if (c == '_' || c == '-' || c == ' ') {
            continue;
        }
        c = '_';
    }
    for (char& c : name) {
        if (c == ' ') {
            c = '_';
        }
    }
    if (name.empty()) {
        return "Material";
    }
    return name;
}

struct SceneTextureReferenceStats {
    size_t entityCount = 0;
    size_t materialCount = 0;
    size_t textureReferenceCount = 0;
    size_t uniqueTextureCount = 0;
};

static void AccumulateTextureReference(const std::shared_ptr<Texture2D>& texture,
                                      std::unordered_set<const Texture2D*>& uniqueTextures,
                                      size_t& referenceCount) {
    if (!texture) {
        return;
    }
    referenceCount += 1;
    uniqueTextures.insert(texture.get());
}

static void AccumulateMaterialTextureReferences(const std::shared_ptr<Material>& material,
                                               std::unordered_set<const Texture2D*>& uniqueTextures,
                                               size_t& referenceCount) {
    if (!material) {
        return;
    }
    AccumulateTextureReference(material->getAlbedoTexture(), uniqueTextures, referenceCount);
    AccumulateTextureReference(material->getNormalTexture(), uniqueTextures, referenceCount);
    AccumulateTextureReference(material->getMetallicTexture(), uniqueTextures, referenceCount);
    AccumulateTextureReference(material->getRoughnessTexture(), uniqueTextures, referenceCount);
    AccumulateTextureReference(material->getAOTexture(), uniqueTextures, referenceCount);
    AccumulateTextureReference(material->getEmissionTexture(), uniqueTextures, referenceCount);
    AccumulateTextureReference(material->getORMTexture(), uniqueTextures, referenceCount);
    AccumulateTextureReference(material->getHeightTexture(), uniqueTextures, referenceCount);
    AccumulateTextureReference(material->getOpacityTexture(), uniqueTextures, referenceCount);
    AccumulateTextureReference(material->getTerrainControlTexture(), uniqueTextures, referenceCount);
    AccumulateTextureReference(material->getTerrainLayer0Texture(), uniqueTextures, referenceCount);
    AccumulateTextureReference(material->getTerrainLayer1Texture(), uniqueTextures, referenceCount);
    AccumulateTextureReference(material->getTerrainLayer2Texture(), uniqueTextures, referenceCount);
    AccumulateTextureReference(material->getTerrainLayer0NormalTexture(), uniqueTextures, referenceCount);
    AccumulateTextureReference(material->getTerrainLayer1NormalTexture(), uniqueTextures, referenceCount);
    AccumulateTextureReference(material->getTerrainLayer2NormalTexture(), uniqueTextures, referenceCount);
    AccumulateTextureReference(material->getTerrainLayer0ORMTexture(), uniqueTextures, referenceCount);
    AccumulateTextureReference(material->getTerrainLayer1ORMTexture(), uniqueTextures, referenceCount);
    AccumulateTextureReference(material->getTerrainLayer2ORMTexture(), uniqueTextures, referenceCount);
}

static SceneTextureReferenceStats GatherSceneTextureReferenceStats(Scene* scene) {
    SceneTextureReferenceStats stats;
    if (!scene) {
        return stats;
    }

    std::unordered_set<const Texture2D*> uniqueTextures;
    for (const auto& entry : scene->getAllEntities()) {
        Entity* entity = entry.get();
        if (!entity) {
            continue;
        }
        stats.entityCount += 1;

        if (auto* renderer = entity->getComponent<MeshRenderer>()) {
            const auto& materials = renderer->getMaterials();
            stats.materialCount += materials.size();
            for (const auto& material : materials) {
                AccumulateMaterialTextureReferences(material, uniqueTextures, stats.textureReferenceCount);
            }
        }
        if (auto* skinned = entity->getComponent<SkinnedMeshRenderer>()) {
            const auto& materials = skinned->getMaterials();
            stats.materialCount += materials.size();
            for (const auto& material : materials) {
                AccumulateMaterialTextureReferences(material, uniqueTextures, stats.textureReferenceCount);
            }
        }
        if (auto* instanced = entity->getComponent<InstancedMeshRenderer>()) {
            const auto& materials = instanced->getMaterials();
            stats.materialCount += materials.size();
            for (const auto& material : materials) {
                AccumulateMaterialTextureReferences(material, uniqueTextures, stats.textureReferenceCount);
            }
        }
        if (auto* decal = entity->getComponent<Decal>()) {
            AccumulateTextureReference(decal->getAlbedoTexture(), uniqueTextures, stats.textureReferenceCount);
            AccumulateTextureReference(decal->getNormalTexture(), uniqueTextures, stats.textureReferenceCount);
            AccumulateTextureReference(decal->getORMTexture(), uniqueTextures, stats.textureReferenceCount);
            AccumulateTextureReference(decal->getMaskTexture(), uniqueTextures, stats.textureReferenceCount);
        }
    }

    stats.uniqueTextureCount = uniqueTextures.size();
    return stats;
}

static void LogSceneTextureDiagnostics(Scene* scene, const std::string& reason) {
    SceneTextureReferenceStats sceneStats = GatherSceneTextureReferenceStats(scene);
    TextureLiveStats liveStats = Texture2D::getLiveStats();
    double sceneMB = static_cast<double>(liveStats.approximateBytes) / (1024.0 * 1024.0);
    std::cerr << "[SceneTextureStats] " << reason
              << " entities=" << sceneStats.entityCount
              << " materials=" << sceneStats.materialCount
              << " sceneTextureRefs=" << sceneStats.textureReferenceCount
              << " sceneUniqueTextures=" << sceneStats.uniqueTextureCount
              << " liveTextures=" << liveStats.liveTextureCount
              << " approxLiveTextureMB=" << std::fixed << std::setprecision(2) << sceneMB
              << std::endl;
    Texture2D::logLiveStats(reason, 8);
}

static std::string SerializeTexturePath(const std::shared_ptr<Texture2D>& tex, AssetDatabase& db) {
    if (!tex) {
        return "";
    }
    const std::string& path = tex->getPath();
    if (path.empty() || path.rfind("builtin://", 0) == 0) {
        return "";
    }
    const std::string& root = db.getRootPath();
    if (!root.empty() && path.rfind(root, 0) == 0) {
        return db.getRelativePath(path);
    }
    return path;
}

static std::shared_ptr<Texture2D> LoadMaterialTexture(const std::string& storedPath,
                                                      bool srgb,
                                                      Renderer* renderer,
                                                      bool editable = false) {
    if (storedPath.empty() || !renderer || !renderer->getTextureLoader()) {
        return nullptr;
    }
    AssetDatabase& db = AssetDatabase::getInstance();
    std::filesystem::path path(storedPath);
    std::string resolved = path.is_absolute() ? storedPath : db.resolvePath(storedPath);
    std::error_code ec;
    if (!std::filesystem::exists(resolved, ec)) {
        return nullptr;
    }
    if (editable) {
        return renderer->getTextureLoader()->loadTextureUncompressed(resolved, srgb, false);
    }
    return renderer->getTextureLoader()->loadTexture(resolved, srgb, true);
}

static bool ApplyMaterialJson(const nlohmann::json& j,
                              const std::shared_ptr<Material>& material,
                              Renderer* renderer) {
    if (!material) {
        return false;
    }
    if (j.contains("name") && j["name"].is_string()) {
        material->setName(j["name"].get<std::string>());
    }
    if (j.contains("properties") && j["properties"].is_object()) {
        const auto& p = j["properties"];
        if (p.contains("albedo") && p["albedo"].is_array() && p["albedo"].size() >= 4) {
            material->setAlbedo(Math::Vector4(
                p["albedo"][0].get<float>(),
                p["albedo"][1].get<float>(),
                p["albedo"][2].get<float>(),
                p["albedo"][3].get<float>()));
        }
        if (p.contains("metallic")) material->setMetallic(p["metallic"].get<float>());
        if (p.contains("roughness")) material->setRoughness(p["roughness"].get<float>());
        if (p.contains("ao")) material->setAO(p["ao"].get<float>());
        if (p.contains("emission") && p["emission"].is_array() && p["emission"].size() >= 3) {
            material->setEmission(Math::Vector3(
                p["emission"][0].get<float>(),
                p["emission"][1].get<float>(),
                p["emission"][2].get<float>()));
        }
        if (p.contains("emissionStrength")) material->setEmissionStrength(p["emissionStrength"].get<float>());
        if (p.contains("normalScale")) material->setNormalScale(p["normalScale"].get<float>());
        if (p.contains("heightScale")) material->setHeightScale(p["heightScale"].get<float>());
        if (p.contains("heightInvert")) material->setHeightInvert(p["heightInvert"].get<bool>());
        if (p.contains("renderMode")) {
            int mode = p["renderMode"].get<int>();
            mode = std::max(0, std::min(2, mode));
            material->setRenderMode(static_cast<Material::RenderMode>(mode));
        }
        if (p.contains("alphaCutoff")) material->setAlphaCutoff(p["alphaCutoff"].get<float>());
        if (p.contains("cullMode")) {
            int mode = p["cullMode"].get<int>();
            mode = std::max(0, std::min(2, mode));
            material->setCullMode(static_cast<Material::CullMode>(mode));
        }
        if (p.contains("twoSided")) material->setTwoSided(p["twoSided"].get<bool>());
        if (p.contains("alphaToCoverage")) material->setAlphaToCoverage(p["alphaToCoverage"].get<bool>());
        if (p.contains("windEnabled")) material->setWindEnabled(p["windEnabled"].get<bool>());
        if (p.contains("windStrength")) material->setWindStrength(p["windStrength"].get<float>());
        if (p.contains("windSpeed")) material->setWindSpeed(p["windSpeed"].get<float>());
        if (p.contains("windScale")) material->setWindScale(p["windScale"].get<float>());
        if (p.contains("windGust")) material->setWindGust(p["windGust"].get<float>());
        if (p.contains("windDirection") && p["windDirection"].is_array() && p["windDirection"].size() >= 3) {
            material->setWindDirection(Math::Vector3(
                p["windDirection"][0].get<float>(),
                p["windDirection"][1].get<float>(),
                p["windDirection"][2].get<float>()));
        }
        if (p.contains("lodFadeEnabled")) material->setLodFadeEnabled(p["lodFadeEnabled"].get<bool>());
        if (p.contains("lodFadeStart")) material->setLodFadeStart(p["lodFadeStart"].get<float>());
        if (p.contains("lodFadeEnd")) material->setLodFadeEnd(p["lodFadeEnd"].get<float>());
        if (p.contains("ditherEnabled")) material->setDitherEnabled(p["ditherEnabled"].get<bool>());
        if (p.contains("billboardEnabled")) material->setBillboardEnabled(p["billboardEnabled"].get<bool>());
        if (p.contains("billboardStart")) material->setBillboardStart(p["billboardStart"].get<float>());
        if (p.contains("billboardEnd")) material->setBillboardEnd(p["billboardEnd"].get<float>());
        if (p.contains("impostorEnabled")) material->setImpostorEnabled(p["impostorEnabled"].get<bool>());
        if (p.contains("impostorRows")) material->setImpostorRows(p["impostorRows"].get<int>());
        if (p.contains("impostorCols")) material->setImpostorCols(p["impostorCols"].get<int>());
        if (p.contains("terrainEnabled")) material->setTerrainEnabled(p["terrainEnabled"].get<bool>());
        if (p.contains("terrainBlendSharpness")) material->setTerrainBlendSharpness(p["terrainBlendSharpness"].get<float>());
        if (p.contains("terrainHeightStart")) material->setTerrainHeightStart(p["terrainHeightStart"].get<float>());
        if (p.contains("terrainHeightEnd")) material->setTerrainHeightEnd(p["terrainHeightEnd"].get<float>());
        if (p.contains("terrainSlopeStart")) material->setTerrainSlopeStart(p["terrainSlopeStart"].get<float>());
        if (p.contains("terrainSlopeEnd")) material->setTerrainSlopeEnd(p["terrainSlopeEnd"].get<float>());
        if (p.contains("tiling") && p["tiling"].is_array() && p["tiling"].size() >= 2) {
            material->setUVTiling(Math::Vector2(
                p["tiling"][0].get<float>(),
                p["tiling"][1].get<float>()));
        }
        if (p.contains("offset") && p["offset"].is_array() && p["offset"].size() >= 2) {
            material->setUVOffset(Math::Vector2(
                p["offset"][0].get<float>(),
                p["offset"][1].get<float>()));
        }
        if (p.contains("terrainLayer0Tiling") && p["terrainLayer0Tiling"].is_array() && p["terrainLayer0Tiling"].size() >= 2) {
            material->setTerrainLayer0Tiling(Math::Vector2(
                p["terrainLayer0Tiling"][0].get<float>(),
                p["terrainLayer0Tiling"][1].get<float>()));
        }
        if (p.contains("terrainLayer1Tiling") && p["terrainLayer1Tiling"].is_array() && p["terrainLayer1Tiling"].size() >= 2) {
            material->setTerrainLayer1Tiling(Math::Vector2(
                p["terrainLayer1Tiling"][0].get<float>(),
                p["terrainLayer1Tiling"][1].get<float>()));
        }
        if (p.contains("terrainLayer2Tiling") && p["terrainLayer2Tiling"].is_array() && p["terrainLayer2Tiling"].size() >= 2) {
            material->setTerrainLayer2Tiling(Math::Vector2(
                p["terrainLayer2Tiling"][0].get<float>(),
                p["terrainLayer2Tiling"][1].get<float>()));
        }
    }

    if (renderer && j.contains("textures") && j["textures"].is_object()) {
        const auto& t = j["textures"];
        auto load = [&](const char* key, bool srgb, bool editable = false) -> std::shared_ptr<Texture2D> {
            if (!t.contains(key) || !t[key].is_string()) {
                return nullptr;
            }
            return LoadMaterialTexture(t[key].get<std::string>(), srgb, renderer, editable);
        };
        material->setAlbedoTexture(load("albedo", true));
        material->setNormalTexture(load("normal", false));
        material->setMetallicTexture(load("metallic", false));
        material->setRoughnessTexture(load("roughness", false));
        material->setAOTexture(load("ao", false));
        material->setEmissionTexture(load("emission", true));
        material->setORMTexture(load("orm", false));
        material->setHeightTexture(load("height", false));
        material->setTerrainControlTexture(load("terrainControl", false, true));
        material->setTerrainLayer0Texture(load("terrainLayer0", true));
        material->setTerrainLayer1Texture(load("terrainLayer1", true));
        material->setTerrainLayer2Texture(load("terrainLayer2", true));
        material->setTerrainLayer0NormalTexture(load("terrainLayer0Normal", false));
        material->setTerrainLayer1NormalTexture(load("terrainLayer1Normal", false));
        material->setTerrainLayer2NormalTexture(load("terrainLayer2Normal", false));
        material->setTerrainLayer0ORMTexture(load("terrainLayer0ORM", false));
        material->setTerrainLayer1ORMTexture(load("terrainLayer1ORM", false));
        material->setTerrainLayer2ORMTexture(load("terrainLayer2ORM", false));
    }
    return true;
}

static std::shared_ptr<Material> EnsureUniqueMaterialForEntity(MaterialBinding& binding) {
    if (!binding.material || !binding.entity) {
        return binding.material;
    }
    Scene* scene = SceneManager::getInstance().getActiveScene();
    if (!IsMaterialShared(scene, binding.material, binding.entity)) {
        return binding.material;
    }
    auto clone = CloneMaterial(binding.material);
    if (!clone) {
        return binding.material;
    }
    if (binding.renderer) {
        const auto& materials = binding.renderer->getMaterials();
        for (size_t i = 0; i < materials.size(); ++i) {
            if (materials[i] == binding.material) {
                binding.renderer->setMaterial(static_cast<uint32_t>(i), clone);
            }
        }
    }
    if (binding.skinned) {
        const auto& materials = binding.skinned->getMaterials();
        for (size_t i = 0; i < materials.size(); ++i) {
            if (materials[i] == binding.material) {
                binding.skinned->setMaterial(static_cast<uint32_t>(i), clone);
            }
        }
    }
    if (binding.instanced) {
        const auto& materials = binding.instanced->getMaterials();
        for (size_t i = 0; i < materials.size(); ++i) {
            if (materials[i] == binding.material) {
                binding.instanced->setMaterial(static_cast<uint32_t>(i), clone);
            }
        }
    }
    binding.material = clone;
    return binding.material;
}

static std::shared_ptr<Material> GetPrimaryMaterialForEntityUUID(const std::string& entityUUID) {
    Scene* scene = SceneManager::getInstance().getActiveScene();
    if (!scene) return nullptr;
    
    Entity* entity = SceneCommands::getEntityByUUID(scene, entityUUID);
    if (!entity) return nullptr;
    
    MeshRenderer* renderer = entity->getComponent<MeshRenderer>();
    if (renderer) {
        return renderer->getMaterial(0);
    }
    if (auto* skinned = entity->getComponent<SkinnedMeshRenderer>()) {
        return skinned->getMaterial(0);
    }
    if (auto* instanced = entity->getComponent<InstancedMeshRenderer>()) {
        return instanced->getMaterial(0);
    }
    return nullptr;
}

static void EnsureUniqueMaterialsForEntity(Entity* entity) {
    if (!entity) {
        return;
    }
    Scene* scene = SceneManager::getInstance().getActiveScene();
    if (!scene) {
        return;
    }

    auto ensureForRenderer = [&](auto* renderer) {
        if (!renderer) {
            return;
        }
        const auto& materials = renderer->getMaterials();
        std::unordered_map<const Material*, std::shared_ptr<Material>> clones;
        for (size_t i = 0; i < materials.size(); ++i) {
            const auto& material = materials[i];
            if (!material) {
                continue;
            }
            if (!IsMaterialShared(scene, material, entity)) {
                continue;
            }
            auto it = clones.find(material.get());
            if (it == clones.end()) {
                auto clone = CloneMaterial(material);
                if (!clone) {
                    continue;
                }
                it = clones.emplace(material.get(), clone).first;
            }
            renderer->setMaterial(static_cast<uint32_t>(i), it->second);
        }
    };

    ensureForRenderer(entity->getComponent<MeshRenderer>());
    ensureForRenderer(entity->getComponent<SkinnedMeshRenderer>());
    ensureForRenderer(entity->getComponent<InstancedMeshRenderer>());
}

static void ApplyMaterialScalar(const std::shared_ptr<Material>& material,
                                const std::string& prop,
                                float value) {
    if (!material) {
        return;
    }
    if (prop == "metallic") material->setMetallic(value);
    else if (prop == "roughness") material->setRoughness(value);
    else if (prop == "ao") material->setAO(value);
    else if (prop == "emissionStrength") material->setEmissionStrength(value);
    else if (prop == "normalScale") material->setNormalScale(value);
    else if (prop == "heightScale") material->setHeightScale(value);
    else if (prop == "heightInvert") material->setHeightInvert(value >= 0.5f);
    else if (prop == "alpha") material->setAlpha(value);
    else if (prop == "alphaCutoff") material->setAlphaCutoff(value);
    else if (prop == "renderMode") {
        int mode = std::max(0, std::min(2, static_cast<int>(std::round(value))));
        material->setRenderMode(static_cast<Material::RenderMode>(mode));
    }
    else if (prop == "twoSided") {
        material->setTwoSided(value >= 0.5f);
    } else if (prop == "alphaToCoverage") {
        material->setAlphaToCoverage(value >= 0.5f);
    }
    else if (prop == "windEnabled") {
        material->setWindEnabled(value >= 0.5f);
    } else if (prop == "windStrength") {
        material->setWindStrength(value);
    } else if (prop == "windSpeed") {
        material->setWindSpeed(value);
    } else if (prop == "windScale") {
        material->setWindScale(value);
    } else if (prop == "windGust") {
        material->setWindGust(value);
    } else if (prop == "windDirX") {
        Math::Vector3 dir = material->getWindDirection();
        dir.x = value;
        material->setWindDirection(dir);
    } else if (prop == "windDirY") {
        Math::Vector3 dir = material->getWindDirection();
        dir.y = value;
        material->setWindDirection(dir);
    } else if (prop == "windDirZ") {
        Math::Vector3 dir = material->getWindDirection();
        dir.z = value;
        material->setWindDirection(dir);
    } else if (prop == "lodFadeEnabled") {
        material->setLodFadeEnabled(value >= 0.5f);
    } else if (prop == "lodFadeStart") {
        material->setLodFadeStart(value);
    } else if (prop == "lodFadeEnd") {
        material->setLodFadeEnd(value);
    } else if (prop == "ditherEnabled") {
        material->setDitherEnabled(value >= 0.5f);
    } else if (prop == "billboardEnabled") {
        material->setBillboardEnabled(value >= 0.5f);
    } else if (prop == "billboardStart") {
        material->setBillboardStart(value);
    } else if (prop == "billboardEnd") {
        material->setBillboardEnd(value);
    } else if (prop == "impostorEnabled") {
        material->setImpostorEnabled(value >= 0.5f);
    } else if (prop == "impostorRows") {
        material->setImpostorRows(static_cast<int>(std::round(value)));
    } else if (prop == "impostorCols") {
        material->setImpostorCols(static_cast<int>(std::round(value)));
    } else if (prop == "terrainEnabled") {
        material->setTerrainEnabled(value >= 0.5f);
    } else if (prop == "terrainBlendSharpness") {
        material->setTerrainBlendSharpness(value);
    } else if (prop == "terrainHeightStart") {
        material->setTerrainHeightStart(value);
    } else if (prop == "terrainHeightEnd") {
        material->setTerrainHeightEnd(value);
    } else if (prop == "terrainSlopeStart") {
        material->setTerrainSlopeStart(value);
    } else if (prop == "terrainSlopeEnd") {
        material->setTerrainSlopeEnd(value);
    } else if (prop == "terrainLayer0TilingX") {
        Math::Vector2 tiling = material->getTerrainLayer0Tiling();
        tiling.x = value;
        material->setTerrainLayer0Tiling(tiling);
    } else if (prop == "terrainLayer0TilingY") {
        Math::Vector2 tiling = material->getTerrainLayer0Tiling();
        tiling.y = value;
        material->setTerrainLayer0Tiling(tiling);
    } else if (prop == "terrainLayer1TilingX") {
        Math::Vector2 tiling = material->getTerrainLayer1Tiling();
        tiling.x = value;
        material->setTerrainLayer1Tiling(tiling);
    } else if (prop == "terrainLayer1TilingY") {
        Math::Vector2 tiling = material->getTerrainLayer1Tiling();
        tiling.y = value;
        material->setTerrainLayer1Tiling(tiling);
    } else if (prop == "terrainLayer2TilingX") {
        Math::Vector2 tiling = material->getTerrainLayer2Tiling();
        tiling.x = value;
        material->setTerrainLayer2Tiling(tiling);
    } else if (prop == "terrainLayer2TilingY") {
        Math::Vector2 tiling = material->getTerrainLayer2Tiling();
        tiling.y = value;
        material->setTerrainLayer2Tiling(tiling);
    }
    else if (prop == "tilingX") {
        Math::Vector2 tiling = material->getUVTiling();
        tiling.x = value;
        material->setUVTiling(tiling);
    } else if (prop == "tilingY") {
        Math::Vector2 tiling = material->getUVTiling();
        tiling.y = value;
        material->setUVTiling(tiling);
    } else if (prop == "offsetX") {
        Math::Vector2 uvOffset = material->getUVOffset();
        uvOffset.x = value;
        material->setUVOffset(uvOffset);
    } else if (prop == "offsetY") {
        Math::Vector2 uvOffset = material->getUVOffset();
        uvOffset.y = value;
        material->setUVOffset(uvOffset);
    }
}

static void ApplyMaterialColor(const std::shared_ptr<Material>& material,
                               const std::string& prop,
                               float r,
                               float g,
                               float b,
                               float a) {
    if (!material) {
        return;
    }
    if (prop == "albedo") {
        material->setAlbedo(Math::Vector4(r, g, b, a));
    } else if (prop == "emission") {
        material->setEmission(Math::Vector3(r, g, b));
    }
}

static void ApplyMaterialTexture(const std::shared_ptr<Material>& material,
                                 const std::string& slotStr,
                                 const std::shared_ptr<Texture2D>& texture) {
    if (!material) {
        return;
    }
    if (slotStr == "albedo") material->setAlbedoTexture(texture);
    else if (slotStr == "normal") material->setNormalTexture(texture);
    else if (slotStr == "metallic") material->setMetallicTexture(texture);
    else if (slotStr == "roughness") material->setRoughnessTexture(texture);
    else if (slotStr == "ao") material->setAOTexture(texture);
    else if (slotStr == "emission") material->setEmissionTexture(texture);
    else if (slotStr == "orm") material->setORMTexture(texture);
    else if (slotStr == "height") material->setHeightTexture(texture);
    else if (slotStr == "terrainControl") material->setTerrainControlTexture(texture);
    else if (slotStr == "terrainLayer0") material->setTerrainLayer0Texture(texture);
    else if (slotStr == "terrainLayer1") material->setTerrainLayer1Texture(texture);
    else if (slotStr == "terrainLayer2") material->setTerrainLayer2Texture(texture);
    else if (slotStr == "terrainLayer0Normal") material->setTerrainLayer0NormalTexture(texture);
    else if (slotStr == "terrainLayer1Normal") material->setTerrainLayer1NormalTexture(texture);
    else if (slotStr == "terrainLayer2Normal") material->setTerrainLayer2NormalTexture(texture);
    else if (slotStr == "terrainLayer0ORM") material->setTerrainLayer0ORMTexture(texture);
    else if (slotStr == "terrainLayer1ORM") material->setTerrainLayer1ORMTexture(texture);
    else if (slotStr == "terrainLayer2ORM") material->setTerrainLayer2ORMTexture(texture);
}

struct TerrainPaintStrokeState {
    bool active = false;
    bool dirty = false;
    bool uploadPending = false;
    std::string entityUUID;
    std::string texturePath;
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels; // RGBA8 in file orientation (top-left origin)
    float lastU = 0.0f;
    float lastV = 0.0f;
    bool hasLastUV = false;
    double lastUploadTimeSeconds = 0.0;
};

struct TerrainPaintTarget {
    MaterialBinding binding;
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Material> material;
};

struct TerrainBrushParams {
    int layer = 0;
    float radius = 1.0f;
    float hardness = 0.7f;
    float strength = 0.5f;
    float spacing = 0.25f;
    int maskPreset = 0;
    std::string maskPath;
    bool autoNormalize = true;
    bool invert = false;
};

struct TerrainPaintHistoryState {
    std::string texturePath;
    int width = 0;
    int height = 0;
    std::vector<std::vector<unsigned char>> undoStack;
    std::vector<std::vector<unsigned char>> redoStack;
};

enum class TerrainBrushModeCpp : int {
    Paint = 0,
    Sculpt = 1
};

enum class TerrainSculptToolCpp : int {
    Raise = 0,
    Smooth = 1,
    Flatten = 2
};

struct TerrainSculptStrokeState {
    bool active = false;
    std::string entityUUID;
    float lastU = 0.0f;
    float lastV = 0.0f;
    bool hasLastUV = false;
    float flattenHeight = 0.0f;
    bool hasFlattenHeight = false;
};

struct TerrainSculptHistoryState {
    size_t vertexCount = 0;
    std::vector<std::vector<float>> undoStack;
    std::vector<std::vector<float>> redoStack;
};

struct TerrainBrushMaskImage {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> alpha;
    bool valid = false;
};

struct TerrainBrushMaskSampler {
    int preset = 0;
    bool squareDomain = false;
    const TerrainBrushMaskImage* customImage = nullptr;
};

struct TerrainBrushPreviewState {
    std::string entityUUID;
    std::shared_ptr<Texture2D> texture;
    int mode = -1;
    int layer = -1;
    int sculptTool = -1;
    float hardness = -1.0f;
    int maskPreset = -1;
    std::string maskPath;
};

static constexpr int kTerrainBrushMaskPresetCustom = 4;
static constexpr size_t kTerrainPaintUndoLimit = 24;
static constexpr int kTerrainBrushPreviewTextureSize = 128;
static constexpr double kTerrainPaintUploadIntervalSeconds = 1.0 / 90.0;

static bool ResolveTerrainPaintTarget(const std::string& entityUUID, TerrainPaintTarget& outTarget) {
    MaterialBinding binding = GetMaterialBindingForEntityUUID(entityUUID);
    if (!binding.entity || !binding.renderer) {
        return false;
    }
    auto* primitive = binding.entity->getComponent<PrimitiveMesh>();
    if (!primitive || primitive->getType() != PrimitiveType::Plane) {
        return false;
    }
    auto mesh = binding.renderer->getMesh();
    if (!mesh) {
        return false;
    }
    auto material = EnsureUniqueMaterialForEntity(binding);
    if (!material) {
        return false;
    }
    outTarget.binding = std::move(binding);
    outTarget.mesh = std::move(mesh);
    outTarget.material = std::move(material);
    return true;
}

static TerrainBrushModeCpp TerrainBrushModeFromInt(int value) {
    return value == static_cast<int>(TerrainBrushModeCpp::Sculpt)
        ? TerrainBrushModeCpp::Sculpt
        : TerrainBrushModeCpp::Paint;
}

static TerrainSculptToolCpp TerrainSculptToolFromInt(int value) {
    switch (value) {
        case 1: return TerrainSculptToolCpp::Smooth;
        case 2: return TerrainSculptToolCpp::Flatten;
        case 0:
        default:
            return TerrainSculptToolCpp::Raise;
    }
}

static std::shared_ptr<Mesh> CloneMeshForTerrainEditing(const std::shared_ptr<Mesh>& source) {
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

static bool EnsureUniqueTerrainMesh(TerrainPaintTarget& target) {
    if (!target.binding.renderer || !target.mesh) {
        return false;
    }
    if (target.mesh.use_count() <= 2) {
        return true;
    }
    auto clone = CloneMeshForTerrainEditing(target.mesh);
    if (!clone) {
        return false;
    }
    target.binding.renderer->setMesh(clone);
    target.mesh = clone;
    return true;
}

static bool InferTerrainGridVertexDimensions(const Mesh& mesh,
                                             uint32_t& outWidthVerts,
                                             uint32_t& outHeightVerts) {
    const auto& vertices = mesh.getVertices();
    if (vertices.size() < 4) {
        return false;
    }

    const float firstRowV = vertices.front().texCoord.y;
    uint32_t widthVerts = 0;
    while (widthVerts < vertices.size() &&
           std::abs(vertices[widthVerts].texCoord.y - firstRowV) < 1e-4f) {
        ++widthVerts;
    }
    if (widthVerts < 2 || (vertices.size() % widthVerts) != 0) {
        return false;
    }

    uint32_t heightVerts = static_cast<uint32_t>(vertices.size() / widthVerts);
    if (heightVerts < 2) {
        return false;
    }

    outWidthVerts = widthVerts;
    outHeightVerts = heightVerts;
    return true;
}

static bool EnsureTerrainSculptGrid(TerrainPaintTarget& target,
                                    int requestedResolution) {
    if (!target.binding.renderer || !target.mesh) {
        return false;
    }
    if (!EnsureUniqueTerrainMesh(target)) {
        return false;
    }

    uint32_t widthVerts = 0;
    uint32_t heightVerts = 0;
    const bool hasGrid = InferTerrainGridVertexDimensions(*target.mesh, widthVerts, heightVerts);
    const bool needsPromotion = !hasGrid || widthVerts <= 2 || heightVerts <= 2;
    if (!needsPromotion) {
        return true;
    }

    const Math::Vector3 minB = target.mesh->getBoundsMin();
    const Math::Vector3 maxB = target.mesh->getBoundsMax();
    const float width = std::max(maxB.x - minB.x, 0.01f);
    const float depth = std::max(maxB.z - minB.z, 0.01f);
    const uint32_t segments = static_cast<uint32_t>(std::clamp(requestedResolution, 8, 256));
    auto promoted = Mesh::CreatePlane(width, depth, segments, segments);
    if (!promoted) {
        return false;
    }
    promoted->setName(target.mesh->getName().empty() ? "Terrain" : target.mesh->getName());
    promoted->setDoubleSided(target.mesh->isDoubleSided());
    target.binding.renderer->setMesh(promoted);
    target.mesh = promoted;
    return true;
}

static std::vector<float> CaptureTerrainVertexHeights(const std::shared_ptr<Mesh>& mesh) {
    std::vector<float> heights;
    if (!mesh) {
        return heights;
    }
    const auto& vertices = mesh->getVertices();
    heights.reserve(vertices.size());
    for (const auto& vertex : vertices) {
        heights.push_back(vertex.position.y);
    }
    return heights;
}

static bool FloatSnapshotEquals(const std::vector<float>& a,
                                const std::vector<float>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    if (a.empty()) {
        return true;
    }
    return std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

static void EnsureTerrainSculptHistoryCompatible(TerrainSculptHistoryState& history,
                                                 const TerrainPaintTarget& target) {
    const size_t vertexCount = target.mesh ? target.mesh->getVertices().size() : 0;
    if (history.vertexCount == vertexCount) {
        return;
    }
    history.vertexCount = vertexCount;
    history.undoStack.clear();
    history.redoStack.clear();
}

static void PushTerrainSculptUndoSnapshot(TerrainSculptHistoryState& history,
                                          const std::vector<float>& heights) {
    if (heights.empty()) {
        return;
    }
    if (!history.undoStack.empty() && FloatSnapshotEquals(history.undoStack.back(), heights)) {
        history.redoStack.clear();
        return;
    }
    history.undoStack.push_back(heights);
    if (history.undoStack.size() > kTerrainPaintUndoLimit) {
        history.undoStack.erase(history.undoStack.begin());
    }
    history.redoStack.clear();
}

static void PushTerrainSculptRedoSnapshot(TerrainSculptHistoryState& history,
                                          const std::vector<float>& heights) {
    if (heights.empty()) {
        return;
    }
    if (!history.redoStack.empty() && FloatSnapshotEquals(history.redoStack.back(), heights)) {
        return;
    }
    history.redoStack.push_back(heights);
    if (history.redoStack.size() > kTerrainPaintUndoLimit) {
        history.redoStack.erase(history.redoStack.begin());
    }
}

static bool ApplyTerrainVertexHeights(const TerrainPaintTarget& target,
                                      const std::vector<float>& heights) {
    if (!target.binding.renderer || !target.mesh) {
        return false;
    }
    auto vertices = target.mesh->getVertices();
    if (vertices.size() != heights.size()) {
        return false;
    }
    for (size_t i = 0; i < vertices.size(); ++i) {
        vertices[i].position.y = heights[i];
    }
    target.mesh->setVertices(vertices);
    target.mesh->calculateNormals();
    target.mesh->calculateTangents();
    target.binding.renderer->setMesh(target.mesh);
    return true;
}

static bool RayIntersectsTriangleLocal(const Math::Vector3& origin,
                                       const Math::Vector3& direction,
                                       const Math::Vector3& a,
                                       const Math::Vector3& b,
                                       const Math::Vector3& c,
                                       float& outT) {
    const Math::Vector3 edge1 = b - a;
    const Math::Vector3 edge2 = c - a;
    const Math::Vector3 p = direction.cross(edge2);
    const float det = edge1.dot(p);
    if (std::abs(det) < 1e-7f) {
        return false;
    }
    const float invDet = 1.0f / det;
    const Math::Vector3 t = origin - a;
    const float u = t.dot(p) * invDet;
    if (u < 0.0f || u > 1.0f) {
        return false;
    }
    const Math::Vector3 q = t.cross(edge1);
    const float v = direction.dot(q) * invDet;
    if (v < 0.0f || (u + v) > 1.0f) {
        return false;
    }
    const float dist = edge2.dot(q) * invDet;
    if (dist < 0.0f) {
        return false;
    }
    outT = dist;
    return true;
}

static bool LoadRGBA8Image(const std::string& path,
                           std::vector<unsigned char>& pixels,
                           int& width,
                           int& height) {
    width = 0;
    height = 0;
    if (path.empty()) {
        return false;
    }
    int channels = 0;
    stbi_set_flip_vertically_on_load(0);
    stbi_uc* raw = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!raw || width <= 0 || height <= 0) {
        if (raw) {
            stbi_image_free(raw);
        }
        return false;
    }
    pixels.assign(raw, raw + static_cast<size_t>(width) * static_cast<size_t>(height) * 4ull);
    stbi_image_free(raw);
    return true;
}

static bool LoadMaskAlphaImage(const std::string& path,
                               TerrainBrushMaskImage& outImage) {
    outImage = TerrainBrushMaskImage{};
    if (path.empty()) {
        return false;
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(0);
    stbi_uc* raw = stbi_load(path.c_str(), &width, &height, &channels, STBI_grey);
    if (!raw || width <= 0 || height <= 0) {
        if (raw) {
            stbi_image_free(raw);
        }
        return false;
    }
    outImage.width = width;
    outImage.height = height;
    outImage.alpha.assign(raw, raw + static_cast<size_t>(width) * static_cast<size_t>(height));
    outImage.valid = true;
    stbi_image_free(raw);
    return true;
}

static bool SaveRGBA8Image(const std::string& path,
                           const std::vector<unsigned char>& pixels,
                           int width,
                           int height) {
    if (path.empty() || width <= 0 || height <= 0) {
        return false;
    }
    if (pixels.size() < static_cast<size_t>(width) * static_cast<size_t>(height) * 4ull) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    int ok = stbi_write_png(path.c_str(), width, height, 4, pixels.data(), width * 4);
    return ok != 0;
}

static double TerrainPaintNowSeconds() {
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

static std::string BuildTerrainControlTexturePath(Entity* entity) {
    std::string base;
    if (auto project = ProjectManager::getInstance().getActiveProject()) {
        base = project->getAssetsPath();
    }
    if (base.empty()) {
        base = AssetDatabase::getInstance().getRootPath();
    }
    if (base.empty()) {
        base = (std::filesystem::current_path() / "Assets").string();
    }
    std::string safeName = SanitizeFilename(entity ? entity->getName() : "Terrain");
    std::string uuidStr = entity ? entity->getUUID().toString() : "00000000";
    if (uuidStr.size() > 8) {
        uuidStr = uuidStr.substr(0, 8);
    }
    std::filesystem::path path = std::filesystem::path(base) / "Generated" / "TerrainControl";
    path /= safeName + "_" + uuidStr + "_control.png";
    return path.lexically_normal().string();
}

static float SmoothStep(float edge0, float edge1, float x) {
    if (edge0 == edge1) {
        return x < edge0 ? 0.0f : 1.0f;
    }
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static float HashNoise2D(int x, int y) {
    uint32_t h = 2166136261u;
    h ^= static_cast<uint32_t>(x) + 0x9e3779b9u + (h << 6) + (h >> 2);
    h ^= static_cast<uint32_t>(y) + 0x85ebca6bu + (h << 6) + (h >> 2);
    h *= 16777619u;
    return static_cast<float>(h & 0x00ffffffu) / 16777215.0f;
}

static float SampleMaskImageBilinear(const TerrainBrushMaskImage& image,
                                     float u,
                                     float v) {
    if (!image.valid || image.width <= 0 || image.height <= 0 || image.alpha.empty()) {
        return 1.0f;
    }
    float x = std::clamp(u, 0.0f, 1.0f) * float(std::max(image.width - 1, 0));
    float y = std::clamp(v, 0.0f, 1.0f) * float(std::max(image.height - 1, 0));
    int x0 = std::clamp(static_cast<int>(std::floor(x)), 0, image.width - 1);
    int y0 = std::clamp(static_cast<int>(std::floor(y)), 0, image.height - 1);
    int x1 = std::min(x0 + 1, image.width - 1);
    int y1 = std::min(y0 + 1, image.height - 1);
    float tx = x - float(x0);
    float ty = y - float(y0);

    auto sample = [&](int px, int py) -> float {
        size_t index = static_cast<size_t>(py) * static_cast<size_t>(image.width) + static_cast<size_t>(px);
        if (index >= image.alpha.size()) {
            return 0.0f;
        }
        return image.alpha[index] / 255.0f;
    };

    float s00 = sample(x0, y0);
    float s10 = sample(x1, y0);
    float s01 = sample(x0, y1);
    float s11 = sample(x1, y1);
    float sx0 = s00 + (s10 - s00) * tx;
    float sx1 = s01 + (s11 - s01) * tx;
    return std::clamp(sx0 + (sx1 - sx0) * ty, 0.0f, 1.0f);
}

static float SampleTerrainBrushPreset(int preset,
                                      float brushU,
                                      float brushV,
                                      float nx,
                                      float ny,
                                      float hardness) {
    const float radial = std::sqrt(nx * nx + ny * ny);
    const float square = std::max(std::abs(nx), std::abs(ny));

    switch (preset) {
        case 1: // hard round
            return radial <= 1.0f ? 1.0f : 0.0f;
        case 2: { // noisy/splat round
            if (radial > 1.0f) {
                return 0.0f;
            }
            float base = 1.0f - SmoothStep(hardness, 1.0f, radial);
            int ix = static_cast<int>(brushU * 63.0f);
            int iy = static_cast<int>(brushV * 63.0f);
            float n = HashNoise2D(ix, iy);
            return std::clamp(base * (0.65f + 0.35f * n), 0.0f, 1.0f);
        }
        case 3: { // soft square
            if (square > 1.0f) {
                return 0.0f;
            }
            return 1.0f - SmoothStep(hardness, 1.0f, square);
        }
        case 0:
        default: // soft round
            if (radial > 1.0f) {
                return 0.0f;
            }
            return 1.0f - SmoothStep(hardness, 1.0f, radial);
    }
}

static void ConfigureTerrainBrushMaskSampler(TerrainBrushMaskSampler& sampler,
                                             const TerrainBrushParams& brush,
                                             std::unordered_map<std::string, TerrainBrushMaskImage>& cache) {
    sampler = TerrainBrushMaskSampler{};
    sampler.preset = std::clamp(brush.maskPreset, 0, kTerrainBrushMaskPresetCustom);
    sampler.squareDomain = (sampler.preset == 3);

    if (sampler.preset != kTerrainBrushMaskPresetCustom || brush.maskPath.empty()) {
        return;
    }

    std::filesystem::path rawPath(brush.maskPath);
    std::string normalizedPath = rawPath.lexically_normal().string();
    if (normalizedPath.empty()) {
        return;
    }

    auto it = cache.find(normalizedPath);
    if (it == cache.end()) {
        TerrainBrushMaskImage loaded;
        if (!LoadMaskAlphaImage(normalizedPath, loaded)) {
            cache[normalizedPath] = TerrainBrushMaskImage{};
            return;
        }
        it = cache.emplace(normalizedPath, std::move(loaded)).first;
    }
    if (!it->second.valid) {
        return;
    }
    sampler.customImage = &it->second;
    sampler.squareDomain = true;
}

static float SampleTerrainBrushMask(const TerrainBrushMaskSampler& sampler,
                                    float brushU,
                                    float brushV,
                                    float nx,
                                    float ny,
                                    float hardness) {
    if (sampler.customImage) {
        return SampleMaskImageBilinear(*sampler.customImage, brushU, brushV);
    }
    return SampleTerrainBrushPreset(sampler.preset, brushU, brushV, nx, ny, hardness);
}

static bool SnapshotEquals(const std::vector<unsigned char>& a,
                           const std::vector<unsigned char>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    if (a.empty()) {
        return true;
    }
    return std::memcmp(a.data(), b.data(), a.size()) == 0;
}

static void EnsureTerrainHistoryCompatible(TerrainPaintHistoryState& history,
                                           const TerrainPaintStrokeState& state) {
    if (history.texturePath == state.texturePath &&
        history.width == state.width &&
        history.height == state.height) {
        return;
    }
    history.texturePath = state.texturePath;
    history.width = state.width;
    history.height = state.height;
    history.undoStack.clear();
    history.redoStack.clear();
}

static void PushTerrainUndoSnapshot(TerrainPaintHistoryState& history,
                                    const std::vector<unsigned char>& pixels) {
    if (pixels.empty()) {
        return;
    }
    if (!history.undoStack.empty() && SnapshotEquals(history.undoStack.back(), pixels)) {
        history.redoStack.clear();
        return;
    }
    history.undoStack.push_back(pixels);
    if (history.undoStack.size() > kTerrainPaintUndoLimit) {
        history.undoStack.erase(history.undoStack.begin());
    }
    history.redoStack.clear();
}

static void PushTerrainRedoSnapshot(TerrainPaintHistoryState& history,
                                    const std::vector<unsigned char>& pixels) {
    if (pixels.empty()) {
        return;
    }
    if (!history.redoStack.empty() && SnapshotEquals(history.redoStack.back(), pixels)) {
        return;
    }
    history.redoStack.push_back(pixels);
    if (history.redoStack.size() > kTerrainPaintUndoLimit) {
        history.redoStack.erase(history.redoStack.begin());
    }
}

static Math::Vector3 TerrainLayerPreviewColor(int layer) {
    switch (std::clamp(layer, 0, 2)) {
        case 1: return Math::Vector3(0.32f, 0.85f, 0.45f);
        case 2: return Math::Vector3(0.28f, 0.62f, 0.95f);
        case 0:
        default: return Math::Vector3(0.96f, 0.62f, 0.25f);
    }
}

static bool EnsureTerrainControlPaintData(TerrainPaintStrokeState& state,
                                          const TerrainPaintTarget& target,
                                          Renderer* renderer) {
    if (!target.material || !target.binding.entity || !renderer || !renderer->getTextureLoader()) {
        return false;
    }
    std::string texturePath;
    if (auto control = target.material->getTerrainControlTexture()) {
        const std::string& currentPath = control->getPath();
        if (!currentPath.empty() && currentPath.rfind("builtin://", 0) != 0) {
            texturePath = currentPath;
        }
    }
    if (texturePath.empty()) {
        texturePath = BuildTerrainControlTexturePath(target.binding.entity);
    }

    auto hasEditableRuntimeTexture = [&]() -> bool {
        auto current = target.material->getTerrainControlTexture();
        if (!current) {
            return false;
        }
        return current->isEditableRGBA8();
    };

    if (state.entityUUID == target.binding.entity->getUUID().toString() &&
        state.texturePath == texturePath &&
        state.width > 0 && state.height > 0 &&
        state.pixels.size() >= static_cast<size_t>(state.width) * static_cast<size_t>(state.height) * 4ull &&
        hasEditableRuntimeTexture()) {
        return true;
    }

    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    if (!LoadRGBA8Image(texturePath, pixels, width, height)) {
        width = 1024;
        height = 1024;
        pixels.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4ull, 0u);
        for (size_t i = 0; i < pixels.size(); i += 4) {
            pixels[i + 0] = 255; // Default to layer 0
            pixels[i + 3] = 255;
        }
        SaveRGBA8Image(texturePath, pixels, width, height);
        AssetDatabase::getInstance().registerAsset(texturePath);
    }

    auto* loader = renderer->getTextureLoader();
    std::string runtimeKey = "builtin://terrain/control_runtime/"
        + target.binding.entity->getUUID().toString();
    std::shared_ptr<Texture2D> texture = loader->createTextureFromRGBA8(
        runtimeKey,
        pixels.data(),
        width,
        height,
        false,
        true
    );
    if (!texture) {
        return false;
    }
    texture->setPath(texturePath);
    if (!loader->updateTextureFromRGBA8(texture, pixels.data(), width, height, true)) {
        return false;
    }

    target.material->setTerrainControlTexture(texture);
    target.material->setTerrainEnabled(true);
    state.entityUUID = target.binding.entity->getUUID().toString();
    state.texturePath = texturePath;
    state.width = width;
    state.height = height;
    state.pixels = std::move(pixels);
    state.dirty = false;
    state.hasLastUV = false;
    return true;
}

static bool ScreenToTerrainUV(const TerrainPaintTarget& target,
                              float screenX,
                              float screenY,
                              float screenWidth,
                              float screenHeight,
                              float& outU,
                              float& outV,
                              Math::Vector3* outWorldHit = nullptr,
                              Math::Vector3* outLocalHit = nullptr) {
    if (!target.binding.entity || !target.mesh) {
        return false;
    }
    Scene* activeScene = SceneManager::getInstance().getActiveScene();
    Camera* camera = Camera::getMainCamera();
    if (!activeScene || !camera || screenWidth <= 1.0f || screenHeight <= 1.0f) {
        return false;
    }

    Ray ray = SelectionSystem::screenPointToRay(
        Math::Vector2(screenX, screenY),
        Math::Vector2(screenWidth, screenHeight),
        camera
    );

    Math::Matrix4x4 world = target.binding.entity->getTransform()->getWorldMatrix();
    Math::Matrix4x4 invWorld = world.inversed();
    Math::Vector3 localOrigin = invWorld.transformPoint(ray.origin);
    Math::Vector3 localDir = invWorld.transformDirection(ray.direction);

    Math::Vector3 localHit;
    bool hitMesh = false;
    float bestT = std::numeric_limits<float>::max();
    const auto& vertices = target.mesh->getVertices();
    const auto& indices = target.mesh->getIndices();
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const uint32_t i0 = indices[i];
        const uint32_t i1 = indices[i + 1];
        const uint32_t i2 = indices[i + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
            continue;
        }
        float t = 0.0f;
        if (!RayIntersectsTriangleLocal(localOrigin,
                                        localDir,
                                        vertices[i0].position,
                                        vertices[i1].position,
                                        vertices[i2].position,
                                        t)) {
            continue;
        }
        if (t < bestT) {
            bestT = t;
            localHit = localOrigin + localDir * t;
            hitMesh = true;
        }
    }

    if (!hitMesh) {
        if (std::abs(localDir.y) < 1e-6f) {
            return false;
        }
        float t = -localOrigin.y / localDir.y;
        if (t < 0.0f) {
            return false;
        }
        localHit = localOrigin + localDir * t;
    }

    Math::Vector3 minB = target.mesh->getBoundsMin();
    Math::Vector3 maxB = target.mesh->getBoundsMax();
    if (localHit.x < minB.x || localHit.x > maxB.x || localHit.z < minB.z || localHit.z > maxB.z) {
        return false;
    }

    float width = std::max(maxB.x - minB.x, 1e-6f);
    float depth = std::max(maxB.z - minB.z, 1e-6f);
    outU = std::clamp((localHit.x - minB.x) / width, 0.0f, 1.0f);
    outV = std::clamp((localHit.z - minB.z) / depth, 0.0f, 1.0f);
    if (outLocalHit) {
        *outLocalHit = localHit;
    }
    if (outWorldHit) {
        *outWorldHit = world.transformPoint(localHit);
    }
    return true;
}

static bool ResolveTerrainPaintTargetAtCursor(const std::string& preferredEntityUUID,
                                              float screenX,
                                              float screenY,
                                              float screenWidth,
                                              float screenHeight,
                                              TerrainPaintTarget& outTarget,
                                              std::string& outEntityUUID,
                                              float& outU,
                                              float& outV,
                                              Math::Vector3* outWorldHit = nullptr) {
    if (!preferredEntityUUID.empty()) {
        TerrainPaintTarget preferredTarget;
        float u = 0.0f;
        float v = 0.0f;
        Math::Vector3 worldHit = Math::Vector3::Zero;
        if (ResolveTerrainPaintTarget(preferredEntityUUID, preferredTarget) &&
            ScreenToTerrainUV(preferredTarget, screenX, screenY, screenWidth, screenHeight, u, v, &worldHit)) {
            outTarget = std::move(preferredTarget);
            outEntityUUID = preferredEntityUUID;
            outU = u;
            outV = v;
            if (outWorldHit) {
                *outWorldHit = worldHit;
            }
            return true;
        }
    }

    Scene* scene = SceneManager::getInstance().getActiveScene();
    Camera* camera = Camera::getMainCamera();
    if (!scene || !camera || screenWidth <= 1.0f || screenHeight <= 1.0f) {
        return false;
    }

    Ray ray = SelectionSystem::screenPointToRay(
        Math::Vector2(screenX, screenY),
        Math::Vector2(screenWidth, screenHeight),
        camera
    );

    std::vector<Entity*> entities;
    const auto& allEntities = scene->getAllEntities();
    entities.reserve(allEntities.size());
    for (const auto& entry : allEntities) {
        if (entry) {
            entities.push_back(entry.get());
        }
    }
    RaycastHit hit = SelectionSystem::raycastAll(ray, entities);
    if (!hit.hit || !hit.entity) {
        return false;
    }

    auto* primitive = hit.entity->getComponent<PrimitiveMesh>();
    if (!primitive || primitive->getType() != PrimitiveType::Plane) {
        return false;
    }

    std::string fallbackUUID = hit.entity->getUUID().toString();
    TerrainPaintTarget fallbackTarget;
    float u = 0.0f;
    float v = 0.0f;
    Math::Vector3 worldHit = Math::Vector3::Zero;
    if (!ResolveTerrainPaintTarget(fallbackUUID, fallbackTarget) ||
        !ScreenToTerrainUV(fallbackTarget, screenX, screenY, screenWidth, screenHeight, u, v, &worldHit)) {
        return false;
    }

    outTarget = std::move(fallbackTarget);
    outEntityUUID = fallbackUUID;
    outU = u;
    outV = v;
    if (outWorldHit) {
        *outWorldHit = worldHit;
    }
    return true;
}

static float BrushFalloff(float distance01, float hardness) {
    if (distance01 >= 1.0f) {
        return 0.0f;
    }
    hardness = std::clamp(hardness, 0.001f, 1.0f);
    if (distance01 <= hardness) {
        return 1.0f;
    }
    float t = (distance01 - hardness) / std::max(1.0f - hardness, 1e-6f);
    t = std::clamp(t, 0.0f, 1.0f);
    float smooth = t * t * (3.0f - 2.0f * t);
    return 1.0f - smooth;
}

static void ApplyBrushSample(TerrainPaintStrokeState& state,
                             const TerrainPaintTarget& target,
                             float u,
                             float v,
                             const TerrainBrushParams& brush,
                             const TerrainBrushMaskSampler& maskSampler) {
    if (state.width <= 0 || state.height <= 0 || state.pixels.empty() || !target.binding.entity || !target.mesh) {
        return;
    }

    Math::Vector3 minB = target.mesh->getBoundsMin();
    Math::Vector3 maxB = target.mesh->getBoundsMax();
    float localWidth = std::max(maxB.x - minB.x, 1e-6f);
    float localDepth = std::max(maxB.z - minB.z, 1e-6f);
    Math::Vector3 scale = target.binding.entity->getTransform()->getScale();
    float worldScaleX = std::max(std::abs(scale.x), 1e-4f);
    float worldScaleZ = std::max(std::abs(scale.z), 1e-4f);
    float localRadiusX = std::max(brush.radius / worldScaleX, 1e-4f);
    float localRadiusZ = std::max(brush.radius / worldScaleZ, 1e-4f);
    float radiusUvX = localRadiusX / localWidth;
    float radiusUvY = localRadiusZ / localDepth;

    float cx = std::clamp(u, 0.0f, 1.0f) * float(state.width - 1);
    float cy = (1.0f - std::clamp(v, 0.0f, 1.0f)) * float(state.height - 1);
    float rx = std::max(radiusUvX * float(state.width - 1), 0.5f);
    float ry = std::max(radiusUvY * float(state.height - 1), 0.5f);

    int minX = std::max(0, static_cast<int>(std::floor(cx - rx - 1.0f)));
    int maxX = std::min(state.width - 1, static_cast<int>(std::ceil(cx + rx + 1.0f)));
    int minY = std::max(0, static_cast<int>(std::floor(cy - ry - 1.0f)));
    int maxY = std::min(state.height - 1, static_cast<int>(std::ceil(cy + ry + 1.0f)));
    int layer = std::clamp(brush.layer, 0, 2);
    float signedStrength = brush.invert ? -brush.strength : brush.strength;
    signedStrength = std::clamp(signedStrength, -1.0f, 1.0f);

    for (int py = minY; py <= maxY; ++py) {
        float ny = (float(py) - cy) / ry;
        for (int px = minX; px <= maxX; ++px) {
            float nx = (float(px) - cx) / rx;
            if (std::max(std::abs(nx), std::abs(ny)) > 1.0f) {
                continue;
            }
            float d = maskSampler.squareDomain
                ? std::max(std::abs(nx), std::abs(ny))
                : std::sqrt(nx * nx + ny * ny);
            if (d > 1.0f) {
                continue;
            }
            float mask = SampleTerrainBrushMask(
                maskSampler,
                std::clamp(nx * 0.5f + 0.5f, 0.0f, 1.0f),
                std::clamp(ny * 0.5f + 0.5f, 0.0f, 1.0f),
                nx,
                ny,
                brush.hardness
            );
            if (mask <= 1e-6f) {
                continue;
            }
            float weight = BrushFalloff(d, brush.hardness) * mask;
            float strength = signedStrength * weight;
            if (std::abs(strength) < 1e-6f) {
                continue;
            }

            size_t index = (static_cast<size_t>(py) * static_cast<size_t>(state.width) + static_cast<size_t>(px)) * 4ull;
            float channels[3] = {
                state.pixels[index + 0] / 255.0f,
                state.pixels[index + 1] / 255.0f,
                state.pixels[index + 2] / 255.0f
            };
            float selected = channels[layer];
            float targetValue = selected;
            if (strength >= 0.0f) {
                targetValue = selected + strength * (1.0f - selected);
            } else {
                targetValue = selected + strength * selected;
            }
            targetValue = std::clamp(targetValue, 0.0f, 1.0f);

            if (brush.autoNormalize) {
                float otherSum = 0.0f;
                for (int i = 0; i < 3; ++i) {
                    if (i != layer) {
                        otherSum += channels[i];
                    }
                }
                if (otherSum > 1e-6f) {
                    float scaleOther = std::max(0.0f, 1.0f - targetValue) / otherSum;
                    for (int i = 0; i < 3; ++i) {
                        if (i != layer) {
                            channels[i] = std::clamp(channels[i] * scaleOther, 0.0f, 1.0f);
                        }
                    }
                } else {
                    float shared = std::max(0.0f, 1.0f - targetValue) * 0.5f;
                    for (int i = 0; i < 3; ++i) {
                        if (i != layer) {
                            channels[i] = shared;
                        }
                    }
                }
                channels[layer] = targetValue;
            } else {
                channels[layer] = targetValue;
            }

            state.pixels[index + 0] = static_cast<unsigned char>(std::clamp(channels[0], 0.0f, 1.0f) * 255.0f + 0.5f);
            state.pixels[index + 1] = static_cast<unsigned char>(std::clamp(channels[1], 0.0f, 1.0f) * 255.0f + 0.5f);
            state.pixels[index + 2] = static_cast<unsigned char>(std::clamp(channels[2], 0.0f, 1.0f) * 255.0f + 0.5f);
            state.pixels[index + 3] = 255;
            state.dirty = true;
            state.uploadPending = true;
        }
    }
}

static void ApplyTerrainBrushStroke(TerrainPaintStrokeState& state,
                                    const TerrainPaintTarget& target,
                                    float u,
                                    float v,
                                    const TerrainBrushParams& brush,
                                    const TerrainBrushMaskSampler& maskSampler,
                                    bool beginStroke) {
    if (beginStroke || !state.hasLastUV) {
        ApplyBrushSample(state, target, u, v, brush, maskSampler);
        state.lastU = u;
        state.lastV = v;
        state.hasLastUV = true;
        return;
    }

    float localWidth = std::max(target.mesh->getBoundsMax().x - target.mesh->getBoundsMin().x, 1e-6f);
    float localDepth = std::max(target.mesh->getBoundsMax().z - target.mesh->getBoundsMin().z, 1e-6f);
    Math::Vector3 scale = target.binding.entity->getTransform()->getScale();
    float worldScaleX = std::max(std::abs(scale.x), 1e-4f);
    float worldScaleZ = std::max(std::abs(scale.z), 1e-4f);
    float localRadiusX = std::max(brush.radius / worldScaleX, 1e-4f);
    float localRadiusZ = std::max(brush.radius / worldScaleZ, 1e-4f);
    float radiusUvX = localRadiusX / localWidth;
    float radiusUvY = localRadiusZ / localDepth;
    float radiusPxX = std::max(radiusUvX * float(state.width - 1), 0.5f);
    float radiusPxY = std::max(radiusUvY * float(state.height - 1), 0.5f);
    float spacingPx = std::max(0.5f, std::min(radiusPxX, radiusPxY) * std::clamp(brush.spacing, 0.05f, 1.0f));

    float dx = (u - state.lastU) * float(state.width - 1);
    float dy = (v - state.lastV) * float(state.height - 1);
    float distancePx = std::sqrt(dx * dx + dy * dy);
    int steps = std::max(1, static_cast<int>(std::ceil(distancePx / spacingPx)));
    for (int i = 1; i <= steps; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(steps);
        float sampleU = state.lastU + (u - state.lastU) * t;
        float sampleV = state.lastV + (v - state.lastV) * t;
        ApplyBrushSample(state, target, sampleU, sampleV, brush, maskSampler);
    }
    state.lastU = u;
    state.lastV = v;
    state.hasLastUV = true;
}

static void CommitTerrainPaintState(TerrainPaintStrokeState& state) {
    if (!state.dirty || state.texturePath.empty() || state.width <= 0 || state.height <= 0) {
        state.hasLastUV = false;
        return;
    }
    if (SaveRGBA8Image(state.texturePath, state.pixels, state.width, state.height)) {
        AssetDatabase::getInstance().registerAsset(state.texturePath);
        state.dirty = false;
    }
    state.hasLastUV = false;
}

static void UploadTerrainPaintTextureIfNeeded(TerrainPaintStrokeState& state,
                                              const TerrainPaintTarget& target,
                                              Renderer* renderer,
                                              bool force) {
    if (!renderer || !renderer->getTextureLoader()) {
        return;
    }
    if (state.width <= 0 || state.height <= 0 || state.pixels.empty()) {
        return;
    }
    auto controlTexture = target.material ? target.material->getTerrainControlTexture() : nullptr;
    if (!controlTexture) {
        return;
    }
    if (!force && !state.uploadPending) {
        return;
    }

    double now = TerrainPaintNowSeconds();
    if (!force && (now - state.lastUploadTimeSeconds) < kTerrainPaintUploadIntervalSeconds) {
        return;
    }

    renderer->getTextureLoader()->updateTextureFromRGBA8(
        controlTexture,
        state.pixels.data(),
        state.width,
        state.height,
        true
    );
    state.lastUploadTimeSeconds = now;
    state.uploadPending = false;
}

static Math::Vector3 TerrainBrushPreviewColor(TerrainBrushModeCpp mode,
                                              int layer,
                                              TerrainSculptToolCpp sculptTool) {
    if (mode == TerrainBrushModeCpp::Paint) {
        return TerrainLayerPreviewColor(layer);
    }
    switch (sculptTool) {
        case TerrainSculptToolCpp::Smooth: return Math::Vector3(0.42f, 0.78f, 0.96f);
        case TerrainSculptToolCpp::Flatten: return Math::Vector3(0.92f, 0.86f, 0.58f);
        case TerrainSculptToolCpp::Raise:
        default:
            return Math::Vector3(0.98f, 0.56f, 0.32f);
    }
}

static void ApplyTerrainSculptSample(TerrainPaintTarget& target,
                                     float u,
                                     float v,
                                     const TerrainBrushParams& brush,
                                     const TerrainBrushMaskSampler& maskSampler,
                                     TerrainSculptToolCpp sculptTool,
                                     TerrainSculptStrokeState& sculptState,
                                     bool beginStroke) {
    if (!target.binding.entity || !target.mesh) {
        return;
    }

    auto vertices = target.mesh->getVertices();
    if (vertices.empty()) {
        return;
    }

    uint32_t widthVerts = 0;
    uint32_t heightVerts = 0;
    if (!InferTerrainGridVertexDimensions(*target.mesh, widthVerts, heightVerts) ||
        widthVerts == 0 || heightVerts == 0) {
        return;
    }

    const Math::Vector3 minB = target.mesh->getBoundsMin();
    const Math::Vector3 maxB = target.mesh->getBoundsMax();
    const float localWidth = std::max(maxB.x - minB.x, 1e-6f);
    const float localDepth = std::max(maxB.z - minB.z, 1e-6f);
    const Math::Vector3 scale = target.binding.entity->getTransform()->getScale();
    const float worldScaleX = std::max(std::abs(scale.x), 1e-4f);
    const float worldScaleZ = std::max(std::abs(scale.z), 1e-4f);
    const float localRadiusX = std::max(brush.radius / worldScaleX, 1e-4f);
    const float localRadiusZ = std::max(brush.radius / worldScaleZ, 1e-4f);
    const float centerX = minB.x + std::clamp(u, 0.0f, 1.0f) * localWidth;
    const float centerZ = minB.z + std::clamp(v, 0.0f, 1.0f) * localDepth;

    const std::vector<Vertex> originalVertices = vertices;
    const float strength = std::clamp(brush.strength, 0.0f, 1.0f);
    const float signedRaise = brush.invert ? -1.0f : 1.0f;
    const float displacementScale = std::max(brush.radius * 0.15f, 0.02f) * strength;

    if (sculptTool == TerrainSculptToolCpp::Flatten && beginStroke && !sculptState.hasFlattenHeight) {
        const size_t centerIndex = static_cast<size_t>(std::round(std::clamp(v, 0.0f, 1.0f) * float(heightVerts - 1))) * widthVerts
            + static_cast<size_t>(std::round(std::clamp(u, 0.0f, 1.0f) * float(widthVerts - 1)));
        if (centerIndex < originalVertices.size()) {
            sculptState.flattenHeight = originalVertices[centerIndex].position.y;
            sculptState.hasFlattenHeight = true;
        }
    }

    for (uint32_t row = 0; row < heightVerts; ++row) {
        for (uint32_t col = 0; col < widthVerts; ++col) {
            const size_t index = static_cast<size_t>(row) * widthVerts + col;
            Vertex& vertex = vertices[index];
            const Vertex& original = originalVertices[index];

            const float dx = (vertex.position.x - centerX) / localRadiusX;
            const float dz = (vertex.position.z - centerZ) / localRadiusZ;
            if (std::max(std::abs(dx), std::abs(dz)) > 1.0f) {
                continue;
            }

            const float d = maskSampler.squareDomain
                ? std::max(std::abs(dx), std::abs(dz))
                : std::sqrt(dx * dx + dz * dz);
            if (d > 1.0f) {
                continue;
            }

            const float mask = SampleTerrainBrushMask(
                maskSampler,
                std::clamp(dx * 0.5f + 0.5f, 0.0f, 1.0f),
                std::clamp(dz * 0.5f + 0.5f, 0.0f, 1.0f),
                dx,
                dz,
                brush.hardness
            );
            if (mask <= 1e-6f) {
                continue;
            }

            const float weight = BrushFalloff(d, brush.hardness) * mask;
            if (weight <= 1e-6f) {
                continue;
            }

            switch (sculptTool) {
                case TerrainSculptToolCpp::Smooth: {
                    float sum = 0.0f;
                    float count = 0.0f;
                    const int rowStart = std::max<int>(0, static_cast<int>(row) - 1);
                    const int rowEnd = std::min<int>(static_cast<int>(heightVerts) - 1, static_cast<int>(row) + 1);
                    const int colStart = std::max<int>(0, static_cast<int>(col) - 1);
                    const int colEnd = std::min<int>(static_cast<int>(widthVerts) - 1, static_cast<int>(col) + 1);
                    for (int sampleRow = rowStart; sampleRow <= rowEnd; ++sampleRow) {
                        for (int sampleCol = colStart; sampleCol <= colEnd; ++sampleCol) {
                            const size_t sampleIndex = static_cast<size_t>(sampleRow) * widthVerts + static_cast<size_t>(sampleCol);
                            sum += originalVertices[sampleIndex].position.y;
                            count += 1.0f;
                        }
                    }
                    const float avg = count > 0.0f ? (sum / count) : original.position.y;
                    const float alpha = std::clamp(weight * strength, 0.0f, 1.0f);
                    vertex.position.y = original.position.y + (avg - original.position.y) * alpha;
                    break;
                }
                case TerrainSculptToolCpp::Flatten: {
                    if (!sculptState.hasFlattenHeight) {
                        break;
                    }
                    const float alpha = std::clamp(weight * strength, 0.0f, 1.0f);
                    vertex.position.y = original.position.y + (sculptState.flattenHeight - original.position.y) * alpha;
                    break;
                }
                case TerrainSculptToolCpp::Raise:
                default:
                    vertex.position.y = original.position.y + signedRaise * displacementScale * weight;
                    break;
            }
        }
    }

    target.mesh->setVertices(vertices);
    target.mesh->calculateNormals();
    target.mesh->calculateTangents();
    target.binding.renderer->setMesh(target.mesh);
}

static void ApplyTerrainSculptStroke(TerrainSculptStrokeState& sculptState,
                                     TerrainPaintTarget& target,
                                     float u,
                                     float v,
                                     const TerrainBrushParams& brush,
                                     const TerrainBrushMaskSampler& maskSampler,
                                     TerrainSculptToolCpp sculptTool,
                                     bool beginStroke) {
    if (beginStroke || !sculptState.hasLastUV) {
        ApplyTerrainSculptSample(target, u, v, brush, maskSampler, sculptTool, sculptState, true);
        sculptState.lastU = u;
        sculptState.lastV = v;
        sculptState.hasLastUV = true;
        return;
    }

    const Math::Vector3 minB = target.mesh->getBoundsMin();
    const Math::Vector3 maxB = target.mesh->getBoundsMax();
    const float localWidth = std::max(maxB.x - minB.x, 1e-6f);
    const float localDepth = std::max(maxB.z - minB.z, 1e-6f);
    const Math::Vector3 scale = target.binding.entity->getTransform()->getScale();
    const float worldScaleX = std::max(std::abs(scale.x), 1e-4f);
    const float worldScaleZ = std::max(std::abs(scale.z), 1e-4f);
    const float localRadiusX = std::max(brush.radius / worldScaleX, 1e-4f);
    const float localRadiusZ = std::max(brush.radius / worldScaleZ, 1e-4f);
    const float radiusUvX = localRadiusX / localWidth;
    const float radiusUvY = localRadiusZ / localDepth;
    const float spacingUv = std::max(0.0005f, std::min(radiusUvX, radiusUvY) * std::clamp(brush.spacing, 0.05f, 1.0f));
    const float du = u - sculptState.lastU;
    const float dv = v - sculptState.lastV;
    const float distanceUv = std::sqrt(du * du + dv * dv);
    const int steps = std::max(1, static_cast<int>(std::ceil(distanceUv / spacingUv)));

    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float sampleU = sculptState.lastU + du * t;
        const float sampleV = sculptState.lastV + dv * t;
        ApplyTerrainSculptSample(target, sampleU, sampleV, brush, maskSampler, sculptTool, sculptState, false);
    }
    sculptState.lastU = u;
    sculptState.lastV = v;
    sculptState.hasLastUV = true;
}

static std::shared_ptr<Texture2D> BuildTerrainBrushPreviewTexture(Renderer* renderer,
                                                                  const TerrainBrushParams& brush,
                                                                  const TerrainBrushMaskSampler& maskSampler) {
    if (!renderer || !renderer->getTextureLoader()) {
        return nullptr;
    }

    const int size = kTerrainBrushPreviewTextureSize;
    std::vector<unsigned char> pixels(static_cast<size_t>(size) * static_cast<size_t>(size) * 4ull, 0u);
    Math::Vector3 layerColor = TerrainLayerPreviewColor(brush.layer);

    for (int y = 0; y < size; ++y) {
        float v = float(y) / float(std::max(size - 1, 1));
        float ny = v * 2.0f - 1.0f;
        for (int x = 0; x < size; ++x) {
            float u = float(x) / float(std::max(size - 1, 1));
            float nx = u * 2.0f - 1.0f;
            float radial = std::sqrt(nx * nx + ny * ny);
            float square = std::max(std::abs(nx), std::abs(ny));
            float d = maskSampler.squareDomain ? square : radial;
            if (d > 1.0f) {
                continue;
            }

            float mask = SampleTerrainBrushMask(maskSampler, u, v, nx, ny, brush.hardness);
            if (mask <= 0.001f) {
                continue;
            }

            float body = BrushFalloff(d, brush.hardness) * 0.22f * mask;
            float outerRing = SmoothStep(0.86f, 0.95f, d) * (1.0f - SmoothStep(0.95f, 1.02f, d));
            float hardRing = SmoothStep(brush.hardness - 0.025f, brush.hardness + 0.025f, d)
                * (1.0f - SmoothStep(brush.hardness + 0.025f, brush.hardness + 0.055f, d));
            float alpha = std::clamp(std::max(body, std::max(outerRing, hardRing * 0.6f)), 0.0f, 1.0f);
            if (alpha <= 0.001f) {
                continue;
            }

            size_t index = (static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)) * 4ull;
            pixels[index + 0] = static_cast<unsigned char>(std::clamp(layerColor.x, 0.0f, 1.0f) * 255.0f + 0.5f);
            pixels[index + 1] = static_cast<unsigned char>(std::clamp(layerColor.y, 0.0f, 1.0f) * 255.0f + 0.5f);
            pixels[index + 2] = static_cast<unsigned char>(std::clamp(layerColor.z, 0.0f, 1.0f) * 255.0f + 0.5f);
            pixels[index + 3] = static_cast<unsigned char>(alpha * 255.0f + 0.5f);
        }
    }

    std::string key = "builtin://terrain/brush_preview/"
        + std::to_string(std::clamp(brush.layer, 0, 2)) + "_"
        + std::to_string(static_cast<int>(std::round(std::clamp(brush.hardness, 0.0f, 1.0f) * 1000.0f))) + "_"
        + std::to_string(std::clamp(brush.maskPreset, 0, kTerrainBrushMaskPresetCustom)) + "_"
        + std::to_string(std::hash<std::string>{}(brush.maskPath));

    return renderer->getTextureLoader()->createTextureFromRGBA8(
        key,
        pixels.data(),
        size,
        size,
        true,
        false
    );
}

static Entity* EnsureTerrainBrushPreviewEntity(Scene* scene,
                                               TerrainBrushPreviewState& previewState) {
    if (!scene) {
        previewState.entityUUID.clear();
        return nullptr;
    }

    Entity* previewEntity = nullptr;
    if (!previewState.entityUUID.empty()) {
        previewEntity = SceneCommands::getEntityByUUID(scene, previewState.entityUUID);
    }
    if (previewEntity) {
        return previewEntity;
    }

    previewEntity = scene->createEntity("Terrain Brush Preview");
    if (!previewEntity) {
        previewState.entityUUID.clear();
        return nullptr;
    }
    previewEntity->setEditorOnly(true);
    previewEntity->setActive(true);
    previewEntity->addComponent<Decal>();
    previewState.entityUUID = previewEntity->getUUID().toString();
    return previewEntity;
}

static SkinnedMeshRenderer* GetSkinnedByUUID(const std::string& entityUUID) {
    Scene* scene = SceneManager::getInstance().getActiveScene();
    if (!scene) return nullptr;
    
    Entity* entity = SceneCommands::getEntityByUUID(scene, entityUUID);
    if (!entity) return nullptr;
    
    return entity->getComponent<SkinnedMeshRenderer>();
}

static Animator* GetAnimatorByUUID(const std::string& entityUUID) {
    Scene* scene = SceneManager::getInstance().getActiveScene();
    if (!scene) return nullptr;
    
    Entity* entity = SceneCommands::getEntityByUUID(scene, entityUUID);
    if (!entity) return nullptr;
    
    return entity->getComponent<Animator>();
}

static Animator* FindAnimatorInParents(Entity* entity) {
    Transform* current = entity ? entity->getTransform() : nullptr;
    while (current) {
        Entity* currentEntity = current->getEntity();
        if (currentEntity) {
            if (auto* animator = currentEntity->getComponent<Animator>()) {
                return animator;
            }
        }
        current = current->getParent();
    }
    return nullptr;
}

static IKConstraint* GetIKByUUID(const std::string& entityUUID) {
    Scene* scene = SceneManager::getInstance().getActiveScene();
    if (!scene) return nullptr;

    Entity* entity = SceneCommands::getEntityByUUID(scene, entityUUID);
    if (!entity) return nullptr;
    
    return entity->getComponent<IKConstraint>();
}

static SkinnedMeshRenderer* FindSkinnedInHierarchy(Entity* entity, Animator* owner) {
    if (!entity) {
        return nullptr;
    }
    Animator* animator = entity->getComponent<Animator>();
    if (animator && animator != owner) {
        return nullptr;
    }
    if (auto* skinned = entity->getComponent<SkinnedMeshRenderer>()) {
        return skinned;
    }
    Transform* transform = entity->getTransform();
    if (!transform) {
        return nullptr;
    }
    for (Transform* child : transform->getChildren()) {
        if (!child) {
            continue;
        }
        if (auto* found = FindSkinnedInHierarchy(child->getEntity(), owner)) {
            return found;
        }
    }
    return nullptr;
}

static void CollectSkinnedInHierarchy(Entity* entity,
                                      Animator* owner,
                                      std::vector<SkinnedMeshRenderer*>& out) {
    if (!entity) {
        return;
    }
    Animator* animator = entity->getComponent<Animator>();
    if (animator && animator != owner) {
        return;
    }
    if (auto* skinned = entity->getComponent<SkinnedMeshRenderer>()) {
        out.push_back(skinned);
    }
    Transform* transform = entity->getTransform();
    if (!transform) {
        return;
    }
    for (Transform* child : transform->getChildren()) {
        if (!child) {
            continue;
        }
        CollectSkinnedInHierarchy(child->getEntity(), owner, out);
    }
}

static bool ResolveAnimatorAndSkinned(const std::string& uuid,
                                      Animator*& outAnimator,
                                      SkinnedMeshRenderer*& outSkinned) {
    outAnimator = GetAnimatorByUUID(uuid);
    if (!outAnimator) {
        Scene* scene = SceneManager::getInstance().getActiveScene();
        Entity* entity = scene ? SceneCommands::getEntityByUUID(scene, uuid) : nullptr;
        outAnimator = FindAnimatorInParents(entity);
    }
    if (!outAnimator) {
        outSkinned = GetSkinnedByUUID(uuid);
        if (!outSkinned) {
            Scene* scene = SceneManager::getInstance().getActiveScene();
            Entity* entity = scene ? SceneCommands::getEntityByUUID(scene, uuid) : nullptr;
            outSkinned = FindSkinnedInHierarchy(entity, nullptr);
        }
        return outSkinned != nullptr;
    }
    outSkinned = GetSkinnedByUUID(uuid);
    if (!outSkinned) {
        Entity* animatorEntity = outAnimator->getEntity();
        outSkinned = FindSkinnedInHierarchy(animatorEntity, outAnimator);
    }
    return outSkinned != nullptr;
}

static bool ResolveAnimatorAndSkinnedTargets(const std::string& uuid,
                                             Animator*& outAnimator,
                                             SkinnedMeshRenderer*& outSkinned,
                                             std::vector<SkinnedMeshRenderer*>& outTargets) {
    outTargets.clear();
    outAnimator = nullptr;
    outSkinned = nullptr;

    Scene* scene = SceneManager::getInstance().getActiveScene();
    Entity* entity = scene ? SceneCommands::getEntityByUUID(scene, uuid) : nullptr;

    outAnimator = GetAnimatorByUUID(uuid);
    if (!outAnimator && entity) {
        outAnimator = FindAnimatorInParents(entity);
    }

    if (outAnimator) {
        if (Entity* animatorEntity = outAnimator->getEntity()) {
            CollectSkinnedInHierarchy(animatorEntity, outAnimator, outTargets);
        }
        if (!outTargets.empty()) {
            outSkinned = outTargets.front();
        }
        return outSkinned != nullptr;
    }

    if (entity) {
        CollectSkinnedInHierarchy(entity, nullptr, outTargets);
    }
    if (outTargets.empty()) {
        if (SkinnedMeshRenderer* direct = GetSkinnedByUUID(uuid)) {
            outTargets.push_back(direct);
        }
    }
    if (!outTargets.empty()) {
        outSkinned = outTargets.front();
    }
    return outSkinned != nullptr;
}

static SceneCommands::ModelImportOptions ResolveAnimationImportOptions(const std::string& path,
                                                                       const std::string& guid) {
    SceneCommands::ModelImportOptions options;
    AssetDatabase& db = AssetDatabase::getInstance();
    AssetRecord record;
    bool hasRecord = false;
    if (!guid.empty()) {
        hasRecord = db.getRecordForGuid(guid, record);
    }
    if (!hasRecord && !path.empty()) {
        hasRecord = db.getRecordForPath(path, record);
    }
    if (hasRecord && record.type == "model") {
        options = record.modelSettings;
    }
    return options;
}

static NSString* AnimatorParamTypeToString(AnimatorParameterType type) {
    switch (type) {
        case AnimatorParameterType::Int: return @"Int";
        case AnimatorParameterType::Bool: return @"Bool";
        case AnimatorParameterType::Trigger: return @"Trigger";
        case AnimatorParameterType::Float:
        default: return @"Float";
    }
}

static AnimatorParameterType AnimatorParamTypeFromString(NSString* type) {
    if (!type) return AnimatorParameterType::Float;
    if ([type isEqualToString:@"Int"]) return AnimatorParameterType::Int;
    if ([type isEqualToString:@"Bool"]) return AnimatorParameterType::Bool;
    if ([type isEqualToString:@"Trigger"]) return AnimatorParameterType::Trigger;
    return AnimatorParameterType::Float;
}

static NSString* AnimatorCondOpToString(AnimatorConditionOp op) {
    switch (op) {
        case AnimatorConditionOp::IfFalse: return @"IfFalse";
        case AnimatorConditionOp::Greater: return @"Greater";
        case AnimatorConditionOp::Less: return @"Less";
        case AnimatorConditionOp::GreaterEqual: return @"GreaterEqual";
        case AnimatorConditionOp::LessEqual: return @"LessEqual";
        case AnimatorConditionOp::Equal: return @"Equal";
        case AnimatorConditionOp::NotEqual: return @"NotEqual";
        case AnimatorConditionOp::IfTrue:
        default: return @"IfTrue";
    }
}

static AnimatorConditionOp AnimatorCondOpFromString(NSString* op) {
    if (!op) return AnimatorConditionOp::IfTrue;
    if ([op isEqualToString:@"IfFalse"]) return AnimatorConditionOp::IfFalse;
    if ([op isEqualToString:@"Greater"]) return AnimatorConditionOp::Greater;
    if ([op isEqualToString:@"Less"]) return AnimatorConditionOp::Less;
    if ([op isEqualToString:@"GreaterEqual"]) return AnimatorConditionOp::GreaterEqual;
    if ([op isEqualToString:@"LessEqual"]) return AnimatorConditionOp::LessEqual;
    if ([op isEqualToString:@"Equal"]) return AnimatorConditionOp::Equal;
    if ([op isEqualToString:@"NotEqual"]) return AnimatorConditionOp::NotEqual;
    return AnimatorConditionOp::IfTrue;
}

static NSString* AnimatorStateTypeToString(AnimatorStateType type) {
    switch (type) {
        case AnimatorStateType::BlendTree: return @"BlendTree";
        case AnimatorStateType::Clip:
        default: return @"Clip";
    }
}

static AnimatorStateType AnimatorStateTypeFromString(NSString* type) {
    if (type && [type isEqualToString:@"BlendTree"]) return AnimatorStateType::BlendTree;
    return AnimatorStateType::Clip;
}

static NSString* AnimatorBlendTreeTypeToString(AnimatorBlendTreeType type) {
    switch (type) {
        case AnimatorBlendTreeType::Blend1D:
        default: return @"Blend1D";
    }
}

static AnimatorBlendTreeType AnimatorBlendTreeTypeFromString(NSString* type) {
    (void)type;
    return AnimatorBlendTreeType::Blend1D;
}

@implementation CrescentEngineBridge {
    Crescent::Engine* _engine;
    dispatch_queue_t _engineQueue;
    std::atomic_bool _frameInFlight;
    std::mutex _inputMutex;
    float _pendingMouseDeltaX;
    float _pendingMouseDeltaY;
    bool _hasPendingMouseDelta;
    float _pendingMouseDragX;
    float _pendingMouseDragY;
    float _pendingMouseDragW;
    float _pendingMouseDragH;
    bool _hasPendingMouseDrag;
    TerrainPaintStrokeState _terrainPaintState;
    std::unordered_map<std::string, TerrainPaintHistoryState> _terrainPaintHistory;
    TerrainSculptStrokeState _terrainSculptState;
    std::unordered_map<std::string, TerrainSculptHistoryState> _terrainSculptHistory;
    std::unordered_map<std::string, TerrainBrushMaskImage> _terrainMaskCache;
    TerrainBrushPreviewState _terrainBrushPreview;
}

+ (instancetype)shared {
    static CrescentEngineBridge *sharedInstance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        sharedInstance = [[self alloc] init];
    });
    return sharedInstance;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _engine = &Crescent::Engine::getInstance();
        dispatch_queue_attr_t attr = dispatch_queue_attr_make_with_qos_class(
            DISPATCH_QUEUE_SERIAL,
            QOS_CLASS_USER_INTERACTIVE,
            0
        );
        _engineQueue = dispatch_queue_create("com.crescent.engine.queue", attr);
        dispatch_queue_set_specific(_engineQueue, kEngineQueueKey, (void *)kEngineQueueKey, nullptr);
        _frameInFlight.store(false);
        _pendingMouseDeltaX = 0.0f;
        _pendingMouseDeltaY = 0.0f;
        _hasPendingMouseDelta = false;
        _pendingMouseDragX = 0.0f;
        _pendingMouseDragY = 0.0f;
        _pendingMouseDragW = 0.0f;
        _pendingMouseDragH = 0.0f;
        _hasPendingMouseDrag = false;
    }
    return self;
}

- (BOOL)isOnEngineQueue {
    return dispatch_get_specific(kEngineQueueKey) != nullptr;
}

- (void)performAsync:(dispatch_block_t)block {
    if ([self isOnEngineQueue]) {
        @autoreleasepool {
            block();
        }
        return;
    }
    dispatch_async(_engineQueue, ^{
        @autoreleasepool {
            block();
        }
    });
}

- (void)performSync:(dispatch_block_t)block {
    if ([self isOnEngineQueue]) {
        @autoreleasepool {
            block();
        }
        return;
    }
    dispatch_sync(_engineQueue, ^{
        @autoreleasepool {
            block();
        }
    });
}

- (id)performSyncObject:(id (^)(void))block {
    if ([self isOnEngineQueue]) {
        @autoreleasepool {
            return block();
        }
    }
    __block id result = nil;
    dispatch_sync(_engineQueue, ^{
        @autoreleasepool {
            result = block();
        }
    });
    return result;
}

- (BOOL)performSyncBool:(BOOL (^)(void))block {
    if ([self isOnEngineQueue]) {
        @autoreleasepool {
            return block();
        }
    }
    __block BOOL result = NO;
    dispatch_sync(_engineQueue, ^{
        @autoreleasepool {
            result = block();
        }
    });
    return result;
}

- (float)performSyncFloat:(float (^)(void))block {
    if ([self isOnEngineQueue]) {
        @autoreleasepool {
            return block();
        }
    }
    __block float result = 0.0f;
    dispatch_sync(_engineQueue, ^{
        @autoreleasepool {
            result = block();
        }
    });
    return result;
}

- (int)performSyncInt:(int (^)(void))block {
    if ([self isOnEngineQueue]) {
        @autoreleasepool {
            return block();
        }
    }
    __block int result = 0;
    dispatch_sync(_engineQueue, ^{
        @autoreleasepool {
            result = block();
        }
    });
    return result;
}

- (void)applyPendingInput {
    if (!_engine) {
        return;
    }
    float deltaX = 0.0f;
    float deltaY = 0.0f;
    bool hasDelta = false;
    float dragX = 0.0f;
    float dragY = 0.0f;
    float dragW = 0.0f;
    float dragH = 0.0f;
    bool hasDrag = false;
    {
        std::lock_guard<std::mutex> lock(_inputMutex);
        if (_hasPendingMouseDelta) {
            deltaX = _pendingMouseDeltaX;
            deltaY = _pendingMouseDeltaY;
            _pendingMouseDeltaX = 0.0f;
            _pendingMouseDeltaY = 0.0f;
            _hasPendingMouseDelta = false;
            hasDelta = true;
        }
        if (_hasPendingMouseDrag) {
            dragX = _pendingMouseDragX;
            dragY = _pendingMouseDragY;
            dragW = _pendingMouseDragW;
            dragH = _pendingMouseDragH;
            _hasPendingMouseDrag = false;
            hasDrag = true;
        }
    }
    if (hasDelta) {
        _engine->handleMouseMove(deltaX, deltaY);
    }
    if (hasDrag) {
        _engine->handleMouseDrag(dragX, dragY, dragW, dragH);
    }
}

- (BOOL)initialize {
    return [self performSyncBool:^BOOL {
        if (_engine) {
            return _engine->initialize();
        }
        return NO;
    }];
}

- (void)shutdown {
    [self performSync:^{
        if (_engine) {
            _engine->shutdown();
        }
    }];
}

- (void)update:(float)deltaTime {
    [self performAsync:^{
        if (_engine) {
            [self applyPendingInput];
            _engine->update(deltaTime);
        }
    }];
}

- (void)render {
    [self performAsync:^{
        if (_engine) {
            _engine->render();
        }
    }];
}

- (BOOL)tick:(float)deltaTime {
    if (!_engine) {
        return NO;
    }
    bool expected = false;
    if (!_frameInFlight.compare_exchange_strong(expected, true)) {
        return NO;
    }
    [self performAsync:^{
        if (_engine) {
            [self applyPendingInput];
            _engine->update(deltaTime);
            _engine->render();
        }
        _frameInFlight.store(false);
    }];
    return YES;
}

- (void)setMetalLayer:(CAMetalLayer *)layer {
    [self setSceneMetalLayer:layer];
}

- (void)resizeWithWidth:(float)width height:(float)height {
    [self resizeSceneWithWidth:width height:height];
}

- (void)setSceneMetalLayer:(CAMetalLayer *)layer {
    [self performSync:^{
        if (_engine) {
            _engine->setSceneMetalLayer((__bridge void*)layer);
        }
    }];
}

- (void)setGameMetalLayer:(CAMetalLayer *)layer {
    [self performSync:^{
        if (_engine) {
            _engine->setGameMetalLayer((__bridge void*)layer);
        }
    }];
}

- (void)setPreviewMetalLayer:(CAMetalLayer *)layer {
    [self performSync:^{
        if (_engine) {
            _engine->setPreviewMetalLayer((__bridge void*)layer);
        }
    }];
}

- (void)resizeSceneWithWidth:(float)width height:(float)height {
    [self performSync:^{
        if (_engine) {
            _engine->resizeScene(width, height);
        }
    }];
}

- (void)resizeGameWithWidth:(float)width height:(float)height {
    [self performSync:^{
        if (_engine) {
            _engine->resizeGame(width, height);
        }
    }];
}

- (void)resizePreviewWithWidth:(float)width height:(float)height {
    [self performSync:^{
        if (_engine) {
            _engine->resizePreview(width, height);
        }
    }];
}

- (void)setAnimationPreviewTargetUUID:(NSString *)uuid {
    [self performSync:^{
        if (_engine) {
            _engine->setAnimationPreviewTargetUUID(uuid ? uuid.UTF8String : "");
        }
    }];
}

- (void)setAnimationPreviewPlaybackState:(NSDictionary *)info {
    [self performSync:^{
        if (!_engine || !info) {
            return;
        }
        Crescent::Engine::AnimationPreviewPlaybackState state;
        if (NSNumber* clipIndex = info[@"clipIndex"]) {
            state.clipIndex = clipIndex.intValue;
        }
        if (NSNumber* time = info[@"time"]) {
            state.time = std::max(0.0f, time.floatValue);
        }
        if (NSNumber* playing = info[@"playing"]) {
            state.playing = playing.boolValue;
        }
        if (NSNumber* looping = info[@"looping"]) {
            state.looping = looping.boolValue;
        }
        if (NSNumber* speed = info[@"speed"]) {
            state.speed = std::max(0.0f, speed.floatValue);
        }
        _engine->setAnimationPreviewPlaybackState(state);
    }];
}

- (BOOL)playPreviewAudioClip:(NSString *)path
                      volume:(float)volume
                       pitch:(float)pitch {
    return [self performSyncBool:^BOOL {
        if (!path || path.length == 0) {
            return NO;
        }

        std::string resolvedPath = path.UTF8String;
        std::filesystem::path candidate(resolvedPath);
        if (!candidate.is_absolute()) {
            resolvedPath = AssetDatabase::getInstance().resolvePath(resolvedPath);
        }

        std::error_code ec;
        if (!std::filesystem::exists(resolvedPath, ec)) {
            return NO;
        }

        return AudioSystem::getInstance().playOneShot(resolvedPath,
                                                      std::max(0.0f, volume),
                                                      std::max(0.01f, pitch));
    }];
}

- (float)getPreviewAudioClipDuration:(NSString *)path {
    __block float durationSeconds = 0.0f;
    [self performSync:^{
        if (!path || path.length == 0) {
            return;
        }

        std::string resolvedPath = path.UTF8String ? path.UTF8String : "";
        if (resolvedPath.empty()) {
            return;
        }

        std::filesystem::path fsPath(resolvedPath);
        if (!fsPath.is_absolute()) {
            resolvedPath = AssetDatabase::getInstance().resolvePath(resolvedPath);
        }

        std::error_code ec;
        if (resolvedPath.empty() || !std::filesystem::exists(resolvedPath, ec)) {
            return;
        }

        ma_decoder decoder;
        ma_result result = ma_decoder_init_file(resolvedPath.c_str(), nullptr, &decoder);
        if (result != MA_SUCCESS) {
            return;
        }
        ma_uint32 sampleRate = 0;
        ma_uint64 frameCount = 0;
        if (ma_decoder_get_data_format(&decoder, nullptr, nullptr, &sampleRate, nullptr, 0) == MA_SUCCESS &&
            ma_decoder_get_length_in_pcm_frames(&decoder, &frameCount) == MA_SUCCESS &&
            sampleRate > 0 && frameCount > 0) {
            durationSeconds = static_cast<float>(static_cast<double>(frameCount) / static_cast<double>(sampleRate));
        }
        ma_decoder_uninit(&decoder);
    }];
    return durationSeconds;
}

- (NSString *)createAnimationPreviewCloneFromUUID:(NSString *)uuid {
    return (NSString *)[self performSyncObject:^id{
        if (!_engine || !uuid || uuid.length == 0) {
            return @"";
        }
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) {
            return @"";
        }

        Entity* source = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!source) {
            return @"";
        }

        std::vector<Entity*> duplicates = SceneSerializer::DuplicateEntities(scene, { source });
        Entity* clone = duplicates.empty() ? nullptr : duplicates.front();
        if (!clone) {
            return @"";
        }

        std::vector<Transform*> stack;
        if (Transform* root = clone->getTransform()) {
            stack.push_back(root);
        }
        while (!stack.empty()) {
            Transform* node = stack.back();
            stack.pop_back();
            if (!node) {
                continue;
            }
            if (Entity* entity = node->getEntity()) {
                entity->setEditorOnly(true);
            }
            const auto& children = node->getChildren();
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                stack.push_back(*it);
            }
        }

        clone->setName(source->getName() + " Preview");
        if (Transform* cloneTransform = clone->getTransform()) {
            Math::Vector3 pos = cloneTransform->getPosition();
            cloneTransform->setPosition(Math::Vector3(pos.x, pos.y + 10000.0f, pos.z));
        }

        return [NSString stringWithUTF8String:clone->getUUID().toString().c_str()];
    }];
}

- (void)destroyAnimationPreviewCloneUUID:(NSString *)uuid {
    [self performAsync:^{
        if (!_engine || !uuid || uuid.length == 0) {
            return;
        }
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) {
            return;
        }
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (entity) {
            scene->destroyEntity(entity);
        }
    }];
}

- (void)handleKeyDown:(unsigned short)keyCode {
    [self performAsync:^{
        if (_engine) {
            _engine->handleKeyDown(keyCode);
        }
    }];
}

- (void)handleKeyUp:(unsigned short)keyCode {
    [self performAsync:^{
        if (_engine) {
            _engine->handleKeyUp(keyCode);
        }
    }];
}

- (void)handleMouseMoveWithDeltaX:(float)deltaX deltaY:(float)deltaY {
    std::lock_guard<std::mutex> lock(_inputMutex);
    _pendingMouseDeltaX += deltaX;
    _pendingMouseDeltaY += deltaY;
    _hasPendingMouseDelta = true;
}

- (void)handleMouseButton:(int)button pressed:(BOOL)pressed {
    [self performAsync:^{
        if (_engine) {
            _engine->handleMouseButton(button, pressed);
        }
    }];
}

// MARK: - Scene Editing Commands

- (void)createCube {
    [self performAsync:^{
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (scene) {
            Crescent::SceneCommands::createCube(scene);
        }
    }];
}

- (void)createSphere {
    [self performAsync:^{
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (scene) {
            Crescent::SceneCommands::createSphere(scene);
        }
    }];
}

- (void)createPlane {
    [self performAsync:^{
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (scene) {
            Crescent::SceneCommands::createPlane(scene);
        }
    }];
}

- (void)createCylinder {
    [self performAsync:^{
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (scene) {
            Crescent::SceneCommands::createCylinder(scene);
        }
    }];
}

- (void)createCone {
    [self performAsync:^{
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (scene) {
            Crescent::SceneCommands::createCone(scene);
        }
    }];
}

- (void)createTorus {
    [self performAsync:^{
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (scene) {
            Crescent::SceneCommands::createTorus(scene);
        }
    }];
}

- (void)createCapsule {
    [self performAsync:^{
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (scene) {
            Crescent::SceneCommands::createCapsule(scene);
        }
    }];
}

- (void)createDirectionalLight {
    [self performAsync:^{
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (scene) {
            Crescent::SceneCommands::createDirectionalLight(scene);
        }
    }];
}

- (void)createPointLight {
    [self performAsync:^{
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (scene) {
            Crescent::SceneCommands::createPointLight(scene);
        }
    }];
}

- (void)createSpotLight {
    [self performAsync:^{
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (scene) {
            Crescent::SceneCommands::createSpotLight(scene);
        }
    }];
}

- (void)createDecal {
    [self performAsync:^{
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (scene) {
            Crescent::SceneCommands::createDecal(scene);
        }
    }];
}

- (void)createCamera {
    [self performAsync:^{
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (scene) {
            Crescent::SceneCommands::createCamera(scene);
        }
    }];
}

- (void)createEmpty {
    [self performAsync:^{
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (scene) {
            Crescent::SceneCommands::createEmpty(scene);
        }
    }];
}

- (BOOL)importModelAtPath:(NSString *)path {
    return [self performSyncBool:^BOOL {
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (!scene || !path) {
            return NO;
        }
        std::string modelPath = [path UTF8String];
        Crescent::Entity* entity = Crescent::SceneCommands::importModel(scene, modelPath);
        return entity != nullptr;
    }];
}

- (BOOL)importModelAtPath:(NSString *)path options:(NSDictionary *)options {
    return [self performSyncBool:^BOOL {
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (!scene || !path) {
            return NO;
        }
        Crescent::SceneCommands::ModelImportOptions importOptions;
        if (options) {
            NSNumber* scale = options[@"scale"];
            if (scale) {
                importOptions.scale = std::max(0.0001f, scale.floatValue);
            }
            NSNumber* flipUVs = options[@"flipUVs"];
            if (flipUVs) {
                importOptions.flipUVs = flipUVs.boolValue;
            }
            NSNumber* onlyLOD0 = options[@"onlyLOD0"];
            if (onlyLOD0) {
                importOptions.onlyLOD0 = onlyLOD0.boolValue;
            }
            NSNumber* mergeStatic = options[@"mergeStaticMeshes"];
            if (mergeStatic) {
                importOptions.mergeStaticMeshes = mergeStatic.boolValue;
            }
        }
        std::string modelPath = [path UTF8String];
        Crescent::Entity* entity = Crescent::SceneCommands::importModel(scene, modelPath, importOptions);
        return entity != nullptr;
    }];
}

- (void)deleteEntitiesByUUID:(NSArray<NSString *> *)uuids {
    [self performAsync:^{
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (!scene) return;

        LogSceneTextureDiagnostics(scene, "before_delete");
        std::vector<std::string> toDelete;
        toDelete.reserve(uuids.count);
        for (NSString* uuid in uuids) {
            toDelete.push_back([uuid UTF8String]);
        }
        Crescent::SceneCommands::destroyEntitiesByUUID(scene, toDelete);
        Crescent::SelectionSystem::clearSelection();
        if (_engine && _engine->getRenderer()) {
            _engine->getRenderer()->invalidateStaticLightingResources();
        }
        LogSceneTextureDiagnostics(scene, "after_delete");
    }];
}

- (NSArray<NSString *> *)duplicateEntitiesByUUID:(NSArray<NSString *> *)uuids {
    return (NSArray<NSString *> *)[self performSyncObject:^id{
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (!scene || !uuids) {
            return @[];
        }
        std::vector<Crescent::Entity*> toDuplicate;
        toDuplicate.reserve(uuids.count);
        for (NSString* uuid in uuids) {
            Crescent::Entity* entity = Crescent::SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
            if (entity) {
                toDuplicate.push_back(entity);
            }
        }
        if (toDuplicate.empty()) {
            return @[];
        }
        std::vector<Crescent::Entity*> duplicates = Crescent::SceneSerializer::DuplicateEntities(scene, toDuplicate);
        NSMutableArray<NSString *> *result = [NSMutableArray arrayWithCapacity:duplicates.size()];
        for (Crescent::Entity* entity : duplicates) {
            if (!entity) {
                continue;
            }
            [result addObject:[NSString stringWithUTF8String: entity->getUUID().toString().c_str()]];
        }
        return result;
    }];
}

// MARK: - Scene Query

- (NSArray<NSDictionary *> *)getAllEntityInfo {
    return (NSArray<NSDictionary *> *)[self performSyncObject:^id{
        NSMutableArray<NSDictionary *> *entityInfos = [NSMutableArray array];
        
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (scene) {
            auto entities = Crescent::SceneCommands::getAllEntities(scene);
            
            for (auto* entity : entities) {
                std::string entityName = entity->getName();
                
                // Skip editor-only helper
                if (entityName == "Editor Gizmo") {
                    continue;
                }
                if (entity->isEditorOnly()) {
                    continue;
                }
                
                std::string uuidStr = entity->getUUID().toString();
                bool hasSkinned = entity->getComponent<SkinnedMeshRenderer>() != nullptr;
                bool hasAnimator = entity->getComponent<Animator>() != nullptr;
                int clipCount = 0;
                if (hasSkinned) {
                    auto* skinned = entity->getComponent<SkinnedMeshRenderer>();
                    if (skinned) {
                        clipCount = static_cast<int>(skinned->getAnimationClips().size());
                    }
                }

                std::string parentUuidStr;
                if (auto* transform = entity->getTransform()) {
                    if (auto* parent = transform->getParent()) {
                        if (auto* parentEntity = parent->getEntity()) {
                            parentUuidStr = parentEntity->getUUID().toString();
                        }
                    }
                }
                
                NSDictionary *info = @{
                    @"uuid": [NSString stringWithUTF8String:uuidStr.c_str()],
                    @"name": [NSString stringWithUTF8String:entityName.c_str()],
                    @"skinned": @(hasSkinned),
                    @"animator": @(hasAnimator),
                    @"clipCount": @(clipCount),
                    @"parent": [NSString stringWithUTF8String:parentUuidStr.c_str()]
                };
                [entityInfos addObject:info];
            }
        }
        
        return entityInfos;
    }];
}

- (NSString *)getSelectedEntityUUID {
    return (NSString *)[self performSyncObject:^id{
        const auto& selection = Crescent::SelectionSystem::getSelection();
        if (!selection.empty()) {
            return [NSString stringWithUTF8String:selection.front()->getUUID().toString().c_str()];
        }
        return @"";
    }];
}

- (NSArray<NSString *> *)getAllSelectedEntityUUIDs {
    return (NSArray<NSString *> *)[self performSyncObject:^id{
        NSMutableArray<NSString *> *uuids = [NSMutableArray array];
        const auto& selection = Crescent::SelectionSystem::getSelection();
        
        for (auto* entity : selection) {
            std::string uuidStr = entity->getUUID().toString();
            [uuids addObject:[NSString stringWithUTF8String:uuidStr.c_str()]];
        }
        
        return uuids;
    }];
}

- (void)setSelectionByUUID:(NSArray<NSString *> *)uuids {
    [self performAsync:^{
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (!scene) {
            Crescent::SelectionSystem::clearSelection();
            return;
        }
        
        std::vector<Crescent::Entity*> entities;
        entities.reserve(uuids.count);
        for (NSString* uuid in uuids) {
            Crescent::Entity* e = Crescent::SceneCommands::getEntityByUUID(scene, [uuid UTF8String]);
            if (e) entities.push_back(e);
        }
        Crescent::SelectionSystem::setSelection(entities);
    }];
}

- (void)selectEntityByUUID:(NSString *)uuid {
    [self performAsync:^{
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (!scene) return;
        
        Crescent::Entity* entity = Crescent::SceneCommands::getEntityByUUID(scene, [uuid UTF8String]);
        if (entity) {
            Crescent::SelectionSystem::setSelectedEntity(entity);
        }
    }];
}

- (void)clearSelection {
    [self performAsync:^{
        Crescent::SelectionSystem::clearSelection();
    }];
}

- (BOOL)setEntityParent:(NSString *)childUUID parent:(NSString *)parentUUID {
    return [self performSyncBool:^BOOL {
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (!scene || !childUUID) {
            return NO;
        }
        std::string child = [childUUID UTF8String];
        std::string parent = parentUUID ? [parentUUID UTF8String] : "";
        return Crescent::SceneCommands::setParent(scene, child, parent);
    }];
}

- (BOOL)setEntityName:(NSString *)uuid name:(NSString *)name {
    return [self performSyncBool:^BOOL {
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (!scene || !uuid || !name) {
            return NO;
        }
        Crescent::Entity* entity = Crescent::SceneCommands::getEntityByUUID(scene, [uuid UTF8String]);
        if (!entity) {
            return NO;
        }
        entity->setName([name UTF8String]);
        return YES;
    }];
}

- (NSString *)buildHLODFromSelection:(NSArray<NSString *> *)uuids {
    return (NSString *)[self performSyncObject:^id{
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (!scene || !uuids || uuids.count == 0) {
            return @"";
        }
        std::vector<std::string> ids;
        ids.reserve(uuids.count);
        for (NSString* uuid in uuids) {
            if (!uuid) continue;
            ids.push_back([uuid UTF8String]);
        }
        Crescent::Entity* proxy = Crescent::SceneCommands::buildHLOD(scene, ids);
        if (!proxy) {
            return @"";
        }
        std::string proxyUuid = proxy->getUUID().toString();
        return [NSString stringWithUTF8String:proxyUuid.c_str()];
    }];
}

// MARK: - Entity Transform Query (by UUID)

- (NSArray<NSNumber *> *)getEntityPositionByUUID:(NSString *)uuid {
    return (NSArray<NSNumber *> *)[self performSyncObject:^id{
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (!scene) return @[@0, @0, @0];
        
        Crescent::Entity* entity = Crescent::SceneCommands::getEntityByUUID(scene, [uuid UTF8String]);
        if (entity) {
            auto pos = entity->getTransform()->getPosition();
            return @[@(pos.x), @(pos.y), @(pos.z)];
        }
        return @[@0, @0, @0];
    }];
}

- (NSArray<NSNumber *> *)getEntityRotationByUUID:(NSString *)uuid {
    return (NSArray<NSNumber *> *)[self performSyncObject:^id{
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (!scene) return @[@0, @0, @0];
        
        Crescent::Entity* entity = Crescent::SceneCommands::getEntityByUUID(scene, [uuid UTF8String]);
        if (entity) {
            auto euler = entity->getTransform()->getLocalEulerAngles();
            return @[@(euler.x * Crescent::Math::RAD_TO_DEG), 
                    @(euler.y * Crescent::Math::RAD_TO_DEG), 
                    @(euler.z * Crescent::Math::RAD_TO_DEG)];
        }
        return @[@0, @0, @0];
    }];
}

- (NSArray<NSNumber *> *)getEntityScaleByUUID:(NSString *)uuid {
    return (NSArray<NSNumber *> *)[self performSyncObject:^id{
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (!scene) return @[@1, @1, @1];
        
        Crescent::Entity* entity = Crescent::SceneCommands::getEntityByUUID(scene, [uuid UTF8String]);
        if (entity) {
            auto scale = entity->getTransform()->getLocalScale();
            return @[@(scale.x), @(scale.y), @(scale.z)];
        }
        return @[@1, @1, @1];
    }];
}

- (void)setEntityPositionByUUID:(NSString *)uuid x:(float)x y:(float)y z:(float)z {
    [self performAsync:^{
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (!scene) return;
        
        Crescent::Entity* entity = Crescent::SceneCommands::getEntityByUUID(scene, [uuid UTF8String]);
        if (entity) {
            entity->getTransform()->setPosition(Crescent::Math::Vector3(x, y, z));
        }
    }];
}

- (void)setEntityRotationByUUID:(NSString *)uuid x:(float)x y:(float)y z:(float)z {
    [self performAsync:^{
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (!scene) return;
        
        Crescent::Entity* entity = Crescent::SceneCommands::getEntityByUUID(scene, [uuid UTF8String]);
        if (entity) {
            Crescent::Math::Vector3 eulerRad(
                x * Crescent::Math::DEG_TO_RAD,
                y * Crescent::Math::DEG_TO_RAD,
                z * Crescent::Math::DEG_TO_RAD
            );
            entity->getTransform()->setLocalEulerAngles(eulerRad);
        }
    }];
}

- (void)setEntityScaleByUUID:(NSString *)uuid x:(float)x y:(float)y z:(float)z {
    [self performAsync:^{
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (!scene) return;
        
        Crescent::Entity* entity = Crescent::SceneCommands::getEntityByUUID(scene, [uuid UTF8String]);
        if (entity) {
            entity->getTransform()->setLocalScale(Crescent::Math::Vector3(x, y, z));
        }
    }];
}

// MARK: - Mouse Picking & Gizmo Interaction

- (void)handleMouseClickAtX:(float)x y:(float)y screenWidth:(float)width screenHeight:(float)height additive:(BOOL)additive {
    [self performAsync:^{
        if (_engine) {
            _engine->handleMouseClick(x, y, width, height, additive);
        }
    }];
}

- (void)handleMouseDragAtX:(float)x y:(float)y screenWidth:(float)width screenHeight:(float)height {
    std::lock_guard<std::mutex> lock(_inputMutex);
    _pendingMouseDragX = x;
    _pendingMouseDragY = y;
    _pendingMouseDragW = width;
    _pendingMouseDragH = height;
    _hasPendingMouseDrag = true;
}

- (void)handleMouseUpEvent {
    [self performAsync:^{
        if (_engine) {
            _engine->handleMouseUp();
        }
    }];
}

- (void)clearTerrainBrushPreviewInternal {
    Scene* scene = SceneManager::getInstance().getActiveScene();
    if (!_terrainBrushPreview.entityUUID.empty() && scene) {
        if (Entity* entity = SceneCommands::getEntityByUUID(scene, _terrainBrushPreview.entityUUID)) {
            scene->destroyEntity(entity);
        }
    }
    _terrainBrushPreview = TerrainBrushPreviewState{};
}

- (void)finishActiveTerrainStrokeInternal {
    if (_terrainPaintState.active) {
        CommitTerrainPaintState(_terrainPaintState);
        _terrainPaintState.active = false;
        _terrainPaintState.hasLastUV = false;
    }
    if (_terrainSculptState.active) {
        _terrainSculptState.active = false;
        _terrainSculptState.hasLastUV = false;
        _terrainSculptState.hasFlattenHeight = false;
    }
}

- (void)beginTerrainPaintForEntity:(NSString *)uuid
                                  x:(float)x
                                  y:(float)y
                        screenWidth:(float)width
                       screenHeight:(float)height
                              mode:(NSInteger)mode
                              layer:(NSInteger)layer
                         sculptTool:(NSInteger)sculptTool
                    sculptResolution:(NSInteger)sculptResolution
                             radius:(float)radius
                           hardness:(float)hardness
                           strength:(float)strength
                            spacing:(float)spacing
                         maskPreset:(NSInteger)maskPreset
                           maskPath:(NSString * _Nullable)maskPath
                      autoNormalize:(BOOL)autoNormalize
                             invert:(BOOL)invert {
    [self performAsync:^{
        if (!_engine || !uuid) {
            return;
        }
        if (!SceneManager::getInstance().isSceneView() || SceneManager::getInstance().isPlaying()) {
            return;
        }
        TerrainBrushModeCpp brushMode = TerrainBrushModeFromInt(static_cast<int>(mode));
        TerrainSculptToolCpp sculptToolMode = TerrainSculptToolFromInt(static_cast<int>(sculptTool));
        std::string requestedEntityUUID = [uuid UTF8String];
        TerrainPaintTarget target;
        std::string entityUUID;
        float u = 0.0f;
        float v = 0.0f;
        if (!ResolveTerrainPaintTargetAtCursor(requestedEntityUUID, x, y, width, height, target, entityUUID, u, v)) {
            return;
        }

        TerrainBrushParams brush;
        brush.layer = static_cast<int>(layer);
        brush.radius = std::max(0.01f, radius);
        brush.hardness = std::clamp(hardness, 0.0f, 1.0f);
        brush.strength = std::clamp(strength, 0.0f, 1.0f);
        brush.spacing = std::clamp(spacing, 0.05f, 1.0f);
        brush.maskPreset = static_cast<int>(maskPreset);
        if (maskPath) {
            brush.maskPath = [maskPath UTF8String];
        }
        brush.autoNormalize = autoNormalize;
        brush.invert = invert;

        TerrainBrushMaskSampler maskSampler;
        ConfigureTerrainBrushMaskSampler(maskSampler, brush, _terrainMaskCache);

        if (brushMode == TerrainBrushModeCpp::Paint) {
            if (_terrainSculptState.active ||
                (_terrainPaintState.active && _terrainPaintState.entityUUID != entityUUID)) {
                [self finishActiveTerrainStrokeInternal];
            }

            Renderer* renderer = _engine->getRenderer();
            if (!EnsureTerrainControlPaintData(_terrainPaintState, target, renderer)) {
                return;
            }

            auto& history = _terrainPaintHistory[entityUUID];
            EnsureTerrainHistoryCompatible(history, _terrainPaintState);
            PushTerrainUndoSnapshot(history, _terrainPaintState.pixels);

            _terrainPaintState.active = true;
            _terrainPaintState.entityUUID = entityUUID;
            _terrainPaintState.hasLastUV = false;
            _terrainSculptState.active = false;

            ApplyTerrainBrushStroke(_terrainPaintState, target, u, v, brush, maskSampler, true);
            UploadTerrainPaintTextureIfNeeded(_terrainPaintState, target, renderer, true);
            return;
        }

        if (_terrainPaintState.active ||
            (_terrainSculptState.active && _terrainSculptState.entityUUID != entityUUID)) {
            [self finishActiveTerrainStrokeInternal];
        }

        if (!EnsureTerrainSculptGrid(target, static_cast<int>(sculptResolution))) {
            return;
        }

        auto& history = _terrainSculptHistory[entityUUID];
        EnsureTerrainSculptHistoryCompatible(history, target);
        PushTerrainSculptUndoSnapshot(history, CaptureTerrainVertexHeights(target.mesh));

        _terrainSculptState.active = true;
        _terrainSculptState.entityUUID = entityUUID;
        _terrainSculptState.hasLastUV = false;
        _terrainSculptState.hasFlattenHeight = false;
        _terrainPaintState.active = false;

        ApplyTerrainSculptStroke(_terrainSculptState, target, u, v, brush, maskSampler, sculptToolMode, true);
    }];
}

- (void)updateTerrainPaintForEntity:(NSString *)uuid
                                   x:(float)x
                                   y:(float)y
                         screenWidth:(float)width
                        screenHeight:(float)height
                               mode:(NSInteger)mode
                               layer:(NSInteger)layer
                          sculptTool:(NSInteger)sculptTool
                     sculptResolution:(NSInteger)sculptResolution
                              radius:(float)radius
                            hardness:(float)hardness
                            strength:(float)strength
                             spacing:(float)spacing
                          maskPreset:(NSInteger)maskPreset
                            maskPath:(NSString * _Nullable)maskPath
                       autoNormalize:(BOOL)autoNormalize
                              invert:(BOOL)invert {
    [self performAsync:^{
        if (!_engine || !uuid) {
            return;
        }
        if (!SceneManager::getInstance().isSceneView() || SceneManager::getInstance().isPlaying()) {
            return;
        }
        TerrainBrushModeCpp brushMode = TerrainBrushModeFromInt(static_cast<int>(mode));
        TerrainSculptToolCpp sculptToolMode = TerrainSculptToolFromInt(static_cast<int>(sculptTool));
        std::string requestedEntityUUID = [uuid UTF8String];
        TerrainPaintTarget target;
        std::string entityUUID;
        float u = 0.0f;
        float v = 0.0f;
        if (!ResolveTerrainPaintTargetAtCursor(requestedEntityUUID, x, y, width, height, target, entityUUID, u, v)) {
            return;
        }

        TerrainBrushParams brush;
        brush.layer = static_cast<int>(layer);
        brush.radius = std::max(0.01f, radius);
        brush.hardness = std::clamp(hardness, 0.0f, 1.0f);
        brush.strength = std::clamp(strength, 0.0f, 1.0f);
        brush.spacing = std::clamp(spacing, 0.05f, 1.0f);
        brush.maskPreset = static_cast<int>(maskPreset);
        if (maskPath) {
            brush.maskPath = [maskPath UTF8String];
        }
        brush.autoNormalize = autoNormalize;
        brush.invert = invert;

        TerrainBrushMaskSampler maskSampler;
        ConfigureTerrainBrushMaskSampler(maskSampler, brush, _terrainMaskCache);

        if (brushMode == TerrainBrushModeCpp::Paint) {
            bool beginStroke = false;
            if (_terrainSculptState.active) {
                [self finishActiveTerrainStrokeInternal];
                beginStroke = true;
            }
            if (!_terrainPaintState.active || _terrainPaintState.entityUUID != entityUUID) {
                if (_terrainPaintState.active && _terrainPaintState.entityUUID != entityUUID) {
                    Renderer* previousRenderer = _engine->getRenderer();
                    TerrainPaintTarget previousTarget;
                    if (previousRenderer && ResolveTerrainPaintTarget(_terrainPaintState.entityUUID, previousTarget)) {
                        UploadTerrainPaintTextureIfNeeded(_terrainPaintState, previousTarget, previousRenderer, true);
                    }
                    CommitTerrainPaintState(_terrainPaintState);
                    _terrainPaintState.active = false;
                }
                beginStroke = true;
            }

            Renderer* renderer = _engine->getRenderer();
            if (!EnsureTerrainControlPaintData(_terrainPaintState, target, renderer)) {
                return;
            }

            if (beginStroke) {
                auto& history = _terrainPaintHistory[entityUUID];
                EnsureTerrainHistoryCompatible(history, _terrainPaintState);
                PushTerrainUndoSnapshot(history, _terrainPaintState.pixels);
            }

            _terrainPaintState.active = true;
            _terrainPaintState.entityUUID = entityUUID;
            if (beginStroke) {
                _terrainPaintState.hasLastUV = false;
            }

            ApplyTerrainBrushStroke(_terrainPaintState, target, u, v, brush, maskSampler, beginStroke);
            UploadTerrainPaintTextureIfNeeded(_terrainPaintState, target, renderer, beginStroke);
            return;
        }

        bool beginStroke = false;
        if (_terrainPaintState.active) {
            [self finishActiveTerrainStrokeInternal];
            beginStroke = true;
        }
        if (!_terrainSculptState.active || _terrainSculptState.entityUUID != entityUUID) {
            if (_terrainSculptState.active && _terrainSculptState.entityUUID != entityUUID) {
                [self finishActiveTerrainStrokeInternal];
            }
            beginStroke = true;
        }

        if (!EnsureTerrainSculptGrid(target, static_cast<int>(sculptResolution))) {
            return;
        }

        if (beginStroke) {
            auto& history = _terrainSculptHistory[entityUUID];
            EnsureTerrainSculptHistoryCompatible(history, target);
            PushTerrainSculptUndoSnapshot(history, CaptureTerrainVertexHeights(target.mesh));
            _terrainSculptState.hasLastUV = false;
            _terrainSculptState.hasFlattenHeight = false;
        }

        _terrainSculptState.active = true;
        _terrainSculptState.entityUUID = entityUUID;
        ApplyTerrainSculptStroke(_terrainSculptState, target, u, v, brush, maskSampler, sculptToolMode, beginStroke);
    }];
}

- (void)endTerrainPaint {
    [self performAsync:^{
        if (_engine && _terrainPaintState.active && !_terrainPaintState.entityUUID.empty()) {
            TerrainPaintTarget target;
            if (ResolveTerrainPaintTarget(_terrainPaintState.entityUUID, target)) {
                UploadTerrainPaintTextureIfNeeded(_terrainPaintState, target, _engine->getRenderer(), true);
            }
        }
        [self finishActiveTerrainStrokeInternal];
    }];
}

- (void)updateTerrainBrushPreviewForEntity:(NSString *)uuid
                                         x:(float)x
                                         y:(float)y
                               screenWidth:(float)width
                              screenHeight:(float)height
                                      mode:(NSInteger)mode
                                     layer:(NSInteger)layer
                               sculptTool:(NSInteger)sculptTool
                                    radius:(float)radius
                                  hardness:(float)hardness
                                 maskPreset:(NSInteger)maskPreset
                                   maskPath:(NSString * _Nullable)maskPath {
    [self performAsync:^{
        if (!_engine || !uuid) {
            [self clearTerrainBrushPreviewInternal];
            return;
        }
        if (!SceneManager::getInstance().isSceneView() || SceneManager::getInstance().isPlaying()) {
            [self clearTerrainBrushPreviewInternal];
            return;
        }

        TerrainBrushModeCpp brushMode = TerrainBrushModeFromInt(static_cast<int>(mode));
        TerrainSculptToolCpp sculptToolMode = TerrainSculptToolFromInt(static_cast<int>(sculptTool));
        std::string requestedEntityUUID = [uuid UTF8String];
        TerrainPaintTarget target;
        float u = 0.0f;
        float v = 0.0f;
        Math::Vector3 worldHit = Math::Vector3::Zero;
        std::string entityUUID;
        if (!ResolveTerrainPaintTargetAtCursor(requestedEntityUUID, x, y, width, height, target, entityUUID, u, v, &worldHit)) {
            [self clearTerrainBrushPreviewInternal];
            return;
        }

        Renderer* renderer = _engine->getRenderer();
        if (!renderer || !renderer->getTextureLoader()) {
            [self clearTerrainBrushPreviewInternal];
            return;
        }

        TerrainBrushParams brush;
        brush.layer = brushMode == TerrainBrushModeCpp::Paint ? static_cast<int>(layer) : 0;
        brush.radius = std::max(0.05f, radius);
        brush.hardness = std::clamp(hardness, 0.0f, 1.0f);
        brush.maskPreset = static_cast<int>(maskPreset);
        if (maskPath) {
            brush.maskPath = [maskPath UTF8String];
        }

        TerrainBrushMaskSampler sampler;
        ConfigureTerrainBrushMaskSampler(sampler, brush, _terrainMaskCache);

        if (!_terrainBrushPreview.texture ||
            _terrainBrushPreview.mode != static_cast<int>(brushMode) ||
            _terrainBrushPreview.layer != brush.layer ||
            _terrainBrushPreview.sculptTool != static_cast<int>(sculptToolMode) ||
            std::abs(_terrainBrushPreview.hardness - brush.hardness) > 0.0001f ||
            _terrainBrushPreview.maskPreset != brush.maskPreset ||
            _terrainBrushPreview.maskPath != brush.maskPath) {
            _terrainBrushPreview.texture = BuildTerrainBrushPreviewTexture(renderer, brush, sampler);
            _terrainBrushPreview.mode = static_cast<int>(brushMode);
            _terrainBrushPreview.layer = brush.layer;
            _terrainBrushPreview.sculptTool = static_cast<int>(sculptToolMode);
            _terrainBrushPreview.hardness = brush.hardness;
            _terrainBrushPreview.maskPreset = brush.maskPreset;
            _terrainBrushPreview.maskPath = brush.maskPath;
        }

        Scene* scene = SceneManager::getInstance().getActiveScene();
        Entity* previewEntity = EnsureTerrainBrushPreviewEntity(scene, _terrainBrushPreview);
        if (!previewEntity) {
            return;
        }
        Decal* decal = previewEntity->getComponent<Decal>();
        if (!decal) {
            decal = previewEntity->addComponent<Decal>();
        }
        if (!decal) {
            return;
        }

        decal->setAlbedoTexture(_terrainBrushPreview.texture);
        decal->setNormalTexture(nullptr);
        decal->setORMTexture(nullptr);
        decal->setMaskTexture(nullptr);
        Math::Vector3 previewColor = TerrainBrushPreviewColor(brushMode, brush.layer, sculptToolMode);
        decal->setTint(Math::Vector4(previewColor.x, previewColor.y, previewColor.z, 1.0f));
        decal->setOpacity(0.95f);
        decal->setEdgeSoftness(0.02f);
        decal->setTiling(Math::Vector2(1.0f, 1.0f));
        decal->setOffset(Math::Vector2(0.0f, 0.0f));

        Transform* targetTransform = target.binding.entity ? target.binding.entity->getTransform() : nullptr;
        Transform* previewTransform = previewEntity->getTransform();
        if (!targetTransform || !previewTransform) {
            return;
        }

        Math::Vector3 normal = targetTransform->up();
        if (normal.lengthSquared() < 1e-6f) {
            normal = Math::Vector3::Up;
        } else {
            normal = normal.normalized();
        }

        float decalThickness = 0.12f;
        float diameter = std::max(brush.radius * 2.0f, 0.05f);
        previewTransform->setPosition(worldHit + normal * 0.03f);
        Math::Quaternion align = targetTransform->getRotation()
            * Math::Quaternion::FromEulerAngles(Math::Vector3(-90.0f * Math::DEG_TO_RAD, 0.0f, 0.0f));
        previewTransform->setRotation(align);
        previewTransform->setLocalScale(Math::Vector3(diameter, diameter, decalThickness));
    }];
}

- (void)clearTerrainBrushPreview {
    [self performAsync:^{
        [self clearTerrainBrushPreviewInternal];
    }];
}

- (BOOL)undoTerrainStrokeForEntity:(NSString *)uuid mode:(NSInteger)mode {
    return [self performSyncBool:^BOOL {
        if (!_engine || !uuid || uuid.length == 0) {
            return NO;
        }
        if (SceneManager::getInstance().isPlaying()) {
            return NO;
        }
        TerrainBrushModeCpp brushMode = TerrainBrushModeFromInt(static_cast<int>(mode));
        std::string entityUUID = [uuid UTF8String];

        TerrainPaintTarget target;
        if (!ResolveTerrainPaintTarget(entityUUID, target)) {
            return NO;
        }
        if (brushMode == TerrainBrushModeCpp::Paint) {
            if (_terrainPaintState.active && _terrainPaintState.entityUUID == entityUUID) {
                CommitTerrainPaintState(_terrainPaintState);
                _terrainPaintState.active = false;
                _terrainPaintState.hasLastUV = false;
            }

            Renderer* renderer = _engine->getRenderer();
            if (!EnsureTerrainControlPaintData(_terrainPaintState, target, renderer)) {
                return NO;
            }

            auto it = _terrainPaintHistory.find(entityUUID);
            if (it == _terrainPaintHistory.end()) {
                return NO;
            }
            TerrainPaintHistoryState& history = it->second;
            EnsureTerrainHistoryCompatible(history, _terrainPaintState);
            if (history.undoStack.empty()) {
                return NO;
            }

            PushTerrainRedoSnapshot(history, _terrainPaintState.pixels);
            std::vector<unsigned char> snapshot = std::move(history.undoStack.back());
            history.undoStack.pop_back();
            if (snapshot.size() != _terrainPaintState.pixels.size()) {
                return NO;
            }

            _terrainPaintState.pixels = std::move(snapshot);
            _terrainPaintState.dirty = true;

            if (renderer && renderer->getTextureLoader()) {
                renderer->getTextureLoader()->updateTextureFromRGBA8(
                    target.material->getTerrainControlTexture(),
                    _terrainPaintState.pixels.data(),
                    _terrainPaintState.width,
                    _terrainPaintState.height,
                    true
                );
            }
            CommitTerrainPaintState(_terrainPaintState);
            return YES;
        }

        if (_terrainSculptState.active && _terrainSculptState.entityUUID == entityUUID) {
            _terrainSculptState.active = false;
            _terrainSculptState.hasLastUV = false;
            _terrainSculptState.hasFlattenHeight = false;
        }

        auto it = _terrainSculptHistory.find(entityUUID);
        if (it == _terrainSculptHistory.end()) {
            return NO;
        }
        TerrainSculptHistoryState& history = it->second;
        EnsureTerrainSculptHistoryCompatible(history, target);
        if (history.undoStack.empty()) {
            return NO;
        }

        std::vector<float> currentHeights = CaptureTerrainVertexHeights(target.mesh);
        PushTerrainSculptRedoSnapshot(history, currentHeights);
        std::vector<float> snapshot = std::move(history.undoStack.back());
        history.undoStack.pop_back();
        return ApplyTerrainVertexHeights(target, snapshot) ? YES : NO;
    }];
}

- (BOOL)redoTerrainStrokeForEntity:(NSString *)uuid mode:(NSInteger)mode {
    return [self performSyncBool:^BOOL {
        if (!_engine || !uuid || uuid.length == 0) {
            return NO;
        }
        if (SceneManager::getInstance().isPlaying()) {
            return NO;
        }
        TerrainBrushModeCpp brushMode = TerrainBrushModeFromInt(static_cast<int>(mode));
        std::string entityUUID = [uuid UTF8String];

        TerrainPaintTarget target;
        if (!ResolveTerrainPaintTarget(entityUUID, target)) {
            return NO;
        }

        if (brushMode == TerrainBrushModeCpp::Paint) {
            if (_terrainPaintState.active && _terrainPaintState.entityUUID == entityUUID) {
                CommitTerrainPaintState(_terrainPaintState);
                _terrainPaintState.active = false;
                _terrainPaintState.hasLastUV = false;
            }

            Renderer* renderer = _engine->getRenderer();
            if (!EnsureTerrainControlPaintData(_terrainPaintState, target, renderer)) {
                return NO;
            }

            auto it = _terrainPaintHistory.find(entityUUID);
            if (it == _terrainPaintHistory.end()) {
                return NO;
            }
            TerrainPaintHistoryState& history = it->second;
            EnsureTerrainHistoryCompatible(history, _terrainPaintState);
            if (history.redoStack.empty()) {
                return NO;
            }

            if (!history.undoStack.empty() && SnapshotEquals(history.undoStack.back(), _terrainPaintState.pixels)) {
                // keep current top
            } else {
                history.undoStack.push_back(_terrainPaintState.pixels);
                if (history.undoStack.size() > kTerrainPaintUndoLimit) {
                    history.undoStack.erase(history.undoStack.begin());
                }
            }

            std::vector<unsigned char> snapshot = std::move(history.redoStack.back());
            history.redoStack.pop_back();
            if (snapshot.size() != _terrainPaintState.pixels.size()) {
                return NO;
            }

            _terrainPaintState.pixels = std::move(snapshot);
            _terrainPaintState.dirty = true;

            if (renderer && renderer->getTextureLoader()) {
                renderer->getTextureLoader()->updateTextureFromRGBA8(
                    target.material->getTerrainControlTexture(),
                    _terrainPaintState.pixels.data(),
                    _terrainPaintState.width,
                    _terrainPaintState.height,
                    true
                );
            }
            CommitTerrainPaintState(_terrainPaintState);
            return YES;
        }

        if (_terrainSculptState.active && _terrainSculptState.entityUUID == entityUUID) {
            _terrainSculptState.active = false;
            _terrainSculptState.hasLastUV = false;
            _terrainSculptState.hasFlattenHeight = false;
        }

        auto it = _terrainSculptHistory.find(entityUUID);
        if (it == _terrainSculptHistory.end()) {
            return NO;
        }
        TerrainSculptHistoryState& history = it->second;
        EnsureTerrainSculptHistoryCompatible(history, target);
        if (history.redoStack.empty()) {
            return NO;
        }

        std::vector<float> currentHeights = CaptureTerrainVertexHeights(target.mesh);
        if (!history.undoStack.empty() && FloatSnapshotEquals(history.undoStack.back(), currentHeights)) {
            // keep current top
        } else {
            history.undoStack.push_back(currentHeights);
            if (history.undoStack.size() > kTerrainPaintUndoLimit) {
                history.undoStack.erase(history.undoStack.begin());
            }
        }

        std::vector<float> snapshot = std::move(history.redoStack.back());
        history.redoStack.pop_back();
        return ApplyTerrainVertexHeights(target, snapshot) ? YES : NO;
    }];
}

// MARK: - Gizmo Controls

- (void)setGizmoMode:(int)mode {
    [self performAsync:^{
        if (_engine) {
            Crescent::GizmoMode gizmoMode;
            switch (mode) {
                case 0: gizmoMode = Crescent::GizmoMode::Translate; break;
                case 1: gizmoMode = Crescent::GizmoMode::Rotate; break;
                case 2: gizmoMode = Crescent::GizmoMode::Scale; break;
                default: gizmoMode = Crescent::GizmoMode::Translate; break;
            }
            _engine->setGizmoMode(gizmoMode);
        }
    }];
}

- (void)toggleGizmoMode {
    [self performAsync:^{
        if (_engine) {
            _engine->toggleGizmoMode();
        }
    }];
}

- (void)toggleGizmoSpace {
    [self performAsync:^{
        if (_engine) {
            _engine->toggleGizmoSpace();
        }
    }];
}

// MARK: - Camera Controls

- (float)getCameraMoveSpeed {
    return [self performSyncFloat:^float {
        Crescent::Camera* camera = Crescent::SceneManager::getInstance().getSceneCamera();
        if (!camera) return 5.0f;

        Crescent::Entity* cameraEntity = camera->getEntity();
        if (!cameraEntity) return 5.0f;

        Crescent::CameraController* controller = cameraEntity->getComponent<Crescent::CameraController>();
        if (!controller) return 5.0f;

        return controller->getMoveSpeed();
    }];
}

- (void)setCameraMoveSpeed:(float)speed {
    [self performAsync:^{
        Crescent::Camera* camera = Crescent::SceneManager::getInstance().getSceneCamera();
        if (!camera) return;

        Crescent::Entity* cameraEntity = camera->getEntity();
        if (!cameraEntity) return;

        Crescent::CameraController* controller = cameraEntity->getComponent<Crescent::CameraController>();
        if (!controller) return;

        controller->setMoveSpeed(speed);
    }];
}

- (NSDictionary *)getSceneCameraBasis {
    return (NSDictionary *)[self performSyncObject:^id{
        Crescent::Scene* scene = Crescent::SceneManager::getInstance().getActiveScene();
        if (!scene) return @{};

        Crescent::Camera* camera = Crescent::SceneManager::getInstance().getSceneCamera();
        if (!camera) return @{};

        Crescent::Entity* entity = camera->getEntity();
        if (!entity) return @{};

        Crescent::Transform* transform = entity->getTransform();
        if (!transform) return @{};

        Math::Vector3 right = transform->right();
        Math::Vector3 up = transform->up();
        Math::Vector3 forward = transform->forward();

        return @{
            @"right": @[@(right.x), @(right.y), @(right.z)],
            @"up": @[@(up.x), @(up.y), @(up.z)],
            @"forward": @[@(forward.x), @(forward.y), @(forward.z)]
        };
    }];
}

// MARK: - Material Editing

- (NSDictionary *)getMaterialInfoForEntity:(NSString *)uuid {
    return (NSDictionary *)[self performSyncObject:^id{
        Scene* scene = SceneManager::getInstance().getActiveScene();
        Entity* entity = scene ? SceneCommands::getEntityByUUID(scene, [uuid UTF8String]) : nullptr;
        auto material = GetPrimaryMaterialForEntityUUID([uuid UTF8String]);
        if (!material) return @{};
        
        Math::Vector4 albedo = material->getAlbedo();
        Math::Vector3 emission = material->getEmission();
        Math::Vector2 tiling = material->getUVTiling();
        Math::Vector2 offset = material->getUVOffset();
        Math::Vector2 terrainLayer0Tiling = material->getTerrainLayer0Tiling();
        Math::Vector2 terrainLayer1Tiling = material->getTerrainLayer1Tiling();
        Math::Vector2 terrainLayer2Tiling = material->getTerrainLayer2Tiling();
        Math::Vector3 windDir = material->getWindDirection();
        
        auto texturePath = [](const std::shared_ptr<Texture2D>& tex) -> NSString* {
            if (!tex) return @"";
            const std::string& path = tex->getPath();
            if (!path.empty() && path.rfind("builtin://", 0) != 0) {
                return [NSString stringWithUTF8String:path.c_str()];
            }
            return @"";
        };
        
        NSDictionary *textures = @{
            @"albedo": texturePath(material->getAlbedoTexture()),
            @"normal": texturePath(material->getNormalTexture()),
            @"metallic": texturePath(material->getMetallicTexture()),
            @"roughness": texturePath(material->getRoughnessTexture()),
            @"ao": texturePath(material->getAOTexture()),
            @"emission": texturePath(material->getEmissionTexture()),
            @"orm": texturePath(material->getORMTexture()),
            @"height": texturePath(material->getHeightTexture()),
            @"terrainControl": texturePath(material->getTerrainControlTexture()),
            @"terrainLayer0": texturePath(material->getTerrainLayer0Texture()),
            @"terrainLayer1": texturePath(material->getTerrainLayer1Texture()),
            @"terrainLayer2": texturePath(material->getTerrainLayer2Texture()),
            @"terrainLayer0Normal": texturePath(material->getTerrainLayer0NormalTexture()),
            @"terrainLayer1Normal": texturePath(material->getTerrainLayer1NormalTexture()),
            @"terrainLayer2Normal": texturePath(material->getTerrainLayer2NormalTexture()),
            @"terrainLayer0ORM": texturePath(material->getTerrainLayer0ORMTexture()),
            @"terrainLayer1ORM": texturePath(material->getTerrainLayer1ORMTexture()),
            @"terrainLayer2ORM": texturePath(material->getTerrainLayer2ORMTexture())
        };

        bool isEnginePlane = false;
        if (entity) {
            if (auto* primitive = entity->getComponent<PrimitiveMesh>()) {
                isEnginePlane = primitive->getType() == PrimitiveType::Plane;
            }
        }
        
        return @{
            @"name": [NSString stringWithUTF8String:material->getName().c_str()],
            @"albedo": @[@(albedo.x), @(albedo.y), @(albedo.z), @(albedo.w)],
            @"metallic": @(material->getMetallic()),
            @"roughness": @(material->getRoughness()),
            @"ao": @(material->getAO()),
            @"emission": @[@(emission.x), @(emission.y), @(emission.z)],
            @"emissionStrength": @(material->getEmissionStrength()),
            @"normalScale": @(material->getNormalScale()),
            @"heightScale": @(material->getHeightScale()),
            @"heightInvert": @(material->getHeightInvert() ? 1 : 0),
            @"renderMode": @(static_cast<int>(material->getRenderMode())),
            @"alphaCutoff": @(material->getAlphaCutoff()),
            @"twoSided": @(material->isTwoSided() ? 1 : 0),
            @"alphaToCoverage": @(material->getAlphaToCoverage() ? 1 : 0),
            @"windEnabled": @(material->getWindEnabled() ? 1 : 0),
            @"windStrength": @(material->getWindStrength()),
            @"windSpeed": @(material->getWindSpeed()),
            @"windScale": @(material->getWindScale()),
            @"windGust": @(material->getWindGust()),
            @"windDirection": @[@(windDir.x), @(windDir.y), @(windDir.z)],
            @"lodFadeEnabled": @(material->getLodFadeEnabled() ? 1 : 0),
            @"lodFadeStart": @(material->getLodFadeStart()),
            @"lodFadeEnd": @(material->getLodFadeEnd()),
            @"ditherEnabled": @(material->getDitherEnabled() ? 1 : 0),
            @"billboardEnabled": @(material->getBillboardEnabled() ? 1 : 0),
            @"billboardStart": @(material->getBillboardStart()),
            @"billboardEnd": @(material->getBillboardEnd()),
            @"impostorEnabled": @(material->getImpostorEnabled() ? 1 : 0),
            @"impostorRows": @(material->getImpostorRows()),
            @"impostorCols": @(material->getImpostorCols()),
            @"terrainEnabled": @(material->getTerrainEnabled() ? 1 : 0),
            @"terrainBlendSharpness": @(material->getTerrainBlendSharpness()),
            @"terrainHeightStart": @(material->getTerrainHeightStart()),
            @"terrainHeightEnd": @(material->getTerrainHeightEnd()),
            @"terrainSlopeStart": @(material->getTerrainSlopeStart()),
            @"terrainSlopeEnd": @(material->getTerrainSlopeEnd()),
            @"terrainLayer0Tiling": @[@(terrainLayer0Tiling.x), @(terrainLayer0Tiling.y)],
            @"terrainLayer1Tiling": @[@(terrainLayer1Tiling.x), @(terrainLayer1Tiling.y)],
            @"terrainLayer2Tiling": @[@(terrainLayer2Tiling.x), @(terrainLayer2Tiling.y)],
            @"isEnginePlane": @(isEnginePlane ? 1 : 0),
            @"tiling": @[@(tiling.x), @(tiling.y)],
            @"offset": @[@(offset.x), @(offset.y)],
            @"textures": textures
        };
    }];
}

- (NSDictionary *)getFoliageAutoLodForEntity:(NSString *)uuid {
    return (NSDictionary *)[self performSyncObject:^id{
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return @{};
        Entity* entity = SceneCommands::getEntityByUUID(scene, [uuid UTF8String]);
        if (!entity) return @{};

        std::shared_ptr<Mesh> mesh;
        if (auto* renderer = entity->getComponent<MeshRenderer>()) {
            mesh = renderer->getMesh();
        } else if (auto* skinned = entity->getComponent<SkinnedMeshRenderer>()) {
            mesh = skinned->getMesh();
        } else if (auto* instanced = entity->getComponent<InstancedMeshRenderer>()) {
            mesh = instanced->getMesh();
        }
        if (!mesh) return @{};

        Camera* camera = SceneManager::getInstance().getSceneCamera();
        if (!camera || !camera->getEntity()) return @{};

        Transform* transform = entity->getTransform();
        if (!transform) return @{};

        Math::Vector3 boundsCenter = mesh->getBoundsCenter();
        Math::Vector3 boundsSize = mesh->getBoundsSize();
        Math::Vector3 centerWorld = transform->getWorldMatrix().transformPoint(boundsCenter);
        Math::Vector3 cameraPos = camera->getEntity()->getTransform()->getPosition();
        float dist = (centerWorld - cameraPos).length();

        float size = std::max(0.1f, std::max(boundsSize.x, std::max(boundsSize.y, boundsSize.z)));
        float lodStart = std::max(size * 15.0f, dist * 0.6f);
        float lodEnd = std::max(lodStart + size * 10.0f, dist * 0.8f);
        float billboardStart = std::max(lodEnd + size * 10.0f, dist * 1.0f);
        float billboardEnd = std::max(billboardStart + size * 10.0f, dist * 1.2f);

        return @{
            @"lodFadeStart": @(lodStart),
            @"lodFadeEnd": @(lodEnd),
            @"billboardStart": @(billboardStart),
            @"billboardEnd": @(billboardEnd)
        };
    }];
}

- (void)setMaterialScalarForEntity:(NSString *)uuid property:(NSString *)property value:(float)value {
    [self performAsync:^{
        auto binding = GetMaterialBindingForEntityUUID([uuid UTF8String]);
        auto material = EnsureUniqueMaterialForEntity(binding);
        if (!material) return;
        
        std::string prop = [property UTF8String];
        ApplyMaterialScalar(material, prop, value);
    }];
}

- (void)setMaterialColorForEntity:(NSString *)uuid property:(NSString *)property r:(float)r g:(float)g b:(float)b a:(float)a {
    [self performAsync:^{
        auto binding = GetMaterialBindingForEntityUUID([uuid UTF8String]);
        auto material = EnsureUniqueMaterialForEntity(binding);
        if (!material) return;
        
        std::string prop = [property UTF8String];
        ApplyMaterialColor(material, prop, r, g, b, a);
    }];
}

- (BOOL)loadTextureForEntity:(NSString *)uuid slot:(NSString *)slot path:(NSString *)path {
    return [self performSyncBool:^BOOL {
        if (!_engine || !_engine->getRenderer() || !_engine->getRenderer()->getTextureLoader()) {
            return NO;
        }
        
        auto binding = GetMaterialBindingForEntityUUID([uuid UTF8String]);
        auto material = EnsureUniqueMaterialForEntity(binding);
        if (!material) return NO;
        
        std::string slotStr = [slot UTF8String];
        bool srgb = (slotStr == "albedo" || slotStr == "emission" || slotStr == "terrainLayer0" || slotStr == "terrainLayer1" || slotStr == "terrainLayer2");
        if (slotStr == "height") srgb = false;
        
        auto texture = _engine->getRenderer()->getTextureLoader()->loadTexture([path UTF8String], srgb, true);
        if (!texture) return NO;
        
        ApplyMaterialTexture(material, slotStr, texture);
        
        return YES;
    }];
}

- (void)clearTextureForEntity:(NSString *)uuid slot:(NSString *)slot {
    [self performAsync:^{
        auto binding = GetMaterialBindingForEntityUUID([uuid UTF8String]);
        auto material = EnsureUniqueMaterialForEntity(binding);
        if (!material) return;
        
        std::string slotStr = [slot UTF8String];
        ApplyMaterialTexture(material, slotStr, nullptr);
    }];
}

- (void)setMaterialScalarForEntityAllMaterials:(NSString *)uuid property:(NSString *)property value:(float)value {
    [self performAsync:^{
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return;
        Entity* entity = SceneCommands::getEntityByUUID(scene, [uuid UTF8String]);
        if (!entity) return;

        EnsureUniqueMaterialsForEntity(entity);
        std::string prop = [property UTF8String];

        if (auto* renderer = entity->getComponent<MeshRenderer>()) {
            const auto& materials = renderer->getMaterials();
            for (const auto& material : materials) {
                ApplyMaterialScalar(material, prop, value);
            }
        }
        if (auto* skinned = entity->getComponent<SkinnedMeshRenderer>()) {
            const auto& materials = skinned->getMaterials();
            for (const auto& material : materials) {
                ApplyMaterialScalar(material, prop, value);
            }
        }
        if (auto* instanced = entity->getComponent<InstancedMeshRenderer>()) {
            const auto& materials = instanced->getMaterials();
            for (const auto& material : materials) {
                ApplyMaterialScalar(material, prop, value);
            }
        }
    }];
}

- (void)setMaterialColorForEntityAllMaterials:(NSString *)uuid property:(NSString *)property r:(float)r g:(float)g b:(float)b a:(float)a {
    [self performAsync:^{
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return;
        Entity* entity = SceneCommands::getEntityByUUID(scene, [uuid UTF8String]);
        if (!entity) return;

        EnsureUniqueMaterialsForEntity(entity);
        std::string prop = [property UTF8String];

        if (auto* renderer = entity->getComponent<MeshRenderer>()) {
            const auto& materials = renderer->getMaterials();
            for (const auto& material : materials) {
                ApplyMaterialColor(material, prop, r, g, b, a);
            }
        }
        if (auto* skinned = entity->getComponent<SkinnedMeshRenderer>()) {
            const auto& materials = skinned->getMaterials();
            for (const auto& material : materials) {
                ApplyMaterialColor(material, prop, r, g, b, a);
            }
        }
        if (auto* instanced = entity->getComponent<InstancedMeshRenderer>()) {
            const auto& materials = instanced->getMaterials();
            for (const auto& material : materials) {
                ApplyMaterialColor(material, prop, r, g, b, a);
            }
        }
    }];
}

- (BOOL)loadTextureForEntityAllMaterials:(NSString *)uuid slot:(NSString *)slot path:(NSString *)path {
    return [self performSyncBool:^BOOL {
        if (!_engine || !_engine->getRenderer() || !_engine->getRenderer()->getTextureLoader()) {
            return NO;
        }
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return NO;
        Entity* entity = SceneCommands::getEntityByUUID(scene, [uuid UTF8String]);
        if (!entity) return NO;

        EnsureUniqueMaterialsForEntity(entity);

        std::string slotStr = [slot UTF8String];
        bool srgb = (slotStr == "albedo" || slotStr == "emission" || slotStr == "terrainLayer0" || slotStr == "terrainLayer1" || slotStr == "terrainLayer2");
        if (slotStr == "height") srgb = false;

        auto texture = _engine->getRenderer()->getTextureLoader()->loadTexture([path UTF8String], srgb, true);
        if (!texture) return NO;

        if (auto* renderer = entity->getComponent<MeshRenderer>()) {
            const auto& materials = renderer->getMaterials();
            for (const auto& material : materials) {
                ApplyMaterialTexture(material, slotStr, texture);
            }
        }
        if (auto* skinned = entity->getComponent<SkinnedMeshRenderer>()) {
            const auto& materials = skinned->getMaterials();
            for (const auto& material : materials) {
                ApplyMaterialTexture(material, slotStr, texture);
            }
        }
        if (auto* instanced = entity->getComponent<InstancedMeshRenderer>()) {
            const auto& materials = instanced->getMaterials();
            for (const auto& material : materials) {
                ApplyMaterialTexture(material, slotStr, texture);
            }
        }
        return YES;
    }];
}

- (void)clearTextureForEntityAllMaterials:(NSString *)uuid slot:(NSString *)slot {
    [self performAsync:^{
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return;
        Entity* entity = SceneCommands::getEntityByUUID(scene, [uuid UTF8String]);
        if (!entity) return;

        EnsureUniqueMaterialsForEntity(entity);

        std::string slotStr = [slot UTF8String];
        if (auto* renderer = entity->getComponent<MeshRenderer>()) {
            const auto& materials = renderer->getMaterials();
            for (const auto& material : materials) {
                ApplyMaterialTexture(material, slotStr, nullptr);
            }
        }
        if (auto* skinned = entity->getComponent<SkinnedMeshRenderer>()) {
            const auto& materials = skinned->getMaterials();
            for (const auto& material : materials) {
                ApplyMaterialTexture(material, slotStr, nullptr);
            }
        }
        if (auto* instanced = entity->getComponent<InstancedMeshRenderer>()) {
            const auto& materials = instanced->getMaterials();
            for (const auto& material : materials) {
                ApplyMaterialTexture(material, slotStr, nullptr);
            }
        }
    }];
}

- (BOOL)bakeImpostorAtlasForEntity:(NSString *)uuid rows:(NSInteger)rows cols:(NSInteger)cols tileSize:(NSInteger)tileSize {
    return [self performSyncBool:^BOOL {
        if (!_engine || !_engine->getRenderer()) {
            return NO;
        }
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) {
            return NO;
        }
        Entity* entity = SceneCommands::getEntityByUUID(scene, [uuid UTF8String]);
        if (!entity) {
            return NO;
        }
        MaterialBinding binding = GetMaterialBindingForEntityUUID([uuid UTF8String]);
        auto material = EnsureUniqueMaterialForEntity(binding);
        if (!material) {
            return NO;
        }

        std::shared_ptr<Mesh> mesh;
        if (binding.renderer) {
            mesh = binding.renderer->getMesh();
        } else if (auto* instanced = entity->getComponent<InstancedMeshRenderer>()) {
            mesh = instanced->getMesh();
        }
        if (!mesh) {
            return NO;
        }

        std::string outputPath;
        auto tex = _engine->getRenderer()->bakeImpostorAtlas(mesh.get(), material.get(),
                                                            static_cast<uint32_t>(std::max<NSInteger>(1, rows)),
                                                            static_cast<uint32_t>(std::max<NSInteger>(1, cols)),
                                                            static_cast<uint32_t>(std::max<NSInteger>(32, tileSize)),
                                                            &outputPath);
        if (!tex) {
            return NO;
        }

        material->setAlbedoTexture(tex);
        material->setImpostorEnabled(true);
        material->setImpostorRows(static_cast<int>(rows));
        material->setImpostorCols(static_cast<int>(cols));
        material->setBillboardEnabled(true);
        if (material->getRenderMode() != Material::RenderMode::Cutout) {
            material->setRenderMode(Material::RenderMode::Cutout);
            material->setAlphaCutoff(0.3f);
        }
        material->setAlpha(1.0f);

        return YES;
    }];
}

- (NSDictionary *)bakeSceneStaticLighting {
    return (NSDictionary *)[self performSyncObject:^id{
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) {
            return @{};
        }
        SceneCommands::StaticLightmapBakeStats stats = SceneCommands::bakeStaticLightmaps(scene);
        if (_engine && _engine->getRenderer()) {
            _engine->getRenderer()->invalidateStaticLightingResources();
        }
        return @{
            @"atlasCount": @(stats.atlasCount),
            @"bakedRendererCount": @(stats.bakedRendererCount),
            @"bakedTexelCount": @(stats.bakedTexelCount),
            @"bakedLightCount": @(stats.bakedLightCount),
            @"staticGeometryRendererCount": @(stats.staticGeometryRendererCount),
            @"layoutRendererCount": @(stats.layoutRendererCount),
            @"layoutSkippedRendererCount": @(stats.layoutSkippedRendererCount),
            @"generatedUVRendererCount": @(stats.generatedUVRendererCount),
            @"reusedUVRendererCount": @(stats.reusedUVRendererCount)
        };
    }];
}

- (NSDictionary *)buildSceneStaticLightingLayout {
    return (NSDictionary *)[self performSyncObject:^id{
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) {
            return @{};
        }
        SceneCommands::StaticLightingLayoutStats stats = SceneCommands::buildStaticLightingLayout(scene);
        return @{
            @"rendererCount": @(stats.rendererCount),
            @"atlasCount": @(stats.atlasCount),
            @"generatedUVRendererCount": @(stats.generatedUVRendererCount),
            @"reusedUVRendererCount": @(stats.reusedUVRendererCount),
            @"skippedRendererCount": @(stats.skippedRendererCount)
        };
    }];
}

- (NSDictionary *)bakeSceneStaticLightmaps {
    return (NSDictionary *)[self performSyncObject:^id{
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) {
            return @{};
        }
        SceneCommands::StaticLightmapBakeStats stats = SceneCommands::bakeStaticLightmaps(scene);
        if (_engine && _engine->getRenderer()) {
            _engine->getRenderer()->invalidateStaticLightingResources();
        }
        return @{
            @"atlasCount": @(stats.atlasCount),
            @"bakedRendererCount": @(stats.bakedRendererCount),
            @"bakedLightCount": @(stats.bakedLightCount),
            @"bakedTexelCount": @(stats.bakedTexelCount),
            @"staticGeometryRendererCount": @(stats.staticGeometryRendererCount),
            @"layoutRendererCount": @(stats.layoutRendererCount),
            @"layoutSkippedRendererCount": @(stats.layoutSkippedRendererCount),
            @"generatedUVRendererCount": @(stats.generatedUVRendererCount),
            @"reusedUVRendererCount": @(stats.reusedUVRendererCount)
        };
    }];
}

// MARK: - Material Assets

- (NSString *)createMaterialAssetFromEntity:(NSString *)uuid name:(NSString *)name {
    return (NSString *)[self performSyncObject:^id{
        if (!_engine || !_engine->getRenderer()) {
            return @"";
        }
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) {
            return @"";
        }
        Entity* entity = SceneCommands::getEntityByUUID(scene, [uuid UTF8String]);
        if (!entity) {
            return @"";
        }

        std::shared_ptr<Material> material;
        if (auto* renderer = entity->getComponent<MeshRenderer>()) {
            material = renderer->getMaterial(0);
        } else if (auto* skinned = entity->getComponent<SkinnedMeshRenderer>()) {
            material = skinned->getMaterial(0);
        } else if (auto* instanced = entity->getComponent<InstancedMeshRenderer>()) {
            material = instanced->getMaterial(0);
        }
        if (!material) {
            return @"";
        }

        AssetDatabase& db = AssetDatabase::getInstance();
        const std::string& root = db.getRootPath();
        if (root.empty()) {
            return @"";
        }

        std::string baseName;
        if (name && name.length > 0) {
            baseName = [name UTF8String];
        }
        if (baseName.empty() && !material->getName().empty()) {
            baseName = material->getName();
        }
        if (baseName.empty()) {
            baseName = entity->getName();
        }
        baseName = SanitizeFilename(baseName);

        std::filesystem::path materialsDir = std::filesystem::path(root) / "Materials";
        std::error_code ec;
        std::filesystem::create_directories(materialsDir, ec);

        std::filesystem::path target = materialsDir / (baseName + ".cmat");
        int counter = 1;
        while (std::filesystem::exists(target, ec)) {
            target = materialsDir / (baseName + "_" + std::to_string(counter++) + ".cmat");
        }

        nlohmann::json j;
        j["version"] = 1;
        j["name"] = material->getName();

        nlohmann::json props;
        Math::Vector4 albedo = material->getAlbedo();
        Math::Vector3 emission = material->getEmission();
        Math::Vector2 tiling = material->getUVTiling();
        Math::Vector2 offset = material->getUVOffset();
        Math::Vector2 terrainLayer0Tiling = material->getTerrainLayer0Tiling();
        Math::Vector2 terrainLayer1Tiling = material->getTerrainLayer1Tiling();
        Math::Vector2 terrainLayer2Tiling = material->getTerrainLayer2Tiling();
        Math::Vector3 windDir = material->getWindDirection();

        props["albedo"] = {albedo.x, albedo.y, albedo.z, albedo.w};
        props["metallic"] = material->getMetallic();
        props["roughness"] = material->getRoughness();
        props["ao"] = material->getAO();
        props["emission"] = {emission.x, emission.y, emission.z};
        props["emissionStrength"] = material->getEmissionStrength();
        props["normalScale"] = material->getNormalScale();
        props["heightScale"] = material->getHeightScale();
        props["heightInvert"] = material->getHeightInvert();
        props["renderMode"] = static_cast<int>(material->getRenderMode());
        props["alphaCutoff"] = material->getAlphaCutoff();
        props["cullMode"] = static_cast<int>(material->getCullMode());
        props["twoSided"] = material->isTwoSided();
        props["alphaToCoverage"] = material->getAlphaToCoverage();
        props["windEnabled"] = material->getWindEnabled();
        props["windStrength"] = material->getWindStrength();
        props["windSpeed"] = material->getWindSpeed();
        props["windScale"] = material->getWindScale();
        props["windGust"] = material->getWindGust();
        props["windDirection"] = {windDir.x, windDir.y, windDir.z};
        props["lodFadeEnabled"] = material->getLodFadeEnabled();
        props["lodFadeStart"] = material->getLodFadeStart();
        props["lodFadeEnd"] = material->getLodFadeEnd();
        props["ditherEnabled"] = material->getDitherEnabled();
        props["billboardEnabled"] = material->getBillboardEnabled();
        props["billboardStart"] = material->getBillboardStart();
        props["billboardEnd"] = material->getBillboardEnd();
        props["impostorEnabled"] = material->getImpostorEnabled();
        props["impostorRows"] = material->getImpostorRows();
        props["impostorCols"] = material->getImpostorCols();
        props["terrainEnabled"] = material->getTerrainEnabled();
        props["terrainBlendSharpness"] = material->getTerrainBlendSharpness();
        props["terrainHeightStart"] = material->getTerrainHeightStart();
        props["terrainHeightEnd"] = material->getTerrainHeightEnd();
        props["terrainSlopeStart"] = material->getTerrainSlopeStart();
        props["terrainSlopeEnd"] = material->getTerrainSlopeEnd();
        props["terrainLayer0Tiling"] = {terrainLayer0Tiling.x, terrainLayer0Tiling.y};
        props["terrainLayer1Tiling"] = {terrainLayer1Tiling.x, terrainLayer1Tiling.y};
        props["terrainLayer2Tiling"] = {terrainLayer2Tiling.x, terrainLayer2Tiling.y};
        props["tiling"] = {tiling.x, tiling.y};
        props["offset"] = {offset.x, offset.y};
        j["properties"] = props;

        nlohmann::json textures;
        textures["albedo"] = SerializeTexturePath(material->getAlbedoTexture(), db);
        textures["normal"] = SerializeTexturePath(material->getNormalTexture(), db);
        textures["metallic"] = SerializeTexturePath(material->getMetallicTexture(), db);
        textures["roughness"] = SerializeTexturePath(material->getRoughnessTexture(), db);
        textures["ao"] = SerializeTexturePath(material->getAOTexture(), db);
        textures["emission"] = SerializeTexturePath(material->getEmissionTexture(), db);
        textures["orm"] = SerializeTexturePath(material->getORMTexture(), db);
        textures["height"] = SerializeTexturePath(material->getHeightTexture(), db);
        textures["terrainControl"] = SerializeTexturePath(material->getTerrainControlTexture(), db);
        textures["terrainLayer0"] = SerializeTexturePath(material->getTerrainLayer0Texture(), db);
        textures["terrainLayer1"] = SerializeTexturePath(material->getTerrainLayer1Texture(), db);
        textures["terrainLayer2"] = SerializeTexturePath(material->getTerrainLayer2Texture(), db);
        textures["terrainLayer0Normal"] = SerializeTexturePath(material->getTerrainLayer0NormalTexture(), db);
        textures["terrainLayer1Normal"] = SerializeTexturePath(material->getTerrainLayer1NormalTexture(), db);
        textures["terrainLayer2Normal"] = SerializeTexturePath(material->getTerrainLayer2NormalTexture(), db);
        textures["terrainLayer0ORM"] = SerializeTexturePath(material->getTerrainLayer0ORMTexture(), db);
        textures["terrainLayer1ORM"] = SerializeTexturePath(material->getTerrainLayer1ORMTexture(), db);
        textures["terrainLayer2ORM"] = SerializeTexturePath(material->getTerrainLayer2ORMTexture(), db);
        j["textures"] = textures;

        std::ofstream out(target);
        if (!out.is_open()) {
            return @"";
        }
        out << j.dump(2);
        out.close();

        std::string savedPath = target.lexically_normal().string();
        db.registerAsset(savedPath, "material");
        return [NSString stringWithUTF8String:savedPath.c_str()];
    }];
}

- (BOOL)applyMaterialAsset:(NSString *)path toEntity:(NSString *)uuid {
    return [self performSyncBool:^BOOL {
        if (!_engine || !_engine->getRenderer()) {
            return NO;
        }
        if (!path || path.length == 0) {
            return NO;
        }
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) {
            return NO;
        }
        Entity* entity = SceneCommands::getEntityByUUID(scene, [uuid UTF8String]);
        if (!entity) {
            return NO;
        }
        std::ifstream in([path UTF8String]);
        if (!in.good()) {
            return NO;
        }
        nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
        if (!j.is_object()) {
            return NO;
        }
        auto material = std::make_shared<Material>();
        if (!ApplyMaterialJson(j, material, _engine->getRenderer())) {
            return NO;
        }

        if (auto* renderer = entity->getComponent<MeshRenderer>()) {
            renderer->setMaterial(material);
        }
        if (auto* skinned = entity->getComponent<SkinnedMeshRenderer>()) {
            skinned->setMaterial(material);
        }
        if (auto* instanced = entity->getComponent<InstancedMeshRenderer>()) {
            instanced->setMaterial(material);
        }
        return YES;
    }];
}

- (BOOL)applyMaterialAssetToAllMaterials:(NSString *)path toEntity:(NSString *)uuid {
    return [self performSyncBool:^BOOL {
        if (!_engine || !_engine->getRenderer()) {
            return NO;
        }
        if (!path || path.length == 0) {
            return NO;
        }
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) {
            return NO;
        }
        Entity* entity = SceneCommands::getEntityByUUID(scene, [uuid UTF8String]);
        if (!entity) {
            return NO;
        }
        std::ifstream in([path UTF8String]);
        if (!in.good()) {
            return NO;
        }
        nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
        if (!j.is_object()) {
            return NO;
        }
        auto baseMaterial = std::make_shared<Material>();
        if (!ApplyMaterialJson(j, baseMaterial, _engine->getRenderer())) {
            return NO;
        }

        auto applyAll = [&](auto* renderer) {
            if (!renderer) {
                return;
            }
            uint32_t count = renderer->getMaterialCount();
            if (count == 0) {
                renderer->setMaterial(baseMaterial);
                return;
            }
            renderer->setMaterial(0, baseMaterial);
            for (uint32_t i = 1; i < count; ++i) {
                renderer->setMaterial(i, CloneMaterial(baseMaterial));
            }
        };

        applyAll(entity->getComponent<MeshRenderer>());
        applyAll(entity->getComponent<SkinnedMeshRenderer>());
        applyAll(entity->getComponent<InstancedMeshRenderer>());
        return YES;
    }];
}

// MARK: - Render Stats

- (NSDictionary *)getRenderStats {
    return (NSDictionary *)[self performSyncObject:^id{
        if (!_engine || !_engine->getRenderer()) {
            return @{};
        }
        const auto& stats = _engine->getRenderer()->getStats();
        return @{
            @"drawCalls": @(stats.drawCalls),
            @"triangles": @(stats.triangles),
            @"vertices": @(stats.vertices),
            @"instanceInput": @(stats.instanceInput),
            @"instanceVisible": @(stats.instanceVisible),
            @"frameTimeMs": @(stats.frameTime)
        };
    }];
}

- (NSDictionary *)getEnvironmentSettings {
    return (NSDictionary *)[self performSyncObject:^id{
        if (!_engine || !_engine->getRenderer()) {
            return @{};
        }
        
        const auto& env = _engine->getRenderer()->getEnvironmentSettings();
        return @{
            @"exposure": @(env.exposureEV),
            @"iblIntensity": @(env.iblIntensity),
            @"skyIntensity": @(env.skyIntensity),
            @"ambientIntensity": @(env.ambientIntensity),
            @"ambientColor": @[@(env.ambientColor.x), @(env.ambientColor.y), @(env.ambientColor.z)],
            @"rotation": @[@(env.rotation.x), @(env.rotation.y), @(env.rotation.z)],
            @"tint": @[@(env.tint.x), @(env.tint.y), @(env.tint.z)],
            @"saturation": @(env.saturation),
            @"contrast": @(env.contrast),
            @"blur": @(env.blurLevel),
            @"skyboxVisible": @(env.skyboxVisible ? 1 : 0),
            @"path": [NSString stringWithUTF8String:env.sourcePath.c_str()]
        };
    }];
}

- (BOOL)loadEnvironmentMap:(NSString *)path {
    return [self performSyncBool:^BOOL {
        if (!_engine || !_engine->getRenderer()) {
            return NO;
        }
        bool ok = _engine->getRenderer()->loadEnvironmentMap([path UTF8String]);
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (scene && ok && path) {
            scene->getSettings().environment.skyboxPath = [path UTF8String];
        }
        return ok ? YES : NO;
    }];
}

- (BOOL)cookEnvironmentMap:(NSString *)path outputPath:(NSString *)outputPath {
    return [self performSyncBool:^BOOL {
        if (!_engine || !_engine->getRenderer() || !path || !outputPath) {
            return NO;
        }
        return _engine->getRenderer()->saveCookedEnvironmentMap(path.UTF8String, outputPath.UTF8String) ? YES : NO;
    }];
}

- (BOOL)cookStaticLightmap:(NSString *)path outputPath:(NSString *)outputPath {
    return [self performSyncBool:^BOOL {
        if (!path || !outputPath) {
            return NO;
        }
        return CookStaticLightmapToKTX2(path.UTF8String, outputPath.UTF8String) ? YES : NO;
    }];
}

- (void)resetEnvironment {
    [self performAsync:^{
        if (_engine && _engine->getRenderer()) {
            _engine->getRenderer()->resetEnvironment();
            Scene* scene = SceneManager::getInstance().getActiveScene();
            if (scene) {
                scene->getSettings().environment.skyboxPath = "Builtin Sky";
            }
        }
    }];
}

- (void)setEnvironmentExposure:(float)ev {
    [self performAsync:^{
        if (_engine && _engine->getRenderer()) {
            _engine->getRenderer()->setEnvironmentExposure(ev);
            Scene* scene = SceneManager::getInstance().getActiveScene();
            if (scene) {
                scene->getSettings().environment.exposureEV = ev;
            }
        }
    }];
}

- (void)setEnvironmentIBLIntensity:(float)intensity {
    [self performAsync:^{
        if (_engine && _engine->getRenderer()) {
            _engine->getRenderer()->setEnvironmentIblIntensity(intensity);
            Scene* scene = SceneManager::getInstance().getActiveScene();
            if (scene) {
                scene->getSettings().environment.iblIntensity = intensity;
            }
        }
    }];
}

- (void)setEnvironmentSkyIntensity:(float)intensity {
    [self performAsync:^{
        if (_engine && _engine->getRenderer()) {
            _engine->getRenderer()->setEnvironmentSkyIntensity(intensity);
            Scene* scene = SceneManager::getInstance().getActiveScene();
            if (scene) {
                scene->getSettings().environment.skyIntensity = intensity;
            }
        }
    }];
}

- (void)setEnvironmentAmbientIntensity:(float)intensity {
    [self performAsync:^{
        if (_engine && _engine->getRenderer()) {
            _engine->getRenderer()->setEnvironmentAmbientIntensity(intensity);
            Scene* scene = SceneManager::getInstance().getActiveScene();
            if (scene) {
                scene->getSettings().environment.ambientIntensity = intensity;
            }
        }
    }];
}

- (void)setEnvironmentAmbientColorWithR:(float)r g:(float)g b:(float)b {
    [self performAsync:^{
        if (_engine && _engine->getRenderer()) {
            _engine->getRenderer()->setEnvironmentAmbientColor(Crescent::Math::Vector3(r, g, b));
            Scene* scene = SceneManager::getInstance().getActiveScene();
            if (scene) {
                scene->getSettings().environment.ambientColor = Math::Vector3(r, g, b);
            }
        }
    }];
}

- (void)setEnvironmentRotationWithX:(float)x y:(float)y z:(float)z {
    [self performAsync:^{
        if (_engine && _engine->getRenderer()) {
            _engine->getRenderer()->setEnvironmentRotation(Crescent::Math::Vector3(x, y, z));
            Scene* scene = SceneManager::getInstance().getActiveScene();
            if (scene) {
                scene->getSettings().environment.rotation = Math::Vector3(x, y, z);
            }
        }
    }];
}

- (void)setEnvironmentTintWithR:(float)r g:(float)g b:(float)b {
    [self performAsync:^{
        if (_engine && _engine->getRenderer()) {
            _engine->getRenderer()->setEnvironmentTint(Crescent::Math::Vector3(r, g, b));
            Scene* scene = SceneManager::getInstance().getActiveScene();
            if (scene) {
                scene->getSettings().environment.tint = Math::Vector3(r, g, b);
            }
        }
    }];
}

- (void)setEnvironmentSaturation:(float)saturation {
    [self performAsync:^{
        if (_engine && _engine->getRenderer()) {
            _engine->getRenderer()->setEnvironmentSaturation(saturation);
            Scene* scene = SceneManager::getInstance().getActiveScene();
            if (scene) {
                scene->getSettings().environment.saturation = saturation;
            }
        }
    }];
}

- (void)setEnvironmentContrast:(float)contrast {
    [self performAsync:^{
        if (_engine && _engine->getRenderer()) {
            _engine->getRenderer()->setEnvironmentContrast(contrast);
            Scene* scene = SceneManager::getInstance().getActiveScene();
            if (scene) {
                scene->getSettings().environment.contrast = contrast;
            }
        }
    }];
}

- (void)setEnvironmentBlur:(float)blur {
    [self performAsync:^{
        if (_engine && _engine->getRenderer()) {
            _engine->getRenderer()->setEnvironmentBlur(blur);
            Scene* scene = SceneManager::getInstance().getActiveScene();
            if (scene) {
                scene->getSettings().environment.blurLevel = blur;
            }
        }
    }];
}

- (void)setEnvironmentSkyboxVisible:(BOOL)visible {
    [self performAsync:^{
        if (_engine && _engine->getRenderer()) {
            _engine->getRenderer()->setEnvironmentSkyboxVisible(visible);
            Scene* scene = SceneManager::getInstance().getActiveScene();
            if (scene) {
                scene->getSettings().environment.skyboxVisible = visible;
            }
        }
    }];
}

// MARK: - Scene IO / Play Mode

- (BOOL)saveSceneAtPath:(NSString *)path {
    return [self performSyncBool:^BOOL {
        [self finishActiveTerrainStrokeInternal];
        [self clearTerrainBrushPreviewInternal];
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene || !path) {
            return NO;
        }
        scene->serialize(path.UTF8String);
        return YES;
    }];
}

- (BOOL)saveCookedRuntimeSceneAtPath:(NSString *)path includeEditorOnly:(BOOL)includeEditorOnly {
    return [self performSyncBool:^BOOL {
        [self finishActiveTerrainStrokeInternal];
        [self clearTerrainBrushPreviewInternal];
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene || !path) {
            return NO;
        }
        return SceneSerializer::SaveCookedRuntimeScene(scene, path.UTF8String, includeEditorOnly);
    }];
}

- (BOOL)loadSceneAtPath:(NSString *)path {
    return [self performSyncBool:^BOOL {
        [self finishActiveTerrainStrokeInternal];
        [self clearTerrainBrushPreviewInternal];
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene || !path) {
            return NO;
        }
        scene->deserialize(path.UTF8String);
        return YES;
    }];
}

- (void)enterPlayMode {
    [self performAsync:^{
        [self finishActiveTerrainStrokeInternal];
        [self clearTerrainBrushPreviewInternal];
        SceneManager::getInstance().enterPlayMode();
    }];
}

- (void)exitPlayMode {
    [self performAsync:^{
        [self clearTerrainBrushPreviewInternal];
        SceneManager::getInstance().exitPlayMode();
    }];
}

- (BOOL)isPlaying {
    return [self performSyncBool:^BOOL {
        return SceneManager::getInstance().isPlaying();
    }];
}

- (BOOL)isPaused {
    return [self performSyncBool:^BOOL {
        return SceneManager::getInstance().isPaused();
    }];
}

- (void)setPaused:(BOOL)paused {
    [self performAsync:^{
        SceneManager::getInstance().setPaused(paused);
    }];
}

- (float)getTimeScale {
    return [self performSyncFloat:^float {
        return SceneManager::getInstance().getTimeScale();
    }];
}

- (void)setTimeScale:(float)scale {
    [self performAsync:^{
        SceneManager::getInstance().setTimeScale(scale);
    }];
}

- (void)setViewMode:(int)mode {
    [self performAsync:^{
        SceneManager::ViewMode viewMode = SceneManager::ViewMode::Scene;
        if (mode == 1) {
            viewMode = SceneManager::ViewMode::Game;
        }
        SceneManager::getInstance().setViewMode(viewMode);
    }];
}

- (int)getViewMode {
    return [self performSyncInt:^int {
        return static_cast<int>(SceneManager::getInstance().getViewMode());
    }];
}

- (void)setAssetRootPath:(NSString *)path {
    [self performAsync:^{
        if (!path) {
            return;
        }
        AssetDatabase::getInstance().setRootPath(path.UTF8String);
    }];
}

- (BOOL)createProjectAtPath:(NSString *)path name:(NSString *)name {
    return [self performSyncBool:^BOOL {
        if (!path || !name) {
            return NO;
        }
        auto project = ProjectManager::getInstance().createProject(path.UTF8String, name.UTF8String);
        return project != nullptr;
    }];
}

- (BOOL)openProjectAtPath:(NSString *)path {
    return [self performSyncBool:^BOOL {
        if (!path) {
            return NO;
        }
        auto project = ProjectManager::getInstance().openProject(path.UTF8String);
        return project != nullptr;
    }];
}

- (NSDictionary *)getProjectInfo {
    return (NSDictionary *)[self performSyncObject:^id{
        auto project = ProjectManager::getInstance().getActiveProject();
        if (!project) {
            return @{};
        }
        return @{
            @"name": [NSString stringWithUTF8String:project->getName().c_str()],
            @"rootPath": [NSString stringWithUTF8String:project->getRootPath().c_str()],
            @"assetsPath": [NSString stringWithUTF8String:project->getAssetsPath().c_str()],
            @"scenesPath": [NSString stringWithUTF8String:project->getScenesPath().c_str()],
            @"libraryPath": [NSString stringWithUTF8String:project->getLibraryPath().c_str()]
        };
    }];
}

- (NSString *)importAssetAtPath:(NSString *)path type:(NSString *)type {
    return (NSString *)[self performSyncObject:^id{
        if (!path) {
            return @"";
        }
        std::string assetType = type ? type.UTF8String : "";
        std::string result = AssetDatabase::getInstance().importAsset(path.UTF8String, assetType);
        if (result.empty()) {
            return @"";
        }
        return [NSString stringWithUTF8String:result.c_str()];
    }];
}

- (NSDictionary *)getAssetMetaAtPath:(NSString *)path {
    return (NSDictionary *)[self performSyncObject:^id{
        if (!path) {
            return @{};
        }
        AssetDatabase& db = AssetDatabase::getInstance();
        AssetRecord record;
        std::string pathStr = path.UTF8String;
        if (!db.getRecordForPath(pathStr, record)) {
            std::string guid = db.registerAsset(pathStr);
            if (!guid.empty()) {
                db.getRecordForPath(pathStr, record);
            }
        }
        if (record.guid.empty()) {
            return @{};
        }

        NSDictionary* model = @{
            @"scale": @(record.modelSettings.scale),
            @"flipUVs": @(record.modelSettings.flipUVs),
            @"onlyLOD0": @(record.modelSettings.onlyLOD0),
            @"mergeStaticMeshes": @(record.modelSettings.mergeStaticMeshes)
        };
        NSDictionary* texture = @{
            @"srgb": @(record.textureSettings.srgb),
            @"generateMipmaps": @(record.textureSettings.generateMipmaps),
            @"flipY": @(record.textureSettings.flipY),
            @"maxSize": @(record.textureSettings.maxSize),
            @"normalMap": @(record.textureSettings.normalMap)
        };
        NSDictionary* hdri = @{
            @"flipY": @(record.hdriSettings.flipY),
            @"maxSize": @(record.hdriSettings.maxSize)
        };

        return @{
            @"guid": [NSString stringWithUTF8String:record.guid.c_str()],
            @"type": [NSString stringWithUTF8String:record.type.c_str()],
            @"model": model,
            @"texture": texture,
            @"hdri": hdri
        };
    }];
}

- (BOOL)updateModelImportSettings:(NSString *)guid settings:(NSDictionary *)settings {
    return [self performSyncBool:^BOOL {
        if (!guid || !settings) {
            return NO;
        }
        ModelImportSettings imported;
        NSNumber* scale = settings[@"scale"];
        NSNumber* flipUVs = settings[@"flipUVs"];
        NSNumber* onlyLOD0 = settings[@"onlyLOD0"];
        NSNumber* mergeStaticMeshes = settings[@"mergeStaticMeshes"];
        if (scale) imported.scale = scale.floatValue;
        if (flipUVs) imported.flipUVs = flipUVs.boolValue;
        if (onlyLOD0) imported.onlyLOD0 = onlyLOD0.boolValue;
        if (mergeStaticMeshes) imported.mergeStaticMeshes = mergeStaticMeshes.boolValue;
        return AssetDatabase::getInstance().updateModelImportSettings(guid.UTF8String, imported);
    }];
}

- (BOOL)updateTextureImportSettings:(NSString *)guid settings:(NSDictionary *)settings {
    return [self performSyncBool:^BOOL {
        if (!guid || !settings) {
            return NO;
        }
        TextureImportSettings imported;
        NSNumber* srgb = settings[@"srgb"];
        NSNumber* generateMipmaps = settings[@"generateMipmaps"];
        NSNumber* flipY = settings[@"flipY"];
        NSNumber* maxSize = settings[@"maxSize"];
        NSNumber* normalMap = settings[@"normalMap"];
        if (srgb) imported.srgb = srgb.boolValue;
        if (generateMipmaps) imported.generateMipmaps = generateMipmaps.boolValue;
        if (flipY) imported.flipY = flipY.boolValue;
        if (maxSize) imported.maxSize = maxSize.intValue;
        if (normalMap) imported.normalMap = normalMap.boolValue;
        return AssetDatabase::getInstance().updateTextureImportSettings(guid.UTF8String, imported);
    }];
}

- (BOOL)updateHdriImportSettings:(NSString *)guid settings:(NSDictionary *)settings {
    return [self performSyncBool:^BOOL {
        if (!guid || !settings) {
            return NO;
        }
        HdriImportSettings imported;
        NSNumber* flipY = settings[@"flipY"];
        NSNumber* maxSize = settings[@"maxSize"];
        if (flipY) imported.flipY = flipY.boolValue;
        if (maxSize) imported.maxSize = maxSize.intValue;
        return AssetDatabase::getInstance().updateHdriImportSettings(guid.UTF8String, imported);
    }];
}

- (BOOL)reimportModelAsset:(NSString *)guid {
    return [self performSyncBool:^BOOL {
        if (!guid) {
            return NO;
        }
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) {
            return NO;
        }
        return SceneCommands::reimportModelAsset(scene, guid.UTF8String);
    }];
}

- (BOOL)reimportTextureAsset:(NSString *)guid {
    return [self performSyncBool:^BOOL {
        if (!guid) {
            return NO;
        }
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) {
            return NO;
        }
        return SceneCommands::reimportTextureAsset(scene, guid.UTF8String);
    }];
}

- (BOOL)reimportHdriAsset:(NSString *)guid {
    return [self performSyncBool:^BOOL {
        if (!guid) {
            return NO;
        }
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) {
            return NO;
        }
        return SceneCommands::reimportHdriAsset(scene, guid.UTF8String);
    }];
}

- (BOOL)moveAssetAtPath:(NSString *)sourcePath toPath:(NSString *)targetPath overwrite:(BOOL)overwrite {
    return [self performSyncBool:^BOOL {
        if (!sourcePath || !targetPath) {
            return NO;
        }
        return AssetDatabase::getInstance().moveAsset(sourcePath.UTF8String, targetPath.UTF8String, overwrite);
    }];
}

- (NSDictionary *)getProjectSettings {
    return (NSDictionary *)[self performSyncObject:^id{
        auto project = ProjectManager::getInstance().getActiveProject();
        if (!project) {
            return @{};
        }
        const auto& settings = project->getSettings();
        auto qualityToDict = [](const SceneQualitySettings& quality) {
            return @{
                @"overrideProject": @(quality.overrideProject),
                @"shadowQuality": @(quality.shadowQuality),
                @"shadowResolution": @(quality.shadowResolution),
                @"msaaSamples": @(quality.msaaSamples),
                @"anisotropy": @(quality.anisotropy),
                @"renderScale": @(quality.renderScale),
                @"lodBias": @(quality.lodBias),
                @"textureQuality": @(quality.textureQuality),
                @"upscaler": @(quality.upscaler)
            };
        };
        NSMutableArray* assetPaths = [NSMutableArray array];
        for (const auto& path : settings.assetPaths) {
            [assetPaths addObject:[NSString stringWithUTF8String:path.c_str()]];
        }
        NSMutableArray* renderProfiles = [NSMutableArray array];
        for (const auto& profile : settings.renderProfiles) {
            [renderProfiles addObject:@{
                @"name": [NSString stringWithUTF8String:profile.name.c_str()],
                @"quality": qualityToDict(profile.quality)
            }];
        }
        NSMutableArray* qualityPresets = [NSMutableArray array];
        for (const auto& preset : settings.qualityPresets) {
            [qualityPresets addObject:@{
                @"name": [NSString stringWithUTF8String:preset.name.c_str()],
                @"quality": qualityToDict(preset.quality)
            }];
        }
        NSMutableArray* inputBindings = [NSMutableArray array];
        for (const auto& binding : settings.inputBindings) {
            [inputBindings addObject:@{
                @"action": [NSString stringWithUTF8String:binding.action.c_str()],
                @"key": [NSString stringWithUTF8String:binding.key.c_str()],
                @"mouseButton": [NSString stringWithUTF8String:binding.mouseButton.c_str()],
                @"scale": @(binding.scale),
                @"invert": @(binding.invert)
            }];
        }
        return @{
            @"defaultRenderProfile": [NSString stringWithUTF8String:settings.defaultRenderProfile.c_str()],
            @"buildTarget": [NSString stringWithUTF8String:settings.buildTarget.c_str()],
            @"productName": [NSString stringWithUTF8String:settings.productName.c_str()],
            @"bundleIdentifier": [NSString stringWithUTF8String:settings.bundleIdentifier.c_str()],
            @"startupScene": [NSString stringWithUTF8String:settings.startupScene.c_str()],
            @"assetPaths": assetPaths,
            @"renderProfiles": renderProfiles,
            @"qualityPresets": qualityPresets,
            @"inputBindings": inputBindings
        };
    }];
}

- (void)setProjectSettings:(NSDictionary *)settings {
    [self performAsync:^{
        auto project = ProjectManager::getInstance().getActiveProject();
        if (!project || !settings) {
            return;
        }
        ProjectSettings updated = project->getSettings();
        auto parseQuality = [](NSDictionary* dict) {
            SceneQualitySettings quality;
            if (!dict) {
                return quality;
            }
            if (dict[@"overrideProject"]) quality.overrideProject = [dict[@"overrideProject"] boolValue];
            if (dict[@"shadowQuality"]) quality.shadowQuality = [dict[@"shadowQuality"] intValue];
            if (dict[@"shadowResolution"]) quality.shadowResolution = [dict[@"shadowResolution"] intValue];
            if (dict[@"msaaSamples"]) quality.msaaSamples = [dict[@"msaaSamples"] intValue];
            if (dict[@"anisotropy"]) quality.anisotropy = [dict[@"anisotropy"] intValue];
            if (dict[@"renderScale"]) quality.renderScale = [dict[@"renderScale"] floatValue];
            if (dict[@"lodBias"]) quality.lodBias = [dict[@"lodBias"] floatValue];
            if (dict[@"textureQuality"]) quality.textureQuality = [dict[@"textureQuality"] intValue];
            if (dict[@"upscaler"]) quality.upscaler = [dict[@"upscaler"] intValue];
            return quality;
        };
        if (settings[@"defaultRenderProfile"]) {
            updated.defaultRenderProfile = [settings[@"defaultRenderProfile"] UTF8String];
        }
        if (settings[@"buildTarget"]) {
            updated.buildTarget = [settings[@"buildTarget"] UTF8String];
        }
        if (settings[@"productName"]) {
            updated.productName = [settings[@"productName"] UTF8String];
        }
        if (settings[@"bundleIdentifier"]) {
            updated.bundleIdentifier = [settings[@"bundleIdentifier"] UTF8String];
        }
        if (settings[@"startupScene"]) {
            updated.startupScene = [settings[@"startupScene"] UTF8String];
        }
        if (settings[@"assetPaths"] && [settings[@"assetPaths"] isKindOfClass:[NSArray class]]) {
            updated.assetPaths.clear();
            NSArray* paths = settings[@"assetPaths"];
            for (id entry in paths) {
                if ([entry isKindOfClass:[NSString class]]) {
                    updated.assetPaths.emplace_back([entry UTF8String]);
                }
            }
            if (updated.assetPaths.empty()) {
                updated.assetPaths.emplace_back("Assets");
            }
        }
        if (settings[@"renderProfiles"] && [settings[@"renderProfiles"] isKindOfClass:[NSArray class]]) {
            NSArray* profiles = settings[@"renderProfiles"];
            if (profiles.count > 0) {
                updated.renderProfiles.clear();
                for (id entry in profiles) {
                    if (![entry isKindOfClass:[NSDictionary class]]) {
                        continue;
                    }
                    NSDictionary* dict = (NSDictionary *)entry;
                    ProjectSettings::RenderProfile profile;
                    if (dict[@"name"]) {
                        profile.name = [dict[@"name"] UTF8String];
                    }
                    if (dict[@"quality"] && [dict[@"quality"] isKindOfClass:[NSDictionary class]]) {
                        profile.quality = parseQuality((NSDictionary *)dict[@"quality"]);
                    }
                    updated.renderProfiles.push_back(profile);
                }
            }
        }
        if (settings[@"qualityPresets"] && [settings[@"qualityPresets"] isKindOfClass:[NSArray class]]) {
            NSArray* presets = settings[@"qualityPresets"];
            if (presets.count > 0) {
                updated.qualityPresets.clear();
                for (id entry in presets) {
                    if (![entry isKindOfClass:[NSDictionary class]]) {
                        continue;
                    }
                    NSDictionary* dict = (NSDictionary *)entry;
                    ProjectSettings::QualityPreset preset;
                    if (dict[@"name"]) {
                        preset.name = [dict[@"name"] UTF8String];
                    }
                    if (dict[@"quality"] && [dict[@"quality"] isKindOfClass:[NSDictionary class]]) {
                        preset.quality = parseQuality((NSDictionary *)dict[@"quality"]);
                    }
                    updated.qualityPresets.push_back(preset);
                }
            }
        }
        if (settings[@"inputBindings"] && [settings[@"inputBindings"] isKindOfClass:[NSArray class]]) {
            updated.inputBindings.clear();
            NSArray* bindings = settings[@"inputBindings"];
            for (id entry in bindings) {
                if (![entry isKindOfClass:[NSDictionary class]]) {
                    continue;
                }
                NSDictionary* dict = (NSDictionary *)entry;
                ProjectSettings::InputBinding binding;
                if (dict[@"action"]) binding.action = [dict[@"action"] UTF8String];
                if (dict[@"key"]) binding.key = [dict[@"key"] UTF8String];
                if (dict[@"mouseButton"]) binding.mouseButton = [dict[@"mouseButton"] UTF8String];
                if (dict[@"scale"]) binding.scale = [dict[@"scale"] floatValue];
                if (dict[@"invert"]) binding.invert = [dict[@"invert"] boolValue];
                updated.inputBindings.push_back(binding);
            }
        }
        project->setSettings(updated);
        project->save();
    }];
}

- (NSDictionary *)getSceneSettings {
    return (NSDictionary *)[self performSyncObject:^id{
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) {
            return @{};
        }
        SceneSettings settings = scene->getSettings();
        Renderer* renderer = Engine::getInstance().getRenderer();
        if (renderer) {
            const auto& env = renderer->getEnvironmentSettings();
            settings.environment.skyboxPath = env.sourcePath.empty() ? "Builtin Sky" : env.sourcePath;
            settings.environment.exposureEV = env.exposureEV;
            settings.environment.iblIntensity = env.iblIntensity;
            settings.environment.skyIntensity = env.skyIntensity;
            settings.environment.ambientIntensity = env.ambientIntensity;
            settings.environment.ambientColor = env.ambientColor;
            settings.environment.saturation = env.saturation;
            settings.environment.contrast = env.contrast;
            settings.environment.blurLevel = env.blurLevel;
            settings.environment.tint = env.tint;
            settings.environment.rotation = env.rotation;
            settings.environment.skyboxVisible = env.skyboxVisible;
            settings.environment.skyMode = env.skyMode;
            settings.environment.autoSunColor = env.autoSunColor;
            scene->setSettings(settings);
        }

        auto vec3ToArray = [](const Math::Vector3& v) {
            return @[@(v.x), @(v.y), @(v.z)];
        };

        NSDictionary* environment = @{
            @"skyboxPath": [NSString stringWithUTF8String:settings.environment.skyboxPath.c_str()],
            @"exposureEV": @(settings.environment.exposureEV),
            @"iblIntensity": @(settings.environment.iblIntensity),
            @"skyIntensity": @(settings.environment.skyIntensity),
            @"ambientIntensity": @(settings.environment.ambientIntensity),
            @"ambientColor": vec3ToArray(settings.environment.ambientColor),
            @"saturation": @(settings.environment.saturation),
            @"contrast": @(settings.environment.contrast),
            @"blurLevel": @(settings.environment.blurLevel),
            @"tint": vec3ToArray(settings.environment.tint),
            @"rotation": vec3ToArray(settings.environment.rotation),
            @"skyboxVisible": @(settings.environment.skyboxVisible),
            @"skyMode": @(settings.environment.skyMode),
            @"autoSunColor": @(settings.environment.autoSunColor)
        };

        NSDictionary* fog = @{
            @"enabled": @(settings.fog.enabled),
            @"color": vec3ToArray(settings.fog.color),
            @"density": @(settings.fog.density),
            @"startDistance": @(settings.fog.startDistance),
            @"endDistance": @(settings.fog.endDistance),
            @"heightFog": @(settings.fog.heightFog),
            @"height": @(settings.fog.height),
            @"heightFalloff": @(settings.fog.heightFalloff),
            @"volumetricScattering": @(settings.fog.volumetricScattering),
            @"volumetricAnisotropy": @(settings.fog.volumetricAnisotropy),
            @"volumetricHistoryWeight": @(settings.fog.volumetricHistoryWeight),
            @"volumetricQuality": @(settings.fog.volumetricQuality)
        };

        NSDictionary* postProcess = @{
            @"shadowDebugMode": @(settings.postProcess.shadowDebugMode),
            @"enabled": @(settings.postProcess.enabled),
            @"bloom": @(settings.postProcess.bloom),
            @"bloomIntensity": @(settings.postProcess.bloomIntensity),
            @"bloomThreshold": @(settings.postProcess.bloomThreshold),
            @"toneMapping": @(settings.postProcess.toneMapping),
            @"toneMappingMode": @(settings.postProcess.toneMappingMode),
            @"colorGrading": @(settings.postProcess.colorGrading),
            @"colorGradingIntensity": @(settings.postProcess.colorGradingIntensity),
            @"colorGradingLUT": [NSString stringWithUTF8String:settings.postProcess.colorGradingLUT.c_str()],
            @"saturation": @(settings.postProcess.saturation),
            @"contrast": @(settings.postProcess.contrast),
            @"vignette": @(settings.postProcess.vignette),
            @"vignetteIntensity": @(settings.postProcess.vignetteIntensity),
            @"filmGrain": @(settings.postProcess.filmGrain),
            @"filmGrainIntensity": @(settings.postProcess.filmGrainIntensity),
            @"ssao": @(settings.postProcess.ssao),
            @"ssaoRadius": @(settings.postProcess.ssaoRadius),
            @"ssaoStrength": @(settings.postProcess.ssaoStrength),
            @"ssr": @(settings.postProcess.ssr),
            @"ssrMaxRoughness": @(settings.postProcess.ssrMaxRoughness),
            @"ssrThickness": @(settings.postProcess.ssrThickness),
            @"taa": @(settings.postProcess.taa),
            @"taaSharpness": @(settings.postProcess.taaSharpness),
            @"taaSpecularStability": @(settings.postProcess.taaSpecularStability),
            @"taaSpecularStabilityStrength": @(settings.postProcess.taaSpecularStabilityStrength),
            @"fxaa": @(settings.postProcess.fxaa),
            @"motionBlur": @(settings.postProcess.motionBlur),
            @"motionBlurStrength": @(settings.postProcess.motionBlurStrength),
            @"depthOfField": @(settings.postProcess.depthOfField),
            @"dofFocusDistance": @(settings.postProcess.dofFocusDistance),
            @"dofAperture": @(settings.postProcess.dofAperture)
        };

        NSDictionary* quality = @{
            @"overrideProject": @(settings.quality.overrideProject),
            @"shadowQuality": @(settings.quality.shadowQuality),
            @"shadowResolution": @(settings.quality.shadowResolution),
            @"msaaSamples": @(settings.quality.msaaSamples),
            @"anisotropy": @(settings.quality.anisotropy),
            @"renderScale": @(settings.quality.renderScale),
            @"lodBias": @(settings.quality.lodBias),
            @"textureQuality": @(settings.quality.textureQuality),
            @"upscaler": @(settings.quality.upscaler)
        };

        NSDictionary* staticLighting = @{
            @"enabled": @(settings.staticLighting.enabled),
            @"mode": @(settings.staticLighting.mode),
            @"atlasSize": @(settings.staticLighting.atlasSize),
            @"maxAtlasCount": @(settings.staticLighting.maxAtlasCount),
            @"texelsPerUnit": @(settings.staticLighting.texelsPerUnit),
            @"samplesPerTexel": @(settings.staticLighting.samplesPerTexel),
            @"indirectBounces": @(settings.staticLighting.indirectBounces),
            @"denoise": @(settings.staticLighting.denoise),
            @"bakeDirectLighting": @(settings.staticLighting.bakeDirectLighting),
            @"directionalLightmaps": @(settings.staticLighting.directionalLightmaps),
            @"shadowmask": @(settings.staticLighting.shadowmask),
            @"autoUnwrap": @(settings.staticLighting.autoUnwrap),
            @"unwrapPadding": @(settings.staticLighting.unwrapPadding),
            @"outputDirectory": [NSString stringWithUTF8String:settings.staticLighting.outputDirectory.c_str()],
            @"bakeManifestPath": [NSString stringWithUTF8String:settings.staticLighting.bakeManifestPath.c_str()],
            @"lastBakeHash": [NSString stringWithUTF8String:settings.staticLighting.lastBakeHash.c_str()]
        };

        return @{
            @"environment": environment,
            @"fog": fog,
            @"postProcess": postProcess,
            @"quality": quality,
            @"staticLighting": staticLighting
        };
    }];
}

- (void)setSceneSettings:(NSDictionary *)settings {
    [self performAsync:^{
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene || !settings) {
            return;
        }
        SceneSettings updated = scene->getSettings();
        if (settings[@"environment"] && [settings[@"environment"] isKindOfClass:[NSDictionary class]]) {
            NSDictionary* env = settings[@"environment"];
            if (env[@"skyboxPath"]) {
                updated.environment.skyboxPath = [env[@"skyboxPath"] UTF8String];
            }
            if (env[@"exposureEV"]) {
                updated.environment.exposureEV = [env[@"exposureEV"] floatValue];
            }
            if (env[@"iblIntensity"]) {
                updated.environment.iblIntensity = [env[@"iblIntensity"] floatValue];
            }
            if (env[@"skyIntensity"]) {
                updated.environment.skyIntensity = [env[@"skyIntensity"] floatValue];
            }
            if (env[@"ambientIntensity"]) {
                updated.environment.ambientIntensity = [env[@"ambientIntensity"] floatValue];
            }
            if (env[@"ambientColor"] && [env[@"ambientColor"] isKindOfClass:[NSArray class]]) {
                NSArray* ambient = env[@"ambientColor"];
                if (ambient.count >= 3) {
                    updated.environment.ambientColor = Math::Vector3([ambient[0] floatValue], [ambient[1] floatValue], [ambient[2] floatValue]);
                }
            }
            if (env[@"saturation"]) {
                updated.environment.saturation = [env[@"saturation"] floatValue];
            }
            if (env[@"contrast"]) {
                updated.environment.contrast = [env[@"contrast"] floatValue];
            }
            if (env[@"blurLevel"]) {
                updated.environment.blurLevel = [env[@"blurLevel"] floatValue];
            }
            if (env[@"tint"] && [env[@"tint"] isKindOfClass:[NSArray class]]) {
                NSArray* tint = env[@"tint"];
                if (tint.count >= 3) {
                    updated.environment.tint = Math::Vector3([tint[0] floatValue], [tint[1] floatValue], [tint[2] floatValue]);
                }
            }
            if (env[@"rotation"] && [env[@"rotation"] isKindOfClass:[NSArray class]]) {
                NSArray* rot = env[@"rotation"];
                if (rot.count >= 3) {
                    updated.environment.rotation = Math::Vector3([rot[0] floatValue], [rot[1] floatValue], [rot[2] floatValue]);
                }
            }
            if (env[@"skyboxVisible"]) {
                updated.environment.skyboxVisible = [env[@"skyboxVisible"] boolValue];
            }
            if (env[@"skyMode"]) {
                updated.environment.skyMode = [env[@"skyMode"] intValue];
            }
            if (env[@"autoSunColor"]) {
                updated.environment.autoSunColor = [env[@"autoSunColor"] boolValue];
            }
        }
        if (settings[@"fog"] && [settings[@"fog"] isKindOfClass:[NSDictionary class]]) {
            NSDictionary* fog = settings[@"fog"];
            if (fog[@"enabled"]) {
                updated.fog.enabled = [fog[@"enabled"] boolValue];
            }
            if (fog[@"color"] && [fog[@"color"] isKindOfClass:[NSArray class]]) {
                NSArray* color = fog[@"color"];
                if (color.count >= 3) {
                    updated.fog.color = Math::Vector3([color[0] floatValue], [color[1] floatValue], [color[2] floatValue]);
                }
            }
            if (fog[@"density"]) {
                updated.fog.density = [fog[@"density"] floatValue];
            }
            if (fog[@"startDistance"]) {
                updated.fog.startDistance = [fog[@"startDistance"] floatValue];
            }
            if (fog[@"endDistance"]) {
                updated.fog.endDistance = [fog[@"endDistance"] floatValue];
            }
            if (fog[@"heightFog"]) {
                updated.fog.heightFog = [fog[@"heightFog"] boolValue];
            }
            if (fog[@"height"]) {
                updated.fog.height = [fog[@"height"] floatValue];
            }
            if (fog[@"heightFalloff"]) {
                updated.fog.heightFalloff = [fog[@"heightFalloff"] floatValue];
            }
            if (fog[@"volumetricScattering"]) {
                updated.fog.volumetricScattering = [fog[@"volumetricScattering"] floatValue];
            }
            if (fog[@"volumetricAnisotropy"]) {
                updated.fog.volumetricAnisotropy = [fog[@"volumetricAnisotropy"] floatValue];
            }
            if (fog[@"volumetricHistoryWeight"]) {
                updated.fog.volumetricHistoryWeight = [fog[@"volumetricHistoryWeight"] floatValue];
            }
            if (fog[@"volumetricQuality"]) {
                updated.fog.volumetricQuality = [fog[@"volumetricQuality"] intValue];
            }
        }
        if (settings[@"postProcess"] && [settings[@"postProcess"] isKindOfClass:[NSDictionary class]]) {
            NSDictionary* post = settings[@"postProcess"];
            if (post[@"shadowDebugMode"]) updated.postProcess.shadowDebugMode = [post[@"shadowDebugMode"] intValue];
            if (post[@"enabled"]) updated.postProcess.enabled = [post[@"enabled"] boolValue];
            if (post[@"bloom"]) updated.postProcess.bloom = [post[@"bloom"] boolValue];
            if (post[@"bloomIntensity"]) updated.postProcess.bloomIntensity = [post[@"bloomIntensity"] floatValue];
            if (post[@"bloomThreshold"]) updated.postProcess.bloomThreshold = [post[@"bloomThreshold"] floatValue];
            if (post[@"toneMapping"]) updated.postProcess.toneMapping = [post[@"toneMapping"] boolValue];
            if (post[@"toneMappingMode"]) updated.postProcess.toneMappingMode = [post[@"toneMappingMode"] intValue];
            if (post[@"colorGrading"]) updated.postProcess.colorGrading = [post[@"colorGrading"] boolValue];
            if (post[@"colorGradingIntensity"]) updated.postProcess.colorGradingIntensity = [post[@"colorGradingIntensity"] floatValue];
            if (post[@"colorGradingLUT"]) {
                NSString* path = post[@"colorGradingLUT"];
                if ([path isKindOfClass:[NSString class]]) {
                    updated.postProcess.colorGradingLUT = [path UTF8String];
                }
            }
            if (post[@"saturation"]) updated.postProcess.saturation = [post[@"saturation"] floatValue];
            if (post[@"contrast"]) updated.postProcess.contrast = [post[@"contrast"] floatValue];
            if (post[@"vignette"]) updated.postProcess.vignette = [post[@"vignette"] boolValue];
            if (post[@"vignetteIntensity"]) updated.postProcess.vignetteIntensity = [post[@"vignetteIntensity"] floatValue];
            if (post[@"filmGrain"]) updated.postProcess.filmGrain = [post[@"filmGrain"] boolValue];
            if (post[@"filmGrainIntensity"]) updated.postProcess.filmGrainIntensity = [post[@"filmGrainIntensity"] floatValue];
            if (post[@"ssao"]) updated.postProcess.ssao = [post[@"ssao"] boolValue];
            if (post[@"ssaoRadius"]) updated.postProcess.ssaoRadius = [post[@"ssaoRadius"] floatValue];
            if (post[@"ssaoStrength"]) updated.postProcess.ssaoStrength = [post[@"ssaoStrength"] floatValue];
            if (post[@"ssr"]) updated.postProcess.ssr = [post[@"ssr"] boolValue];
            if (post[@"ssrMaxRoughness"]) updated.postProcess.ssrMaxRoughness = [post[@"ssrMaxRoughness"] floatValue];
            if (post[@"ssrThickness"]) updated.postProcess.ssrThickness = [post[@"ssrThickness"] floatValue];
            if (post[@"taa"]) updated.postProcess.taa = [post[@"taa"] boolValue];
            if (post[@"taaSharpness"]) updated.postProcess.taaSharpness = [post[@"taaSharpness"] floatValue];
            if (post[@"taaSpecularStability"]) updated.postProcess.taaSpecularStability = [post[@"taaSpecularStability"] boolValue];
            if (post[@"taaSpecularStabilityStrength"]) updated.postProcess.taaSpecularStabilityStrength = [post[@"taaSpecularStabilityStrength"] floatValue];
            if (post[@"fxaa"]) updated.postProcess.fxaa = [post[@"fxaa"] boolValue];
            if (post[@"motionBlur"]) updated.postProcess.motionBlur = [post[@"motionBlur"] boolValue];
            if (post[@"motionBlurStrength"]) updated.postProcess.motionBlurStrength = [post[@"motionBlurStrength"] floatValue];
            if (post[@"depthOfField"]) updated.postProcess.depthOfField = [post[@"depthOfField"] boolValue];
            if (post[@"dofFocusDistance"]) updated.postProcess.dofFocusDistance = [post[@"dofFocusDistance"] floatValue];
            if (post[@"dofAperture"]) updated.postProcess.dofAperture = [post[@"dofAperture"] floatValue];
        }
        if (settings[@"quality"] && [settings[@"quality"] isKindOfClass:[NSDictionary class]]) {
            NSDictionary* quality = settings[@"quality"];
            if (quality[@"overrideProject"]) updated.quality.overrideProject = [quality[@"overrideProject"] boolValue];
            if (quality[@"shadowQuality"]) updated.quality.shadowQuality = [quality[@"shadowQuality"] intValue];
            if (quality[@"shadowResolution"]) updated.quality.shadowResolution = [quality[@"shadowResolution"] intValue];
            if (quality[@"msaaSamples"]) updated.quality.msaaSamples = [quality[@"msaaSamples"] intValue];
            if (quality[@"anisotropy"]) updated.quality.anisotropy = [quality[@"anisotropy"] intValue];
            if (quality[@"renderScale"]) updated.quality.renderScale = [quality[@"renderScale"] floatValue];
            if (quality[@"lodBias"]) updated.quality.lodBias = [quality[@"lodBias"] floatValue];
            if (quality[@"textureQuality"]) updated.quality.textureQuality = [quality[@"textureQuality"] intValue];
            if (quality[@"upscaler"]) updated.quality.upscaler = [quality[@"upscaler"] intValue];
        }
        if (settings[@"staticLighting"] && [settings[@"staticLighting"] isKindOfClass:[NSDictionary class]]) {
            NSDictionary* staticLighting = settings[@"staticLighting"];
            if (staticLighting[@"enabled"]) updated.staticLighting.enabled = [staticLighting[@"enabled"] boolValue];
            if (staticLighting[@"mode"]) updated.staticLighting.mode = [staticLighting[@"mode"] intValue];
            if (staticLighting[@"atlasSize"]) updated.staticLighting.atlasSize = [staticLighting[@"atlasSize"] intValue];
            if (staticLighting[@"maxAtlasCount"]) updated.staticLighting.maxAtlasCount = [staticLighting[@"maxAtlasCount"] intValue];
            if (staticLighting[@"texelsPerUnit"]) updated.staticLighting.texelsPerUnit = [staticLighting[@"texelsPerUnit"] floatValue];
            if (staticLighting[@"samplesPerTexel"]) updated.staticLighting.samplesPerTexel = [staticLighting[@"samplesPerTexel"] intValue];
            if (staticLighting[@"indirectBounces"]) updated.staticLighting.indirectBounces = [staticLighting[@"indirectBounces"] intValue];
            if (staticLighting[@"denoise"]) updated.staticLighting.denoise = [staticLighting[@"denoise"] boolValue];
            if (staticLighting[@"bakeDirectLighting"]) updated.staticLighting.bakeDirectLighting = [staticLighting[@"bakeDirectLighting"] boolValue];
            if (staticLighting[@"directionalLightmaps"]) updated.staticLighting.directionalLightmaps = [staticLighting[@"directionalLightmaps"] boolValue];
            if (staticLighting[@"shadowmask"]) updated.staticLighting.shadowmask = [staticLighting[@"shadowmask"] boolValue];
            if (staticLighting[@"autoUnwrap"]) updated.staticLighting.autoUnwrap = [staticLighting[@"autoUnwrap"] boolValue];
            if (staticLighting[@"unwrapPadding"]) updated.staticLighting.unwrapPadding = [staticLighting[@"unwrapPadding"] intValue];
            if (staticLighting[@"outputDirectory"]) {
                NSString* path = staticLighting[@"outputDirectory"];
                if ([path isKindOfClass:[NSString class]]) {
                    updated.staticLighting.outputDirectory = [path UTF8String];
                }
            }
            if (staticLighting[@"bakeManifestPath"]) {
                NSString* path = staticLighting[@"bakeManifestPath"];
                if ([path isKindOfClass:[NSString class]]) {
                    updated.staticLighting.bakeManifestPath = [path UTF8String];
                }
            }
            if (staticLighting[@"lastBakeHash"]) {
                NSString* value = staticLighting[@"lastBakeHash"];
                if ([value isKindOfClass:[NSString class]]) {
                    updated.staticLighting.lastBakeHash = [value UTF8String];
                }
            }
        }
        scene->setSettings(updated);
        scene->applySettings();
    }];
}

// MARK: - Mesh Renderer / Static Lighting

- (NSDictionary *)getMeshRendererInfo:(NSString *)uuid {
    return (NSDictionary *)[self performSyncObject:^id{
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return @{};
        Entity* entity = SceneCommands::getEntityByUUID(scene, [uuid UTF8String]);
        if (!entity) return @{};
        MeshRenderer* renderer = entity->getComponent<MeshRenderer>();
        if (!renderer || entity->getComponent<SkinnedMeshRenderer>() || entity->getComponent<InstancedMeshRenderer>()) {
            return @{};
        }

        const auto& staticLighting = renderer->getStaticLighting();
        return @{
            @"castsShadows": @(renderer->getCastShadows()),
            @"receiveShadows": @(renderer->getReceiveShadows()),
            @"staticGeometry": @(staticLighting.staticGeometry),
            @"contributeGI": @(staticLighting.contributeGI),
            @"receiveGI": @(staticLighting.receiveGI),
            @"lightmapIndex": @(staticLighting.lightmapIndex)
        };
    }];
}

- (BOOL)setMeshRendererInfo:(NSString *)uuid info:(NSDictionary *)info {
    return [self performSyncBool:^BOOL {
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return NO;
        Entity* entity = SceneCommands::getEntityByUUID(scene, [uuid UTF8String]);
        if (!entity) return NO;
        MeshRenderer* renderer = entity->getComponent<MeshRenderer>();
        if (!renderer || entity->getComponent<SkinnedMeshRenderer>() || entity->getComponent<InstancedMeshRenderer>()) {
            return NO;
        }

        if (NSNumber* castsShadows = info[@"castsShadows"]) {
            renderer->setCastShadows(castsShadows.boolValue);
        }
        if (NSNumber* receiveShadows = info[@"receiveShadows"]) {
            renderer->setReceiveShadows(receiveShadows.boolValue);
        }
        if (info[@"staticGeometry"] || info[@"contributeGI"] || info[@"receiveGI"]) {
            MeshRenderer::StaticLightingData staticLighting = renderer->getStaticLighting();
            if (NSNumber* staticGeometry = info[@"staticGeometry"]) {
                bool isStatic = staticGeometry.boolValue;
                staticLighting.staticGeometry = isStatic;
                if (!isStatic) {
                    staticLighting.lightmapIndex = -1;
                    staticLighting.lightmapScaleOffset = Math::Vector4(1.0f, 1.0f, 0.0f, 0.0f);
                    staticLighting.lightmapPath.clear();
                    staticLighting.directionalLightmapPath.clear();
                    staticLighting.shadowmaskPath.clear();
                }
            }
            if (NSNumber* contributeGI = info[@"contributeGI"]) {
                staticLighting.contributeGI = contributeGI.boolValue;
            }
            if (NSNumber* receiveGI = info[@"receiveGI"]) {
                staticLighting.receiveGI = receiveGI.boolValue;
            }
            renderer->setStaticLighting(staticLighting);
        }
        return YES;
    }];
}

// MARK: - Light Utilities

static Crescent::Light* GetLightByUUID(const std::string& uuidStr) {
    Scene* scene = SceneManager::getInstance().getActiveScene();
    if (!scene) return nullptr;
    uint64_t uuidVal = 0;
    try {
        uuidVal = std::stoull(uuidStr, nullptr, 16); // UUID strings are hex
    } catch (...) {
        return nullptr;
    }
    Entity* entity = scene->findEntity(Crescent::UUID(uuidVal));
    if (!entity) return nullptr;
    return entity->getComponent<Crescent::Light>();
}

static Crescent::Decal* GetDecalByUUID(const std::string& uuidStr) {
    Scene* scene = SceneManager::getInstance().getActiveScene();
    if (!scene) return nullptr;
    uint64_t uuidVal = 0;
    try {
        uuidVal = std::stoull(uuidStr, nullptr, 16);
    } catch (...) {
        return nullptr;
    }
    Entity* entity = scene->findEntity(Crescent::UUID(uuidVal));
    if (!entity) return nullptr;
    return entity->getComponent<Crescent::Decal>();
}

// MARK: - Light bridging

- (NSArray<NSDictionary *> *)listLights {
    return (NSArray<NSDictionary *> *)[self performSyncObject:^id{
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return @[];
        NSMutableArray* result = [NSMutableArray array];
        const auto& entities = scene->getAllEntities();
        for (const auto& entPtr : entities) {
            Entity* e = entPtr.get();
            if (!e) continue;
            Light* l = e->getComponent<Light>();
            if (!l) continue;
            NSDictionary* info = @{
                @"uuid": [NSString stringWithUTF8String:e->getUUID().toString().c_str()],
                @"name": [NSString stringWithUTF8String:e->getName().c_str()],
                @"type": @(static_cast<int>(l->getType()))
            };
            [result addObject:info];
        }
        return result;
    }];
}

- (NSDictionary *)getLightInfo:(NSString *)uuid {
    return (NSDictionary *)[self performSyncObject:^id{
        Light* light = GetLightByUUID(uuid.UTF8String);
        if (!light) return @{};
        NSMutableDictionary* dict = [NSMutableDictionary dictionary];
        dict[@"type"] = @(static_cast<int>(light->getType()));
        dict[@"enabled"] = @YES;
        Math::Vector3 c = light->getColor();
        dict[@"color"] = @[@(c.x), @(c.y), @(c.z)];
        dict[@"temperatureK"] = @(light->getColorTemperature());
        dict[@"intensity"] = @(light->getIntensity());
        dict[@"intensityUnit"] = @(static_cast<int>(light->getIntensityUnit()));
        dict[@"range"] = @(light->getRange());
        dict[@"falloff"] = @(static_cast<int>(light->getFalloffModel()));
        dict[@"spotAngle"] = @(light->getSpotAngle());
        dict[@"innerSpotAngle"] = @(light->getInnerSpotAngle());
        Math::Vector2 area = light->getAreaSize();
        dict[@"areaSize"] = @[@(area.x), @(area.y)];
        dict[@"sourceRadius"] = @(light->getSourceRadius());
        dict[@"sourceLength"] = @(light->getSourceLength());
        dict[@"castsShadows"] = @(light->getCastShadows());
        dict[@"shadowResolution"] = @(light->getShadowMapResolution());
        dict[@"shadowBias"] = @(light->getShadowBias());
        dict[@"shadowNormalBias"] = @(light->getShadowNormalBias());
        dict[@"shadowNearPlane"] = @(light->getShadowNearPlane());
        dict[@"shadowFarPlane"] = @(light->getShadowFarPlane());
        dict[@"softShadows"] = @(light->getSoftShadows());
        dict[@"contactShadows"] = @(light->getContactShadows());
        dict[@"penumbra"] = @(light->getPenumbra());
        dict[@"cascadeCount"] = @(light->getCascadeCount());
        NSMutableArray* splits = [NSMutableArray array];
        for (float s : light->getCascadeSplits()) { [splits addObject:@(s)]; }
        dict[@"cascadeSplits"] = splits;
        dict[@"cookieIndex"] = @(light->getCookieIndex());
        dict[@"iesIndex"] = @(light->getIESProfileIndex());
        dict[@"volumetric"] = @(light->getVolumetric());
        dict[@"contributeToStaticBake"] = @(light->getContributeToStaticBake());
        dict[@"mobility"] = @(static_cast<int>(light->getMobility()));
        dict[@"shadowmaskChannel"] = @(light->getShadowmaskChannel());
        return dict;
    }];
}

- (BOOL)setLightInfo:(NSString *)uuid info:(NSDictionary *)info {
    return [self performSyncBool:^BOOL {
        Light* light = GetLightByUUID(uuid.UTF8String);
        if (!light) return NO;
        Light::Type previousType = light->getType();
        bool typeChanged = false;
        bool localLightTypeSelected = false;
        
        if (NSNumber* t = info[@"type"]) {
            Light::Type newType = static_cast<Light::Type>(t.intValue);
            typeChanged = newType != previousType;
            localLightTypeSelected = newType == Light::Type::Point || newType == Light::Type::Spot;
            light->setType(newType);
        }
        if (NSNumber* temp = info[@"temperatureK"]) {
            light->setColorTemperature(temp.floatValue);
        }
        if (NSArray* col = info[@"color"]; col.count == 3) {
            light->setColor(Math::Vector3([col[0] floatValue], [col[1] floatValue], [col[2] floatValue]));
        }
        if (NSNumber* inten = info[@"intensity"]) {
            light->setIntensity(inten.floatValue);
        }
        if (NSNumber* unit = info[@"intensityUnit"]) {
            light->setIntensityUnit(static_cast<Light::IntensityUnit>(unit.intValue));
        }
        if (NSNumber* range = info[@"range"]) {
            light->setRange(range.floatValue);
        }
        if (NSNumber* falloff = info[@"falloff"]) {
            light->setFalloffModel(static_cast<Light::FalloffModel>(falloff.intValue));
        }
        if (NSNumber* spot = info[@"spotAngle"]) {
            light->setSpotAngle(spot.floatValue);
        }
        if (NSNumber* inner = info[@"innerSpotAngle"]) {
            light->setInnerSpotAngle(inner.floatValue);
        }
        if (NSArray* area = info[@"areaSize"]; area.count >= 2) {
            light->setAreaSize(Math::Vector2([area[0] floatValue], [area[1] floatValue]));
        }
        if (NSNumber* sourceRadius = info[@"sourceRadius"]) {
            light->setSourceRadius(sourceRadius.floatValue);
        }
        if (NSNumber* sourceLength = info[@"sourceLength"]) {
            light->setSourceLength(sourceLength.floatValue);
        }
        if (typeChanged && localLightTypeSelected && !info[@"castsShadows"] && !light->getCastShadows()) {
            // When artists switch a generic/local light type in the inspector,
            // default to shadowed practical lights unless they explicitly disabled it.
            light->setCastShadows(true);
            if (!info[@"shadowResolution"] && light->getShadowMapResolution() < 512u) {
                light->setShadowMapResolution(512u);
            }
        }
        if (NSNumber* cast = info[@"castsShadows"]) {
            light->setCastShadows(cast.boolValue);
        }
        if (NSNumber* res = info[@"shadowResolution"]) {
            int preset = res.intValue;
            int clamped = std::max(256, std::min(8192, preset));
            light->setShadowMapResolution(clamped);
        }
        if (NSNumber* bias = info[@"shadowBias"]) {
            light->setShadowBias(bias.floatValue);
        }
        if (NSNumber* nbias = info[@"shadowNormalBias"]) {
            light->setShadowNormalBias(nbias.floatValue);
        }
        if (NSNumber* nearPlane = info[@"shadowNearPlane"]) {
            float farPlane = light->getShadowFarPlane();
            if (NSNumber* farValue = info[@"shadowFarPlane"]) {
                farPlane = farValue.floatValue;
            }
            light->setShadowRange(nearPlane.floatValue, farPlane);
        } else if (NSNumber* farPlane = info[@"shadowFarPlane"]) {
            light->setShadowRange(light->getShadowNearPlane(), farPlane.floatValue);
        }
        if (NSNumber* soft = info[@"softShadows"]) {
            light->setSoftShadows(soft.boolValue);
        }
        if (NSNumber* contact = info[@"contactShadows"]) {
            light->setContactShadows(contact.boolValue);
        }
        if (NSNumber* pen = info[@"penumbra"]) {
            light->setPenumbra(pen.floatValue);
        }
        if (NSNumber* casc = info[@"cascadeCount"]) {
            light->setCascadeCount((uint8_t)casc.unsignedIntValue);
        }
        if (NSArray* splits = info[@"cascadeSplits"]) {
            std::array<float,4> s = light->getCascadeSplits();
            for (NSUInteger i = 0; i < MIN(4, splits.count); ++i) {
                s[i] = [splits[i] floatValue];
            }
            light->setCascadeSplits(s);
        }
        if (NSNumber* cookie = info[@"cookieIndex"]) {
            light->setCookieIndex(cookie.intValue);
        }
        if (NSNumber* ies = info[@"iesIndex"]) {
            light->setIESProfileIndex(ies.intValue);
        }
        if (NSNumber* vol = info[@"volumetric"]) {
            light->setVolumetric(vol.boolValue);
        }
        if (NSNumber* bake = info[@"contributeToStaticBake"]) {
            light->setContributeToStaticBake(bake.boolValue);
        } else if (NSNumber* legacyBake = info[@"bakeToVertexLighting"]) {
            light->setContributeToStaticBake(legacyBake.boolValue);
        }
        if (NSNumber* mobility = info[@"mobility"]) {
            int clamped = std::max(0, std::min(2, mobility.intValue));
            light->setMobility(static_cast<Light::Mobility>(clamped));
        }
        if (NSNumber* channel = info[@"shadowmaskChannel"]) {
            light->setShadowmaskChannel(channel.intValue);
        }
        return YES;
    }];
}

// MARK: - Decal bridging

- (NSDictionary *)getDecalInfo:(NSString *)uuid {
    return (NSDictionary *)[self performSyncObject:^id{
        Decal* decal = GetDecalByUUID(uuid.UTF8String);
        if (!decal) return @{};

        Math::Vector4 tint = decal->getTint();
        Math::Vector2 tiling = decal->getTiling();
        Math::Vector2 offset = decal->getOffset();

        auto pathFor = [](const std::shared_ptr<Texture2D>& tex, const std::string& fallback) -> NSString* {
            std::string path = tex ? tex->getPath() : fallback;
            if (!path.empty() && path.rfind("builtin://", 0) != 0) {
                return [NSString stringWithUTF8String:path.c_str()];
            }
            return @"";
        };

        NSString* albedoPath = pathFor(decal->getAlbedoTexture(), decal->getAlbedoPath());
        NSString* normalPath = pathFor(decal->getNormalTexture(), decal->getNormalPath());
        NSString* ormPath = pathFor(decal->getORMTexture(), decal->getORMPath());
        NSString* maskPath = pathFor(decal->getMaskTexture(), decal->getMaskPath());

        return @{
            @"color": @[@(tint.x), @(tint.y), @(tint.z), @(tint.w)],
            @"opacity": @(decal->getOpacity()),
            @"tiling": @[@(tiling.x), @(tiling.y)],
            @"offset": @[@(offset.x), @(offset.y)],
            @"softness": @(decal->getEdgeSoftness()),
            @"albedo": albedoPath,
            @"normal": normalPath,
            @"orm": ormPath,
            @"mask": maskPath
        };
    }];
}

- (BOOL)setDecalInfo:(NSString *)uuid info:(NSDictionary *)info {
    return [self performSyncBool:^BOOL {
        Decal* decal = GetDecalByUUID(uuid.UTF8String);
        if (!decal) return NO;

        if (NSArray* color = info[@"color"]; color.count >= 3) {
            float a = (color.count >= 4) ? [color[3] floatValue] : 1.0f;
            decal->setTint(Math::Vector4([color[0] floatValue],
                                         [color[1] floatValue],
                                         [color[2] floatValue],
                                         a));
        }
        if (NSNumber* opacity = info[@"opacity"]) {
            decal->setOpacity(opacity.floatValue);
        }
        if (NSArray* tiling = info[@"tiling"]; tiling.count >= 2) {
            decal->setTiling(Math::Vector2([tiling[0] floatValue], [tiling[1] floatValue]));
        }
        if (NSArray* offset = info[@"offset"]; offset.count >= 2) {
            decal->setOffset(Math::Vector2([offset[0] floatValue], [offset[1] floatValue]));
        }
        if (NSNumber* softness = info[@"softness"]) {
            decal->setEdgeSoftness(softness.floatValue);
        }
        return YES;
    }];
}

- (BOOL)loadDecalTexture:(NSString *)uuid slot:(NSString *)slot path:(NSString *)path {
    return [self performSyncBool:^BOOL {
        if (!_engine || !_engine->getRenderer() || !_engine->getRenderer()->getTextureLoader()) {
            return NO;
        }
        Decal* decal = GetDecalByUUID(uuid.UTF8String);
        if (!decal) return NO;

        std::string slotStr = [slot UTF8String];
        bool srgb = (slotStr == "albedo");
        auto texture = _engine->getRenderer()->getTextureLoader()->loadTexture([path UTF8String], srgb, true);
        if (!texture) return NO;
        if (slotStr == "normal") {
            decal->setNormalTexture(texture);
        } else if (slotStr == "orm") {
            decal->setORMTexture(texture);
        } else if (slotStr == "mask") {
            decal->setMaskTexture(texture);
        } else {
            decal->setAlbedoTexture(texture);
        }
        return YES;
    }];
}

- (void)clearDecalTexture:(NSString *)uuid slot:(NSString *)slot {
    [self performAsync:^{
        Decal* decal = GetDecalByUUID(uuid.UTF8String);
        if (!decal) return;
        std::string slotStr = [slot UTF8String];
        if (slotStr == "normal") {
            decal->setNormalTexture(nullptr);
        } else if (slotStr == "orm") {
            decal->setORMTexture(nullptr);
        } else if (slotStr == "mask") {
            decal->setMaskTexture(nullptr);
        } else {
            decal->setAlbedoTexture(nullptr);
        }
    }];
}

// MARK: - Skinned Mesh / Animation

- (NSDictionary *)getSkinnedMeshInfo:(NSString *)uuid {
    return (NSDictionary *)[self performSyncObject:^id{
        Animator* animator = nullptr;
        SkinnedMeshRenderer* skinned = nullptr;
        std::vector<SkinnedMeshRenderer*> targets;
        if (!ResolveAnimatorAndSkinnedTargets(uuid.UTF8String, animator, skinned, targets) || !skinned) {
            return @{};
        }
        
        NSMutableArray* clipNames = [NSMutableArray array];
        const auto& clips = skinned->getAnimationClips();
        for (size_t i = 0; i < clips.size(); ++i) {
            std::string name = clips[i] ? clips[i]->getName() : "";
            if (name.empty()) {
                name = "Clip " + std::to_string(i);
            }
            [clipNames addObject:[NSString stringWithUTF8String:name.c_str()]];
        }
        
        int activeIndex = skinned->getActiveClipIndex();
        float duration = 0.0f;
        if (activeIndex >= 0 && activeIndex < (int)clips.size() && clips[activeIndex]) {
            duration = clips[activeIndex]->getDurationSeconds();
        }

        NSMutableDictionary* info = [@{
            @"clips": clipNames,
            @"clipIndex": @(activeIndex),
            @"playing": @(skinned->isPlaying()),
            @"looping": @(skinned->isLooping()),
            @"speed": @(skinned->getPlaybackSpeed()),
            @"time": @(skinned->getTimeSeconds()),
            @"duration": @(duration)
        } mutableCopy];

        NSMutableArray* clipSources = [NSMutableArray array];
        const auto& sources = skinned->getAnimationClipSources();
        for (size_t i = 0; i < sources.size(); ++i) {
            const auto& source = sources[i];
            NSString* path = [NSString stringWithUTF8String:source.path.c_str()];
            NSString* name = path.lastPathComponent.length > 0 ? path.lastPathComponent : path;
            NSString* guid = source.guid.empty() ? @"" : [NSString stringWithUTF8String:source.guid.c_str()];
            [clipSources addObject:@{
                @"index": @(static_cast<int>(i)),
                @"name": name ?: @"",
                @"path": path ?: @"",
                @"guid": guid,
                @"clipCount": @(static_cast<int>(source.clips.size()))
            }];
        }
        info[@"clipSources"] = clipSources;

        if (animator) {
            NSMutableArray* stateNames = [NSMutableArray array];
            const auto& states = animator->getStates();
            for (size_t i = 0; i < states.size(); ++i) {
                const std::string& name = states[i].name.empty() ? std::string("State ") + std::to_string(i) : states[i].name;
                [stateNames addObject:[NSString stringWithUTF8String:name.c_str()]];
            }
            info[@"states"] = stateNames;
            info[@"stateIndex"] = @(animator->getCurrentStateIndex());
            info[@"blendDuration"] = @(animator->getDefaultBlendDuration());
            info[@"rootMotionEnabled"] = @(animator->getRootMotionEnabled());
            info[@"rootMotionPosition"] = @(animator->getApplyRootMotionPosition());
            info[@"rootMotionRotation"] = @(animator->getApplyRootMotionRotation());

            NSMutableArray* params = [NSMutableArray array];
            const auto& parameters = animator->getParameters();
            for (const auto& param : parameters) {
                NSString* type = @"Float";
                switch (param.type) {
                    case AnimatorParameterType::Int: type = @"Int"; break;
                    case AnimatorParameterType::Bool: type = @"Bool"; break;
                    case AnimatorParameterType::Trigger: type = @"Trigger"; break;
                    case AnimatorParameterType::Float:
                    default: type = @"Float"; break;
                }
                NSMutableDictionary* entry = [@{
                    @"name": [NSString stringWithUTF8String:param.name.c_str()],
                    @"type": type,
                    @"float": @(param.floatValue),
                    @"int": @(param.intValue),
                    @"bool": @(param.boolValue),
                    @"trigger": @(param.triggerValue)
                } mutableCopy];
                [params addObject:entry];
            }
            info[@"parameters"] = params;
        } else {
            info[@"rootMotionEnabled"] = @(skinned->getRootMotionEnabled());
            info[@"rootMotionPosition"] = @(skinned->getApplyRootMotionPosition());
            info[@"rootMotionRotation"] = @(skinned->getApplyRootMotionRotation());
        }

        return info;
    }];
}

- (BOOL)setSkinnedMeshInfo:(NSString *)uuid info:(NSDictionary *)info {
    return [self performSyncBool:^BOOL {
        Animator* animator = nullptr;
        SkinnedMeshRenderer* skinned = nullptr;
        std::vector<SkinnedMeshRenderer*> targets;
        if (!ResolveAnimatorAndSkinnedTargets(uuid.UTF8String, animator, skinned, targets) || !skinned) {
            return NO;
        }
        
        bool appliedState = false;
        if (animator) {
            if (NSNumber* blend = info[@"blendDuration"]) {
                animator->setDefaultBlendDuration(std::max(0.0f, blend.floatValue));
            }
            if (NSNumber* stateIndex = info[@"stateIndex"]) {
                float blend = animator->getDefaultBlendDuration();
                if (NSNumber* blendOverride = info[@"blendDuration"]) {
                    blend = std::max(0.0f, blendOverride.floatValue);
                }
                animator->setCurrentStateIndex(stateIndex.intValue, blend, true);
                appliedState = true;
            }
        }
        
        if (!appliedState) {
            if (NSNumber* clipIndex = info[@"clipIndex"]) {
                for (auto* target : targets) {
                    if (target) {
                        target->setActiveClipIndex(clipIndex.intValue);
                    }
                }
            }
        }
        if (NSNumber* playing = info[@"playing"]) {
            for (auto* target : targets) {
                if (target) {
                    target->setPlaying(playing.boolValue);
                }
            }
        }
        if (NSNumber* looping = info[@"looping"]) {
            for (auto* target : targets) {
                if (target) {
                    target->setLooping(looping.boolValue);
                }
            }
        }
        if (NSNumber* speed = info[@"speed"]) {
            float playbackSpeed = std::max(0.0f, speed.floatValue);
            for (auto* target : targets) {
                if (target) {
                    target->setPlaybackSpeed(playbackSpeed);
                }
            }
        }
        if (NSNumber* time = info[@"time"]) {
            float timeSeconds = std::max(0.0f, time.floatValue);
            for (auto* target : targets) {
                if (target) {
                    target->setTimeSeconds(timeSeconds);
                }
            }
        }
        return YES;
    }];
}

- (BOOL)addAnimationSource:(NSString *)uuid path:(NSString *)path {
    return [self performSyncBool:^BOOL {
        if (!path || path.length == 0) {
            return NO;
        }

        Animator* animator = nullptr;
        SkinnedMeshRenderer* skinned = nullptr;
        std::vector<SkinnedMeshRenderer*> targets;
        if (!ResolveAnimatorAndSkinnedTargets(uuid.UTF8String, animator, skinned, targets) || !skinned) {
            return NO;
        }

        std::string resolvedPath = path.UTF8String;
        AssetDatabase& db = AssetDatabase::getInstance();
        std::string guid = db.registerAsset(resolvedPath, "model");
        SceneCommands::ModelImportOptions options = ResolveAnimationImportOptions(resolvedPath, guid);

        int oldClipCount = static_cast<int>(skinned->getAnimationClips().size());
        bool addedAny = false;
        for (auto* target : targets) {
            if (!target) {
                continue;
            }
            auto skeleton = target->getSkeleton();
            if (!skeleton) {
                continue;
            }
            auto clips = SceneCommands::importAnimationClipsForSkeleton(resolvedPath, *skeleton, options);
            if (clips.empty()) {
                continue;
            }
            AnimationClipSource source;
            source.path = resolvedPath;
            source.guid = guid;
            source.clips = std::move(clips);
            target->addAnimationClipSource(source);
            addedAny = true;
        }
        if (!addedAny) {
            return NO;
        }

        if (animator) {
            auto states = animator->getStates();
            const auto& combined = skinned->getAnimationClips();
            for (int clipIndex = oldClipCount; clipIndex < static_cast<int>(combined.size()); ++clipIndex) {
                AnimatorState state;
                if (combined[static_cast<size_t>(clipIndex)]) {
                    state.name = combined[static_cast<size_t>(clipIndex)]->getName();
                }
                if (state.name.empty()) {
                    state.name = "Clip " + std::to_string(clipIndex);
                }
                state.clipIndex = clipIndex;
                state.speed = 1.0f;
                state.loop = true;
                states.push_back(state);
            }
            animator->setStates(states);
        }
        return YES;
    }];
}

- (BOOL)removeAnimationSource:(NSString *)uuid index:(NSInteger)index {
    return [self performSyncBool:^BOOL {
        Animator* animator = nullptr;
        SkinnedMeshRenderer* skinned = nullptr;
        std::vector<SkinnedMeshRenderer*> targets;
        if (!ResolveAnimatorAndSkinnedTargets(uuid.UTF8String, animator, skinned, targets) || !skinned) {
            return NO;
        }
        if (index < 0) {
            return NO;
        }
        const auto& sources = skinned->getAnimationClipSources();
        size_t sourceIndex = static_cast<size_t>(index);
        if (sourceIndex >= sources.size()) {
            return NO;
        }

        int removedStart = static_cast<int>(skinned->getBaseAnimationClipCount());
        for (size_t i = 0; i < sourceIndex; ++i) {
            removedStart += static_cast<int>(sources[i].clips.size());
        }
        int removedCount = static_cast<int>(sources[sourceIndex].clips.size());
        if (removedCount <= 0) {
            bool removedAny = false;
            for (auto* target : targets) {
                if (target) {
                    removedAny = target->removeAnimationClipSource(sourceIndex) || removedAny;
                }
            }
            return removedAny;
        }

        bool removed = false;
        for (auto* target : targets) {
            if (target) {
                removed = target->removeAnimationClipSource(sourceIndex) || removed;
            }
        }
        if (!removed) {
            return NO;
        }

        if (animator) {
            auto states = animator->getStates();
            for (auto& state : states) {
                if (state.clipIndex >= removedStart && state.clipIndex < removedStart + removedCount) {
                    state.clipIndex = -1;
                } else if (state.clipIndex >= removedStart + removedCount) {
                    state.clipIndex -= removedCount;
                }
            }
            animator->setStates(states);

            auto blendTrees = animator->getBlendTrees();
            for (auto& tree : blendTrees) {
                tree.motions.erase(std::remove_if(tree.motions.begin(), tree.motions.end(), [&](AnimatorBlendMotion& motion) {
                    if (motion.clipIndex >= removedStart && motion.clipIndex < removedStart + removedCount) {
                        return true;
                    }
                    if (motion.clipIndex >= removedStart + removedCount) {
                        motion.clipIndex -= removedCount;
                    }
                    return false;
                }), tree.motions.end());
            }
            animator->setBlendTrees(blendTrees);
        }
        return YES;
    }];
}

- (BOOL)setAnimatorParameter:(NSString *)uuid name:(NSString *)name type:(NSString *)type value:(id)value {
    return [self performSyncBool:^BOOL {
        Animator* animator = GetAnimatorByUUID(uuid.UTF8String);
        if (!animator) return NO;
        std::vector<AnimatorParameter> params = animator->getParameters();
        std::string nameStr = name ? name.UTF8String : "";
        std::string typeStr = type ? type.UTF8String : "Float";
        if (nameStr.empty()) return NO;

        auto it = std::find_if(params.begin(), params.end(), [&](const AnimatorParameter& p) {
            return p.name == nameStr;
        });
        if (it == params.end()) {
            AnimatorParameter param;
            param.name = nameStr;
            param.type = AnimatorParameterType::Float;
            if (typeStr == "Int") param.type = AnimatorParameterType::Int;
            else if (typeStr == "Bool") param.type = AnimatorParameterType::Bool;
            else if (typeStr == "Trigger") param.type = AnimatorParameterType::Trigger;
            params.push_back(param);
            it = std::prev(params.end());
        }

        if (typeStr == "Int") {
            it->type = AnimatorParameterType::Int;
            it->intValue = [value intValue];
        } else if (typeStr == "Bool") {
            it->type = AnimatorParameterType::Bool;
            it->boolValue = [value boolValue];
        } else if (typeStr == "Trigger") {
            it->type = AnimatorParameterType::Trigger;
            it->triggerValue = [value boolValue];
        } else {
            it->type = AnimatorParameterType::Float;
            it->floatValue = [value floatValue];
        }

        animator->setParameters(params);
        return YES;
    }];
}

- (BOOL)fireAnimatorTrigger:(NSString *)uuid name:(NSString *)name {
    return [self performSyncBool:^BOOL {
        Animator* animator = GetAnimatorByUUID(uuid.UTF8String);
        if (!animator || !name) return NO;
        return animator->setTrigger(name.UTF8String);
    }];
}

- (BOOL)setAnimatorRootMotion:(NSString *)uuid enabled:(BOOL)enabled applyPosition:(BOOL)applyPosition applyRotation:(BOOL)applyRotation {
    return [self performSyncBool:^BOOL {
        Animator* animator = GetAnimatorByUUID(uuid.UTF8String);
        if (animator) {
            animator->setRootMotionEnabled(enabled);
            animator->setApplyRootMotionPosition(applyPosition);
            animator->setApplyRootMotionRotation(applyRotation);
            return YES;
        }
        Scene* scene = SceneManager::getInstance().getActiveScene();
        Entity* entity = scene ? SceneCommands::getEntityByUUID(scene, uuid.UTF8String) : nullptr;
        std::vector<SkinnedMeshRenderer*> targets;
        if (entity) {
            CollectSkinnedInHierarchy(entity, nullptr, targets);
        }
        if (targets.empty()) {
            return NO;
        }
        for (auto* skinned : targets) {
            if (!skinned) continue;
            skinned->setRootMotionEnabled(enabled);
            skinned->setApplyRootMotionPosition(applyPosition);
            skinned->setApplyRootMotionRotation(applyRotation);
        }
        return YES;
    }];
}

- (NSDictionary *)getAnimatorGraph:(NSString *)uuid {
    return (NSDictionary *)[self performSyncObject:^id{
        Animator* animator = GetAnimatorByUUID(uuid.UTF8String);
        if (!animator) return @{};

        NSMutableArray* params = [NSMutableArray array];
        for (const auto& param : animator->getParameters()) {
            [params addObject:@{
                @"name": [NSString stringWithUTF8String:param.name.c_str()],
                @"type": AnimatorParamTypeToString(param.type),
                @"float": @(param.floatValue),
                @"int": @(param.intValue),
                @"bool": @(param.boolValue)
            }];
        }

        NSMutableArray* states = [NSMutableArray array];
        const auto& stateList = animator->getStates();
        for (const auto& state : stateList) {
            [states addObject:@{
                @"name": [NSString stringWithUTF8String:state.name.c_str()],
                @"type": AnimatorStateTypeToString(state.type),
                @"clipIndex": @(state.clipIndex),
                @"blendTree": @(state.blendTreeIndex),
                @"speed": @(state.speed),
                @"loop": @(state.loop)
            }];
        }

        NSMutableArray* blendTrees = [NSMutableArray array];
        const auto& treeList = animator->getBlendTrees();
        for (const auto& tree : treeList) {
            NSMutableArray* motions = [NSMutableArray array];
            for (const auto& motion : tree.motions) {
                [motions addObject:@{
                    @"clipIndex": @(motion.clipIndex),
                    @"threshold": @(motion.threshold)
                }];
            }
            [blendTrees addObject:@{
                @"name": [NSString stringWithUTF8String:tree.name.c_str()],
                @"type": AnimatorBlendTreeTypeToString(tree.type),
                @"parameter": [NSString stringWithUTF8String:tree.parameter.c_str()],
                @"motions": motions
            }];
        }

        NSMutableArray* transitions = [NSMutableArray array];
        const auto& transitionList = animator->getTransitions();
        for (const auto& transition : transitionList) {
            NSMutableArray* conditions = [NSMutableArray array];
            for (const auto& cond : transition.conditions) {
                [conditions addObject:@{
                    @"parameter": [NSString stringWithUTF8String:cond.parameter.c_str()],
                    @"op": AnimatorCondOpToString(cond.op),
                    @"threshold": @(cond.threshold),
                    @"intThreshold": @(cond.intThreshold),
                    @"boolThreshold": @(cond.boolThreshold)
                }];
            }
            [transitions addObject:@{
                @"from": @(transition.fromState),
                @"to": @(transition.toState),
                @"duration": @(transition.duration),
                @"hasExitTime": @(transition.hasExitTime),
                @"exitTime": @(transition.exitTime),
                @"fixedDuration": @(transition.fixedDuration),
                @"conditions": conditions
            }];
        }

        return @{
            @"parameters": params,
            @"states": states,
            @"blendTrees": blendTrees,
            @"transitions": transitions
        };
    }];
}

- (BOOL)setAnimatorGraph:(NSString *)uuid graph:(NSDictionary *)graph {
    return [self performSyncBool:^BOOL {
        Animator* animator = GetAnimatorByUUID(uuid.UTF8String);
        if (!animator || !graph) return NO;

        if (NSArray* params = graph[@"parameters"]) {
            std::vector<AnimatorParameter> newParams;
            for (NSDictionary* entry in params) {
                NSString* name = entry[@"name"];
                if (!name || name.length == 0) continue;
                AnimatorParameter param;
                param.name = name.UTF8String;
                param.type = AnimatorParamTypeFromString(entry[@"type"]);
                param.floatValue = [entry[@"float"] floatValue];
                param.intValue = [entry[@"int"] intValue];
                param.boolValue = [entry[@"bool"] boolValue];
                param.triggerValue = false;
                newParams.push_back(param);
            }
            animator->setParameters(newParams);
        }

        if (NSArray* trees = graph[@"blendTrees"]) {
            std::vector<AnimatorBlendTree> newTrees;
            for (NSDictionary* entry in trees) {
                AnimatorBlendTree tree;
                NSString* name = entry[@"name"];
                if (name) tree.name = name.UTF8String;
                tree.type = AnimatorBlendTreeTypeFromString(entry[@"type"]);
                NSString* param = entry[@"parameter"];
                if (param) tree.parameter = param.UTF8String;
                if (NSArray* motions = entry[@"motions"]) {
                    for (NSDictionary* motionDict in motions) {
                        AnimatorBlendMotion motion;
                        motion.clipIndex = [motionDict[@"clipIndex"] intValue];
                        motion.threshold = [motionDict[@"threshold"] floatValue];
                        tree.motions.push_back(motion);
                    }
                }
                newTrees.push_back(tree);
            }
            animator->setBlendTrees(newTrees);
        }

        if (NSArray* states = graph[@"states"]) {
            std::vector<AnimatorState> newStates;
            for (NSDictionary* entry in states) {
                AnimatorState state;
                NSString* name = entry[@"name"];
                if (name) state.name = name.UTF8String;
                state.type = AnimatorStateTypeFromString(entry[@"type"]);
                state.clipIndex = [entry[@"clipIndex"] intValue];
                state.blendTreeIndex = [entry[@"blendTree"] intValue];
                state.speed = [entry[@"speed"] floatValue];
                state.loop = [entry[@"loop"] boolValue];
                newStates.push_back(state);
            }
            animator->setStates(newStates);
        }

        if (NSArray* transitions = graph[@"transitions"]) {
            std::vector<AnimatorTransition> newTransitions;
            for (NSDictionary* entry in transitions) {
                AnimatorTransition transition;
                transition.fromState = [entry[@"from"] intValue];
                transition.toState = [entry[@"to"] intValue];
                transition.duration = [entry[@"duration"] floatValue];
                transition.hasExitTime = [entry[@"hasExitTime"] boolValue];
                transition.exitTime = [entry[@"exitTime"] floatValue];
                transition.fixedDuration = [entry[@"fixedDuration"] boolValue];
                if (NSArray* conditions = entry[@"conditions"]) {
                    for (NSDictionary* condDict in conditions) {
                        NSString* paramName = condDict[@"parameter"];
                        if (!paramName || paramName.length == 0) continue;
                        AnimatorCondition cond;
                        cond.parameter = paramName.UTF8String;
                        cond.op = AnimatorCondOpFromString(condDict[@"op"]);
                        cond.threshold = [condDict[@"threshold"] floatValue];
                        cond.intThreshold = [condDict[@"intThreshold"] intValue];
                        cond.boolThreshold = [condDict[@"boolThreshold"] boolValue];
                        transition.conditions.push_back(cond);
                    }
                }
                newTransitions.push_back(transition);
            }
            animator->setTransitions(newTransitions);
        }

        return YES;
    }];
}

- (NSArray<NSDictionary *> *)getAnimationEvents:(NSString *)uuid clipIndex:(NSInteger)clipIndex {
    return (NSArray<NSDictionary *> *)[self performSyncObject:^id{
        Animator* animator = nullptr;
        SkinnedMeshRenderer* skinned = nullptr;
        if (!ResolveAnimatorAndSkinned(uuid.UTF8String, animator, skinned) || !skinned) {
            return @[];
        }
        const auto& clips = skinned->getAnimationClips();
        if (clipIndex < 0 || clipIndex >= (NSInteger)clips.size()) return @[];
        auto clip = clips[static_cast<size_t>(clipIndex)];
        if (!clip) return @[];

        NSMutableArray* events = [NSMutableArray array];
        for (const auto& evt : clip->getEvents()) {
            [events addObject:@{
                @"time": @(evt.time),
                @"name": [NSString stringWithUTF8String:evt.name.c_str()],
                @"eventType": evt.eventType.empty() ? @"" : [NSString stringWithUTF8String:evt.eventType.c_str()],
                @"eventTag": evt.eventTag.empty() ? @"" : [NSString stringWithUTF8String:evt.eventTag.c_str()],
                @"payload": evt.payload.empty() ? @"" : [NSString stringWithUTF8String:evt.payload.c_str()],
                @"volume": @(evt.volume),
                @"pitchMin": @(evt.pitchMin),
                @"pitchMax": @(evt.pitchMax),
                @"spatial": @(evt.spatial)
            }];
        }
        return events;
    }];
}

- (BOOL)setAnimationEvents:(NSString *)uuid clipIndex:(NSInteger)clipIndex events:(NSArray<NSDictionary *> *)events {
    return [self performSyncBool:^BOOL {
        Animator* animator = nullptr;
        SkinnedMeshRenderer* skinned = nullptr;
        if (!ResolveAnimatorAndSkinned(uuid.UTF8String, animator, skinned) || !skinned) {
            return NO;
        }
        const auto& clips = skinned->getAnimationClips();
        if (clipIndex < 0 || clipIndex >= (NSInteger)clips.size()) return NO;
        auto clip = clips[static_cast<size_t>(clipIndex)];
        if (!clip) return NO;

        clip->clearEvents();
        for (NSDictionary* entry in events) {
            NSString* name = entry[@"name"];
            if (!name || name.length == 0) continue;
            AnimationEvent evt;
            evt.time = [entry[@"time"] floatValue];
            evt.name = name.UTF8String;
            NSString* eventType = entry[@"eventType"];
            NSString* eventTag = entry[@"eventTag"];
            evt.eventType = eventType ? eventType.UTF8String : "";
            evt.eventTag = eventTag ? eventTag.UTF8String : "";
            NSString* payload = entry[@"payload"];
            evt.payload = payload ? payload.UTF8String : "";
            evt.volume = entry[@"volume"] ? [entry[@"volume"] floatValue] : 1.0f;
            evt.pitchMin = entry[@"pitchMin"] ? [entry[@"pitchMin"] floatValue] : 1.0f;
            evt.pitchMax = entry[@"pitchMax"] ? [entry[@"pitchMax"] floatValue] : 1.0f;
            evt.spatial = entry[@"spatial"] ? [entry[@"spatial"] boolValue] : YES;
            if (evt.eventTag.empty() && !evt.name.empty()) {
                evt.eventTag = evt.name;
            }
            if (evt.eventType.empty() && !evt.payload.empty()) {
                evt.eventType = "audio";
            }
            clip->addEvent(evt);
        }
        return YES;
    }];
}

- (NSArray<NSDictionary *> *)pollAnimatorEvents:(NSString *)uuid {
    return (NSArray<NSDictionary *> *)[self performSyncObject:^id{
        Animator* animator = GetAnimatorByUUID(uuid.UTF8String);
        if (!animator) return @[];
        NSMutableArray* events = [NSMutableArray array];
        for (const auto& evt : animator->getFiredEvents()) {
            [events addObject:@{
                @"time": @(evt.time),
                @"name": [NSString stringWithUTF8String:evt.name.c_str()],
                @"eventType": evt.eventType.empty() ? @"" : [NSString stringWithUTF8String:evt.eventType.c_str()],
                @"eventTag": evt.eventTag.empty() ? @"" : [NSString stringWithUTF8String:evt.eventTag.c_str()],
                @"payload": evt.payload.empty() ? @"" : [NSString stringWithUTF8String:evt.payload.c_str()],
                @"volume": @(evt.volume),
                @"pitchMin": @(evt.pitchMin),
                @"pitchMax": @(evt.pitchMax),
                @"spatial": @(evt.spatial)
            }];
        }
        animator->clearFiredEvents();
        return events;
    }];
}

- (NSArray<NSDictionary *> *)getAnimationClipsInfo:(NSString *)uuid {
    return (NSArray<NSDictionary *> *)[self performSyncObject:^id{
        Animator* animator = nullptr;
        SkinnedMeshRenderer* skinned = nullptr;
        if (!ResolveAnimatorAndSkinned(uuid.UTF8String, animator, skinned) || !skinned) {
            return @[];
        }
        NSMutableArray* clips = [NSMutableArray array];
        const auto& list = skinned->getAnimationClips();
        for (size_t i = 0; i < list.size(); ++i) {
            std::string name = list[i] ? list[i]->getName() : "";
            if (name.empty()) {
                name = "Clip " + std::to_string(i);
            }
            float duration = list[i] ? list[i]->getDurationSeconds() : 0.0f;
            [clips addObject:@{
                @"index": @(static_cast<int>(i)),
                @"name": [NSString stringWithUTF8String:name.c_str()],
                @"duration": @(duration)
            }];
        }
        return clips;
    }];
}

- (NSDictionary *)getSkeletonInfo:(NSString *)uuid {
    return (NSDictionary *)[self performSyncObject:^id{
        Animator* animator = nullptr;
        SkinnedMeshRenderer* skinned = nullptr;
        if (!ResolveAnimatorAndSkinned(uuid.UTF8String, animator, skinned) || !skinned) {
            return @{};
        }
        auto skeleton = skinned->getSkeleton();
        if (!skeleton) {
            return @{};
        }
        NSMutableArray* bones = [NSMutableArray array];
        const auto& list = skeleton->getBones();
        for (size_t i = 0; i < list.size(); ++i) {
            const auto& bone = list[i];
            [bones addObject:@{
                @"index": @(static_cast<int>(i)),
                @"name": [NSString stringWithUTF8String:bone.name.c_str()],
                @"parent": @(bone.parentIndex)
            }];
        }
        return @{
            @"rootIndex": @(static_cast<int>(skeleton->getRootIndex())),
            @"bones": bones
        };
    }];
}

- (NSDictionary *)getIKConstraintInfo:(NSString *)uuid {
    return (NSDictionary *)[self performSyncObject:^id{
        IKConstraint* ik = GetIKByUUID(uuid.UTF8String);
        if (!ik) return @{};
        Math::Vector3 target = ik->getTargetPosition();
        return @{
            @"root": [NSString stringWithUTF8String:ik->getRootBone().c_str()],
            @"mid": [NSString stringWithUTF8String:ik->getMidBone().c_str()],
            @"end": [NSString stringWithUTF8String:ik->getEndBone().c_str()],
            @"target": @[@(target.x), @(target.y), @(target.z)],
            @"targetEntityUUID": [NSString stringWithUTF8String:ik->getTargetEntityUUID().c_str()],
            @"targetOffset": @[@(ik->getTargetOffset().x), @(ik->getTargetOffset().y), @(ik->getTargetOffset().z)],
            @"world": @(ik->getTargetInWorld()),
            @"weight": @(ik->getWeight())
        };
    }];
}

- (BOOL)setIKConstraintInfo:(NSString *)uuid info:(NSDictionary *)info {
    return [self performSyncBool:^BOOL {
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene || !info) return NO;
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return NO;

        IKConstraint* ik = entity->getComponent<IKConstraint>();
        if (!ik) {
            ik = entity->addComponent<IKConstraint>();
        }

        if (NSString* root = info[@"root"]) {
            ik->setRootBone(root.UTF8String);
        }
        if (NSString* mid = info[@"mid"]) {
            ik->setMidBone(mid.UTF8String);
        }
        if (NSString* end = info[@"end"]) {
            ik->setEndBone(end.UTF8String);
        }
        if (NSArray* target = info[@"target"]) {
            if (target.count >= 3) {
                Math::Vector3 pos([target[0] floatValue],
                                  [target[1] floatValue],
                                  [target[2] floatValue]);
                ik->setTargetPosition(pos);
            }
        }
        if (NSString* targetEntityUUID = info[@"targetEntityUUID"]) {
            ik->setTargetEntityUUID(targetEntityUUID.UTF8String);
        }
        if (NSArray* targetOffset = info[@"targetOffset"]) {
            if (targetOffset.count >= 3) {
                Math::Vector3 pos([targetOffset[0] floatValue],
                                  [targetOffset[1] floatValue],
                                  [targetOffset[2] floatValue]);
                ik->setTargetOffset(pos);
            }
        }
        if (NSNumber* world = info[@"world"]) {
            ik->setTargetInWorld(world.boolValue);
        }
        if (NSNumber* weight = info[@"weight"]) {
            ik->setWeight(weight.floatValue);
        }
        return YES;
    }];
}

// MARK: - Physics

static NSString* RigidbodyTypeToString(RigidbodyType type) {
    switch (type) {
    case RigidbodyType::Static: return @"Static";
    case RigidbodyType::Kinematic: return @"Kinematic";
    case RigidbodyType::Dynamic:
    default:
        return @"Dynamic";
    }
}

static RigidbodyType RigidbodyTypeFromString(NSString* value) {
    if (!value) return RigidbodyType::Dynamic;
    if ([value isEqualToString:@"Static"]) return RigidbodyType::Static;
    if ([value isEqualToString:@"Kinematic"]) return RigidbodyType::Kinematic;
    return RigidbodyType::Dynamic;
}

static NSString* ColliderShapeToString(PhysicsCollider::ShapeType type) {
    switch (type) {
    case PhysicsCollider::ShapeType::Sphere: return @"Sphere";
    case PhysicsCollider::ShapeType::Capsule: return @"Capsule";
    case PhysicsCollider::ShapeType::Mesh: return @"Mesh";
    case PhysicsCollider::ShapeType::Box:
    default:
        return @"Box";
    }
}

static PhysicsCollider::ShapeType ColliderShapeFromString(NSString* value) {
    if (!value) return PhysicsCollider::ShapeType::Box;
    if ([value isEqualToString:@"Sphere"]) return PhysicsCollider::ShapeType::Sphere;
    if ([value isEqualToString:@"Capsule"]) return PhysicsCollider::ShapeType::Capsule;
    if ([value isEqualToString:@"Mesh"]) return PhysicsCollider::ShapeType::Mesh;
    return PhysicsCollider::ShapeType::Box;
}

static NSString* CombineModeToString(PhysicsCollider::CombineMode mode) {
    switch (mode) {
    case PhysicsCollider::CombineMode::Min: return @"Min";
    case PhysicsCollider::CombineMode::Multiply: return @"Multiply";
    case PhysicsCollider::CombineMode::Max: return @"Max";
    case PhysicsCollider::CombineMode::Average:
    default:
        return @"Average";
    }
}

static PhysicsCollider::CombineMode CombineModeFromString(NSString* value) {
    if (!value) return PhysicsCollider::CombineMode::Average;
    if ([value isEqualToString:@"Min"]) return PhysicsCollider::CombineMode::Min;
    if ([value isEqualToString:@"Multiply"]) return PhysicsCollider::CombineMode::Multiply;
    if ([value isEqualToString:@"Max"]) return PhysicsCollider::CombineMode::Max;
    return PhysicsCollider::CombineMode::Average;
}

- (NSDictionary *)getRigidbodyInfo:(NSString *)uuid {
    return (NSDictionary *)[self performSyncObject:^id{
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return @{};
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return @{};
        Rigidbody* rb = entity->getComponent<Rigidbody>();
        if (!rb) return @{};
        return @{
            @"type": RigidbodyTypeToString(rb->getType()),
            @"mass": @(rb->getMass()),
            @"linearDamping": @(rb->getLinearDamping()),
            @"angularDamping": @(rb->getAngularDamping()),
            @"useGravity": @(rb->getUseGravity()),
            @"continuous": @(rb->getContinuousCollision()),
            @"allowSleep": @(rb->getAllowSleep())
        };
    }];
}

- (BOOL)setRigidbodyInfo:(NSString *)uuid info:(NSDictionary *)info {
    return [self performSyncBool:^BOOL {
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene || !info) return NO;
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return NO;
        Rigidbody* rb = entity->getComponent<Rigidbody>();
        if (!rb) {
            rb = entity->addComponent<Rigidbody>();
        }

        if (NSString* type = info[@"type"]) {
            rb->setType(RigidbodyTypeFromString(type));
        }
        if (NSNumber* mass = info[@"mass"]) {
            rb->setMass(mass.floatValue);
        }
        if (NSNumber* linear = info[@"linearDamping"]) {
            rb->setLinearDamping(linear.floatValue);
        }
        if (NSNumber* angular = info[@"angularDamping"]) {
            rb->setAngularDamping(angular.floatValue);
        }
        if (NSNumber* gravity = info[@"useGravity"]) {
            rb->setUseGravity(gravity.boolValue);
        }
        if (NSNumber* continuous = info[@"continuous"]) {
            rb->setContinuousCollision(continuous.boolValue);
        }
        if (NSNumber* allowSleep = info[@"allowSleep"]) {
            rb->setAllowSleep(allowSleep.boolValue);
        }
        return YES;
    }];
}

- (BOOL)addRigidbody:(NSString *)uuid {
    return [self performSyncBool:^BOOL {
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return NO;
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return NO;
        if (!entity->getComponent<Rigidbody>()) {
            entity->addComponent<Rigidbody>();
        }
        return YES;
    }];
}

- (void)removeRigidbody:(NSString *)uuid {
    [self performAsync:^{
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return;
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return;
        entity->removeComponent<Rigidbody>();
    }];
}

- (NSDictionary *)getColliderInfo:(NSString *)uuid {
    return (NSDictionary *)[self performSyncObject:^id{
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return @{};
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return @{};
        PhysicsCollider* collider = entity->getComponent<PhysicsCollider>();
        if (!collider) return @{};
        Math::Vector3 size = collider->getSize();
        Math::Vector3 center = collider->getCenter();
        return @{
            @"shape": ColliderShapeToString(collider->getShapeType()),
            @"size": @[@(size.x), @(size.y), @(size.z)],
            @"radius": @(collider->getRadius()),
            @"height": @(collider->getHeight()),
            @"center": @[@(center.x), @(center.y), @(center.z)],
            @"trigger": @(collider->isTrigger()),
            @"friction": @(collider->getFriction()),
            @"restitution": @(collider->getRestitution()),
            @"frictionCombine": CombineModeToString(collider->getFrictionCombine()),
            @"restitutionCombine": CombineModeToString(collider->getRestitutionCombine()),
            @"layer": @(collider->getCollisionLayer()),
            @"mask": @(collider->getCollisionMask())
        };
    }];
}

- (BOOL)setColliderInfo:(NSString *)uuid info:(NSDictionary *)info {
    return [self performSyncBool:^BOOL {
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene || !info) return NO;
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return NO;
        PhysicsCollider* collider = entity->getComponent<PhysicsCollider>();
        if (!collider) {
            collider = entity->addComponent<PhysicsCollider>();
        }

        if (NSString* shape = info[@"shape"]) {
            collider->setShapeType(ColliderShapeFromString(shape));
        }
        if (NSArray* size = info[@"size"]) {
            if (size.count >= 3) {
                Math::Vector3 v([size[0] floatValue], [size[1] floatValue], [size[2] floatValue]);
                collider->setSize(v);
            }
        }
        if (NSNumber* radius = info[@"radius"]) {
            collider->setRadius(radius.floatValue);
        }
        if (NSNumber* height = info[@"height"]) {
            collider->setHeight(height.floatValue);
        }
        if (NSArray* center = info[@"center"]) {
            if (center.count >= 3) {
                Math::Vector3 v([center[0] floatValue], [center[1] floatValue], [center[2] floatValue]);
                collider->setCenter(v);
            }
        }
        if (NSNumber* trigger = info[@"trigger"]) {
            collider->setTrigger(trigger.boolValue);
        }
        if (NSNumber* friction = info[@"friction"]) {
            collider->setFriction(friction.floatValue);
        }
        if (NSNumber* restitution = info[@"restitution"]) {
            collider->setRestitution(restitution.floatValue);
        }
        if (NSString* frictionCombine = info[@"frictionCombine"]) {
            collider->setFrictionCombine(CombineModeFromString(frictionCombine));
        }
        if (NSString* restitutionCombine = info[@"restitutionCombine"]) {
            collider->setRestitutionCombine(CombineModeFromString(restitutionCombine));
        }
        if (NSNumber* layer = info[@"layer"]) {
            collider->setCollisionLayer(layer.unsignedIntValue);
        }
        if (NSNumber* mask = info[@"mask"]) {
            collider->setCollisionMask(mask.unsignedIntValue);
        }
        return YES;
    }];
}

- (BOOL)addCollider:(NSString *)uuid {
    return [self performSyncBool:^BOOL {
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return NO;
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return NO;
        if (!entity->getComponent<PhysicsCollider>()) {
            entity->addComponent<PhysicsCollider>();
        }
        return YES;
    }];
}

- (void)removeCollider:(NSString *)uuid {
    [self performAsync:^{
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return;
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return;
        entity->removeComponent<PhysicsCollider>();
    }];
}

- (NSDictionary *)getHealthInfo:(NSString *)uuid {
    return (NSDictionary *)[self performSyncObject:^id{
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return @{};
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return @{};
        Health* health = entity->getComponent<Health>();
        if (!health) return @{};
        return @{
            @"max": @(health->getMaxHealth()),
            @"current": @(health->getCurrentHealth()),
            @"destroyOnDeath": @(health->getDestroyOnDeath())
        };
    }];
}

- (BOOL)setHealthInfo:(NSString *)uuid info:(NSDictionary *)info {
    return [self performSyncBool:^BOOL {
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene || !info) return NO;
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return NO;
        Health* health = entity->getComponent<Health>();
        if (!health) {
            health = entity->addComponent<Health>();
        }

        if (NSNumber* value = info[@"max"]) {
            health->setMaxHealth(value.floatValue);
        }
        if (NSNumber* value = info[@"current"]) {
            health->setCurrentHealth(value.floatValue);
        }
        if (NSNumber* value = info[@"destroyOnDeath"]) {
            health->setDestroyOnDeath(value.boolValue);
        }
        return YES;
    }];
}

- (BOOL)addHealth:(NSString *)uuid {
    return [self performSyncBool:^BOOL {
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return NO;
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return NO;
        if (!entity->getComponent<Health>()) {
            entity->addComponent<Health>();
        }
        return YES;
    }];
}

- (void)removeHealth:(NSString *)uuid {
    [self performAsync:^{
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return;
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return;
        entity->removeComponent<Health>();
    }];
}

- (NSDictionary *)getCharacterControllerInfo:(NSString *)uuid {
    return (NSDictionary *)[self performSyncObject:^id{
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return @{};
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return @{};
        CharacterController* controller = entity->getComponent<CharacterController>();
        if (!controller) return @{};
        return @{
            @"radius": @(controller->getRadius()),
            @"height": @(controller->getHeight()),
            @"skinWidth": @(controller->getSkinWidth()),
            @"moveSpeed": @(controller->getMoveSpeed()),
            @"acceleration": @(controller->getAcceleration()),
            @"airAcceleration": @(controller->getAirAcceleration()),
            @"jumpSpeed": @(controller->getJumpSpeed()),
            @"gravity": @(controller->getGravity()),
            @"maxFallSpeed": @(controller->getMaxFallSpeed()),
            @"groundSnapSpeed": @(controller->getGroundSnapSpeed()),
            @"stepOffset": @(controller->getStepOffset()),
            @"slopeLimit": @(controller->getSlopeLimit()),
            @"slopeSlideSpeed": @(controller->getSlopeSlideSpeed()),
            @"groundCheckDistance": @(controller->getGroundCheckDistance()),
            @"useInput": @(controller->getUseInput()),
            @"useGravity": @(controller->getUseGravity()),
            @"enableStep": @(controller->getEnableStep()),
            @"enableSlopeLimit": @(controller->getEnableSlopeLimit()),
            @"snapToGround": @(controller->getSnapToGround()),
            @"collisionMask": @(controller->getCollisionMask())
        };
    }];
}

- (BOOL)setCharacterControllerInfo:(NSString *)uuid info:(NSDictionary *)info {
    return [self performSyncBool:^BOOL {
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene || !info) return NO;
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return NO;
        CharacterController* controller = entity->getComponent<CharacterController>();
        if (!controller) {
            controller = entity->addComponent<CharacterController>();
        }

        if (NSNumber* radius = info[@"radius"]) {
            controller->setRadius(radius.floatValue);
        }
        if (NSNumber* height = info[@"height"]) {
            controller->setHeight(height.floatValue);
        }
        if (NSNumber* skinWidth = info[@"skinWidth"]) {
            controller->setSkinWidth(skinWidth.floatValue);
        }
        if (NSNumber* moveSpeed = info[@"moveSpeed"]) {
            controller->setMoveSpeed(moveSpeed.floatValue);
        }
        if (NSNumber* acceleration = info[@"acceleration"]) {
            controller->setAcceleration(acceleration.floatValue);
        }
        if (NSNumber* airAcceleration = info[@"airAcceleration"]) {
            controller->setAirAcceleration(airAcceleration.floatValue);
        }
        if (NSNumber* jumpSpeed = info[@"jumpSpeed"]) {
            controller->setJumpSpeed(jumpSpeed.floatValue);
        }
        if (NSNumber* gravity = info[@"gravity"]) {
            controller->setGravity(gravity.floatValue);
        }
        if (NSNumber* maxFallSpeed = info[@"maxFallSpeed"]) {
            controller->setMaxFallSpeed(maxFallSpeed.floatValue);
        }
        if (NSNumber* groundSnapSpeed = info[@"groundSnapSpeed"]) {
            controller->setGroundSnapSpeed(groundSnapSpeed.floatValue);
        }
        if (NSNumber* stepOffset = info[@"stepOffset"]) {
            controller->setStepOffset(stepOffset.floatValue);
        }
        if (NSNumber* slopeLimit = info[@"slopeLimit"]) {
            controller->setSlopeLimit(slopeLimit.floatValue);
        }
        if (NSNumber* slopeSlideSpeed = info[@"slopeSlideSpeed"]) {
            controller->setSlopeSlideSpeed(slopeSlideSpeed.floatValue);
        }
        if (NSNumber* groundCheckDistance = info[@"groundCheckDistance"]) {
            controller->setGroundCheckDistance(groundCheckDistance.floatValue);
        }
        if (NSNumber* useInput = info[@"useInput"]) {
            controller->setUseInput(useInput.boolValue);
        }
        if (NSNumber* useGravity = info[@"useGravity"]) {
            controller->setUseGravity(useGravity.boolValue);
        }
        if (NSNumber* enableStep = info[@"enableStep"]) {
            controller->setEnableStep(enableStep.boolValue);
        }
        if (NSNumber* enableSlopeLimit = info[@"enableSlopeLimit"]) {
            controller->setEnableSlopeLimit(enableSlopeLimit.boolValue);
        }
        if (NSNumber* snapToGround = info[@"snapToGround"]) {
            controller->setSnapToGround(snapToGround.boolValue);
        }
        if (NSNumber* collisionMask = info[@"collisionMask"]) {
            controller->setCollisionMask(collisionMask.unsignedIntValue);
        }
        return YES;
    }];
}

- (BOOL)addCharacterController:(NSString *)uuid {
    return [self performSyncBool:^BOOL {
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return NO;
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return NO;
        if (!entity->getComponent<CharacterController>()) {
            entity->addComponent<CharacterController>();
        }
        return YES;
    }];
}

- (void)removeCharacterController:(NSString *)uuid {
    [self performAsync:^{
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return;
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return;
        entity->removeComponent<CharacterController>();
    }];
}

- (NSDictionary *)getFirstPersonControllerInfo:(NSString *)uuid {
    return (NSDictionary *)[self performSyncObject:^id{
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return @{};
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return @{};
        FirstPersonController* controller = entity->getComponent<FirstPersonController>();
        if (!controller) return @{};
        return @{
            @"mouseSensitivity": @(controller->getMouseSensitivity()),
            @"invertY": @(controller->getInvertY()),
            @"requireLookButton": @(controller->getRequireLookButton()),
            @"lookButton": @((int)controller->getLookButton()),
            @"minPitch": @(controller->getMinPitch()),
            @"maxPitch": @(controller->getMaxPitch()),
            @"walkSpeed": @(controller->getWalkSpeed()),
            @"sprintMultiplier": @(controller->getSprintMultiplier()),
            @"enableSprint": @(controller->getEnableSprint()),
            @"enableCrouch": @(controller->getEnableCrouch()),
            @"crouchHeight": @(controller->getCrouchHeight()),
            @"crouchEyeHeight": @(controller->getCrouchEyeHeight()),
            @"crouchSpeed": @(controller->getCrouchSpeed()),
            @"eyeHeight": @(controller->getEyeHeight()),
            @"useEyeHeight": @(controller->getUseEyeHeight()),
            @"driveCharacterController": @(controller->getDriveCharacterController()),
            @"fireCooldown": @(controller->getFireCooldown()),
            @"fireDamage": @(controller->getFireDamage()),
            @"fireRange": @(controller->getFireRange()),
            @"fireMask": @(controller->getFireHitMask()),
            @"fireHitTriggers": @(controller->getFireHitTriggers()),
            @"muzzleTexture": controller->getMuzzleTexturePath().empty()
                ? @""
                : [NSString stringWithUTF8String: controller->getMuzzleTexturePath().c_str()]
        };
    }];
}

- (BOOL)setFirstPersonControllerInfo:(NSString *)uuid info:(NSDictionary *)info {
    return [self performSyncBool:^BOOL {
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene || !info) return NO;
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return NO;
        FirstPersonController* controller = entity->getComponent<FirstPersonController>();
        if (!controller) {
            controller = entity->addComponent<FirstPersonController>();
        }

        if (NSNumber* value = info[@"mouseSensitivity"]) {
            controller->setMouseSensitivity(value.floatValue);
        }
        if (NSNumber* value = info[@"invertY"]) {
            controller->setInvertY(value.boolValue);
        }
        if (NSNumber* value = info[@"requireLookButton"]) {
            controller->setRequireLookButton(value.boolValue);
        }
        if (NSNumber* value = info[@"lookButton"]) {
            controller->setLookButton(static_cast<MouseButton>(value.intValue));
        }
        if (NSNumber* value = info[@"minPitch"]) {
            controller->setMinPitch(value.floatValue);
        }
        if (NSNumber* value = info[@"maxPitch"]) {
            controller->setMaxPitch(value.floatValue);
        }
        if (NSNumber* value = info[@"walkSpeed"]) {
            controller->setWalkSpeed(value.floatValue);
        }
        if (NSNumber* value = info[@"sprintMultiplier"]) {
            controller->setSprintMultiplier(value.floatValue);
        }
        if (NSNumber* value = info[@"enableSprint"]) {
            controller->setEnableSprint(value.boolValue);
        }
        if (NSNumber* value = info[@"enableCrouch"]) {
            controller->setEnableCrouch(value.boolValue);
        }
        if (NSNumber* value = info[@"crouchHeight"]) {
            controller->setCrouchHeight(value.floatValue);
        }
        if (NSNumber* value = info[@"crouchEyeHeight"]) {
            controller->setCrouchEyeHeight(value.floatValue);
        }
        if (NSNumber* value = info[@"crouchSpeed"]) {
            controller->setCrouchSpeed(value.floatValue);
        }
        if (NSNumber* value = info[@"eyeHeight"]) {
            controller->setEyeHeight(value.floatValue);
        }
        if (NSNumber* value = info[@"useEyeHeight"]) {
            controller->setUseEyeHeight(value.boolValue);
        }
        if (NSNumber* value = info[@"driveCharacterController"]) {
            controller->setDriveCharacterController(value.boolValue);
        }
        if (NSNumber* value = info[@"fireCooldown"]) {
            controller->setFireCooldown(value.floatValue);
        }
        if (NSNumber* value = info[@"fireDamage"]) {
            controller->setFireDamage(value.floatValue);
        }
        if (NSNumber* value = info[@"fireRange"]) {
            controller->setFireRange(value.floatValue);
        }
        if (NSNumber* value = info[@"fireMask"]) {
            controller->setFireHitMask(value.unsignedIntValue);
        }
        if (NSNumber* value = info[@"fireHitTriggers"]) {
            controller->setFireHitTriggers(value.boolValue);
        }
        if (NSString* value = info[@"muzzleTexture"]) {
            controller->setMuzzleTexturePath(value.UTF8String);
        }
        return YES;
    }];
}

- (BOOL)addFirstPersonController:(NSString *)uuid {
    return [self performSyncBool:^BOOL {
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return NO;
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return NO;
        if (!entity->getComponent<FirstPersonController>()) {
            entity->addComponent<FirstPersonController>();
        }
        return YES;
    }];
}

- (void)removeFirstPersonController:(NSString *)uuid {
    [self performAsync:^{
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return;
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return;
        entity->removeComponent<FirstPersonController>();
    }];
}

- (NSDictionary *)getThirdPersonControllerInfo:(NSString *)uuid {
    return (NSDictionary *)[self performSyncObject:^id{
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return @{};
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return @{};
        ThirdPersonController* controller = entity->getComponent<ThirdPersonController>();
        if (!controller) return @{};
        return @{
            @"mouseSensitivity": @(controller->getMouseSensitivity()),
            @"invertY": @(controller->getInvertY()),
            @"requireLookButton": @(controller->getRequireLookButton()),
            @"lookButton": @((int)controller->getLookButton()),
            @"minPitch": @(controller->getMinPitch()),
            @"maxPitch": @(controller->getMaxPitch()),
            @"pivotHeight": @(controller->getPivotHeight()),
            @"lookAhead": @(controller->getLookAhead()),
            @"shoulderOffset": @(controller->getShoulderOffset()),
            @"cameraDistance": @(controller->getCameraDistance()),
            @"minDistance": @(controller->getMinDistance()),
            @"maxDistance": @(controller->getMaxDistance()),
            @"zoomSpeed": @(controller->getZoomSpeed()),
            @"cameraCollisionRadius": @(controller->getCameraCollisionRadius()),
            @"positionSmoothSpeed": @(controller->getPositionSmoothSpeed()),
            @"rotationSmoothSpeed": @(controller->getRotationSmoothSpeed()),
            @"cameraSmoothSpeed": @(controller->getCameraSmoothSpeed()),
            @"walkSpeed": @(controller->getWalkSpeed()),
            @"runSpeed": @(controller->getRunSpeed()),
            @"sprintSpeed": @(controller->getSprintSpeed()),
            @"enableSprint": @(controller->getEnableSprint()),
            @"driveCharacterController": @(controller->getDriveCharacterController()),
            @"weaponGripPositionOffset": @[
                @(controller->getWeaponGripPositionOffset().x),
                @(controller->getWeaponGripPositionOffset().y),
                @(controller->getWeaponGripPositionOffset().z)
            ],
            @"weaponGripRotationOffset": @[
                @(controller->getWeaponGripRotationOffsetDegrees().x),
                @(controller->getWeaponGripRotationOffsetDegrees().y),
                @(controller->getWeaponGripRotationOffsetDegrees().z)
            ],
            @"weaponSupportHandOffset": @[
                @(controller->getWeaponSupportHandOffset().x),
                @(controller->getWeaponSupportHandOffset().y),
                @(controller->getWeaponSupportHandOffset().z)
            ]
        };
    }];
}

- (BOOL)setThirdPersonControllerInfo:(NSString *)uuid info:(NSDictionary *)info {
    return [self performSyncBool:^BOOL {
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene || !info) return NO;
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return NO;
        ThirdPersonController* controller = entity->getComponent<ThirdPersonController>();
        if (!controller) {
            controller = entity->addComponent<ThirdPersonController>();
        }
        if (!entity->getComponent<CharacterController>()) {
            entity->addComponent<CharacterController>();
        }

        if (NSNumber* value = info[@"mouseSensitivity"]) {
            controller->setMouseSensitivity(value.floatValue);
        }
        if (NSNumber* value = info[@"invertY"]) {
            controller->setInvertY(value.boolValue);
        }
        if (NSNumber* value = info[@"requireLookButton"]) {
            controller->setRequireLookButton(value.boolValue);
        }
        if (NSNumber* value = info[@"lookButton"]) {
            controller->setLookButton(static_cast<MouseButton>(value.intValue));
        }
        if (NSNumber* value = info[@"minPitch"]) {
            controller->setMinPitch(value.floatValue);
        }
        if (NSNumber* value = info[@"maxPitch"]) {
            controller->setMaxPitch(value.floatValue);
        }
        if (NSNumber* value = info[@"pivotHeight"]) {
            controller->setPivotHeight(value.floatValue);
        }
        if (NSNumber* value = info[@"lookAhead"]) {
            controller->setLookAhead(value.floatValue);
        }
        if (NSNumber* value = info[@"shoulderOffset"]) {
            controller->setShoulderOffset(value.floatValue);
        }
        if (NSNumber* value = info[@"cameraDistance"]) {
            controller->setCameraDistance(value.floatValue);
        }
        if (NSNumber* value = info[@"minDistance"]) {
            controller->setMinDistance(value.floatValue);
        }
        if (NSNumber* value = info[@"maxDistance"]) {
            controller->setMaxDistance(value.floatValue);
        }
        if (NSNumber* value = info[@"zoomSpeed"]) {
            controller->setZoomSpeed(value.floatValue);
        }
        if (NSNumber* value = info[@"cameraCollisionRadius"]) {
            controller->setCameraCollisionRadius(value.floatValue);
        }
        if (NSNumber* value = info[@"positionSmoothSpeed"]) {
            controller->setPositionSmoothSpeed(value.floatValue);
        }
        if (NSNumber* value = info[@"rotationSmoothSpeed"]) {
            controller->setRotationSmoothSpeed(value.floatValue);
        }
        if (NSNumber* value = info[@"cameraSmoothSpeed"]) {
            controller->setCameraSmoothSpeed(value.floatValue);
        }
        if (NSNumber* value = info[@"walkSpeed"]) {
            controller->setWalkSpeed(value.floatValue);
        }
        if (NSNumber* value = info[@"runSpeed"]) {
            controller->setRunSpeed(value.floatValue);
        }
        if (NSNumber* value = info[@"sprintSpeed"]) {
            controller->setSprintSpeed(value.floatValue);
        }
        if (NSNumber* value = info[@"enableSprint"]) {
            controller->setEnableSprint(value.boolValue);
        }
        if (NSNumber* value = info[@"driveCharacterController"]) {
            controller->setDriveCharacterController(value.boolValue);
        }
        if (NSArray* values = info[@"weaponGripPositionOffset"]; values.count >= 3) {
            controller->setWeaponGripPositionOffset(Math::Vector3(
                [values[0] floatValue],
                [values[1] floatValue],
                [values[2] floatValue]
            ));
        }
        if (NSArray* values = info[@"weaponGripRotationOffset"]; values.count >= 3) {
            controller->setWeaponGripRotationOffsetDegrees(Math::Vector3(
                [values[0] floatValue],
                [values[1] floatValue],
                [values[2] floatValue]
            ));
        }
        if (NSArray* values = info[@"weaponSupportHandOffset"]; values.count >= 3) {
            controller->setWeaponSupportHandOffset(Math::Vector3(
                [values[0] floatValue],
                [values[1] floatValue],
                [values[2] floatValue]
            ));
        }
        return YES;
    }];
}

- (BOOL)addThirdPersonController:(NSString *)uuid {
    return [self performSyncBool:^BOOL {
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return NO;
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return NO;
        if (!entity->getComponent<CharacterController>()) {
            entity->addComponent<CharacterController>();
        }
        if (!entity->getComponent<ThirdPersonController>()) {
            entity->addComponent<ThirdPersonController>();
        }
        return YES;
    }];
}

- (void)removeThirdPersonController:(NSString *)uuid {
    [self performAsync:^{
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return;
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return;
        entity->removeComponent<ThirdPersonController>();
    }];
}

- (NSDictionary *)getBoneAttachmentInfo:(NSString *)uuid {
    return (NSDictionary *)[self performSyncObject:^id{
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return @{};
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return @{};
        BoneAttachment* attachment = entity->getComponent<BoneAttachment>();
        if (!attachment) return @{};
        const Math::Vector3& pos = attachment->getPositionOffset();
        const Math::Vector3& rot = attachment->getRotationOffsetDegrees();
        const Math::Vector3& scale = attachment->getScaleOffset();
        std::string sourceName = attachment->getResolvedSourceEntityName();
        return @{
            @"boneName": attachment->getBoneName().empty() ? @"" : [NSString stringWithUTF8String:attachment->getBoneName().c_str()],
            @"sourceEntityUUID": attachment->getSourceEntityUUID().empty() ? @"" : [NSString stringWithUTF8String:attachment->getSourceEntityUUID().c_str()],
            @"positionOffset": @[@(pos.x), @(pos.y), @(pos.z)],
            @"rotationOffset": @[@(rot.x), @(rot.y), @(rot.z)],
            @"scaleOffset": @[@(scale.x), @(scale.y), @(scale.z)],
            @"inheritBoneScale": @(attachment->getInheritBoneScale()),
            @"resolvedSource": sourceName.empty() ? @"" : [NSString stringWithUTF8String:sourceName.c_str()]
        };
    }];
}

- (BOOL)setBoneAttachmentInfo:(NSString *)uuid info:(NSDictionary *)info {
    return [self performSyncBool:^BOOL {
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene || !info) return NO;
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return NO;
        BoneAttachment* attachment = entity->getComponent<BoneAttachment>();
        if (!attachment) {
            attachment = entity->addComponent<BoneAttachment>();
        }
        if (NSString* boneName = info[@"boneName"]) {
            attachment->setBoneName(boneName.UTF8String);
        }
        if (NSString* sourceEntityUUID = info[@"sourceEntityUUID"]) {
            attachment->setSourceEntityUUID(sourceEntityUUID.UTF8String);
        }
        if (NSArray* position = info[@"positionOffset"]) {
            if (position.count >= 3) {
                attachment->setPositionOffset(Math::Vector3([position[0] floatValue], [position[1] floatValue], [position[2] floatValue]));
            }
        }
        if (NSArray* rotation = info[@"rotationOffset"]) {
            if (rotation.count >= 3) {
                attachment->setRotationOffsetDegrees(Math::Vector3([rotation[0] floatValue], [rotation[1] floatValue], [rotation[2] floatValue]));
            }
        }
        if (NSArray* scale = info[@"scaleOffset"]) {
            if (scale.count >= 3) {
                attachment->setScaleOffset(Math::Vector3([scale[0] floatValue], [scale[1] floatValue], [scale[2] floatValue]));
            }
        }
        if (NSNumber* inheritBoneScale = info[@"inheritBoneScale"]) {
            attachment->setInheritBoneScale(inheritBoneScale.boolValue);
        }
        return YES;
    }];
}

- (BOOL)addBoneAttachment:(NSString *)uuid {
    return [self performSyncBool:^BOOL {
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return NO;
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return NO;
        if (!entity->getComponent<BoneAttachment>()) {
            entity->addComponent<BoneAttachment>();
        }
        return YES;
    }];
}

- (void)removeBoneAttachment:(NSString *)uuid {
    [self performAsync:^{
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return;
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return;
        entity->removeComponent<BoneAttachment>();
    }];
}

- (NSNumber *)getFireEventCounter {
    return (NSNumber *)[self performSyncObject:^id{
        return @(FirstPersonController::getFireEventCounter());
    }];
}

- (NSDictionary *)getAudioSourceInfo:(NSString *)uuid {
    return (NSDictionary *)[self performSyncObject:^id{
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return @{};
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return @{};
        AudioSource* audio = entity->getComponent<AudioSource>();
        if (!audio) return @{};
        NSString* path = audio->getFilePath().empty() ? @"" : [NSString stringWithUTF8String: audio->getFilePath().c_str()];
        return @{
            @"filePath": path,
            @"volume": @(audio->getVolume()),
            @"pitch": @(audio->getPitch()),
            @"looping": @(audio->isLooping()),
            @"playOnStart": @(audio->getPlayOnStart()),
            @"spatial": @(audio->isSpatial()),
            @"bus": [NSString stringWithUTF8String: AudioSystem::audioBusToString(audio->getBus())],
            @"stream": @(audio->isStreaming()),
            @"minDistance": @(audio->getMinDistance()),
            @"maxDistance": @(audio->getMaxDistance()),
            @"rolloff": @(audio->getRolloff())
        };
    }];
}

- (BOOL)setAudioSourceInfo:(NSString *)uuid info:(NSDictionary *)info {
    return [self performSyncBool:^BOOL {
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene || !info) return NO;
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return NO;
        AudioSource* audio = entity->getComponent<AudioSource>();
        if (!audio) {
            audio = entity->addComponent<AudioSource>();
        }

        if (NSString* path = info[@"filePath"]) {
            audio->setFilePath(path.UTF8String);
        }
        if (NSNumber* volume = info[@"volume"]) {
            audio->setVolume(volume.floatValue);
        }
        if (NSNumber* pitch = info[@"pitch"]) {
            audio->setPitch(pitch.floatValue);
        }
        if (NSNumber* looping = info[@"looping"]) {
            audio->setLooping(looping.boolValue);
        }
        if (NSNumber* playOnStart = info[@"playOnStart"]) {
            audio->setPlayOnStart(playOnStart.boolValue);
        }
        if (NSNumber* spatial = info[@"spatial"]) {
            audio->setSpatial(spatial.boolValue);
        }
        if (NSString* bus = info[@"bus"]) {
            audio->setBus(AudioSystem::audioBusFromString(bus.UTF8String));
        }
        if (NSNumber* stream = info[@"stream"]) {
            audio->setStreaming(stream.boolValue);
        }
        if (NSNumber* minDistance = info[@"minDistance"]) {
            audio->setMinDistance(minDistance.floatValue);
        }
        if (NSNumber* maxDistance = info[@"maxDistance"]) {
            audio->setMaxDistance(maxDistance.floatValue);
        }
        if (NSNumber* rolloff = info[@"rolloff"]) {
            audio->setRolloff(rolloff.floatValue);
        }
        return YES;
    }];
}

- (BOOL)addAudioSource:(NSString *)uuid {
    return [self performSyncBool:^BOOL {
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return NO;
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return NO;
        if (!entity->getComponent<AudioSource>()) {
            entity->addComponent<AudioSource>();
        }
        return YES;
    }];
}

- (void)removeAudioSource:(NSString *)uuid {
    [self performAsync:^{
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene) return;
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.UTF8String);
        if (!entity) return;
        entity->removeComponent<AudioSource>();
    }];
}

- (BOOL)getPhysicsDebugDraw {
    return [self performSyncBool:^BOOL {
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene || !scene->getPhysicsWorld()) return NO;
        return scene->getPhysicsWorld()->isDebugDrawEnabled();
    }];
}

- (void)setPhysicsDebugDraw:(BOOL)enabled {
    [self performAsync:^{
        Scene* scene = SceneManager::getInstance().getActiveScene();
        if (!scene || !scene->getPhysicsWorld()) return;
        scene->getPhysicsWorld()->setDebugDrawEnabled(enabled);
    }];
}

- (void)setDebugDrawShadowAtlas:(BOOL)enabled {
    [self performAsync:^{
        if (_engine && _engine->getRenderer()) {
            _engine->getRenderer()->setDebugDrawShadowAtlas(enabled);
        }
    }];
}

- (void)setDebugDrawCascades:(BOOL)enabled {
    [self performAsync:^{
        if (_engine && _engine->getRenderer()) {
            _engine->getRenderer()->setDebugDrawCascades(enabled);
        }
    }];
}

- (void)setDebugDrawPointFrusta:(BOOL)enabled {
    [self performAsync:^{
        if (_engine && _engine->getRenderer()) {
            _engine->getRenderer()->setDebugDrawPointFrusta(enabled);
        }
    }];
}

@end
