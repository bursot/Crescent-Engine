#include "Entity.hpp"
#include "Transform.hpp"
#include <algorithm>

namespace Crescent {

std::unordered_map<std::string, Entity*> Entity::s_NameRegistry;
std::unordered_multimap<std::string, Entity*> Entity::s_TagRegistry;

Entity::Entity(const std::string& name)
    : m_UUID()
    , m_Name(name)
    , m_Tag("Untagged")
    , m_Layer(0)
    , m_IsActive(true)
    , m_Scene(nullptr)
    , m_Transform(nullptr) {
    
    // Every entity must have a Transform component
    m_Transform = addComponent<Transform>();
    
    // Register in name registry
    s_NameRegistry[m_Name] = this;
}

Entity::Entity(UUID uuid, const std::string& name)
    : m_UUID(uuid)
    , m_Name(name)
    , m_Tag("Untagged")
    , m_Layer(0)
    , m_IsActive(true)
    , m_Scene(nullptr)
    , m_Transform(nullptr) {
    
    // Every entity must have a Transform component
    m_Transform = addComponent<Transform>();
    
    // Register in name registry
    s_NameRegistry[m_Name] = this;
}

Entity::~Entity() {
    // Unregister from registries
    s_NameRegistry.erase(m_Name);
    
    auto range = s_TagRegistry.equal_range(m_Tag);
    for (auto it = range.first; it != range.second; ++it) {
        if (it->second == this) {
            s_TagRegistry.erase(it);
            break;
        }
    }
    
    // Destroy all components
    removeAllComponents();
}

void Entity::setActive(bool active) {
    if (m_IsActive == active) return;
    
    m_IsActive = active;
    
    if (m_IsActive) {
        OnCreate();
        for (auto& component : m_Components) {
            if (component->isEnabled()) {
                component->OnEnable();
            }
        }
    } else {
        for (auto& component : m_Components) {
            if (component->isEnabled()) {
                component->OnDisable();
            }
        }
        OnDestroy();
    }
}

bool Entity::isActiveInHierarchy() const {
    if (!m_IsActive) return false;
    
    Transform* parent = m_Transform->getParent();
    while (parent) {
        if (!parent->getEntity()->isActive()) {
            return false;
        }
        parent = parent->getParent();
    }
    
    return true;
}

void Entity::setTag(const std::string& tag) {
    if (m_Tag == tag) return;
    
    // Remove from old tag registry
    auto range = s_TagRegistry.equal_range(m_Tag);
    for (auto it = range.first; it != range.second; ++it) {
        if (it->second == this) {
            s_TagRegistry.erase(it);
            break;
        }
    }
    
    // Update tag
    m_Tag = tag;
    
    // Add to new tag registry
    s_TagRegistry.insert({m_Tag, this});
}

void Entity::removeComponent(Component* component) {
    if (!component) return;
    
    // Call lifecycle
    if (component->isEnabled()) {
        component->OnDisable();
    }
    component->OnDestroy();
    
    // Remove from map
    m_ComponentMap.erase(component->getTypeIndex());
    
    // Remove from vector
    auto it = std::find_if(m_Components.begin(), m_Components.end(),
        [component](const std::unique_ptr<Component>& c) {
            return c.get() == component;
        });
    
    if (it != m_Components.end()) {
        m_Components.erase(it);
    }
}

void Entity::removeAllComponents() {
    // Call lifecycle for all components
    for (auto& component : m_Components) {
        if (component->isEnabled()) {
            component->OnDisable();
        }
        component->OnDestroy();
    }
    
    m_Components.clear();
    m_ComponentMap.clear();
    m_Transform = nullptr;
}

void Entity::OnCreate() {
    for (auto& component : m_Components) {
        component->OnCreate();
    }
}

void Entity::OnDestroy() {
    for (auto& component : m_Components) {
        component->OnDestroy();
    }
}

void Entity::OnUpdate(float deltaTime) {
    if (!m_IsActive) return;
    
    for (auto& component : m_Components) {
        if (component->isEnabled()) {
            component->OnUpdate(deltaTime);
        }
    }
}

Entity* Entity::Find(const std::string& name) {
    auto it = s_NameRegistry.find(name);
    if (it != s_NameRegistry.end()) {
        return it->second;
    }
    return nullptr;
}

Entity* Entity::FindWithTag(const std::string& tag) {
    auto it = s_TagRegistry.find(tag);
    if (it != s_TagRegistry.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<Entity*> Entity::FindAllWithTag(const std::string& tag) {
    std::vector<Entity*> result;
    auto range = s_TagRegistry.equal_range(tag);
    for (auto it = range.first; it != range.second; ++it) {
        result.push_back(it->second);
    }
    return result;
}

} // namespace Crescent
