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

    enum class ViewMode {
        Scene = 0,
        Game = 1
    };
    
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
    class Camera* getSceneCamera() const;
    class Camera* getGameCamera() const;
    
    const std::vector<std::unique_ptr<Scene>>& getAllScenes() const { 
        return m_Scenes; 
    }
    
    int getSceneCount() const { return static_cast<int>(m_Scenes.size()); }
    
    // Update active scene
    void update(float deltaTime);
    void beginFrame();
    void updateStart();
    void updateEditor(float deltaTime);
    void updateFixed(float fixedStep, int steps);
    void updateFixedPhysics(float fixedStep, int steps);
    void updateFixedComponents(float fixedStep, int steps);
    void updateVariable(float deltaTime);
    float getFixedTimeStep() const;

    // Play mode
    bool isPlaying() const { return m_IsPlaying; }
    void enterPlayMode();
    void exitPlayMode();
    bool isPaused() const { return m_IsPaused; }
    void setPaused(bool paused);
    float getTimeScale() const { return m_TimeScale; }
    void setTimeScale(float scale);
    ViewMode getViewMode() const { return m_ViewMode; }
    bool isSceneView() const { return m_ViewMode == ViewMode::Scene; }
    void setViewMode(ViewMode mode);
    
private:
    SceneManager() = default;
    ~SceneManager() = default;
    
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;
    
private:
    class Camera* findPreferredCamera(Scene* scene, bool preferEditor) const;
    class Light* findFirstMainLight(Scene* scene) const;
    void applySelectionForScene(Scene* scene, const std::vector<UUID>& selection);
    void ensureEditorCamera(Scene* scene);

    std::vector<std::unique_ptr<Scene>> m_Scenes;
    Scene* m_ActiveScene = nullptr;
    Scene* m_EditorScene = nullptr;
    Scene* m_RuntimeScene = nullptr;
    bool m_IsPlaying = false;
    bool m_IsPaused = false;
    float m_TimeScale = 1.0f;
    float m_FixedAccumulator = 0.0f;
    ViewMode m_ViewMode = ViewMode::Scene;
    std::vector<UUID> m_EditorSelection;
};

} // namespace Crescent
