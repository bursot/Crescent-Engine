#include "AssetDatabase.hpp"
#include "../Core/UUID.hpp"
#include "../../../ThirdParty/nlohmann/json.hpp"
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <unordered_set>

namespace Crescent {
namespace {

using json = nlohmann::json;

constexpr int kMetaVersion = 1;

std::string ToLower(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

uint64_t ToUnixTimestamp(const std::filesystem::file_time_type& time) {
    using namespace std::chrono;
    auto systemTime = time_point_cast<system_clock::duration>(
        time - std::filesystem::file_time_type::clock::now() + system_clock::now());
    auto seconds = duration_cast<std::chrono::seconds>(systemTime.time_since_epoch());
    return static_cast<uint64_t>(seconds.count());
}

json SerializeModelSettings(const ModelImportSettings& settings) {
    return {
        {"scale", settings.scale},
        {"flipUVs", settings.flipUVs},
        {"onlyLOD0", settings.onlyLOD0},
        {"mergeStaticMeshes", settings.mergeStaticMeshes}
    };
}

ModelImportSettings DeserializeModelSettings(const json& j, const ModelImportSettings& fallback = ModelImportSettings()) {
    if (!j.is_object()) {
        return fallback;
    }
    ModelImportSettings settings = fallback;
    settings.scale = j.value("scale", settings.scale);
    settings.flipUVs = j.value("flipUVs", settings.flipUVs);
    settings.onlyLOD0 = j.value("onlyLOD0", settings.onlyLOD0);
    settings.mergeStaticMeshes = j.value("mergeStaticMeshes", settings.mergeStaticMeshes);
    return settings;
}

json SerializeTextureSettings(const TextureImportSettings& settings) {
    return {
        {"srgb", settings.srgb},
        {"generateMipmaps", settings.generateMipmaps},
        {"flipY", settings.flipY},
        {"maxSize", settings.maxSize},
        {"normalMap", settings.normalMap}
    };
}

TextureImportSettings DeserializeTextureSettings(const json& j, const TextureImportSettings& fallback = TextureImportSettings()) {
    if (!j.is_object()) {
        return fallback;
    }
    TextureImportSettings settings = fallback;
    settings.srgb = j.value("srgb", settings.srgb);
    settings.generateMipmaps = j.value("generateMipmaps", settings.generateMipmaps);
    settings.flipY = j.value("flipY", settings.flipY);
    settings.maxSize = j.value("maxSize", settings.maxSize);
    settings.normalMap = j.value("normalMap", settings.normalMap);
    return settings;
}

json SerializeHdriSettings(const HdriImportSettings& settings) {
    return {
        {"flipY", settings.flipY},
        {"maxSize", settings.maxSize}
    };
}

HdriImportSettings DeserializeHdriSettings(const json& j, const HdriImportSettings& fallback = HdriImportSettings()) {
    if (!j.is_object()) {
        return fallback;
    }
    HdriImportSettings settings = fallback;
    settings.flipY = j.value("flipY", settings.flipY);
    settings.maxSize = j.value("maxSize", settings.maxSize);
    return settings;
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

void AssetDatabase::setLibraryPath(const std::string& path) {
    m_LibraryPath = normalizePath(path);
    if (m_LibraryPath.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(m_LibraryPath, ec);
    std::filesystem::create_directories(std::filesystem::path(m_LibraryPath) / "ImportCache", ec);
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
        if (path.extension() == ".cmeta" || path.extension() == ".meta") {
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
        AssetRecord record = loadOrCreateRecord(absPath, type);
        if (record.guid.empty()) {
            continue;
        }
        record.relativePath = getRelativePath(absPath);
        m_GuidToRecord[record.guid] = record;
        m_PathToGuid[absPath] = record.guid;
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
    AssetRecord record = loadOrCreateRecord(normalized, assetType);
    if (record.guid.empty()) {
        return "";
    }
    record.relativePath = getRelativePath(normalized);
    m_GuidToRecord[record.guid] = record;
    m_PathToGuid[normalized] = record.guid;
    recordImportForGuid(record.guid);
    return record.guid;
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
    } else if (assetType == "audio") {
        subdir = "Audio";
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

bool AssetDatabase::moveAsset(const std::string& sourcePath, const std::string& targetPath, bool overwrite) {
    if (m_RootPath.empty()) {
        return false;
    }
    std::string source = normalizePath(sourcePath);
    std::string target = normalizePath(targetPath);
    if (source.empty() || target.empty()) {
        return false;
    }
    if (source == target) {
        return true;
    }
    if (!isUnderRoot(source) || !isUnderRoot(target)) {
        return false;
    }

    std::error_code ec;
    std::filesystem::path sourcePathFs(source);
    if (!std::filesystem::exists(sourcePathFs, ec) || !std::filesystem::is_regular_file(sourcePathFs, ec)) {
        return false;
    }

    std::string sourceType;
    std::string sourceExt = ToLower(sourcePathFs.extension().string());
    if (!isAssetFile(sourceExt, sourceType)) {
        return false;
    }

    std::filesystem::path targetPathFs(target);
    std::string targetType;
    std::string targetExt = ToLower(targetPathFs.extension().string());
    if (!isAssetFile(targetExt, targetType)) {
        return false;
    }
    if (sourceType != targetType) {
        return false;
    }
    if (std::filesystem::exists(targetPathFs, ec)) {
        if (!overwrite) {
            return false;
        }
        std::filesystem::remove(targetPathFs, ec);
        if (ec) {
            return false;
        }
    }

    std::string metaSource = metaPathForAsset(source);
    std::string metaTarget = metaPathForAsset(target);
    if (std::filesystem::exists(metaTarget, ec)) {
        if (!overwrite) {
            return false;
        }
        std::filesystem::remove(metaTarget, ec);
        if (ec) {
            return false;
        }
    }

    std::filesystem::create_directories(targetPathFs.parent_path(), ec);
    if (ec) {
        return false;
    }

    AssetRecord record;
    auto guidIt = m_PathToGuid.find(source);
    if (guidIt != m_PathToGuid.end()) {
        auto recordIt = m_GuidToRecord.find(guidIt->second);
        if (recordIt != m_GuidToRecord.end()) {
            record = recordIt->second;
        }
    } else {
        record = loadOrCreateRecord(source, targetType);
    }
    if (record.guid.empty()) {
        record = loadOrCreateRecord(source, targetType);
    }
    if (record.guid.empty()) {
        return false;
    }
    record.type = targetType;
    record.relativePath = getRelativePath(target);

    std::filesystem::rename(sourcePathFs, targetPathFs, ec);
    if (ec) {
        ec.clear();
        std::filesystem::copy_file(sourcePathFs, targetPathFs,
                                   overwrite ? std::filesystem::copy_options::overwrite_existing
                                             : std::filesystem::copy_options::none,
                                   ec);
        if (ec) {
            return false;
        }
        std::filesystem::remove(sourcePathFs, ec);
        if (ec) {
            return false;
        }
    }

    if (std::filesystem::exists(metaSource, ec)) {
        std::filesystem::rename(metaSource, metaTarget, ec);
        if (ec) {
            ec.clear();
            std::filesystem::copy_file(metaSource, metaTarget,
                                       overwrite ? std::filesystem::copy_options::overwrite_existing
                                                 : std::filesystem::copy_options::none,
                                       ec);
            if (ec) {
                return false;
            }
            std::filesystem::remove(metaSource, ec);
            if (ec) {
                return false;
            }
        }
    } else {
        if (!saveMeta(metaTarget, record)) {
            return false;
        }
    }

    m_PathToGuid.erase(source);
    m_PathToGuid[target] = record.guid;
    m_GuidToRecord[record.guid] = record;
    recordImportForGuid(record.guid);
    return true;
}

bool AssetDatabase::getRecordForGuid(const std::string& guid, AssetRecord& outRecord) const {
    auto it = m_GuidToRecord.find(guid);
    if (it == m_GuidToRecord.end()) {
        return false;
    }
    outRecord = it->second;
    return true;
}

bool AssetDatabase::getRecordForPath(const std::string& absolutePath, AssetRecord& outRecord) const {
    auto it = m_PathToGuid.find(normalizePath(absolutePath));
    if (it == m_PathToGuid.end()) {
        return false;
    }
    return getRecordForGuid(it->second, outRecord);
}

bool AssetDatabase::updateModelImportSettings(const std::string& guid, const ModelImportSettings& settings) {
    auto it = m_GuidToRecord.find(guid);
    if (it == m_GuidToRecord.end()) {
        return false;
    }
    it->second.modelSettings = settings;
    std::string assetPath = getPathForGuid(guid);
    if (assetPath.empty()) {
        return false;
    }
    if (!saveMeta(metaPathForAsset(assetPath), it->second)) {
        return false;
    }
    recordImportForGuid(guid);
    return true;
}

bool AssetDatabase::updateTextureImportSettings(const std::string& guid, const TextureImportSettings& settings) {
    auto it = m_GuidToRecord.find(guid);
    if (it == m_GuidToRecord.end()) {
        return false;
    }
    it->second.textureSettings = settings;
    std::string assetPath = getPathForGuid(guid);
    if (assetPath.empty()) {
        return false;
    }
    if (!saveMeta(metaPathForAsset(assetPath), it->second)) {
        return false;
    }
    recordImportForGuid(guid);
    return true;
}

bool AssetDatabase::updateHdriImportSettings(const std::string& guid, const HdriImportSettings& settings) {
    auto it = m_GuidToRecord.find(guid);
    if (it == m_GuidToRecord.end()) {
        return false;
    }
    it->second.hdriSettings = settings;
    std::string assetPath = getPathForGuid(guid);
    if (assetPath.empty()) {
        return false;
    }
    if (!saveMeta(metaPathForAsset(assetPath), it->second)) {
        return false;
    }
    recordImportForGuid(guid);
    return true;
}

bool AssetDatabase::recordImportForGuid(const std::string& guid) {
    if (guid.empty() || m_LibraryPath.empty()) {
        return false;
    }
    auto it = m_GuidToRecord.find(guid);
    if (it == m_GuidToRecord.end()) {
        return false;
    }
    std::string sourcePath = getPathForGuid(guid);
    if (sourcePath.empty()) {
        return false;
    }
    std::string cachePath = importCachePathForGuid(guid);
    if (cachePath.empty()) {
        return false;
    }
    return saveImportCache(cachePath, it->second, sourcePath);
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
    static const std::unordered_set<std::string> kAudio = {
        ".wav", ".mp3", ".ogg", ".flac"
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
    if (kAudio.count(extension) > 0) {
        outType = "audio";
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

std::string AssetDatabase::importCachePathForGuid(const std::string& guid) const {
    if (m_LibraryPath.empty() || guid.empty()) {
        return "";
    }
    return (std::filesystem::path(m_LibraryPath) / "ImportCache" / (guid + ".json")).string();
}

bool AssetDatabase::saveImportCache(const std::string& cachePath,
                                    const AssetRecord& record,
                                    const std::string& sourcePath) const {
    if (cachePath.empty() || record.guid.empty() || sourcePath.empty()) {
        return false;
    }
    std::error_code ec;
    std::filesystem::path cacheDir = std::filesystem::path(cachePath).parent_path();
    std::filesystem::create_directories(cacheDir, ec);

    uint64_t timestamp = 0;
    if (std::filesystem::exists(sourcePath, ec)) {
        timestamp = ToUnixTimestamp(std::filesystem::last_write_time(sourcePath, ec));
    }

    json cache;
    cache["version"] = kMetaVersion;
    cache["guid"] = record.guid;
    cache["type"] = record.type;
    cache["source"] = getRelativePath(sourcePath);
    cache["sourceTimestamp"] = timestamp;

    json import;
    if (record.type == "model") {
        import["model"] = SerializeModelSettings(record.modelSettings);
    } else if (record.type == "texture") {
        import["texture"] = SerializeTextureSettings(record.textureSettings);
    } else if (record.type == "hdri") {
        import["hdri"] = SerializeHdriSettings(record.hdriSettings);
    }
    if (!import.empty()) {
        cache["import"] = import;
    }

    std::ofstream out(cachePath);
    if (!out.is_open()) {
        return false;
    }
    out << cache.dump(2);
    return true;
}

AssetRecord AssetDatabase::loadOrCreateRecord(const std::string& absolutePath, const std::string& type) {
    AssetRecord record;
    record.type = type;
    bool loaded = false;
    bool dirty = false;
    if (!absolutePath.empty()) {
        bool needsSave = false;
        loaded = loadMeta(metaPathForAsset(absolutePath), record, needsSave);
        dirty = needsSave;
    }
    if (record.guid.empty()) {
        UUID uuid;
        record.guid = uuid.toString();
        dirty = true;
    }
    if (record.type.empty() && !type.empty()) {
        record.type = type;
        dirty = true;
    } else if (!type.empty() && record.type != type) {
        record.type = type;
        dirty = true;
    }
    if (!absolutePath.empty() && (!loaded || dirty)) {
        saveMeta(metaPathForAsset(absolutePath), record);
    }
    return record;
}

bool AssetDatabase::loadMeta(const std::string& metaPath, AssetRecord& outRecord, bool& outNeedsSave) const {
    outNeedsSave = false;
    std::ifstream in(metaPath);
    if (!in.good()) {
        return false;
    }
    json meta = json::parse(in, nullptr, false);
    if (!meta.is_object()) {
        return false;
    }
    int version = meta.value("version", 0);
    if (version != kMetaVersion) {
        outNeedsSave = true;
    }
    outRecord.guid = meta.value("guid", outRecord.guid);
    outRecord.type = meta.value("type", outRecord.type);
    if (meta.contains("import") && meta["import"].is_object()) {
        const json& import = meta["import"];
        if (import.contains("model")) {
            outRecord.modelSettings = DeserializeModelSettings(import["model"], outRecord.modelSettings);
        }
        if (import.contains("texture")) {
            outRecord.textureSettings = DeserializeTextureSettings(import["texture"], outRecord.textureSettings);
        }
        if (import.contains("hdri")) {
            outRecord.hdriSettings = DeserializeHdriSettings(import["hdri"], outRecord.hdriSettings);
        }
        if (!outRecord.type.empty()) {
            if (outRecord.type == "model" && !import.contains("model")) {
                outNeedsSave = true;
            } else if (outRecord.type == "texture" && !import.contains("texture")) {
                outNeedsSave = true;
            } else if (outRecord.type == "hdri" && !import.contains("hdri")) {
                outNeedsSave = true;
            }
        }
    } else {
        outNeedsSave = true;
    }
    return true;
}

bool AssetDatabase::saveMeta(const std::string& metaPath, const AssetRecord& record) const {
    if (metaPath.empty() || record.guid.empty()) {
        return false;
    }
    std::filesystem::path metaDir = std::filesystem::path(metaPath).parent_path();
    std::error_code ec;
    std::filesystem::create_directories(metaDir, ec);

    json meta;
    meta["version"] = kMetaVersion;
    meta["guid"] = record.guid;
    meta["type"] = record.type;

    json import;
    if (record.type == "model") {
        import["model"] = SerializeModelSettings(record.modelSettings);
    } else if (record.type == "texture") {
        import["texture"] = SerializeTextureSettings(record.textureSettings);
    } else if (record.type == "hdri") {
        import["hdri"] = SerializeHdriSettings(record.hdriSettings);
    }
    if (!import.empty()) {
        meta["import"] = import;
    }

    std::ofstream out(metaPath);
    if (!out.is_open()) {
        return false;
    }
    out << meta.dump(2);
    return true;
}

} // namespace Crescent
