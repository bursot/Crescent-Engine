#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace Crescent {

struct AssetRecord {
    std::string guid;
    std::string relativePath;
    std::string type;
};

class AssetDatabase {
public:
    static AssetDatabase& getInstance();

    void setRootPath(const std::string& path);
    const std::string& getRootPath() const { return m_RootPath; }

    void rescan();

    std::string registerAsset(const std::string& absolutePath, const std::string& type = "");
    std::string importAsset(const std::string& sourcePath, const std::string& type = "");
    std::string getGuidForPath(const std::string& absolutePath) const;
    std::string getPathForGuid(const std::string& guid) const;
    std::string getRelativePath(const std::string& absolutePath) const;
    std::string resolvePath(const std::string& storedPath) const;

private:
    AssetDatabase() = default;
    AssetDatabase(const AssetDatabase&) = delete;
    AssetDatabase& operator=(const AssetDatabase&) = delete;

    bool isAssetFile(const std::string& extension, std::string& outType) const;
    bool isUnderRoot(const std::string& path) const;
    std::string normalizePath(const std::string& path) const;
    std::string metaPathForAsset(const std::string& absolutePath) const;
    std::string loadOrCreateGuid(const std::string& absolutePath, const std::string& type);
    void clear();

    std::string m_RootPath;
    std::unordered_map<std::string, AssetRecord> m_GuidToRecord;
    std::unordered_map<std::string, std::string> m_PathToGuid;
};

} // namespace Crescent
