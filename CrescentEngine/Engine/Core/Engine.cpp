#include "Engine.hpp"
#include "SelectionSystem.hpp"
#include "../Renderer/Renderer.hpp"
#include "../Renderer/DebugRenderer.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Scene/SceneCommands.hpp"
#include "../Components/Camera.hpp"
#include "../Components/CameraController.hpp"
#include "../Components/MeshRenderer.hpp"
#include "../Components/Light.hpp"
#include "../Audio/AudioSystem.hpp"
#include "../Physics/PhysicsWorld.hpp"
#include "../ECS/Entity.hpp"
#include "../ECS/Transform.hpp"
#include "../Input/InputManager.hpp"
#include <iostream>

namespace {
constexpr bool kInputDebug = false;
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
    
    std::cout << "Selection and Gizmo systems created" << std::endl;
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
    
    // CRITICAL FIX: Update scene FIRST (to read input from previous frame)
    // Then clear input state for next frame
    // This fixes the mouse delta timing issue
    
    // Update active scene - Components read input here
    SceneManager::getInstance().update(deltaTime);

    Camera* listenerCamera = nullptr;
    SceneManager& sceneManager = SceneManager::getInstance();
    if (sceneManager.isPlaying()) {
        listenerCamera = sceneManager.getGameCamera();
    }
    if (!listenerCamera) {
        listenerCamera = sceneManager.getSceneCamera();
    }
    AudioSystem::getInstance().updateListenerFromCamera(listenerCamera);
    
    // Clear frame-based input (mouse delta, etc.) for next frame
    // Previous key/button states are preserved for isKeyDown/isKeyUp
    InputManager::getInstance().update();
}

void Engine::render() {
    if (!m_isInitialized || !m_renderer) {
        return;
    }
    
    Scene* activeScene = SceneManager::getInstance().getActiveScene();
    if (!activeScene) {
        return;
    }

    bool hasSceneSurface = m_sceneSurface.isValid();
    bool hasGameSurface = m_gameSurface.isValid();
    if (!hasSceneSurface && !hasGameSurface) {
        m_renderer->setRenderTargetPool(Renderer::RenderTargetPool::Scene);
        m_renderer->render();
        return;
    }

    auto viewMode = SceneManager::getInstance().getViewMode();
    bool renderSceneSurface = hasSceneSurface && (viewMode == SceneManager::ViewMode::Scene || !hasGameSurface);
    bool renderGameSurface = hasGameSurface && (viewMode == SceneManager::ViewMode::Game || !hasSceneSurface);

    auto* debugRenderer = m_renderer->getDebugRenderer();
    const auto& selection = SelectionSystem::getSelection();
    static int renderDebugCount = 0;

    // Scene view render (editor camera)
    if (renderSceneSurface) {
        m_renderer->setRenderTargetPool(Renderer::RenderTargetPool::Scene);
        m_renderer->setMetalLayer(m_sceneSurface.layer, false);
        m_renderer->setViewportSize(m_sceneSurface.width, m_sceneSurface.height, true);
        if (debugRenderer) {
            debugRenderer->clear();
        }

        if (renderDebugCount < 5) {
            if (!selection.empty()) {
                std::cout << "[RENDER] Drawing gizmo for " << selection.size() << " entities" << std::endl;
            } else {
                std::cout << "[RENDER] No selection, skipping gizmo" << std::endl;
            }
            renderDebugCount++;
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

void Engine::resizeScene(float width, float height) {
    m_sceneSurface.width = width;
    m_sceneSurface.height = height;
}

void Engine::resizeGame(float width, float height) {
    m_gameSurface.width = width;
    m_gameSurface.height = height;
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
        
        const char* modeName = "Unknown";
        switch (mode) {
            case GizmoMode::Translate: modeName = "Translate"; break;
            case GizmoMode::Rotate: modeName = "Rotate"; break;
            case GizmoMode::Scale: modeName = "Scale"; break;
        }
        std::cout << "Gizmo mode: " << modeName << std::endl;
    }
}

void Engine::setGizmoSpace(GizmoSpace space) {
    m_currentGizmoSpace = space;
    if (m_gizmoSystem) {
        m_gizmoSystem->setSpace(space);
        std::cout << "Gizmo space: " << (space == GizmoSpace::World ? "World" : "Local") << std::endl;
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
