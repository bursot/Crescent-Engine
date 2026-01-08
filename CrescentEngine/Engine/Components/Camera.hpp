#pragma once

#include "../ECS/Component.hpp"
#include "../Math/Math.hpp"

namespace Crescent {

// Camera component
class Camera : public Component {
public:
    enum class ProjectionType {
        Perspective,
        Orthographic
    };
    
    Camera();
    virtual ~Camera() = default;
    
    COMPONENT_TYPE(Camera)
    
    // Projection
    ProjectionType getProjectionType() const { return m_ProjectionType; }
    void setProjectionType(ProjectionType type);
    
    // Perspective settings
    float getFieldOfView() const { return m_FieldOfView; }
    void setFieldOfView(float fov);
    
    // Orthographic settings
    float getOrthographicSize() const { return m_OrthographicSize; }
    void setOrthographicSize(float size);
    
    // Common settings
    float getNearClip() const { return m_NearClip; }
    void setNearClip(float nearClip);
    
    float getFarClip() const { return m_FarClip; }
    void setFarClip(float farClip);
    
    float getAspectRatio() const { return m_AspectRatio; }
    void setAspectRatio(float aspect);
    
    // Viewport
    Math::Vector4 getViewport() const { return m_Viewport; }
    void setViewport(const Math::Vector4& viewport);
    
    // Clear settings
    const Math::Vector4& getClearColor() const { return m_ClearColor; }
    void setClearColor(const Math::Vector4& color) { m_ClearColor = color; }
    
    bool getClearDepth() const { return m_ClearDepth; }
    void setClearDepth(bool clear) { m_ClearDepth = clear; }

    bool isEditorCamera() const { return m_IsEditorCamera; }
    void setEditorCamera(bool editorCamera) { m_IsEditorCamera = editorCamera; }
    
    // Matrices
    Math::Matrix4x4 getProjectionMatrix() const;
    Math::Matrix4x4 getViewMatrix() const;
    Math::Matrix4x4 getViewProjectionMatrix() const;
    
    // World to screen
    Math::Vector3 worldToScreenPoint(const Math::Vector3& worldPoint) const;
    Math::Vector3 screenToWorldPoint(const Math::Vector3& screenPoint) const;
    
    // Main camera
    static Camera* getMainCamera() { return s_MainCamera; }
    static void setMainCamera(Camera* camera) { s_MainCamera = camera; }
    
    void OnCreate() override;
    void OnDestroy() override;
    
private:
    void updateProjectionMatrix();
    
private:
    ProjectionType m_ProjectionType;
    
    // Perspective
    float m_FieldOfView;  // In radians
    
    // Orthographic
    float m_OrthographicSize;
    
    // Common
    float m_NearClip;
    float m_FarClip;
    float m_AspectRatio;
    
    // Viewport (x, y, width, height) - normalized 0-1
    Math::Vector4 m_Viewport;
    
    // Clear settings
    Math::Vector4 m_ClearColor;
    bool m_ClearDepth;
    bool m_IsEditorCamera;
    
    // Cached matrices
    mutable Math::Matrix4x4 m_ProjectionMatrix;
    mutable bool m_ProjectionDirty;
    
    // Main camera
    static Camera* s_MainCamera;
};

} // namespace Crescent
