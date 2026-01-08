#pragma once

#include "../ECS/Component.hpp"
#include "../ECS/Entity.hpp"
#include "../ECS/Transform.hpp"
#include "../Input/InputManager.hpp"
#include "../Math/Math.hpp"
#include "../Scene/Scene.hpp"
#include "../Physics/PhysicsWorld.hpp"
#include "../Components/Rigidbody.hpp"
#include <algorithm>
#include <cmath>

namespace Crescent {

class CharacterController : public Component {
public:
    COMPONENT_TYPE(CharacterController)

    CharacterController()
        : m_Radius(0.5f)
        , m_Height(2.0f)
        , m_SkinWidth(0.02f)
        , m_MoveSpeed(5.0f)
        , m_Acceleration(30.0f)
        , m_AirAcceleration(10.0f)
        , m_JumpSpeed(5.5f)
        , m_Gravity(20.0f)
        , m_MaxFallSpeed(40.0f)
        , m_GroundSnapSpeed(2.0f)
        , m_StepOffset(0.4f)
        , m_SlopeLimit(50.0f)
        , m_SlopeSlideSpeed(8.0f)
        , m_GroundCheckDistance(0.08f)
        , m_UseInput(true)
        , m_UseGravity(true)
        , m_EnableStep(true)
        , m_EnableSlopeLimit(true)
        , m_SnapToGround(true)
        , m_CollisionMask(PhysicsWorld::kAllLayersMask) {
    }

    float getRadius() const { return m_Radius; }
    void setRadius(float radius) { m_Radius = std::max(0.01f, radius); }

    float getHeight() const { return m_Height; }
    void setHeight(float height) { m_Height = std::max(0.05f, height); }

    float getSkinWidth() const { return m_SkinWidth; }
    void setSkinWidth(float skin) { m_SkinWidth = std::max(0.0f, skin); }

    float getMoveSpeed() const { return m_MoveSpeed; }
    void setMoveSpeed(float speed) { m_MoveSpeed = std::max(0.0f, speed); }

    float getAcceleration() const { return m_Acceleration; }
    void setAcceleration(float accel) { m_Acceleration = std::max(0.0f, accel); }

    float getAirAcceleration() const { return m_AirAcceleration; }
    void setAirAcceleration(float accel) { m_AirAcceleration = std::max(0.0f, accel); }

    float getJumpSpeed() const { return m_JumpSpeed; }
    void setJumpSpeed(float speed) { m_JumpSpeed = std::max(0.0f, speed); }

    float getGravity() const { return m_Gravity; }
    void setGravity(float gravity) { m_Gravity = std::max(0.0f, gravity); }

    float getMaxFallSpeed() const { return m_MaxFallSpeed; }
    void setMaxFallSpeed(float speed) { m_MaxFallSpeed = std::max(0.0f, speed); }

    float getGroundSnapSpeed() const { return m_GroundSnapSpeed; }
    void setGroundSnapSpeed(float speed) { m_GroundSnapSpeed = std::max(0.0f, speed); }

    float getStepOffset() const { return m_StepOffset; }
    void setStepOffset(float step) { m_StepOffset = std::max(0.0f, step); }

    float getSlopeLimit() const { return m_SlopeLimit; }
    void setSlopeLimit(float degrees) { m_SlopeLimit = std::max(0.0f, degrees); }

    float getSlopeSlideSpeed() const { return m_SlopeSlideSpeed; }
    void setSlopeSlideSpeed(float speed) { m_SlopeSlideSpeed = std::max(0.0f, speed); }

    float getGroundCheckDistance() const { return m_GroundCheckDistance; }
    void setGroundCheckDistance(float distance) { m_GroundCheckDistance = std::max(0.0f, distance); }

    bool getUseInput() const { return m_UseInput; }
    void setUseInput(bool enabled) { m_UseInput = enabled; }

    bool getUseGravity() const { return m_UseGravity; }
    void setUseGravity(bool enabled) { m_UseGravity = enabled; }

    bool getEnableStep() const { return m_EnableStep; }
    void setEnableStep(bool enabled) { m_EnableStep = enabled; }

    bool getEnableSlopeLimit() const { return m_EnableSlopeLimit; }
    void setEnableSlopeLimit(bool enabled) { m_EnableSlopeLimit = enabled; }

    bool getSnapToGround() const { return m_SnapToGround; }
    void setSnapToGround(bool enabled) { m_SnapToGround = enabled; }

    bool isGrounded() const { return m_IsGrounded; }
    Math::Vector3 getGroundNormal() const { return m_GroundNormal; }

    Math::Vector3 getVelocity() const { return m_Velocity; }
    void setVelocity(const Math::Vector3& velocity) { m_Velocity = velocity; }

    Math::Vector2 getMoveInput() const { return m_MoveInput; }
    void setMoveInput(const Math::Vector2& input) { m_MoveInput = input; }

    void requestJump() { m_JumpQueued = true; }

    uint32_t getCollisionMask() const { return m_CollisionMask; }
    void setCollisionMask(uint32_t mask) { m_CollisionMask = mask; }

    void OnCreate() override {
        ensureRigidbodyCompatibility();
    }

    void OnUpdate(float deltaTime) override {
        if (!m_Entity || deltaTime <= 0.0f) {
            return;
        }
        ensureRigidbodyCompatibility();
        Transform* transform = m_Entity->getTransform();
        if (!transform) {
            return;
        }

        if (m_UseInput) {
            gatherInput();
        }

        Scene* scene = m_Entity->getScene();
        PhysicsWorld* physics = scene ? scene->getPhysicsWorld() : nullptr;
        if (!physics) {
            return;
        }

        Math::Vector3 position = transform->getPosition();
        Math::Vector3 groundNormal = Math::Vector3::Up;
        bool groundHit = probeGround(physics, position, groundNormal);
        bool grounded = groundHit && isWalkable(groundNormal);
        m_IsGrounded = grounded;
        m_GroundNormal = grounded ? groundNormal : Math::Vector3::Up;

        if (groundHit && !grounded && m_EnableSlopeLimit) {
            applySlopeSlide(groundNormal, deltaTime);
        }

        if (m_JumpQueued && grounded) {
            m_Velocity.y = m_JumpSpeed;
            m_IsGrounded = false;
        }
        m_JumpQueued = false;

        Math::Vector3 wishDir = computeMoveDirection(transform);
        if (wishDir.lengthSquared() > Math::EPSILON) {
            wishDir.normalize();
        }

        Math::Vector3 targetVelocity = wishDir * m_MoveSpeed;
        if (m_IsGrounded) {
            targetVelocity = projectOnPlane(targetVelocity, m_GroundNormal);
        }

        Math::Vector3 horizontalVelocity(m_Velocity.x, 0.0f, m_Velocity.z);
        float accel = m_IsGrounded ? m_Acceleration : m_AirAcceleration;
        horizontalVelocity = moveToward(horizontalVelocity, targetVelocity, accel * deltaTime);
        m_Velocity.x = horizontalVelocity.x;
        m_Velocity.z = horizontalVelocity.z;

        if (m_UseGravity) {
            if (!m_IsGrounded || m_Velocity.y > 0.0f) {
                m_Velocity.y -= m_Gravity * deltaTime;
                if (m_Velocity.y < -m_MaxFallSpeed) {
                    m_Velocity.y = -m_MaxFallSpeed;
                }
            } else if (m_SnapToGround) {
                m_Velocity.y = -m_GroundSnapSpeed;
            } else {
                m_Velocity.y = 0.0f;
            }
        }

        Math::Vector3 startPosition = position;
        Math::Vector3 horizontalMove = Math::Vector3(m_Velocity.x, 0.0f, m_Velocity.z) * deltaTime;
        position = moveHorizontal(physics, position, horizontalMove);

        Math::Vector3 verticalMove = Math::Vector3(0.0f, m_Velocity.y * deltaTime, 0.0f);
        position = moveVertical(physics, position, verticalMove);

        Math::Vector3 postNormal = Math::Vector3::Up;
        bool postHit = probeGround(physics, position, postNormal);
        bool postGrounded = postHit && isWalkable(postNormal);
        if (postGrounded) {
            if (!m_IsGrounded && m_Velocity.y < 0.0f) {
                m_Velocity.y = 0.0f;
            }
            m_IsGrounded = true;
            m_GroundNormal = postNormal;
        } else {
            m_IsGrounded = false;
            m_GroundNormal = Math::Vector3::Up;
        }

        transform->setPosition(position);

        Math::Vector3 actualDelta = position - startPosition;
        m_Velocity = actualDelta / deltaTime;
        if (m_IsGrounded && m_Velocity.y < 0.0f) {
            m_Velocity.y = 0.0f;
        }
    }

private:
    void gatherInput() {
        InputManager& input = InputManager::getInstance();

        float x = 0.0f;
        float y = 0.0f;
        if (input.isKeyPressed(KeyCode::A)) x -= 1.0f;
        if (input.isKeyPressed(KeyCode::D)) x += 1.0f;
        if (input.isKeyPressed(KeyCode::W)) y += 1.0f;
        if (input.isKeyPressed(KeyCode::S)) y -= 1.0f;
        m_MoveInput = Math::Vector2(x, y);
        if (m_MoveInput.lengthSquared() > 1.0f) {
            m_MoveInput.normalize();
        }

        if (input.isKeyDown(KeyCode::Space)) {
            m_JumpQueued = true;
        }
    }

    Math::Vector3 computeMoveDirection(Transform* transform) const {
        if (!transform) {
            return Math::Vector3::Zero;
        }
        Math::Vector3 forward = transform->forward();
        Math::Vector3 right = transform->right();
        Math::Vector3 dir = (right * m_MoveInput.x) + (forward * m_MoveInput.y);
        return dir;
    }

    Math::Vector3 moveToward(const Math::Vector3& current,
                             const Math::Vector3& target,
                             float maxDelta) const {
        Math::Vector3 delta = target - current;
        float length = delta.length();
        if (length <= maxDelta || length <= Math::EPSILON) {
            return target;
        }
        return current + delta * (maxDelta / length);
    }

    bool isWalkable(const Math::Vector3& normal) const {
        if (!m_EnableSlopeLimit) {
            return true;
        }
        float cosLimit = std::cos(m_SlopeLimit * Math::DEG_TO_RAD);
        return normal.dot(Math::Vector3::Up) >= cosLimit;
    }

    Math::Vector3 projectOnPlane(const Math::Vector3& v, const Math::Vector3& normal) const {
        return v - normal * v.dot(normal);
    }

    bool probeGround(PhysicsWorld* physics,
                     const Math::Vector3& position,
                     Math::Vector3& outNormal) const {
        if (!physics) {
            return false;
        }
        PhysicsRaycastHit hit;
        float distance = m_GroundCheckDistance + m_SkinWidth;
        bool hasHit = physics->capsuleCast(position, m_Radius, m_Height, Math::Vector3::Down,
                                           distance, hit, m_CollisionMask, false, m_Entity);
        if (hasHit) {
            outNormal = hit.normal;
        }
        return hasHit;
    }

    void applySlopeSlide(const Math::Vector3& normal, float deltaTime) {
        if (m_SlopeSlideSpeed <= 0.0f) {
            return;
        }
        Math::Vector3 down = Math::Vector3::Down;
        Math::Vector3 slideDir = down - normal * down.dot(normal);
        if (slideDir.lengthSquared() <= Math::EPSILON) {
            return;
        }
        slideDir.normalize();
        m_Velocity += slideDir * (m_SlopeSlideSpeed * deltaTime);
    }

    Math::Vector3 moveHorizontal(PhysicsWorld* physics,
                                 const Math::Vector3& position,
                                 const Math::Vector3& move) {
        if (!physics) {
            return position + move;
        }
        float distance = move.length();
        if (distance <= Math::EPSILON) {
            return position;
        }

        Math::Vector3 dir = move / distance;
        PhysicsRaycastHit hit;
        if (!physics->capsuleCast(position, m_Radius, m_Height, dir, distance + m_SkinWidth,
                                  hit, m_CollisionMask, false, m_Entity)) {
            return position + move;
        }

        float travel = std::max(0.0f, hit.distance - m_SkinWidth);
        Math::Vector3 newPos = position + dir * travel;
        float remaining = distance - travel;

        if (m_EnableStep && m_IsGrounded && remaining > 0.0f && hit.normal.y < 0.4f) {
            Math::Vector3 stepped = newPos;
            if (tryStep(physics, newPos, dir, remaining, stepped)) {
                return stepped;
            }
        }

        Math::Vector3 slide = move - dir * travel;
        slide = slide - hit.normal * slide.dot(hit.normal);
        float slideDist = slide.length();
        if (slideDist > Math::EPSILON) {
            Math::Vector3 slideDir = slide / slideDist;
            PhysicsRaycastHit slideHit;
            if (physics->capsuleCast(newPos, m_Radius, m_Height, slideDir, slideDist + m_SkinWidth,
                                     slideHit, m_CollisionMask, false, m_Entity)) {
                float slideTravel = std::max(0.0f, slideHit.distance - m_SkinWidth);
                newPos += slideDir * slideTravel;
            } else {
                newPos += slide;
            }
        }

        return newPos;
    }

    Math::Vector3 moveVertical(PhysicsWorld* physics,
                               const Math::Vector3& position,
                               const Math::Vector3& move) {
        if (!physics) {
            return position + move;
        }
        float distance = std::abs(move.y);
        if (distance <= Math::EPSILON) {
            return position;
        }

        Math::Vector3 dir = (move.y > 0.0f) ? Math::Vector3::Up : Math::Vector3::Down;
        PhysicsRaycastHit hit;
        if (!physics->capsuleCast(position, m_Radius, m_Height, dir, distance + m_SkinWidth,
                                  hit, m_CollisionMask, false, m_Entity)) {
            return position + move;
        }

        float travel = std::max(0.0f, hit.distance - m_SkinWidth);
        Math::Vector3 newPos = position + dir * travel;
        if (dir.y > 0.0f) {
            m_Velocity.y = 0.0f;
        }
        return newPos;
    }

    bool tryStep(PhysicsWorld* physics,
                 const Math::Vector3& position,
                 const Math::Vector3& dir,
                 float distance,
                 Math::Vector3& outPosition) {
        float stepHeight = std::min(m_StepOffset, m_Height * 0.5f);
        if (stepHeight <= Math::EPSILON) {
            return false;
        }

        PhysicsRaycastHit upHit;
        if (physics->capsuleCast(position, m_Radius, m_Height, Math::Vector3::Up,
                                 stepHeight + m_SkinWidth, upHit, m_CollisionMask, false, m_Entity)) {
            return false;
        }

        Math::Vector3 stepPos = position + Math::Vector3::Up * stepHeight;
        PhysicsRaycastHit forwardHit;
        if (physics->capsuleCast(stepPos, m_Radius, m_Height, dir, distance + m_SkinWidth,
                                 forwardHit, m_CollisionMask, false, m_Entity)) {
            float travel = std::max(0.0f, forwardHit.distance - m_SkinWidth);
            stepPos += dir * travel;
            if (travel <= Math::EPSILON) {
                return false;
            }
        } else {
            stepPos += dir * distance;
        }

        PhysicsRaycastHit downHit;
        float downDist = stepHeight + m_GroundCheckDistance + m_SkinWidth;
        if (physics->capsuleCast(stepPos, m_Radius, m_Height, Math::Vector3::Down,
                                 downDist, downHit, m_CollisionMask, false, m_Entity)) {
            if (!isWalkable(downHit.normal)) {
                return false;
            }
            float descend = std::max(0.0f, downHit.distance - m_SkinWidth);
            stepPos += Math::Vector3::Down * descend;
            outPosition = stepPos;
            m_IsGrounded = true;
            m_GroundNormal = downHit.normal;
            return true;
        }

        return false;
    }

    void ensureRigidbodyCompatibility() {
        if (!m_Entity) {
            return;
        }
        Rigidbody* rb = m_Entity->getComponent<Rigidbody>();
        if (!rb) {
            return;
        }
        if (rb->getType() != RigidbodyType::Kinematic) {
            rb->setType(RigidbodyType::Kinematic);
        }
        if (rb->getUseGravity()) {
            rb->setUseGravity(false);
        }
    }

private:
    float m_Radius;
    float m_Height;
    float m_SkinWidth;
    float m_MoveSpeed;
    float m_Acceleration;
    float m_AirAcceleration;
    float m_JumpSpeed;
    float m_Gravity;
    float m_MaxFallSpeed;
    float m_GroundSnapSpeed;
    float m_StepOffset;
    float m_SlopeLimit;
    float m_SlopeSlideSpeed;
    float m_GroundCheckDistance;
    bool m_UseInput;
    bool m_UseGravity;
    bool m_EnableStep;
    bool m_EnableSlopeLimit;
    bool m_SnapToGround;
    uint32_t m_CollisionMask;

    bool m_IsGrounded = false;
    Math::Vector3 m_GroundNormal = Math::Vector3::Up;
    Math::Vector3 m_Velocity = Math::Vector3::Zero;
    Math::Vector2 m_MoveInput = Math::Vector2::Zero;
    bool m_JumpQueued = false;
};

} // namespace Crescent
