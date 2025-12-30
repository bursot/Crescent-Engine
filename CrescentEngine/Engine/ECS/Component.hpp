#pragma once

#include <string>
#include <typeindex>
#include <memory>

namespace Crescent {

// Forward declaration
class Entity;

// Base component class - all components inherit from this
class Component {
public:
    Component() = default;
    virtual ~Component() = default;
    
    // Lifecycle callbacks
    virtual void OnCreate() {}
    virtual void OnDestroy() {}
    virtual void OnEnable() {}
    virtual void OnDisable() {}
    virtual void OnUpdate(float deltaTime) {}
    
    // Get component type name (for reflection)
    virtual std::string getTypeName() const = 0;
    virtual std::type_index getTypeIndex() const = 0;
    
    // Entity reference
    Entity* getEntity() const { return m_Entity; }
    void setEntity(Entity* entity) { m_Entity = entity; }
    
    // Enable/Disable
    bool isEnabled() const { return m_Enabled; }
    void setEnabled(bool enabled);
    
    // Component serialization interface
    virtual void serialize(class Serializer& serializer) {}
    virtual void deserialize(class Deserializer& deserializer) {}
    
protected:
    Entity* m_Entity = nullptr;
    bool m_Enabled = true;
};

// Macro to help implement component type info
#define COMPONENT_TYPE(Type) \
    std::string getTypeName() const override { return #Type; } \
    std::type_index getTypeIndex() const override { return std::type_index(typeid(Type)); } \
    static std::type_index StaticTypeIndex() { return std::type_index(typeid(Type)); }

} // namespace Crescent
