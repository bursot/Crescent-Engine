#include "Project.hpp"
#include "../Assets/AssetDatabase.hpp"
#include "../../../ThirdParty/nlohmann/json.hpp"
#include <filesystem>
#include <fstream>

namespace Crescent {
namespace {

using json = nlohmann::json;

std::string NormalizePath(const std::string& path) {
    std::error_code ec;
    std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
    if (ec) {
        normalized = std::filesystem::path(path).lexically_normal();
    }
    return normalized.string();
}

std::string JoinPath(const std::string& root, const std::string& sub) {
    return (std::filesystem::path(root) / sub).lexically_normal().string();
}

std::string ProjectFileName() {
    return "Project.cproj";
}

SceneQualitySettings MakeQuality(int shadowQuality,
                                 int shadowResolution,
                                 int msaaSamples,
                                 int anisotropy,
                                 float renderScale,
                                 float lodBias,
                                 int textureQuality) {
    SceneQualitySettings quality;
    quality.shadowQuality = shadowQuality;
    quality.shadowResolution = shadowResolution;
    quality.msaaSamples = msaaSamples;
    quality.anisotropy = anisotropy;
    quality.renderScale = renderScale;
    quality.lodBias = lodBias;
    quality.textureQuality = textureQuality;
    return quality;
}

std::vector<ProjectSettings::RenderProfile> DefaultRenderProfiles() {
    return {
        {"Low", MakeQuality(0, 512, 1, 2, 0.85f, 1.0f, 0)},
        {"Medium", MakeQuality(1, 1024, 2, 4, 1.0f, 0.5f, 1)},
        {"High", MakeQuality(2, 2048, 4, 8, 1.0f, 0.0f, 2)},
        {"Ultra", MakeQuality(3, 4096, 8, 16, 1.0f, -0.5f, 3)}
    };
}

std::vector<ProjectSettings::QualityPreset> DefaultQualityPresets() {
    return {
        {"Low", MakeQuality(0, 512, 1, 2, 0.85f, 1.0f, 0)},
        {"Medium", MakeQuality(1, 1024, 2, 4, 1.0f, 0.5f, 1)},
        {"High", MakeQuality(2, 2048, 4, 8, 1.0f, 0.0f, 2)},
        {"Ultra", MakeQuality(3, 4096, 8, 16, 1.0f, -0.5f, 3)}
    };
}

std::vector<ProjectSettings::InputBinding> DefaultInputBindings() {
    return {
        {"MoveForward", "W", "", 1.0f, false},
        {"MoveBackward", "S", "", 1.0f, false},
        {"MoveLeft", "A", "", 1.0f, false},
        {"MoveRight", "D", "", 1.0f, false},
        {"MoveUp", "E", "", 1.0f, false},
        {"MoveDown", "Q", "", 1.0f, false},
        {"Jump", "Space", "", 1.0f, false}
    };
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

} // namespace

std::shared_ptr<Project> Project::Create(const std::string& rootPath, const std::string& name) {
    if (rootPath.empty()) {
        return nullptr;
    }
    std::string root = NormalizePath(rootPath);
    if (root.empty()) {
        return nullptr;
    }

    auto project = std::shared_ptr<Project>(new Project());
    project->m_Name = name.empty() ? std::filesystem::path(root).filename().string() : name;
    project->m_RootPath = root;
    project->m_AssetsPath = JoinPath(root, "Assets");
    project->m_ScenesPath = JoinPath(root, "Scenes");
    project->m_LibraryPath = JoinPath(root, "Library");
    project->m_SettingsPath = JoinPath(root, "Settings");
    project->m_ProjectFilePath = JoinPath(root, ProjectFileName());
    project->m_Settings.defaultRenderProfile = "High";
    project->m_Settings.buildTarget = "macOS";
    project->m_Settings.assetPaths = {"Assets"};
    project->m_Settings.renderProfiles = DefaultRenderProfiles();
    project->m_Settings.qualityPresets = DefaultQualityPresets();
    project->m_Settings.inputBindings = DefaultInputBindings();

    std::error_code ec;
    std::filesystem::create_directories(project->m_AssetsPath, ec);
    std::filesystem::create_directories(project->m_ScenesPath, ec);
    std::filesystem::create_directories(project->m_LibraryPath, ec);
    std::filesystem::create_directories(project->m_SettingsPath, ec);

    if (!project->save()) {
        return nullptr;
    }

    return project;
}

std::shared_ptr<Project> Project::Load(const std::string& projectFilePath) {
    if (projectFilePath.empty()) {
        return nullptr;
    }
    std::ifstream in(projectFilePath);
    if (!in.is_open()) {
        return nullptr;
    }
    json data = json::parse(in, nullptr, false);
    if (data.is_discarded() || !data.is_object()) {
        return nullptr;
    }

    std::filesystem::path rootPath = std::filesystem::path(projectFilePath).parent_path();
    auto project = std::shared_ptr<Project>(new Project());
    project->m_Name = data.value("name", rootPath.filename().string());
    project->m_RootPath = NormalizePath(rootPath.string());
    project->m_AssetsPath = JoinPath(project->m_RootPath, data.value("assets", std::string("Assets")));
    project->m_ScenesPath = JoinPath(project->m_RootPath, data.value("scenes", std::string("Scenes")));
    project->m_LibraryPath = JoinPath(project->m_RootPath, data.value("library", std::string("Library")));
    project->m_SettingsPath = JoinPath(project->m_RootPath, data.value("settings", std::string("Settings")));
    project->m_ProjectFilePath = NormalizePath(projectFilePath);
    project->m_Settings.defaultRenderProfile = data.value("defaultRenderProfile", std::string("High"));
    project->m_Settings.buildTarget = data.value("buildTarget", std::string("macOS"));
    if (data.contains("assetPaths") && data["assetPaths"].is_array()) {
        project->m_Settings.assetPaths.clear();
        for (const auto& entry : data["assetPaths"]) {
            if (entry.is_string()) {
                project->m_Settings.assetPaths.push_back(entry.get<std::string>());
            }
        }
    }
    if (project->m_Settings.assetPaths.empty()) {
        project->m_Settings.assetPaths = {"Assets"};
    }
    
    if (data.contains("renderProfiles") && data["renderProfiles"].is_array()) {
        project->m_Settings.renderProfiles.clear();
        for (const auto& entry : data["renderProfiles"]) {
            if (!entry.is_object()) {
                continue;
            }
            ProjectSettings::RenderProfile profile;
            profile.name = entry.value("name", std::string("Profile"));
            if (entry.contains("quality")) {
                profile.quality = DeserializeQualitySettings(entry["quality"]);
            }
            project->m_Settings.renderProfiles.push_back(profile);
        }
    }
    if (project->m_Settings.renderProfiles.empty()) {
        project->m_Settings.renderProfiles = DefaultRenderProfiles();
    }
    bool defaultFound = false;
    for (const auto& profile : project->m_Settings.renderProfiles) {
        if (profile.name == project->m_Settings.defaultRenderProfile) {
            defaultFound = true;
            break;
        }
    }
    if (!defaultFound && !project->m_Settings.renderProfiles.empty()) {
        project->m_Settings.defaultRenderProfile = project->m_Settings.renderProfiles.front().name;
    }
    
    if (data.contains("qualityPresets") && data["qualityPresets"].is_array()) {
        project->m_Settings.qualityPresets.clear();
        for (const auto& entry : data["qualityPresets"]) {
            if (!entry.is_object()) {
                continue;
            }
            ProjectSettings::QualityPreset preset;
            preset.name = entry.value("name", std::string("Preset"));
            if (entry.contains("quality")) {
                preset.quality = DeserializeQualitySettings(entry["quality"]);
            }
            project->m_Settings.qualityPresets.push_back(preset);
        }
    }
    if (project->m_Settings.qualityPresets.empty()) {
        project->m_Settings.qualityPresets = DefaultQualityPresets();
    }
    
    if (data.contains("inputBindings") && data["inputBindings"].is_array()) {
        project->m_Settings.inputBindings.clear();
        for (const auto& entry : data["inputBindings"]) {
            if (!entry.is_object()) {
                continue;
            }
            ProjectSettings::InputBinding binding;
            binding.action = entry.value("action", std::string());
            binding.key = entry.value("key", std::string());
            binding.mouseButton = entry.value("mouseButton", std::string());
            binding.scale = entry.value("scale", binding.scale);
            binding.invert = entry.value("invert", binding.invert);
            project->m_Settings.inputBindings.push_back(binding);
        }
    }
    if (project->m_Settings.inputBindings.empty()) {
        project->m_Settings.inputBindings = DefaultInputBindings();
    }

    std::error_code ec;
    std::filesystem::create_directories(project->m_AssetsPath, ec);
    std::filesystem::create_directories(project->m_ScenesPath, ec);
    std::filesystem::create_directories(project->m_LibraryPath, ec);
    std::filesystem::create_directories(project->m_SettingsPath, ec);

    return project;
}

bool Project::save() const {
    if (m_ProjectFilePath.empty()) {
        return false;
    }
    json data = {
        {"version", 1},
        {"name", m_Name},
        {"assets", "Assets"},
        {"scenes", "Scenes"},
        {"library", "Library"},
        {"settings", "Settings"},
        {"defaultRenderProfile", m_Settings.defaultRenderProfile},
        {"buildTarget", m_Settings.buildTarget},
        {"assetPaths", m_Settings.assetPaths}
    };
    if (!m_Settings.renderProfiles.empty()) {
        json profiles = json::array();
        for (const auto& profile : m_Settings.renderProfiles) {
            profiles.push_back({
                {"name", profile.name},
                {"quality", SerializeQualitySettings(profile.quality)}
            });
        }
        data["renderProfiles"] = profiles;
    }
    if (!m_Settings.qualityPresets.empty()) {
        json presets = json::array();
        for (const auto& preset : m_Settings.qualityPresets) {
            presets.push_back({
                {"name", preset.name},
                {"quality", SerializeQualitySettings(preset.quality)}
            });
        }
        data["qualityPresets"] = presets;
    }
    if (!m_Settings.inputBindings.empty()) {
        json bindings = json::array();
        for (const auto& binding : m_Settings.inputBindings) {
            bindings.push_back({
                {"action", binding.action},
                {"key", binding.key},
                {"mouseButton", binding.mouseButton},
                {"scale", binding.scale},
                {"invert", binding.invert}
            });
        }
        data["inputBindings"] = bindings;
    }
    std::ofstream out(m_ProjectFilePath);
    if (!out.is_open()) {
        return false;
    }
    out << data.dump(2);
    return true;
}

ProjectManager& ProjectManager::getInstance() {
    static ProjectManager instance;
    return instance;
}

std::shared_ptr<Project> ProjectManager::createProject(const std::string& rootPath, const std::string& name) {
    m_ActiveProject = Project::Create(rootPath, name);
    if (m_ActiveProject) {
        AssetDatabase::getInstance().setRootPath(m_ActiveProject->getAssetsPath());
    }
    return m_ActiveProject;
}

std::shared_ptr<Project> ProjectManager::openProject(const std::string& projectFilePath) {
    m_ActiveProject = Project::Load(projectFilePath);
    if (m_ActiveProject) {
        AssetDatabase::getInstance().setRootPath(m_ActiveProject->getAssetsPath());
    }
    return m_ActiveProject;
}

void ProjectManager::closeProject() {
    m_ActiveProject.reset();
    AssetDatabase::getInstance().setRootPath("");
}

} // namespace Crescent
