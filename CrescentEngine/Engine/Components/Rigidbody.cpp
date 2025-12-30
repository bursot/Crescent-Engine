#include "Rigidbody.hpp"
#include "../Scene/Scene.hpp"
#include "../Physics/PhysicsWorld.hpp"
#include <algorithm>

namespace Crescent {

Rigidbody::Rigidbody()
    : m_Type(RigidbodyType::Dynamic)
    , m_Mass(1.0f)
    , m_LinearDamping(0.05f)
    , m_AngularDamping(0.05f)
    , m_UseGravity(true)
    , m_Continuous(false)
    , m_AllowSleep(true) {
}

void Rigidbody::setType(RigidbodyType type) {
    if (m_Type == type) {
        return;
    }
    m_Type = type;
    notifyChanged();
}

void Rigidbody::setMass(float mass) {
    float clamped = std::max(0.01f, mass);
    if (m_Mass == clamped) {
        return;
    }
    m_Mass = clamped;
    notifyChanged();
}

void Rigidbody::setLinearDamping(float damping) {
    float clamped = std::max(0.0f, damping);
    if (m_LinearDamping == clamped) {
        return;
    }
    m_LinearDamping = clamped;
    notifyChanged();
}

void Rigidbody::setAngularDamping(float damping) {
    float clamped = std::max(0.0f, damping);
    if (m_AngularDamping == clamped) {
        return;
    }
    m_AngularDamping = clamped;
    notifyChanged();
}

void Rigidbody::setUseGravity(bool enabled) {
    if (m_UseGravity == enabled) {
        return;
    }
    m_UseGravity = enabled;
    notifyChanged();
}

void Rigidbody::setContinuousCollision(bool enabled) {
    if (m_Continuous == enabled) {
        return;
    }
    m_Continuous = enabled;
    notifyChanged();
}

void Rigidbody::setAllowSleep(bool enabled) {
    if (m_AllowSleep == enabled) {
        return;
    }
    m_AllowSleep = enabled;
    notifyChanged();
}

void Rigidbody::OnCreate() {
    notifyChanged();
}

void Rigidbody::OnDestroy() {
    if (Entity* entity = getEntity()) {
        Scene* scene = entity->getScene();
        if (scene && scene->getPhysicsWorld()) {
            scene->getPhysicsWorld()->removeBody(entity);
        }
    }
}

void Rigidbody::notifyChanged() {
    Entity* entity = getEntity();
    if (!entity) {
        return;
    }
    Scene* scene = entity->getScene();
    if (!scene || !scene->getPhysicsWorld()) {
        return;
    }
    scene->getPhysicsWorld()->queueBodyRebuild(entity);
}

} // namespace Crescent
