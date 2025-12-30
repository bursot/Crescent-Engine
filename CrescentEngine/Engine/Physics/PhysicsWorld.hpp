#pragma once

#include "../Core/UUID.hpp"
#include "../Math/Math.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/Core/Reference.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <unordered_map>
#include <unordered_set>
#include <memory>

namespace Crescent {

class Scene;
class Entity;
class DebugRenderer;
class Rigidbody;
class PhysicsCollider;

class PhysicsWorld {
public:
    explicit PhysicsWorld(Scene* scene);
    ~PhysicsWorld();

    bool initialize();
    void shutdown();
    void update(float deltaTime, bool simulate);

    void setGravity(const Math::Vector3& gravity);
    Math::Vector3 getGravity() const { return m_Gravity; }

    void queueBodyRebuild(Entity* entity);
    void removeBody(Entity* entity);

    void setDebugDrawEnabled(bool enabled) { m_DebugDraw = enabled; }
    bool isDebugDrawEnabled() const { return m_DebugDraw; }
    void debugDraw(DebugRenderer* renderer);

private:
    struct BodyRecord;
    class PhysicsWorldImpl;

    void rebuildBody(Entity* entity);
    void syncKinematicBodies();
    void syncDynamicBodies();
    void syncEditorBodies();
    void flushPending();

    JPH::RefConst<JPH::Shape> buildShape(const PhysicsCollider& collider,
                                         const Math::Vector3& scale) const;
    void updateBodyTransform(BodyRecord& record, Entity* entity);

private:
    Scene* m_Scene;
    std::unordered_map<UUID, BodyRecord> m_Bodies;
    std::unordered_set<UUID> m_Pending;
    Math::Vector3 m_Gravity;
    float m_FixedTimeStep;
    float m_TimeAccumulator;
    bool m_DebugDraw;
    bool m_Initialized;
    std::unique_ptr<PhysicsWorldImpl> m_Impl;
};

} // namespace Crescent
