#include "SceneSerializer.hpp"
#include "SceneCommands.hpp"
#include "../Assets/AssetDatabase.hpp"
#include "../Core/Engine.hpp"
#include "../Project/Project.hpp"
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
#include <chrono>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#if __has_include("../../../ThirdParty/meshoptimizer/src/meshoptimizer.h")
#define CRESCENT_HAS_MESHOPTIMIZER 1
#include "../../../ThirdParty/meshoptimizer/src/meshoptimizer.h"
#include "../../../ThirdParty/meshoptimizer/src/allocator.cpp"
#include "../../../ThirdParty/meshoptimizer/src/indexcodec.cpp"
#include "../../../ThirdParty/meshoptimizer/src/indexgenerator.cpp"
#include "../../../ThirdParty/meshoptimizer/src/overdrawoptimizer.cpp"
#include "../../../ThirdParty/meshoptimizer/src/quantization.cpp"
#include "../../../ThirdParty/meshoptimizer/src/vcacheoptimizer.cpp"
#include "../../../ThirdParty/meshoptimizer/src/vertexcodec.cpp"
#include "../../../ThirdParty/meshoptimizer/src/vfetchoptimizer.cpp"
#else
#define CRESCENT_HAS_MESHOPTIMIZER 0
#endif

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

std::string FormatUTCNowISO8601() {
    using clock = std::chrono::system_clock;
    std::time_t now = clock::to_time_t(clock::now());
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    std::ostringstream stream;
    stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

std::string NormalizePathString(const std::string& path) {
    if (path.empty()) {
        return "";
    }
    std::error_code ec;
    std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
    if (ec) {
        normalized = std::filesystem::path(path).lexically_normal();
    }
    return normalized.generic_string();
}

std::string SanitizeFileComponent(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (char c : value) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        } else if (c == '_' || c == '-') {
            result.push_back(c);
        } else if (std::isspace(static_cast<unsigned char>(c)) || c == '.') {
            result.push_back('_');
        }
    }
    while (!result.empty() && result.front() == '_') {
        result.erase(result.begin());
    }
    while (!result.empty() && result.back() == '_') {
        result.pop_back();
    }
    return result.empty() ? "scene" : result;
}

std::string GetProjectRootPath() {
    if (auto project = ProjectManager::getInstance().getActiveProject()) {
        return NormalizePathString(project->getRootPath());
    }

    AssetDatabase& db = AssetDatabase::getInstance();
    if (!db.getLibraryPath().empty()) {
        return NormalizePathString(std::filesystem::path(db.getLibraryPath()).parent_path().string());
    }
    if (!db.getRootPath().empty()) {
        return NormalizePathString(std::filesystem::path(db.getRootPath()).parent_path().string());
    }
    return "";
}

std::string MakeProjectRelativePath(const std::string& path) {
    if (path.empty()) {
        return "";
    }

    std::string projectRoot = GetProjectRootPath();
    if (projectRoot.empty()) {
        return std::filesystem::path(path).lexically_normal().generic_string();
    }

    std::filesystem::path normalizedPath(NormalizePathString(path));
    std::error_code ec;
    std::filesystem::path relative = std::filesystem::relative(normalizedPath, std::filesystem::path(projectRoot), ec);
    if (!ec && !relative.empty() && !relative.is_absolute() && relative.native().front() != '.') {
        return relative.generic_string();
    }
    return normalizedPath.generic_string();
}

std::string ResolveProjectPath(const std::string& path) {
    if (path.empty()) {
        return "";
    }
    std::filesystem::path stored(path);
    if (stored.is_absolute()) {
        return NormalizePathString(stored.string());
    }
    std::string projectRoot = GetProjectRootPath();
    if (projectRoot.empty()) {
        return stored.lexically_normal().generic_string();
    }
    return NormalizePathString((std::filesystem::path(projectRoot) / stored).string());
}

std::string ResolveStaticLightingOutputDirectory(const SceneStaticLightingSettings& settings) {
    if (settings.outputDirectory.empty()) {
        return ResolveProjectPath("Library/BakedLighting");
    }
    return ResolveProjectPath(settings.outputDirectory);
}

std::string ResolveSceneStem(const Scene* scene, const std::string& scenePath) {
    std::filesystem::path sourcePath(scenePath);
    std::string stem = sourcePath.stem().string();
    if (stem.empty() && scene) {
        stem = SanitizeFileComponent(scene->getName());
    }
    return stem.empty() ? "scene" : stem;
}

std::string BuildStaticLightingArtifactPath(const Scene* scene,
                                            const std::string& scenePath,
                                            int atlasIndex,
                                            const char* suffix) {
    if (atlasIndex < 0 || !suffix) {
        return "";
    }
    std::filesystem::path outputDir = ResolveStaticLightingOutputDirectory(scene->getSettings().staticLighting);
    std::ostringstream name;
    name << ResolveSceneStem(scene, scenePath) << "_atlas_" << std::setw(3) << std::setfill('0') << atlasIndex << suffix;
    return NormalizePathString((outputDir / name.str()).string());
}

bool ShouldPersistStaticLightingManifest(Scene* scene) {
    if (!scene) {
        return false;
    }

    const SceneStaticLightingSettings& settings = scene->getSettings().staticLighting;
    if (settings.enabled || !settings.bakeManifestPath.empty() || !settings.lastBakeHash.empty()) {
        return true;
    }

    for (const auto& entityHandle : scene->getAllEntities()) {
        Entity* entity = entityHandle.get();
        if (!entity) {
            continue;
        }
        if (auto* renderer = entity->getComponent<MeshRenderer>()) {
            if (renderer->hasStaticLightingData()) {
                return true;
            }
        }
    }

    return false;
}

json SerializeProjectPathRef(const std::string& path) {
    if (path.empty()) {
        return json();
    }

    std::string stored = MakeProjectRelativePath(path);
    if (stored.empty()) {
        return json();
    }
    return json{
        {"path", stored},
        {"projectRelative", true}
    };
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

json MatrixToJson(const Math::Matrix4x4& matrix) {
    json values = json::array();
    for (float value : matrix.m) {
        values.push_back(value);
    }
    return values;
}

Math::Matrix4x4 JsonToMatrix(const json& j, const Math::Matrix4x4& fallback = Math::Matrix4x4::Identity) {
    if (!j.is_array() || j.size() < 16) {
        return fallback;
    }
    Math::Matrix4x4 matrix = fallback;
    for (size_t i = 0; i < 16; ++i) {
        matrix.m[i] = j[i].get<float>();
    }
    return matrix;
}

struct BuildSceneOptions {
    bool includeAssetRoot = false;
    bool includeEditorOnly = true;
    bool embedRuntimePayloads = false;
    bool externalizeRuntimeMeshes = false;
    std::string cookedScenePath;
    struct CookedMeshWriter* cookedMeshWriter = nullptr;
};

json BuildSceneJson(Scene* scene, const std::string& scenePath, const BuildSceneOptions& options);
json SerializeMeshData(const Mesh& mesh);
std::shared_ptr<Mesh> DeserializeMeshData(const json& j);

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

bool ShouldSerializeCustomizedPrimitiveMesh(const Entity* entity,
                                            const MeshRenderer* renderer) {
    if (!entity || !renderer) {
        return false;
    }
    const auto* primitive = entity->getComponent<PrimitiveMesh>();
    if (!primitive) {
        return false;
    }
    auto mesh = renderer->getMesh();
    if (!mesh) {
        return false;
    }

    switch (primitive->getType()) {
        case PrimitiveType::Plane: {
            const auto& vertices = mesh->getVertices();
            const auto& indices = mesh->getIndices();
            if (vertices.size() != 4 || indices.size() != 6) {
                return true;
            }
            for (const auto& vertex : vertices) {
                if (std::abs(vertex.position.y) > 1e-4f) {
                    return true;
                }
            }
            return false;
        }
        default:
            return false;
    }
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

std::string HashRuntimeCookKey(const std::string& input) {
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : input) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ull;
    }
    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << hash;
    return oss.str();
}

std::string NormalizeEmbeddedCookKey(const std::string& path) {
    const std::string marker = "#embedded:";
    size_t markerPos = path.find(marker);
    if (markerPos == std::string::npos || markerPos == 0) {
        return path;
    }

    std::string sourcePath = path.substr(0, markerPos);
    std::string suffix = path.substr(markerPos);

    AssetDatabase& db = AssetDatabase::getInstance();
    std::error_code ec;
    std::filesystem::path canonical = std::filesystem::weakly_canonical(sourcePath, ec);
    std::string normalizedSource = ec ? std::filesystem::path(sourcePath).lexically_normal().generic_string()
                                      : canonical.generic_string();

    std::string rootPath = db.getRootPath();
    if (!rootPath.empty()) {
        std::filesystem::path root(rootPath);
        std::filesystem::path source(normalizedSource);
        std::filesystem::path relative = std::filesystem::relative(source, root, ec);
        if (!ec && !relative.empty() && relative.native().front() != '.') {
            normalizedSource = relative.generic_string();
        }
    }

    return normalizedSource + suffix;
}

std::string BuildCookedTextureRelativePath(const std::string& path) {
    if (path.empty()) {
        return "";
    }

    if (path.find("#embedded:") != std::string::npos) {
        return "Library/ImportCache/embedded_" + HashRuntimeCookKey(NormalizeEmbeddedCookKey(path)) + "_v2a.ktx2";
    }

    AssetDatabase& db = AssetDatabase::getInstance();
    std::string guid = db.getGuidForPath(path);
    if (guid.empty()) {
        guid = db.registerAsset(path, "texture");
    }
    if (guid.empty()) {
        return "";
    }
    return "Library/ImportCache/" + guid + "_v2a.ktx2";
}

std::string BuildCookedStaticLightmapRelativePath(const std::string& path) {
    if (path.empty()) {
        return "";
    }

    std::filesystem::path lightmapPath(path);
    std::string ext = lightmapPath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (ext != ".exr" && ext != ".hdr") {
        return "";
    }

    std::string normalized = MakeProjectRelativePath(path);
    if (normalized.empty()) {
        normalized = lightmapPath.lexically_normal().generic_string();
    }
    return "Library/ImportCache/lightmap_rgbm_" + HashRuntimeCookKey(normalized) + "_v1.ktx2";
}

std::string NormalizeCookedEnvironmentKey(const std::string& path) {
    if (path.empty()) {
        return path;
    }

    AssetDatabase& db = AssetDatabase::getInstance();
    std::error_code ec;
    std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
    std::string normalized = ec ? std::filesystem::path(path).lexically_normal().generic_string()
                                : canonical.generic_string();

    std::string rootPath = db.getRootPath();
    if (!rootPath.empty()) {
        std::filesystem::path root(rootPath);
        std::filesystem::path source(normalized);
        std::filesystem::path relative = std::filesystem::relative(source, root, ec);
        if (!ec && !relative.empty() && relative.native().front() != '.') {
            normalized = relative.generic_string();
        }
    }

    return normalized;
}

std::string BuildCookedEnvironmentRelativePath(const std::string& path) {
    if (path.empty() || path == "Builtin Sky") {
        return "";
    }
    return "Library/ImportCache/hdri_" + HashRuntimeCookKey(NormalizeCookedEnvironmentKey(path)) + "_v1.cenv";
}

struct CookedMeshWriter {
    std::filesystem::path sceneOutputPath;
    std::unordered_map<std::string, std::string> emittedPaths;
};

class CookedMeshBinaryWriter {
public:
    void writeU32(uint32_t value) {
        for (int i = 0; i < 4; ++i) {
            m_bytes.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xffu));
        }
    }

    void writeF32(float value) {
        uint32_t bits = 0;
        static_assert(sizeof(float) == sizeof(uint32_t), "Expected 32-bit float");
        std::memcpy(&bits, &value, sizeof(uint32_t));
        writeU32(bits);
    }

    void writeBytes(const void* data, size_t size) {
        if (!data || size == 0) {
            return;
        }
        const uint8_t* src = static_cast<const uint8_t*>(data);
        m_bytes.insert(m_bytes.end(), src, src + size);
    }

    void writeString(const std::string& value) {
        writeU32(static_cast<uint32_t>(value.size()));
        writeBytes(value.data(), value.size());
    }

    const std::vector<uint8_t>& bytes() const {
        return m_bytes;
    }

private:
    std::vector<uint8_t> m_bytes;
};

std::vector<uint8_t> SerializeCookedMeshBinary(const Mesh& mesh) {
#if CRESCENT_HAS_MESHOPTIMIZER
    struct OptimizedCookedMeshData {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<Submesh> submeshes;
        std::vector<SkinWeight> skinWeights;
    };

    struct QuantizedVertex {
        uint16_t position[3];
        int16_t normal[3];
        uint16_t texCoord[2];
        uint16_t texCoord1[2];
        int16_t tangent[3];
        int16_t bitangent[3];
        uint8_t color[4];
    };

    auto quantizePosition = [](float value, float minValue, float maxValue) -> uint16_t {
        float extent = maxValue - minValue;
        if (extent <= 1e-8f) {
            return 0;
        }
        float normalized = (value - minValue) / extent;
        return static_cast<uint16_t>(meshopt_quantizeUnorm(normalized, 16));
    };

    auto quantizeSnorm16 = [](float value) -> int16_t {
        return static_cast<int16_t>(meshopt_quantizeSnorm(value, 16));
    };

    auto quantizeColor8 = [](float value) -> uint8_t {
        return static_cast<uint8_t>(meshopt_quantizeUnorm(value, 8));
    };

    auto optimizeRange = [](std::vector<uint32_t>& indices,
                            size_t start,
                            size_t count,
                            const std::vector<Vertex>& vertices) {
        if (count < 3 || (count % 3) != 0 || start + count > indices.size() || vertices.empty()) {
            return;
        }

        std::vector<uint32_t> cacheOptimized(count);
        meshopt_optimizeVertexCache(cacheOptimized.data(),
                                    indices.data() + start,
                                    count,
                                    vertices.size());

        std::vector<uint32_t> overdrawOptimized(count);
        meshopt_optimizeOverdraw(overdrawOptimized.data(),
                                 cacheOptimized.data(),
                                 count,
                                 &vertices[0].position.x,
                                 vertices.size(),
                                 sizeof(Vertex),
                                 1.05f);
        std::copy(overdrawOptimized.begin(), overdrawOptimized.end(), indices.begin() + static_cast<std::ptrdiff_t>(start));
    };

    auto buildOptimizedMesh = [&mesh, &optimizeRange]() -> OptimizedCookedMeshData {
        OptimizedCookedMeshData result{
            mesh.getVertices(),
            mesh.getIndices(),
            mesh.getSubmeshes(),
            mesh.getSkinWeights()
        };

        if (result.vertices.empty()) {
            return result;
        }

        if (!result.indices.empty()) {
            if (!result.submeshes.empty()) {
                for (const Submesh& submesh : result.submeshes) {
                    optimizeRange(result.indices, submesh.indexStart, submesh.indexCount, result.vertices);
                }
            } else {
                optimizeRange(result.indices, 0, result.indices.size(), result.vertices);
            }

            std::vector<unsigned int> dedupeRemap(result.vertices.size());
            size_t uniqueVertexCount = meshopt_generateVertexRemap(dedupeRemap.data(),
                                                                  result.indices.data(),
                                                                  result.indices.size(),
                                                                  result.vertices.data(),
                                                                  result.vertices.size(),
                                                                  sizeof(Vertex));
            if (uniqueVertexCount > 0 && uniqueVertexCount < result.vertices.size()) {
                std::vector<Vertex> dedupedVertices(uniqueVertexCount);
                meshopt_remapVertexBuffer(dedupedVertices.data(),
                                          result.vertices.data(),
                                          result.vertices.size(),
                                          sizeof(Vertex),
                                          dedupeRemap.data());

                std::vector<uint32_t> dedupedIndices(result.indices.size());
                meshopt_remapIndexBuffer(dedupedIndices.data(),
                                         result.indices.data(),
                                         result.indices.size(),
                                         dedupeRemap.data());

                result.vertices = std::move(dedupedVertices);
                result.indices = std::move(dedupedIndices);

                if (!result.skinWeights.empty()) {
                    std::vector<SkinWeight> dedupedWeights(uniqueVertexCount);
                    meshopt_remapVertexBuffer(dedupedWeights.data(),
                                              result.skinWeights.data(),
                                              result.skinWeights.size(),
                                              sizeof(SkinWeight),
                                              dedupeRemap.data());
                    result.skinWeights = std::move(dedupedWeights);
                }
            }

            std::vector<unsigned int> fetchRemap(result.vertices.size());
            size_t compactVertexCount = meshopt_optimizeVertexFetchRemap(fetchRemap.data(),
                                                                         result.indices.data(),
                                                                         result.indices.size(),
                                                                         result.vertices.size());
            if (compactVertexCount > 0 && compactVertexCount <= result.vertices.size()) {
                std::vector<Vertex> compactVertices(compactVertexCount);
                meshopt_remapVertexBuffer(compactVertices.data(),
                                          result.vertices.data(),
                                          result.vertices.size(),
                                          sizeof(Vertex),
                                          fetchRemap.data());

                std::vector<uint32_t> compactIndices(result.indices.size());
                meshopt_remapIndexBuffer(compactIndices.data(),
                                         result.indices.data(),
                                         result.indices.size(),
                                         fetchRemap.data());

                result.vertices = std::move(compactVertices);
                result.indices = std::move(compactIndices);

                if (!result.skinWeights.empty()) {
                    std::vector<SkinWeight> compactWeights(compactVertexCount);
                    meshopt_remapVertexBuffer(compactWeights.data(),
                                              result.skinWeights.data(),
                                              result.skinWeights.size(),
                                              sizeof(SkinWeight),
                                              fetchRemap.data());
                    result.skinWeights = std::move(compactWeights);
                }
            }
        }

        return result;
    };

    auto encodeVertexLikeBuffer = [](const void* data,
                                     size_t count,
                                     size_t stride,
                                     std::vector<uint8_t>& output) -> bool {
        if (!data || count == 0 || stride == 0) {
            output.clear();
            return true;
        }

        size_t bound = meshopt_encodeVertexBufferBound(count, stride);
        output.resize(bound);
        size_t encodedSize = meshopt_encodeVertexBuffer(output.data(), output.size(), data, count, stride);
        if (encodedSize == 0) {
            output.clear();
            return false;
        }
        output.resize(encodedSize);
        return true;
    };

    OptimizedCookedMeshData optimized = buildOptimizedMesh();
    std::vector<QuantizedVertex> quantizedVertices;
    quantizedVertices.reserve(optimized.vertices.size());
    const Math::Vector3 boundsMin = mesh.getBoundsMin();
    const Math::Vector3 boundsMax = mesh.getBoundsMax();
    for (const Vertex& vertex : optimized.vertices) {
        QuantizedVertex q{};
        q.position[0] = quantizePosition(vertex.position.x, boundsMin.x, boundsMax.x);
        q.position[1] = quantizePosition(vertex.position.y, boundsMin.y, boundsMax.y);
        q.position[2] = quantizePosition(vertex.position.z, boundsMin.z, boundsMax.z);
        q.normal[0] = quantizeSnorm16(vertex.normal.x);
        q.normal[1] = quantizeSnorm16(vertex.normal.y);
        q.normal[2] = quantizeSnorm16(vertex.normal.z);
        q.texCoord[0] = meshopt_quantizeHalf(vertex.texCoord.x);
        q.texCoord[1] = meshopt_quantizeHalf(vertex.texCoord.y);
        q.texCoord1[0] = meshopt_quantizeHalf(vertex.texCoord1.x);
        q.texCoord1[1] = meshopt_quantizeHalf(vertex.texCoord1.y);
        q.tangent[0] = quantizeSnorm16(vertex.tangent.x);
        q.tangent[1] = quantizeSnorm16(vertex.tangent.y);
        q.tangent[2] = quantizeSnorm16(vertex.tangent.z);
        q.bitangent[0] = quantizeSnorm16(vertex.bitangent.x);
        q.bitangent[1] = quantizeSnorm16(vertex.bitangent.y);
        q.bitangent[2] = quantizeSnorm16(vertex.bitangent.z);
        q.color[0] = quantizeColor8(vertex.color.x);
        q.color[1] = quantizeColor8(vertex.color.y);
        q.color[2] = quantizeColor8(vertex.color.z);
        q.color[3] = quantizeColor8(vertex.color.w);
        quantizedVertices.push_back(q);
    }

    std::vector<uint8_t> encodedVertices;
    std::vector<uint8_t> encodedIndices;
    std::vector<uint8_t> encodedSkinWeights;

    bool vertexEncoded = encodeVertexLikeBuffer(quantizedVertices.data(),
                                                quantizedVertices.size(),
                                                sizeof(QuantizedVertex),
                                                encodedVertices);
    bool skinWeightEncoded = encodeVertexLikeBuffer(optimized.skinWeights.data(),
                                                    optimized.skinWeights.size(),
                                                    sizeof(SkinWeight),
                                                    encodedSkinWeights);

    bool indexEncoded = true;
    if (!optimized.indices.empty()) {
        size_t bound = meshopt_encodeIndexBufferBound(optimized.indices.size(), optimized.vertices.size());
        encodedIndices.resize(bound);
        size_t encodedSize = meshopt_encodeIndexBuffer(encodedIndices.data(),
                                                       encodedIndices.size(),
                                                       optimized.indices.data(),
                                                       optimized.indices.size());
        if (encodedSize == 0) {
            indexEncoded = false;
            encodedIndices.clear();
        } else {
            encodedIndices.resize(encodedSize);
        }
    }

    if (vertexEncoded && skinWeightEncoded && indexEncoded) {
        CookedMeshBinaryWriter writer;
        writer.writeBytes("CMSH", 4);
        writer.writeU32(3);
        writer.writeU32(static_cast<uint32_t>(optimized.vertices.size()));
        writer.writeU32(static_cast<uint32_t>(optimized.indices.size()));
        writer.writeU32(static_cast<uint32_t>(optimized.submeshes.size()));
        writer.writeU32(static_cast<uint32_t>(optimized.skinWeights.size()));
        writer.writeU32(static_cast<uint32_t>(mesh.getName().size()));
        writer.writeU32(static_cast<uint32_t>(encodedVertices.size()));
        writer.writeU32(static_cast<uint32_t>(encodedIndices.size()));
        writer.writeU32(static_cast<uint32_t>(optimized.submeshes.size() * sizeof(Submesh)));
        writer.writeU32(static_cast<uint32_t>(encodedSkinWeights.size()));
        writer.writeU32(mesh.isDoubleSided() ? 1u : 0u);
        writer.writeF32(mesh.getBoundsMin().x);
        writer.writeF32(mesh.getBoundsMin().y);
        writer.writeF32(mesh.getBoundsMin().z);
        writer.writeF32(mesh.getBoundsMax().x);
        writer.writeF32(mesh.getBoundsMax().y);
        writer.writeF32(mesh.getBoundsMax().z);
        writer.writeBytes(mesh.getName().data(), mesh.getName().size());
        writer.writeBytes(encodedVertices.data(), encodedVertices.size());
        writer.writeBytes(encodedIndices.data(), encodedIndices.size());
        writer.writeBytes(optimized.submeshes.data(), optimized.submeshes.size() * sizeof(Submesh));
        writer.writeBytes(encodedSkinWeights.data(), encodedSkinWeights.size());
        return writer.bytes();
    }
#endif

    json payload = SerializeMeshData(mesh);
    std::vector<uint8_t> packed = json::to_msgpack(payload);

    CookedMeshBinaryWriter writer;
    writer.writeBytes("CMSH", 4);
    writer.writeU32(1);
    writer.writeU32(static_cast<uint32_t>(packed.size()));
    writer.writeBytes(packed.data(), packed.size());
    return writer.bytes();
}

bool SaveCookedMeshBinary(const std::filesystem::path& outputPath, const Mesh& mesh) {
    std::vector<uint8_t> bytes = SerializeCookedMeshBinary(mesh);
    if (bytes.empty()) {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(outputPath.parent_path(), ec);
    std::ofstream out(outputPath, std::ios::binary);
    if (!out.is_open()) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

std::shared_ptr<Mesh> DeserializeCookedMeshBinary(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 12) {
        return nullptr;
    }
    if (std::memcmp(bytes.data(), "CMSH", 4) != 0) {
        return nullptr;
    }

    auto readU32 = [&bytes](size_t offset) -> uint32_t {
        return static_cast<uint32_t>(bytes[offset]) |
               (static_cast<uint32_t>(bytes[offset + 1]) << 8u) |
               (static_cast<uint32_t>(bytes[offset + 2]) << 16u) |
               (static_cast<uint32_t>(bytes[offset + 3]) << 24u);
    };

    uint32_t version = readU32(4);
    if (version == 1) {
        uint32_t payloadSize = readU32(8);
        if (bytes.size() < 12ull + payloadSize) {
            return nullptr;
        }

        std::vector<uint8_t> payload(bytes.begin() + 12, bytes.begin() + 12 + payloadSize);
        json root = json::from_msgpack(payload, true, false);
        if (root.is_discarded() || !root.is_object()) {
            return nullptr;
        }
        return DeserializeMeshData(root);
    }

#if CRESCENT_HAS_MESHOPTIMIZER
    constexpr size_t kHeaderSize = 72;
    if ((version != 2 && version != 3) || bytes.size() < kHeaderSize) {
        return nullptr;
    }

    auto readF32 = [&bytes](size_t offset) -> float {
        uint32_t bits = static_cast<uint32_t>(bytes[offset]) |
                        (static_cast<uint32_t>(bytes[offset + 1]) << 8u) |
                        (static_cast<uint32_t>(bytes[offset + 2]) << 16u) |
                        (static_cast<uint32_t>(bytes[offset + 3]) << 24u);
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(float));
        return value;
    };

    const uint32_t vertexCount = readU32(8);
    const uint32_t indexCount = readU32(12);
    const uint32_t submeshCount = readU32(16);
    const uint32_t skinWeightCount = readU32(20);
    const uint32_t nameSize = readU32(24);
    const uint32_t vertexDataSize = readU32(28);
    const uint32_t indexDataSize = readU32(32);
    const uint32_t submeshDataSize = readU32(36);
    const uint32_t skinWeightDataSize = readU32(40);
    const bool doubleSided = readU32(44) != 0;

    const size_t expectedSubmeshBytes = static_cast<size_t>(submeshCount) * sizeof(Submesh);
    if (submeshDataSize != expectedSubmeshBytes) {
        return nullptr;
    }

    const size_t totalPayloadSize = static_cast<size_t>(nameSize)
        + static_cast<size_t>(vertexDataSize)
        + static_cast<size_t>(indexDataSize)
        + static_cast<size_t>(submeshDataSize)
        + static_cast<size_t>(skinWeightDataSize);
    if (bytes.size() < kHeaderSize + totalPayloadSize) {
        return nullptr;
    }

    size_t cursor = kHeaderSize;
    std::string name;
    if (nameSize > 0) {
        name.assign(reinterpret_cast<const char*>(bytes.data() + cursor), nameSize);
        cursor += nameSize;
    }

    std::vector<Vertex> vertices(vertexCount);
    if (vertexCount > 0) {
        if (vertexDataSize == 0) {
            return nullptr;
        }

        if (version == 2) {
            if (meshopt_decodeVertexBuffer(vertices.data(),
                                           vertexCount,
                                           sizeof(Vertex),
                                           bytes.data() + cursor,
                                           vertexDataSize) != 0) {
                return nullptr;
            }
        } else {
            struct QuantizedVertex {
                uint16_t position[3];
                int16_t normal[3];
                uint16_t texCoord[2];
                uint16_t texCoord1[2];
                int16_t tangent[3];
                int16_t bitangent[3];
                uint8_t color[4];
            };

            auto dequantizePosition = [](uint16_t value, float minValue, float maxValue) -> float {
                float extent = maxValue - minValue;
                if (extent <= 1e-8f) {
                    return minValue;
                }
                float normalized = static_cast<float>(value) / 65535.0f;
                return minValue + normalized * extent;
            };

            auto dequantizeSnorm16 = [](int16_t value) -> float {
                float normalized = static_cast<float>(value) / 32767.0f;
                return std::max(-1.0f, std::min(1.0f, normalized));
            };

            auto dequantizeColor8 = [](uint8_t value) -> float {
                return static_cast<float>(value) / 255.0f;
            };

            std::vector<QuantizedVertex> quantizedVertices(vertexCount);
            if (meshopt_decodeVertexBuffer(quantizedVertices.data(),
                                           vertexCount,
                                           sizeof(QuantizedVertex),
                                           bytes.data() + cursor,
                                           vertexDataSize) != 0) {
                return nullptr;
            }

            const float boundsMinX = readF32(48);
            const float boundsMinY = readF32(52);
            const float boundsMinZ = readF32(56);
            const float boundsMaxX = readF32(60);
            const float boundsMaxY = readF32(64);
            const float boundsMaxZ = readF32(68);

            for (size_t i = 0; i < quantizedVertices.size(); ++i) {
                const QuantizedVertex& q = quantizedVertices[i];
                Vertex& v = vertices[i];
                v.position.x = dequantizePosition(q.position[0], boundsMinX, boundsMaxX);
                v.position.y = dequantizePosition(q.position[1], boundsMinY, boundsMaxY);
                v.position.z = dequantizePosition(q.position[2], boundsMinZ, boundsMaxZ);
                v.normal.x = dequantizeSnorm16(q.normal[0]);
                v.normal.y = dequantizeSnorm16(q.normal[1]);
                v.normal.z = dequantizeSnorm16(q.normal[2]);
                v.texCoord.x = meshopt_dequantizeHalf(q.texCoord[0]);
                v.texCoord.y = meshopt_dequantizeHalf(q.texCoord[1]);
                v.texCoord1.x = meshopt_dequantizeHalf(q.texCoord1[0]);
                v.texCoord1.y = meshopt_dequantizeHalf(q.texCoord1[1]);
                v.tangent.x = dequantizeSnorm16(q.tangent[0]);
                v.tangent.y = dequantizeSnorm16(q.tangent[1]);
                v.tangent.z = dequantizeSnorm16(q.tangent[2]);
                v.bitangent.x = dequantizeSnorm16(q.bitangent[0]);
                v.bitangent.y = dequantizeSnorm16(q.bitangent[1]);
                v.bitangent.z = dequantizeSnorm16(q.bitangent[2]);
                v.color.x = dequantizeColor8(q.color[0]);
                v.color.y = dequantizeColor8(q.color[1]);
                v.color.z = dequantizeColor8(q.color[2]);
                v.color.w = dequantizeColor8(q.color[3]);
            }
        }
        cursor += vertexDataSize;
    }

    std::vector<uint32_t> indices(indexCount);
    if (indexCount > 0) {
        if (indexDataSize == 0 ||
            meshopt_decodeIndexBuffer(indices.data(),
                                      indexCount,
                                      sizeof(uint32_t),
                                      bytes.data() + cursor,
                                      indexDataSize) != 0) {
            return nullptr;
        }
        cursor += indexDataSize;
    }

    std::vector<Submesh> submeshes(submeshCount);
    if (submeshCount > 0) {
        std::memcpy(submeshes.data(), bytes.data() + cursor, submeshDataSize);
        cursor += submeshDataSize;
    }

    std::vector<SkinWeight> skinWeights(skinWeightCount);
    if (skinWeightCount > 0) {
        if (skinWeightDataSize == 0 ||
            meshopt_decodeVertexBuffer(skinWeights.data(),
                                       skinWeightCount,
                                       sizeof(SkinWeight),
                                       bytes.data() + cursor,
                                       skinWeightDataSize) != 0) {
            return nullptr;
        }
        cursor += skinWeightDataSize;
    }

    auto mesh = std::make_shared<Mesh>();
    mesh->setVertices(vertices);
    mesh->setIndices(indices);
    if (!submeshes.empty()) {
        mesh->setSubmeshes(submeshes);
    }
    if (!skinWeights.empty()) {
        mesh->setSkinWeights(skinWeights);
    }
    mesh->setName(name);
    mesh->setDoubleSided(doubleSided);

    const Math::Vector3 expectedBoundsMin(readF32(48), readF32(52), readF32(56));
    const Math::Vector3 expectedBoundsMax(readF32(60), readF32(64), readF32(68));
    (void)expectedBoundsMin;
    (void)expectedBoundsMax;
    return mesh;
#else
    (void)bytes;
    return nullptr;
#endif
}

std::string BuildCookedSceneMeshRelativePath(const std::filesystem::path& cookedScenePath,
                                             const std::string& meshKey) {
    if (meshKey.empty()) {
        return "";
    }
    std::filesystem::path sceneStem = cookedScenePath.stem();
    std::filesystem::path sidecarDir = sceneStem.string() + ".meshes";
    return (sidecarDir / (meshKey + ".cmesh")).generic_string();
}

std::string BuildRuntimeMeshCookKey(const Entity* entity,
                                    const ModelMeshReference* modelRef,
                                    const char* tag) {
    std::string seed;
    if (modelRef && !modelRef->getSourcePath().empty()) {
        seed = NormalizeEmbeddedCookKey(modelRef->getSourcePath()) + "|" +
               std::to_string(modelRef->getMeshIndex()) + "|" +
               std::to_string(modelRef->getMaterialIndex()) + "|" +
               (modelRef->isSkinned() ? "1" : "0") + "|" +
               (modelRef->isMerged() ? "1" : "0");
    } else if (entity) {
        seed = entity->getUUID().toString();
    }
    if (tag && *tag) {
        seed += "|";
        seed += tag;
    }
    if (seed.empty()) {
        return "";
    }
    return HashRuntimeCookKey(seed);
}

std::string EmitCookedMeshRef(Entity* entity,
                              const std::shared_ptr<Mesh>& mesh,
                              const ModelMeshReference* modelRef,
                              const char* tag,
                              const BuildSceneOptions& options) {
    if (!mesh || !options.externalizeRuntimeMeshes || !options.cookedMeshWriter ||
        options.cookedMeshWriter->sceneOutputPath.empty()) {
        return "";
    }

    std::string meshKey = BuildRuntimeMeshCookKey(entity, modelRef, tag);
    if (meshKey.empty()) {
        return "";
    }

    auto existing = options.cookedMeshWriter->emittedPaths.find(meshKey);
    if (existing != options.cookedMeshWriter->emittedPaths.end()) {
        return existing->second;
    }

    std::string relativePath = BuildCookedSceneMeshRelativePath(options.cookedMeshWriter->sceneOutputPath, meshKey);
    if (relativePath.empty()) {
        return "";
    }

    std::filesystem::path outputPath = options.cookedMeshWriter->sceneOutputPath.parent_path() / relativePath;
    if (!SaveCookedMeshBinary(outputPath, *mesh)) {
        return "";
    }

    options.cookedMeshWriter->emittedPaths[meshKey] = relativePath;
    return relativePath;
}

std::string ResolveSceneRelativePath(const std::string& scenePath, const std::string& storedPath) {
    if (storedPath.empty()) {
        return "";
    }
    std::filesystem::path path(storedPath);
    if (path.is_absolute() || scenePath.empty()) {
        return path.lexically_normal().string();
    }
    return (std::filesystem::path(scenePath).parent_path() / path).lexically_normal().string();
}

std::shared_ptr<Mesh> LoadCookedMeshRef(const json& meshRef,
                                        const std::string& scenePath,
                                        std::unordered_map<std::string, std::shared_ptr<Mesh>>& cookedMeshCache) {
    std::string storedPath;
    if (meshRef.is_string()) {
        storedPath = meshRef.get<std::string>();
    } else if (meshRef.is_object()) {
        storedPath = meshRef.value("path", std::string());
    }
    if (storedPath.empty()) {
        return nullptr;
    }

    std::string resolvedPath = ResolveSceneRelativePath(scenePath, storedPath);
    if (resolvedPath.empty()) {
        return nullptr;
    }

    auto it = cookedMeshCache.find(resolvedPath);
    if (it != cookedMeshCache.end()) {
        return it->second;
    }

    std::ifstream in(resolvedPath, std::ios::binary);
    if (!in.is_open()) {
        return nullptr;
    }

    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto mesh = DeserializeCookedMeshBinary(bytes);
    if (mesh) {
        cookedMeshCache[resolvedPath] = mesh;
    }
    return mesh;
}

json SerializeTextureRef(const std::shared_ptr<Texture2D>& texture, const char* typeKey, bool includeCookedPath = false) {
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
        if (includeCookedPath && typeKey && std::string(typeKey) == "texture") {
            std::string cookedPath = BuildCookedTextureRelativePath(path);
            if (!cookedPath.empty()) {
                ref["cookedPath"] = cookedPath;
            }
        }
        return ref;
    }
    return path;
}

json SerializeAssetPath(const std::string& path, const char* typeKey, bool includeCookedPath = false) {
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
        if (includeCookedPath && typeKey) {
            std::string type = typeKey;
            std::string cookedPath;
            if (type == "texture") {
                cookedPath = BuildCookedTextureRelativePath(path);
            } else if (type == "hdri") {
                cookedPath = BuildCookedEnvironmentRelativePath(path);
            }
            if (!cookedPath.empty()) {
                ref["cookedPath"] = cookedPath;
            }
        }
        return ref;
    }
    return SerializeProjectPathRef(path);
}

json SerializeMaterial(const Material& material, bool includeCookedTexturePaths = false) {
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
    j["terrainEnabled"] = material.getTerrainEnabled();
    j["terrainBlendSharpness"] = material.getTerrainBlendSharpness();
    j["terrainHeightStart"] = material.getTerrainHeightStart();
    j["terrainHeightEnd"] = material.getTerrainHeightEnd();
    j["terrainSlopeStart"] = material.getTerrainSlopeStart();
    j["terrainSlopeEnd"] = material.getTerrainSlopeEnd();
    j["terrainLayer0Tiling"] = Vec2ToJson(material.getTerrainLayer0Tiling());
    j["terrainLayer1Tiling"] = Vec2ToJson(material.getTerrainLayer1Tiling());
    j["terrainLayer2Tiling"] = Vec2ToJson(material.getTerrainLayer2Tiling());

    json textures = json::object();
    auto pushPath = [&textures, includeCookedTexturePaths](const char* key, const std::shared_ptr<Texture2D>& tex, const char* typeKey) {
        bool includeCooked = includeCookedTexturePaths && std::string(key) != "terrainControl";
        json ref = SerializeTextureRef(tex, typeKey, includeCooked);
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
    pushPath("opacity", material.getOpacityTexture(), "texture");
    pushPath("terrainControl", material.getTerrainControlTexture(), "texture");
    pushPath("terrainLayer0", material.getTerrainLayer0Texture(), "texture");
    pushPath("terrainLayer1", material.getTerrainLayer1Texture(), "texture");
    pushPath("terrainLayer2", material.getTerrainLayer2Texture(), "texture");
    pushPath("terrainLayer0Normal", material.getTerrainLayer0NormalTexture(), "texture");
    pushPath("terrainLayer1Normal", material.getTerrainLayer1NormalTexture(), "texture");
    pushPath("terrainLayer2Normal", material.getTerrainLayer2NormalTexture(), "texture");
    pushPath("terrainLayer0ORM", material.getTerrainLayer0ORMTexture(), "texture");
    pushPath("terrainLayer1ORM", material.getTerrainLayer1ORMTexture(), "texture");
    pushPath("terrainLayer2ORM", material.getTerrainLayer2ORMTexture(), "texture");

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
    const auto& skinWeights = mesh.getSkinWeights();

    json verts = json::array();
    for (const auto& v : vertices) {
        verts.push_back({
            v.position.x, v.position.y, v.position.z,
            v.normal.x, v.normal.y, v.normal.z,
            v.texCoord.x, v.texCoord.y,
            v.texCoord1.x, v.texCoord1.y,
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
    if (!mesh.getName().empty()) {
        j["name"] = mesh.getName();
    }
    j["doubleSided"] = mesh.isDoubleSided();
    if (!skinWeights.empty()) {
        json weights = json::array();
        for (const auto& weight : skinWeights) {
            weights.push_back({
                weight.indices[0], weight.indices[1], weight.indices[2], weight.indices[3],
                weight.weights[0], weight.weights[1], weight.weights[2], weight.weights[3]
            });
        }
        j["skinWeights"] = weights;
    }
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
        if (entry.size() >= 20) {
            v.texCoord1 = Math::Vector2(entry[8], entry[9]);
            v.tangent = Math::Vector3(entry[10], entry[11], entry[12]);
            v.bitangent = Math::Vector3(entry[13], entry[14], entry[15]);
            v.color = Math::Vector4(entry[16], entry[17], entry[18], entry[19]);
        } else {
            v.texCoord1 = v.texCoord;
            v.tangent = Math::Vector3(entry[8], entry[9], entry[10]);
            v.bitangent = Math::Vector3(entry[11], entry[12], entry[13]);
            v.color = Math::Vector4(entry[14], entry[15], entry[16], entry[17]);
        }
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

    std::vector<SkinWeight> skinWeights;
    if (j.contains("skinWeights") && j["skinWeights"].is_array()) {
        for (const auto& entry : j["skinWeights"]) {
            if (!entry.is_array() || entry.size() < 8) {
                continue;
            }
            SkinWeight weight;
            for (size_t i = 0; i < 4; ++i) {
                weight.indices[i] = entry[i].get<uint32_t>();
                weight.weights[i] = entry[i + 4].get<float>();
            }
            skinWeights.push_back(weight);
        }
    }

    auto mesh = std::make_shared<Mesh>();
    mesh->setVertices(vertices);
    mesh->setIndices(indices);
    if (!submeshes.empty()) {
        mesh->setSubmeshes(submeshes);
    }
    if (!skinWeights.empty()) {
        mesh->setSkinWeights(skinWeights);
    }
    mesh->setName(j.value("name", std::string()));
    mesh->setDoubleSided(j.value("doubleSided", mesh->isDoubleSided()));
    return mesh;
}

json SerializeSkeletonData(const Skeleton& skeleton) {
    json bones = json::array();
    for (const Bone& bone : skeleton.getBones()) {
        bones.push_back({
            {"name", bone.name},
            {"parentIndex", bone.parentIndex},
            {"localBind", MatrixToJson(bone.localBind)},
            {"inverseBind", MatrixToJson(bone.inverseBind)}
        });
    }
    return {
        {"rootIndex", skeleton.getRootIndex()},
        {"globalInverse", MatrixToJson(skeleton.getGlobalInverse())},
        {"bones", bones}
    };
}

std::shared_ptr<Skeleton> DeserializeSkeletonData(const json& j) {
    if (!j.is_object() || !j.contains("bones") || !j["bones"].is_array()) {
        return nullptr;
    }

    auto skeleton = std::make_shared<Skeleton>();
    skeleton->setGlobalInverse(JsonToMatrix(j.value("globalInverse", json::array()), Math::Matrix4x4::Identity));
    for (const auto& entry : j["bones"]) {
        if (!entry.is_object()) {
            continue;
        }
        skeleton->addBone(entry.value("name", std::string()),
                          entry.value("parentIndex", -1),
                          JsonToMatrix(entry.value("localBind", json::array()), Math::Matrix4x4::Identity),
                          JsonToMatrix(entry.value("inverseBind", json::array()), Math::Matrix4x4::Identity));
    }
    skeleton->setRootIndex(j.value("rootIndex", skeleton->getRootIndex()));
    return skeleton;
}

json SerializeAnimationClipData(const AnimationClip& clip) {
    json channels = json::array();
    for (const AnimationChannel& channel : clip.getChannels()) {
        json positions = json::array();
        for (const VectorKeyframe& key : channel.positionKeys) {
            positions.push_back({key.time, key.value.x, key.value.y, key.value.z});
        }
        json rotations = json::array();
        for (const QuaternionKeyframe& key : channel.rotationKeys) {
            rotations.push_back({key.time, key.value.x, key.value.y, key.value.z, key.value.w});
        }
        json scales = json::array();
        for (const VectorKeyframe& key : channel.scaleKeys) {
            scales.push_back({key.time, key.value.x, key.value.y, key.value.z});
        }
        channels.push_back({
            {"boneName", channel.boneName},
            {"boneIndex", channel.boneIndex},
            {"positionKeys", positions},
            {"rotationKeys", rotations},
            {"scaleKeys", scales}
        });
    }

    json events = json::array();
    for (const AnimationEvent& event : clip.getEvents()) {
        events.push_back({
            {"time", event.time},
            {"name", event.name}
        });
    }

    return {
        {"name", clip.getName()},
        {"durationTicks", clip.getDurationTicks()},
        {"ticksPerSecond", clip.getTicksPerSecond()},
        {"channels", channels},
        {"events", events}
    };
}

std::shared_ptr<AnimationClip> DeserializeAnimationClipData(const json& j) {
    if (!j.is_object()) {
        return nullptr;
    }

    auto clip = std::make_shared<AnimationClip>();
    clip->setName(j.value("name", std::string()));
    clip->setDurationTicks(j.value("durationTicks", clip->getDurationTicks()));
    clip->setTicksPerSecond(j.value("ticksPerSecond", clip->getTicksPerSecond()));

    if (j.contains("channels") && j["channels"].is_array()) {
        for (const auto& entry : j["channels"]) {
            if (!entry.is_object()) {
                continue;
            }
            AnimationChannel channel;
            channel.boneName = entry.value("boneName", std::string());
            channel.boneIndex = entry.value("boneIndex", -1);
            if (entry.contains("positionKeys") && entry["positionKeys"].is_array()) {
                for (const auto& key : entry["positionKeys"]) {
                    if (!key.is_array() || key.size() < 4) {
                        continue;
                    }
                    channel.positionKeys.push_back({
                        key[0].get<float>(),
                        Math::Vector3(key[1].get<float>(), key[2].get<float>(), key[3].get<float>())
                    });
                }
            }
            if (entry.contains("rotationKeys") && entry["rotationKeys"].is_array()) {
                for (const auto& key : entry["rotationKeys"]) {
                    if (!key.is_array() || key.size() < 5) {
                        continue;
                    }
                    channel.rotationKeys.push_back({
                        key[0].get<float>(),
                        Math::Quaternion(key[1].get<float>(),
                                         key[2].get<float>(),
                                         key[3].get<float>(),
                                         key[4].get<float>())
                    });
                }
            }
            if (entry.contains("scaleKeys") && entry["scaleKeys"].is_array()) {
                for (const auto& key : entry["scaleKeys"]) {
                    if (!key.is_array() || key.size() < 4) {
                        continue;
                    }
                    channel.scaleKeys.push_back({
                        key[0].get<float>(),
                        Math::Vector3(key[1].get<float>(), key[2].get<float>(), key[3].get<float>())
                    });
                }
            }
            clip->addChannel(channel);
        }
    }

    if (j.contains("events") && j["events"].is_array()) {
        for (const auto& entry : j["events"]) {
            AnimationEvent event;
            event.time = entry.value("time", 0.0f);
            event.name = entry.value("name", std::string());
            if (!event.name.empty()) {
                clip->addEvent(event);
            }
        }
    }

    return clip;
}

std::string ResolveCookedAssetPath(const json& entry) {
    AssetDatabase& db = AssetDatabase::getInstance();
    if (!entry.is_object() || !entry.contains("cookedPath")) {
        return "";
    }
    std::string stored = entry.value("cookedPath", std::string());
    if (stored.empty()) {
        return "";
    }
    std::filesystem::path cookedPath(stored);
    if (cookedPath.is_absolute()) {
        return cookedPath.lexically_normal().string();
    }
    std::string libraryPath = db.getLibraryPath();
    if (!libraryPath.empty()) {
        std::filesystem::path projectRoot = std::filesystem::path(libraryPath).parent_path();
        return (projectRoot / cookedPath).lexically_normal().string();
    }
    return cookedPath.lexically_normal().string();
}

SceneEnvironmentSettings EnvironmentFromRenderer(Renderer* renderer) {
    SceneEnvironmentSettings env;
    if (!renderer) {
        return env;
    }
    const Renderer::EnvironmentSettings& src = renderer->getEnvironmentSettings();
    env.skyboxPath = src.sourcePath.empty() ? "Builtin Sky" : src.sourcePath;
    env.cookedSkyboxPath = src.cookedIBLPath;
    env.exposureEV = src.exposureEV;
    env.iblIntensity = src.iblIntensity;
    env.skyIntensity = src.skyIntensity;
    env.ambientIntensity = src.ambientIntensity;
    env.ambientColor = src.ambientColor;
    env.saturation = src.saturation;
    env.contrast = src.contrast;
    env.blurLevel = src.blurLevel;
    env.tint = src.tint;
    env.rotation = src.rotation;
    env.skyboxVisible = src.skyboxVisible;
    env.skyMode = src.skyMode;
    env.autoSunColor = src.autoSunColor;
    return env;
}

json SerializeEnvironmentSettings(const SceneEnvironmentSettings& env, bool includeCookedAssetPaths = false) {
    json e;
    if (env.skyboxPath.empty() || env.skyboxPath == "Builtin Sky") {
        e["skybox"] = "Builtin Sky";
    } else {
        json ref = SerializeAssetPath(env.skyboxPath, "hdri", includeCookedAssetPaths);
        if (!ref.is_null() && !ref.empty()) {
            e["skybox"] = ref;
        }
    }
    e["exposureEV"] = env.exposureEV;
    e["iblIntensity"] = env.iblIntensity;
    e["skyIntensity"] = env.skyIntensity;
    e["ambientIntensity"] = env.ambientIntensity;
    e["ambientColor"] = Vec3ToJson(env.ambientColor);
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
        {"taaSpecularStability", post.taaSpecularStability},
        {"taaSpecularStabilityStrength", post.taaSpecularStabilityStrength},
        {"fxaa", post.fxaa},
        {"motionBlur", post.motionBlur},
        {"motionBlurStrength", post.motionBlurStrength},
        {"depthOfField", post.depthOfField},
        {"dofFocusDistance", post.dofFocusDistance},
        {"dofAperture", post.dofAperture}
    };
    if (!post.colorGradingLUT.empty()) {
        json ref = SerializeAssetPath(post.colorGradingLUT, "texture", true);
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
        {"textureQuality", quality.textureQuality},
        {"upscaler", quality.upscaler}
    };
}

json SerializeStaticLightingSettings(const SceneStaticLightingSettings& staticLighting) {
    std::string outputDirectory = staticLighting.outputDirectory.empty()
        ? std::string("Library/BakedLighting")
        : MakeProjectRelativePath(ResolveStaticLightingOutputDirectory(staticLighting));
    std::string bakeManifestPath = staticLighting.bakeManifestPath.empty()
        ? std::string()
        : MakeProjectRelativePath(ResolveProjectPath(staticLighting.bakeManifestPath));
    return {
        {"enabled", staticLighting.enabled},
        {"mode", staticLighting.mode},
        {"atlasSize", staticLighting.atlasSize},
        {"maxAtlasCount", staticLighting.maxAtlasCount},
        {"texelsPerUnit", staticLighting.texelsPerUnit},
        {"samplesPerTexel", staticLighting.samplesPerTexel},
        {"indirectBounces", staticLighting.indirectBounces},
        {"denoise", staticLighting.denoise},
        {"bakeDirectLighting", staticLighting.bakeDirectLighting},
        {"directionalLightmaps", staticLighting.directionalLightmaps},
        {"shadowmask", staticLighting.shadowmask},
        {"probeVolume", staticLighting.probeVolume},
        {"localReflectionProbes", staticLighting.localReflectionProbes},
        {"reflectionProbeBoxProjection", staticLighting.reflectionProbeBoxProjection},
        {"reflectionProbeOcclusion", staticLighting.reflectionProbeOcclusion},
        {"probeCountX", staticLighting.probeCountX},
        {"probeCountY", staticLighting.probeCountY},
        {"probeCountZ", staticLighting.probeCountZ},
        {"probeSamples", staticLighting.probeSamples},
        {"reflectionProbeIntensity", staticLighting.reflectionProbeIntensity},
        {"reflectionProbeBlendSharpness", staticLighting.reflectionProbeBlendSharpness},
        {"reflectionProbeFilterStrength", staticLighting.reflectionProbeFilterStrength},
        {"specularOcclusionStrength", staticLighting.specularOcclusionStrength},
        {"reflectionProbeMaxBlendCount", staticLighting.reflectionProbeMaxBlendCount},
        {"probeBoundsMin", Vec3ToJson(staticLighting.probeBoundsMin)},
        {"probeBoundsMax", Vec3ToJson(staticLighting.probeBoundsMax)},
        {"autoUnwrap", staticLighting.autoUnwrap},
        {"unwrapPadding", staticLighting.unwrapPadding},
        {"outputDirectory", outputDirectory},
        {"bakeManifestPath", bakeManifestPath},
        {"probeDataPath", staticLighting.probeDataPath.empty() ? std::string() : MakeProjectRelativePath(ResolveProjectPath(staticLighting.probeDataPath))},
        {"lastBakeHash", staticLighting.lastBakeHash}
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
            env.cookedSkyboxPath = ResolveCookedAssetPath(j["skybox"]);
        }
    }
    env.exposureEV = j.value("exposureEV", env.exposureEV);
    env.iblIntensity = j.value("iblIntensity", env.iblIntensity);
    env.skyIntensity = j.value("skyIntensity", env.skyIntensity);
    env.ambientIntensity = j.value("ambientIntensity", env.ambientIntensity);
    if (j.contains("ambientColor")) {
        env.ambientColor = JsonToVec3(j["ambientColor"], env.ambientColor);
    }
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
    post.taaSpecularStability = j.value("taaSpecularStability", post.taaSpecularStability);
    post.taaSpecularStabilityStrength = j.value("taaSpecularStabilityStrength", post.taaSpecularStabilityStrength);
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
    quality.upscaler = j.value("upscaler", quality.upscaler);
    return quality;
}

SceneStaticLightingSettings DeserializeStaticLightingSettings(const json& j) {
    SceneStaticLightingSettings staticLighting;
    if (!j.is_object()) {
        return staticLighting;
    }
    staticLighting.enabled = j.value("enabled", staticLighting.enabled);
    staticLighting.mode = j.value("mode", staticLighting.mode);
    staticLighting.atlasSize = j.value("atlasSize", staticLighting.atlasSize);
    staticLighting.maxAtlasCount = j.value("maxAtlasCount", staticLighting.maxAtlasCount);
    staticLighting.texelsPerUnit = j.value("texelsPerUnit", staticLighting.texelsPerUnit);
    staticLighting.samplesPerTexel = j.value("samplesPerTexel", staticLighting.samplesPerTexel);
    staticLighting.indirectBounces = j.value("indirectBounces", staticLighting.indirectBounces);
    staticLighting.denoise = j.value("denoise", staticLighting.denoise);
    staticLighting.bakeDirectLighting = j.value("bakeDirectLighting", staticLighting.bakeDirectLighting);
    staticLighting.directionalLightmaps = j.value("directionalLightmaps", staticLighting.directionalLightmaps);
    staticLighting.shadowmask = j.value("shadowmask", staticLighting.shadowmask);
    staticLighting.probeVolume = j.value("probeVolume", staticLighting.probeVolume);
    staticLighting.localReflectionProbes = j.value("localReflectionProbes", staticLighting.localReflectionProbes);
    staticLighting.reflectionProbeBoxProjection = j.value("reflectionProbeBoxProjection", staticLighting.reflectionProbeBoxProjection);
    staticLighting.reflectionProbeOcclusion = j.value("reflectionProbeOcclusion", staticLighting.reflectionProbeOcclusion);
    staticLighting.probeCountX = j.value("probeCountX", staticLighting.probeCountX);
    staticLighting.probeCountY = j.value("probeCountY", staticLighting.probeCountY);
    staticLighting.probeCountZ = j.value("probeCountZ", staticLighting.probeCountZ);
    staticLighting.probeSamples = j.value("probeSamples", staticLighting.probeSamples);
    staticLighting.reflectionProbeIntensity = j.value("reflectionProbeIntensity", staticLighting.reflectionProbeIntensity);
    staticLighting.reflectionProbeBlendSharpness = j.value("reflectionProbeBlendSharpness", staticLighting.reflectionProbeBlendSharpness);
    staticLighting.reflectionProbeFilterStrength = j.value("reflectionProbeFilterStrength", staticLighting.reflectionProbeFilterStrength);
    staticLighting.specularOcclusionStrength = j.value("specularOcclusionStrength", staticLighting.specularOcclusionStrength);
    staticLighting.reflectionProbeMaxBlendCount = j.value("reflectionProbeMaxBlendCount", staticLighting.reflectionProbeMaxBlendCount);
    if (j.contains("probeBoundsMin")) {
        staticLighting.probeBoundsMin = JsonToVec3(j["probeBoundsMin"], staticLighting.probeBoundsMin);
    }
    if (j.contains("probeBoundsMax")) {
        staticLighting.probeBoundsMax = JsonToVec3(j["probeBoundsMax"], staticLighting.probeBoundsMax);
    }
    staticLighting.autoUnwrap = j.value("autoUnwrap", staticLighting.autoUnwrap);
    staticLighting.unwrapPadding = j.value("unwrapPadding", staticLighting.unwrapPadding);
    staticLighting.outputDirectory = j.value("outputDirectory", staticLighting.outputDirectory);
    staticLighting.bakeManifestPath = j.value("bakeManifestPath", staticLighting.bakeManifestPath);
    staticLighting.probeDataPath = j.value("probeDataPath", staticLighting.probeDataPath);
    staticLighting.lastBakeHash = j.value("lastBakeHash", staticLighting.lastBakeHash);
    return staticLighting;
}

json SerializeStaticLightingAtlasRecord(const StaticLightingAtlasRecord& atlas) {
    json j = {
        {"index", atlas.index},
        {"width", atlas.width},
        {"height", atlas.height},
        {"rendererCount", atlas.rendererCount}
    };
    if (!atlas.lightmapPath.empty()) {
        j["lightmap"] = SerializeProjectPathRef(atlas.lightmapPath);
    }
    if (!atlas.directionalLightmapPath.empty()) {
        j["directionalLightmap"] = SerializeProjectPathRef(atlas.directionalLightmapPath);
    }
    if (!atlas.shadowmaskPath.empty()) {
        j["shadowmask"] = SerializeProjectPathRef(atlas.shadowmaskPath);
    }
    if (!atlas.expectedLightmapPath.empty()) {
        j["expectedLightmap"] = SerializeProjectPathRef(atlas.expectedLightmapPath);
    }
    if (!atlas.expectedDirectionalLightmapPath.empty()) {
        j["expectedDirectionalLightmap"] = SerializeProjectPathRef(atlas.expectedDirectionalLightmapPath);
    }
    if (!atlas.expectedShadowmaskPath.empty()) {
        j["expectedShadowmask"] = SerializeProjectPathRef(atlas.expectedShadowmaskPath);
    }
    return j;
}

StaticLightingAtlasRecord DeserializeStaticLightingAtlasRecord(const json& j) {
    StaticLightingAtlasRecord atlas;
    if (!j.is_object()) {
        return atlas;
    }
    atlas.index = j.value("index", atlas.index);
    atlas.width = j.value("width", atlas.width);
    atlas.height = j.value("height", atlas.height);
    atlas.rendererCount = j.value("rendererCount", atlas.rendererCount);
    if (j.contains("lightmap")) {
        atlas.lightmapPath = ResolveTextureEntryPath(j["lightmap"]);
    }
    if (j.contains("directionalLightmap")) {
        atlas.directionalLightmapPath = ResolveTextureEntryPath(j["directionalLightmap"]);
    }
    if (j.contains("shadowmask")) {
        atlas.shadowmaskPath = ResolveTextureEntryPath(j["shadowmask"]);
    }
    if (j.contains("expectedLightmap")) {
        atlas.expectedLightmapPath = ResolveTextureEntryPath(j["expectedLightmap"]);
    }
    if (j.contains("expectedDirectionalLightmap")) {
        atlas.expectedDirectionalLightmapPath = ResolveTextureEntryPath(j["expectedDirectionalLightmap"]);
    }
    if (j.contains("expectedShadowmask")) {
        atlas.expectedShadowmaskPath = ResolveTextureEntryPath(j["expectedShadowmask"]);
    }
    return atlas;
}

json SerializeStaticLightingRendererRecord(const StaticLightingRendererRecord& renderer) {
    json j = {
        {"entityUUID", renderer.entityUUID},
        {"entityName", renderer.entityName},
        {"staticGeometry", renderer.staticGeometry},
        {"contributeGI", renderer.contributeGI},
        {"receiveGI", renderer.receiveGI},
        {"lightmapIndex", renderer.lightmapIndex},
        {"lightmapUVChannel", renderer.lightmapUVChannel},
        {"lightmapScaleOffset", Vec4ToJson(renderer.lightmapScaleOffset)}
    };
    if (!renderer.lightmapPath.empty()) {
        j["lightmap"] = SerializeProjectPathRef(renderer.lightmapPath);
    }
    if (!renderer.directionalLightmapPath.empty()) {
        j["directionalLightmap"] = SerializeProjectPathRef(renderer.directionalLightmapPath);
    }
    if (!renderer.shadowmaskPath.empty()) {
        j["shadowmask"] = SerializeProjectPathRef(renderer.shadowmaskPath);
    }
    return j;
}

StaticLightingRendererRecord DeserializeStaticLightingRendererRecord(const json& j) {
    StaticLightingRendererRecord renderer;
    if (!j.is_object()) {
        return renderer;
    }
    renderer.entityUUID = j.value("entityUUID", renderer.entityUUID);
    renderer.entityName = j.value("entityName", renderer.entityName);
    renderer.staticGeometry = j.value("staticGeometry", renderer.staticGeometry);
    renderer.contributeGI = j.value("contributeGI", renderer.contributeGI);
    renderer.receiveGI = j.value("receiveGI", renderer.receiveGI);
    renderer.lightmapIndex = j.value("lightmapIndex", renderer.lightmapIndex);
    renderer.lightmapUVChannel = j.value("lightmapUVChannel", renderer.lightmapUVChannel);
    if (j.contains("lightmapScaleOffset")) {
        renderer.lightmapScaleOffset = JsonToVec4(j["lightmapScaleOffset"], renderer.lightmapScaleOffset);
    }
    if (j.contains("lightmap")) {
        renderer.lightmapPath = ResolveTextureEntryPath(j["lightmap"]);
    }
    if (j.contains("directionalLightmap")) {
        renderer.directionalLightmapPath = ResolveTextureEntryPath(j["directionalLightmap"]);
    }
    if (j.contains("shadowmask")) {
        renderer.shadowmaskPath = ResolveTextureEntryPath(j["shadowmask"]);
    }
    return renderer;
}

json SerializeStaticLightingManifestJson(const StaticLightingManifest& manifest) {
    json j = {
        {"version", manifest.version},
        {"sceneName", manifest.sceneName},
        {"scenePath", MakeProjectRelativePath(manifest.scenePath)},
        {"generatedAtUTC", manifest.generatedAtUTC},
        {"bakeHash", manifest.bakeHash},
        {"settings", SerializeStaticLightingSettings(manifest.settings)}
    };

    json atlases = json::array();
    for (const auto& atlas : manifest.atlases) {
        atlases.push_back(SerializeStaticLightingAtlasRecord(atlas));
    }
    j["atlases"] = atlases;

    json renderers = json::array();
    for (const auto& renderer : manifest.renderers) {
        renderers.push_back(SerializeStaticLightingRendererRecord(renderer));
    }
    j["renderers"] = renderers;

    return j;
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
        if (entry.contains("cookedPath")) {
            std::string stored = entry.value("cookedPath", std::string());
            if (!stored.empty()) {
                std::filesystem::path cookedPath(stored);
                if (cookedPath.is_absolute()) {
                    return cookedPath.string();
                }
                std::string libraryPath = db.getLibraryPath();
                if (!libraryPath.empty()) {
                    std::filesystem::path projectRoot = std::filesystem::path(libraryPath).parent_path();
                    return (projectRoot / cookedPath).lexically_normal().string();
                }
                return cookedPath.lexically_normal().string();
            }
        }
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
                if (entry.value("projectRelative", false)) {
                    return ResolveProjectPath(stored);
                }
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
        if (auto cooked = loader->loadEmbeddedCookedTexture(path, srgb, normalMap)) {
            return cooked;
        }
        return LoadEmbeddedTextureFromModel(loader, path, info, srgb, normalMap);
    }
    return loader->loadTexture(path, srgb, true, normalMap);
}

std::shared_ptr<Texture2D> LoadTerrainControlTexturePath(TextureLoader* loader,
                                                         const std::string& path) {
    if (!loader || path.empty()) {
        return nullptr;
    }
    std::filesystem::path texturePath(path);
    if (texturePath.extension() == ".ktx2") {
        return loader->loadTexture(path, false, false, false);
    }
    return loader->loadTextureUncompressed(path, false, false);
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
    material->setTerrainEnabled(j.value("terrainEnabled", material->getTerrainEnabled()));
    material->setTerrainBlendSharpness(j.value("terrainBlendSharpness", material->getTerrainBlendSharpness()));
    material->setTerrainHeightStart(j.value("terrainHeightStart", material->getTerrainHeightStart()));
    material->setTerrainHeightEnd(j.value("terrainHeightEnd", material->getTerrainHeightEnd()));
    material->setTerrainSlopeStart(j.value("terrainSlopeStart", material->getTerrainSlopeStart()));
    material->setTerrainSlopeEnd(j.value("terrainSlopeEnd", material->getTerrainSlopeEnd()));
    if (j.contains("terrainLayer0Tiling")) {
        material->setTerrainLayer0Tiling(JsonToVec2(j["terrainLayer0Tiling"], material->getTerrainLayer0Tiling()));
    }
    if (j.contains("terrainLayer1Tiling")) {
        material->setTerrainLayer1Tiling(JsonToVec2(j["terrainLayer1Tiling"], material->getTerrainLayer1Tiling()));
    }
    if (j.contains("terrainLayer2Tiling")) {
        material->setTerrainLayer2Tiling(JsonToVec2(j["terrainLayer2Tiling"], material->getTerrainLayer2Tiling()));
    }

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
        auto loadTerrainControl = [&](const char* key) -> std::shared_ptr<Texture2D> {
            if (!t.contains(key)) {
                return nullptr;
            }
            std::string resolved = ResolveTextureEntryPath(t[key]);
            if (resolved.empty()) {
                return nullptr;
            }
            return LoadTerrainControlTexturePath(loader, resolved);
        };
        if (auto tex = load("albedo", true, true)) material->setAlbedoTexture(tex);
        if (auto tex = load("normal", false, true)) material->setNormalTexture(tex);
        if (auto tex = load("metallic", false, false)) material->setMetallicTexture(tex);
        if (auto tex = load("roughness", false, false)) material->setRoughnessTexture(tex);
        if (auto tex = load("ao", false, false)) material->setAOTexture(tex);
        if (auto tex = load("emissive", true, false)) material->setEmissionTexture(tex);
        if (auto tex = load("orm", false, false)) material->setORMTexture(tex);
        if (auto tex = load("height", false, false)) material->setHeightTexture(tex);
        if (auto tex = load("opacity", false, false)) material->setOpacityTexture(tex);
        if (auto tex = loadTerrainControl("terrainControl")) material->setTerrainControlTexture(tex);
        if (auto tex = load("terrainLayer0", true, false)) material->setTerrainLayer0Texture(tex);
        if (auto tex = load("terrainLayer1", true, false)) material->setTerrainLayer1Texture(tex);
        if (auto tex = load("terrainLayer2", true, false)) material->setTerrainLayer2Texture(tex);
        if (auto tex = load("terrainLayer0Normal", false, true)) material->setTerrainLayer0NormalTexture(tex);
        if (auto tex = load("terrainLayer1Normal", false, true)) material->setTerrainLayer1NormalTexture(tex);
        if (auto tex = load("terrainLayer2Normal", false, true)) material->setTerrainLayer2NormalTexture(tex);
        if (auto tex = load("terrainLayer0ORM", false, false)) material->setTerrainLayer0ORMTexture(tex);
        if (auto tex = load("terrainLayer1ORM", false, false)) material->setTerrainLayer1ORMTexture(tex);
        if (auto tex = load("terrainLayer2ORM", false, false)) material->setTerrainLayer2ORMTexture(tex);
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

std::unordered_map<std::string, EntityRecord> BuildEntityRecords(Scene* scene,
                                                                 const json& entities,
                                                                 bool preserveUUIDs);
void ResolveEntityParents(Scene* scene, std::unordered_map<std::string, EntityRecord>& records);
void ApplyEntityComponents(Entity* entity,
                           const json& components,
                           Scene* scene,
                           const std::string& scenePath,
                           TextureLoader* textureLoader,
                           std::unordered_map<std::string, ModelCacheEntry>& modelCache,
                           std::unordered_map<std::string, std::shared_ptr<Mesh>>& cookedMeshCache);

bool EnvEnabled(const char* key) {
    const char* value = std::getenv(key);
    if (!value || !*value) {
        return false;
    }
    return !(value[0] == '0' && value[1] == '\0');
}

bool HasCookedMeshPayload(const json& components) {
    if (!components.is_object()) {
        return false;
    }
    if (components.contains("MeshRenderer")) {
        const json& meshRenderer = components["MeshRenderer"];
        if (meshRenderer.is_object() && (meshRenderer.contains("mesh") || meshRenderer.contains("meshRef"))) {
            return true;
        }
    }
    if (components.contains("SkinnedMeshRenderer")) {
        const json& skinned = components["SkinnedMeshRenderer"];
        if (skinned.is_object() &&
            (skinned.contains("mesh") || skinned.contains("meshRef") ||
             skinned.contains("skeleton") || skinned.contains("animationClips"))) {
            return true;
        }
    }
    if (components.contains("HLODProxy")) {
        const json& hlod = components["HLODProxy"];
        if (hlod.is_object() && (hlod.contains("mesh") || hlod.contains("meshRef"))) {
            return true;
        }
    }
    return false;
}

bool DeserializeSceneRoot(Scene* scene, const json& root, const std::string& scenePath) {
    if (!scene || !root.is_object()) {
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
        if (s.contains("staticLighting")) {
            sceneSettings.staticLighting = DeserializeStaticLightingSettings(s["staticLighting"]);
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
    std::unordered_map<std::string, std::shared_ptr<Mesh>> cookedMeshCache;

    for (auto& [uuid, record] : records) {
        if (!record.entity) {
            continue;
        }
        record.entity->setEditorOnly(record.editorOnly);
        ApplyEntityComponents(record.entity, record.components, scene, scenePath, textureLoader, modelCache, cookedMeshCache);
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

std::string ResolveSceneLoadPath(const std::string& path) {
    if (path.empty()) {
        return path;
    }

    std::filesystem::path source(path);
    if (source.extension() == ".ccscene") {
        return path;
    }

    if (!EnvEnabled("CRESCENT_PREFER_COOKED_SCENES") && !EnvEnabled("CRESCENT_REQUIRE_COOKED_SCENES")) {
        return path;
    }

    std::filesystem::path cooked = source;
    cooked.replace_extension(".ccscene");
    std::error_code ec;
    if (std::filesystem::exists(cooked, ec)) {
        return cooked.string();
    }

    if (EnvEnabled("CRESCENT_REQUIRE_COOKED_SCENES")) {
        return "";
    }
    return path;
}

} // namespace

bool SceneSerializer::SaveScene(Scene* scene, const std::string& path) {
    if (!scene) {
        return false;
    }
    if (ShouldPersistStaticLightingManifest(scene)) {
        SceneSettings updatedSettings = scene->getSettings();
        std::string manifestPath = SceneSerializer::ResolveStaticLightingManifestPath(scene, path);
        if (!manifestPath.empty()) {
            updatedSettings.staticLighting.bakeManifestPath = MakeProjectRelativePath(manifestPath);
            scene->setSettings(updatedSettings);
        }
    }
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path);
    if (!out.is_open()) {
        return false;
    }
    out << SerializeScene(scene, path);
    if (!out.good()) {
        return false;
    }
    if (ShouldPersistStaticLightingManifest(scene)) {
        return SceneSerializer::SaveStaticLightingManifest(scene, path);
    }
    return true;
}

bool SceneSerializer::SaveCookedRuntimeScene(Scene* scene, const std::string& path, bool includeEditorOnly) {
    if (!scene) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return false;
    }
    BuildSceneOptions options;
    options.includeEditorOnly = includeEditorOnly;
    options.embedRuntimePayloads = true;
    options.externalizeRuntimeMeshes = true;
    options.cookedScenePath = path;
    CookedMeshWriter writer;
    writer.sceneOutputPath = path;
    options.cookedMeshWriter = &writer;

    json root = BuildSceneJson(scene, "", options);
    std::vector<uint8_t> payload = json::to_msgpack(root);
    if (payload.empty()) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    return out.good();
}

bool SceneSerializer::LoadScene(Scene* scene, const std::string& path) {
    if (!scene) {
        return false;
    }
    std::string resolvedPath = ResolveSceneLoadPath(path);
    if (resolvedPath.empty()) {
        return false;
    }
    std::filesystem::path scenePath(resolvedPath);
    if (scenePath.extension() == ".ccscene") {
        std::ifstream in(resolvedPath, std::ios::binary);
        if (!in.is_open()) {
            return false;
        }
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return DeserializeSceneBinary(scene, data, resolvedPath);
    }
    std::ifstream in(resolvedPath);
    if (!in.is_open()) {
        return false;
    }
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return DeserializeScene(scene, data, resolvedPath);
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
                           std::unordered_map<std::string, ModelCacheEntry>& modelCache,
                           std::unordered_map<std::string, std::shared_ptr<Mesh>>& cookedMeshCache) {
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

    ModelMeshReference* modelMeshReference = nullptr;
    if (components.contains("ModelMeshReference")) {
        const json& m = components["ModelMeshReference"];
        modelMeshReference = entity->addComponent<ModelMeshReference>();
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
        modelMeshReference->setSourcePath(resolvedPath);
        modelMeshReference->setSourceGuid(guid);
        modelMeshReference->setMeshIndex(m.value("meshIndex", -1));
        modelMeshReference->setMaterialIndex(m.value("materialIndex", -1));
        modelMeshReference->setMeshName(m.value("meshName", std::string()));
        modelMeshReference->setSkinned(m.value("skinned", false));
        modelMeshReference->setMerged(m.value("merged", false));
        if (m.contains("importOptions")) {
            modelMeshReference->setImportOptions(DeserializeImportOptions(m["importOptions"]));
        }
    }

    MeshRenderer* meshRenderer = nullptr;
    SkinnedMeshRenderer* skinnedRenderer = nullptr;
    const bool hasCookedPayload = HasCookedMeshPayload(components);

    if (modelMeshReference && !hasCookedPayload) {
        std::string cacheKey = MakeModelCacheKey(modelMeshReference->getSourcePath(), modelMeshReference->getImportOptions());
        auto it = modelCache.find(cacheKey);
        if (it == modelCache.end()) {
            modelCache[cacheKey] = BuildModelCache(modelMeshReference->getSourcePath(), modelMeshReference->getImportOptions());
            it = modelCache.find(cacheKey);
        }
        const ModelCacheEntry& cache = it->second;
        MeshCacheEntry meshEntry;
        bool found = false;
        if (modelMeshReference->isMerged()) {
            auto mergedIt = cache.mergedByMaterial.find(modelMeshReference->getMaterialIndex());
            if (mergedIt != cache.mergedByMaterial.end()) {
                meshEntry = mergedIt->second;
                found = true;
            }
        } else {
            auto meshIt = cache.meshesByIndex.find(modelMeshReference->getMeshIndex());
            if (meshIt != cache.meshesByIndex.end()) {
                meshEntry = meshIt->second;
                found = true;
            }
        }
        if (found) {
            meshRenderer = entity->addComponent<MeshRenderer>();
            meshRenderer->setMesh(meshEntry.mesh);
            meshRenderer->setMaterial(meshEntry.material ? meshEntry.material : Material::CreateDefault());
            if (modelMeshReference->isSkinned() && cache.skeleton) {
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
        if (r.contains("meshRef")) {
            if (auto mesh = LoadCookedMeshRef(r["meshRef"], scenePath, cookedMeshCache)) {
                meshRenderer->setMesh(mesh);
                if (skinnedRenderer) {
                    skinnedRenderer->setMesh(mesh);
                }
            }
        } else if (r.contains("mesh")) {
            if (auto mesh = DeserializeMeshData(r["mesh"])) {
                meshRenderer->setMesh(mesh);
                if (skinnedRenderer) {
                    skinnedRenderer->setMesh(mesh);
                }
            }
        }
        meshRenderer->setCastShadows(r.value("castShadows", meshRenderer->getCastShadows()));
        meshRenderer->setReceiveShadows(r.value("receiveShadows", meshRenderer->getReceiveShadows()));
        meshRenderer->setUseBakedVertexLighting(r.value("useBakedVertexLighting", meshRenderer->getUseBakedVertexLighting()));

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
        if (r.contains("bakedVertexColors") && r["bakedVertexColors"].is_array()) {
            std::vector<Math::Vector4> bakedColors;
            bakedColors.reserve(r["bakedVertexColors"].size());
            for (const auto& entry : r["bakedVertexColors"]) {
                bakedColors.push_back(JsonToVec4(entry, Math::Vector4::Zero));
            }
            meshRenderer->setBakedVertexColors(bakedColors);
            meshRenderer->setUseBakedVertexLighting(!bakedColors.empty());
        }
        if (r.contains("staticLighting") && r["staticLighting"].is_object()) {
            MeshRenderer::StaticLightingData staticLighting = meshRenderer->getStaticLighting();
            const json& sl = r["staticLighting"];
            staticLighting.staticGeometry = sl.value("staticGeometry", staticLighting.staticGeometry);
            staticLighting.contributeGI = sl.value("contributeGI", staticLighting.contributeGI);
            staticLighting.receiveGI = sl.value("receiveGI", staticLighting.receiveGI);
            staticLighting.lightmapIndex = sl.value("lightmapIndex", staticLighting.lightmapIndex);
            staticLighting.lightmapUVChannel = sl.value("lightmapUVChannel", staticLighting.lightmapUVChannel);
            if (sl.contains("lightmapScaleOffset")) {
                staticLighting.lightmapScaleOffset = JsonToVec4(sl["lightmapScaleOffset"], staticLighting.lightmapScaleOffset);
            }
            if (sl.contains("lightmap")) {
                staticLighting.lightmapPath = ResolveTextureEntryPath(sl["lightmap"]);
            }
            if (sl.contains("directionalLightmap")) {
                staticLighting.directionalLightmapPath = ResolveTextureEntryPath(sl["directionalLightmap"]);
            }
            if (sl.contains("shadowmask")) {
                staticLighting.shadowmaskPath = ResolveTextureEntryPath(sl["shadowmask"]);
            }
            meshRenderer->setStaticLighting(staticLighting);
            if (sl.contains("lightmapUVs") && sl["lightmapUVs"].is_array()) {
                if (auto mesh = meshRenderer->getMesh()) {
                    auto vertices = mesh->getVertices();
                    const json& lightmapUVs = sl["lightmapUVs"];
                    if (vertices.size() == lightmapUVs.size()) {
                        bool valid = true;
                        for (size_t i = 0; i < vertices.size(); ++i) {
                            const auto& entry = lightmapUVs[i];
                            if (!entry.is_array() || entry.size() < 2) {
                                valid = false;
                                break;
                            }
                            vertices[i].texCoord1 = Math::Vector2(entry[0].get<float>(), entry[1].get<float>());
                        }
                        if (valid) {
                            mesh->setVertices(vertices);
                            meshRenderer->setMesh(mesh);
                            if (skinnedRenderer) {
                                skinnedRenderer->setMesh(mesh);
                            }
                        }
                    }
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
        if (h.contains("meshRef")) {
            if (auto mesh = LoadCookedMeshRef(h["meshRef"], scenePath, cookedMeshCache)) {
                if (!meshRenderer) {
                    meshRenderer = entity->addComponent<MeshRenderer>();
                }
                meshRenderer->setMesh(mesh);
            }
        } else if (h.contains("mesh")) {
            auto mesh = DeserializeMeshData(h["mesh"]);
            if (mesh) {
                if (!meshRenderer) {
                    meshRenderer = entity->addComponent<MeshRenderer>();
                }
                meshRenderer->setMesh(mesh);
            }
        }
    }

    if (components.contains("SkinnedMeshRenderer")) {
        const json& s = components["SkinnedMeshRenderer"];
        if (!skinnedRenderer && (s.contains("mesh") || s.contains("meshRef") ||
                                 s.contains("skeleton") || s.contains("animationClips"))) {
            skinnedRenderer = entity->addComponent<SkinnedMeshRenderer>();
        }
        if (skinnedRenderer && s.contains("meshRef")) {
            if (auto mesh = LoadCookedMeshRef(s["meshRef"], scenePath, cookedMeshCache)) {
                skinnedRenderer->setMesh(mesh);
                if (!meshRenderer) {
                    meshRenderer = entity->addComponent<MeshRenderer>();
                }
                meshRenderer->setMesh(mesh);
            }
        } else if (skinnedRenderer && s.contains("mesh")) {
            if (auto mesh = DeserializeMeshData(s["mesh"])) {
                skinnedRenderer->setMesh(mesh);
                if (!meshRenderer) {
                    meshRenderer = entity->addComponent<MeshRenderer>();
                }
                meshRenderer->setMesh(mesh);
            }
        }
        if (skinnedRenderer && s.contains("skeleton")) {
            if (auto skeleton = DeserializeSkeletonData(s["skeleton"])) {
                skinnedRenderer->setSkeleton(skeleton);
            }
        }
        if (skinnedRenderer && s.contains("animationClips") && s["animationClips"].is_array()) {
            std::vector<std::shared_ptr<AnimationClip>> clips;
            clips.reserve(s["animationClips"].size());
            for (const auto& clipData : s["animationClips"]) {
                clips.push_back(DeserializeAnimationClipData(clipData));
            }
            skinnedRenderer->setAnimationClips(clips);
        }
        if (skinnedRenderer && !s.contains("animationClips") &&
            s.contains("animationSources") && s["animationSources"].is_array()) {
            auto skeleton = skinnedRenderer->getSkeleton();
            if (skeleton) {
                for (const auto& sourceEntry : s["animationSources"]) {
                    std::string resolvedPath = ResolveTextureEntryPath(sourceEntry);
                    if (resolvedPath.empty()) {
                        continue;
                    }

                    std::string guid;
                    if (sourceEntry.is_object()) {
                        guid = sourceEntry.value("guid", std::string());
                    }

                    SceneCommands::ModelImportOptions importOptions;
                    AssetDatabase& db = AssetDatabase::getInstance();
                    AssetRecord record;
                    bool hasRecord = false;
                    if (!guid.empty()) {
                        hasRecord = db.getRecordForGuid(guid, record);
                    }
                    if (!hasRecord) {
                        hasRecord = db.getRecordForPath(resolvedPath, record);
                    }
                    if (hasRecord && record.type == "model") {
                        importOptions = record.modelSettings;
                    }

                    auto clips = SceneCommands::importAnimationClipsForSkeleton(resolvedPath, *skeleton, importOptions);
                    if (clips.empty()) {
                        continue;
                    }

                    AnimationClipSource source;
                    source.path = resolvedPath;
                    source.guid = guid;
                    source.clips = std::move(clips);
                    skinnedRenderer->addAnimationClipSource(source);
                }
            }
        }
        if (skinnedRenderer && meshRenderer) {
            const auto& materials = meshRenderer->getMaterials();
            for (size_t i = 0; i < materials.size(); ++i) {
                if (materials[i]) {
                    skinnedRenderer->setMaterial(static_cast<uint32_t>(i), materials[i]);
                }
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
        bool contributeToStaticBake = l.value(
            "contributeToStaticBake",
            l.value("bakeToVertexLighting", light->getContributeToStaticBake())
        );
        light->setContributeToStaticBake(contributeToStaticBake);
        light->setMobility(static_cast<Light::Mobility>(l.value("mobility", static_cast<int>(light->getMobility()))));
        light->setShadowmaskChannel(l.value("shadowmaskChannel", light->getShadowmaskChannel()));
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

json BuildSceneJson(Scene* scene, const std::string& scenePath, const BuildSceneOptions& options) {
    json root;
    root["version"] = 1;
    root["name"] = scene ? scene->getName() : "Scene";
    if (options.embedRuntimePayloads) {
        root["runtimeCooked"] = true;
    }
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
        if (!options.includeEditorOnly && entity->isEditorOnly()) {
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

        ModelMeshReference* modelMeshReference = entity->getComponent<ModelMeshReference>();
        if (auto* renderer = entity->getComponent<MeshRenderer>()) {
            json materials = json::array();
            const auto& mats = renderer->getMaterials();
            for (const auto& mat : mats) {
                if (mat) {
                    materials.push_back(SerializeMaterial(*mat, options.embedRuntimePayloads));
                } else {
                    materials.push_back(json::object());
                }
            }
            components["MeshRenderer"] = {
                {"castShadows", renderer->getCastShadows()},
                {"receiveShadows", renderer->getReceiveShadows()},
                {"useBakedVertexLighting", renderer->getUseBakedVertexLighting()},
                {"materials", materials}
            };
            if (renderer->getUseBakedVertexLighting() && !renderer->getBakedVertexColors().empty()) {
                json bakedColors = json::array();
                for (const auto& color : renderer->getBakedVertexColors()) {
                    bakedColors.push_back(Vec4ToJson(color));
                }
                components["MeshRenderer"]["bakedVertexColors"] = bakedColors;
            }
            const bool shouldSerializeMeshData =
                (!entity->getComponent<SkinnedMeshRenderer>()) &&
                (options.embedRuntimePayloads || ShouldSerializeCustomizedPrimitiveMesh(entity, renderer));
            if (shouldSerializeMeshData) {
                if (auto mesh = renderer->getMesh()) {
                    if (options.embedRuntimePayloads && options.externalizeRuntimeMeshes) {
                        std::string meshRef = EmitCookedMeshRef(entity, mesh, modelMeshReference, "mesh_renderer", options);
                        if (!meshRef.empty()) {
                            components["MeshRenderer"]["meshRef"] = meshRef;
                        } else {
                            components["MeshRenderer"]["mesh"] = SerializeMeshData(*mesh);
                        }
                    } else {
                        components["MeshRenderer"]["mesh"] = SerializeMeshData(*mesh);
                    }
                }
            }
            if (renderer->hasStaticLightingData()) {
                const auto& staticLighting = renderer->getStaticLighting();
                json staticLightingJson = {
                    {"staticGeometry", staticLighting.staticGeometry},
                    {"contributeGI", staticLighting.contributeGI},
                    {"receiveGI", staticLighting.receiveGI},
                    {"lightmapIndex", staticLighting.lightmapIndex},
                    {"lightmapUVChannel", staticLighting.lightmapUVChannel},
                    {"lightmapScaleOffset", Vec4ToJson(staticLighting.lightmapScaleOffset)}
                };
                if (!staticLighting.lightmapPath.empty()) {
                    json ref = SerializeAssetPath(staticLighting.lightmapPath, "texture", false);
                    if (options.embedRuntimePayloads) {
                        std::string cookedPath = BuildCookedStaticLightmapRelativePath(staticLighting.lightmapPath);
                        if (!cookedPath.empty()) {
                            if (!ref.is_object()) {
                                ref = SerializeProjectPathRef(staticLighting.lightmapPath);
                            }
                            if (ref.is_object()) {
                                ref["cookedPath"] = cookedPath;
                            }
                        }
                    }
                    if (!ref.is_null() && !ref.empty()) {
                        staticLightingJson["lightmap"] = ref;
                    }
                }
                if (!staticLighting.directionalLightmapPath.empty()) {
                    json ref = SerializeAssetPath(staticLighting.directionalLightmapPath, "texture", false);
                    if (!ref.is_null() && !ref.empty()) {
                        staticLightingJson["directionalLightmap"] = ref;
                    }
                }
                if (!staticLighting.shadowmaskPath.empty()) {
                    json ref = SerializeAssetPath(staticLighting.shadowmaskPath, "texture", false);
                    if (!ref.is_null() && !ref.empty()) {
                        staticLightingJson["shadowmask"] = ref;
                    }
                }
                if (!options.embedRuntimePayloads) {
                    if (auto mesh = renderer->getMesh()) {
                        const auto& vertices = mesh->getVertices();
                        if (!vertices.empty()) {
                            json lightmapUVs = json::array();
                            for (const auto& vertex : vertices) {
                                lightmapUVs.push_back({vertex.texCoord1.x, vertex.texCoord1.y});
                            }
                            staticLightingJson["lightmapUVs"] = lightmapUVs;
                        }
                    }
                }
                components["MeshRenderer"]["staticLighting"] = staticLightingJson;
            }
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
            if (options.embedRuntimePayloads) {
                if (auto mesh = skinned->getMesh()) {
                    if (options.externalizeRuntimeMeshes) {
                        std::string meshRef = EmitCookedMeshRef(entity, mesh, modelMeshReference, "skinned_mesh", options);
                        if (!meshRef.empty()) {
                            skinnedData["meshRef"] = meshRef;
                        } else {
                            skinnedData["mesh"] = SerializeMeshData(*mesh);
                        }
                    } else {
                        skinnedData["mesh"] = SerializeMeshData(*mesh);
                    }
                }
                if (auto skeleton = skinned->getSkeleton()) {
                    skinnedData["skeleton"] = SerializeSkeletonData(*skeleton);
                }
                const auto& clips = skinned->getAnimationClips();
                if (!clips.empty()) {
                    json clipData = json::array();
                    for (const auto& clip : clips) {
                        if (clip) {
                            clipData.push_back(SerializeAnimationClipData(*clip));
                        } else {
                            clipData.push_back(json::object());
                        }
                    }
                    skinnedData["animationClips"] = clipData;
                }
            } else {
                const auto& sources = skinned->getAnimationClipSources();
                if (!sources.empty()) {
                    json sourceData = json::array();
                    for (const auto& source : sources) {
                        if (source.path.empty()) {
                            continue;
                        }
                        json ref = SerializeAssetPath(source.path, "model");
                        if (ref.is_null() || ref.empty()) {
                            continue;
                        }
                        if (ref.is_object() && !source.guid.empty() && !ref.contains("guid")) {
                            ref["guid"] = source.guid;
                        }
                        sourceData.push_back(ref);
                    }
                    if (!sourceData.empty()) {
                        skinnedData["animationSources"] = sourceData;
                    }
                }
            }
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
                    if (options.externalizeRuntimeMeshes) {
                        std::string meshRef = EmitCookedMeshRef(entity, mesh, modelMeshReference, "hlod_mesh", options);
                        if (!meshRef.empty()) {
                            hlodData["meshRef"] = meshRef;
                        } else {
                            hlodData["mesh"] = SerializeMeshData(*mesh);
                        }
                    } else {
                        hlodData["mesh"] = SerializeMeshData(*mesh);
                    }
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
            json muzzleRef = SerializeAssetPath(controller->getMuzzleTexturePath(), "texture", options.embedRuntimePayloads);
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
                {"volumetric", light->getVolumetric()},
                {"contributeToStaticBake", light->getContributeToStaticBake()},
                {"mobility", static_cast<int>(light->getMobility())},
                {"shadowmaskChannel", light->getShadowmaskChannel()}
            };
        }

        if (auto* decal = entity->getComponent<Decal>()) {
            json decalData;
            decalData["color"] = Vec4ToJson(decal->getTint());
            decalData["opacity"] = decal->getOpacity();
            decalData["tiling"] = Vec2ToJson(decal->getTiling());
            decalData["offset"] = Vec2ToJson(decal->getOffset());
            decalData["softness"] = decal->getEdgeSoftness();

            json albedoRef = SerializeTextureRef(decal->getAlbedoTexture(), "texture", options.embedRuntimePayloads);
            if (!albedoRef.is_null() && !albedoRef.empty()) {
                decalData["albedo"] = albedoRef;
            } else if (!decal->getAlbedoPath().empty()) {
                json pathRef = SerializeAssetPath(decal->getAlbedoPath(), "texture", options.embedRuntimePayloads);
                if (!pathRef.is_null() && !pathRef.empty()) {
                    decalData["albedo"] = pathRef;
                }
            }
            json normalRef = SerializeTextureRef(decal->getNormalTexture(), "texture", options.embedRuntimePayloads);
            if (!normalRef.is_null() && !normalRef.empty()) {
                decalData["normal"] = normalRef;
            } else if (!decal->getNormalPath().empty()) {
                json pathRef = SerializeAssetPath(decal->getNormalPath(), "texture", options.embedRuntimePayloads);
                if (!pathRef.is_null() && !pathRef.empty()) {
                    decalData["normal"] = pathRef;
                }
            }
            json ormRef = SerializeTextureRef(decal->getORMTexture(), "texture", options.embedRuntimePayloads);
            if (!ormRef.is_null() && !ormRef.empty()) {
                decalData["orm"] = ormRef;
            } else if (!decal->getORMPath().empty()) {
                json pathRef = SerializeAssetPath(decal->getORMPath(), "texture", options.embedRuntimePayloads);
                if (!pathRef.is_null() && !pathRef.empty()) {
                    decalData["orm"] = pathRef;
                }
            }
            json maskRef = SerializeTextureRef(decal->getMaskTexture(), "texture", options.embedRuntimePayloads);
            if (!maskRef.is_null() && !maskRef.empty()) {
                decalData["mask"] = maskRef;
            } else if (!decal->getMaskPath().empty()) {
                json pathRef = SerializeAssetPath(decal->getMaskPath(), "texture", options.embedRuntimePayloads);
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

    if (options.includeAssetRoot) {
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
    if (!scenePath.empty() && ShouldPersistStaticLightingManifest(scene)) {
        std::string manifestPath = SceneSerializer::ResolveStaticLightingManifestPath(scene, scenePath);
        if (!manifestPath.empty()) {
            settings.staticLighting.bakeManifestPath = MakeProjectRelativePath(manifestPath);
        }
    }
    json sceneSettings;
    sceneSettings["environment"] = SerializeEnvironmentSettings(settings.environment, options.embedRuntimePayloads);
    sceneSettings["fog"] = SerializeFogSettings(settings.fog);
    sceneSettings["postProcess"] = SerializePostProcessSettings(settings.postProcess);
    sceneSettings["quality"] = SerializeQualitySettings(settings.quality);
    sceneSettings["staticLighting"] = SerializeStaticLightingSettings(settings.staticLighting);
    root["sceneSettings"] = sceneSettings;

    return root;
}

} // namespace

std::string SceneSerializer::SerializeScene(Scene* scene) {
    BuildSceneOptions options;
    json root = BuildSceneJson(scene, "", options);
    return root.dump(2);
}

std::string SceneSerializer::SerializeScene(Scene* scene, const std::string& scenePath) {
    BuildSceneOptions options;
    options.includeAssetRoot = true;
    json root = BuildSceneJson(scene, scenePath, options);
    return root.dump(2);
}

std::string SceneSerializer::SerializeScene(Scene* scene, bool includeEditorOnly) {
    BuildSceneOptions options;
    options.includeEditorOnly = includeEditorOnly;
    json root = BuildSceneJson(scene, "", options);
    return root.dump(2);
}

std::string SceneSerializer::SerializeCookedRuntimeScene(Scene* scene, bool includeEditorOnly) {
    BuildSceneOptions options;
    options.includeEditorOnly = includeEditorOnly;
    options.embedRuntimePayloads = true;
    json root = BuildSceneJson(scene, "", options);
    return root.dump();
}

std::vector<uint8_t> SceneSerializer::SerializeCookedRuntimeSceneBinary(Scene* scene, bool includeEditorOnly) {
    BuildSceneOptions options;
    options.includeEditorOnly = includeEditorOnly;
    options.embedRuntimePayloads = true;
    options.externalizeRuntimeMeshes = true;
    json root = BuildSceneJson(scene, "", options);
    return json::to_msgpack(root);
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
    return DeserializeSceneRoot(scene, root, scenePath);
}

bool SceneSerializer::DeserializeSceneBinary(Scene* scene, const std::vector<uint8_t>& data, const std::string& scenePath) {
    if (!scene || data.empty()) {
        return false;
    }
    json root = json::from_msgpack(data, true, false);
    if (root.is_discarded() || !root.is_object()) {
        return false;
    }
    return DeserializeSceneRoot(scene, root, scenePath);
}

std::string SceneSerializer::ResolveStaticLightingManifestPath(Scene* scene, const std::string& scenePath) {
    if (!scene) {
        return "";
    }

    const SceneStaticLightingSettings& settings = scene->getSettings().staticLighting;
    if (!settings.bakeManifestPath.empty()) {
        return ResolveProjectPath(settings.bakeManifestPath);
    }

    std::filesystem::path outputDir = ResolveStaticLightingOutputDirectory(settings);
    std::string sceneStem = ResolveSceneStem(scene, scenePath);
    return NormalizePathString((outputDir / (sceneStem + ".lightmaps.json")).string());
}

StaticLightingManifest SceneSerializer::BuildStaticLightingManifest(Scene* scene, const std::string& scenePath) {
    StaticLightingManifest manifest;
    if (!scene) {
        return manifest;
    }

    manifest.sceneName = scene->getName();
    manifest.scenePath = scenePath.empty() ? std::string() : MakeProjectRelativePath(scenePath);
    manifest.generatedAtUTC = FormatUTCNowISO8601();
    manifest.settings = scene->getSettings().staticLighting;
    manifest.settings.outputDirectory = MakeProjectRelativePath(ResolveStaticLightingOutputDirectory(manifest.settings));
    manifest.settings.bakeManifestPath = MakeProjectRelativePath(SceneSerializer::ResolveStaticLightingManifestPath(scene, scenePath));
    manifest.bakeHash = manifest.settings.lastBakeHash;

    std::map<int, size_t> atlasByIndex;

    for (const auto& entityHandle : scene->getAllEntities()) {
        Entity* entity = entityHandle.get();
        if (!entity) {
            continue;
        }

        auto* renderer = entity->getComponent<MeshRenderer>();
        if (!renderer || !renderer->hasStaticLightingData()) {
            continue;
        }

        const auto& staticLighting = renderer->getStaticLighting();
        StaticLightingRendererRecord rendererRecord;
        rendererRecord.entityUUID = entity->getUUID().toString();
        rendererRecord.entityName = entity->getName();
        rendererRecord.staticGeometry = staticLighting.staticGeometry;
        rendererRecord.contributeGI = staticLighting.contributeGI;
        rendererRecord.receiveGI = staticLighting.receiveGI;
        rendererRecord.lightmapIndex = staticLighting.lightmapIndex;
        rendererRecord.lightmapUVChannel = staticLighting.lightmapUVChannel;
        rendererRecord.lightmapScaleOffset = staticLighting.lightmapScaleOffset;
        rendererRecord.lightmapPath = staticLighting.lightmapPath;
        rendererRecord.directionalLightmapPath = staticLighting.directionalLightmapPath;
        rendererRecord.shadowmaskPath = staticLighting.shadowmaskPath;
        manifest.renderers.push_back(rendererRecord);

        if (staticLighting.lightmapIndex < 0) {
            continue;
        }

        size_t atlasRecordIndex = 0;
        auto atlasIt = atlasByIndex.find(staticLighting.lightmapIndex);
        if (atlasIt == atlasByIndex.end()) {
            StaticLightingAtlasRecord atlas;
            atlas.index = staticLighting.lightmapIndex;
            atlas.width = manifest.settings.atlasSize;
            atlas.height = manifest.settings.atlasSize;
            atlas.expectedLightmapPath = BuildStaticLightingArtifactPath(scene, scenePath, atlas.index, "_light.exr");
            if (manifest.settings.directionalLightmaps) {
                atlas.expectedDirectionalLightmapPath = BuildStaticLightingArtifactPath(scene, scenePath, atlas.index, "_dir.png");
            }
            if (manifest.settings.shadowmask) {
                atlas.expectedShadowmaskPath = BuildStaticLightingArtifactPath(scene, scenePath, atlas.index, "_shadowmask.png");
            }
            manifest.atlases.push_back(atlas);
            atlasRecordIndex = manifest.atlases.size() - 1;
            atlasByIndex[staticLighting.lightmapIndex] = atlasRecordIndex;
        } else {
            atlasRecordIndex = atlasIt->second;
        }

        StaticLightingAtlasRecord& atlas = manifest.atlases[atlasRecordIndex];
        atlas.rendererCount += 1;
        if (!staticLighting.lightmapPath.empty()) {
            atlas.lightmapPath = staticLighting.lightmapPath;
        }
        if (!staticLighting.directionalLightmapPath.empty()) {
            atlas.directionalLightmapPath = staticLighting.directionalLightmapPath;
        }
        if (!staticLighting.shadowmaskPath.empty()) {
            atlas.shadowmaskPath = staticLighting.shadowmaskPath;
        }
    }

    return manifest;
}

bool SceneSerializer::SaveStaticLightingManifest(Scene* scene, const std::string& scenePath) {
    if (!scene) {
        return false;
    }

    std::string manifestPath = ResolveStaticLightingManifestPath(scene, scenePath);
    if (manifestPath.empty()) {
        return false;
    }

    StaticLightingManifest manifest = BuildStaticLightingManifest(scene, scenePath);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(manifestPath).parent_path(), ec);

    std::ofstream out(manifestPath);
    if (!out.is_open()) {
        return false;
    }
    out << SerializeStaticLightingManifestJson(manifest).dump(2);
    return out.good();
}

bool SceneSerializer::LoadStaticLightingManifest(const std::string& manifestPath, StaticLightingManifest& outManifest) {
    outManifest = StaticLightingManifest();
    if (manifestPath.empty()) {
        return false;
    }

    std::ifstream in(manifestPath);
    if (!in.is_open()) {
        return false;
    }

    json root = json::parse(in, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return false;
    }

    outManifest.version = root.value("version", outManifest.version);
    outManifest.sceneName = root.value("sceneName", outManifest.sceneName);
    outManifest.scenePath = ResolveProjectPath(root.value("scenePath", std::string()));
    outManifest.generatedAtUTC = root.value("generatedAtUTC", outManifest.generatedAtUTC);
    outManifest.bakeHash = root.value("bakeHash", outManifest.bakeHash);
    if (root.contains("settings")) {
        outManifest.settings = DeserializeStaticLightingSettings(root["settings"]);
    }

    if (root.contains("atlases") && root["atlases"].is_array()) {
        for (const auto& entry : root["atlases"]) {
            outManifest.atlases.push_back(DeserializeStaticLightingAtlasRecord(entry));
        }
    }
    if (root.contains("renderers") && root["renderers"].is_array()) {
        for (const auto& entry : root["renderers"]) {
            outManifest.renderers.push_back(DeserializeStaticLightingRendererRecord(entry));
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
    std::unordered_map<std::string, std::shared_ptr<Mesh>> cookedMeshCache;
    for (auto& [uuid, record] : records) {
        if (!record.entity) {
            continue;
        }
        record.entity->setEditorOnly(record.editorOnly);
        ApplyEntityComponents(record.entity, record.components, scene, "", textureLoader, modelCache, cookedMeshCache);
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
