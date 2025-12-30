#pragma once

#include "../Core/UUID.hpp"
#include "../ECS/Entity.hpp"
#include "SceneSettings.hpp"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace Crescent {

class PhysicsWorld;

// Scene - container for entities
class Scene {
public:
    Scene(const std::string& name = "Untitled Scene");
    ~Scene();
    
    // Scene properties
    const std::string& getName() const { return m_Name; }
    void setName(const std::string& name) { m_Name = name; }
    
    UUID getUUID() const { return m_UUID; }
    
    // Entity management
    Entity* createEntity(const std::string& name = "Entity");
    Entity* createEntityWithUUID(UUID uuid, const std::string& name = "Entity");
    
    void destroyEntity(Entity* entity);
    void destroyEntity(UUID uuid);
    void destroyAllEntities();
    
    Entity* findEntity(UUID uuid) const;
    Entity* findEntityByName(const std::string& name) const;
    std::vector<Entity*> findEntitiesWithTag(const std::string& tag) const;
    
    const std::vector<std::unique_ptr<Entity>>& getAllEntities() const { 
        return m_Entities; 
    }
    
    int getEntityCount() const { return static_cast<int>(m_Entities.size()); }
    
    // Scene lifecycle
    void OnCreate();
    void OnDestroy();
    void OnUpdate(float deltaTime);
    
    // Scene root entities (entities without parent)
    std::vector<Entity*> getRootEntities() const;
    
    // Active state
    bool isActive() const { return m_IsActive; }
    void setActive(bool active);
    
    // Serialization
    void serialize(const std::string& filepath);
    void deserialize(const std::string& filepath);

    // Settings
    SceneSettings& getSettings() { return m_Settings; }
    const SceneSettings& getSettings() const { return m_Settings; }
    void setSettings(const SceneSettings& settings) { m_Settings = settings; }
    void applySettings();

    // Physics
    class PhysicsWorld* getPhysicsWorld() const { return m_PhysicsWorld.get(); }
    
private:
    UUID m_UUID;
    std::string m_Name;
    bool m_IsActive;
    SceneSettings m_Settings;
    std::unique_ptr<class PhysicsWorld> m_PhysicsWorld;
    
    std::vector<std::unique_ptr<Entity>> m_Entities;
    std::unordered_map<UUID, Entity*> m_EntityMap;
};

} // namespace Crescent
