#pragma once

#include "AssetImportSettings.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace Crescent {

struct AssetRecord {
    std::string guid;
    std::string relativePath;
    std::string type;
    ModelImportSettings modelSettings;
    TextureImportSettings textureSettings;
    HdriImportSettings hdriSettings;
};

class AssetDatabase {
public:
    static AssetDatabase& getInstance();

    void setRootPath(const std::string& path);
    const std::string& getRootPath() const { return m_RootPath; }
    void setLibraryPath(const std::string& path);
    const std::string& getLibraryPath() const { return m_LibraryPath; }

    void rescan();

    std::string registerAsset(const std::string& absolutePath, const std::string& type = "");
    std::string importAsset(const std::string& sourcePath, const std::string& type = "");
    std::string getGuidForPath(const std::string& absolutePath) const;
    std::string getPathForGuid(const std::string& guid) const;
    std::string getRelativePath(const std::string& absolutePath) const;
    std::string resolvePath(const std::string& storedPath) const;
    bool moveAsset(const std::string& sourcePath, const std::string& targetPath, bool overwrite = false);
    bool getRecordForGuid(const std::string& guid, AssetRecord& outRecord) const;
    bool getRecordForPath(const std::string& absolutePath, AssetRecord& outRecord) const;
    bool updateModelImportSettings(const std::string& guid, const ModelImportSettings& settings);
    bool updateTextureImportSettings(const std::string& guid, const TextureImportSettings& settings);
    bool updateHdriImportSettings(const std::string& guid, const HdriImportSettings& settings);
    bool recordImportForGuid(const std::string& guid);

private:
    AssetDatabase() = default;
    AssetDatabase(const AssetDatabase&) = delete;
    AssetDatabase& operator=(const AssetDatabase&) = delete;

    bool isAssetFile(const std::string& extension, std::string& outType) const;
    bool isUnderRoot(const std::string& path) const;
    std::string normalizePath(const std::string& path) const;
    std::string metaPathForAsset(const std::string& absolutePath) const;
    std::string importCachePathForGuid(const std::string& guid) const;
    bool saveImportCache(const std::string& cachePath, const AssetRecord& record, const std::string& sourcePath) const;
    AssetRecord loadOrCreateRecord(const std::string& absolutePath, const std::string& type);
    bool loadMeta(const std::string& metaPath, AssetRecord& outRecord, bool& outNeedsSave) const;
    bool saveMeta(const std::string& metaPath, const AssetRecord& record) const;
    void clear();

    std::string m_RootPath;
    std::string m_LibraryPath;
    std::unordered_map<std::string, AssetRecord> m_GuidToRecord;
    std::unordered_map<std::string, std::string> m_PathToGuid;
};

} // namespace Crescent
