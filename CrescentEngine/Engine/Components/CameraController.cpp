#include "CameraController.hpp"
#include "../ECS/Entity.hpp"
#include "../ECS/Transform.hpp"
#include "../Input/InputManager.hpp"
#include <iostream>

namespace Crescent {

CameraController::CameraController()
    : m_MoveSpeed(10.0f)
    , m_RotationSpeed(90.0f)
    , m_Yaw(0.0f)
    , m_Pitch(0.0f) {
}

void CameraController::moveForward(float amount) {
    if (!m_Entity) return;
    
    Transform* transform = m_Entity->getTransform();
    Math::Vector3 forward = transform->forward();
    transform->translate(forward * amount * m_MoveSpeed, false);
}

void CameraController::moveRight(float amount) {
    if (!m_Entity) return;
    
    Transform* transform = m_Entity->getTransform();
    Math::Vector3 right = transform->right();
    transform->translate(right * amount * m_MoveSpeed, false);
}

void CameraController::moveUp(float amount) {
    if (!m_Entity) return;
    
    Transform* transform = m_Entity->getTransform();
    Math::Vector3 up = Math::Vector3::Up;
    transform->translate(up * amount * m_MoveSpeed, false);
}

void CameraController::rotateYaw(float amount) {
    m_Yaw += amount * m_RotationSpeed * Math::DEG_TO_RAD;
    updateRotation();
}

void CameraController::rotatePitch(float amount) {
    m_Pitch += amount * m_RotationSpeed * Math::DEG_TO_RAD;
    
    const float maxPitch = 89.0f * Math::DEG_TO_RAD;
    if (m_Pitch > maxPitch) m_Pitch = maxPitch;
    if (m_Pitch < -maxPitch) m_Pitch = -maxPitch;
    
    updateRotation();
}

void CameraController::updateRotation() {
    if (!m_Entity) return;
    
    Transform* transform = m_Entity->getTransform();
    
    // Unity/Unreal style FPS camera:
    // 1. Apply yaw around world Y axis
    // 2. Apply pitch around LOCAL X axis (after yaw)
    
    Math::Quaternion yawQuat = Math::Quaternion::FromAxisAngle(Math::Vector3::Up, m_Yaw);
    
    // After yaw rotation, get the local right vector
    Math::Vector3 localRight = yawQuat * Math::Vector3::Right;
    
    // Apply pitch around local right
    Math::Quaternion pitchQuat = Math::Quaternion::FromAxisAngle(localRight, m_Pitch);
    
    // Final rotation: pitch * yaw (order matters!)
    Math::Quaternion finalRotation = pitchQuat * yawQuat;
    
    transform->setRotation(finalRotation);
}

void CameraController::OnCreate() {
    if (m_Entity) {
        // Initialize yaw/pitch from current transform
        Transform* transform = m_Entity->getTransform();
        Math::Vector3 forward = transform->forward();
        
        // Calculate yaw from forward vector (xz plane projection)
        m_Yaw = std::atan2(forward.x, -forward.z);
        
        // Calculate pitch from forward vector
        float horizontalLength = std::sqrt(forward.x * forward.x + forward.z * forward.z);
        m_Pitch = std::atan2(-forward.y, horizontalLength);
        
        std::cout << "FPS Camera Controller initialized" << std::endl;
        std::cout << "  Initial Yaw: " << (m_Yaw * Math::RAD_TO_DEG) << " deg" << std::endl;
        std::cout << "  Initial Pitch: " << (m_Pitch * Math::RAD_TO_DEG) << " deg" << std::endl;
        std::cout << "  HOLD RIGHT-CLICK and move mouse to look around" << std::endl;
    }
}

void CameraController::OnDestroy() {
}

void CameraController::OnUpdate(float deltaTime) {
    if (!m_Entity) return;
    
    InputManager& input = InputManager::getInstance();
    
    // Sprint
    float currentSpeed = m_MoveSpeed;
    if (input.isKeyPressed(KeyCode::Shift)) {
        currentSpeed *= 2.0f;
    }
    
    Transform* transform = m_Entity->getTransform();
    
    // Movement (WASD)
    if (input.isKeyPressed(KeyCode::W)) {
        Math::Vector3 forward = transform->forward();
        transform->translate(forward * currentSpeed * deltaTime, false);
    }
    if (input.isKeyPressed(KeyCode::S)) {
        Math::Vector3 forward = transform->forward();
        transform->translate(forward * -currentSpeed * deltaTime, false);
    }
    if (input.isKeyPressed(KeyCode::A)) {
        Math::Vector3 right = transform->right();
        transform->translate(right * -currentSpeed * deltaTime, false);
    }
    if (input.isKeyPressed(KeyCode::D)) {
        Math::Vector3 right = transform->right();
        transform->translate(right * currentSpeed * deltaTime, false);
    }
    
    // Up/Down (QE)
    if (input.isKeyPressed(KeyCode::E)) {
        transform->translate(Math::Vector3::Up * currentSpeed * deltaTime, false);
    }
    if (input.isKeyPressed(KeyCode::Q)) {
        transform->translate(Math::Vector3::Up * -currentSpeed * deltaTime, false);
    }
    
    // Mouse look - ONLY when right mouse button is held
    bool rightMouseHeld = input.isMouseButtonPressed(MouseButton::Right);
    
    if (rightMouseHeld) {
        Math::Vector2 mouseDelta = input.getMouseDelta();
        
        if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f) {
            const float mouseSensitivity = 0.002f;
            
            // Update yaw and pitch
            m_Yaw -= mouseDelta.x * mouseSensitivity;  // Inverted X
            m_Pitch -= mouseDelta.y * mouseSensitivity; // Inverted Y
            
            // Clamp pitch
            const float maxPitch = 89.0f * Math::DEG_TO_RAD;
            m_Pitch = Math::Clamp(m_Pitch, -maxPitch, maxPitch);
            
            // Update rotation
            updateRotation();
        }
    }
}

} // namespace Crescent
