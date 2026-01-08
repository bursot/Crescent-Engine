#pragma once

#include "../ECS/Component.hpp"
#include "../ECS/Entity.hpp"
#include "../ECS/Transform.hpp"
#include "../Input/InputManager.hpp"
#include "../Math/Math.hpp"
#include "../Scene/Scene.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Physics/PhysicsWorld.hpp"
#include "../Components/CharacterController.hpp"
#include "../Components/Camera.hpp"
#include <algorithm>
#include <cmath>

namespace Crescent {

class FirstPersonController : public Component {
public:
    COMPONENT_TYPE(FirstPersonController)

    FirstPersonController()
        : m_MouseSensitivity(0.002f)
        , m_InvertY(false)
        , m_RequireLookButton(true)
        , m_LookButton(MouseButton::Right)
        , m_MinPitch(-89.0f)
        , m_MaxPitch(89.0f)
        , m_WalkSpeed(5.0f)
        , m_SprintMultiplier(1.6f)
        , m_EnableSprint(true)
        , m_EnableCrouch(true)
        , m_CrouchHeight(1.2f)
        , m_CrouchEyeHeight(1.0f)
        , m_CrouchSpeed(6.0f)
        , m_EyeHeight(1.6f)
        , m_UseEyeHeight(true)
        , m_DriveCharacterController(true) {
    }

    float getMouseSensitivity() const { return m_MouseSensitivity; }
    void setMouseSensitivity(float value) { m_MouseSensitivity = std::max(0.0f, value); }

    bool getInvertY() const { return m_InvertY; }
    void setInvertY(bool value) { m_InvertY = value; }

    bool getRequireLookButton() const { return m_RequireLookButton; }
    void setRequireLookButton(bool value) { m_RequireLookButton = value; }

    MouseButton getLookButton() const { return m_LookButton; }
    void setLookButton(MouseButton button) { m_LookButton = button; }

    float getMinPitch() const { return m_MinPitch; }
    void setMinPitch(float value) { m_MinPitch = value; }

    float getMaxPitch() const { return m_MaxPitch; }
    void setMaxPitch(float value) { m_MaxPitch = value; }

    float getWalkSpeed() const { return m_WalkSpeed; }
    void setWalkSpeed(float value) { m_WalkSpeed = std::max(0.0f, value); }

    float getSprintMultiplier() const { return m_SprintMultiplier; }
    void setSprintMultiplier(float value) { m_SprintMultiplier = std::max(1.0f, value); }

    bool getEnableSprint() const { return m_EnableSprint; }
    void setEnableSprint(bool value) { m_EnableSprint = value; }

    bool getEnableCrouch() const { return m_EnableCrouch; }
    void setEnableCrouch(bool value) { m_EnableCrouch = value; }

    float getCrouchHeight() const { return m_CrouchHeight; }
    void setCrouchHeight(float value) { m_CrouchHeight = std::max(0.2f, value); }

    float getCrouchEyeHeight() const { return m_CrouchEyeHeight; }
    void setCrouchEyeHeight(float value) { m_CrouchEyeHeight = std::max(0.0f, value); }

    float getCrouchSpeed() const { return m_CrouchSpeed; }
    void setCrouchSpeed(float value) { m_CrouchSpeed = std::max(0.0f, value); }

    float getEyeHeight() const { return m_EyeHeight; }
    void setEyeHeight(float value) { m_EyeHeight = std::max(0.0f, value); }

    bool getUseEyeHeight() const { return m_UseEyeHeight; }
    void setUseEyeHeight(bool value) { m_UseEyeHeight = value; }

    bool getDriveCharacterController() const { return m_DriveCharacterController; }
    void setDriveCharacterController(bool value) { m_DriveCharacterController = value; }

    void OnCreate() override {
        findDependencies();
        initializeLook();
        if (m_Controller) {
            m_Controller->setUseInput(false);
            m_StandingHeight = m_Controller->getHeight();
            m_CurrentHeight = m_StandingHeight;
            m_HeightsInitialized = true;
        }
        m_CurrentEyeHeight = m_EyeHeight;
        if (m_CameraTransform && m_CameraTransform != m_BodyTransform && m_UseEyeHeight) {
            m_CameraTransform->setLocalPosition(Math::Vector3(0.0f, m_CurrentEyeHeight, 0.0f));
        }
        if (m_Controller && m_DriveCharacterController) {
            m_Controller->setEnabled(false);
        }
    }

    void OnDestroy() override {
        if (m_Controller && m_DriveCharacterController) {
            m_Controller->setEnabled(true);
        }
    }

    void OnUpdate(float deltaTime) override {
        if (!m_Entity || deltaTime <= 0.0f) {
            return;
        }
        if (SceneManager::getInstance().getViewMode() != SceneManager::ViewMode::Game) {
            return;
        }

        if (!m_CameraTransform || !m_BodyTransform || !m_Controller) {
            findDependencies();
            if (!m_CameraTransform || !m_BodyTransform) {
                return;
            }
        }

        if (m_Controller) {
            if (m_DriveCharacterController && m_Controller->isEnabled()) {
                m_Controller->setEnabled(false);
            } else if (!m_DriveCharacterController && !m_Controller->isEnabled()) {
                m_Controller->setEnabled(true);
            }
        }

        InputManager& input = InputManager::getInstance();
        bool lookAllowed = !m_RequireLookButton || input.isMouseButtonPressed(m_LookButton);
        if (lookAllowed) {
            Math::Vector2 mouseDelta = input.getMouseDelta();
            float yawDelta = mouseDelta.x * m_MouseSensitivity;
            float pitchDelta = mouseDelta.y * m_MouseSensitivity * (m_InvertY ? -1.0f : 1.0f);

            m_Yaw -= yawDelta;
            m_Pitch -= pitchDelta;
            m_Pitch = Math::Clamp(m_Pitch, m_MinPitch * Math::DEG_TO_RAD, m_MaxPitch * Math::DEG_TO_RAD);
        }

        applyRotation();
        applyCameraHeight(deltaTime, input);
        driveCharacter(deltaTime, input);
    }

private:
    void findDependencies() {
        m_Controller = nullptr;
        m_BodyTransform = nullptr;
        m_CameraTransform = nullptr;

        if (!m_Entity) {
            return;
        }

        Entity* bodyEntity = resolveBodyEntity();
        if (bodyEntity) {
            m_Controller = bodyEntity->getComponent<CharacterController>();
            m_BodyTransform = bodyEntity->getTransform();
        } else {
            m_BodyTransform = m_Entity->getTransform();
        }

        if (!m_BodyTransform) {
            return;
        }

        if (m_Controller && !m_HeightsInitialized) {
            m_Controller->setUseInput(false);
            m_StandingHeight = m_Controller->getHeight();
            m_CurrentHeight = m_StandingHeight;
            m_CurrentEyeHeight = m_EyeHeight;
            m_HeightsInitialized = true;
        }

        if (m_Entity->getComponent<Camera>()) {
            m_CameraTransform = m_Entity->getTransform();
            return;
        }

        m_CameraTransform = findCameraTransform(m_BodyTransform);
    }

    Entity* resolveBodyEntity() const {
        Entity* current = m_Entity;
        Transform* cursor = m_Entity ? m_Entity->getTransform() : nullptr;
        while (current) {
            if (current->getComponent<CharacterController>()) {
                return current;
            }
            if (!cursor) {
                break;
            }
            Transform* parent = cursor->getParent();
            if (!parent) {
                break;
            }
            cursor = parent;
            current = parent->getEntity();
        }
        return nullptr;
    }

    Transform* findCameraTransform(Transform* root) const {
        if (!root) {
            return nullptr;
        }
        std::vector<Transform*> stack;
        stack.reserve(8);
        stack.push_back(root);
        while (!stack.empty()) {
            Transform* node = stack.back();
            stack.pop_back();
            if (!node) {
                continue;
            }
            if (node->getEntity() && node->getEntity()->getComponent<Camera>()) {
                return node;
            }
            const auto& children = node->getChildren();
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                stack.push_back(*it);
            }
        }
        return nullptr;
    }

    void initializeLook() {
        if (!m_BodyTransform) {
            return;
        }
        Math::Vector3 forward = m_BodyTransform->forward();
        m_Yaw = std::atan2(forward.x, -forward.z);
        float horizontalLength = std::sqrt(forward.x * forward.x + forward.z * forward.z);
        m_Pitch = std::atan2(-forward.y, horizontalLength);
    }

    void applyRotation() {
        if (!m_BodyTransform || !m_CameraTransform) {
            return;
        }

        Math::Quaternion yawQuat = Math::Quaternion::FromAxisAngle(Math::Vector3::Up, m_Yaw);
        if (m_CameraTransform == m_BodyTransform) {
            Math::Quaternion pitchQuat = Math::Quaternion::FromAxisAngle(Math::Vector3::Right, m_Pitch);
            m_BodyTransform->setRotation(pitchQuat * yawQuat);
            return;
        }

        m_BodyTransform->setRotation(yawQuat);
        Math::Quaternion pitchQuat = Math::Quaternion::FromAxisAngle(Math::Vector3::Right, m_Pitch);
        m_CameraTransform->setLocalRotation(pitchQuat);
    }

    void applyCameraHeight(float deltaTime, InputManager& input) {
        if (!m_Controller || !m_CameraTransform || !m_UseEyeHeight) {
            return;
        }

        bool crouch = m_EnableCrouch && input.isKeyPressed(KeyCode::Control);
        float targetHeight = crouch ? m_CrouchHeight : m_StandingHeight;
        float targetEye = crouch ? m_CrouchEyeHeight : m_EyeHeight;

        float nextHeight = Math::MoveTowards(m_CurrentHeight, targetHeight, m_CrouchSpeed * deltaTime);
        float nextEye = Math::MoveTowards(m_CurrentEyeHeight, targetEye, m_CrouchSpeed * deltaTime);

        if (m_CurrentHeight != nextHeight) {
            m_CurrentHeight = nextHeight;
            m_Controller->setHeight(m_CurrentHeight);
        }
        if (m_CurrentEyeHeight != nextEye && m_CameraTransform != m_BodyTransform) {
            m_CurrentEyeHeight = nextEye;
            m_CameraTransform->setLocalPosition(Math::Vector3(0.0f, m_CurrentEyeHeight, 0.0f));
        }
    }

    void driveCharacter(float deltaTime, InputManager& input) {
        if (!m_Controller) {
            return;
        }

        float speed = m_WalkSpeed;
        if (m_EnableSprint && input.isKeyPressed(KeyCode::Shift)) {
            speed *= m_SprintMultiplier;
        }
        m_Controller->setMoveSpeed(speed);

        float x = 0.0f;
        float y = 0.0f;
        if (input.isKeyPressed(KeyCode::A)) x -= 1.0f;
        if (input.isKeyPressed(KeyCode::D)) x += 1.0f;
        if (input.isKeyPressed(KeyCode::W)) y += 1.0f;
        if (input.isKeyPressed(KeyCode::S)) y -= 1.0f;

        Math::Vector2 move(x, y);
        if (move.lengthSquared() > 1.0f) {
            move.normalize();
        }

        m_Controller->setMoveInput(move);
        if (input.isKeyDown(KeyCode::Space)) {
            m_Controller->requestJump();
        }

        if (m_DriveCharacterController) {
            m_Controller->OnUpdate(deltaTime);
        }
    }

private:
    float m_MouseSensitivity;
    bool m_InvertY;
    bool m_RequireLookButton;
    MouseButton m_LookButton;
    float m_MinPitch;
    float m_MaxPitch;
    float m_WalkSpeed;
    float m_SprintMultiplier;
    bool m_EnableSprint;
    bool m_EnableCrouch;
    float m_CrouchHeight;
    float m_CrouchEyeHeight;
    float m_CrouchSpeed;
    float m_EyeHeight;
    bool m_UseEyeHeight;
    bool m_DriveCharacterController;

    CharacterController* m_Controller = nullptr;
    Transform* m_BodyTransform = nullptr;
    Transform* m_CameraTransform = nullptr;

    float m_Yaw = 0.0f;
    float m_Pitch = 0.0f;
    float m_StandingHeight = 2.0f;
    float m_CurrentHeight = 2.0f;
    float m_CurrentEyeHeight = 1.6f;
    bool m_HeightsInitialized = false;
};

} // namespace Crescent
