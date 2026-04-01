#include "Engine.hpp"
#include "SelectionSystem.hpp"
#include "TaskGraph.hpp"
#include "Time.hpp"
#include "../Renderer/Renderer.hpp"
#include "../Renderer/DebugRenderer.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Scene/SceneCommands.hpp"
#include "../Scene/SceneSerializer.hpp"
#include "../Components/Camera.hpp"
#include "../Components/CameraController.hpp"
#include "../Components/Animator.hpp"
#include "../Components/MeshRenderer.hpp"
#include "../Components/SkinnedMeshRenderer.hpp"
#include "../Components/Light.hpp"
#include "../Audio/AudioSystem.hpp"
#include "../Physics/PhysicsWorld.hpp"
#include "../ECS/Entity.hpp"
#include "../ECS/Transform.hpp"
#include "../Input/InputManager.hpp"
#include <iostream>
#include <limits>

namespace {
constexpr bool kInputDebug = false;

static void EncapsulateTransformedLocalAABB(const Crescent::Math::Matrix4x4& worldMatrix,
                                            const Crescent::Math::Vector3& localMin,
                                            const Crescent::Math::Vector3& localMax,
                                            Crescent::AABB& outBounds,
                                            bool& hasBounds) {
    using namespace Crescent;
    Math::Vector3 worldMin;
    Math::Vector3 worldMax;
    worldMatrix.transformAABB(localMin, localMax, worldMin, worldMax);

    AABB transformed(worldMin, worldMax);
    if (!hasBounds) {
        outBounds = transformed;
        hasBounds = true;
    } else {
        outBounds.encapsulate(transformed);
    }
}

static void AccumulateRenderableHierarchyBounds(Crescent::Entity* entity,
                                                Crescent::AABB& outBounds,
                                                bool& hasBounds) {
    using namespace Crescent;
    if (!entity) {
        return;
    }

    Transform* transform = entity->getTransform();
    if (transform) {
        const Math::Matrix4x4 worldMatrix = transform->getWorldMatrix();
        if (auto* meshRenderer = entity->getComponent<MeshRenderer>()) {
            if (auto mesh = meshRenderer->getMesh()) {
                EncapsulateTransformedLocalAABB(worldMatrix, mesh->getBoundsMin(), mesh->getBoundsMax(), outBounds, hasBounds);
            }
        }
        if (auto* skinnedRenderer = entity->getComponent<SkinnedMeshRenderer>()) {
            if (skinnedRenderer->getMesh()) {
                EncapsulateTransformedLocalAABB(worldMatrix,
                                               skinnedRenderer->getBoundsMin(),
                                               skinnedRenderer->getBoundsMax(),
                                               outBounds,
                                               hasBounds);
            }
        }

        for (Transform* child : transform->getChildren()) {
            if (!child) {
                continue;
            }
            AccumulateRenderableHierarchyBounds(child->getEntity(), outBounds, hasBounds);
        }
    }
}

static Crescent::AABB ComputeRenderableHierarchyBounds(Crescent::Entity* entity) {
    Crescent::AABB bounds;
    bool hasBounds = false;
    AccumulateRenderableHierarchyBounds(entity, bounds, hasBounds);
    if (hasBounds) {
        return bounds;
    }
    return Crescent::SelectionSystem::getEntityBounds(entity);
}

static void CollectSkinnedPreviewTargets(Crescent::Entity* entity,
                                         std::vector<Crescent::SkinnedMeshRenderer*>& outTargets) {
    if (!entity) {
        return;
    }
    if (auto* skinned = entity->getComponent<Crescent::SkinnedMeshRenderer>()) {
        outTargets.push_back(skinned);
    }
    if (Crescent::Transform* transform = entity->getTransform()) {
        for (Crescent::Transform* child : transform->getChildren()) {
            if (!child) {
                continue;
            }
            CollectSkinnedPreviewTargets(child->getEntity(), outTargets);
        }
    }
}

static Crescent::Animator* FindAnimatorPreviewTarget(Crescent::Entity* entity) {
    if (!entity) {
        return nullptr;
    }
    if (auto* animator = entity->getComponent<Crescent::Animator>()) {
        return animator;
    }
    if (Crescent::Transform* transform = entity->getTransform()) {
        for (Crescent::Transform* child : transform->getChildren()) {
            if (!child) {
                continue;
            }
            if (auto* animator = FindAnimatorPreviewTarget(child->getEntity())) {
                return animator;
            }
        }
    }
    return nullptr;
}

static void DestroyLegacyAnimationPreviewArtifacts(Crescent::Scene* scene) {
    if (!scene) {
        return;
    }

    std::vector<Crescent::Entity*> toDestroy;
    for (const auto& entry : scene->getAllEntities()) {
        Crescent::Entity* entity = entry.get();
        if (!entity || !entity->isEditorOnly()) {
            continue;
        }
        if (entity->getName() == "Animation Preview Camera") {
            toDestroy.push_back(entity);
        }
    }

    for (Crescent::Entity* entity : toDestroy) {
        scene->destroyEntity(entity);
    }
}

static bool IsSameOrAncestorOf(Crescent::Entity* candidate, Crescent::Entity* target) {
    if (!candidate || !target) {
        return false;
    }
    Crescent::Entity* cursor = target;
    while (cursor) {
        if (cursor == candidate) {
            return true;
        }
        Crescent::Transform* transform = cursor->getTransform();
        Crescent::Transform* parent = transform ? transform->getParent() : nullptr;
        cursor = parent ? parent->getEntity() : nullptr;
    }
    return false;
}

static bool IsSameOrDescendantOf(Crescent::Entity* candidate, Crescent::Entity* target) {
    return IsSameOrAncestorOf(target, candidate);
}

static bool IsPreviewTargetRelated(Crescent::Entity* entity, Crescent::Entity* target) {
    return IsSameOrAncestorOf(entity, target) || IsSameOrDescendantOf(entity, target);
}

static void PruneAnimationPreviewScene(Crescent::Scene* scene, Crescent::Entity* target) {
    if (!scene || !target) {
        return;
    }

    std::vector<Crescent::Entity*> toDestroy;
    for (const auto& entry : scene->getAllEntities()) {
        Crescent::Entity* entity = entry.get();
        if (!entity) {
            continue;
        }
        if (!IsPreviewTargetRelated(entity, target)) {
            toDestroy.push_back(entity);
        }
    }

    for (Crescent::Entity* entity : toDestroy) {
        scene->destroyEntity(entity);
    }
}
}

namespace Crescent {

Engine* Engine::s_instance = nullptr;

Engine::Engine() 
    : m_isInitialized(false)
    , m_currentGizmoMode(GizmoMode::Translate)
    , m_currentGizmoSpace(GizmoSpace::World)
    , m_isLeftMouseDown(false)
    , m_lastMouseX(0.0f)
    , m_lastMouseY(0.0f) {
    s_instance = this;
    
    // Initialize selection and gizmo systems
    m_selectionSystem = std::make_unique<SelectionSystem>();
    m_gizmoSystem = std::make_unique<GizmoSystem>();
    
}

Engine::~Engine() {
    shutdown();
    s_instance = nullptr;
}

bool Engine::initialize() {
    if (m_isInitialized) {
        std::cout << "Engine already initialized!" << std::endl;
        return true;
    }
    
    std::cout << "============================================" << std::endl;
    std::cout << "   Initializing Crescent Engine..." << std::endl;
    std::cout << "============================================" << std::endl;
    
    // Create renderer
    m_renderer = std::make_unique<Renderer>();
    if (!m_renderer->initialize()) {
        std::cerr << "Failed to initialize renderer!" << std::endl;
        return false;
    }

    if (!AudioSystem::getInstance().initialize()) {
        std::cerr << "Failed to initialize audio system!" << std::endl;
    }
    
    // Create default scene
    Scene* defaultScene = SceneManager::getInstance().createScene("Default Scene");
    
    // CREATE GAME CAMERA
    Entity* cameraEntity = SceneCommands::createCamera(defaultScene, "Main Camera");
    if (cameraEntity) {
        std::cout << "Main Camera created for runtime" << std::endl;
    }
    
    // CREATE DIRECTIONAL LIGHT
    Entity* lightEntity = defaultScene->createEntity("Directional Light");
    Light* light = lightEntity->addComponent<Light>();
    light->setType(Light::Type::Directional);
    light->setColor(Math::Vector3(1.0f, 0.95f, 0.9f));
    light->setIntensity(2.5f);
    
    lightEntity->getTransform()->setRotation(
        Math::Quaternion::FromEulerAngles(Math::Vector3(-45.0f * Math::DEG_TO_RAD, 45.0f * Math::DEG_TO_RAD, 0))
    );
    
    std::cout << "Directional light created" << std::endl;
    
    std::cout << "Scene created: camera + directional light (grid only)" << std::endl;
    
    // Initialize gizmo system with debug renderer
    if (m_renderer && m_renderer->getDebugRenderer()) {
        m_gizmoSystem->initialize(m_renderer->getDebugRenderer());
        m_gizmoSystem->setMode(m_currentGizmoMode);
        m_gizmoSystem->setSpace(m_currentGizmoSpace);
        std::cout << "Gizmo system initialized (Translate, World space)" << std::endl;
    }

    m_updateJobs.start();
    m_physicsJobs.start(1);
    m_audioJobs.start(1);
    m_renderJobs.start(1);
    m_framePacer.setMaxDelta(0.05f);
    m_framePacer.setMaxSteps(5);
    m_framePacer.setMaxAccumulatorMultiplier(2.0f);
    m_framePacer.setSmoothing(0.1f);
    m_framePacer.reset();
    m_lastPlaying = false;
    
    m_isInitialized = true;
    std::cout << "============================================" << std::endl;
    std::cout << "   Crescent Engine Initialized!" << std::endl;
    std::cout << "   Total Entities: " << defaultScene->getEntityCount() << std::endl;
    std::cout << "============================================" << std::endl;
    return true;
}

void Engine::shutdown() {
    if (!m_isInitialized) {
        return;
    }
    
    std::cout << "Shutting down Crescent Engine..." << std::endl;

    m_renderJobs.stop();
    m_audioJobs.stop();
    m_physicsJobs.stop();
    m_updateJobs.stop();
    
    // Destroy all scenes
    SceneManager::getInstance().destroyAllScenes();

    AudioSystem::getInstance().shutdown();
    
    // Shutdown renderer
    if (m_renderer) {
        m_renderer->shutdown();
        m_renderer.reset();
    }
    
    m_isInitialized = false;
    std::cout << "Crescent Engine shutdown complete." << std::endl;
}

void Engine::update(float deltaTime) {
    if (!m_isInitialized) {
        return;
    }
    
    InputManager& input = InputManager::getInstance();
    if (SceneManager::getInstance().isSceneView()) {
        // Handle gizmo shortcuts
        if (input.isKeyDown(KeyCode::Q)) {
            toggleGizmoMode();
        }
        if (input.isKeyDown(KeyCode::E)) {
            toggleGizmoSpace();
        }
        
        // CRITICAL: Clear mouse delta if gizmo is manipulating
        // This prevents camera from moving during gizmo drag
        if (m_gizmoSystem && m_gizmoSystem->isManipulating()) {
            input.setMouseDelta(Math::Vector2::Zero);
        }
    }
    
    SceneManager& sceneManager = SceneManager::getInstance();
    if (!sceneManager.getActiveScene()) {
        input.update();
        return;
    }

    bool isPlaying = sceneManager.isPlaying();
    if (isPlaying != m_lastPlaying) {
        m_framePacer.reset();
        m_lastPlaying = isPlaying;
    }

    float unscaledDelta = m_framePacer.prepareDelta(deltaTime);
    sceneManager.beginFrame();

    if (isPlaying && sceneManager.isSceneView()) {
        sceneManager.updateEditor(unscaledDelta);
    }

    if (!isPlaying) {
        sceneManager.updateEditor(unscaledDelta);
        JobSystem::JobHandle handle = m_audioJobs.createHandle();
        m_audioJobs.submit([&sceneManager]() {
            Camera* listenerCamera = sceneManager.getSceneCamera();
            AudioSystem::getInstance().updateListenerFromCamera(listenerCamera);
        }, handle);
        m_audioJobs.wait(handle);
        input.update();
        return;
    }

    Time::update(unscaledDelta);

    float fixedStep = sceneManager.getFixedTimeStep();
    FramePacer::Result pacing = m_framePacer.advance(Time::deltaTime(), fixedStep);
    float scaledDelta = Time::deltaTime();

    TaskGraph graph;
    auto startTask = graph.addTask("Start", [&sceneManager]() {
        sceneManager.updateStart();
    });
    auto physicsTask = graph.addTask("FixedPhysics", [this, &sceneManager, pacing]() {
        if (pacing.fixedSteps <= 0) {
            return;
        }
        JobSystem::JobHandle handle = m_physicsJobs.createHandle();
        m_physicsJobs.submit([&sceneManager, pacing]() {
            sceneManager.updateFixedPhysics(pacing.fixedStep, pacing.fixedSteps);
        }, handle);
        m_physicsJobs.wait(handle);
    });
    auto fixedComponentsTask = graph.addTask("FixedUpdate", [&sceneManager, pacing]() {
        sceneManager.updateFixedComponents(pacing.fixedStep, pacing.fixedSteps);
    });
    auto updateTask = graph.addTask("Update", [&sceneManager, scaledDelta]() {
        sceneManager.updateVariable(scaledDelta);
    });
    graph.addDependency(physicsTask, startTask);
    graph.addDependency(fixedComponentsTask, physicsTask);
    graph.addDependency(updateTask, fixedComponentsTask);

    auto audioTask = graph.addTask("Audio", [this, &sceneManager]() {
        JobSystem::JobHandle handle = m_audioJobs.createHandle();
        m_audioJobs.submit([&sceneManager]() {
            Camera* listenerCamera = nullptr;
            if (sceneManager.isPlaying()) {
                listenerCamera = sceneManager.getGameCamera();
            }
            if (!listenerCamera) {
                listenerCamera = sceneManager.getSceneCamera();
            }
            AudioSystem::getInstance().updateListenerFromCamera(listenerCamera);
        }, handle);
        m_audioJobs.wait(handle);
    });
    graph.addDependency(audioTask, updateTask);

    JobSystem::JobHandle handle = graph.run(m_updateJobs);
    m_updateJobs.wait(handle);

    input.update();
}

void Engine::render() {
    if (!m_isInitialized || !m_renderer) {
        return;
    }
    
    JobSystem::JobHandle handle = m_renderJobs.submit([this]() {
        Scene* activeScene = SceneManager::getInstance().getActiveScene();
        if (!activeScene) {
            return;
        }

    bool hasSceneSurface = m_sceneSurface.isValid();
    bool hasGameSurface = m_gameSurface.isValid();
    bool hasPreviewSurface = m_previewSurface.isValid();
    if (!hasSceneSurface && !hasGameSurface && !hasPreviewSurface) {
        m_renderer->setRenderTargetPool(Renderer::RenderTargetPool::Scene);
        m_renderer->render();
        return;
    }

        auto viewMode = SceneManager::getInstance().getViewMode();
        bool renderSceneSurface = hasSceneSurface && (viewMode == SceneManager::ViewMode::Scene || !hasGameSurface);
        bool renderGameSurface = hasGameSurface && (viewMode == SceneManager::ViewMode::Game || !hasSceneSurface);
        bool renderPreviewSurface = hasPreviewSurface;

        auto* debugRenderer = m_renderer->getDebugRenderer();
        const auto& selection = SelectionSystem::getSelection();

        // Scene view render (editor camera)
        if (renderSceneSurface) {
            m_renderer->setRenderTargetPool(Renderer::RenderTargetPool::Scene);
            m_renderer->setMetalLayer(m_sceneSurface.layer, false);
            m_renderer->setViewportSize(m_sceneSurface.width, m_sceneSurface.height, true);
            if (debugRenderer) {
                debugRenderer->setGridEnabled(true);
                debugRenderer->clear();
            }

            Camera* sceneCamera = SceneManager::getInstance().getSceneCamera();
            if (sceneCamera && m_gizmoSystem) {
                float viewportWidth = m_renderer->getViewportWidth();
                float viewportHeight = m_renderer->getViewportHeight();
                Math::Vector2 screenSize(viewportWidth, viewportHeight);

                if (!selection.empty()) {
                    for (Entity* entity : selection) {
                        m_gizmoSystem->drawSelectionBox(entity);
                    }
                    Entity* primarySelection = selection.front();
                    m_gizmoSystem->drawGizmo(primarySelection, sceneCamera, screenSize);
                } else {
                    m_gizmoSystem->hideGizmoMesh();
                }
            }

            if (activeScene && debugRenderer) {
                if (auto* physics = activeScene->getPhysicsWorld()) {
                    physics->debugDraw(debugRenderer);
                }
            }

            Renderer::RenderOptions sceneOptions;
            sceneOptions.allowTemporal = false;
            sceneOptions.updateHistory = false;
            m_renderer->renderScene(activeScene, sceneCamera, sceneOptions);
        }

        // Game view render (runtime camera)
        if (renderGameSurface) {
            m_renderer->setRenderTargetPool(Renderer::RenderTargetPool::Game);
            m_renderer->setMetalLayer(m_gameSurface.layer, false);
            m_renderer->setViewportSize(m_gameSurface.width, m_gameSurface.height, true);
            if (debugRenderer) {
                debugRenderer->setGridEnabled(false);
                debugRenderer->clear();
            }

            Camera* gameCamera = SceneManager::getInstance().getGameCamera();
            if (gameCamera) {
                Renderer::RenderOptions gameOptions;
                gameOptions.allowTemporal = true;
                gameOptions.updateHistory = true;
                m_renderer->renderScene(activeScene, gameCamera, gameOptions);
            }
        }

        if (renderPreviewSurface) {
            Scene* previewScene = ensureAnimationPreviewScene(activeScene);
            Camera* previewCamera = ensureAnimationPreviewCamera(previewScene);
            Entity* previewTarget = resolveAnimationPreviewTarget(previewScene);
            if (previewCamera && previewTarget) {
                applyAnimationPreviewPlayback(previewTarget);
                frameAnimationPreviewCamera(previewCamera, previewTarget);
                m_renderer->setRenderTargetPool(Renderer::RenderTargetPool::Preview);
                m_renderer->setMetalLayer(m_previewSurface.layer, false);
                m_renderer->setViewportSize(m_previewSurface.width, m_previewSurface.height, true);
                if (debugRenderer) {
                    debugRenderer->setGridEnabled(false);
                    debugRenderer->clear();
                }

                Renderer::RenderOptions previewOptions;
                previewOptions.allowTemporal = false;
                previewOptions.updateHistory = false;
                m_renderer->renderScene(previewScene, previewCamera, previewOptions);
            }
        }
    });
    m_renderJobs.wait(handle);
}

void Engine::setSceneMetalLayer(void* layer) {
    m_sceneSurface.layer = layer;
    if (m_renderer && layer) {
        m_renderer->setRenderTargetPool(Renderer::RenderTargetPool::Scene);
        m_renderer->setMetalLayer(layer, true);
    }
}

void Engine::setGameMetalLayer(void* layer) {
    m_gameSurface.layer = layer;
    if (m_renderer && layer) {
        m_renderer->setRenderTargetPool(Renderer::RenderTargetPool::Game);
        m_renderer->setMetalLayer(layer, true);
    }
}

void Engine::setPreviewMetalLayer(void* layer) {
    m_previewSurface.layer = layer;
    if (m_renderer && layer) {
        m_renderer->setRenderTargetPool(Renderer::RenderTargetPool::Preview);
        m_renderer->setMetalLayer(layer, true);
    }
}

void Engine::resizeScene(float width, float height) {
    m_sceneSurface.width = width;
    m_sceneSurface.height = height;
}

void Engine::resizeGame(float width, float height) {
    m_gameSurface.width = width;
    m_gameSurface.height = height;
}

void Engine::resizePreview(float width, float height) {
    m_previewSurface.width = width;
    m_previewSurface.height = height;
}

void Engine::setAnimationPreviewTargetUUID(const std::string& uuid) {
    if (m_animationPreviewTargetUUID != uuid) {
        m_animationPreviewSceneDirty = true;
    }
    m_animationPreviewTargetUUID = uuid;
}

void Engine::setAnimationPreviewPlaybackState(const AnimationPreviewPlaybackState& state) {
    m_animationPreviewPlaybackState = state;
}

Entity* Engine::ensureAnimationPreviewCameraEntity(Scene* scene) {
    if (!scene) {
        return nullptr;
    }

    for (const auto& entry : scene->getAllEntities()) {
        Entity* entity = entry.get();
        if (!entity || !entity->isEditorOnly()) {
            continue;
        }
        Camera* camera = entity->getComponent<Camera>();
        if (camera && entity->getName() == "Animation Preview Camera") {
            return entity;
        }
    }

    Entity* previewEntity = scene->createEntity("Animation Preview Camera");
    if (!previewEntity) {
        return nullptr;
    }
    previewEntity->setEditorOnly(true);
    Camera* camera = previewEntity->addComponent<Camera>();
    camera->setEditorCamera(false);
    camera->setFieldOfView(45.0f * Math::DEG_TO_RAD);
    camera->setClearColor(Math::Vector4(0.08f, 0.09f, 0.12f, 1.0f));
    previewEntity->getTransform()->setPosition(Math::Vector3(0.0f, 1.5f, 3.0f));
    previewEntity->getTransform()->setRotation(Math::Quaternion::Identity);
    return previewEntity;
}

static Crescent::Entity* EnsureAnimationPreviewLightEntity(Crescent::Scene* scene) {
    if (!scene) {
        return nullptr;
    }

    for (const auto& entry : scene->getAllEntities()) {
        Crescent::Entity* entity = entry.get();
        if (!entity) {
            continue;
        }
        if (entity->getName() != "Animation Preview Light") {
            continue;
        }
        if (auto* light = entity->getComponent<Crescent::Light>()) {
            light->setType(Crescent::Light::Type::Directional);
            return entity;
        }
    }

    Crescent::Entity* lightEntity = scene->createEntity("Animation Preview Light");
    if (!lightEntity) {
        return nullptr;
    }

    auto* light = lightEntity->addComponent<Crescent::Light>();
    light->setType(Crescent::Light::Type::Directional);
    light->setColor(Crescent::Math::Vector3(1.0f, 0.97f, 0.92f));
    light->setIntensity(3.0f);
    light->setCastShadows(false);
    lightEntity->getTransform()->setRotation(
        Crescent::Math::Quaternion::FromEulerAngles(
            Crescent::Math::Vector3(-38.0f * Crescent::Math::DEG_TO_RAD,
                                    32.0f * Crescent::Math::DEG_TO_RAD,
                                    0.0f)));
    return lightEntity;
}

Camera* Engine::ensureAnimationPreviewCamera(Scene* scene) {
    Entity* entity = ensureAnimationPreviewCameraEntity(scene);
    return entity ? entity->getComponent<Camera>() : nullptr;
}

Scene* Engine::ensureAnimationPreviewScene(Scene* sourceScene) {
    if (!sourceScene || m_animationPreviewTargetUUID.empty()) {
        return nullptr;
    }

    DestroyLegacyAnimationPreviewArtifacts(sourceScene);

    if (!m_animationPreviewScene) {
        m_animationPreviewScene = std::make_unique<Scene>("Animation Preview Scene");
        m_animationPreviewSceneDirty = true;
    }

    if (m_animationPreviewSceneDirty || m_animationPreviewSourceScene != sourceScene) {
        std::string snapshot = SceneSerializer::SerializeScene(sourceScene, false);
        if (!SceneSerializer::DeserializeScene(m_animationPreviewScene.get(), snapshot)) {
            return nullptr;
        }
        Entity* target = resolveAnimationPreviewTarget(m_animationPreviewScene.get());
        if (!target) {
            return nullptr;
        }
        PruneAnimationPreviewScene(m_animationPreviewScene.get(), target);
        EnsureAnimationPreviewLightEntity(m_animationPreviewScene.get());
        m_animationPreviewSourceScene = sourceScene;
        m_animationPreviewSceneDirty = false;
    }

    return m_animationPreviewScene.get();
}

Entity* Engine::resolveAnimationPreviewTarget(Scene* scene) {
    if (!scene || m_animationPreviewTargetUUID.empty()) {
        return nullptr;
    }
    return SceneCommands::getEntityByUUID(scene, m_animationPreviewTargetUUID);
}

void Engine::applyAnimationPreviewPlayback(Entity* targetEntity) {
    if (!targetEntity) {
        return;
    }

    if (Animator* animator = FindAnimatorPreviewTarget(targetEntity)) {
        animator->setEnabled(false);
    }

    std::vector<SkinnedMeshRenderer*> targets;
    CollectSkinnedPreviewTargets(targetEntity, targets);
    for (auto* target : targets) {
        if (!target) {
            continue;
        }
        target->setDrivenByAnimator(false);
        target->setActiveClipIndex(m_animationPreviewPlaybackState.clipIndex);
        target->setTimeSeconds(std::max(0.0f, m_animationPreviewPlaybackState.time));
        target->setLooping(m_animationPreviewPlaybackState.looping);
        target->setPlaying(true);
        target->setPlaybackSpeed(0.0f);
        target->OnUpdate(Time::fixedDeltaTime());
    }
}

void Engine::frameAnimationPreviewCamera(Camera* camera, Entity* targetEntity) {
    if (!camera || !camera->getEntity() || !targetEntity) {
        return;
    }

    AABB bounds = ComputeRenderableHierarchyBounds(targetEntity);
    Math::Vector3 center = bounds.center();
    Math::Vector3 size = bounds.size();
    float radius = std::max(0.35f, 0.5f * std::sqrt(size.x * size.x + size.y * size.y + size.z * size.z));

    Transform* targetTransform = targetEntity->getTransform();
    Math::Vector3 targetForward = targetTransform ? targetTransform->forward() : Math::Vector3(0.0f, 0.0f, -1.0f);
    targetForward.y = 0.0f;
    if (targetForward.lengthSquared() <= Math::EPSILON) {
        targetForward = Math::Vector3(0.0f, 0.0f, -1.0f);
    } else {
        targetForward.normalize();
    }
    Math::Vector3 targetRight = targetForward.cross(Math::Vector3::Up).normalized();
    Math::Vector3 focusPoint = center + Math::Vector3(0.0f, size.y * 0.2f, 0.0f);
    Math::Vector3 viewDir = (-targetForward * 1.9f) + (targetRight * 0.65f) + (Math::Vector3::Up * 0.32f);
    if (viewDir.lengthSquared() <= Math::EPSILON) {
        viewDir = Math::Vector3(0.3f, 0.2f, 1.0f);
    }
    viewDir.normalize();

    float distance = std::max(1.25f, radius * 2.6f);
    Transform* cameraTransform = camera->getEntity()->getTransform();
    Math::Vector3 cameraPos = focusPoint + viewDir * distance;
    cameraTransform->setPosition(cameraPos);
    cameraTransform->setRotation(Math::Quaternion::LookRotation((focusPoint - cameraPos).normalized(), Math::Vector3::Up));
}

// ============================================================================
// INPUT HANDLING
// ============================================================================

void Engine::handleKeyDown(unsigned short keyCode) {
    InputManager& input = InputManager::getInstance();
    
    // Map macOS key codes to our KeyCode enum
    KeyCode key = KeyCode::Unknown;
    
    switch (keyCode) {
        case 13: key = KeyCode::W; break;
        case 0:  key = KeyCode::A; break;
        case 1:  key = KeyCode::S; break;
        case 2:  key = KeyCode::D; break;
        case 12: key = KeyCode::Q; break;
        case 14: key = KeyCode::E; break;
        case 15: key = KeyCode::R; break;
        case 49: key = KeyCode::Space; break;
        case 56: key = KeyCode::Shift; break;
        case 60: key = KeyCode::Shift; break;
        case 59: key = KeyCode::Control; break;
        case 62: key = KeyCode::Control; break;
        case 58: key = KeyCode::Alt; break;
        case 61: key = KeyCode::Alt; break;
        case 55: key = KeyCode::Command; break;
        case 54: key = KeyCode::Command; break;
        case 53: key = KeyCode::Escape; break;
        default: break;
    }
    
    if (key != KeyCode::Unknown) {
        input.setKeyPressed(key, true);
    }

}

void Engine::handleKeyUp(unsigned short keyCode) {
    InputManager& input = InputManager::getInstance();
    
    // Map macOS key codes to our KeyCode enum
    KeyCode key = KeyCode::Unknown;
    
    switch (keyCode) {
        case 13: key = KeyCode::W; break;
        case 0:  key = KeyCode::A; break;
        case 1:  key = KeyCode::S; break;
        case 2:  key = KeyCode::D; break;
        case 12: key = KeyCode::Q; break;
        case 14: key = KeyCode::E; break;
        case 15: key = KeyCode::R; break;
        case 49: key = KeyCode::Space; break;
        case 56: key = KeyCode::Shift; break;
        case 60: key = KeyCode::Shift; break;
        case 59: key = KeyCode::Control; break;
        case 62: key = KeyCode::Control; break;
        case 58: key = KeyCode::Alt; break;
        case 61: key = KeyCode::Alt; break;
        case 55: key = KeyCode::Command; break;
        case 54: key = KeyCode::Command; break;
        case 53: key = KeyCode::Escape; break;
        default: break;
    }
    
    if (key != KeyCode::Unknown) {
        input.setKeyPressed(key, false);
    }

}

void Engine::handleMouseMove(float deltaX, float deltaY) {
    InputManager& input = InputManager::getInstance();
    
    // Accumulate mouse delta (will be cleared after scene update)
    Math::Vector2 currentDelta = input.getMouseDelta();
    input.setMouseDelta(Math::Vector2(currentDelta.x + deltaX, currentDelta.y + deltaY));
    
    if (kInputDebug) {
        static int debugCount = 0;
        if (debugCount < 10 && (deltaX != 0.0f || deltaY != 0.0f)) {
            std::cout << "[ENGINE] Mouse delta: (" << deltaX << ", " << deltaY
                      << ") | Total accumulated: (" << (currentDelta.x + deltaX)
                      << ", " << (currentDelta.y + deltaY) << ")" << std::endl;
            debugCount++;
        }
    }
}

void Engine::handleMouseButton(int button, bool pressed) {
    InputManager& input = InputManager::getInstance();
    
    MouseButton mouseBtn = MouseButton::Left;
    switch (button) {
        case 0: mouseBtn = MouseButton::Left; break;
        case 1: mouseBtn = MouseButton::Right; break;
        case 2: mouseBtn = MouseButton::Middle; break;
        default: return;
    }
    
    input.setMouseButtonPressed(mouseBtn, pressed);
    
    if (kInputDebug) {
        static int buttonDebugCount = 0;
        if (buttonDebugCount < 5) {
            std::cout << "[ENGINE] Mouse button " << button << " "
                      << (pressed ? "PRESSED" : "RELEASED") << std::endl;
            buttonDebugCount++;
        }
    }
}

// ============================================================================
// MOUSE PICKING & GIZMO INTERACTION
// ============================================================================

void Engine::handleMouseClick(float x, float y, float screenWidth, float screenHeight, bool additive) {
    if (!SceneManager::getInstance().isSceneView()) {
        return;
    }
    if (SceneManager::getInstance().isPlaying()) {
        return;
    }
    Scene* activeScene = SceneManager::getInstance().getActiveScene();
    Camera* mainCamera = Camera::getMainCamera();
    
    if (!activeScene || !mainCamera) return;
    
    if (kInputDebug) {
        std::cout << "\n[MOUSE CLICK] x=" << x << " y=" << y << " screen=(" << screenWidth << "," << screenHeight << ")" << std::endl;
    }
    
    m_isLeftMouseDown = true;
    m_lastMouseX = x;
    m_lastMouseY = y;
    
    // Check if we're clicking on the gizmo
    Entity* currentSelected = SelectionSystem::getSelectedEntity();
    if (currentSelected && m_gizmoSystem->isEnabled()) {
        Math::Vector2 mousePos(x, y);
        Math::Vector2 screenSize(screenWidth, screenHeight);
        
        // Try to interact with gizmo first
        m_gizmoSystem->handleMouseDown(mousePos, screenSize, currentSelected, mainCamera);
        
        // If gizmo was activated, clear mouse delta and don't do selection
        if (m_gizmoSystem->isManipulating()) {
            InputManager::getInstance().setMouseDelta(Math::Vector2::Zero);
            if (kInputDebug) {
                std::cout << "Gizmo interaction started - mouse delta cleared" << std::endl;
            }
            return;
        }
    }
    
    // No gizmo interaction, do entity selection
    Ray ray = SelectionSystem::screenPointToRay(
        Math::Vector2(x, y),
        Math::Vector2(screenWidth, screenHeight),
        mainCamera
    );
    
    if (kInputDebug) {
        std::cout << "[RAY] origin=(" << ray.origin.x << "," << ray.origin.y << "," << ray.origin.z << ")"
                  << " dir=(" << ray.direction.x << "," << ray.direction.y << "," << ray.direction.z << ")" << std::endl;
    }
    
    // Get all entities from scene
    std::vector<Entity*> entities = SceneCommands::getAllEntities(activeScene);
    if (kInputDebug) {
        std::cout << "[RAYCAST] Testing against " << entities.size() << " entities" << std::endl;
    }
    
    // Raycast
    RaycastHit hit = SelectionSystem::raycastAll(ray, entities);
    
    if (hit.hit) {
        if (additive) {
            SelectionSystem::toggleSelection(hit.entity);
        } else {
            SelectionSystem::setSelectedEntity(hit.entity);
        }
        if (kInputDebug) {
            std::cout << "[HIT SUCCESS] Selected: " << hit.entity->getName()
                      << " at distance: " << hit.distance << std::endl;
        }
    } else {
        if (kInputDebug) {
            std::cout << "[HIT FAILED] No entity hit, clearing selection" << std::endl;
        }
        SelectionSystem::clearSelection();
    }
}

void Engine::handleMouseDrag(float x, float y, float screenWidth, float screenHeight) {
    if (!SceneManager::getInstance().isSceneView()) {
        return;
    }
    if (SceneManager::getInstance().isPlaying()) {
        return;
    }
    if (!m_isLeftMouseDown) return;
    
    if (m_gizmoSystem->isManipulating()) {
        // Clear mouse delta to prevent camera movement
        InputManager::getInstance().setMouseDelta(Math::Vector2::Zero);
        
        Entity* selected = SelectionSystem::getSelectedEntity();
        Camera* mainCamera = Camera::getMainCamera();
        
        if (selected && mainCamera) {
            Math::Vector2 mousePos(x, y);
            Math::Vector2 screenSize(screenWidth, screenHeight);
            m_gizmoSystem->handleMouseDrag(mousePos, screenSize, selected, mainCamera);
        }
    }
    
    m_lastMouseX = x;
    m_lastMouseY = y;
}

void Engine::handleMouseUp() {
    if (SceneManager::getInstance().isPlaying()) {
        m_isLeftMouseDown = false;
        return;
    }
    m_isLeftMouseDown = false;
    if (m_gizmoSystem) {
        m_gizmoSystem->handleMouseUp();
    }
}

// ============================================================================
// GIZMO CONTROLS
// ============================================================================

void Engine::setGizmoMode(GizmoMode mode) {
    m_currentGizmoMode = mode;
    if (m_gizmoSystem) {
        m_gizmoSystem->setMode(mode);
        
    }
}

void Engine::setGizmoSpace(GizmoSpace space) {
    m_currentGizmoSpace = space;
    if (m_gizmoSystem) {
        m_gizmoSystem->setSpace(space);
    }
}

void Engine::toggleGizmoMode() {
    // Cycle: Translate -> Rotate -> Scale -> Translate
    switch (m_currentGizmoMode) {
        case GizmoMode::Translate:
            setGizmoMode(GizmoMode::Rotate);
            break;
        case GizmoMode::Rotate:
            setGizmoMode(GizmoMode::Scale);
            break;
        case GizmoMode::Scale:
            setGizmoMode(GizmoMode::Translate);
            break;
    }
}

void Engine::toggleGizmoSpace() {
    if (m_currentGizmoSpace == GizmoSpace::World) {
        setGizmoSpace(GizmoSpace::Local);
    } else {
        setGizmoSpace(GizmoSpace::World);
    }
}

Engine& Engine::getInstance() {
    if (s_instance == nullptr) {
        s_instance = new Engine();
    }
    return *s_instance;
}

} // namespace Crescent
