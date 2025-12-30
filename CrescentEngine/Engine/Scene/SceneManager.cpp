#include "SceneManager.hpp"
#include "SceneSerializer.hpp"
#include "../Components/Camera.hpp"
#include "../Components/Light.hpp"
#include "../Core/SelectionSystem.hpp"
#include "SceneCommands.hpp"
#include <algorithm>
#include <iostream>

namespace Crescent {

SceneManager& SceneManager::getInstance() {
    static SceneManager instance;
    return instance;
}

Scene* SceneManager::createScene(const std::string& name) {
    auto scene = std::make_unique<Scene>(name);
    Scene* scenePtr = scene.get();
    
    m_Scenes.push_back(std::move(scene));
    
    // If this is the first scene, make it active
    if (m_Scenes.size() == 1) {
        setActiveScene(scenePtr);
    }
    
    return scenePtr;
}

void SceneManager::destroyScene(Scene* scene) {
    if (!scene) return;
    
    // If this is the active scene, deactivate it
    if (m_ActiveScene == scene) {
        m_ActiveScene = nullptr;
    }
    
    // Remove from vector
    auto it = std::find_if(m_Scenes.begin(), m_Scenes.end(),
        [scene](const std::unique_ptr<Scene>& s) {
            return s.get() == scene;
        });
    
    if (it != m_Scenes.end()) {
        m_Scenes.erase(it);
    }
}

void SceneManager::destroyAllScenes() {
    m_ActiveScene = nullptr;
    m_EditorScene = nullptr;
    m_RuntimeScene = nullptr;
    m_IsPlaying = false;
    m_Scenes.clear();
}

void SceneManager::setActiveScene(Scene* scene) {
    if (m_ActiveScene == scene) return;
    
    // Deactivate current scene
    if (m_ActiveScene) {
        m_ActiveScene->setActive(false);
    }
    
    // Activate new scene
    m_ActiveScene = scene;
    if (m_ActiveScene) {
        m_ActiveScene->setActive(true);
        m_ActiveScene->applySettings();
    }
    Camera::setMainCamera(findFirstCamera(m_ActiveScene));
    Light::setMainLight(findFirstMainLight(m_ActiveScene));
}

Scene* SceneManager::getSceneByName(const std::string& name) const {
    for (const auto& scene : m_Scenes) {
        if (scene->getName() == name) {
            return scene.get();
        }
    }
    return nullptr;
}

Scene* SceneManager::getSceneByUUID(UUID uuid) const {
    for (const auto& scene : m_Scenes) {
        if (scene->getUUID() == uuid) {
            return scene.get();
        }
    }
    return nullptr;
}

void SceneManager::update(float deltaTime) {
    if (m_ActiveScene) {
        m_ActiveScene->OnUpdate(deltaTime);
    }
}

void SceneManager::enterPlayMode() {
    if (m_IsPlaying || !m_ActiveScene) {
        return;
    }
    m_EditorSelection.clear();
    const auto& selection = SelectionSystem::getSelection();
    m_EditorSelection.reserve(selection.size());
    for (auto* entity : selection) {
        if (entity) {
            m_EditorSelection.push_back(entity->getUUID());
        }
    }
    m_EditorScene = m_ActiveScene;
    Scene* runtimeScene = createScene(m_EditorScene->getName() + " (Play)");
    if (!runtimeScene) {
        return;
    }
    std::string snapshot = SceneSerializer::SerializeScene(m_EditorScene);
    SceneSerializer::DeserializeScene(runtimeScene, snapshot);
    m_RuntimeScene = runtimeScene;
    setActiveScene(m_RuntimeScene);
    applySelectionForScene(m_RuntimeScene, m_EditorSelection);
    m_IsPlaying = true;
}

void SceneManager::exitPlayMode() {
    if (!m_IsPlaying) {
        return;
    }
    if (m_EditorScene) {
        setActiveScene(m_EditorScene);
    }
    if (m_RuntimeScene) {
        destroyScene(m_RuntimeScene);
    }
    m_RuntimeScene = nullptr;
    m_EditorScene = nullptr;
    m_IsPlaying = false;
    applySelectionForScene(m_ActiveScene, m_EditorSelection);
    m_EditorSelection.clear();
}

Camera* SceneManager::findFirstCamera(Scene* scene) const {
    if (!scene) {
        return nullptr;
    }
    for (const auto& entityPtr : scene->getAllEntities()) {
        Entity* entity = entityPtr.get();
        if (!entity || !entity->isActiveInHierarchy()) {
            continue;
        }
        if (auto* camera = entity->getComponent<Camera>()) {
            return camera;
        }
    }
    return nullptr;
}

Light* SceneManager::findFirstMainLight(Scene* scene) const {
    if (!scene) {
        return nullptr;
    }
    Light* firstAny = nullptr;
    for (const auto& entityPtr : scene->getAllEntities()) {
        Entity* entity = entityPtr.get();
        if (!entity || !entity->isActiveInHierarchy()) {
            continue;
        }
        if (auto* light = entity->getComponent<Light>()) {
            if (!firstAny) {
                firstAny = light;
            }
            if (light->getType() == Light::Type::Directional) {
                return light;
            }
        }
    }
    return firstAny;
}

void SceneManager::applySelectionForScene(Scene* scene, const std::vector<UUID>& selection) {
    if (!scene) {
        SelectionSystem::clearSelection();
        return;
    }
    std::vector<Entity*> entities;
    entities.reserve(selection.size());
    for (const auto& uuid : selection) {
        Entity* entity = SceneCommands::getEntityByUUID(scene, uuid.toString());
        if (entity) {
            entities.push_back(entity);
        }
    }
    if (entities.empty()) {
        SelectionSystem::clearSelection();
    } else {
        SelectionSystem::setSelection(entities);
    }
}

} // namespace Crescent
