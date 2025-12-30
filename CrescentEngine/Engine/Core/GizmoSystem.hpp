#pragma once

#include "../Math/Math.hpp"
#include "SelectionSystem.hpp"  // For Ray struct
#include <memory>

namespace Crescent {

// Forward declarations
class Entity;
class Camera;
class DebugRenderer;
class MeshRenderer;

// Gizmo modes
enum class GizmoMode {
    Translate,  // Move arrows
    Rotate,     // Rotation circles
    Scale       // Scale boxes
};

// Gizmo coordinate space
enum class GizmoSpace {
    World,      // World-space axes
    Local       // Object-space axes
};

// Gizmo axis
enum class GizmoAxis {
    None,
    X,
    Y,
    Z,
    XY,   // Plane
    YZ,   // Plane
    XZ,   // Plane
    XYZ   // Center (uniform scale)
};

// Gizmo interaction state
struct GizmoState {
    bool isActive;
    GizmoAxis activeAxis;
    Math::Vector3 dragStartPosition;
    Math::Vector3 dragStartValue;  // Original transform value
    Math::Vector2 mouseStartPosition;
    
    GizmoState() 
        : isActive(false)
        , activeAxis(GizmoAxis::None)
        , dragStartPosition(Math::Vector3::Zero)
        , dragStartValue(Math::Vector3::Zero)
        , mouseStartPosition(Math::Vector2::Zero) {}
};

// Gizmo rendering and interaction system
class GizmoSystem {
public:
    GizmoSystem();
    ~GizmoSystem();
    
    // Initialize gizmo system
    void initialize(DebugRenderer* debugRenderer);
    
    // Set gizmo mode and space
    void setMode(GizmoMode mode) { m_mode = mode; }
    GizmoMode getMode() const { return m_mode; }
    
    void setSpace(GizmoSpace space) { m_space = space; }
    GizmoSpace getSpace() const { return m_space; }
    
    // Enable/disable gizmos
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }
    
    // Gizmo size in screen space (pixels)
    void setGizmoSize(float size) { m_gizmoSize = size; }
    
    // Snap settings
    void setTranslateSnap(float snap) { m_translateSnap = snap; }
    void setRotateSnap(float snap) { m_rotateSnapDegrees = snap; }
    void setScaleSnap(float snap) { m_scaleSnap = snap; }
    
    void setSnapEnabled(bool enabled) { m_snapEnabled = enabled; }
    bool isSnapEnabled() const { return m_snapEnabled; }
    
    // Draw gizmo for entity
    void drawGizmo(Entity* entity, Camera* camera, const Math::Vector2& screenSize);
    
    // Handle mouse input
    void handleMouseDown(const Math::Vector2& mousePos, const Math::Vector2& screenSize, 
                        Entity* entity, Camera* camera);
    void handleMouseDrag(const Math::Vector2& mousePos, const Math::Vector2& screenSize,
                        Entity* entity, Camera* camera);
    void handleMouseUp();
    
    // Check if gizmo is being manipulated
    bool isManipulating() const { return m_state.isActive; }
    
    // Draw selection wireframe
    void drawSelectionBox(Entity* entity);
    void hideGizmoMesh();
    
private:
    // Draw methods for each mode
    void drawTranslateGizmo(const Math::Vector3& position, const Math::Quaternion& orientation,
                           float scale, Camera* camera);
    void drawRotateGizmo(const Math::Vector3& position, const Math::Quaternion& orientation,
                        float scale, Camera* camera);
    void drawScaleGizmo(const Math::Vector3& position, const Math::Quaternion& orientation,
                       float scale, Camera* camera);
    
    // Draw individual gizmo components
    void drawArrow(const Math::Vector3& origin, const Math::Vector3& direction, 
                  float length, const Math::Vector4& color, bool highlight);
    void drawCircle(const Math::Vector3& center, const Math::Vector3& normal,
                   float radius, const Math::Vector4& color, bool highlight);
    void drawScaleBox(const Math::Vector3& position, const Math::Vector3& direction,
                     float size, const Math::Vector4& color, bool highlight);
    
    // Hit testing
    GizmoAxis hitTestTranslateGizmo(const Ray& ray, const Math::Vector3& position,
                                   const Math::Quaternion& orientation, float scale) const;
    GizmoAxis hitTestRotateGizmo(const Ray& ray, const Math::Vector3& position,
                                const Math::Quaternion& orientation, float scale) const;
    GizmoAxis hitTestScaleGizmo(const Ray& ray, const Math::Vector3& position,
                               const Math::Quaternion& orientation, float scale) const;
    
    // Helper: Ray-line distance
    float rayToLineDistance(const Ray& ray, const Math::Vector3& lineStart,
                           const Math::Vector3& lineEnd) const;
    
    // Helper: Ray-circle distance
    float rayToCircleDistance(const Ray& ray, const Math::Vector3& center,
                             const Math::Vector3& normal, float radius) const;
    
    // Calculate gizmo world-space scale (to keep constant screen size)
    float calculateGizmoScale(const Math::Vector3& gizmoPosition, Camera* camera,
                             const Math::Vector2& screenSize) const;
    
    // Draw mesh wireframe for selection
    void drawMeshWire(Entity* entity, MeshRenderer* meshRenderer, const Math::Vector4& color);
    
private:
    DebugRenderer* m_debugRenderer;
    
    GizmoMode m_mode;
    GizmoSpace m_space;
    bool m_enabled;
    
    float m_gizmoSize;  // Pixels
    
    // Snapping
    bool m_snapEnabled;
    float m_translateSnap;
    float m_rotateSnapDegrees;
    float m_scaleSnap;
    
    // Interaction state
    GizmoState m_state;
    
    // Colors
    Math::Vector4 m_colorX;
    Math::Vector4 m_colorY;
    Math::Vector4 m_colorZ;
    Math::Vector4 m_colorHighlight;
    
    // Constants
    static constexpr float ARROW_LENGTH = 1.5f;
    static constexpr float ARROW_HEAD_SIZE = 0.2f;
    static constexpr float CIRCLE_RADIUS = 1.2f;
    static constexpr float SCALE_BOX_SIZE = 0.15f;
    static constexpr float HIT_THRESHOLD = 0.15f;  // Hit detection threshold
};

} // namespace Crescent
