#include "AssetDatabase.hpp"
#include "../Core/UUID.hpp"
#include "../../../ThirdParty/nlohmann/json.hpp"
#include <filesystem>
#include <fstream>
#include <unordered_set>

namespace Crescent {
namespace {

using json = nlohmann::json;

std::string ToLower(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

} // namespace

AssetDatabase& AssetDatabase::getInstance() {
    static AssetDatabase instance;
    return instance;
}

void AssetDatabase::setRootPath(const std::string& path) {
    m_RootPath = normalizePath(path);
    rescan();
}

void AssetDatabase::clear() {
    m_GuidToRecord.clear();
    m_PathToGuid.clear();
}

void AssetDatabase::rescan() {
    clear();
    if (m_RootPath.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::path root(m_RootPath);
    if (!std::filesystem::exists(root, ec)) {
        return;
    }

    std::filesystem::recursive_directory_iterator it(root, ec);
    if (ec) {
        return;
    }

    for (const auto& entry : it) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        std::filesystem::path path = entry.path();
        if (path.extension() == ".cmeta") {
            continue;
        }
        if (path.filename() == ".DS_Store") {
            continue;
        }
        std::string type;
        std::string ext = ToLower(path.extension().string());
        if (!isAssetFile(ext, type)) {
            continue;
        }
        std::string absPath = normalizePath(path.string());
        std::string guid = loadOrCreateGuid(absPath, type);
        if (guid.empty()) {
            continue;
        }
        AssetRecord record;
        record.guid = guid;
        record.relativePath = getRelativePath(absPath);
        record.type = type;
        m_GuidToRecord[guid] = record;
        m_PathToGuid[absPath] = guid;
    }
}

std::string AssetDatabase::registerAsset(const std::string& absolutePath, const std::string& type) {
    if (m_RootPath.empty()) {
        return "";
    }
    std::string normalized = normalizePath(absolutePath);
    if (!isUnderRoot(normalized)) {
        return "";
    }
    auto it = m_PathToGuid.find(normalized);
    if (it != m_PathToGuid.end()) {
        return it->second;
    }
    std::string assetType = type;
    if (assetType.empty()) {
        std::string ext = ToLower(std::filesystem::path(normalized).extension().string());
        if (!isAssetFile(ext, assetType)) {
            return "";
        }
    }
    std::string guid = loadOrCreateGuid(normalized, assetType);
    if (guid.empty()) {
        return "";
    }
    AssetRecord record;
    record.guid = guid;
    record.relativePath = getRelativePath(normalized);
    record.type = assetType;
    m_GuidToRecord[guid] = record;
    m_PathToGuid[normalized] = guid;
    return guid;
}

std::string AssetDatabase::importAsset(const std::string& sourcePath, const std::string& type) {
    if (sourcePath.empty()) {
        return "";
    }
    std::string normalizedSource = normalizePath(sourcePath);
    if (m_RootPath.empty()) {
        return normalizedSource;
    }
    if (isUnderRoot(normalizedSource)) {
        registerAsset(normalizedSource, type);
        return normalizedSource;
    }

    std::string assetType = type;
    if (assetType.empty()) {
        std::string ext = ToLower(std::filesystem::path(sourcePath).extension().string());
        if (!isAssetFile(ext, assetType)) {
            return normalizePath(sourcePath);
        }
    }

    std::string subdir = "Assets";
    if (assetType == "model") {
        subdir = "Models";
    } else if (assetType == "texture") {
        subdir = "Textures";
    } else if (assetType == "hdri") {
        subdir = "HDRI";
    }

    std::filesystem::path root(m_RootPath);
    std::filesystem::path targetDir = root / subdir;
    std::error_code ec;
    std::filesystem::create_directories(targetDir, ec);

    std::filesystem::path source = normalizedSource;
    std::filesystem::path filename = source.filename();
    std::filesystem::path target = targetDir / filename;

    int counter = 1;
    while (std::filesystem::exists(target, ec)) {
        std::string stem = filename.stem().string();
        std::string ext = filename.extension().string();
        std::string newName = stem + "_" + std::to_string(counter++) + ext;
        target = targetDir / newName;
    }

    std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return normalizePath(sourcePath);
    }

    std::string targetPath = normalizePath(target.string());
    registerAsset(targetPath, assetType);
    return targetPath;
}

std::string AssetDatabase::getGuidForPath(const std::string& absolutePath) const {
    auto it = m_PathToGuid.find(normalizePath(absolutePath));
    if (it != m_PathToGuid.end()) {
        return it->second;
    }
    return "";
}

std::string AssetDatabase::getPathForGuid(const std::string& guid) const {
    auto it = m_GuidToRecord.find(guid);
    if (it == m_GuidToRecord.end()) {
        return "";
    }
    return resolvePath(it->second.relativePath);
}

std::string AssetDatabase::getRelativePath(const std::string& absolutePath) const {
    if (m_RootPath.empty()) {
        return absolutePath;
    }
    std::error_code ec;
    std::filesystem::path root(m_RootPath);
    std::filesystem::path path(absolutePath);
    std::filesystem::path rel = std::filesystem::relative(path, root, ec);
    if (ec) {
        return absolutePath;
    }
    return rel.generic_string();
}

std::string AssetDatabase::resolvePath(const std::string& storedPath) const {
    if (storedPath.empty()) {
        return "";
    }
    std::filesystem::path path(storedPath);
    if (path.is_absolute() || m_RootPath.empty()) {
        return storedPath;
    }
    std::filesystem::path root(m_RootPath);
    return (root / path).lexically_normal().string();
}

bool AssetDatabase::isAssetFile(const std::string& extension, std::string& outType) const {
    static const std::unordered_set<std::string> kModels = {
        ".fbx", ".obj", ".gltf", ".glb", ".dae", ".blend", ".3ds",
        ".stl", ".ply", ".x", ".smd", ".md5mesh", ".md2", ".md3", ".ms3d", ".lwo", ".lws"
    };
    static const std::unordered_set<std::string> kTextures = {
        ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".gif", ".tif", ".tiff", ".ktx", ".ktx2", ".dds", ".cube"
    };
    static const std::unordered_set<std::string> kHdri = {
        ".hdr", ".exr"
    };
    if (kModels.count(extension) > 0) {
        outType = "model";
        return true;
    }
    if (kHdri.count(extension) > 0) {
        outType = "hdri";
        return true;
    }
    if (kTextures.count(extension) > 0) {
        outType = "texture";
        return true;
    }
    return false;
}

bool AssetDatabase::isUnderRoot(const std::string& path) const {
    if (m_RootPath.empty()) {
        return false;
    }
    std::string normalized = normalizePath(path);
    if (normalized.size() < m_RootPath.size()) {
        return false;
    }
    if (normalized == m_RootPath) {
        return true;
    }
    return normalized.rfind(m_RootPath + "/", 0) == 0;
}

std::string AssetDatabase::normalizePath(const std::string& path) const {
    std::error_code ec;
    std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
    if (ec) {
        normalized = std::filesystem::path(path).lexically_normal();
    }
    return normalized.string();
}

std::string AssetDatabase::metaPathForAsset(const std::string& absolutePath) const {
    return absolutePath + ".cmeta";
}

std::string AssetDatabase::loadOrCreateGuid(const std::string& absolutePath, const std::string& type) {
    if (absolutePath.empty() || type.empty()) {
        return "";
    }
    std::string metaPath = metaPathForAsset(absolutePath);
    std::ifstream in(metaPath);
    if (in.good()) {
        json meta = json::parse(in, nullptr, false);
        if (meta.is_object()) {
            std::string guid = meta.value("guid", "");
            if (!guid.empty()) {
                return guid;
            }
        }
    }

    std::filesystem::path metaDir = std::filesystem::path(metaPath).parent_path();
    std::error_code ec;
    std::filesystem::create_directories(metaDir, ec);

    UUID uuid;
    std::string guid = uuid.toString();
    json meta = {
        {"guid", guid},
        {"type", type}
    };
    std::ofstream out(metaPath);
    if (out.is_open()) {
        out << meta.dump(2);
    }
    return guid;
}

} // namespace Crescent
