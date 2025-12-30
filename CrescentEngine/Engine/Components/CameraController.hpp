#pragma once

#include "../ECS/Component.hpp"
#include "../Math/Math.hpp"

namespace Crescent {

class CameraController : public Component {
public:
    COMPONENT_TYPE(CameraController)
    
    CameraController();
    
    // Movement speeds
    void setMoveSpeed(float speed) { m_MoveSpeed = speed; }
    void setRotationSpeed(float speed) { m_RotationSpeed = speed; }
    
    float getMoveSpeed() const { return m_MoveSpeed; }
    float getRotationSpeed() const { return m_RotationSpeed; }
    
    // Manual control methods (call from main loop with key states)
    void moveForward(float amount);
    void moveRight(float amount);
    void moveUp(float amount);
    void rotateYaw(float amount);   // Y axis rotation
    void rotatePitch(float amount);  // X axis rotation
    
    // Component lifecycle
    void OnCreate() override;
    void OnDestroy() override;
    void OnUpdate(float deltaTime) override;
    
private:
    float m_MoveSpeed;
    float m_RotationSpeed;
    float m_Yaw;   // Y axis rotation
    float m_Pitch; // X axis rotation
    
    void updateRotation();
};

} // namespace Crescent
