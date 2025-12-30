#pragma once

#include "Scene.hpp"
#include "../Core/UUID.hpp"
#include <memory>
#include <vector>

namespace Crescent {

// SceneManager - manages multiple scenes and active scene
class SceneManager {
public:
    static SceneManager& getInstance();
    
    // Scene management
    Scene* createScene(const std::string& name = "New Scene");
    void destroyScene(Scene* scene);
    void destroyAllScenes();
    
    // Active scene
    Scene* getActiveScene() const { return m_ActiveScene; }
    void setActiveScene(Scene* scene);
    
    // Scene access
    Scene* getSceneByName(const std::string& name) const;
    Scene* getSceneByUUID(UUID uuid) const;
    
    const std::vector<std::unique_ptr<Scene>>& getAllScenes() const { 
        return m_Scenes; 
    }
    
    int getSceneCount() const { return static_cast<int>(m_Scenes.size()); }
    
    // Update active scene
    void update(float deltaTime);

    // Play mode
    bool isPlaying() const { return m_IsPlaying; }
    void enterPlayMode();
    void exitPlayMode();
    
private:
    SceneManager() = default;
    ~SceneManager() = default;
    
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;
    
private:
    class Camera* findFirstCamera(Scene* scene) const;
    class Light* findFirstMainLight(Scene* scene) const;
    void applySelectionForScene(Scene* scene, const std::vector<UUID>& selection);

    std::vector<std::unique_ptr<Scene>> m_Scenes;
    Scene* m_ActiveScene = nullptr;
    Scene* m_EditorScene = nullptr;
    Scene* m_RuntimeScene = nullptr;
    bool m_IsPlaying = false;
    std::vector<UUID> m_EditorSelection;
};

} // namespace Crescent
