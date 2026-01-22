#include "Scene.hpp"
#include "SceneSerializer.hpp"
#include "SceneManager.hpp"
#include "../Core/Engine.hpp"
#include "../Core/SelectionSystem.hpp"
#include "../Renderer/Renderer.hpp"
#include "../Components/Light.hpp"
#include "../Physics/PhysicsWorld.hpp"
#include "../Project/Project.hpp"
#include <algorithm>
#include <iostream>

namespace Crescent {
namespace {

SceneQualitySettings ResolveQualitySettings(const SceneSettings& settings) {
    if (settings.quality.overrideProject) {
        return settings.quality;
    }
    auto project = ProjectManager::getInstance().getActiveProject();
    if (!project) {
        return settings.quality;
    }
    const auto& projectSettings = project->getSettings();
    const std::string& profileName = projectSettings.defaultRenderProfile;
    for (const auto& profile : projectSettings.renderProfiles) {
        if (profile.name == profileName) {
            return profile.quality;
        }
    }
    return settings.quality;
}

Light::ShadowQuality MapShadowQuality(int quality) {
    switch (quality) {
    case 0: return Light::ShadowQuality::Low;
    case 1: return Light::ShadowQuality::Medium;
    case 2: return Light::ShadowQuality::High;
    default: return Light::ShadowQuality::Ultra;
    }
}

uint8_t CascadesForShadowQuality(int quality) {
    switch (quality) {
    case 0: return 2;
    case 1: return 3;
    default: return 4;
    }
}

} // namespace

Scene::Scene(const std::string& name)
    : m_UUID()
    , m_Name(name)
    , m_IsActive(true)
    , m_PhysicsWorld(std::make_unique<PhysicsWorld>(this)) {
    if (m_PhysicsWorld) {
        m_PhysicsWorld->initialize();
    }
    std::cout << "Scene created: " << m_Name << std::endl;
}

Scene::~Scene() {
    destroyAllEntities();
    if (m_PhysicsWorld) {
        m_PhysicsWorld->shutdown();
    }
    std::cout << "Scene destroyed: " << m_Name << std::endl;
}

Entity* Scene::createEntity(const std::string& name) {
    auto entity = std::make_unique<Entity>(name);
    Entity* entityPtr = entity.get();
    
    entity->setScene(this);
    m_EntityMap[entity->getUUID()] = entityPtr;
    m_Entities.push_back(std::move(entity));
    
    if (m_IsActive) {
        entityPtr->onSceneActivated();
    }
    
    return entityPtr;
}

Entity* Scene::createEntityWithUUID(UUID uuid, const std::string& name) {
    auto entity = std::make_unique<Entity>(uuid, name);
    Entity* entityPtr = entity.get();
    
    entity->setScene(this);
    m_EntityMap[entity->getUUID()] = entityPtr;
    m_Entities.push_back(std::move(entity));
    
    if (m_IsActive) {
        entityPtr->onSceneActivated();
    }
    
    return entityPtr;
}

void Scene::destroyEntity(Entity* entity) {
    if (!entity) return;

    Transform* transform = entity->getTransform();
    if (transform) {
        std::vector<Transform*> children = transform->getChildren();
        for (Transform* child : children) {
            if (!child) {
                continue;
            }
            Entity* childEntity = child->getEntity();
            if (childEntity) {
                destroyEntity(childEntity);
            }
        }
    }

    auto pendingIt = std::find(m_PendingDestroy.begin(), m_PendingDestroy.end(), entity);
    if (pendingIt != m_PendingDestroy.end()) {
        return;
    }

    entity->setActive(false);
    SelectionSystem::removeEntity(entity);
    UUID uuid = entity->getUUID();

    // Call lifecycle
    entity->OnDestroy();
    
    // Remove from map
    m_EntityMap.erase(uuid);
    
    queueDestroyEntity(entity);
    if (m_IterationDepth == 0) {
        flushPendingDestroys();
    }
}

void Scene::destroyEntity(UUID uuid) {
    Entity* entity = findEntity(uuid);
    if (entity) {
        destroyEntity(entity);
    }
}

void Scene::destroyAllEntities() {
    for (auto& entity : m_Entities) {
        SelectionSystem::removeEntity(entity.get());
        entity->OnDestroy();
    }
    
    m_Entities.clear();
    m_EntityMap.clear();
}

Entity* Scene::findEntity(UUID uuid) const {
    auto it = m_EntityMap.find(uuid);
    if (it != m_EntityMap.end()) {
        return it->second;
    }
    return nullptr;
}

Entity* Scene::findEntityByName(const std::string& name) const {
    for (const auto& entity : m_Entities) {
        if (entity->getName() == name) {
            return entity.get();
        }
    }
    return nullptr;
}

std::vector<Entity*> Scene::findEntitiesWithTag(const std::string& tag) const {
    std::vector<Entity*> result;
    for (const auto& entity : m_Entities) {
        if (entity->getTag() == tag) {
            result.push_back(entity.get());
        }
    }
    return result;
}

std::vector<Entity*> Scene::getRootEntities() const {
    std::vector<Entity*> roots;
    for (const auto& entity : m_Entities) {
        if (entity->getTransform()->getParent() == nullptr) {
            roots.push_back(entity.get());
        }
    }
    return roots;
}

void Scene::setActive(bool active) {
    if (m_IsActive == active) return;
    
    m_IsActive = active;
    
    if (m_IsActive) {
        OnCreate();
    } else {
        OnDestroy();
    }
}

void Scene::OnCreate() {
    beginIteration();
    for (auto& entity : m_Entities) {
        if (entity->isActive()) {
            entity->onSceneActivated();
        }
    }
    endIteration();
}

void Scene::OnDestroy() {
    beginIteration();
    for (auto& entity : m_Entities) {
        if (entity->isActive()) {
            entity->onSceneDeactivated();
        }
    }
    endIteration();
}

void Scene::OnStart() {
    if (!m_IsActive) {
        return;
    }
    beginIteration();
    for (auto& entity : m_Entities) {
        if (entity->isActive() && !entity->isEditorOnly()) {
            entity->OnStart();
        }
    }
    endIteration();
}

void Scene::OnUpdate(float deltaTime) {
    if (!m_IsActive) return;
    
    beginIteration();
    for (auto& entity : m_Entities) {
        if (entity->isActive() && !entity->isEditorOnly()) {
            entity->OnUpdate(deltaTime);
        }
    }
    endIteration();
}

void Scene::OnFixedUpdate(float deltaTime) {
    if (!m_IsActive) {
        return;
    }
    beginIteration();
    for (auto& entity : m_Entities) {
        if (entity->isActive() && !entity->isEditorOnly()) {
            entity->OnFixedUpdate(deltaTime);
        }
    }
    endIteration();
}

void Scene::OnFixedPhysicsUpdate(float deltaTime) {
    if (!m_IsActive) {
        return;
    }
    if (m_PhysicsWorld) {
        m_PhysicsWorld->update(deltaTime, true);
    }
}

void Scene::OnEditorUpdate(float deltaTime) {
    if (!m_IsActive) {
        return;
    }
    if (m_PhysicsWorld && !SceneManager::getInstance().isPlaying()) {
        m_PhysicsWorld->update(deltaTime, false);
    }
    beginIteration();
    for (auto& entity : m_Entities) {
        if (entity->isActive() && entity->isEditorOnly()) {
            entity->OnEditorUpdate(deltaTime);
        }
    }
    endIteration();
}

void Scene::beginFrame() {
    if (!m_IsActive) {
        return;
    }
    if (m_IterationDepth == 0) {
        flushPendingDestroys();
    }
    beginIteration();
    for (auto& entity : m_Entities) {
        if (entity->isActive()) {
            entity->getTransform()->capturePreviousWorldMatrix();
        }
    }
    endIteration();
}

void Scene::queueDestroyEntity(Entity* entity) {
    if (!entity) {
        return;
    }
    if (std::find(m_PendingDestroy.begin(), m_PendingDestroy.end(), entity) == m_PendingDestroy.end()) {
        m_PendingDestroy.push_back(entity);
    }
}

void Scene::flushPendingDestroys() {
    if (m_PendingDestroy.empty()) {
        return;
    }
    for (Entity* entity : m_PendingDestroy) {
        auto it = std::find_if(m_Entities.begin(), m_Entities.end(),
            [entity](const std::unique_ptr<Entity>& e) {
                return e.get() == entity;
            });
        if (it != m_Entities.end()) {
            m_Entities.erase(it);
        }
    }
    m_PendingDestroy.clear();
}

void Scene::endIteration() {
    if (m_IterationDepth > 0) {
        --m_IterationDepth;
    }
    if (m_IterationDepth == 0) {
        flushPendingDestroys();
    }
}

void Scene::serialize(const std::string& filepath) {
    if (!SceneSerializer::SaveScene(this, filepath)) {
        std::cout << "Scene serialization failed: " << filepath << std::endl;
    }
}

void Scene::deserialize(const std::string& filepath) {
    if (!SceneSerializer::LoadScene(this, filepath)) {
        std::cout << "Scene deserialization failed: " << filepath << std::endl;
        return;
    }
    if (m_IsActive) {
        applySettings();
    }
}

void Scene::applySettings() {
    SceneQualitySettings quality = ResolveQualitySettings(m_Settings);
    Light::ShadowQuality shadowQuality = MapShadowQuality(quality.shadowQuality);
    uint32_t shadowResolution = static_cast<uint32_t>(quality.shadowResolution);
    uint8_t cascadeCount = CascadesForShadowQuality(quality.shadowQuality);
    
    for (auto& entity : m_Entities) {
        if (!entity) {
            continue;
        }
        Light* light = entity->getComponent<Light>();
        if (!light) {
            continue;
        }
        light->setShadowQuality(shadowQuality);
        light->setShadowMapResolution(shadowResolution);
        if (light->getType() == Light::Type::Directional) {
            light->setCascadeCount(cascadeCount);
        }
    }
    
    Renderer* renderer = Engine::getInstance().getRenderer();
    if (!renderer) {
        return;
    }
    const SceneEnvironmentSettings& env = m_Settings.environment;
    if (env.skyboxPath.empty() || env.skyboxPath == "Builtin Sky") {
        if (renderer->getEnvironmentPath() != "Builtin Sky") {
            renderer->resetEnvironment();
        }
    } else if (renderer->getEnvironmentPath() != env.skyboxPath) {
        if (!renderer->loadEnvironmentMap(env.skyboxPath)) {
            renderer->resetEnvironment();
        }
    }
    renderer->setEnvironmentExposure(env.exposureEV);
    renderer->setEnvironmentIblIntensity(env.iblIntensity);
    renderer->setEnvironmentSkyIntensity(env.skyIntensity);
    renderer->setEnvironmentTint(env.tint);
    renderer->setEnvironmentSaturation(env.saturation);
    renderer->setEnvironmentContrast(env.contrast);
    renderer->setEnvironmentBlur(env.blurLevel);
    renderer->setEnvironmentRotation(env.rotation);
    renderer->setEnvironmentSkyboxVisible(env.skyboxVisible);
    renderer->setEnvironmentSkyMode(env.skyMode);
    renderer->setEnvironmentAutoSunColor(env.autoSunColor);
    renderer->setColorGradingLUT(m_Settings.postProcess.colorGradingLUT);
    renderer->applyQualitySettings(quality);
}

} // namespace Crescent
