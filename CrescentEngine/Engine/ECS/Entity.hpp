#pragma once

#include "../Core/UUID.hpp"
#include "Component.hpp"
#include "Transform.hpp"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <typeindex>

namespace Crescent {

// Forward declaration
class Scene;

// Entity (GameObject) - container for components
class Entity {
public:
    Entity(const std::string& name = "Entity");
    Entity(UUID uuid, const std::string& name = "Entity");
    ~Entity();
    
    // Unique identifier
    UUID getUUID() const { return m_UUID; }
    
    // Name
    const std::string& getName() const { return m_Name; }
    void setName(const std::string& name) { m_Name = name; }
    
    // Tag and Layer
    const std::string& getTag() const { return m_Tag; }
    void setTag(const std::string& tag);  // Declaration only - implemented in .cpp
    
    int getLayer() const { return m_Layer; }
    void setLayer(int layer) { m_Layer = layer; }
    
    // Active state
    bool isActive() const { return m_IsActive; }
    void setActive(bool active);
    bool isActiveSelf() const { return m_IsActive; }
    bool isActiveInHierarchy() const;
    
    // Scene reference
    Scene* getScene() const { return m_Scene; }
    void setScene(Scene* scene) { m_Scene = scene; }
    
    // Transform (every entity has a transform)
    Transform* getTransform() const { return m_Transform; }
    
    // Component management
    template<typename T, typename... Args>
    T* addComponent(Args&&... args);
    
    template<typename T>
    T* getComponent() const;
    
    template<typename T>
    bool hasComponent() const;
    
    template<typename T>
    void removeComponent();
    
    void removeComponent(Component* component);
    void removeAllComponents();
    
    const std::vector<std::unique_ptr<Component>>& getAllComponents() const { 
        return m_Components; 
    }
    
    // Lifecycle
    void OnCreate();
    void OnDestroy();
    void OnUpdate(float deltaTime);
    
    // Static methods
    static Entity* Find(const std::string& name);
    static Entity* FindWithTag(const std::string& tag);
    static std::vector<Entity*> FindAllWithTag(const std::string& tag);
    
private:
    void addComponentInternal(std::unique_ptr<Component> component);
    
private:
    UUID m_UUID;
    std::string m_Name;
    std::string m_Tag;
    int m_Layer;
    bool m_IsActive;
    
    Scene* m_Scene;
    Transform* m_Transform; // Cached for fast access
    
    std::vector<std::unique_ptr<Component>> m_Components;
    std::unordered_map<std::type_index, Component*> m_ComponentMap;
    
    static std::unordered_map<std::string, Entity*> s_NameRegistry;
    static std::unordered_multimap<std::string, Entity*> s_TagRegistry;
};
    
// Template implementations

template<typename T, typename... Args>
T* Entity::addComponent(Args&&... args) {
    static_assert(std::is_base_of<Component, T>::value, "T must inherit from Component");
    
    // Check if component already exists
    std::type_index typeIndex = T::StaticTypeIndex();
    if (m_ComponentMap.find(typeIndex) != m_ComponentMap.end()) {
        return static_cast<T*>(m_ComponentMap[typeIndex]);
    }
    
    // Create new component
    auto component = std::make_unique<T>(std::forward<Args>(args)...);
    T* componentPtr = component.get();
    
    component->setEntity(this);
    
    // Add to map for fast lookup
    m_ComponentMap[typeIndex] = componentPtr;
    
    // Add to vector
    m_Components.push_back(std::move(component));
    
    // Call lifecycle
    if (m_IsActive) {
        componentPtr->OnCreate();
        if (componentPtr->isEnabled()) {
            componentPtr->OnEnable();
        }
    }
    
    return componentPtr;
}

template<typename T>
T* Entity::getComponent() const {
    static_assert(std::is_base_of<Component, T>::value, "T must inherit from Component");
    
    std::type_index typeIndex = T::StaticTypeIndex();
    auto it = m_ComponentMap.find(typeIndex);
    if (it != m_ComponentMap.end()) {
        return static_cast<T*>(it->second);
    }
    return nullptr;
}

template<typename T>
bool Entity::hasComponent() const {
    static_assert(std::is_base_of<Component, T>::value, "T must inherit from Component");
    
    std::type_index typeIndex = T::StaticTypeIndex();
    return m_ComponentMap.find(typeIndex) != m_ComponentMap.end();
}

template<typename T>
void Entity::removeComponent() {
    static_assert(std::is_base_of<Component, T>::value, "T must inherit from Component");
    
    std::type_index typeIndex = T::StaticTypeIndex();
    auto mapIt = m_ComponentMap.find(typeIndex);
    if (mapIt == m_ComponentMap.end()) {
        return;
    }
    
    Component* component = mapIt->second;
    
    // Call lifecycle
    if (component->isEnabled()) {
        component->OnDisable();
    }
    component->OnDestroy();
    
    // Remove from map
    m_ComponentMap.erase(mapIt);
    
    // Remove from vector
    auto it = std::find_if(m_Components.begin(), m_Components.end(),
        [component](const std::unique_ptr<Component>& c) {
            return c.get() == component;
        });
    
    if (it != m_Components.end()) {
        m_Components.erase(it);
    }
}

} // namespace Crescent
