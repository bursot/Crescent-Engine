#pragma once

#include "../Core/UUID.hpp"
#include "../Math/Math.hpp"
#include "PhysicsTypes.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/Core/Reference.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <vector>

namespace Crescent {

class Scene;
class Entity;
class DebugRenderer;
class Rigidbody;
class PhysicsCollider;
enum class RigidbodyType;

class PhysicsWorld {
public:
    explicit PhysicsWorld(Scene* scene);
    ~PhysicsWorld();

    static constexpr uint32_t kAllLayersMask = 0xFFFFFFFFu;

    bool initialize();
    void shutdown();
    void update(float deltaTime, bool simulate);

    void setGravity(const Math::Vector3& gravity);
    Math::Vector3 getGravity() const { return m_Gravity; }
    float getFixedTimeStep() const { return m_FixedTimeStep; }

    void queueBodyRebuild(Entity* entity);
    void removeBody(Entity* entity);

    void setDebugDrawEnabled(bool enabled) { m_DebugDraw = enabled; }
    bool isDebugDrawEnabled() const { return m_DebugDraw; }
    void debugDraw(DebugRenderer* renderer);

    bool raycast(const Math::Vector3& origin,
                 const Math::Vector3& direction,
                 float maxDistance,
                 PhysicsRaycastHit& outHit,
                 uint32_t layerMask = kAllLayersMask,
                 bool includeTriggers = true,
                 const Entity* ignore = nullptr) const;
    int raycastAll(const Math::Vector3& origin,
                   const Math::Vector3& direction,
                   float maxDistance,
                   std::vector<PhysicsRaycastHit>& outHits,
                   uint32_t layerMask = kAllLayersMask,
                   bool includeTriggers = true,
                   const Entity* ignore = nullptr) const;
    bool sphereCast(const Math::Vector3& center,
                    float radius,
                    const Math::Vector3& direction,
                    float maxDistance,
                    PhysicsRaycastHit& outHit,
                    uint32_t layerMask = kAllLayersMask,
                    bool includeTriggers = true,
                    const Entity* ignore = nullptr) const;
    int sphereCastAll(const Math::Vector3& center,
                      float radius,
                      const Math::Vector3& direction,
                      float maxDistance,
                      std::vector<PhysicsRaycastHit>& outHits,
                      uint32_t layerMask = kAllLayersMask,
                      bool includeTriggers = true,
                      const Entity* ignore = nullptr) const;
    bool boxCast(const Math::Vector3& center,
                 const Math::Vector3& halfExtents,
                 const Math::Vector3& direction,
                 float maxDistance,
                 PhysicsRaycastHit& outHit,
                 uint32_t layerMask = kAllLayersMask,
                 bool includeTriggers = true,
                 const Entity* ignore = nullptr) const;
    int boxCastAll(const Math::Vector3& center,
                   const Math::Vector3& halfExtents,
                   const Math::Vector3& direction,
                   float maxDistance,
                   std::vector<PhysicsRaycastHit>& outHits,
                   uint32_t layerMask = kAllLayersMask,
                   bool includeTriggers = true,
                   const Entity* ignore = nullptr) const;
    bool capsuleCast(const Math::Vector3& center,
                     float radius,
                     float height,
                     const Math::Vector3& direction,
                     float maxDistance,
                     PhysicsRaycastHit& outHit,
                     uint32_t layerMask = kAllLayersMask,
                     bool includeTriggers = true,
                     const Entity* ignore = nullptr) const;
    int capsuleCastAll(const Math::Vector3& center,
                       float radius,
                       float height,
                       const Math::Vector3& direction,
                       float maxDistance,
                       std::vector<PhysicsRaycastHit>& outHits,
                       uint32_t layerMask = kAllLayersMask,
                       bool includeTriggers = true,
                       const Entity* ignore = nullptr) const;
    int overlapSphere(const Math::Vector3& center,
                      float radius,
                      std::vector<PhysicsOverlapHit>& outHits,
                      uint32_t layerMask = kAllLayersMask,
                      bool includeTriggers = true,
                      const Entity* ignore = nullptr) const;
    int overlapBox(const Math::Vector3& center,
                   const Math::Vector3& halfExtents,
                   std::vector<PhysicsOverlapHit>& outHits,
                   uint32_t layerMask = kAllLayersMask,
                   bool includeTriggers = true,
                   const Entity* ignore = nullptr) const;

private:
    struct BodyRecord;
    class PhysicsWorldImpl;

    void rebuildBody(Entity* entity);
    void syncKinematicBodies();
    void syncDynamicBodies();
    void syncEditorBodies();
    void flushPending();
    void dispatchContactEvents();
    Entity* resolveEntity(const JPH::BodyID& bodyID) const;

    JPH::RefConst<JPH::Shape> buildShape(const PhysicsCollider& collider,
                                         const Math::Vector3& scale,
                                         const Entity* entity,
                                         RigidbodyType bodyType) const;
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
