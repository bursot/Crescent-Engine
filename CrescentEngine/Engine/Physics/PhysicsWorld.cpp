#include "PhysicsWorld.hpp"
#include "../Scene/Scene.hpp"
#include "../ECS/Entity.hpp"
#include "../ECS/Transform.hpp"
#include "../Renderer/DebugRenderer.hpp"
#include "../Components/Rigidbody.hpp"
#include "../Components/PhysicsCollider.hpp"

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/CollisionGroup.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Core/HashCombine.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <thread>

namespace Crescent {
namespace {

static bool g_JoltInitialized = false;
static int g_JoltRefCount = 0;

namespace Layers {
    static constexpr JPH::ObjectLayer NonMoving = 0;
    static constexpr JPH::ObjectLayer Moving = 1;
    static constexpr uint32_t NumLayers = 2;
}

class BroadPhaseLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    BroadPhaseLayerInterfaceImpl() {
        m_Layers[Layers::NonMoving] = JPH::BroadPhaseLayer(0);
        m_Layers[Layers::Moving] = JPH::BroadPhaseLayer(1);
    }

    uint GetNumBroadPhaseLayers() const override {
        return Layers::NumLayers;
    }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
        return m_Layers[inLayer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
        switch ((JPH::BroadPhaseLayer::Type)inLayer) {
        case 0: return "NON_MOVING";
        case 1: return "MOVING";
        default: return "UNKNOWN";
        }
    }
#endif

private:
    JPH::BroadPhaseLayer m_Layers[Layers::NumLayers];
};

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override {
        if (inObject1 == Layers::NonMoving) {
            return inObject2 == Layers::Moving;
        }
        return true;
    }
};

class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override {
        if (inLayer1 == Layers::NonMoving) {
            return inLayer2 == JPH::BroadPhaseLayer(1);
        }
        return true;
    }
};

JPH::Vec3 ToJolt(const Math::Vector3& v) {
    return JPH::Vec3(v.x, v.y, v.z);
}

JPH::Quat ToJolt(const Math::Quaternion& q) {
    return JPH::Quat(q.x, q.y, q.z, q.w);
}

Math::Vector3 ToCrescent(const JPH::Vec3& v) {
    return Math::Vector3(v.GetX(), v.GetY(), v.GetZ());
}

Math::Quaternion ToCrescent(const JPH::Quat& q) {
    return Math::Quaternion(q.GetX(), q.GetY(), q.GetZ(), q.GetW());
}

struct BodyPairKey {
    JPH::BodyID body1;
    JPH::BodyID body2;

    bool operator==(const BodyPairKey& other) const {
        return body1 == other.body1 && body2 == other.body2;
    }
};

struct BodyPairKeyHash {
    size_t operator()(const BodyPairKey& key) const {
        return static_cast<size_t>(JPH::HashCombineArgs(key.body1.GetIndexAndSequenceNumber(),
                                                        key.body2.GetIndexAndSequenceNumber()));
    }
};

inline BodyPairKey MakeBodyPairKey(const JPH::BodyID& a, const JPH::BodyID& b) {
    if (a < b) {
        return {a, b};
    }
    return {b, a};
}

inline PhysicsCollider::CombineMode ResolveCombineMode(PhysicsCollider::CombineMode a,
                                                       PhysicsCollider::CombineMode b) {
    return (static_cast<int>(a) >= static_cast<int>(b)) ? a : b;
}

inline float CombineValues(float a, float b, PhysicsCollider::CombineMode mode) {
    switch (mode) {
    case PhysicsCollider::CombineMode::Min:
        return std::min(a, b);
    case PhysicsCollider::CombineMode::Multiply:
        return a * b;
    case PhysicsCollider::CombineMode::Max:
        return std::max(a, b);
    case PhysicsCollider::CombineMode::Average:
    default:
        return 0.5f * (a + b);
    }
}

inline const PhysicsCollider* GetColliderFromBody(const JPH::Body& body) {
    auto* entity = reinterpret_cast<Entity*>(body.GetUserData());
    return entity ? entity->getComponent<PhysicsCollider>() : nullptr;
}

float CombineFriction(const JPH::Body& body1,
                      const JPH::SubShapeID&,
                      const JPH::Body& body2,
                      const JPH::SubShapeID&) {
    const PhysicsCollider* collider1 = GetColliderFromBody(body1);
    const PhysicsCollider* collider2 = GetColliderFromBody(body2);
    float friction1 = collider1 ? collider1->getFriction() : body1.GetFriction();
    float friction2 = collider2 ? collider2->getFriction() : body2.GetFriction();
    PhysicsCollider::CombineMode mode1 = collider1 ? collider1->getFrictionCombine()
                                                   : PhysicsCollider::CombineMode::Average;
    PhysicsCollider::CombineMode mode2 = collider2 ? collider2->getFrictionCombine()
                                                   : PhysicsCollider::CombineMode::Average;
    PhysicsCollider::CombineMode mode = ResolveCombineMode(mode1, mode2);
    return CombineValues(friction1, friction2, mode);
}

float CombineRestitution(const JPH::Body& body1,
                         const JPH::SubShapeID&,
                         const JPH::Body& body2,
                         const JPH::SubShapeID&) {
    const PhysicsCollider* collider1 = GetColliderFromBody(body1);
    const PhysicsCollider* collider2 = GetColliderFromBody(body2);
    float restitution1 = collider1 ? collider1->getRestitution() : body1.GetRestitution();
    float restitution2 = collider2 ? collider2->getRestitution() : body2.GetRestitution();
    PhysicsCollider::CombineMode mode1 = collider1 ? collider1->getRestitutionCombine()
                                                   : PhysicsCollider::CombineMode::Average;
    PhysicsCollider::CombineMode mode2 = collider2 ? collider2->getRestitutionCombine()
                                                   : PhysicsCollider::CombineMode::Average;
    PhysicsCollider::CombineMode mode = ResolveCombineMode(mode1, mode2);
    return CombineValues(restitution1, restitution2, mode);
}

enum class ContactEventType {
    Enter,
    Stay,
    Exit
};

struct ContactEvent {
    JPH::BodyID body1;
    JPH::BodyID body2;
    Math::Vector3 point1;
    Math::Vector3 point2;
    Math::Vector3 normal;
    float penetration;
    bool isTrigger;
    ContactEventType type;
};

} // namespace

struct PhysicsWorld::BodyRecord {
    JPH::BodyID id;
    RigidbodyType type;
    Math::Vector3 lastPosition;
    Math::Quaternion lastRotation;
    Math::Vector3 lastScale;
    bool isSensor;
};

class PhysicsWorld::PhysicsWorldImpl {
public:
    class ContactListenerImpl final : public JPH::ContactListener {
    public:
        explicit ContactListenerImpl(PhysicsWorldImpl& impl)
            : m_Impl(impl) {}

        JPH::ValidateResult OnContactValidate(const JPH::Body& inBody1,
                                              const JPH::Body& inBody2,
                                              JPH::RVec3Arg,
                                              const JPH::CollideShapeResult&) override {
            const PhysicsCollider* collider1 = GetColliderFromBody(inBody1);
            const PhysicsCollider* collider2 = GetColliderFromBody(inBody2);
            if (!collider1 || !collider2) {
                return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
            }

            uint32_t layer1 = collider1->getCollisionLayer();
            uint32_t layer2 = collider2->getCollisionLayer();
            uint32_t mask1 = collider1->getCollisionMask();
            uint32_t mask2 = collider2->getCollisionMask();

            if (layer1 >= PhysicsCollider::kMaxLayers || layer2 >= PhysicsCollider::kMaxLayers) {
                return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
            }

            uint32_t bit1 = 1u << layer2;
            uint32_t bit2 = 1u << layer1;
            bool collide = (mask1 & bit1) != 0 && (mask2 & bit2) != 0;
            return collide ? JPH::ValidateResult::AcceptAllContactsForThisBodyPair
                           : JPH::ValidateResult::RejectAllContactsForThisBodyPair;
        }

        void OnContactAdded(const JPH::Body& inBody1,
                            const JPH::Body& inBody2,
                            const JPH::ContactManifold& inManifold,
                            JPH::ContactSettings&) override {
            recordEvent(inBody1, inBody2, inManifold, ContactEventType::Enter);
        }

        void OnContactPersisted(const JPH::Body& inBody1,
                                const JPH::Body& inBody2,
                                const JPH::ContactManifold& inManifold,
                                JPH::ContactSettings&) override {
            recordEvent(inBody1, inBody2, inManifold, ContactEventType::Stay);
        }

        void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override {
            BodyPairKey key = MakeBodyPairKey(inSubShapePair.GetBody1ID(), inSubShapePair.GetBody2ID());
            bool isTrigger = false;
            {
                std::lock_guard<std::mutex> lock(m_Impl.eventMutex);
                auto it = m_Impl.activeContacts.find(key);
                if (it != m_Impl.activeContacts.end()) {
                    isTrigger = it->second;
                    m_Impl.activeContacts.erase(it);
                }
            }

            ContactEvent event{};
            event.body1 = key.body1;
            event.body2 = key.body2;
            event.isTrigger = isTrigger;
            event.type = ContactEventType::Exit;
            {
                std::lock_guard<std::mutex> lock(m_Impl.eventMutex);
                m_Impl.pendingEvents.push_back(event);
            }
        }

    private:
        void recordEvent(const JPH::Body& body1,
                         const JPH::Body& body2,
                         const JPH::ContactManifold& manifold,
                         ContactEventType type) {
            ContactEvent event{};
            event.body1 = body1.GetID();
            event.body2 = body2.GetID();
            event.isTrigger = body1.IsSensor() || body2.IsSensor();
            event.normal = ToCrescent(manifold.mWorldSpaceNormal);
            event.penetration = manifold.mPenetrationDepth;

            if (!manifold.mRelativeContactPointsOn1.empty()) {
                JPH::RVec3 p1 = manifold.GetWorldSpaceContactPointOn1(0);
                JPH::RVec3 p2 = manifold.GetWorldSpaceContactPointOn2(0);
                event.point1 = Math::Vector3(static_cast<float>(p1.GetX()),
                                             static_cast<float>(p1.GetY()),
                                             static_cast<float>(p1.GetZ()));
                event.point2 = Math::Vector3(static_cast<float>(p2.GetX()),
                                             static_cast<float>(p2.GetY()),
                                             static_cast<float>(p2.GetZ()));
            }

            event.type = type;
            BodyPairKey key = MakeBodyPairKey(event.body1, event.body2);
            {
                std::lock_guard<std::mutex> lock(m_Impl.eventMutex);
                m_Impl.activeContacts[key] = event.isTrigger;
                m_Impl.pendingEvents.push_back(event);
            }
        }

        PhysicsWorldImpl& m_Impl;
    };

    BroadPhaseLayerInterfaceImpl broadphaseLayerInterface;
    ObjectVsBroadPhaseLayerFilterImpl objectVsBroadphaseLayerFilter;
    ObjectLayerPairFilterImpl objectLayerPairFilter;
    JPH::PhysicsSystem physicsSystem;
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
    std::unique_ptr<ContactListenerImpl> contactListener;
    std::mutex eventMutex;
    std::vector<ContactEvent> pendingEvents;
    std::unordered_map<BodyPairKey, bool, BodyPairKeyHash> activeContacts;
};

PhysicsWorld::PhysicsWorld(Scene* scene)
    : m_Scene(scene)
    , m_Gravity(0.0f, -9.81f, 0.0f)
    , m_FixedTimeStep(1.0f / 60.0f)
    , m_TimeAccumulator(0.0f)
    , m_DebugDraw(false)
    , m_Initialized(false) {
}

PhysicsWorld::~PhysicsWorld() {
    shutdown();
}

bool PhysicsWorld::initialize() {
    if (m_Initialized) {
        return true;
    }

    if (!g_JoltInitialized) {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
        g_JoltInitialized = true;
    }
    g_JoltRefCount++;

    m_Impl = std::make_unique<PhysicsWorldImpl>();
    m_Impl->tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);
    uint32_t threadCount = std::max(1u, std::thread::hardware_concurrency());
    uint32_t workerCount = (threadCount > 1) ? (threadCount - 1) : 1;
    m_Impl->jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs,
        JPH::cMaxPhysicsBarriers,
        workerCount
    );

    const uint32_t maxBodies = 65536;
    const uint32_t numBodyMutexes = 0;
    const uint32_t maxBodyPairs = 65536;
    const uint32_t maxContactConstraints = 10240;
    m_Impl->physicsSystem.Init(maxBodies,
                               numBodyMutexes,
                               maxBodyPairs,
                               maxContactConstraints,
                               m_Impl->broadphaseLayerInterface,
                               m_Impl->objectVsBroadphaseLayerFilter,
                               m_Impl->objectLayerPairFilter);

    m_Impl->contactListener = std::make_unique<PhysicsWorldImpl::ContactListenerImpl>(*m_Impl);
    m_Impl->physicsSystem.SetContactListener(m_Impl->contactListener.get());
    m_Impl->physicsSystem.SetCombineFriction(&CombineFriction);
    m_Impl->physicsSystem.SetCombineRestitution(&CombineRestitution);
    m_Impl->physicsSystem.SetGravity(ToJolt(m_Gravity));

    m_Initialized = true;
    m_DebugDraw = false;
    m_Pending.clear();
    m_Bodies.clear();
    {
        std::lock_guard<std::mutex> lock(m_Impl->eventMutex);
        m_Impl->pendingEvents.clear();
        m_Impl->activeContacts.clear();
    }
    m_TimeAccumulator = 0.0f;

    return true;
}

void PhysicsWorld::shutdown() {
    if (!m_Initialized) {
        return;
    }

    if (m_Impl) {
        JPH::BodyInterface& bodyInterface = m_Impl->physicsSystem.GetBodyInterface();
        for (auto& entry : m_Bodies) {
            bodyInterface.RemoveBody(entry.second.id);
            bodyInterface.DestroyBody(entry.second.id);
        }
        m_Bodies.clear();
        m_Pending.clear();
        m_Impl->physicsSystem.SetContactListener(nullptr);
        m_Impl->contactListener.reset();
        {
            std::lock_guard<std::mutex> lock(m_Impl->eventMutex);
            m_Impl->pendingEvents.clear();
            m_Impl->activeContacts.clear();
        }
    }

    if (--g_JoltRefCount == 0) {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
        g_JoltInitialized = false;
    }

    m_Impl.reset();
    m_Initialized = false;
}

void PhysicsWorld::update(float deltaTime, bool simulate) {
    if (!m_Initialized) {
        return;
    }

    if (!m_Impl) {
        return;
    }

    flushPending();
    if (!simulate) {
        syncEditorBodies();
        return;
    }
    syncKinematicBodies();

    m_TimeAccumulator += deltaTime;
    int steps = 0;
    while (m_TimeAccumulator >= m_FixedTimeStep && steps < 4) {
        m_Impl->physicsSystem.Update(m_FixedTimeStep, 1, m_Impl->tempAllocator.get(), m_Impl->jobSystem.get());
        m_TimeAccumulator -= m_FixedTimeStep;
        steps++;
    }

    syncDynamicBodies();
    dispatchContactEvents();
}

void PhysicsWorld::setGravity(const Math::Vector3& gravity) {
    m_Gravity = gravity;
    if (!m_Initialized) {
        return;
    }
    if (m_Impl) {
        m_Impl->physicsSystem.SetGravity(ToJolt(m_Gravity));
    }
}

void PhysicsWorld::queueBodyRebuild(Entity* entity) {
    if (!entity) {
        return;
    }
    m_Pending.insert(entity->getUUID());
}

void PhysicsWorld::removeBody(Entity* entity) {
    if (!entity || !m_Initialized) {
        return;
    }
    auto it = m_Bodies.find(entity->getUUID());
    if (it == m_Bodies.end()) {
        return;
    }
    m_Pending.erase(entity->getUUID());
    if (!m_Impl) {
        return;
    }
    JPH::BodyInterface& bodyInterface = m_Impl->physicsSystem.GetBodyInterface();
    JPH::BodyID bodyID = it->second.id;
    bodyInterface.RemoveBody(it->second.id);
    bodyInterface.DestroyBody(it->second.id);
    {
        std::lock_guard<std::mutex> lock(m_Impl->eventMutex);
        for (auto pairIt = m_Impl->activeContacts.begin(); pairIt != m_Impl->activeContacts.end(); ) {
            if (pairIt->first.body1 == bodyID || pairIt->first.body2 == bodyID) {
                pairIt = m_Impl->activeContacts.erase(pairIt);
            } else {
                ++pairIt;
            }
        }
    }
    m_Bodies.erase(it);
}

void PhysicsWorld::debugDraw(DebugRenderer* renderer) {
    if (!renderer || !m_DebugDraw) {
        return;
    }
    for (const auto& entry : m_Bodies) {
        Entity* entity = m_Scene->findEntity(entry.first);
        if (!entity) {
            continue;
        }
        PhysicsCollider* collider = entity->getComponent<PhysicsCollider>();
        if (!collider) {
            continue;
        }
        Transform* transform = entity->getTransform();
        if (!transform) {
            continue;
        }
        Math::Vector3 pos = transform->getPosition() + collider->getCenter();
        Math::Vector3 scale = transform->getScale();
        Math::Vector3 absScale(std::abs(scale.x), std::abs(scale.y), std::abs(scale.z));
        Math::Vector4 color(0.6f, 0.6f, 0.6f, 1.0f);
        if (collider->isTrigger()) {
            color = Math::Vector4(1.0f, 0.55f, 0.2f, 1.0f);
        } else {
            switch (entry.second.type) {
            case RigidbodyType::Dynamic:
                color = Math::Vector4(0.2f, 0.8f, 1.0f, 1.0f);
                break;
            case RigidbodyType::Kinematic:
                color = Math::Vector4(1.0f, 0.85f, 0.2f, 1.0f);
                break;
            case RigidbodyType::Static:
            default:
                color = Math::Vector4(0.65f, 0.7f, 0.75f, 1.0f);
                break;
            }
        }

        switch (collider->getShapeType()) {
        case PhysicsCollider::ShapeType::Sphere: {
            float radius = collider->getRadius() * std::max({absScale.x, absScale.y, absScale.z});
            radius *= 1.01f;
            renderer->drawSphere(pos, radius, color);
            break;
        }
        case PhysicsCollider::ShapeType::Capsule: {
            float radius = collider->getRadius() * std::max(absScale.x, absScale.z);
            radius *= 1.01f;
            float halfHeight = std::max(0.0f, (collider->getHeight() * absScale.y * 0.5f) - radius);
            Math::Vector3 top = pos + Math::Vector3(0.0f, halfHeight, 0.0f);
            Math::Vector3 bottom = pos - Math::Vector3(0.0f, halfHeight, 0.0f);
            renderer->drawSphere(top, radius, color);
            renderer->drawSphere(bottom, radius, color);
            renderer->drawLine(top + Math::Vector3(radius, 0, 0), bottom + Math::Vector3(radius, 0, 0), color);
            renderer->drawLine(top - Math::Vector3(radius, 0, 0), bottom - Math::Vector3(radius, 0, 0), color);
            renderer->drawLine(top + Math::Vector3(0, 0, radius), bottom + Math::Vector3(0, 0, radius), color);
            renderer->drawLine(top - Math::Vector3(0, 0, radius), bottom - Math::Vector3(0, 0, radius), color);
            break;
        }
        case PhysicsCollider::ShapeType::Box:
        default: {
            Math::Vector3 size = collider->getSize();
            size.x *= absScale.x;
            size.y *= absScale.y;
            size.z *= absScale.z;
            size *= 1.01f;
            renderer->drawBox(pos, size, color);
            break;
        }
        }
        renderer->drawAxes(pos, 0.2f * std::max({absScale.x, absScale.y, absScale.z}));
    }
}

void PhysicsWorld::flushPending() {
    if (m_Pending.empty()) {
        return;
    }
    std::unordered_set<UUID> pending = std::move(m_Pending);
    m_Pending.clear();
    for (const UUID& uuid : pending) {
        Entity* entity = m_Scene->findEntity(uuid);
        if (!entity) {
            continue;
        }
        rebuildBody(entity);
    }
    if (m_Impl) {
        m_Impl->physicsSystem.OptimizeBroadPhase();
    }
}

void PhysicsWorld::rebuildBody(Entity* entity) {
    if (!entity || !m_Initialized) {
        return;
    }
    PhysicsCollider* collider = entity->getComponent<PhysicsCollider>();
    if (!collider) {
        removeBody(entity);
        return;
    }

    Rigidbody* rb = entity->getComponent<Rigidbody>();
    RigidbodyType type = rb ? rb->getType() : RigidbodyType::Static;

    removeBody(entity);

    if (!m_Impl) {
        return;
    }

    Transform* transform = entity->getTransform();
    if (!transform) {
        return;
    }

    Math::Vector3 pos = transform->getPosition();
    Math::Quaternion rot = transform->getRotation();
    Math::Vector3 scale = transform->getScale();

    JPH::RefConst<JPH::Shape> shape = buildShape(*collider, scale);
    if (!shape) {
        return;
    }

    JPH::EMotionType motionType = JPH::EMotionType::Static;
    JPH::ObjectLayer layer = Layers::NonMoving;
    if (type == RigidbodyType::Dynamic) {
        motionType = JPH::EMotionType::Dynamic;
        layer = Layers::Moving;
    } else if (type == RigidbodyType::Kinematic) {
        motionType = JPH::EMotionType::Kinematic;
        layer = Layers::Moving;
    }

    JPH::BodyCreationSettings settings(shape,
                                       JPH::RVec3(pos.x, pos.y, pos.z),
                                       ToJolt(rot),
                                       motionType,
                                       layer);
    uint32_t collisionLayer = collider->getCollisionLayer();
    if (collisionLayer >= PhysicsCollider::kMaxLayers) {
        collisionLayer = PhysicsCollider::kMaxLayers - 1;
    }
    uint32_t collisionMask = collider->getCollisionMask();
    settings.mCollisionGroup.SetGroupID(collisionLayer);
    settings.mCollisionGroup.SetSubGroupID(collisionMask);
    settings.mIsSensor = collider->isTrigger();
    settings.mFriction = collider->getFriction();
    settings.mRestitution = collider->getRestitution();
    settings.mUserData = reinterpret_cast<uint64_t>(entity);

    if (rb) {
        settings.mLinearDamping = rb->getLinearDamping();
        settings.mAngularDamping = rb->getAngularDamping();
        settings.mAllowSleeping = rb->getAllowSleep();
        settings.mGravityFactor = rb->getUseGravity() ? 1.0f : 0.0f;
        settings.mMotionQuality = rb->getContinuousCollision()
            ? JPH::EMotionQuality::LinearCast
            : JPH::EMotionQuality::Discrete;

        if (motionType == JPH::EMotionType::Dynamic) {
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = rb->getMass();
        }
    }

    JPH::BodyInterface& bodyInterface = m_Impl->physicsSystem.GetBodyInterface();
    JPH::Body* body = bodyInterface.CreateBody(settings);
    if (!body) {
        return;
    }
    JPH::EActivation activation = (motionType == JPH::EMotionType::Dynamic)
        ? JPH::EActivation::Activate
        : JPH::EActivation::DontActivate;
    bodyInterface.AddBody(body->GetID(), activation);

    BodyRecord record;
    record.id = body->GetID();
    record.type = type;
    record.lastPosition = pos;
    record.lastRotation = rot;
    record.lastScale = scale;
    record.isSensor = collider->isTrigger();
    m_Bodies[entity->getUUID()] = record;
}

void PhysicsWorld::syncKinematicBodies() {
    if (!m_Initialized) {
        return;
    }
    if (!m_Impl) {
        return;
    }
    JPH::BodyInterface& bodyInterface = m_Impl->physicsSystem.GetBodyInterface();
    for (auto& entry : m_Bodies) {
        Entity* entity = m_Scene->findEntity(entry.first);
        if (!entity) {
            continue;
        }
        if (entry.second.type == RigidbodyType::Dynamic) {
            continue;
        }
        Transform* transform = entity->getTransform();
        if (!transform) {
            continue;
        }
        Math::Vector3 scale = transform->getScale();
        if (scale != entry.second.lastScale) {
            queueBodyRebuild(entity);
            continue;
        }
        updateBodyTransform(entry.second, entity);
        bodyInterface.SetPositionAndRotation(entry.second.id,
                                             JPH::RVec3(entry.second.lastPosition.x,
                                                        entry.second.lastPosition.y,
                                                        entry.second.lastPosition.z),
                                             ToJolt(entry.second.lastRotation),
                                             JPH::EActivation::DontActivate);
    }
}

void PhysicsWorld::syncDynamicBodies() {
    if (!m_Initialized) {
        return;
    }
    if (!m_Impl) {
        return;
    }
    JPH::BodyInterface& bodyInterface = m_Impl->physicsSystem.GetBodyInterface();
    for (auto& entry : m_Bodies) {
        if (entry.second.type != RigidbodyType::Dynamic) {
            continue;
        }
        Entity* entity = m_Scene->findEntity(entry.first);
        if (!entity) {
            continue;
        }
        Transform* transform = entity->getTransform();
        if (transform && transform->getScale() != entry.second.lastScale) {
            queueBodyRebuild(entity);
            continue;
        }
        JPH::RVec3 pos = bodyInterface.GetPosition(entry.second.id);
        JPH::Quat rot = bodyInterface.GetRotation(entry.second.id);
        Math::Vector3 newPos = Math::Vector3(static_cast<float>(pos.GetX()),
                                             static_cast<float>(pos.GetY()),
                                             static_cast<float>(pos.GetZ()));
        Math::Quaternion newRot = ToCrescent(rot);
        if (transform) {
            transform->setPosition(newPos);
            transform->setRotation(newRot);
        }
        entry.second.lastPosition = newPos;
        entry.second.lastRotation = newRot;
    }
}

void PhysicsWorld::syncEditorBodies() {
    if (!m_Initialized) {
        return;
    }
    if (!m_Impl) {
        return;
    }
    JPH::BodyInterface& bodyInterface = m_Impl->physicsSystem.GetBodyInterface();
    for (auto& entry : m_Bodies) {
        Entity* entity = m_Scene->findEntity(entry.first);
        if (!entity) {
            continue;
        }
        Transform* transform = entity->getTransform();
        if (!transform) {
            continue;
        }
        Math::Vector3 scale = transform->getScale();
        if (scale != entry.second.lastScale) {
            queueBodyRebuild(entity);
            continue;
        }
        updateBodyTransform(entry.second, entity);
        bodyInterface.SetPositionAndRotation(entry.second.id,
                                             JPH::RVec3(entry.second.lastPosition.x,
                                                        entry.second.lastPosition.y,
                                                        entry.second.lastPosition.z),
                                             ToJolt(entry.second.lastRotation),
                                             JPH::EActivation::DontActivate);
    }
}

void PhysicsWorld::updateBodyTransform(BodyRecord& record, Entity* entity) {
    Transform* transform = entity->getTransform();
    if (!transform) {
        return;
    }
    record.lastPosition = transform->getPosition();
    record.lastRotation = transform->getRotation();
    record.lastScale = transform->getScale();
}

JPH::RefConst<JPH::Shape> PhysicsWorld::buildShape(const PhysicsCollider& collider,
                                                   const Math::Vector3& scale) const {
    Math::Vector3 absScale(std::abs(scale.x), std::abs(scale.y), std::abs(scale.z));
    JPH::RefConst<JPH::Shape> shape;

    switch (collider.getShapeType()) {
    case PhysicsCollider::ShapeType::Sphere: {
        float radius = collider.getRadius() * std::max({absScale.x, absScale.y, absScale.z});
        shape = new JPH::SphereShape(radius);
        break;
    }
    case PhysicsCollider::ShapeType::Capsule: {
        float radius = collider.getRadius() * std::max(absScale.x, absScale.z);
        float halfHeight = std::max(0.0f, (collider.getHeight() * absScale.y * 0.5f) - radius);
        shape = new JPH::CapsuleShape(halfHeight, radius);
        break;
    }
    case PhysicsCollider::ShapeType::Box:
    default: {
        Math::Vector3 half = collider.getSize() * 0.5f;
        half.x *= absScale.x;
        half.y *= absScale.y;
        half.z *= absScale.z;
        shape = new JPH::BoxShape(JPH::Vec3(half.x, half.y, half.z));
        break;
    }
    }

    Math::Vector3 center = collider.getCenter();
    if (shape && (center.x != 0.0f || center.y != 0.0f || center.z != 0.0f)) {
        shape = new JPH::RotatedTranslatedShape(JPH::Vec3(center.x, center.y, center.z),
                                                JPH::Quat::sIdentity(),
                                                shape);
    }
    return shape;
}

Entity* PhysicsWorld::resolveEntity(const JPH::BodyID& bodyID) const {
    if (!m_Impl) {
        return nullptr;
    }
    const auto& lockInterface = m_Impl->physicsSystem.GetBodyLockInterface();
    JPH::BodyLockRead lock(lockInterface, bodyID);
    if (!lock.Succeeded()) {
        return nullptr;
    }
    const JPH::Body& body = lock.GetBody();
    return reinterpret_cast<Entity*>(body.GetUserData());
}

void PhysicsWorld::dispatchContactEvents() {
    if (!m_Impl) {
        return;
    }
    std::vector<ContactEvent> events;
    {
        std::lock_guard<std::mutex> lock(m_Impl->eventMutex);
        if (m_Impl->pendingEvents.empty()) {
            return;
        }
        events.swap(m_Impl->pendingEvents);
    }

    struct EventKey {
        BodyPairKey pair;
        ContactEventType type;

        bool operator==(const EventKey& other) const {
            return pair == other.pair && type == other.type;
        }
    };

    struct EventKeyHash {
        size_t operator()(const EventKey& key) const {
            return static_cast<size_t>(JPH::HashCombineArgs(
                static_cast<uint64_t>(BodyPairKeyHash{}(key.pair)),
                static_cast<uint32_t>(key.type)));
        }
    };

    std::unordered_set<EventKey, EventKeyHash> seen;
    for (const auto& event : events) {
        BodyPairKey pair = MakeBodyPairKey(event.body1, event.body2);
        EventKey key{pair, event.type};
        if (!seen.insert(key).second) {
            continue;
        }

        Entity* entity1 = resolveEntity(event.body1);
        Entity* entity2 = resolveEntity(event.body2);
        if (!entity1 || !entity2) {
            continue;
        }
        if (!entity1->isActiveInHierarchy() || !entity2->isActiveInHierarchy()) {
            continue;
        }

        PhysicsContact contact1;
        contact1.other = entity2;
        contact1.point = event.point1;
        contact1.normal = event.normal;
        contact1.penetration = event.penetration;
        contact1.isTrigger = event.isTrigger;

        PhysicsContact contact2;
        contact2.other = entity1;
        contact2.point = event.point2;
        contact2.normal = -event.normal;
        contact2.penetration = event.penetration;
        contact2.isTrigger = event.isTrigger;

        if (event.isTrigger) {
            switch (event.type) {
            case ContactEventType::Enter:
                entity1->OnTriggerEnter(contact1);
                entity2->OnTriggerEnter(contact2);
                break;
            case ContactEventType::Stay:
                entity1->OnTriggerStay(contact1);
                entity2->OnTriggerStay(contact2);
                break;
            case ContactEventType::Exit:
                entity1->OnTriggerExit(contact1);
                entity2->OnTriggerExit(contact2);
                break;
            }
        } else {
            switch (event.type) {
            case ContactEventType::Enter:
                entity1->OnCollisionEnter(contact1);
                entity2->OnCollisionEnter(contact2);
                break;
            case ContactEventType::Stay:
                entity1->OnCollisionStay(contact1);
                entity2->OnCollisionStay(contact2);
                break;
            case ContactEventType::Exit:
                entity1->OnCollisionExit(contact1);
                entity2->OnCollisionExit(contact2);
                break;
            }
        }
    }
}

namespace {

class QueryBodyFilter final : public JPH::BodyFilter {
public:
    QueryBodyFilter(uint32_t mask, bool includeTriggers, const Entity* ignore)
        : m_Mask(mask)
        , m_IncludeTriggers(includeTriggers)
        , m_Ignore(ignore) {}

    bool ShouldCollideLocked(const JPH::Body& body) const override {
        if (!m_IncludeTriggers && body.IsSensor()) {
            return false;
        }
        if (m_Ignore) {
            const auto* entity = reinterpret_cast<const Entity*>(body.GetUserData());
            if (entity == m_Ignore) {
                return false;
            }
        }
        const JPH::CollisionGroup& group = body.GetCollisionGroup();
        uint32_t layer = group.GetGroupID();
        if (layer == JPH::CollisionGroup::cInvalidGroup) {
            return true;
        }
        if (layer >= PhysicsCollider::kMaxLayers) {
            return false;
        }
        return (m_Mask & (1u << layer)) != 0;
    }

private:
    uint32_t m_Mask;
    bool m_IncludeTriggers;
    const Entity* m_Ignore;
};

bool PopulateShapeCastHit(const JPH::ShapeCastResult& hit,
                          float maxDistance,
                          const Math::Vector3& direction,
                          const JPH::BodyLockInterface& lockInterface,
                          PhysicsRaycastHit& outHit) {
    JPH::BodyLockRead lock(lockInterface, hit.mBodyID2);
    if (!lock.Succeeded()) {
        return false;
    }

    const JPH::Body& body = lock.GetBody();
    Entity* entity = reinterpret_cast<Entity*>(body.GetUserData());
    if (!entity) {
        return false;
    }

    JPH::Vec3 axis = hit.mPenetrationAxis;
    JPH::Vec3 fallback = JPH::Vec3(direction.x, direction.y, direction.z);
    JPH::Vec3 normal = (axis.LengthSq() > 0.0f) ? -axis.Normalized() : -fallback;

    outHit.hit = true;
    outHit.entity = entity;
    outHit.distance = hit.mFraction * maxDistance;
    outHit.point = Math::Vector3(hit.mContactPointOn2.GetX(),
                                 hit.mContactPointOn2.GetY(),
                                 hit.mContactPointOn2.GetZ());
    outHit.normal = Math::Vector3(normal.GetX(), normal.GetY(), normal.GetZ());
    outHit.isTrigger = body.IsSensor();
    return true;
}

} // namespace

bool PhysicsWorld::raycast(const Math::Vector3& origin,
                           const Math::Vector3& direction,
                           float maxDistance,
                           PhysicsRaycastHit& outHit,
                           uint32_t layerMask,
                           bool includeTriggers,
                           const Entity* ignore) const {
    outHit = PhysicsRaycastHit{};
    if (!m_Impl || maxDistance <= 0.0f) {
        return false;
    }

    Math::Vector3 dir = direction.normalized();
    if (dir.lengthSquared() == 0.0f) {
        return false;
    }
    Math::Vector3 scaled = dir * maxDistance;

    JPH::RRayCast ray(JPH::RVec3(origin.x, origin.y, origin.z),
                      JPH::Vec3(scaled.x, scaled.y, scaled.z));
    JPH::RayCastResult hit;
    QueryBodyFilter bodyFilter(layerMask, includeTriggers, ignore);
    const auto& query = m_Impl->physicsSystem.GetNarrowPhaseQuery();
    if (!query.CastRay(ray, hit, {}, {}, bodyFilter)) {
        return false;
    }

    const auto& lockInterface = m_Impl->physicsSystem.GetBodyLockInterface();
    JPH::BodyLockRead lock(lockInterface, hit.mBodyID);
    if (!lock.Succeeded()) {
        return false;
    }

    const JPH::Body& body = lock.GetBody();
    Entity* entity = reinterpret_cast<Entity*>(body.GetUserData());
    if (!entity) {
        return false;
    }

    JPH::RVec3 hitPoint = ray.GetPointOnRay(hit.mFraction);
    JPH::Vec3 normal = body.GetTransformedShape().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, hitPoint);

    outHit.hit = true;
    outHit.entity = entity;
    outHit.distance = hit.mFraction * maxDistance;
    outHit.point = Math::Vector3(static_cast<float>(hitPoint.GetX()),
                                 static_cast<float>(hitPoint.GetY()),
                                 static_cast<float>(hitPoint.GetZ()));
    outHit.normal = Math::Vector3(normal.GetX(), normal.GetY(), normal.GetZ());
    outHit.isTrigger = body.IsSensor();
    return true;
}

int PhysicsWorld::raycastAll(const Math::Vector3& origin,
                             const Math::Vector3& direction,
                             float maxDistance,
                             std::vector<PhysicsRaycastHit>& outHits,
                             uint32_t layerMask,
                             bool includeTriggers,
                             const Entity* ignore) const {
    outHits.clear();
    if (!m_Impl || maxDistance <= 0.0f) {
        return 0;
    }

    Math::Vector3 dir = direction.normalized();
    if (dir.lengthSquared() == 0.0f) {
        return 0;
    }
    Math::Vector3 scaled = dir * maxDistance;

    JPH::RRayCast ray(JPH::RVec3(origin.x, origin.y, origin.z),
                      JPH::Vec3(scaled.x, scaled.y, scaled.z));
    JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
    QueryBodyFilter bodyFilter(layerMask, includeTriggers, ignore);
    const auto& query = m_Impl->physicsSystem.GetNarrowPhaseQuery();
    query.CastRay(ray, JPH::RayCastSettings(), collector, {}, {}, bodyFilter);

    if (!collector.HadHit()) {
        return 0;
    }
    collector.Sort();

    const auto& lockInterface = m_Impl->physicsSystem.GetBodyLockInterface();
    outHits.reserve(collector.mHits.size());
    for (const auto& hit : collector.mHits) {
        JPH::BodyLockRead lock(lockInterface, hit.mBodyID);
        if (!lock.Succeeded()) {
            continue;
        }
        const JPH::Body& body = lock.GetBody();
        Entity* entity = reinterpret_cast<Entity*>(body.GetUserData());
        if (!entity) {
            continue;
        }

        JPH::RVec3 hitPoint = ray.GetPointOnRay(hit.mFraction);
        JPH::Vec3 normal = body.GetTransformedShape().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, hitPoint);

        PhysicsRaycastHit result;
        result.hit = true;
        result.entity = entity;
        result.distance = hit.mFraction * maxDistance;
        result.point = Math::Vector3(static_cast<float>(hitPoint.GetX()),
                                     static_cast<float>(hitPoint.GetY()),
                                     static_cast<float>(hitPoint.GetZ()));
        result.normal = Math::Vector3(normal.GetX(), normal.GetY(), normal.GetZ());
        result.isTrigger = body.IsSensor();
        outHits.push_back(result);
    }

    return static_cast<int>(outHits.size());
}

bool PhysicsWorld::sphereCast(const Math::Vector3& center,
                              float radius,
                              const Math::Vector3& direction,
                              float maxDistance,
                              PhysicsRaycastHit& outHit,
                              uint32_t layerMask,
                              bool includeTriggers,
                              const Entity* ignore) const {
    outHit = PhysicsRaycastHit{};
    if (!m_Impl || radius <= 0.0f || maxDistance <= 0.0f) {
        return false;
    }

    Math::Vector3 dir = direction.normalized();
    if (dir.lengthSquared() == 0.0f) {
        return false;
    }

    JPH::RefConst<JPH::Shape> shape = new JPH::SphereShape(radius);
    JPH::Vec3 castDir(dir.x * maxDistance, dir.y * maxDistance, dir.z * maxDistance);
    JPH::RMat44 world = JPH::RMat44::sTranslation(JPH::RVec3(center.x, center.y, center.z));
    JPH::RShapeCast shapeCast = JPH::RShapeCast::sFromWorldTransform(shape,
                                                                     JPH::Vec3(1.0f, 1.0f, 1.0f),
                                                                     world,
                                                                     castDir);
    JPH::ShapeCastSettings settings;
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    QueryBodyFilter bodyFilter(layerMask, includeTriggers, ignore);
    const auto& query = m_Impl->physicsSystem.GetNarrowPhaseQuery();
    query.CastShape(shapeCast, settings, JPH::RVec3::sZero(), collector, {}, {}, bodyFilter);

    if (!collector.HadHit()) {
        return false;
    }

    const auto& lockInterface = m_Impl->physicsSystem.GetBodyLockInterface();
    return PopulateShapeCastHit(collector.mHit, maxDistance, dir, lockInterface, outHit);
}

int PhysicsWorld::sphereCastAll(const Math::Vector3& center,
                                float radius,
                                const Math::Vector3& direction,
                                float maxDistance,
                                std::vector<PhysicsRaycastHit>& outHits,
                                uint32_t layerMask,
                                bool includeTriggers,
                                const Entity* ignore) const {
    outHits.clear();
    if (!m_Impl || radius <= 0.0f || maxDistance <= 0.0f) {
        return 0;
    }

    Math::Vector3 dir = direction.normalized();
    if (dir.lengthSquared() == 0.0f) {
        return 0;
    }

    JPH::RefConst<JPH::Shape> shape = new JPH::SphereShape(radius);
    JPH::Vec3 castDir(dir.x * maxDistance, dir.y * maxDistance, dir.z * maxDistance);
    JPH::RMat44 world = JPH::RMat44::sTranslation(JPH::RVec3(center.x, center.y, center.z));
    JPH::RShapeCast shapeCast = JPH::RShapeCast::sFromWorldTransform(shape,
                                                                     JPH::Vec3(1.0f, 1.0f, 1.0f),
                                                                     world,
                                                                     castDir);
    JPH::ShapeCastSettings settings;
    JPH::AllHitCollisionCollector<JPH::CastShapeCollector> collector;
    QueryBodyFilter bodyFilter(layerMask, includeTriggers, ignore);
    const auto& query = m_Impl->physicsSystem.GetNarrowPhaseQuery();
    query.CastShape(shapeCast, settings, JPH::RVec3::sZero(), collector, {}, {}, bodyFilter);

    if (!collector.HadHit()) {
        return 0;
    }
    collector.Sort();

    const auto& lockInterface = m_Impl->physicsSystem.GetBodyLockInterface();
    outHits.reserve(collector.mHits.size());
    for (const auto& hit : collector.mHits) {
        PhysicsRaycastHit result;
        if (!PopulateShapeCastHit(hit, maxDistance, dir, lockInterface, result)) {
            continue;
        }
        outHits.push_back(result);
    }

    return static_cast<int>(outHits.size());
}

bool PhysicsWorld::boxCast(const Math::Vector3& center,
                           const Math::Vector3& halfExtents,
                           const Math::Vector3& direction,
                           float maxDistance,
                           PhysicsRaycastHit& outHit,
                           uint32_t layerMask,
                           bool includeTriggers,
                           const Entity* ignore) const {
    outHit = PhysicsRaycastHit{};
    if (!m_Impl || maxDistance <= 0.0f) {
        return false;
    }
    if (halfExtents.x <= 0.0f || halfExtents.y <= 0.0f || halfExtents.z <= 0.0f) {
        return false;
    }

    Math::Vector3 dir = direction.normalized();
    if (dir.lengthSquared() == 0.0f) {
        return false;
    }

    JPH::RefConst<JPH::Shape> shape = new JPH::BoxShape(JPH::Vec3(halfExtents.x,
                                                                 halfExtents.y,
                                                                 halfExtents.z));
    JPH::Vec3 castDir(dir.x * maxDistance, dir.y * maxDistance, dir.z * maxDistance);
    JPH::RMat44 world = JPH::RMat44::sTranslation(JPH::RVec3(center.x, center.y, center.z));
    JPH::RShapeCast shapeCast = JPH::RShapeCast::sFromWorldTransform(shape,
                                                                     JPH::Vec3(1.0f, 1.0f, 1.0f),
                                                                     world,
                                                                     castDir);
    JPH::ShapeCastSettings settings;
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    QueryBodyFilter bodyFilter(layerMask, includeTriggers, ignore);
    const auto& query = m_Impl->physicsSystem.GetNarrowPhaseQuery();
    query.CastShape(shapeCast, settings, JPH::RVec3::sZero(), collector, {}, {}, bodyFilter);

    if (!collector.HadHit()) {
        return false;
    }

    const auto& lockInterface = m_Impl->physicsSystem.GetBodyLockInterface();
    return PopulateShapeCastHit(collector.mHit, maxDistance, dir, lockInterface, outHit);
}

int PhysicsWorld::boxCastAll(const Math::Vector3& center,
                             const Math::Vector3& halfExtents,
                             const Math::Vector3& direction,
                             float maxDistance,
                             std::vector<PhysicsRaycastHit>& outHits,
                             uint32_t layerMask,
                             bool includeTriggers,
                             const Entity* ignore) const {
    outHits.clear();
    if (!m_Impl || maxDistance <= 0.0f) {
        return 0;
    }
    if (halfExtents.x <= 0.0f || halfExtents.y <= 0.0f || halfExtents.z <= 0.0f) {
        return 0;
    }

    Math::Vector3 dir = direction.normalized();
    if (dir.lengthSquared() == 0.0f) {
        return 0;
    }

    JPH::RefConst<JPH::Shape> shape = new JPH::BoxShape(JPH::Vec3(halfExtents.x,
                                                                 halfExtents.y,
                                                                 halfExtents.z));
    JPH::Vec3 castDir(dir.x * maxDistance, dir.y * maxDistance, dir.z * maxDistance);
    JPH::RMat44 world = JPH::RMat44::sTranslation(JPH::RVec3(center.x, center.y, center.z));
    JPH::RShapeCast shapeCast = JPH::RShapeCast::sFromWorldTransform(shape,
                                                                     JPH::Vec3(1.0f, 1.0f, 1.0f),
                                                                     world,
                                                                     castDir);
    JPH::ShapeCastSettings settings;
    JPH::AllHitCollisionCollector<JPH::CastShapeCollector> collector;
    QueryBodyFilter bodyFilter(layerMask, includeTriggers, ignore);
    const auto& query = m_Impl->physicsSystem.GetNarrowPhaseQuery();
    query.CastShape(shapeCast, settings, JPH::RVec3::sZero(), collector, {}, {}, bodyFilter);

    if (!collector.HadHit()) {
        return 0;
    }
    collector.Sort();

    const auto& lockInterface = m_Impl->physicsSystem.GetBodyLockInterface();
    outHits.reserve(collector.mHits.size());
    for (const auto& hit : collector.mHits) {
        PhysicsRaycastHit result;
        if (!PopulateShapeCastHit(hit, maxDistance, dir, lockInterface, result)) {
            continue;
        }
        outHits.push_back(result);
    }

    return static_cast<int>(outHits.size());
}

bool PhysicsWorld::capsuleCast(const Math::Vector3& center,
                               float radius,
                               float height,
                               const Math::Vector3& direction,
                               float maxDistance,
                               PhysicsRaycastHit& outHit,
                               uint32_t layerMask,
                               bool includeTriggers,
                               const Entity* ignore) const {
    outHit = PhysicsRaycastHit{};
    if (!m_Impl || radius <= 0.0f || maxDistance <= 0.0f) {
        return false;
    }

    float halfHeight = std::max(0.0f, (height * 0.5f) - radius);
    Math::Vector3 dir = direction.normalized();
    if (dir.lengthSquared() == 0.0f) {
        return false;
    }

    JPH::RefConst<JPH::Shape> shape = new JPH::CapsuleShape(halfHeight, radius);
    JPH::Vec3 castDir(dir.x * maxDistance, dir.y * maxDistance, dir.z * maxDistance);
    JPH::RMat44 world = JPH::RMat44::sTranslation(JPH::RVec3(center.x, center.y, center.z));
    JPH::RShapeCast shapeCast = JPH::RShapeCast::sFromWorldTransform(shape,
                                                                     JPH::Vec3(1.0f, 1.0f, 1.0f),
                                                                     world,
                                                                     castDir);
    JPH::ShapeCastSettings settings;
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    QueryBodyFilter bodyFilter(layerMask, includeTriggers, ignore);
    const auto& query = m_Impl->physicsSystem.GetNarrowPhaseQuery();
    query.CastShape(shapeCast, settings, JPH::RVec3::sZero(), collector, {}, {}, bodyFilter);

    if (!collector.HadHit()) {
        return false;
    }

    const auto& lockInterface = m_Impl->physicsSystem.GetBodyLockInterface();
    return PopulateShapeCastHit(collector.mHit, maxDistance, dir, lockInterface, outHit);
}

int PhysicsWorld::capsuleCastAll(const Math::Vector3& center,
                                 float radius,
                                 float height,
                                 const Math::Vector3& direction,
                                 float maxDistance,
                                 std::vector<PhysicsRaycastHit>& outHits,
                                 uint32_t layerMask,
                                 bool includeTriggers,
                                 const Entity* ignore) const {
    outHits.clear();
    if (!m_Impl || radius <= 0.0f || maxDistance <= 0.0f) {
        return 0;
    }

    float halfHeight = std::max(0.0f, (height * 0.5f) - radius);
    Math::Vector3 dir = direction.normalized();
    if (dir.lengthSquared() == 0.0f) {
        return 0;
    }

    JPH::RefConst<JPH::Shape> shape = new JPH::CapsuleShape(halfHeight, radius);
    JPH::Vec3 castDir(dir.x * maxDistance, dir.y * maxDistance, dir.z * maxDistance);
    JPH::RMat44 world = JPH::RMat44::sTranslation(JPH::RVec3(center.x, center.y, center.z));
    JPH::RShapeCast shapeCast = JPH::RShapeCast::sFromWorldTransform(shape,
                                                                     JPH::Vec3(1.0f, 1.0f, 1.0f),
                                                                     world,
                                                                     castDir);
    JPH::ShapeCastSettings settings;
    JPH::AllHitCollisionCollector<JPH::CastShapeCollector> collector;
    QueryBodyFilter bodyFilter(layerMask, includeTriggers, ignore);
    const auto& query = m_Impl->physicsSystem.GetNarrowPhaseQuery();
    query.CastShape(shapeCast, settings, JPH::RVec3::sZero(), collector, {}, {}, bodyFilter);

    if (!collector.HadHit()) {
        return 0;
    }
    collector.Sort();

    const auto& lockInterface = m_Impl->physicsSystem.GetBodyLockInterface();
    outHits.reserve(collector.mHits.size());
    for (const auto& hit : collector.mHits) {
        PhysicsRaycastHit result;
        if (!PopulateShapeCastHit(hit, maxDistance, dir, lockInterface, result)) {
            continue;
        }
        outHits.push_back(result);
    }

    return static_cast<int>(outHits.size());
}

int PhysicsWorld::overlapSphere(const Math::Vector3& center,
                                float radius,
                                std::vector<PhysicsOverlapHit>& outHits,
                                uint32_t layerMask,
                                bool includeTriggers,
                                const Entity* ignore) const {
    outHits.clear();
    if (!m_Impl || radius <= 0.0f) {
        return 0;
    }

    JPH::SphereShape shape(radius);
    JPH::RMat44 transform = JPH::RMat44::sTranslation(JPH::RVec3(center.x, center.y, center.z));
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    QueryBodyFilter bodyFilter(layerMask, includeTriggers, ignore);
    const auto& query = m_Impl->physicsSystem.GetNarrowPhaseQuery();
    query.CollideShape(&shape,
                       JPH::Vec3(1.0f, 1.0f, 1.0f),
                       transform,
                       JPH::CollideShapeSettings(),
                       JPH::RVec3::sZero(),
                       collector,
                       {},
                       {},
                       bodyFilter);

    if (!collector.HadHit()) {
        return 0;
    }

    const auto& lockInterface = m_Impl->physicsSystem.GetBodyLockInterface();
    outHits.reserve(collector.mHits.size());
    for (const auto& hit : collector.mHits) {
        JPH::BodyLockRead lock(lockInterface, hit.mBodyID2);
        if (!lock.Succeeded()) {
            continue;
        }
        const JPH::Body& body = lock.GetBody();
        Entity* entity = reinterpret_cast<Entity*>(body.GetUserData());
        if (!entity) {
            continue;
        }

        PhysicsOverlapHit result;
        result.entity = entity;
        result.point = Math::Vector3(hit.mContactPointOn2.GetX(),
                                     hit.mContactPointOn2.GetY(),
                                     hit.mContactPointOn2.GetZ());
        Math::Vector3 normal = Math::Vector3(hit.mPenetrationAxis.GetX(),
                                             hit.mPenetrationAxis.GetY(),
                                             hit.mPenetrationAxis.GetZ());
        result.normal = -normal.normalized();
        result.penetration = hit.mPenetrationDepth;
        result.isTrigger = body.IsSensor();
        outHits.push_back(result);
    }

    return static_cast<int>(outHits.size());
}

int PhysicsWorld::overlapBox(const Math::Vector3& center,
                             const Math::Vector3& halfExtents,
                             std::vector<PhysicsOverlapHit>& outHits,
                             uint32_t layerMask,
                             bool includeTriggers,
                             const Entity* ignore) const {
    outHits.clear();
    if (!m_Impl) {
        return 0;
    }

    Math::Vector3 clamped(std::max(0.001f, halfExtents.x),
                          std::max(0.001f, halfExtents.y),
                          std::max(0.001f, halfExtents.z));
    JPH::BoxShape shape(JPH::Vec3(clamped.x, clamped.y, clamped.z));
    JPH::RMat44 transform = JPH::RMat44::sTranslation(JPH::RVec3(center.x, center.y, center.z));
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    QueryBodyFilter bodyFilter(layerMask, includeTriggers, ignore);
    const auto& query = m_Impl->physicsSystem.GetNarrowPhaseQuery();
    query.CollideShape(&shape,
                       JPH::Vec3(1.0f, 1.0f, 1.0f),
                       transform,
                       JPH::CollideShapeSettings(),
                       JPH::RVec3::sZero(),
                       collector,
                       {},
                       {},
                       bodyFilter);

    if (!collector.HadHit()) {
        return 0;
    }

    const auto& lockInterface = m_Impl->physicsSystem.GetBodyLockInterface();
    outHits.reserve(collector.mHits.size());
    for (const auto& hit : collector.mHits) {
        JPH::BodyLockRead lock(lockInterface, hit.mBodyID2);
        if (!lock.Succeeded()) {
            continue;
        }
        const JPH::Body& body = lock.GetBody();
        Entity* entity = reinterpret_cast<Entity*>(body.GetUserData());
        if (!entity) {
            continue;
        }

        PhysicsOverlapHit result;
        result.entity = entity;
        result.point = Math::Vector3(hit.mContactPointOn2.GetX(),
                                     hit.mContactPointOn2.GetY(),
                                     hit.mContactPointOn2.GetZ());
        Math::Vector3 normal = Math::Vector3(hit.mPenetrationAxis.GetX(),
                                             hit.mPenetrationAxis.GetY(),
                                             hit.mPenetrationAxis.GetZ());
        result.normal = -normal.normalized();
        result.penetration = hit.mPenetrationDepth;
        result.isTrigger = body.IsSensor();
        outHits.push_back(result);
    }

    return static_cast<int>(outHits.size());
}

} // namespace Crescent
