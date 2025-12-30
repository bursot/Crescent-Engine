#pragma once

#include "../ECS/Component.hpp"
#include "../Scene/SceneCommands.hpp"
#include <string>

namespace Crescent {

class ModelMeshReference : public Component {
public:
    ModelMeshReference() = default;

    COMPONENT_TYPE(ModelMeshReference)

    const std::string& getSourcePath() const { return m_SourcePath; }
    void setSourcePath(const std::string& path) { m_SourcePath = path; }

    const std::string& getSourceGuid() const { return m_SourceGuid; }
    void setSourceGuid(const std::string& guid) { m_SourceGuid = guid; }

    int getMeshIndex() const { return m_MeshIndex; }
    void setMeshIndex(int index) { m_MeshIndex = index; }

    int getMaterialIndex() const { return m_MaterialIndex; }
    void setMaterialIndex(int index) { m_MaterialIndex = index; }

    bool isSkinned() const { return m_IsSkinned; }
    void setSkinned(bool skinned) { m_IsSkinned = skinned; }

    bool isMerged() const { return m_IsMerged; }
    void setMerged(bool merged) { m_IsMerged = merged; }

    const std::string& getMeshName() const { return m_MeshName; }
    void setMeshName(const std::string& name) { m_MeshName = name; }

    const SceneCommands::ModelImportOptions& getImportOptions() const { return m_ImportOptions; }
    void setImportOptions(const SceneCommands::ModelImportOptions& options) { m_ImportOptions = options; }

private:
    std::string m_SourcePath;
    std::string m_SourceGuid;
    std::string m_MeshName;
    int m_MeshIndex = -1;
    int m_MaterialIndex = -1;
    bool m_IsSkinned = false;
    bool m_IsMerged = false;
    SceneCommands::ModelImportOptions m_ImportOptions;
};

} // namespace Crescent
