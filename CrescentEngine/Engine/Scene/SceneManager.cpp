#include "SceneManager.hpp"
#include "SceneSerializer.hpp"
#include "../Components/Camera.hpp"
#include "../Components/CameraController.hpp"
#include "../Components/Light.hpp"
#include "../Core/SelectionSystem.hpp"
#include "../Core/Time.hpp"
#include "../Physics/PhysicsWorld.hpp"
#include "../ECS/Transform.hpp"
#include "../Math/Math.hpp"
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
    m_IsPaused = false;
    m_FixedAccumulator = 0.0f;
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
        if (!m_IsPlaying || m_ViewMode == ViewMode::Scene) {
            ensureEditorCamera(m_ActiveScene);
        }
    }
    Camera::setMainCamera(findPreferredCamera(m_ActiveScene, m_ViewMode == ViewMode::Scene));
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

Camera* SceneManager::getSceneCamera() const {
    return findPreferredCamera(m_ActiveScene, true);
}

Camera* SceneManager::getGameCamera() const {
    return findPreferredCamera(m_ActiveScene, false);
}

void SceneManager::beginFrame() {
    if (!m_ActiveScene) {
        return;
    }
    Camera::setMainCamera(findPreferredCamera(m_ActiveScene, m_ViewMode == ViewMode::Scene));
    m_ActiveScene->beginFrame();
}

void SceneManager::updateStart() {
    if (!m_ActiveScene || !m_IsPlaying) {
        return;
    }
    m_ActiveScene->OnStart();
}

void SceneManager::updateEditor(float deltaTime) {
    if (!m_ActiveScene) {
        return;
    }
    m_ActiveScene->OnEditorUpdate(deltaTime);
}

void SceneManager::updateFixed(float fixedStep, int steps) {
    if (!m_ActiveScene || !m_IsPlaying) {
        return;
    }
    updateStart();
    updateFixedPhysics(fixedStep, steps);
    updateFixedComponents(fixedStep, steps);
}

void SceneManager::updateFixedPhysics(float fixedStep, int steps) {
    if (!m_ActiveScene || !m_IsPlaying || steps <= 0) {
        return;
    }
    for (int i = 0; i < steps; ++i) {
        m_ActiveScene->OnFixedPhysicsUpdate(fixedStep);
    }
}

void SceneManager::updateFixedComponents(float fixedStep, int steps) {
    if (!m_ActiveScene || !m_IsPlaying || steps <= 0) {
        return;
    }
    for (int i = 0; i < steps; ++i) {
        m_ActiveScene->OnFixedUpdate(fixedStep);
    }
}

void SceneManager::updateVariable(float deltaTime) {
    if (!m_ActiveScene || !m_IsPlaying) {
        return;
    }
    m_ActiveScene->OnUpdate(deltaTime);
}

float SceneManager::getFixedTimeStep() const {
    if (!m_ActiveScene) {
        return Time::fixedDeltaTime();
    }
    if (auto* physics = m_ActiveScene->getPhysicsWorld()) {
        return physics->getFixedTimeStep();
    }
    return Time::fixedDeltaTime();
}

void SceneManager::update(float deltaTime) {
    if (!m_ActiveScene) {
        return;
    }
    deltaTime = std::max(0.0f, std::min(deltaTime, 0.1f));

    beginFrame();

    if (m_IsPlaying && m_ViewMode == ViewMode::Scene) {
        updateEditor(deltaTime);
    }

    if (!m_IsPlaying) {
        updateEditor(deltaTime);
        return;
    }

    Time::update(deltaTime);
    updateStart();
    float fixedStep = getFixedTimeStep();
    if (!Time::isPaused()) {
        m_FixedAccumulator += Time::deltaTime();
        float maxAccumulator = fixedStep * 4.0f;
        if (m_FixedAccumulator > maxAccumulator) {
            m_FixedAccumulator = maxAccumulator;
        }
        int steps = 0;
        while (m_FixedAccumulator >= fixedStep && steps < 8) {
            m_FixedAccumulator -= fixedStep;
            steps++;
        }
        updateFixedPhysics(fixedStep, steps);
        updateFixedComponents(fixedStep, steps);
    } else {
        updateFixedPhysics(fixedStep, 0);
        updateFixedComponents(fixedStep, 0);
    }

    updateVariable(Time::deltaTime());
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
    std::string snapshot = SceneSerializer::SerializeScene(m_EditorScene, false);
    SceneSerializer::DeserializeScene(runtimeScene, snapshot);
    m_RuntimeScene = runtimeScene;
    m_IsPlaying = true;
    m_IsPaused = false;
    Time::reset();
    Time::setPaused(false);
    Time::setTimeScale(m_TimeScale);
    m_FixedAccumulator = 0.0f;
    setActiveScene(m_RuntimeScene);
    applySelectionForScene(m_RuntimeScene, m_EditorSelection);
}

void SceneManager::exitPlayMode() {
    if (!m_IsPlaying) {
        return;
    }
    m_IsPlaying = false;
    m_IsPaused = false;
    Time::setPaused(false);
    m_FixedAccumulator = 0.0f;
    if (m_EditorScene) {
        setActiveScene(m_EditorScene);
    }
    if (m_RuntimeScene) {
        destroyScene(m_RuntimeScene);
    }
    m_RuntimeScene = nullptr;
    m_EditorScene = nullptr;
    applySelectionForScene(m_ActiveScene, m_EditorSelection);
    m_EditorSelection.clear();
}

void SceneManager::setPaused(bool paused) {
    m_IsPaused = paused;
    Time::setPaused(paused);
}

void SceneManager::setTimeScale(float scale) {
    m_TimeScale = std::max(0.0f, scale);
    Time::setTimeScale(m_TimeScale);
}

void SceneManager::setViewMode(ViewMode mode) {
    if (m_ViewMode == mode) {
        return;
    }
    m_ViewMode = mode;
    if (!m_ActiveScene) {
        return;
    }
    if (m_ViewMode == ViewMode::Scene) {
        ensureEditorCamera(m_ActiveScene);
    }
    Camera::setMainCamera(findPreferredCamera(m_ActiveScene, m_ViewMode == ViewMode::Scene));
    Light::setMainLight(findFirstMainLight(m_ActiveScene));
}

Camera* SceneManager::findPreferredCamera(Scene* scene, bool preferEditor) const {
    if (!scene) {
        return nullptr;
    }
    Camera* editorCamera = nullptr;
    Camera* gameCamera = nullptr;
    for (const auto& entityPtr : scene->getAllEntities()) {
        Entity* entity = entityPtr.get();
        if (!entity || !entity->isActiveInHierarchy()) {
            continue;
        }
        if (!preferEditor && entity->isEditorOnly()) {
            continue;
        }
        if (auto* camera = entity->getComponent<Camera>()) {
            if (camera->isEditorCamera()) {
                if (!editorCamera) {
                    editorCamera = camera;
                }
                if (preferEditor) {
                    return camera;
                }
            } else {
                if (!gameCamera) {
                    gameCamera = camera;
                }
                if (!preferEditor) {
                    return camera;
                }
            }
        }
    }
    return preferEditor ? (editorCamera ? editorCamera : gameCamera)
                        : (gameCamera ? gameCamera : editorCamera);
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

void SceneManager::ensureEditorCamera(Scene* scene) {
    if (!scene) {
        return;
    }
    Camera* existingEditor = nullptr;
    for (const auto& entityPtr : scene->getAllEntities()) {
        Entity* entity = entityPtr.get();
        if (!entity) {
            continue;
        }
        Camera* camera = entity->getComponent<Camera>();
        if (camera && camera->isEditorCamera()) {
            existingEditor = camera;
            if (!entity->isActive()) {
                entity->setActive(true);
            }
            if (!entity->getComponent<CameraController>()) {
                CameraController* controller = entity->addComponent<CameraController>();
                controller->setMoveSpeed(5.0f);
                controller->setRotationSpeed(45.0f);
            }
            break;
        }
    }

    if (existingEditor) {
        return;
    }

    Entity* editorEntity = scene->createEntity("Editor Camera");
    editorEntity->setEditorOnly(true);
    Camera* camera = editorEntity->addComponent<Camera>();
    camera->setEditorCamera(true);
    camera->setFieldOfView(75.0f * Math::DEG_TO_RAD);
    camera->setClearColor(Math::Vector4(0.08f, 0.09f, 0.12f, 1.0f));

    CameraController* controller = editorEntity->addComponent<CameraController>();
    controller->setMoveSpeed(5.0f);
    controller->setRotationSpeed(45.0f);

    Transform* transform = editorEntity->getTransform();
    transform->setPosition(Math::Vector3(0.0f, 8.0f, 20.0f));
    transform->setRotation(Math::Quaternion::FromEulerAngles(
        Math::Vector3(-25.0f * Math::DEG_TO_RAD, 0, 0)));
}

} // namespace Crescent
