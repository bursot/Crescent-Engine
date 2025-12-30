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
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>

#include <algorithm>
#include <cmath>
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
    BroadPhaseLayerInterfaceImpl broadphaseLayerInterface;
    ObjectVsBroadPhaseLayerFilterImpl objectVsBroadphaseLayerFilter;
    ObjectLayerPairFilterImpl objectLayerPairFilter;
    JPH::PhysicsSystem physicsSystem;
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
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

    m_Impl->physicsSystem.SetGravity(ToJolt(m_Gravity));

    m_Initialized = true;
    m_DebugDraw = false;
    m_Pending.clear();
    m_Bodies.clear();
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
    bodyInterface.RemoveBody(it->second.id);
    bodyInterface.DestroyBody(it->second.id);
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
    settings.mIsSensor = collider->isTrigger();
    settings.mFriction = collider->getFriction();
    settings.mRestitution = collider->getRestitution();

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

} // namespace Crescent
