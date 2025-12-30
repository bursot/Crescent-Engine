#pragma once

#include "../ECS/Component.hpp"
#include "../Math/Math.hpp"

namespace Crescent {

class PhysicsCollider : public Component {
public:
    enum class ShapeType {
        Box,
        Sphere,
        Capsule
    };

    PhysicsCollider();
    ~PhysicsCollider() override = default;

    COMPONENT_TYPE(PhysicsCollider)

    ShapeType getShapeType() const { return m_Shape; }
    void setShapeType(ShapeType type);

    const Math::Vector3& getSize() const { return m_Size; }
    void setSize(const Math::Vector3& size);

    float getRadius() const { return m_Radius; }
    void setRadius(float radius);

    float getHeight() const { return m_Height; }
    void setHeight(float height);

    const Math::Vector3& getCenter() const { return m_Center; }
    void setCenter(const Math::Vector3& center);

    bool isTrigger() const { return m_IsTrigger; }
    void setTrigger(bool trigger);

    float getFriction() const { return m_Friction; }
    void setFriction(float friction);

    float getRestitution() const { return m_Restitution; }
    void setRestitution(float restitution);

    void OnCreate() override;
    void OnDestroy() override;

private:
    void notifyChanged();

private:
    ShapeType m_Shape;
    Math::Vector3 m_Size;
    float m_Radius;
    float m_Height;
    Math::Vector3 m_Center;
    bool m_IsTrigger;
    float m_Friction;
    float m_Restitution;
};

} // namespace Crescent
