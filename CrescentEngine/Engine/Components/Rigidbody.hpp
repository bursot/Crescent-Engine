#pragma once

#include "../ECS/Component.hpp"
#include "../Math/Math.hpp"

namespace Crescent {

enum class RigidbodyType {
    Static,
    Dynamic,
    Kinematic
};

class Rigidbody : public Component {
public:
    Rigidbody();
    ~Rigidbody() override = default;

    COMPONENT_TYPE(Rigidbody)

    RigidbodyType getType() const { return m_Type; }
    void setType(RigidbodyType type);

    float getMass() const { return m_Mass; }
    void setMass(float mass);

    float getLinearDamping() const { return m_LinearDamping; }
    void setLinearDamping(float damping);

    float getAngularDamping() const { return m_AngularDamping; }
    void setAngularDamping(float damping);

    bool getUseGravity() const { return m_UseGravity; }
    void setUseGravity(bool enabled);

    bool getContinuousCollision() const { return m_Continuous; }
    void setContinuousCollision(bool enabled);

    bool getAllowSleep() const { return m_AllowSleep; }
    void setAllowSleep(bool enabled);

    void OnCreate() override;
    void OnDestroy() override;

private:
    void notifyChanged();

private:
    RigidbodyType m_Type;
    float m_Mass;
    float m_LinearDamping;
    float m_AngularDamping;
    bool m_UseGravity;
    bool m_Continuous;
    bool m_AllowSleep;
};

} // namespace Crescent
