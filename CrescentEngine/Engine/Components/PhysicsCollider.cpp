#include "PhysicsCollider.hpp"
#include "../Scene/Scene.hpp"
#include "../Physics/PhysicsWorld.hpp"
#include "MeshRenderer.hpp"
#include "SkinnedMeshRenderer.hpp"
#include <algorithm>

namespace Crescent {

PhysicsCollider::PhysicsCollider()
    : m_Shape(ShapeType::Box)
    , m_Size(1.0f, 1.0f, 1.0f)
    , m_Radius(0.5f)
    , m_Height(1.0f)
    , m_Center(0.0f, 0.0f, 0.0f)
    , m_IsTrigger(false)
    , m_Friction(0.5f)
    , m_Restitution(0.0f)
    , m_FrictionCombine(CombineMode::Average)
    , m_RestitutionCombine(CombineMode::Average)
    , m_CollisionLayer(0)
    , m_CollisionMask(kAllLayersMask) {
}

void PhysicsCollider::setShapeType(ShapeType type) {
    if (m_Shape == type) {
        return;
    }
    m_Shape = type;
    notifyChanged();
}

void PhysicsCollider::setSize(const Math::Vector3& size) {
    Math::Vector3 clamped(
        std::max(0.001f, size.x),
        std::max(0.001f, size.y),
        std::max(0.001f, size.z)
    );
    if (m_Size == clamped) {
        return;
    }
    m_Size = clamped;
    notifyChanged();
}

void PhysicsCollider::setRadius(float radius) {
    float clamped = std::max(0.001f, radius);
    if (m_Radius == clamped) {
        return;
    }
    m_Radius = clamped;
    notifyChanged();
}

void PhysicsCollider::setHeight(float height) {
    float clamped = std::max(0.001f, height);
    if (m_Height == clamped) {
        return;
    }
    m_Height = clamped;
    notifyChanged();
}

void PhysicsCollider::setCenter(const Math::Vector3& center) {
    if (m_Center == center) {
        return;
    }
    m_Center = center;
    notifyChanged();
}

void PhysicsCollider::setTrigger(bool trigger) {
    if (m_IsTrigger == trigger) {
        return;
    }
    m_IsTrigger = trigger;
    notifyChanged();
}

void PhysicsCollider::setFriction(float friction) {
    float clamped = std::max(0.0f, friction);
    if (m_Friction == clamped) {
        return;
    }
    m_Friction = clamped;
    notifyChanged();
}

void PhysicsCollider::setRestitution(float restitution) {
    float clamped = std::max(0.0f, restitution);
    if (m_Restitution == clamped) {
        return;
    }
    m_Restitution = clamped;
    notifyChanged();
}

void PhysicsCollider::setFrictionCombine(CombineMode mode) {
    if (m_FrictionCombine == mode) {
        return;
    }
    m_FrictionCombine = mode;
    notifyChanged();
}

void PhysicsCollider::setRestitutionCombine(CombineMode mode) {
    if (m_RestitutionCombine == mode) {
        return;
    }
    m_RestitutionCombine = mode;
    notifyChanged();
}

void PhysicsCollider::setCollisionLayer(uint32_t layer) {
    uint32_t clamped = std::min(layer, kMaxLayers - 1);
    if (m_CollisionLayer == clamped) {
        return;
    }
    m_CollisionLayer = clamped;
    notifyChanged();
}

void PhysicsCollider::setCollisionMask(uint32_t mask) {
    if (m_CollisionMask == mask) {
        return;
    }
    m_CollisionMask = mask;
    notifyChanged();
}

void PhysicsCollider::OnCreate() {
    if (Entity* entity = getEntity()) {
        std::shared_ptr<Mesh> mesh;
        if (auto* renderer = entity->getComponent<MeshRenderer>()) {
            mesh = renderer->getMesh();
        } else if (auto* skinned = entity->getComponent<SkinnedMeshRenderer>()) {
            mesh = skinned->getMesh();
        }
        if (mesh) {
            Math::Vector3 bounds = mesh->getBoundsSize();
            if (m_Shape == ShapeType::Box && m_Size == Math::Vector3(1.0f, 1.0f, 1.0f)) {
                m_Size = bounds;
            } else if (m_Shape == ShapeType::Sphere && m_Radius == 0.5f) {
                m_Radius = 0.5f * std::max({bounds.x, bounds.y, bounds.z});
            } else if (m_Shape == ShapeType::Capsule && m_Height == 1.0f && m_Radius == 0.5f) {
                m_Height = bounds.y;
                m_Radius = 0.5f * std::max(bounds.x, bounds.z);
            }
        }
    }
    notifyChanged();
}

void PhysicsCollider::OnDestroy() {
    if (Entity* entity = getEntity()) {
        Scene* scene = entity->getScene();
        if (scene && scene->getPhysicsWorld()) {
            scene->getPhysicsWorld()->removeBody(entity);
        }
    }
}

void PhysicsCollider::notifyChanged() {
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
