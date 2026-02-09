#include "SceneSerializer.hpp"
#include "SceneCommands.hpp"
#include "../Assets/AssetDatabase.hpp"
#include "../Core/Engine.hpp"
#include "../Renderer/Renderer.hpp"
#include "../Rendering/Texture.hpp"
#include "../Components/MeshRenderer.hpp"
#include "../Components/SkinnedMeshRenderer.hpp"
#include "../Components/Animator.hpp"
#include "../Components/IKConstraint.hpp"
#include "../Components/Rigidbody.hpp"
#include "../Components/PhysicsCollider.hpp"
#include "../Components/CharacterController.hpp"
#include "../Components/FirstPersonController.hpp"
#include "../Components/Health.hpp"
#include "../Components/AudioSource.hpp"
#include "../Input/InputManager.hpp"
#include "../Animation/AnimationClip.hpp"
#include "../Components/ModelMeshReference.hpp"
#include "../Components/HLODProxy.hpp"
#include "../Components/PrimitiveMesh.hpp"
#include "../Components/Light.hpp"
#include "../Components/Decal.hpp"
#include "../Components/Camera.hpp"
#include "../Components/CameraController.hpp"
#include "SceneSettings.hpp"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <filesystem>
#include "../ECS/Transform.hpp"
#include "../../../ThirdParty/nlohmann/json.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace Crescent {
namespace {

using json = nlohmann::json;

json Vec2ToJson(const Math::Vector2& v) {
    return json::array({v.x, v.y});
}

json Vec3ToJson(const Math::Vector3& v) {
    return json::array({v.x, v.y, v.z});
}

json Vec4ToJson(const Math::Vector4& v) {
    return json::array({v.x, v.y, v.z, v.w});
}

std::string AnimatorParamTypeToString(AnimatorParameterType type) {
    switch (type) {
        case AnimatorParameterType::Float: return "Float";
        case AnimatorParameterType::Int: return "Int";
        case AnimatorParameterType::Bool: return "Bool";
        case AnimatorParameterType::Trigger: return "Trigger";
    }
    return "Float";
}

AnimatorParameterType AnimatorParamTypeFromString(const std::string& type) {
    if (type == "Int") return AnimatorParameterType::Int;
    if (type == "Bool") return AnimatorParameterType::Bool;
    if (type == "Trigger") return AnimatorParameterType::Trigger;
    return AnimatorParameterType::Float;
}

std::string AnimatorCondOpToString(AnimatorConditionOp op) {
    switch (op) {
        case AnimatorConditionOp::IfTrue: return "IfTrue";
        case AnimatorConditionOp::IfFalse: return "IfFalse";
        case AnimatorConditionOp::Greater: return "Greater";
        case AnimatorConditionOp::Less: return "Less";
        case AnimatorConditionOp::GreaterEqual: return "GreaterEqual";
        case AnimatorConditionOp::LessEqual: return "LessEqual";
        case AnimatorConditionOp::Equal: return "Equal";
        case AnimatorConditionOp::NotEqual: return "NotEqual";
    }
    return "IfTrue";
}

AnimatorConditionOp AnimatorCondOpFromString(const std::string& op) {
    if (op == "IfFalse") return AnimatorConditionOp::IfFalse;
    if (op == "Greater") return AnimatorConditionOp::Greater;
    if (op == "Less") return AnimatorConditionOp::Less;
    if (op == "GreaterEqual") return AnimatorConditionOp::GreaterEqual;
    if (op == "LessEqual") return AnimatorConditionOp::LessEqual;
    if (op == "Equal") return AnimatorConditionOp::Equal;
    if (op == "NotEqual") return AnimatorConditionOp::NotEqual;
    return AnimatorConditionOp::IfTrue;
}

std::string RigidbodyTypeToString(RigidbodyType type) {
    switch (type) {
    case RigidbodyType::Static: return "Static";
    case RigidbodyType::Kinematic: return "Kinematic";
    case RigidbodyType::Dynamic:
    default:
        return "Dynamic";
    }
}

RigidbodyType RigidbodyTypeFromString(const std::string& value) {
    if (value == "Static") return RigidbodyType::Static;
    if (value == "Kinematic") return RigidbodyType::Kinematic;
    return RigidbodyType::Dynamic;
}

std::string ColliderShapeToString(PhysicsCollider::ShapeType type) {
    switch (type) {
    case PhysicsCollider::ShapeType::Sphere: return "Sphere";
    case PhysicsCollider::ShapeType::Capsule: return "Capsule";
    case PhysicsCollider::ShapeType::Mesh: return "Mesh";
    case PhysicsCollider::ShapeType::Box:
    default:
        return "Box";
    }
}

std::string CombineModeToString(PhysicsCollider::CombineMode mode) {
    switch (mode) {
    case PhysicsCollider::CombineMode::Min: return "Min";
    case PhysicsCollider::CombineMode::Multiply: return "Multiply";
    case PhysicsCollider::CombineMode::Max: return "Max";
    case PhysicsCollider::CombineMode::Average:
    default:
        return "Average";
    }
}

PhysicsCollider::ShapeType ColliderShapeFromString(const std::string& value) {
    if (value == "Sphere") return PhysicsCollider::ShapeType::Sphere;
    if (value == "Capsule") return PhysicsCollider::ShapeType::Capsule;
    if (value == "Mesh") return PhysicsCollider::ShapeType::Mesh;
    return PhysicsCollider::ShapeType::Box;
}

PhysicsCollider::CombineMode CombineModeFromString(const std::string& value) {
    if (value == "Min") return PhysicsCollider::CombineMode::Min;
    if (value == "Multiply") return PhysicsCollider::CombineMode::Multiply;
    if (value == "Max") return PhysicsCollider::CombineMode::Max;
    return PhysicsCollider::CombineMode::Average;
}

std::string AnimatorStateTypeToString(AnimatorStateType type) {
    switch (type) {
        case AnimatorStateType::BlendTree: return "BlendTree";
        case AnimatorStateType::Clip:
        default: return "Clip";
    }
}

AnimatorStateType AnimatorStateTypeFromString(const std::string& type) {
    if (type == "BlendTree") return AnimatorStateType::BlendTree;
    return AnimatorStateType::Clip;
}

std::string AnimatorBlendTypeToString(AnimatorBlendTreeType type) {
    switch (type) {
        case AnimatorBlendTreeType::Blend1D:
        default: return "Blend1D";
    }
}

AnimatorBlendTreeType AnimatorBlendTypeFromString(const std::string& type) {
    (void)type;
    return AnimatorBlendTreeType::Blend1D;
}

json QuatToJson(const Math::Quaternion& q) {
    return json::array({q.x, q.y, q.z, q.w});
}

Math::Vector2 JsonToVec2(const json& j, const Math::Vector2& fallback = Math::Vector2::Zero) {
    if (!j.is_array() || j.size() < 2) {
        return fallback;
    }
    return Math::Vector2(j[0].get<float>(), j[1].get<float>());
}

Math::Vector3 JsonToVec3(const json& j, const Math::Vector3& fallback = Math::Vector3::Zero) {
    if (!j.is_array() || j.size() < 3) {
        return fallback;
    }
    return Math::Vector3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
}

Math::Vector4 JsonToVec4(const json& j, const Math::Vector4& fallback = Math::Vector4::Zero) {
    if (!j.is_array() || j.size() < 4) {
        return fallback;
    }
    return Math::Vector4(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>());
}

Math::Quaternion JsonToQuat(const json& j, const Math::Quaternion& fallback = Math::Quaternion::Identity) {
    if (!j.is_array() || j.size() < 4) {
        return fallback;
    }
    return Math::Quaternion(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>());
}

std::string PrimitiveTypeToString(PrimitiveType type) {
    switch (type) {
        case PrimitiveType::Cube: return "Cube";
        case PrimitiveType::Sphere: return "Sphere";
        case PrimitiveType::Plane: return "Plane";
        case PrimitiveType::Cylinder: return "Cylinder";
        case PrimitiveType::Cone: return "Cone";
        case PrimitiveType::Torus: return "Torus";
        case PrimitiveType::Capsule: return "Capsule";
        default: return "Cube";
    }
}

PrimitiveType PrimitiveTypeFromString(const std::string& value) {
    if (value == "Sphere") return PrimitiveType::Sphere;
    if (value == "Plane") return PrimitiveType::Plane;
    if (value == "Cylinder") return PrimitiveType::Cylinder;
    if (value == "Cone") return PrimitiveType::Cone;
    if (value == "Torus") return PrimitiveType::Torus;
    if (value == "Capsule") return PrimitiveType::Capsule;
    return PrimitiveType::Cube;
}

json SerializeImportOptions(const SceneCommands::ModelImportOptions& options) {
    return {
        {"scale", options.scale},
        {"flipUVs", options.flipUVs},
        {"onlyLOD0", options.onlyLOD0},
        {"mergeStaticMeshes", options.mergeStaticMeshes}
    };
}

SceneCommands::ModelImportOptions DeserializeImportOptions(const json& j) {
    SceneCommands::ModelImportOptions options;
    if (j.is_object()) {
        options.scale = j.value("scale", options.scale);
        options.flipUVs = j.value("flipUVs", options.flipUVs);
        options.onlyLOD0 = j.value("onlyLOD0", options.onlyLOD0);
        options.mergeStaticMeshes = j.value("mergeStaticMeshes", options.mergeStaticMeshes);
    }
    return options;
}

json SerializeTextureRef(const std::shared_ptr<Texture2D>& texture, const char* typeKey) {
    if (!texture || texture->getPath().empty()) {
        return json();
    }
    const std::string& path = texture->getPath();
    AssetDatabase& db = AssetDatabase::getInstance();
    std::string guid = db.registerAsset(path, typeKey ? typeKey : "");
    if (!guid.empty()) {
        std::string relative = db.getRelativePath(path);
        json ref;
        ref["guid"] = guid;
        ref["path"] = relative.empty() ? path : relative;
        return ref;
    }
    return path;
}

json SerializeAssetPath(const std::string& path, const char* typeKey) {
    if (path.empty()) {
        return json();
    }
    AssetDatabase& db = AssetDatabase::getInstance();
    std::string guid = db.registerAsset(path, typeKey ? typeKey : "");
    if (!guid.empty()) {
        std::string relative = db.getRelativePath(path);
        json ref;
        ref["guid"] = guid;
        ref["path"] = relative.empty() ? path : relative;
        return ref;
    }
    return path;
}

json SerializeMaterial(const Material& material) {
    json j;
    j["name"] = material.getName();
    j["albedo"] = Vec4ToJson(material.getAlbedo());
    j["metallic"] = material.getMetallic();
    j["roughness"] = material.getRoughness();
    j["ao"] = material.getAO();
    j["emission"] = Vec3ToJson(material.getEmission());
    j["emissionStrength"] = material.getEmissionStrength();
    j["uvTiling"] = Vec2ToJson(material.getUVTiling());
    j["uvOffset"] = Vec2ToJson(material.getUVOffset());
    j["normalScale"] = material.getNormalScale();
    j["heightScale"] = material.getHeightScale();
    j["heightInvert"] = material.getHeightInvert();
    j["renderMode"] = static_cast<int>(material.getRenderMode());
    j["alphaCutoff"] = material.getAlphaCutoff();
    j["cullMode"] = static_cast<int>(material.getCullMode());
    j["twoSided"] = material.isTwoSided();
    j["alphaToCoverage"] = material.getAlphaToCoverage();
    j["windEnabled"] = material.getWindEnabled();
    j["windStrength"] = material.getWindStrength();
    j["windSpeed"] = material.getWindSpeed();
    j["windScale"] = material.getWindScale();
    j["windGust"] = material.getWindGust();
    j["windDirection"] = Vec3ToJson(material.getWindDirection());
    j["lodFadeEnabled"] = material.getLodFadeEnabled();
    j["lodFadeStart"] = material.getLodFadeStart();
    j["lodFadeEnd"] = material.getLodFadeEnd();
    j["ditherEnabled"] = material.getDitherEnabled();
    j["billboardEnabled"] = material.getBillboardEnabled();
    j["billboardStart"] = material.getBillboardStart();
    j["billboardEnd"] = material.getBillboardEnd();
    j["impostorEnabled"] = material.getImpostorEnabled();
    j["impostorRows"] = material.getImpostorRows();
    j["impostorCols"] = material.getImpostorCols();

    json textures = json::object();
    auto pushPath = [&textures](const char* key, const std::shared_ptr<Texture2D>& tex, const char* typeKey) {
        json ref = SerializeTextureRef(tex, typeKey);
        if (!ref.is_null() && !ref.empty()) {
            textures[key] = ref;
        }
    };
    pushPath("albedo", material.getAlbedoTexture(), "texture");
    pushPath("normal", material.getNormalTexture(), "texture");
    pushPath("metallic", material.getMetallicTexture(), "texture");
    pushPath("roughness", material.getRoughnessTexture(), "texture");
    pushPath("ao", material.getAOTexture(), "texture");
    pushPath("emissive", material.getEmissionTexture(), "texture");
    pushPath("orm", material.getORMTexture(), "texture");
    pushPath("height", material.getHeightTexture(), "texture");

    if (!textures.empty()) {
        j["textures"] = textures;
    }

    return j;
}

json SerializeMeshData(const Mesh& mesh) {
    json j;
    const auto& vertices = mesh.getVertices();
    const auto& indices = mesh.getIndices();
    const auto& submeshes = mesh.getSubmeshes();

    json verts = json::array();
    for (const auto& v : vertices) {
        verts.push_back({
            v.position.x, v.position.y, v.position.z,
            v.normal.x, v.normal.y, v.normal.z,
            v.texCoord.x, v.texCoord.y,
            v.tangent.x, v.tangent.y, v.tangent.z,
            v.bitangent.x, v.bitangent.y, v.bitangent.z,
            v.color.x, v.color.y, v.color.z, v.color.w
        });
    }

    json idx = json::array();
    for (auto i : indices) {
        idx.push_back(i);
    }

    json subs = json::array();
    for (const auto& sm : submeshes) {
        subs.push_back({
            {"start", sm.indexStart},
            {"count", sm.indexCount},
            {"material", sm.materialIndex}
        });
    }

    j["vertices"] = verts;
    j["indices"] = idx;
    j["submeshes"] = subs;
    j["boundsMin"] = Vec3ToJson(mesh.getBoundsMin());
    j["boundsMax"] = Vec3ToJson(mesh.getBoundsMax());
    return j;
}

std::shared_ptr<Mesh> DeserializeMeshData(const json& j) {
    if (!j.is_object() || !j.contains("vertices") || !j.contains("indices")) {
        return nullptr;
    }

    const json& verts = j["vertices"];
    const json& idx = j["indices"];
    if (!verts.is_array() || !idx.is_array()) {
        return nullptr;
    }

    std::vector<Vertex> vertices;
    vertices.reserve(verts.size());
    for (const auto& entry : verts) {
        if (!entry.is_array() || entry.size() < 18) {
            continue;
        }
        Vertex v;
        v.position = Math::Vector3(entry[0], entry[1], entry[2]);
        v.normal = Math::Vector3(entry[3], entry[4], entry[5]);
        v.texCoord = Math::Vector2(entry[6], entry[7]);
        v.tangent = Math::Vector3(entry[8], entry[9], entry[10]);
        v.bitangent = Math::Vector3(entry[11], entry[12], entry[13]);
        v.color = Math::Vector4(entry[14], entry[15], entry[16], entry[17]);
        vertices.push_back(v);
    }

    std::vector<uint32_t> indices;
    indices.reserve(idx.size());
    for (const auto& entry : idx) {
        indices.push_back(entry.get<uint32_t>());
    }

    std::vector<Submesh> submeshes;
    if (j.contains("submeshes") && j["submeshes"].is_array()) {
        for (const auto& sm : j["submeshes"]) {
            Submesh sub;
            sub.indexStart = sm.value("start", 0u);
            sub.indexCount = sm.value("count", 0u);
            sub.materialIndex = sm.value("material", 0u);
            submeshes.push_back(sub);
        }
    }

    auto mesh = std::make_shared<Mesh>();
    mesh->setVertices(vertices);
    mesh->setIndices(indices);
    if (!submeshes.empty()) {
        mesh->setSubmeshes(submeshes);
    }
    return mesh;
}

SceneEnvironmentSettings EnvironmentFromRenderer(Renderer* renderer) {
    SceneEnvironmentSettings env;
    if (!renderer) {
        return env;
    }
    const Renderer::EnvironmentSettings& src = renderer->getEnvironmentSettings();
    env.skyboxPath = src.sourcePath.empty() ? "Builtin Sky" : src.sourcePath;
    env.exposureEV = src.exposureEV;
    env.iblIntensity = src.iblIntensity;
    env.skyIntensity = src.skyIntensity;
    env.saturation = src.saturation;
    env.contrast = src.contrast;
    env.blurLevel = src.blurLevel;
    env.tint = src.tint;
    env.rotation = src.rotation;
    env.skyboxVisible = src.skyboxVisible;
    env.skyMode = src.skyMode;
    return env;
}

json SerializeEnvironmentSettings(const SceneEnvironmentSettings& env) {
    json e;
    if (env.skyboxPath.empty() || env.skyboxPath == "Builtin Sky") {
        e["skybox"] = "Builtin Sky";
    } else {
        json ref = SerializeAssetPath(env.skyboxPath, "hdri");
        if (!ref.is_null() && !ref.empty()) {
            e["skybox"] = ref;
        }
    }
    e["exposureEV"] = env.exposureEV;
    e["iblIntensity"] = env.iblIntensity;
    e["skyIntensity"] = env.skyIntensity;
    e["saturation"] = env.saturation;
    e["contrast"] = env.contrast;
    e["blurLevel"] = env.blurLevel;
    e["tint"] = Vec3ToJson(env.tint);
    e["rotation"] = Vec3ToJson(env.rotation);
    e["skyboxVisible"] = env.skyboxVisible;
    e["skyMode"] = env.skyMode;
    e["autoSunColor"] = env.autoSunColor;
    return e;
}

json SerializeFogSettings(const SceneFogSettings& fog) {
    return {
        {"enabled", fog.enabled},
        {"color", Vec3ToJson(fog.color)},
        {"density", fog.density},
        {"startDistance", fog.startDistance},
        {"endDistance", fog.endDistance},
        {"heightFog", fog.heightFog},
        {"height", fog.height},
        {"heightFalloff", fog.heightFalloff},
        {"volumetricScattering", fog.volumetricScattering},
        {"volumetricAnisotropy", fog.volumetricAnisotropy},
        {"volumetricHistoryWeight", fog.volumetricHistoryWeight},
        {"volumetricQuality", fog.volumetricQuality}
    };
}

json SerializePostProcessSettings(const ScenePostProcessSettings& post) {
    json j = {
        {"enabled", post.enabled},
        {"bloom", post.bloom},
        {"bloomIntensity", post.bloomIntensity},
        {"bloomThreshold", post.bloomThreshold},
        {"toneMapping", post.toneMapping},
        {"toneMappingMode", post.toneMappingMode},
        {"colorGrading", post.colorGrading},
        {"colorGradingIntensity", post.colorGradingIntensity},
        {"saturation", post.saturation},
        {"contrast", post.contrast},
        {"vignette", post.vignette},
        {"vignetteIntensity", post.vignetteIntensity},
        {"filmGrain", post.filmGrain},
        {"filmGrainIntensity", post.filmGrainIntensity},
        {"ssao", post.ssao},
        {"ssaoRadius", post.ssaoRadius},
        {"ssaoStrength", post.ssaoStrength},
        {"ssr", post.ssr},
        {"ssrMaxRoughness", post.ssrMaxRoughness},
        {"ssrThickness", post.ssrThickness},
        {"taa", post.taa},
        {"taaSharpness", post.taaSharpness},
        {"fxaa", post.fxaa},
        {"motionBlur", post.motionBlur},
        {"motionBlurStrength", post.motionBlurStrength},
        {"depthOfField", post.depthOfField},
        {"dofFocusDistance", post.dofFocusDistance},
        {"dofAperture", post.dofAperture}
    };
    if (!post.colorGradingLUT.empty()) {
        json ref = SerializeAssetPath(post.colorGradingLUT, "texture");
        if (!ref.is_null() && !ref.empty()) {
            j["colorGradingLUT"] = ref;
        }
    }
    return j;
}

json SerializeQualitySettings(const SceneQualitySettings& quality) {
    return {
        {"overrideProject", quality.overrideProject},
        {"shadowQuality", quality.shadowQuality},
        {"shadowResolution", quality.shadowResolution},
        {"msaaSamples", quality.msaaSamples},
        {"anisotropy", quality.anisotropy},
        {"renderScale", quality.renderScale},
        {"lodBias", quality.lodBias},
        {"textureQuality", quality.textureQuality}
    };
}

std::string ResolveTextureEntryPath(const json& entry);

SceneEnvironmentSettings DeserializeEnvironmentSettings(const json& j) {
    SceneEnvironmentSettings env;
    if (!j.is_object()) {
        return env;
    }
    if (j.contains("skybox")) {
        if (j["skybox"].is_string() && j["skybox"].get<std::string>() == "Builtin Sky") {
            env.skyboxPath = "Builtin Sky";
        } else {
            std::string resolved = ResolveTextureEntryPath(j["skybox"]);
            if (!resolved.empty()) {
                env.skyboxPath = resolved;
            }
        }
    }
    env.exposureEV = j.value("exposureEV", env.exposureEV);
    env.iblIntensity = j.value("iblIntensity", env.iblIntensity);
    env.skyIntensity = j.value("skyIntensity", env.skyIntensity);
    env.saturation = j.value("saturation", env.saturation);
    env.contrast = j.value("contrast", env.contrast);
    env.blurLevel = j.value("blurLevel", env.blurLevel);
    if (j.contains("tint")) {
        env.tint = JsonToVec3(j["tint"], env.tint);
    }
    if (j.contains("rotation")) {
        env.rotation = JsonToVec3(j["rotation"], env.rotation);
    }
    env.skyboxVisible = j.value("skyboxVisible", env.skyboxVisible);
    env.skyMode = j.value("skyMode", env.skyMode);
    env.autoSunColor = j.value("autoSunColor", env.autoSunColor);
    return env;
}

SceneFogSettings DeserializeFogSettings(const json& j) {
    SceneFogSettings fog;
    if (!j.is_object()) {
        return fog;
    }
    fog.enabled = j.value("enabled", fog.enabled);
    if (j.contains("color")) {
        fog.color = JsonToVec3(j["color"], fog.color);
    }
    fog.density = j.value("density", fog.density);
    fog.startDistance = j.value("startDistance", fog.startDistance);
    fog.endDistance = j.value("endDistance", fog.endDistance);
    fog.heightFog = j.value("heightFog", fog.heightFog);
    fog.height = j.value("height", fog.height);
    fog.heightFalloff = j.value("heightFalloff", fog.heightFalloff);
    fog.volumetricScattering = j.value("volumetricScattering", fog.volumetricScattering);
    fog.volumetricAnisotropy = j.value("volumetricAnisotropy", fog.volumetricAnisotropy);
    fog.volumetricHistoryWeight = j.value("volumetricHistoryWeight", fog.volumetricHistoryWeight);
    fog.volumetricQuality = j.value("volumetricQuality", fog.volumetricQuality);
    return fog;
}

ScenePostProcessSettings DeserializePostProcessSettings(const json& j) {
    ScenePostProcessSettings post;
    if (!j.is_object()) {
        return post;
    }
    post.enabled = j.value("enabled", post.enabled);
    post.bloom = j.value("bloom", post.bloom);
    post.bloomIntensity = j.value("bloomIntensity", post.bloomIntensity);
    post.bloomThreshold = j.value("bloomThreshold", post.bloomThreshold);
    post.toneMapping = j.value("toneMapping", post.toneMapping);
    post.toneMappingMode = j.value("toneMappingMode", post.toneMappingMode);
    post.colorGrading = j.value("colorGrading", post.colorGrading);
    post.colorGradingIntensity = j.value("colorGradingIntensity", post.colorGradingIntensity);
    post.saturation = j.value("saturation", post.saturation);
    post.contrast = j.value("contrast", post.contrast);
    post.vignette = j.value("vignette", post.vignette);
    post.vignetteIntensity = j.value("vignetteIntensity", post.vignetteIntensity);
    post.filmGrain = j.value("filmGrain", post.filmGrain);
    post.filmGrainIntensity = j.value("filmGrainIntensity", post.filmGrainIntensity);
    post.ssao = j.value("ssao", post.ssao);
    post.ssaoRadius = j.value("ssaoRadius", post.ssaoRadius);
    post.ssaoStrength = j.value("ssaoStrength", post.ssaoStrength);
    post.ssr = j.value("ssr", post.ssr);
    post.ssrMaxRoughness = j.value("ssrMaxRoughness", post.ssrMaxRoughness);
    post.ssrThickness = j.value("ssrThickness", post.ssrThickness);
    post.taa = j.value("taa", post.taa);
    post.taaSharpness = j.value("taaSharpness", post.taaSharpness);
    post.fxaa = j.value("fxaa", post.fxaa);
    post.motionBlur = j.value("motionBlur", post.motionBlur);
    post.motionBlurStrength = j.value("motionBlurStrength", post.motionBlurStrength);
    post.depthOfField = j.value("depthOfField", post.depthOfField);
    post.dofFocusDistance = j.value("dofFocusDistance", post.dofFocusDistance);
    post.dofAperture = j.value("dofAperture", post.dofAperture);
    if (j.contains("colorGradingLUT")) {
        std::string resolved = ResolveTextureEntryPath(j["colorGradingLUT"]);
        if (!resolved.empty()) {
            post.colorGradingLUT = resolved;
        }
    }
    return post;
}

SceneQualitySettings DeserializeQualitySettings(const json& j) {
    SceneQualitySettings quality;
    if (!j.is_object()) {
        return quality;
    }
    quality.overrideProject = j.value("overrideProject", quality.overrideProject);
    quality.shadowQuality = j.value("shadowQuality", quality.shadowQuality);
    quality.shadowResolution = j.value("shadowResolution", quality.shadowResolution);
    quality.msaaSamples = j.value("msaaSamples", quality.msaaSamples);
    quality.anisotropy = j.value("anisotropy", quality.anisotropy);
    quality.renderScale = j.value("renderScale", quality.renderScale);
    quality.lodBias = j.value("lodBias", quality.lodBias);
    quality.textureQuality = j.value("textureQuality", quality.textureQuality);
    return quality;
}

struct EmbeddedTextureInfo {
    std::string sourcePath;
    int index = -1;
};

std::string ExtractEmbeddedChannelOrder(const aiTexture* texture) {
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

bool BuildEmbeddedRGBA(const aiTexture* texture, std::vector<unsigned char>& rgba) {
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

bool ParseEmbeddedTextureKey(const std::string& path, EmbeddedTextureInfo& outInfo) {
    const std::string marker = "#embedded:";
    size_t markerPos = path.find(marker);
    if (markerPos == std::string::npos) {
        return false;
    }
    outInfo.sourcePath = path.substr(0, markerPos);
    std::string rest = path.substr(markerPos + marker.size());
    if (rest.empty()) {
        return false;
    }
    size_t dotPos = rest.find('.');
    std::string indexStr = (dotPos == std::string::npos) ? rest : rest.substr(0, dotPos);
    if (indexStr.empty()) {
        return false;
    }
    try {
        outInfo.index = std::stoi(indexStr);
    } catch (...) {
        return false;
    }
    return !outInfo.sourcePath.empty() && outInfo.index >= 0;
}

std::shared_ptr<Texture2D> LoadEmbeddedTextureFromModel(TextureLoader* loader,
                                                        const std::string& cacheKey,
                                                        const EmbeddedTextureInfo& info,
                                                        bool srgb,
                                                        bool normalMap) {
    if (!loader) {
        return nullptr;
    }
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(info.sourcePath, 0);
    if (!scene || info.index < 0 || info.index >= static_cast<int>(scene->mNumTextures)) {
        return nullptr;
    }
    const aiTexture* texture = scene->mTextures[info.index];
    if (!texture) {
        return nullptr;
    }
    if (texture->mHeight == 0) {
        const unsigned char* data = reinterpret_cast<const unsigned char*>(texture->pcData);
        size_t size = static_cast<size_t>(texture->mWidth);
        return loader->loadTextureFromMemory(data, size, srgb, true, cacheKey, normalMap);
    }
    std::vector<unsigned char> rgba;
    if (!BuildEmbeddedRGBA(texture, rgba)) {
        return nullptr;
    }
    return loader->createTextureFromRGBA8(cacheKey, rgba.data(),
                                          static_cast<int>(texture->mWidth),
                                          static_cast<int>(texture->mHeight),
                                          srgb,
                                          true,
                                          normalMap);
}

std::string ResolveTextureEntryPath(const json& entry) {
    AssetDatabase& db = AssetDatabase::getInstance();
    if (entry.is_string()) {
        return entry.get<std::string>();
    }
    if (entry.is_object()) {
        if (entry.contains("guid")) {
            std::string guid = entry.value("guid", std::string());
            if (!guid.empty()) {
                std::string path = db.getPathForGuid(guid);
                if (!path.empty()) {
                    return path;
                }
            }
        }
        if (entry.contains("path")) {
            std::string stored = entry.value("path", std::string());
            if (!stored.empty()) {
                return db.resolvePath(stored);
            }
        }
    }
    return "";
}

std::shared_ptr<Texture2D> LoadTexturePath(TextureLoader* loader,
                                           const std::string& path,
                                           bool srgb,
                                           bool normalMap) {
    if (!loader || path.empty()) {
        return nullptr;
    }
    EmbeddedTextureInfo info;
    if (ParseEmbeddedTextureKey(path, info)) {
        return LoadEmbeddedTextureFromModel(loader, path, info, srgb, normalMap);
    }
    return loader->loadTexture(path, srgb, true, normalMap);
}

std::shared_ptr<Material> DeserializeMaterial(const json& j, TextureLoader* loader) {
    auto material = Material::CreateDefault();
    if (!j.is_object()) {
        return material;
    }
    if (j.contains("name")) {
        material->setName(j.value("name", std::string("Material")));
    }
    if (j.contains("albedo")) {
        material->setAlbedo(JsonToVec4(j["albedo"], material->getAlbedo()));
    }
    material->setMetallic(j.value("metallic", material->getMetallic()));
    material->setRoughness(j.value("roughness", material->getRoughness()));
    material->setAO(j.value("ao", material->getAO()));
    if (j.contains("emission")) {
        material->setEmission(JsonToVec3(j["emission"], material->getEmission()));
    }
    material->setEmissionStrength(j.value("emissionStrength", material->getEmissionStrength()));
    if (j.contains("uvTiling")) {
        material->setUVTiling(JsonToVec2(j["uvTiling"], material->getUVTiling()));
    }
    if (j.contains("uvOffset")) {
        material->setUVOffset(JsonToVec2(j["uvOffset"], material->getUVOffset()));
    }
    material->setNormalScale(j.value("normalScale", material->getNormalScale()));
    material->setHeightScale(j.value("heightScale", material->getHeightScale()));
    material->setHeightInvert(j.value("heightInvert", material->getHeightInvert()));
    material->setRenderMode(static_cast<Material::RenderMode>(j.value("renderMode", static_cast<int>(material->getRenderMode()))));
    material->setAlphaCutoff(j.value("alphaCutoff", material->getAlphaCutoff()));
    material->setCullMode(static_cast<Material::CullMode>(j.value("cullMode", static_cast<int>(material->getCullMode()))));
    material->setTwoSided(j.value("twoSided", material->isTwoSided()));
    material->setAlphaToCoverage(j.value("alphaToCoverage", material->getAlphaToCoverage()));
    material->setWindEnabled(j.value("windEnabled", material->getWindEnabled()));
    material->setWindStrength(j.value("windStrength", material->getWindStrength()));
    material->setWindSpeed(j.value("windSpeed", material->getWindSpeed()));
    material->setWindScale(j.value("windScale", material->getWindScale()));
    material->setWindGust(j.value("windGust", material->getWindGust()));
    if (j.contains("windDirection")) {
        material->setWindDirection(JsonToVec3(j["windDirection"], material->getWindDirection()));
    }
    material->setLodFadeEnabled(j.value("lodFadeEnabled", material->getLodFadeEnabled()));
    material->setLodFadeStart(j.value("lodFadeStart", material->getLodFadeStart()));
    material->setLodFadeEnd(j.value("lodFadeEnd", material->getLodFadeEnd()));
    material->setDitherEnabled(j.value("ditherEnabled", material->getDitherEnabled()));
    material->setBillboardEnabled(j.value("billboardEnabled", material->getBillboardEnabled()));
    material->setBillboardStart(j.value("billboardStart", material->getBillboardStart()));
    material->setBillboardEnd(j.value("billboardEnd", material->getBillboardEnd()));
    material->setImpostorEnabled(j.value("impostorEnabled", material->getImpostorEnabled()));
    material->setImpostorRows(j.value("impostorRows", material->getImpostorRows()));
    material->setImpostorCols(j.value("impostorCols", material->getImpostorCols()));

    if (j.contains("textures") && j["textures"].is_object()) {
        const json& t = j["textures"];
        auto load = [&](const char* key, bool srgb, bool normalMap) -> std::shared_ptr<Texture2D> {
            if (!t.contains(key)) {
                return nullptr;
            }
            std::string resolved = ResolveTextureEntryPath(t[key]);
            if (resolved.empty()) {
                return nullptr;
            }
            return LoadTexturePath(loader, resolved, srgb, normalMap);
        };
        if (auto tex = load("albedo", true, true)) material->setAlbedoTexture(tex);
        if (auto tex = load("normal", false, true)) material->setNormalTexture(tex);
        if (auto tex = load("metallic", false, false)) material->setMetallicTexture(tex);
        if (auto tex = load("roughness", false, false)) material->setRoughnessTexture(tex);
        if (auto tex = load("ao", false, false)) material->setAOTexture(tex);
        if (auto tex = load("emissive", true, false)) material->setEmissionTexture(tex);
        if (auto tex = load("orm", false, false)) material->setORMTexture(tex);
        if (auto tex = load("height", false, false)) material->setHeightTexture(tex);
    }

    return material;
}

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

struct EntityRecord {
    Entity* entity = nullptr;
    json components;
    std::string parentUUID;
    bool active = true;
    bool editorOnly = false;
};

std::string MakeModelCacheKey(const std::string& path, const SceneCommands::ModelImportOptions& options) {
    return path + "|" + std::to_string(options.scale) + "|" +
           (options.flipUVs ? "1" : "0") + "|" +
           (options.onlyLOD0 ? "1" : "0") + "|" +
           (options.mergeStaticMeshes ? "1" : "0");
}

ModelCacheEntry BuildModelCache(const std::string& path, const SceneCommands::ModelImportOptions& options) {
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

bool ParseUUID(const std::string& value, UUID& outUUID) {
    try {
        uint64_t uuidValue = std::stoull(value, nullptr, 16);
        outUUID = UUID(uuidValue);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

bool SceneSerializer::SaveScene(Scene* scene, const std::string& path) {
    if (!scene) {
        return false;
    }
    std::ofstream out(path);
    if (!out.is_open()) {
        return false;
    }
    out << SerializeScene(scene, path);
    return true;
}

bool SceneSerializer::LoadScene(Scene* scene, const std::string& path) {
    if (!scene) {
        return false;
    }
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return DeserializeScene(scene, data, path);
}

namespace {

std::unordered_map<std::string, EntityRecord> BuildEntityRecords(Scene* scene,
                                                                 const json& entities,
                                                                 bool preserveUUIDs) {
    std::unordered_map<std::string, EntityRecord> records;
    if (!scene || !entities.is_array()) {
        return records;
    }
    for (const auto& e : entities) {
        if (!e.is_object()) {
            continue;
        }
        std::string uuidStr = e.value("uuid", std::string());
        std::string name = e.value("name", std::string("Entity"));
        Entity* entity = nullptr;
        UUID uuid;
        if (preserveUUIDs && !uuidStr.empty() && ParseUUID(uuidStr, uuid)) {
            entity = scene->createEntityWithUUID(uuid, name);
        } else {
            entity = scene->createEntity(name);
        }
        if (!entity) {
            continue;
        }
        EntityRecord record;
        record.entity = entity;
        record.components = e.value("components", json::object());
        record.parentUUID = e.value("parent", std::string());
        record.active = e.value("active", true);
        record.editorOnly = e.value("editorOnly", false);
        std::string key = uuidStr.empty() ? entity->getUUID().toString() : uuidStr;
        records[key] = record;
    }
    return records;
}

void ResolveEntityParents(Scene* scene, std::unordered_map<std::string, EntityRecord>& records) {
    for (auto& [uuid, record] : records) {
        if (!record.entity) {
            continue;
        }
        if (record.parentUUID.empty()) {
            continue;
        }
        auto parentIt = records.find(record.parentUUID);
        if (parentIt != records.end() && parentIt->second.entity) {
            record.entity->getTransform()->setParent(parentIt->second.entity->getTransform(), false);
            continue;
        }
        if (!scene) {
            continue;
        }
        UUID parentUUID;
        if (ParseUUID(record.parentUUID, parentUUID)) {
            if (Entity* parent = scene->findEntity(parentUUID)) {
                record.entity->getTransform()->setParent(parent->getTransform(), false);
            }
        }
    }
}

void ApplyEntityComponents(Entity* entity,
                           const json& components,
                           Scene* scene,
                           const std::string& scenePath,
                           TextureLoader* textureLoader,
                           std::unordered_map<std::string, ModelCacheEntry>& modelCache) {
    if (!entity) {
        return;
    }
    (void)scene;
    if (components.contains("Transform")) {
        const json& t = components["Transform"];
        Transform* transform = entity->getTransform();
        transform->setLocalPosition(JsonToVec3(t.value("position", json::array()), transform->getLocalPosition()));
        transform->setLocalRotation(JsonToQuat(t.value("rotation", json::array()), transform->getLocalRotation()));
        transform->setLocalScale(JsonToVec3(t.value("scale", json::array({1.0f, 1.0f, 1.0f})), transform->getLocalScale()));
    }

    if (components.contains("PrimitiveMesh")) {
        const json& p = components["PrimitiveMesh"];
        PrimitiveType type = PrimitiveTypeFromString(p.value("type", std::string("Cube")));
        entity->addComponent<PrimitiveMesh>()->setType(type);
    }

    ModelMeshReference* modelRef = nullptr;
    if (components.contains("ModelMeshReference")) {
        const json& m = components["ModelMeshReference"];
        modelRef = entity->addComponent<ModelMeshReference>();
        std::string storedPath = m.value("sourcePath", std::string());
        std::string guid = m.value("sourceGuid", std::string());
        std::string resolvedPath;
        AssetDatabase& db = AssetDatabase::getInstance();
        if (!guid.empty()) {
            resolvedPath = db.getPathForGuid(guid);
        }
        if (resolvedPath.empty()) {
            resolvedPath = db.resolvePath(storedPath);
        }
        if (resolvedPath.empty()) {
            resolvedPath = storedPath;
        }
        if (!resolvedPath.empty()) {
            std::filesystem::path resolved(resolvedPath);
            if (!std::filesystem::exists(resolved)) {
                if (!scenePath.empty()) {
                    std::filesystem::path sceneDir = std::filesystem::path(scenePath).parent_path();
                    std::filesystem::path candidate = (sceneDir / storedPath).lexically_normal();
                    if (std::filesystem::exists(candidate)) {
                        resolvedPath = candidate.string();
                    }
                }
            }
        }
        modelRef->setSourcePath(resolvedPath);
        modelRef->setSourceGuid(guid);
        modelRef->setMeshIndex(m.value("meshIndex", -1));
        modelRef->setMaterialIndex(m.value("materialIndex", -1));
        modelRef->setMeshName(m.value("meshName", std::string()));
        modelRef->setSkinned(m.value("skinned", false));
        modelRef->setMerged(m.value("merged", false));
        if (m.contains("importOptions")) {
            modelRef->setImportOptions(DeserializeImportOptions(m["importOptions"]));
        }
    }

    MeshRenderer* meshRenderer = nullptr;
    SkinnedMeshRenderer* skinnedRenderer = nullptr;

    if (modelRef) {
        std::string cacheKey = MakeModelCacheKey(modelRef->getSourcePath(), modelRef->getImportOptions());
        auto it = modelCache.find(cacheKey);
        if (it == modelCache.end()) {
            modelCache[cacheKey] = BuildModelCache(modelRef->getSourcePath(), modelRef->getImportOptions());
            it = modelCache.find(cacheKey);
        }
        const ModelCacheEntry& cache = it->second;
        MeshCacheEntry meshEntry;
        bool found = false;
        if (modelRef->isMerged()) {
            auto mergedIt = cache.mergedByMaterial.find(modelRef->getMaterialIndex());
            if (mergedIt != cache.mergedByMaterial.end()) {
                meshEntry = mergedIt->second;
                found = true;
            }
        } else {
            auto meshIt = cache.meshesByIndex.find(modelRef->getMeshIndex());
            if (meshIt != cache.meshesByIndex.end()) {
                meshEntry = meshIt->second;
                found = true;
            }
        }
        if (found) {
            meshRenderer = entity->addComponent<MeshRenderer>();
            meshRenderer->setMesh(meshEntry.mesh);
            meshRenderer->setMaterial(meshEntry.material ? meshEntry.material : Material::CreateDefault());
            if (modelRef->isSkinned() && cache.skeleton) {
                skinnedRenderer = entity->addComponent<SkinnedMeshRenderer>();
                skinnedRenderer->setMesh(meshEntry.mesh);
                skinnedRenderer->setMaterial(meshEntry.material ? meshEntry.material : Material::CreateDefault());
                skinnedRenderer->setSkeleton(cache.skeleton);
                if (!cache.animations.empty()) {
                    skinnedRenderer->setAnimationClips(cache.animations);
                }
            }
        }
    }

    if (!meshRenderer && components.contains("PrimitiveMesh")) {
        meshRenderer = entity->addComponent<MeshRenderer>();
        PrimitiveType type = entity->getComponent<PrimitiveMesh>()->getType();
        switch (type) {
            case PrimitiveType::Sphere:
                meshRenderer->setMesh(Mesh::CreateSphere(0.5f, 32, 16));
                break;
            case PrimitiveType::Plane:
                meshRenderer->setMesh(Mesh::CreatePlane(10.0f, 10.0f, 1, 1));
                break;
            case PrimitiveType::Cylinder:
                meshRenderer->setMesh(Mesh::CreateCylinder(0.5f, 1.0f, 32));
                break;
            case PrimitiveType::Cone:
                meshRenderer->setMesh(Mesh::CreateCone(0.5f, 1.0f, 32));
                break;
            case PrimitiveType::Torus:
                meshRenderer->setMesh(Mesh::CreateTorus(0.75f, 0.25f, 32, 16));
                break;
            case PrimitiveType::Capsule:
                meshRenderer->setMesh(Mesh::CreateCapsule(0.5f, 2.0f, 16));
                break;
            default:
                meshRenderer->setMesh(Mesh::CreateCube(1.0f));
                break;
        }
        meshRenderer->setMaterial(Material::CreateDefault());
    }

    if (components.contains("MeshRenderer")) {
        const json& r = components["MeshRenderer"];
        if (!meshRenderer) {
            meshRenderer = entity->addComponent<MeshRenderer>();
        }
        meshRenderer->setCastShadows(r.value("castShadows", meshRenderer->getCastShadows()));
        meshRenderer->setReceiveShadows(r.value("receiveShadows", meshRenderer->getReceiveShadows()));

        if (r.contains("materials") && r["materials"].is_array()) {
            const json& mats = r["materials"];
            for (size_t i = 0; i < mats.size(); ++i) {
                auto material = DeserializeMaterial(mats[i], textureLoader);
                meshRenderer->setMaterial(static_cast<uint32_t>(i), material);
                if (skinnedRenderer) {
                    skinnedRenderer->setMaterial(static_cast<uint32_t>(i), material);
                }
            }
        }
    }

    if (components.contains("HLODProxy")) {
        const json& h = components["HLODProxy"];
        HLODProxy* proxy = entity->getComponent<HLODProxy>();
        if (!proxy) {
            proxy = entity->addComponent<HLODProxy>();
        }
        proxy->setEnabled(h.value("enabled", proxy->isEnabled()));
        proxy->setLodStart(h.value("lodStart", proxy->getLodStart()));
        proxy->setLodEnd(h.value("lodEnd", proxy->getLodEnd()));
        if (h.contains("sources") && h["sources"].is_array()) {
            std::vector<std::string> sources;
            for (const auto& entry : h["sources"]) {
                sources.push_back(entry.get<std::string>());
            }
            proxy->setSourceUuids(sources);
        }
        if (h.contains("mesh")) {
            auto mesh = DeserializeMeshData(h["mesh"]);
            if (mesh) {
                if (!meshRenderer) {
                    meshRenderer = entity->addComponent<MeshRenderer>();
                }
                meshRenderer->setMesh(mesh);
            }
        }
    }

    if (components.contains("SkinnedMeshRenderer") && skinnedRenderer) {
        const json& s = components["SkinnedMeshRenderer"];
        skinnedRenderer->setPlaying(s.value("playing", skinnedRenderer->isPlaying()));
        skinnedRenderer->setLooping(s.value("looping", skinnedRenderer->isLooping()));
        skinnedRenderer->setPlaybackSpeed(std::max(0.0f, s.value("speed", skinnedRenderer->getPlaybackSpeed())));
        int clipIndex = s.value("clipIndex", skinnedRenderer->getActiveClipIndex());
        skinnedRenderer->setActiveClipIndex(clipIndex);
        skinnedRenderer->setTimeSeconds(std::max(0.0f, s.value("time", skinnedRenderer->getTimeSeconds())));
        skinnedRenderer->setRootMotionEnabled(s.value("rootMotionEnabled", skinnedRenderer->getRootMotionEnabled()));
        skinnedRenderer->setApplyRootMotionPosition(s.value("rootMotionPosition", skinnedRenderer->getApplyRootMotionPosition()));
        skinnedRenderer->setApplyRootMotionRotation(s.value("rootMotionRotation", skinnedRenderer->getApplyRootMotionRotation()));
        if (s.contains("clipEvents") && s["clipEvents"].is_array()) {
            const auto& clips = skinnedRenderer->getAnimationClips();
            for (const auto& entry : s["clipEvents"]) {
                int eventClipIndex = entry.value("clipIndex", -1);
                if (eventClipIndex < 0 || eventClipIndex >= static_cast<int>(clips.size())) {
                    continue;
                }
                auto clip = clips[static_cast<size_t>(eventClipIndex)];
                if (!clip) continue;
                clip->clearEvents();
                if (entry.contains("events") && entry["events"].is_array()) {
                    for (const auto& evt : entry["events"]) {
                        AnimationEvent ev;
                        ev.time = evt.value("time", 0.0f);
                        ev.name = evt.value("name", "");
                        if (!ev.name.empty()) {
                            clip->addEvent(ev);
                        }
                    }
                }
            }
        }
    }

    if (components.contains("Animator")) {
        const json& a = components["Animator"];
        Animator* animator = entity->getComponent<Animator>();
        if (animator) {
            animator->setDefaultBlendDuration(a.value("blendDuration", animator->getDefaultBlendDuration()));
            animator->setAutoPlay(a.value("autoPlay", animator->getAutoPlay()));
            if (a.contains("rootMotion")) {
                const json& rm = a["rootMotion"];
                animator->setRootMotionEnabled(rm.value("enabled", animator->getRootMotionEnabled()));
                animator->setApplyRootMotionPosition(rm.value("applyPosition", animator->getApplyRootMotionPosition()));
                animator->setApplyRootMotionRotation(rm.value("applyRotation", animator->getApplyRootMotionRotation()));
            }

            if (a.contains("parameters") && a["parameters"].is_array()) {
                std::vector<AnimatorParameter> params;
                for (const auto& p : a["parameters"]) {
                    AnimatorParameter param;
                    param.name = p.value("name", "");
                    param.type = AnimatorParamTypeFromString(p.value("type", "Float"));
                    param.floatValue = p.value("float", 0.0f);
                    param.intValue = p.value("int", 0);
                    param.boolValue = p.value("bool", false);
                    param.triggerValue = false;
                    if (!param.name.empty()) {
                        params.push_back(param);
                    }
                }
                animator->setParameters(params);
            }

            if (a.contains("blendTrees") && a["blendTrees"].is_array()) {
                std::vector<AnimatorBlendTree> trees;
                for (const auto& t : a["blendTrees"]) {
                    AnimatorBlendTree tree;
                    tree.name = t.value("name", "");
                    tree.type = AnimatorBlendTypeFromString(t.value("type", "Blend1D"));
                    tree.parameter = t.value("parameter", "");
                    if (t.contains("motions") && t["motions"].is_array()) {
                        for (const auto& m : t["motions"]) {
                            AnimatorBlendMotion motion;
                            motion.clipIndex = m.value("clipIndex", -1);
                            motion.threshold = m.value("threshold", 0.0f);
                            tree.motions.push_back(motion);
                        }
                    }
                    trees.push_back(tree);
                }
                animator->setBlendTrees(trees);
            }

            if (a.contains("states") && a["states"].is_array()) {
                std::vector<AnimatorState> states;
                for (const auto& s : a["states"]) {
                    AnimatorState state;
                    state.name = s.value("name", "");
                    state.type = AnimatorStateTypeFromString(s.value("type", "Clip"));
                    state.clipIndex = s.value("clipIndex", -1);
                    state.blendTreeIndex = s.value("blendTree", -1);
                    state.speed = s.value("speed", 1.0f);
                    state.loop = s.value("loop", true);
                    states.push_back(state);
                }
                animator->setStates(states);
            }

            if (a.contains("transitions") && a["transitions"].is_array()) {
                std::vector<AnimatorTransition> transitions;
                for (const auto& t : a["transitions"]) {
                    AnimatorTransition transition;
                    transition.fromState = t.value("from", -1);
                    transition.toState = t.value("to", -1);
                    transition.duration = t.value("duration", 0.25f);
                    transition.hasExitTime = t.value("hasExitTime", false);
                    transition.exitTime = t.value("exitTime", 0.9f);
                    transition.fixedDuration = t.value("fixedDuration", true);
                    if (t.contains("conditions") && t["conditions"].is_array()) {
                        for (const auto& c : t["conditions"]) {
                            AnimatorCondition cond;
                            cond.parameter = c.value("parameter", "");
                            cond.op = AnimatorCondOpFromString(c.value("op", "IfTrue"));
                            cond.threshold = c.value("threshold", 0.0f);
                            cond.intThreshold = c.value("intThreshold", 0);
                            cond.boolThreshold = c.value("boolThreshold", false);
                            if (!cond.parameter.empty()) {
                                transition.conditions.push_back(cond);
                            }
                        }
                    }
                    transitions.push_back(transition);
                }
                animator->setTransitions(transitions);
            }

            int stateIndex = a.value("stateIndex", animator->getCurrentStateIndex());
            animator->setCurrentStateIndex(stateIndex, 0.0f, true);
        }
    }

    if (components.contains("IKConstraint")) {
        const json& k = components["IKConstraint"];
        IKConstraint* ik = entity->getComponent<IKConstraint>();
        if (!ik) {
            ik = entity->addComponent<IKConstraint>();
        }
        ik->setRootBone(k.value("root", ""));
        ik->setMidBone(k.value("mid", ""));
        ik->setEndBone(k.value("end", ""));
        if (k.contains("target")) {
            ik->setTargetPosition(JsonToVec3(k["target"], ik->getTargetPosition()));
        }
        ik->setTargetInWorld(k.value("world", ik->getTargetInWorld()));
        ik->setWeight(k.value("weight", ik->getWeight()));
    }

    if (components.contains("Rigidbody")) {
        const json& r = components["Rigidbody"];
        Rigidbody* rb = entity->getComponent<Rigidbody>();
        if (!rb) {
            rb = entity->addComponent<Rigidbody>();
        }
        rb->setType(RigidbodyTypeFromString(r.value("type", std::string("Dynamic"))));
        rb->setMass(r.value("mass", rb->getMass()));
        rb->setLinearDamping(r.value("linearDamping", rb->getLinearDamping()));
        rb->setAngularDamping(r.value("angularDamping", rb->getAngularDamping()));
        rb->setUseGravity(r.value("useGravity", rb->getUseGravity()));
        rb->setContinuousCollision(r.value("continuous", rb->getContinuousCollision()));
        rb->setAllowSleep(r.value("allowSleep", rb->getAllowSleep()));
    }

    if (components.contains("PhysicsCollider")) {
        const json& c = components["PhysicsCollider"];
        PhysicsCollider* collider = entity->getComponent<PhysicsCollider>();
        if (!collider) {
            collider = entity->addComponent<PhysicsCollider>();
        }
        collider->setShapeType(ColliderShapeFromString(c.value("shape", std::string("Box"))));
        if (c.contains("size")) {
            collider->setSize(JsonToVec3(c["size"], collider->getSize()));
        }
        collider->setRadius(c.value("radius", collider->getRadius()));
        collider->setHeight(c.value("height", collider->getHeight()));
        if (c.contains("center")) {
            collider->setCenter(JsonToVec3(c["center"], collider->getCenter()));
        }
        collider->setTrigger(c.value("trigger", collider->isTrigger()));
        collider->setFriction(c.value("friction", collider->getFriction()));
        collider->setRestitution(c.value("restitution", collider->getRestitution()));
        collider->setFrictionCombine(CombineModeFromString(c.value("frictionCombine", std::string("Average"))));
        collider->setRestitutionCombine(CombineModeFromString(c.value("restitutionCombine", std::string("Average"))));
        collider->setCollisionLayer(c.value("collisionLayer", collider->getCollisionLayer()));
        collider->setCollisionMask(c.value("collisionMask", collider->getCollisionMask()));
    }

    if (components.contains("Health")) {
        const json& h = components["Health"];
        Health* health = entity->getComponent<Health>();
        if (!health) {
            health = entity->addComponent<Health>();
        }
        health->setMaxHealth(h.value("max", health->getMaxHealth()));
        health->setCurrentHealth(h.value("current", health->getCurrentHealth()));
        health->setDestroyOnDeath(h.value("destroyOnDeath", health->getDestroyOnDeath()));
    }

    if (components.contains("CharacterController")) {
        const json& c = components["CharacterController"];
        CharacterController* controller = entity->getComponent<CharacterController>();
        if (!controller) {
            controller = entity->addComponent<CharacterController>();
        }
        controller->setRadius(c.value("radius", controller->getRadius()));
        controller->setHeight(c.value("height", controller->getHeight()));
        controller->setSkinWidth(c.value("skinWidth", controller->getSkinWidth()));
        controller->setMoveSpeed(c.value("moveSpeed", controller->getMoveSpeed()));
        controller->setAcceleration(c.value("acceleration", controller->getAcceleration()));
        controller->setAirAcceleration(c.value("airAcceleration", controller->getAirAcceleration()));
        controller->setJumpSpeed(c.value("jumpSpeed", controller->getJumpSpeed()));
        controller->setGravity(c.value("gravity", controller->getGravity()));
        controller->setMaxFallSpeed(c.value("maxFallSpeed", controller->getMaxFallSpeed()));
        controller->setGroundSnapSpeed(c.value("groundSnapSpeed", controller->getGroundSnapSpeed()));
        controller->setStepOffset(c.value("stepOffset", controller->getStepOffset()));
        controller->setSlopeLimit(c.value("slopeLimit", controller->getSlopeLimit()));
        controller->setSlopeSlideSpeed(c.value("slopeSlideSpeed", controller->getSlopeSlideSpeed()));
        controller->setGroundCheckDistance(c.value("groundCheckDistance", controller->getGroundCheckDistance()));
        controller->setUseInput(c.value("useInput", controller->getUseInput()));
        controller->setUseGravity(c.value("useGravity", controller->getUseGravity()));
        controller->setEnableStep(c.value("enableStep", controller->getEnableStep()));
        controller->setEnableSlopeLimit(c.value("enableSlopeLimit", controller->getEnableSlopeLimit()));
        controller->setSnapToGround(c.value("snapToGround", controller->getSnapToGround()));
        controller->setCollisionMask(c.value("collisionMask", controller->getCollisionMask()));
    }

    if (components.contains("FirstPersonController")) {
        const json& c = components["FirstPersonController"];
        FirstPersonController* controller = entity->getComponent<FirstPersonController>();
        if (!controller) {
            controller = entity->addComponent<FirstPersonController>();
        }
        controller->setMouseSensitivity(c.value("mouseSensitivity", controller->getMouseSensitivity()));
        controller->setInvertY(c.value("invertY", controller->getInvertY()));
        controller->setRequireLookButton(c.value("requireLookButton", controller->getRequireLookButton()));
        controller->setLookButton(static_cast<MouseButton>(c.value("lookButton", static_cast<int>(controller->getLookButton()))));
        controller->setMinPitch(c.value("minPitch", controller->getMinPitch()));
        controller->setMaxPitch(c.value("maxPitch", controller->getMaxPitch()));
        controller->setWalkSpeed(c.value("walkSpeed", controller->getWalkSpeed()));
        controller->setSprintMultiplier(c.value("sprintMultiplier", controller->getSprintMultiplier()));
        controller->setEnableSprint(c.value("enableSprint", controller->getEnableSprint()));
        controller->setEnableCrouch(c.value("enableCrouch", controller->getEnableCrouch()));
        controller->setCrouchHeight(c.value("crouchHeight", controller->getCrouchHeight()));
        controller->setCrouchEyeHeight(c.value("crouchEyeHeight", controller->getCrouchEyeHeight()));
        controller->setCrouchSpeed(c.value("crouchSpeed", controller->getCrouchSpeed()));
        controller->setEyeHeight(c.value("eyeHeight", controller->getEyeHeight()));
        controller->setUseEyeHeight(c.value("useEyeHeight", controller->getUseEyeHeight()));
        controller->setDriveCharacterController(c.value("driveCharacterController", controller->getDriveCharacterController()));
        controller->setFireCooldown(c.value("fireCooldown", controller->getFireCooldown()));
        controller->setFireDamage(c.value("fireDamage", controller->getFireDamage()));
        controller->setFireRange(c.value("fireRange", controller->getFireRange()));
        controller->setFireHitMask(c.value("fireMask", controller->getFireHitMask()));
        controller->setFireHitTriggers(c.value("fireHitTriggers", controller->getFireHitTriggers()));
        if (c.contains("muzzleTexture")) {
            std::string resolved = ResolveTextureEntryPath(c["muzzleTexture"]);
            if (!resolved.empty()) {
                controller->setMuzzleTexturePath(resolved);
            } else if (c["muzzleTexture"].is_string()) {
                controller->setMuzzleTexturePath(c["muzzleTexture"].get<std::string>());
            }
        }
    }

    if (components.contains("AudioSource")) {
        const json& a = components["AudioSource"];
        AudioSource* audio = entity->getComponent<AudioSource>();
        if (!audio) {
            audio = entity->addComponent<AudioSource>();
        }
        audio->setStreaming(a.value("stream", audio->isStreaming()));
        if (a.contains("file")) {
            std::string resolved = ResolveTextureEntryPath(a["file"]);
            if (!resolved.empty()) {
                audio->setFilePath(resolved);
            } else if (a["file"].is_string()) {
                audio->setFilePath(a["file"].get<std::string>());
            }
        }
        audio->setVolume(a.value("volume", audio->getVolume()));
        audio->setPitch(a.value("pitch", audio->getPitch()));
        audio->setLooping(a.value("looping", audio->isLooping()));
        audio->setPlayOnStart(a.value("playOnStart", audio->getPlayOnStart()));
        audio->setSpatial(a.value("spatial", audio->isSpatial()));
        audio->setMinDistance(a.value("minDistance", audio->getMinDistance()));
        audio->setMaxDistance(a.value("maxDistance", audio->getMaxDistance()));
        audio->setRolloff(a.value("rolloff", audio->getRolloff()));
    }

    if (components.contains("Light")) {
        const json& l = components["Light"];
        Light* light = entity->addComponent<Light>();
        light->setType(static_cast<Light::Type>(l.value("type", static_cast<int>(light->getType()))));
        if (l.contains("temperatureK")) {
            light->setColorTemperature(l.value("temperatureK", light->getColorTemperature()));
        }
        if (l.contains("color")) {
            light->setColor(JsonToVec3(l["color"], light->getColor()));
        }
        light->setIntensity(l.value("intensity", light->getIntensity()));
            light->setIntensityUnit(static_cast<Light::IntensityUnit>(l.value("intensityUnit", static_cast<int>(light->getIntensityUnit()))));
            light->setRange(l.value("range", light->getRange()));
            if (l.contains("areaSize")) {
                light->setAreaSize(JsonToVec2(l["areaSize"], light->getAreaSize()));
            }
            light->setSourceRadius(l.value("sourceRadius", light->getSourceRadius()));
            light->setSourceLength(l.value("sourceLength", light->getSourceLength()));
            light->setFalloffModel(static_cast<Light::FalloffModel>(l.value("falloff", static_cast<int>(light->getFalloffModel()))));
            light->setSpotAngle(l.value("spotAngle", light->getSpotAngle()));
            light->setInnerSpotAngle(l.value("innerSpotAngle", light->getInnerSpotAngle()));
        light->setCastShadows(l.value("castsShadows", light->getCastShadows()));
        light->setSoftShadows(l.value("softShadows", light->getSoftShadows()));
        light->setShadowMapResolution(l.value("shadowResolution", light->getShadowMapResolution()));
        light->setShadowBias(l.value("shadowBias", light->getShadowBias()));
        light->setShadowNormalBias(l.value("shadowNormalBias", light->getShadowNormalBias()));
        if (l.contains("shadowNearPlane") || l.contains("shadowFarPlane")) {
            float nearPlane = l.value("shadowNearPlane", light->getShadowNearPlane());
            float farPlane = l.value("shadowFarPlane", light->getShadowFarPlane());
            light->setShadowRange(nearPlane, farPlane);
        }
        light->setPenumbra(l.value("penumbra", light->getPenumbra()));
        light->setContactShadows(l.value("contactShadows", light->getContactShadows()));
        light->setCascadeCount(static_cast<uint8_t>(l.value("cascadeCount", light->getCascadeCount())));
        if (l.contains("cascadeSplits") && l["cascadeSplits"].is_array() && l["cascadeSplits"].size() == 4) {
            std::array<float, 4> splits = {
                l["cascadeSplits"][0].get<float>(),
                l["cascadeSplits"][1].get<float>(),
                l["cascadeSplits"][2].get<float>(),
                l["cascadeSplits"][3].get<float>()
            };
            light->setCascadeSplits(splits);
        }
        light->setVolumetric(l.value("volumetric", light->getVolumetric()));
    }

    if (components.contains("Decal")) {
        const json& d = components["Decal"];
        Decal* decal = entity->addComponent<Decal>();
        if (d.contains("color")) {
            decal->setTint(JsonToVec4(d["color"], decal->getTint()));
        }
        decal->setOpacity(d.value("opacity", decal->getOpacity()));
        if (d.contains("tiling")) {
            decal->setTiling(JsonToVec2(d["tiling"], decal->getTiling()));
        }
        if (d.contains("offset")) {
            decal->setOffset(JsonToVec2(d["offset"], decal->getOffset()));
        }
        decal->setEdgeSoftness(d.value("softness", decal->getEdgeSoftness()));
        auto loadDecal = [&](const char* key, bool srgb, bool normalMap,
                             auto setPath, auto setTexture) {
            if (!d.contains(key)) {
                return;
            }
            std::string resolved = ResolveTextureEntryPath(d[key]);
            if (resolved.empty()) {
                return;
            }
            setPath(resolved);
            if (auto tex = LoadTexturePath(textureLoader, resolved, srgb, normalMap)) {
                setTexture(tex);
            }
        };
        loadDecal("albedo", true, true,
                  [&](const std::string& path){ decal->setAlbedoPath(path); },
                  [&](const std::shared_ptr<Texture2D>& tex){ decal->setAlbedoTexture(tex); });
        if (d.contains("texture")) {
            loadDecal("texture", true, true,
                      [&](const std::string& path){ decal->setAlbedoPath(path); },
                      [&](const std::shared_ptr<Texture2D>& tex){ decal->setAlbedoTexture(tex); });
        }
        loadDecal("normal", false, true,
                  [&](const std::string& path){ decal->setNormalPath(path); },
                  [&](const std::shared_ptr<Texture2D>& tex){ decal->setNormalTexture(tex); });
        loadDecal("orm", false, false,
                  [&](const std::string& path){ decal->setORMPath(path); },
                  [&](const std::shared_ptr<Texture2D>& tex){ decal->setORMTexture(tex); });
        loadDecal("mask", false, false,
                  [&](const std::string& path){ decal->setMaskPath(path); },
                  [&](const std::shared_ptr<Texture2D>& tex){ decal->setMaskTexture(tex); });
    }

    if (components.contains("Camera")) {
        const json& c = components["Camera"];
        Camera* camera = entity->addComponent<Camera>();
        camera->setProjectionType(static_cast<Camera::ProjectionType>(c.value("projection", static_cast<int>(camera->getProjectionType()))));
        camera->setFieldOfView(c.value("fov", camera->getFieldOfView()));
        camera->setOrthographicSize(c.value("orthoSize", camera->getOrthographicSize()));
        camera->setNearClip(c.value("nearClip", camera->getNearClip()));
        camera->setFarClip(c.value("farClip", camera->getFarClip()));
        camera->setAspectRatio(c.value("aspect", camera->getAspectRatio()));
        if (c.contains("viewport")) {
            camera->setViewport(JsonToVec4(c["viewport"], camera->getViewport()));
        }
        if (c.contains("clearColor")) {
            camera->setClearColor(JsonToVec4(c["clearColor"], camera->getClearColor()));
        }
        camera->setClearDepth(c.value("clearDepth", camera->getClearDepth()));
        camera->setEditorCamera(c.value("editorCamera", false));
    }

    if (components.contains("CameraController")) {
        const json& cc = components["CameraController"];
        CameraController* controller = entity->addComponent<CameraController>();
        controller->setMoveSpeed(cc.value("moveSpeed", controller->getMoveSpeed()));
        controller->setRotationSpeed(cc.value("rotationSpeed", controller->getRotationSpeed()));
    }
}

json BuildSceneJson(Scene* scene, bool includeAssetRoot, const std::string& scenePath, bool includeEditorOnly) {
    json root;
    root["version"] = 1;
    root["name"] = scene ? scene->getName() : "Scene";
    json entities = json::array();

    if (!scene) {
        root["entities"] = entities;
        return root;
    }

    const auto& allEntities = scene->getAllEntities();
    for (const auto& entityPtr : allEntities) {
        Entity* entity = entityPtr.get();
        if (!entity) {
            continue;
        }
        if (!includeEditorOnly && entity->isEditorOnly()) {
            continue;
        }
        json e;
        e["uuid"] = entity->getUUID().toString();
        e["name"] = entity->getName();
        e["active"] = entity->isActive();
        e["editorOnly"] = entity->isEditorOnly();

        Transform* transform = entity->getTransform();
        if (transform && transform->getParent()) {
            e["parent"] = transform->getParent()->getEntity()->getUUID().toString();
        }

        json components = json::object();
        if (transform) {
            components["Transform"] = {
                {"position", Vec3ToJson(transform->getLocalPosition())},
                {"rotation", QuatToJson(transform->getLocalRotation())},
                {"scale", Vec3ToJson(transform->getLocalScale())}
            };
        }

        if (auto* primitive = entity->getComponent<PrimitiveMesh>()) {
            components["PrimitiveMesh"] = {
                {"type", PrimitiveTypeToString(primitive->getType())}
            };
        }

        if (auto* modelRef = entity->getComponent<ModelMeshReference>()) {
            AssetDatabase& db = AssetDatabase::getInstance();
            std::string sourcePath = modelRef->getSourcePath();
            std::string guid = modelRef->getSourceGuid();
            if (guid.empty()) {
                guid = db.registerAsset(sourcePath, "model");
            }
            std::string storedPath = db.getRelativePath(sourcePath);
            if (storedPath.empty()) {
                storedPath = sourcePath;
            }
            components["ModelMeshReference"] = {
                {"sourcePath", storedPath},
                {"sourceGuid", guid},
                {"meshIndex", modelRef->getMeshIndex()},
                {"materialIndex", modelRef->getMaterialIndex()},
                {"meshName", modelRef->getMeshName()},
                {"skinned", modelRef->isSkinned()},
                {"merged", modelRef->isMerged()},
                {"importOptions", SerializeImportOptions(modelRef->getImportOptions())}
            };
        }

        if (auto* renderer = entity->getComponent<MeshRenderer>()) {
            json materials = json::array();
            const auto& mats = renderer->getMaterials();
            for (const auto& mat : mats) {
                if (mat) {
                    materials.push_back(SerializeMaterial(*mat));
                } else {
                    materials.push_back(json::object());
                }
            }
            components["MeshRenderer"] = {
                {"castShadows", renderer->getCastShadows()},
                {"receiveShadows", renderer->getReceiveShadows()},
                {"materials", materials}
            };
        }

        if (auto* skinned = entity->getComponent<SkinnedMeshRenderer>()) {
            json skinnedData = {
                {"clipIndex", skinned->getActiveClipIndex()},
                {"playing", skinned->isPlaying()},
                {"looping", skinned->isLooping()},
                {"speed", skinned->getPlaybackSpeed()},
                {"time", skinned->getTimeSeconds()},
                {"rootMotionEnabled", skinned->getRootMotionEnabled()},
                {"rootMotionPosition", skinned->getApplyRootMotionPosition()},
                {"rootMotionRotation", skinned->getApplyRootMotionRotation()}
            };
            const auto& clips = skinned->getAnimationClips();
            json clipEvents = json::array();
            for (size_t i = 0; i < clips.size(); ++i) {
                if (!clips[i]) continue;
                const auto& events = clips[i]->getEvents();
                if (events.empty()) continue;
                json eventsJson = json::array();
                for (const auto& evt : events) {
                    eventsJson.push_back({
                        {"time", evt.time},
                        {"name", evt.name}
                    });
                }
                clipEvents.push_back({
                    {"clipIndex", static_cast<int>(i)},
                    {"events", eventsJson}
                });
            }
            if (!clipEvents.empty()) {
                skinnedData["clipEvents"] = clipEvents;
            }
            components["SkinnedMeshRenderer"] = skinnedData;
        }

        if (auto* hlod = entity->getComponent<HLODProxy>()) {
            json hlodData = {
                {"enabled", hlod->isEnabled()},
                {"lodStart", hlod->getLodStart()},
                {"lodEnd", hlod->getLodEnd()},
                {"sources", hlod->getSourceUuids()}
            };

            if (auto* renderer = entity->getComponent<MeshRenderer>()) {
                if (auto mesh = renderer->getMesh()) {
                    hlodData["mesh"] = SerializeMeshData(*mesh);
                }
            }

            components["HLODProxy"] = hlodData;
        }

        if (auto* animator = entity->getComponent<Animator>()) {
            json animatorData = {
                {"stateIndex", animator->getCurrentStateIndex()},
                {"blendDuration", animator->getDefaultBlendDuration()},
                {"autoPlay", animator->getAutoPlay()},
                {"rootMotion", {
                    {"enabled", animator->getRootMotionEnabled()},
                    {"applyPosition", animator->getApplyRootMotionPosition()},
                    {"applyRotation", animator->getApplyRootMotionRotation()}
                }}
            };

            if (!animator->getParameters().empty()) {
                json params = json::array();
                for (const auto& p : animator->getParameters()) {
                    json entry = {
                        {"name", p.name},
                        {"type", AnimatorParamTypeToString(p.type)},
                        {"float", p.floatValue},
                        {"int", p.intValue},
                        {"bool", p.boolValue}
                    };
                    params.push_back(entry);
                }
                animatorData["parameters"] = params;
            }

            if (!animator->getStates().empty()) {
                json states = json::array();
                for (const auto& s : animator->getStates()) {
                    json entry = {
                        {"name", s.name},
                        {"type", AnimatorStateTypeToString(s.type)},
                        {"clipIndex", s.clipIndex},
                        {"blendTree", s.blendTreeIndex},
                        {"speed", s.speed},
                        {"loop", s.loop}
                    };
                    states.push_back(entry);
                }
                animatorData["states"] = states;
            }

            if (!animator->getBlendTrees().empty()) {
                json trees = json::array();
                for (const auto& tree : animator->getBlendTrees()) {
                    json motions = json::array();
                    for (const auto& motion : tree.motions) {
                        motions.push_back({
                            {"clipIndex", motion.clipIndex},
                            {"threshold", motion.threshold}
                        });
                    }
                    trees.push_back({
                        {"name", tree.name},
                        {"type", AnimatorBlendTypeToString(tree.type)},
                        {"parameter", tree.parameter},
                        {"motions", motions}
                    });
                }
                animatorData["blendTrees"] = trees;
            }

            if (!animator->getTransitions().empty()) {
                json transitions = json::array();
                for (const auto& t : animator->getTransitions()) {
                    json conditions = json::array();
                    for (const auto& c : t.conditions) {
                        conditions.push_back({
                            {"parameter", c.parameter},
                            {"op", AnimatorCondOpToString(c.op)},
                            {"threshold", c.threshold},
                            {"intThreshold", c.intThreshold},
                            {"boolThreshold", c.boolThreshold}
                        });
                    }
                    transitions.push_back({
                        {"from", t.fromState},
                        {"to", t.toState},
                        {"duration", t.duration},
                        {"hasExitTime", t.hasExitTime},
                        {"exitTime", t.exitTime},
                        {"fixedDuration", t.fixedDuration},
                        {"conditions", conditions}
                    });
                }
                animatorData["transitions"] = transitions;
            }

            components["Animator"] = animatorData;
        }

        if (auto* ik = entity->getComponent<IKConstraint>()) {
            components["IKConstraint"] = {
                {"root", ik->getRootBone()},
                {"mid", ik->getMidBone()},
                {"end", ik->getEndBone()},
                {"target", Vec3ToJson(ik->getTargetPosition())},
                {"world", ik->getTargetInWorld()},
                {"weight", ik->getWeight()}
            };
        }

        if (auto* rb = entity->getComponent<Rigidbody>()) {
            components["Rigidbody"] = {
                {"type", RigidbodyTypeToString(rb->getType())},
                {"mass", rb->getMass()},
                {"linearDamping", rb->getLinearDamping()},
                {"angularDamping", rb->getAngularDamping()},
                {"useGravity", rb->getUseGravity()},
                {"continuous", rb->getContinuousCollision()},
                {"allowSleep", rb->getAllowSleep()}
            };
        }

        if (auto* collider = entity->getComponent<PhysicsCollider>()) {
            components["PhysicsCollider"] = {
                {"shape", ColliderShapeToString(collider->getShapeType())},
                {"size", Vec3ToJson(collider->getSize())},
                {"radius", collider->getRadius()},
                {"height", collider->getHeight()},
                {"center", Vec3ToJson(collider->getCenter())},
                {"trigger", collider->isTrigger()},
                {"friction", collider->getFriction()},
                {"restitution", collider->getRestitution()},
                {"frictionCombine", CombineModeToString(collider->getFrictionCombine())},
                {"restitutionCombine", CombineModeToString(collider->getRestitutionCombine())},
                {"collisionLayer", collider->getCollisionLayer()},
                {"collisionMask", collider->getCollisionMask()}
            };
        }

        if (auto* health = entity->getComponent<Health>()) {
            components["Health"] = {
                {"max", health->getMaxHealth()},
                {"current", health->getCurrentHealth()},
                {"destroyOnDeath", health->getDestroyOnDeath()}
            };
        }

        if (auto* controller = entity->getComponent<CharacterController>()) {
            components["CharacterController"] = {
                {"radius", controller->getRadius()},
                {"height", controller->getHeight()},
                {"skinWidth", controller->getSkinWidth()},
                {"moveSpeed", controller->getMoveSpeed()},
                {"acceleration", controller->getAcceleration()},
                {"airAcceleration", controller->getAirAcceleration()},
                {"jumpSpeed", controller->getJumpSpeed()},
                {"gravity", controller->getGravity()},
                {"maxFallSpeed", controller->getMaxFallSpeed()},
                {"groundSnapSpeed", controller->getGroundSnapSpeed()},
                {"stepOffset", controller->getStepOffset()},
                {"slopeLimit", controller->getSlopeLimit()},
                {"slopeSlideSpeed", controller->getSlopeSlideSpeed()},
                {"groundCheckDistance", controller->getGroundCheckDistance()},
                {"useInput", controller->getUseInput()},
                {"useGravity", controller->getUseGravity()},
                {"enableStep", controller->getEnableStep()},
                {"enableSlopeLimit", controller->getEnableSlopeLimit()},
                {"snapToGround", controller->getSnapToGround()},
                {"collisionMask", controller->getCollisionMask()}
            };
        }

        if (auto* controller = entity->getComponent<FirstPersonController>()) {
            components["FirstPersonController"] = {
                {"mouseSensitivity", controller->getMouseSensitivity()},
                {"invertY", controller->getInvertY()},
                {"requireLookButton", controller->getRequireLookButton()},
                {"lookButton", static_cast<int>(controller->getLookButton())},
                {"minPitch", controller->getMinPitch()},
                {"maxPitch", controller->getMaxPitch()},
                {"walkSpeed", controller->getWalkSpeed()},
                {"sprintMultiplier", controller->getSprintMultiplier()},
                {"enableSprint", controller->getEnableSprint()},
                {"enableCrouch", controller->getEnableCrouch()},
                {"crouchHeight", controller->getCrouchHeight()},
                {"crouchEyeHeight", controller->getCrouchEyeHeight()},
                {"crouchSpeed", controller->getCrouchSpeed()},
                {"eyeHeight", controller->getEyeHeight()},
                {"useEyeHeight", controller->getUseEyeHeight()},
                {"driveCharacterController", controller->getDriveCharacterController()},
                {"fireCooldown", controller->getFireCooldown()},
                {"fireDamage", controller->getFireDamage()},
                {"fireRange", controller->getFireRange()},
                {"fireMask", controller->getFireHitMask()},
                {"fireHitTriggers", controller->getFireHitTriggers()}
            };
            json muzzleRef = SerializeAssetPath(controller->getMuzzleTexturePath(), "texture");
            if (!muzzleRef.is_null() && !muzzleRef.empty()) {
                components["FirstPersonController"]["muzzleTexture"] = muzzleRef;
            }
        }

        if (auto* audio = entity->getComponent<AudioSource>()) {
            json audioData = {
                {"volume", audio->getVolume()},
                {"pitch", audio->getPitch()},
                {"looping", audio->isLooping()},
                {"playOnStart", audio->getPlayOnStart()},
                {"spatial", audio->isSpatial()},
                {"stream", audio->isStreaming()},
                {"minDistance", audio->getMinDistance()},
                {"maxDistance", audio->getMaxDistance()},
                {"rolloff", audio->getRolloff()}
            };
            json pathRef = SerializeAssetPath(audio->getFilePath(), "audio");
            if (!pathRef.is_null() && !pathRef.empty()) {
                audioData["file"] = pathRef;
            } else if (!audio->getFilePath().empty()) {
                audioData["file"] = audio->getFilePath();
            }
            components["AudioSource"] = audioData;
        }

        if (auto* light = entity->getComponent<Light>()) {
            components["Light"] = {
                {"type", static_cast<int>(light->getType())},
                {"color", Vec3ToJson(light->getColor())},
                {"temperatureK", light->getColorTemperature()},
                {"intensity", light->getIntensity()},
                {"intensityUnit", static_cast<int>(light->getIntensityUnit())},
                {"range", light->getRange()},
                {"spotAngle", light->getSpotAngle()},
                {"innerSpotAngle", light->getInnerSpotAngle()},
                {"areaSize", Vec2ToJson(light->getAreaSize())},
                {"sourceRadius", light->getSourceRadius()},
                {"sourceLength", light->getSourceLength()},
                {"falloff", static_cast<int>(light->getFalloffModel())},
                {"castsShadows", light->getCastShadows()},
                {"softShadows", light->getSoftShadows()},
                {"shadowResolution", light->getShadowMapResolution()},
                {"shadowBias", light->getShadowBias()},
                {"shadowNormalBias", light->getShadowNormalBias()},
                {"shadowNearPlane", light->getShadowNearPlane()},
                {"shadowFarPlane", light->getShadowFarPlane()},
                {"penumbra", light->getPenumbra()},
                {"contactShadows", light->getContactShadows()},
                {"cascadeCount", light->getCascadeCount()},
                {"cascadeSplits", json::array({light->getCascadeSplits()[0], light->getCascadeSplits()[1], light->getCascadeSplits()[2], light->getCascadeSplits()[3]})},
                {"volumetric", light->getVolumetric()}
            };
        }

        if (auto* decal = entity->getComponent<Decal>()) {
            json decalData;
            decalData["color"] = Vec4ToJson(decal->getTint());
            decalData["opacity"] = decal->getOpacity();
            decalData["tiling"] = Vec2ToJson(decal->getTiling());
            decalData["offset"] = Vec2ToJson(decal->getOffset());
            decalData["softness"] = decal->getEdgeSoftness();

            json albedoRef = SerializeTextureRef(decal->getAlbedoTexture(), "texture");
            if (!albedoRef.is_null() && !albedoRef.empty()) {
                decalData["albedo"] = albedoRef;
            } else if (!decal->getAlbedoPath().empty()) {
                json pathRef = SerializeAssetPath(decal->getAlbedoPath(), "texture");
                if (!pathRef.is_null() && !pathRef.empty()) {
                    decalData["albedo"] = pathRef;
                }
            }
            json normalRef = SerializeTextureRef(decal->getNormalTexture(), "texture");
            if (!normalRef.is_null() && !normalRef.empty()) {
                decalData["normal"] = normalRef;
            } else if (!decal->getNormalPath().empty()) {
                json pathRef = SerializeAssetPath(decal->getNormalPath(), "texture");
                if (!pathRef.is_null() && !pathRef.empty()) {
                    decalData["normal"] = pathRef;
                }
            }
            json ormRef = SerializeTextureRef(decal->getORMTexture(), "texture");
            if (!ormRef.is_null() && !ormRef.empty()) {
                decalData["orm"] = ormRef;
            } else if (!decal->getORMPath().empty()) {
                json pathRef = SerializeAssetPath(decal->getORMPath(), "texture");
                if (!pathRef.is_null() && !pathRef.empty()) {
                    decalData["orm"] = pathRef;
                }
            }
            json maskRef = SerializeTextureRef(decal->getMaskTexture(), "texture");
            if (!maskRef.is_null() && !maskRef.empty()) {
                decalData["mask"] = maskRef;
            } else if (!decal->getMaskPath().empty()) {
                json pathRef = SerializeAssetPath(decal->getMaskPath(), "texture");
                if (!pathRef.is_null() && !pathRef.empty()) {
                    decalData["mask"] = pathRef;
                }
            }
            components["Decal"] = decalData;
        }

        if (auto* camera = entity->getComponent<Camera>()) {
            components["Camera"] = {
                {"projection", static_cast<int>(camera->getProjectionType())},
                {"fov", camera->getFieldOfView()},
                {"orthoSize", camera->getOrthographicSize()},
                {"nearClip", camera->getNearClip()},
                {"farClip", camera->getFarClip()},
                {"aspect", camera->getAspectRatio()},
                {"viewport", Vec4ToJson(camera->getViewport())},
                {"clearColor", Vec4ToJson(camera->getClearColor())},
                {"clearDepth", camera->getClearDepth()},
                {"editorCamera", camera->isEditorCamera()}
            };
        }
        
        if (auto* controller = entity->getComponent<CameraController>()) {
            components["CameraController"] = {
                {"moveSpeed", controller->getMoveSpeed()},
                {"rotationSpeed", controller->getRotationSpeed()}
            };
        }

        e["components"] = components;
        entities.push_back(e);
    }

    root["entities"] = entities;

    if (includeAssetRoot) {
        AssetDatabase& db = AssetDatabase::getInstance();
        std::string rootPath = db.getRootPath();
        if (!rootPath.empty()) {
            json assetRoot;
            assetRoot["path"] = rootPath;
            if (!scenePath.empty()) {
                std::filesystem::path sceneDir = std::filesystem::path(scenePath).parent_path();
                std::error_code ec;
                std::filesystem::path rel = std::filesystem::relative(rootPath, sceneDir, ec);
                if (!ec) {
                    assetRoot["path"] = rel.generic_string();
                    assetRoot["relativeToScene"] = true;
                }
            }
            root["assetRoot"] = assetRoot;
        }
    }

    SceneSettings settings = scene->getSettings();
    Renderer* renderer = Engine::getInstance().getRenderer();
    if (renderer) {
        settings.environment = EnvironmentFromRenderer(renderer);
        scene->setSettings(settings);
    }
    json sceneSettings;
    sceneSettings["environment"] = SerializeEnvironmentSettings(settings.environment);
    sceneSettings["fog"] = SerializeFogSettings(settings.fog);
    sceneSettings["postProcess"] = SerializePostProcessSettings(settings.postProcess);
    sceneSettings["quality"] = SerializeQualitySettings(settings.quality);
    root["sceneSettings"] = sceneSettings;

    return root;
}

} // namespace

std::string SceneSerializer::SerializeScene(Scene* scene) {
    json root = BuildSceneJson(scene, false, "", true);
    return root.dump(2);
}

std::string SceneSerializer::SerializeScene(Scene* scene, const std::string& scenePath) {
    json root = BuildSceneJson(scene, true, scenePath, true);
    return root.dump(2);
}

std::string SceneSerializer::SerializeScene(Scene* scene, bool includeEditorOnly) {
    json root = BuildSceneJson(scene, false, "", includeEditorOnly);
    return root.dump(2);
}

bool SceneSerializer::DeserializeScene(Scene* scene, const std::string& data) {
    return DeserializeScene(scene, data, "");
}

bool SceneSerializer::DeserializeScene(Scene* scene, const std::string& data, const std::string& scenePath) {
    if (!scene) {
        return false;
    }

    json root = json::parse(data, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return false;
    }

    if (root.contains("assetRoot")) {
        AssetDatabase& db = AssetDatabase::getInstance();
        std::string rootPath;
        const json& assetRoot = root["assetRoot"];
        if (assetRoot.is_object()) {
            rootPath = assetRoot.value("path", std::string());
            bool relative = assetRoot.value("relativeToScene", false);
            if (relative && !scenePath.empty()) {
                std::filesystem::path sceneDir = std::filesystem::path(scenePath).parent_path();
                rootPath = (sceneDir / rootPath).lexically_normal().string();
            }
        } else if (assetRoot.is_string()) {
            rootPath = assetRoot.get<std::string>();
            if (!rootPath.empty() && !scenePath.empty()) {
                std::filesystem::path p(rootPath);
                if (!p.is_absolute()) {
                    std::filesystem::path sceneDir = std::filesystem::path(scenePath).parent_path();
                    rootPath = (sceneDir / p).lexically_normal().string();
                }
            }
        }
        if (!rootPath.empty()) {
            std::error_code ec;
            if (std::filesystem::exists(rootPath, ec)) {
                db.setRootPath(rootPath);
            }
        }
    }

    SceneSettings sceneSettings;
    if (root.contains("sceneSettings") && root["sceneSettings"].is_object()) {
        const json& s = root["sceneSettings"];
        if (s.contains("environment")) {
            sceneSettings.environment = DeserializeEnvironmentSettings(s["environment"]);
        }
        if (s.contains("fog")) {
            sceneSettings.fog = DeserializeFogSettings(s["fog"]);
        }
        if (s.contains("postProcess")) {
            sceneSettings.postProcess = DeserializePostProcessSettings(s["postProcess"]);
        }
        if (s.contains("quality")) {
            sceneSettings.quality = DeserializeQualitySettings(s["quality"]);
        }
    }
    scene->setSettings(sceneSettings);

    bool wasActive = scene->isActive();
    if (wasActive) {
        scene->setActive(false);
    }

    scene->destroyAllEntities();
    if (root.contains("name")) {
        scene->setName(root.value("name", scene->getName()));
    }

    auto records = BuildEntityRecords(scene,
                                      root.contains("entities") && root["entities"].is_array()
                                          ? root["entities"]
                                          : json::array(),
                                      true);
    ResolveEntityParents(scene, records);

    Renderer* renderer = Engine::getInstance().getRenderer();
    TextureLoader* textureLoader = renderer ? renderer->getTextureLoader() : nullptr;
    std::unordered_map<std::string, ModelCacheEntry> modelCache;

    for (auto& [uuid, record] : records) {
        if (!record.entity) {
            continue;
        }
        record.entity->setEditorOnly(record.editorOnly);
        ApplyEntityComponents(record.entity, record.components, scene, scenePath, textureLoader, modelCache);
    }

    if (wasActive) {
        scene->setActive(true);
    }

    for (auto& [uuid, record] : records) {
        if (record.entity && !record.active) {
            record.entity->setActive(false);
        }
    }

    return true;
}

std::vector<Entity*> SceneSerializer::DuplicateEntities(Scene* scene, const std::vector<Entity*>& entities) {
    using json = nlohmann::json;
    std::vector<Entity*> duplicates;
    if (!scene || entities.empty()) {
        return duplicates;
    }

    std::unordered_set<UUID> selectionRoots;
    selectionRoots.reserve(entities.size());
    for (Entity* entity : entities) {
        if (entity) {
            selectionRoots.insert(entity->getUUID());
        }
    }

    std::vector<Entity*> rootEntities;
    rootEntities.reserve(entities.size());
    for (Entity* entity : entities) {
        if (!entity) {
            continue;
        }
        Transform* parent = entity->getTransform() ? entity->getTransform()->getParent() : nullptr;
        bool hasAncestorSelected = false;
        while (parent) {
            Entity* parentEntity = parent->getEntity();
            if (parentEntity && selectionRoots.count(parentEntity->getUUID()) > 0) {
                hasAncestorSelected = true;
                break;
            }
            parent = parent->getParent();
        }
        if (!hasAncestorSelected) {
            rootEntities.push_back(entity);
        }
    }

    std::unordered_set<std::string> selection;
    for (Entity* root : rootEntities) {
        Transform* rootTransform = root ? root->getTransform() : nullptr;
        if (!rootTransform) {
            continue;
        }
        std::vector<Transform*> stack;
        stack.push_back(rootTransform);
        while (!stack.empty()) {
            Transform* node = stack.back();
            stack.pop_back();
            if (!node) {
                continue;
            }
            Entity* nodeEntity = node->getEntity();
            if (nodeEntity) {
                selection.insert(nodeEntity->getUUID().toString());
            }
            const auto& children = node->getChildren();
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                stack.push_back(*it);
            }
        }
    }

    if (selection.empty()) {
        return duplicates;
    }

    json rootJson = json::parse(SerializeScene(scene, true), nullptr, false);
    if (rootJson.is_discarded() || !rootJson.contains("entities") || !rootJson["entities"].is_array()) {
        return duplicates;
    }

    json subset = json::array();
    for (const auto& entry : rootJson["entities"]) {
        if (!entry.is_object()) {
            continue;
        }
        std::string uuidStr = entry.value("uuid", std::string());
        if (selection.count(uuidStr) > 0) {
            subset.push_back(entry);
        }
    }

    auto records = BuildEntityRecords(scene, subset, false);
    ResolveEntityParents(scene, records);

    Renderer* renderer = Engine::getInstance().getRenderer();
    TextureLoader* textureLoader = renderer ? renderer->getTextureLoader() : nullptr;
    std::unordered_map<std::string, ModelCacheEntry> modelCache;
    for (auto& [uuid, record] : records) {
        if (!record.entity) {
            continue;
        }
        record.entity->setEditorOnly(record.editorOnly);
        ApplyEntityComponents(record.entity, record.components, scene, "", textureLoader, modelCache);
    }

    for (auto& [uuid, record] : records) {
        if (record.entity && !record.active) {
            record.entity->setActive(false);
        }
    }

    for (Entity* root : rootEntities) {
        if (!root) {
            continue;
        }
        std::string key = root->getUUID().toString();
        auto it = records.find(key);
        if (it != records.end() && it->second.entity) {
            duplicates.push_back(it->second.entity);
        }
    }

    return duplicates;
}

} // namespace Crescent
