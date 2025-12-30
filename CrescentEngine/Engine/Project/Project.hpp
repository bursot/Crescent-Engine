#pragma once

#include "ProjectSettings.hpp"
#include <memory>
#include <string>

namespace Crescent {

class Project {
public:
    static std::shared_ptr<Project> Create(const std::string& rootPath, const std::string& name);
    static std::shared_ptr<Project> Load(const std::string& projectFilePath);

    const std::string& getName() const { return m_Name; }
    const std::string& getRootPath() const { return m_RootPath; }
    const std::string& getAssetsPath() const { return m_AssetsPath; }
    const std::string& getScenesPath() const { return m_ScenesPath; }
    const std::string& getLibraryPath() const { return m_LibraryPath; }
    const std::string& getSettingsPath() const { return m_SettingsPath; }
    const std::string& getProjectFilePath() const { return m_ProjectFilePath; }
    ProjectSettings& getSettings() { return m_Settings; }
    const ProjectSettings& getSettings() const { return m_Settings; }
    void setSettings(const ProjectSettings& settings) { m_Settings = settings; }

    bool save() const;

private:
    Project() = default;

    std::string m_Name;
    std::string m_RootPath;
    std::string m_AssetsPath;
    std::string m_ScenesPath;
    std::string m_LibraryPath;
    std::string m_SettingsPath;
    std::string m_ProjectFilePath;
    ProjectSettings m_Settings;
};

class ProjectManager {
public:
    static ProjectManager& getInstance();

    std::shared_ptr<Project> createProject(const std::string& rootPath, const std::string& name);
    std::shared_ptr<Project> openProject(const std::string& projectFilePath);
    void closeProject();

    std::shared_ptr<Project> getActiveProject() const { return m_ActiveProject; }

private:
    ProjectManager() = default;
    std::shared_ptr<Project> m_ActiveProject;
};

} // namespace Crescent
