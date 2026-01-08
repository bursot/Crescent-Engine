#include "Camera.hpp"
#include "../ECS/Entity.hpp"
#include "../ECS/Transform.hpp"

namespace Crescent {

Camera* Camera::s_MainCamera = nullptr;

Camera::Camera()
    : m_ProjectionType(ProjectionType::Perspective)
    , m_FieldOfView(Math::PI / 3.0f)  // 60 degrees
    , m_OrthographicSize(5.0f)
    , m_NearClip(0.1f)
    , m_FarClip(1000.0f)
    , m_AspectRatio(16.0f / 9.0f)
    , m_Viewport(0.0f, 0.0f, 1.0f, 1.0f)
    , m_ClearColor(0.1f, 0.1f, 0.15f, 1.0f)
    , m_ClearDepth(true)
    , m_IsEditorCamera(false)
    , m_ProjectionDirty(true) {
}

void Camera::setProjectionType(ProjectionType type) {
    if (m_ProjectionType != type) {
        m_ProjectionType = type;
        m_ProjectionDirty = true;
    }
}

void Camera::setFieldOfView(float fov) {
    if (m_FieldOfView != fov) {
        m_FieldOfView = fov;
        m_ProjectionDirty = true;
    }
}

void Camera::setOrthographicSize(float size) {
    if (m_OrthographicSize != size) {
        m_OrthographicSize = size;
        m_ProjectionDirty = true;
    }
}

void Camera::setNearClip(float nearClip) {
    if (m_NearClip != nearClip) {
        m_NearClip = nearClip;
        m_ProjectionDirty = true;
    }
}

void Camera::setFarClip(float farClip) {
    if (m_FarClip != farClip) {
        m_FarClip = farClip;
        m_ProjectionDirty = true;
    }
}

void Camera::setAspectRatio(float aspect) {
    if (m_AspectRatio != aspect) {
        m_AspectRatio = aspect;
        m_ProjectionDirty = true;
    }
}

void Camera::setViewport(const Math::Vector4& viewport) {
    m_Viewport = viewport;
}

Math::Matrix4x4 Camera::getProjectionMatrix() const {
    if (m_ProjectionDirty) {
        const_cast<Camera*>(this)->updateProjectionMatrix();
    }
    return m_ProjectionMatrix;
}

void Camera::updateProjectionMatrix() {
    if (m_ProjectionType == ProjectionType::Perspective) {
        m_ProjectionMatrix = Math::Matrix4x4::Perspective(
            m_FieldOfView,
            m_AspectRatio,
            m_NearClip,
            m_FarClip
        );
    } else {
        float height = m_OrthographicSize;
        float width = height * m_AspectRatio;
        m_ProjectionMatrix = Math::Matrix4x4::Orthographic(
            -width, width,
            -height, height,
            m_NearClip, m_FarClip
        );
    }
    m_ProjectionDirty = false;
}

Math::Matrix4x4 Camera::getViewMatrix() const {
    if (!m_Entity) {
        return Math::Matrix4x4::Identity;
    }
    
    Transform* transform = m_Entity->getTransform();
    Math::Vector3 position = transform->getPosition();
    Math::Vector3 forward = transform->forward();
    Math::Vector3 up = transform->up();
    
    return Math::Matrix4x4::LookAt(position, position + forward, up);
}

Math::Matrix4x4 Camera::getViewProjectionMatrix() const {
    return getProjectionMatrix() * getViewMatrix();
}

Math::Vector3 Camera::worldToScreenPoint(const Math::Vector3& worldPoint) const {
    Math::Vector4 clipSpace = getViewProjectionMatrix() * Math::Vector4(worldPoint.x, worldPoint.y, worldPoint.z, 1.0f);
    
    if (clipSpace.w != 0.0f) {
        clipSpace.x /= clipSpace.w;
        clipSpace.y /= clipSpace.w;
        clipSpace.z /= clipSpace.w;
    }
    
    // Convert to screen space (0-1)
    Math::Vector3 screenPoint;
    screenPoint.x = (clipSpace.x + 1.0f) * 0.5f;
    screenPoint.y = (1.0f - clipSpace.y) * 0.5f;
    screenPoint.z = clipSpace.z;
    
    return screenPoint;
}

Math::Vector3 Camera::screenToWorldPoint(const Math::Vector3& screenPoint) const {
    // Convert from screen space (0-1) to clip space (-1 to 1)
    Math::Vector4 clipSpace;
    clipSpace.x = screenPoint.x * 2.0f - 1.0f;
    clipSpace.y = (1.0f - screenPoint.y) * 2.0f - 1.0f;
    clipSpace.z = screenPoint.z;
    clipSpace.w = 1.0f;
    
    // Transform to world space
    Math::Matrix4x4 invViewProj = getViewProjectionMatrix().inversed();
    Math::Vector4 worldSpace = invViewProj * clipSpace;
    
    if (worldSpace.w != 0.0f) {
        return Math::Vector3(
            worldSpace.x / worldSpace.w,
            worldSpace.y / worldSpace.w,
            worldSpace.z / worldSpace.w
        );
    }
    
    return Math::Vector3::Zero;
}

void Camera::OnCreate() {
    // If no main camera is set, make this the main camera
    if (s_MainCamera == nullptr) {
        s_MainCamera = this;
    }
}

void Camera::OnDestroy() {
    // If this was the main camera, clear it
    if (s_MainCamera == this) {
        s_MainCamera = nullptr;
    }
}

} // namespace Crescent
