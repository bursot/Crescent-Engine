#include "Entity.hpp"
#include "Transform.hpp"
#include "../Physics/PhysicsTypes.hpp"
#include "../Scene/Scene.hpp"
#include "../Scene/SceneManager.hpp"
#include <algorithm>

namespace Crescent {

std::unordered_map<Scene*, std::unordered_map<std::string, Entity*>> Entity::s_NameRegistry;
std::unordered_map<Scene*, std::unordered_multimap<std::string, Entity*>> Entity::s_TagRegistry;

std::unordered_map<std::string, Entity*>& Entity::getNameRegistry(Scene* scene) {
    return s_NameRegistry[scene];
}

std::unordered_multimap<std::string, Entity*>& Entity::getTagRegistry(Scene* scene) {
    return s_TagRegistry[scene];
}

std::string Entity::makeUniqueName(const std::string& desired, const Entity* self, Scene* scene) {
    std::string baseName = desired.empty() ? "Entity" : desired;
    if (!scene) {
        return baseName;
    }
    auto& registry = getNameRegistry(scene);
    auto it = registry.find(baseName);
    if (it == registry.end() || it->second == self) {
        return baseName;
    }

    int suffix = 1;
    std::string candidate;
    do {
        candidate = baseName + " (" + std::to_string(suffix++) + ")";
        it = registry.find(candidate);
    } while (it != registry.end() && it->second != self);

    return candidate;
}

Entity::Entity(const std::string& name)
    : m_UUID()
    , m_Name(name.empty() ? "Entity" : name)
    , m_Tag("Untagged")
    , m_Layer(0)
    , m_IsActive(true)
    , m_Destroyed(false)
    , m_HasCreated(false)
    , m_EditorOnly(false)
    , m_Scene(nullptr)
    , m_Transform(nullptr) {
    
    // Every entity must have a Transform component
    m_Transform = addComponent<Transform>();
}

Entity::Entity(UUID uuid, const std::string& name)
    : m_UUID(uuid)
    , m_Name(name.empty() ? "Entity" : name)
    , m_Tag("Untagged")
    , m_Layer(0)
    , m_IsActive(true)
    , m_Destroyed(false)
    , m_HasCreated(false)
    , m_EditorOnly(false)
    , m_Scene(nullptr)
    , m_Transform(nullptr) {
    
    // Every entity must have a Transform component
    m_Transform = addComponent<Transform>();
}

Entity::~Entity() {
    if (!m_Destroyed) {
        OnDestroy();
    }

    // Unregister from registries
    if (m_Scene) {
        auto& nameRegistry = getNameRegistry(m_Scene);
        auto nameIt = nameRegistry.find(m_Name);
        if (nameIt != nameRegistry.end() && nameIt->second == this) {
            nameRegistry.erase(nameIt);
        }
        
        auto& tagRegistry = getTagRegistry(m_Scene);
        auto range = tagRegistry.equal_range(m_Tag);
        for (auto it = range.first; it != range.second; ++it) {
            if (it->second == this) {
                tagRegistry.erase(it);
                break;
            }
        }
    }
    
    // Destroy all components
    removeAllComponentsInternal(false);
}

void Entity::setScene(Scene* scene) {
    if (m_Scene == scene) {
        return;
    }
    
    if (m_Scene) {
        auto& oldNameRegistry = getNameRegistry(m_Scene);
        auto nameIt = oldNameRegistry.find(m_Name);
        if (nameIt != oldNameRegistry.end() && nameIt->second == this) {
            oldNameRegistry.erase(nameIt);
        }
        
        auto& oldTagRegistry = getTagRegistry(m_Scene);
        auto range = oldTagRegistry.equal_range(m_Tag);
        for (auto it = range.first; it != range.second; ++it) {
            if (it->second == this) {
                oldTagRegistry.erase(it);
                break;
            }
        }
    }
    
    m_Scene = scene;
    
    if (m_Scene) {
        m_Name = makeUniqueName(m_Name, this, m_Scene);
        auto& nameRegistry = getNameRegistry(m_Scene);
        nameRegistry[m_Name] = this;
        getTagRegistry(m_Scene).insert({m_Tag, this});
    }
}

void Entity::setName(const std::string& name) {
    std::string uniqueName = makeUniqueName(name, this, m_Scene);
    if (m_Name == uniqueName) {
        return;
    }
    if (m_Scene) {
        auto& nameRegistry = getNameRegistry(m_Scene);
        auto it = nameRegistry.find(m_Name);
        if (it != nameRegistry.end() && it->second == this) {
            nameRegistry.erase(it);
        }
    }
    m_Name = uniqueName;
    if (m_Scene) {
        auto& nameRegistry = getNameRegistry(m_Scene);
        nameRegistry[m_Name] = this;
    }
}

void Entity::setActive(bool active) {
    if (m_IsActive == active) {
        return;
    }
    
    m_IsActive = active;
    
    if (!m_Scene || !m_Scene->isActive()) {
        return;
    }
    
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
    }
}

bool Entity::isSceneActive() const {
    return m_Scene && m_Scene->isActive();
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
    
    if (m_Scene) {
        // Remove from old tag registry
        auto& tagRegistry = getTagRegistry(m_Scene);
        auto range = tagRegistry.equal_range(m_Tag);
        for (auto it = range.first; it != range.second; ++it) {
            if (it->second == this) {
                tagRegistry.erase(it);
                break;
            }
        }
    }
    
    // Update tag
    m_Tag = tag;
    
    // Add to new tag registry
    if (m_Scene) {
        getTagRegistry(m_Scene).insert({m_Tag, this});
    }
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
    removeAllComponentsInternal(true);
}

void Entity::removeAllComponentsInternal(bool callLifecycle) {
    // Call lifecycle for all components
    if (callLifecycle) {
        for (auto& component : m_Components) {
            if (component->isEnabled()) {
                component->OnDisable();
            }
            component->OnDestroy();
        }
    }
    
    m_Components.clear();
    m_ComponentMap.clear();
    m_Transform = nullptr;
}

void Entity::OnCreate() {
    if (m_HasCreated) {
        return;
    }
    m_HasCreated = true;
    for (auto& component : m_Components) {
        component->OnCreate();
    }
}

void Entity::OnStart() {
    if (!m_IsActive) {
        return;
    }
    for (auto& component : m_Components) {
        if (!component->isEnabled() || component->hasStarted()) {
            continue;
        }
        component->OnStart();
        component->markStarted(true);
    }
}

void Entity::OnDestroy() {
    if (m_Destroyed) {
        return;
    }
    m_Destroyed = true;
    if (m_IsActive && isSceneActive()) {
        for (auto& component : m_Components) {
            if (component->isEnabled()) {
                component->OnDisable();
            }
        }
    }
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

void Entity::OnFixedUpdate(float deltaTime) {
    if (!m_IsActive) {
        return;
    }
    for (auto& component : m_Components) {
        if (component->isEnabled()) {
            component->OnFixedUpdate(deltaTime);
        }
    }
}

void Entity::OnEditorUpdate(float deltaTime) {
    if (!m_IsActive) {
        return;
    }
    for (auto& component : m_Components) {
        if (component->isEnabled()) {
            component->OnEditorUpdate(deltaTime);
        }
    }
}

void Entity::OnCollisionEnter(const PhysicsContact& contact) {
    if (!m_IsActive) {
        return;
    }
    for (auto& component : m_Components) {
        if (component->isEnabled()) {
            component->OnCollisionEnter(contact);
        }
    }
}

void Entity::OnCollisionStay(const PhysicsContact& contact) {
    if (!m_IsActive) {
        return;
    }
    for (auto& component : m_Components) {
        if (component->isEnabled()) {
            component->OnCollisionStay(contact);
        }
    }
}

void Entity::OnCollisionExit(const PhysicsContact& contact) {
    if (!m_IsActive) {
        return;
    }
    for (auto& component : m_Components) {
        if (component->isEnabled()) {
            component->OnCollisionExit(contact);
        }
    }
}

void Entity::OnTriggerEnter(const PhysicsContact& contact) {
    if (!m_IsActive) {
        return;
    }
    for (auto& component : m_Components) {
        if (component->isEnabled()) {
            component->OnTriggerEnter(contact);
        }
    }
}

void Entity::OnTriggerStay(const PhysicsContact& contact) {
    if (!m_IsActive) {
        return;
    }
    for (auto& component : m_Components) {
        if (component->isEnabled()) {
            component->OnTriggerStay(contact);
        }
    }
}

void Entity::OnTriggerExit(const PhysicsContact& contact) {
    if (!m_IsActive) {
        return;
    }
    for (auto& component : m_Components) {
        if (component->isEnabled()) {
            component->OnTriggerExit(contact);
        }
    }
}

void Entity::onSceneActivated() {
    if (!m_IsActive) {
        return;
    }
    OnCreate();
    for (auto& component : m_Components) {
        if (component->isEnabled()) {
            component->OnEnable();
        }
    }
}

void Entity::onSceneDeactivated() {
    if (!m_IsActive) {
        return;
    }
    for (auto& component : m_Components) {
        if (component->isEnabled()) {
            component->OnDisable();
        }
    }
}

Entity* Entity::Find(const std::string& name) {
    Scene* scene = SceneManager::getInstance().getActiveScene();
    if (!scene) {
        return nullptr;
    }
    auto& nameRegistry = getNameRegistry(scene);
    auto it = nameRegistry.find(name);
    if (it != nameRegistry.end()) {
        return it->second;
    }
    return nullptr;
}

Entity* Entity::FindWithTag(const std::string& tag) {
    Scene* scene = SceneManager::getInstance().getActiveScene();
    if (!scene) {
        return nullptr;
    }
    auto& tagRegistry = getTagRegistry(scene);
    auto it = tagRegistry.find(tag);
    if (it != tagRegistry.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<Entity*> Entity::FindAllWithTag(const std::string& tag) {
    std::vector<Entity*> result;
    Scene* scene = SceneManager::getInstance().getActiveScene();
    if (!scene) {
        return result;
    }
    auto& tagRegistry = getTagRegistry(scene);
    auto range = tagRegistry.equal_range(tag);
    for (auto it = range.first; it != range.second; ++it) {
        result.push_back(it->second);
    }
    return result;
}

} // namespace Crescent
