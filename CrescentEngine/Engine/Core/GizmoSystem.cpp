#include "GizmoSystem.hpp"
#include "SelectionSystem.hpp"
#include "../Renderer/DebugRenderer.hpp"
#include "../Components/Camera.hpp"
#include "../Components/MeshRenderer.hpp"
#include "../Rendering/Mesh.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Scene/Scene.hpp"
#include "../ECS/Entity.hpp"
#include "../ECS/Transform.hpp"
#include <limits>

namespace Crescent {

namespace {

constexpr float kTranslatePlaneInner = 0.34f;
constexpr float kTranslatePlaneOuter = 0.56f;
constexpr float kRotateOuterRingScale = 1.45f;
constexpr float kCenterSphereScale = 0.08f;
constexpr float kScaleStemLength = 1.35f;
constexpr float kPlaneHitPadding = 0.05f;
constexpr float kRotateHitBand = 0.2f;

Math::Vector3 safeNormalized(const Math::Vector3& value, const Math::Vector3& fallback) {
    return value.lengthSquared() > 1e-6f ? value.normalized() : fallback;
}

bool intersectRayPlane(const Ray& ray,
                       const Math::Vector3& planePoint,
                       const Math::Vector3& planeNormal,
                       Math::Vector3& intersection) {
    float denominator = ray.direction.dot(planeNormal);
    if (std::abs(denominator) < 1e-6f) {
        return false;
    }

    float t = (planePoint - ray.origin).dot(planeNormal) / denominator;
    if (t < 0.0f) {
        return false;
    }

    intersection = ray.getPoint(t);
    return true;
}

Math::Vector3 getCameraForward(Camera* camera) {
    if (!camera || !camera->getEntity()) {
        return Math::Vector3::Forward;
    }

    return safeNormalized(camera->getEntity()->getTransform()->forward(), Math::Vector3::Forward);
}

Math::Vector3 getCameraUp(Camera* camera) {
    if (!camera || !camera->getEntity()) {
        return Math::Vector3::Up;
    }

    return safeNormalized(camera->getEntity()->getTransform()->up(), Math::Vector3::Up);
}

Math::Vector3 getAxisDragPlaneNormal(const Math::Vector3& axis, Camera* camera) {
    Math::Vector3 cameraForward = getCameraForward(camera);
    Math::Vector3 planeNormal = axis.cross(cameraForward).cross(axis);

    if (planeNormal.lengthSquared() < 1e-6f) {
        planeNormal = axis.cross(getCameraUp(camera)).cross(axis);
    }

    if (planeNormal.lengthSquared() < 1e-6f) {
        planeNormal = axis.cross(Math::Vector3::Right).cross(axis);
    }

    if (planeNormal.lengthSquared() < 1e-6f) {
        planeNormal = axis.cross(Math::Vector3::Up).cross(axis);
    }

    return safeNormalized(planeNormal, Math::Vector3::Up);
}

void drawQuadOutline(DebugRenderer* debugRenderer,
                     const Math::Vector3& a,
                     const Math::Vector3& b,
                     const Math::Vector3& c,
                     const Math::Vector3& d,
                     const Math::Vector4& color,
                     bool drawDiagonal) {
    debugRenderer->drawLine(a, b, color);
    debugRenderer->drawLine(b, c, color);
    debugRenderer->drawLine(c, d, color);
    debugRenderer->drawLine(d, a, color);

    if (drawDiagonal) {
        debugRenderer->drawLine(a, c, color);
        debugRenderer->drawLine(b, d, color);
    }
}

void drawPlanarHandle(DebugRenderer* debugRenderer,
                      const Math::Vector3& origin,
                      const Math::Vector3& axisA,
                      const Math::Vector3& axisB,
                      float scale,
                      const Math::Vector4& color,
                      bool highlight) {
    float inner = kTranslatePlaneInner * scale;
    float outer = kTranslatePlaneOuter * scale;

    Math::Vector3 p0 = origin + axisA * inner + axisB * inner;
    Math::Vector3 p1 = origin + axisA * outer + axisB * inner;
    Math::Vector3 p2 = origin + axisA * outer + axisB * outer;
    Math::Vector3 p3 = origin + axisA * inner + axisB * outer;

    drawQuadOutline(debugRenderer, p0, p1, p2, p3, color, highlight);
}

bool rayHitsPlanarHandle(const Ray& ray,
                         const Math::Vector3& origin,
                         const Math::Vector3& axisA,
                         const Math::Vector3& axisB,
                         float scale,
                         float& rayDistance) {
    Math::Vector3 planeNormal = safeNormalized(axisA.cross(axisB), Math::Vector3::Up);
    Math::Vector3 hitPoint;
    if (!intersectRayPlane(ray, origin, planeNormal, hitPoint)) {
        return false;
    }

    Math::Vector3 localPoint = hitPoint - origin;
    float u = localPoint.dot(axisA);
    float v = localPoint.dot(axisB);

    float inner = (kTranslatePlaneInner - kPlaneHitPadding) * scale;
    float outer = (kTranslatePlaneOuter + kPlaneHitPadding) * scale;

    if (u < inner || u > outer || v < inner || v > outer) {
        return false;
    }

    rayDistance = (hitPoint - ray.origin).length();
    return true;
}

void drawAxisStem(DebugRenderer* debugRenderer,
                  const Math::Vector3& origin,
                  const Math::Vector3& direction,
                  float length,
                  const Math::Vector4& color,
                  bool highlight) {
    Math::Vector3 tangent = safeNormalized(direction, Math::Vector3::Right);
    Math::Vector3 perp1 = std::abs(tangent.y) < 0.9f
        ? safeNormalized(tangent.cross(Math::Vector3::Up), Math::Vector3::Right)
        : safeNormalized(tangent.cross(Math::Vector3::Right), Math::Vector3::Forward);
    Math::Vector3 perp2 = safeNormalized(tangent.cross(perp1), Math::Vector3::Up);
    float thickness = highlight ? 0.06f : 0.032f;

    Math::Vector3 start = origin;
    Math::Vector3 end = origin + tangent * length;
    Math::Vector3 offset1 = perp1 * thickness;
    Math::Vector3 offset2 = perp2 * thickness;

    debugRenderer->drawLine(start, end, color);
    debugRenderer->drawLine(start + offset1, end + offset1, color);
    debugRenderer->drawLine(start - offset1, end - offset1, color);
    debugRenderer->drawLine(start + offset2, end + offset2, color);
    debugRenderer->drawLine(start - offset2, end - offset2, color);
}

Math::Vector3 projectPointOnAxis(const Math::Vector3& point,
                                 const Math::Vector3& origin,
                                 const Math::Vector3& axis) {
    return origin + axis * (point - origin).dot(axis);
}

Math::Vector3 projectPointOnPlane(const Math::Vector3& point,
                                  const Math::Vector3& origin,
                                  const Math::Vector3& axisA,
                                  const Math::Vector3& axisB) {
    Math::Vector3 local = point - origin;
    return origin + axisA * local.dot(axisA) + axisB * local.dot(axisB);
}

} // namespace

GizmoSystem::GizmoSystem()
    : m_debugRenderer(nullptr)
    , m_mode(GizmoMode::Translate)
    , m_space(GizmoSpace::World)
    , m_enabled(true)
    , m_gizmoSize(100.0f)
    , m_snapEnabled(false)
    , m_translateSnap(0.5f)
    , m_rotateSnapDegrees(15.0f)
    , m_scaleSnap(0.1f)
    , m_colorX(1.0f, 0.2f, 0.2f, 1.0f)      // Red
    , m_colorY(0.2f, 1.0f, 0.2f, 1.0f)      // Green
    , m_colorZ(0.2f, 0.4f, 1.0f, 1.0f)      // Blue
    , m_colorHighlight(1.0f, 1.0f, 0.0f, 1.0f) {   // Yellow
}

GizmoSystem::~GizmoSystem() {
}

void GizmoSystem::initialize(DebugRenderer* debugRenderer) {
    m_debugRenderer = debugRenderer;
}

float GizmoSystem::calculateGizmoScale(const Math::Vector3& gizmoPosition, 
                                       Camera* camera,
                                       const Math::Vector2& screenSize) const {
    if (!camera) return 1.0f;
    
    // Get distance from camera to gizmo
    Math::Vector3 cameraPos = camera->getEntity()->getTransform()->getPosition();
    float distance = (gizmoPosition - cameraPos).length();
    
    // Calculate scale to maintain constant screen size
    // This is a simple perspective scale - adjust multiplier as needed
    float scale = distance * (m_gizmoSize / screenSize.y) * 2.0f;
    
    return std::max(scale, 0.1f);  // Minimum scale
}

void GizmoSystem::drawMeshWire(Entity* entity, MeshRenderer* meshRenderer, const Math::Vector4& color) {
    auto mesh = meshRenderer->getMesh();
    if (!mesh) return;
    
    const auto& verts = mesh->getVertices();
    const auto& indices = mesh->getIndices();
    if (verts.empty() || indices.empty()) return;
    
    Math::Matrix4x4 world = entity->getTransform()->getWorldMatrix();

    static constexpr size_t kMaxWireframeTriangles = 20000;
    size_t triangleCount = indices.size() / 3;
    if (triangleCount > kMaxWireframeTriangles) {
        AABB bounds = SelectionSystem::getEntityBounds(entity);
        Math::Vector3 center = bounds.center();
        Math::Vector3 size = bounds.size();
        Math::Vector3 inflate = size * 0.02f + Math::Vector3(0.01f, 0.01f, 0.01f);
        m_debugRenderer->drawBox(center, size + inflate, color);
        return;
    }

    const auto& edges = mesh->getWireframeEdges();
    if (edges.empty()) return;

    for (const auto& edge : edges) {
        const Math::Vector3& p0 = verts[edge.first].position;
        const Math::Vector3& p1 = verts[edge.second].position;
        Math::Vector3 w0 = world.transformPoint(p0);
        Math::Vector3 w1 = world.transformPoint(p1);
        m_debugRenderer->drawLine(w0, w1, color);
    }
}

void GizmoSystem::drawGizmo(Entity* entity, Camera* camera, const Math::Vector2& screenSize) {
    if (!m_enabled || !entity || !camera || !m_debugRenderer) return;
    
    Transform* transform = entity->getTransform();
    Math::Vector3 position = transform->getPosition();
    Math::Quaternion orientation = (m_space == GizmoSpace::Local) 
        ? transform->getRotation() 
        : Math::Quaternion::Identity;
    
    float scale = calculateGizmoScale(position, camera, screenSize);
    
    switch (m_mode) {
        case GizmoMode::Translate:
            drawTranslateGizmo(position, orientation, scale, camera);
            break;
        case GizmoMode::Rotate:
            drawRotateGizmo(position, orientation, scale, camera);
            break;
        case GizmoMode::Scale:
            drawScaleGizmo(position, orientation, scale, camera);
            break;
    }
}

void GizmoSystem::drawTranslateGizmo(const Math::Vector3& position, 
                                     const Math::Quaternion& orientation,
                                     float scale, Camera* camera) {
    m_debugRenderer->drawSphere(position, kCenterSphereScale * scale,
        Math::Vector4(0.86f, 0.9f, 0.98f, 1.0f), 16);

    // X axis (Red)
    Math::Vector3 xDir = orientation * Math::Vector3::Right;
    bool highlightX = (m_state.activeAxis == GizmoAxis::X);
    drawArrow(position, xDir, ARROW_LENGTH * scale, 
             highlightX ? m_colorHighlight : m_colorX, highlightX);
    
    // Y axis (Green)
    Math::Vector3 yDir = orientation * Math::Vector3::Up;
    bool highlightY = (m_state.activeAxis == GizmoAxis::Y);
    drawArrow(position, yDir, ARROW_LENGTH * scale,
             highlightY ? m_colorHighlight : m_colorY, highlightY);
    
    // Z axis (Blue)
    Math::Vector3 zDir = orientation * Math::Vector3::Forward;
    bool highlightZ = (m_state.activeAxis == GizmoAxis::Z);
    drawArrow(position, zDir, ARROW_LENGTH * scale,
             highlightZ ? m_colorHighlight : m_colorZ, highlightZ);

    bool highlightXY = (m_state.activeAxis == GizmoAxis::XY);
    drawPlanarHandle(m_debugRenderer, position, xDir, yDir, scale,
        highlightXY ? m_colorHighlight : Math::Vector4(0.95f, 0.82f, 0.28f, 1.0f),
        highlightXY);

    bool highlightYZ = (m_state.activeAxis == GizmoAxis::YZ);
    drawPlanarHandle(m_debugRenderer, position, yDir, zDir, scale,
        highlightYZ ? m_colorHighlight : Math::Vector4(0.3f, 0.94f, 0.88f, 1.0f),
        highlightYZ);

    bool highlightXZ = (m_state.activeAxis == GizmoAxis::XZ);
    drawPlanarHandle(m_debugRenderer, position, xDir, zDir, scale,
        highlightXZ ? m_colorHighlight : Math::Vector4(0.95f, 0.35f, 0.82f, 1.0f),
        highlightXZ);
}

void GizmoSystem::drawRotateGizmo(const Math::Vector3& position,
                                  const Math::Quaternion& orientation,
                                  float scale, Camera* camera) {
    drawCircle(position, -getCameraForward(camera), CIRCLE_RADIUS * scale * kRotateOuterRingScale,
        Math::Vector4(0.82f, 0.84f, 0.9f, 1.0f), false);

    // X axis circle (Red)
    Math::Vector3 xNormal = orientation * Math::Vector3::Right;
    bool highlightX = (m_state.activeAxis == GizmoAxis::X);
    drawCircle(position, xNormal, CIRCLE_RADIUS * scale,
              highlightX ? m_colorHighlight : m_colorX, highlightX);
    
    // Y axis circle (Green)
    Math::Vector3 yNormal = orientation * Math::Vector3::Up;
    bool highlightY = (m_state.activeAxis == GizmoAxis::Y);
    drawCircle(position, yNormal, CIRCLE_RADIUS * scale,
              highlightY ? m_colorHighlight : m_colorY, highlightY);
    
    // Z axis circle (Blue)
    Math::Vector3 zNormal = orientation * Math::Vector3::Forward;
    bool highlightZ = (m_state.activeAxis == GizmoAxis::Z);
    drawCircle(position, zNormal, CIRCLE_RADIUS * scale,
              highlightZ ? m_colorHighlight : m_colorZ, highlightZ);
}

void GizmoSystem::drawScaleGizmo(const Math::Vector3& position,
                                 const Math::Quaternion& orientation,
                                 float scale, Camera* camera) {
    // X axis (Red)
    Math::Vector3 xDir = orientation * Math::Vector3::Right;
    bool highlightX = (m_state.activeAxis == GizmoAxis::X);
    drawAxisStem(m_debugRenderer, position, xDir, kScaleStemLength * scale,
                highlightX ? m_colorHighlight : m_colorX, highlightX);
    drawScaleBox(position + xDir * kScaleStemLength * scale, xDir, 
                SCALE_BOX_SIZE * scale,
                highlightX ? m_colorHighlight : m_colorX, highlightX);
    
    // Y axis (Green)
    Math::Vector3 yDir = orientation * Math::Vector3::Up;
    bool highlightY = (m_state.activeAxis == GizmoAxis::Y);
    drawAxisStem(m_debugRenderer, position, yDir, kScaleStemLength * scale,
                highlightY ? m_colorHighlight : m_colorY, highlightY);
    drawScaleBox(position + yDir * kScaleStemLength * scale, yDir,
                SCALE_BOX_SIZE * scale,
                highlightY ? m_colorHighlight : m_colorY, highlightY);
    
    // Z axis (Blue)
    Math::Vector3 zDir = orientation * Math::Vector3::Forward;
    bool highlightZ = (m_state.activeAxis == GizmoAxis::Z);
    drawAxisStem(m_debugRenderer, position, zDir, kScaleStemLength * scale,
                highlightZ ? m_colorHighlight : m_colorZ, highlightZ);
    drawScaleBox(position + zDir * kScaleStemLength * scale, zDir,
                SCALE_BOX_SIZE * scale,
                highlightZ ? m_colorHighlight : m_colorZ, highlightZ);
    
    // Center box for uniform scale
    bool highlightCenter = (m_state.activeAxis == GizmoAxis::XYZ);
    drawScaleBox(position, Math::Vector3::Zero, SCALE_BOX_SIZE * scale * 1.5f,
                highlightCenter ? m_colorHighlight : Math::Vector4(0.8f, 0.8f, 0.8f, 1.0f),
                highlightCenter);
}

void GizmoSystem::drawArrow(const Math::Vector3& origin, const Math::Vector3& direction,
                            float length, const Math::Vector4& color, bool highlight) {
    if (!m_debugRenderer) return;
    
    Math::Vector3 end = origin + direction * length;
    
    // Always draw thick lines (not just when highlighted)
    Math::Vector3 perp1, perp2;
    if (std::abs(direction.y) < 0.9f) {
        perp1 = direction.cross(Math::Vector3::Up).normalized();
    } else {
        perp1 = direction.cross(Math::Vector3::Right).normalized();
    }
    perp2 = direction.cross(perp1).normalized();
    
    // Thickness: bigger when highlighted
    float thickness = highlight ? 0.08f : 0.04f;  //  Increased thickness!
    perp1 = perp1 * thickness;
    perp2 = perp2 * thickness;
    
    // Draw 4 offset lines for thickness
    m_debugRenderer->drawLine(origin + perp1, end + perp1, color);
    m_debugRenderer->drawLine(origin - perp1, end - perp1, color);
    m_debugRenderer->drawLine(origin + perp2, end + perp2, color);
    m_debugRenderer->drawLine(origin - perp2, end - perp2, color);
    
    // Center line
    m_debugRenderer->drawLine(origin, end, color);
    
    // Arrow head (cone)
    Math::Vector3 arrowStart = end - direction * ARROW_HEAD_SIZE * 0.5f;
    
    // Get perpendicular vectors for arrow head
    Math::Vector3 arrowPerp1, arrowPerp2;
    if (std::abs(direction.y) < 0.9f) {
        arrowPerp1 = direction.cross(Math::Vector3::Up).normalized();
    } else {
        arrowPerp1 = direction.cross(Math::Vector3::Right).normalized();
    }
    arrowPerp2 = direction.cross(arrowPerp1).normalized();
    
    // Draw cone as lines
    int segments = 8;
    float radius = ARROW_HEAD_SIZE * 0.3f;
    for (int i = 0; i < segments; ++i) {
        float angle1 = (float)i / segments * Math::TWO_PI;
        float angle2 = (float)(i + 1) / segments * Math::TWO_PI;
        
        Math::Vector3 p1 = arrowStart + (arrowPerp1 * std::cos(angle1) + arrowPerp2 * std::sin(angle1)) * radius;
        Math::Vector3 p2 = arrowStart + (arrowPerp1 * std::cos(angle2) + arrowPerp2 * std::sin(angle2)) * radius;
        
        m_debugRenderer->drawLine(p1, end, color);
        m_debugRenderer->drawLine(p1, p2, color);
    }
}

void GizmoSystem::drawCircle(const Math::Vector3& center, const Math::Vector3& normal,
                             float radius, const Math::Vector4& color, bool highlight) {
    if (!m_debugRenderer) return;
    
    // Get two perpendicular vectors to normal
    Math::Vector3 perp1, perp2;
    if (std::abs(normal.y) < 0.9f) {
        perp1 = normal.cross(Math::Vector3::Up).normalized();
    } else {
        perp1 = normal.cross(Math::Vector3::Right).normalized();
    }
    perp2 = normal.cross(perp1).normalized();
    
    // Draw circle as line segments
    int segments = highlight ? 64 : 32;
    float thickness = highlight ? 0.06f : 0.03f;  // ✅ Thickness for circles!
    
    for (int i = 0; i < segments; ++i) {
        float angle1 = (float)i / segments * Math::TWO_PI;
        float angle2 = (float)(i + 1) / segments * Math::TWO_PI;
        
        Math::Vector3 p1 = center + (perp1 * std::cos(angle1) + perp2 * std::sin(angle1)) * radius;
        Math::Vector3 p2 = center + (perp1 * std::cos(angle2) + perp2 * std::sin(angle2)) * radius;
        
        // Center line
        m_debugRenderer->drawLine(p1, p2, color);
        
        // Offset lines for thickness (draw 2 inner + 2 outer)
        Math::Vector3 tangent = (p2 - p1).normalized();
        Math::Vector3 offset = normal.cross(tangent).normalized() * thickness;
        
        m_debugRenderer->drawLine(p1 + offset, p2 + offset, color);
        m_debugRenderer->drawLine(p1 - offset, p2 - offset, color);
    }
}

void GizmoSystem::drawScaleBox(const Math::Vector3& position, const Math::Vector3& direction,
                               float size, const Math::Vector4& color, bool highlight) {
    if (!m_debugRenderer) return;
    
    Math::Vector3 extent(size, size, size);
    m_debugRenderer->drawBox(position, extent * 2.0f, color);
}

void GizmoSystem::drawSelectionBox(Entity* entity) {
    if (!entity || !m_debugRenderer) return;
    
    Math::Vector4 selectionColor(1.0f, 0.85f, 0.2f, 1.0f);  // Warm yellow
    
    MeshRenderer* meshRenderer = entity->getComponent<MeshRenderer>();
    if (meshRenderer && meshRenderer->getMesh()) {
        drawMeshWire(entity, meshRenderer, selectionColor);
        return;
    }
    
    // Fallback to bounds box
    AABB bounds = SelectionSystem::getEntityBounds(entity);
    Math::Vector3 center = bounds.center();
    Math::Vector3 size = bounds.size();
    Math::Vector3 inflate = size * 0.02f + Math::Vector3(0.01f, 0.01f, 0.01f);
    m_debugRenderer->drawBox(center, size + inflate, selectionColor);
}

void GizmoSystem::hideGizmoMesh() {
    // No-op (using debug gizmo only)
}

void GizmoSystem::handleMouseDown(const Math::Vector2& mousePos, const Math::Vector2& screenSize,
                                  Entity* entity, Camera* camera) {
    if (!m_enabled || !entity || !camera) return;
    
    // Create ray from mouse position
    Ray ray = SelectionSystem::screenPointToRay(mousePos, screenSize, camera);
    
    Transform* transform = entity->getTransform();
    Math::Vector3 position = transform->getPosition();
    Math::Quaternion orientation = (m_space == GizmoSpace::Local)
        ? transform->getRotation()
        : Math::Quaternion::Identity;
    
    float scale = calculateGizmoScale(position, camera, screenSize);
    
    // Hit test based on mode
    GizmoAxis hitAxis = GizmoAxis::None;
    switch (m_mode) {
        case GizmoMode::Translate:
            hitAxis = hitTestTranslateGizmo(ray, position, orientation, scale);
            break;
        case GizmoMode::Rotate:
            hitAxis = hitTestRotateGizmo(ray, position, orientation, scale);
            break;
        case GizmoMode::Scale:
            hitAxis = hitTestScaleGizmo(ray, position, orientation, scale);
            break;
    }
    
    if (hitAxis != GizmoAxis::None) {
        m_state.isActive = true;
        m_state.activeAxis = hitAxis;
        m_state.dragStartPosition = position;
        m_state.mouseStartPosition = mousePos;
        
        // Store initial transform value
        switch (m_mode) {
            case GizmoMode::Translate:
                m_state.dragStartValue = position;
                break;
            case GizmoMode::Rotate:
                m_state.dragStartValue = transform->getEulerAngles() * Math::RAD_TO_DEG;
                break;
            case GizmoMode::Scale:
                m_state.dragStartValue = transform->getLocalScale();
                break;
        }
        
    }
}

void GizmoSystem::handleMouseDrag(const Math::Vector2& mousePos, const Math::Vector2& screenSize,
                                  Entity* entity, Camera* camera) {
    if (!m_state.isActive || !entity || !camera) return;
    
    // Calculate mouse delta
    Math::Vector2 mouseDelta = mousePos - m_state.mouseStartPosition;
    Ray startRay = SelectionSystem::screenPointToRay(m_state.mouseStartPosition, screenSize, camera);
    Ray currentRay = SelectionSystem::screenPointToRay(mousePos, screenSize, camera);
    
    // Apply transformation based on mode
    Transform* transform = entity->getTransform();
    
    switch (m_mode) {
        case GizmoMode::Translate: {
            Math::Quaternion orientation = (m_space == GizmoSpace::Local)
                ? transform->getRotation()
                : Math::Quaternion::Identity;
            Math::Vector3 xDir = orientation * Math::Vector3::Right;
            Math::Vector3 yDir = orientation * Math::Vector3::Up;
            Math::Vector3 zDir = orientation * Math::Vector3::Forward;

            Math::Vector3 dragStartPoint;
            Math::Vector3 dragCurrentPoint;
            Math::Vector3 newPos = m_state.dragStartValue;
            bool hasProjectedDrag = false;

            switch (m_state.activeAxis) {
                case GizmoAxis::X: {
                    Math::Vector3 planeNormal = getAxisDragPlaneNormal(xDir, camera);
                    hasProjectedDrag =
                        intersectRayPlane(startRay, m_state.dragStartPosition, planeNormal, dragStartPoint) &&
                        intersectRayPlane(currentRay, m_state.dragStartPosition, planeNormal, dragCurrentPoint);
                    if (hasProjectedDrag) {
                        Math::Vector3 startAxisPoint = projectPointOnAxis(dragStartPoint, m_state.dragStartPosition, xDir);
                        Math::Vector3 currentAxisPoint = projectPointOnAxis(dragCurrentPoint, m_state.dragStartPosition, xDir);
                        newPos += currentAxisPoint - startAxisPoint;
                    }
                    break;
                }
                case GizmoAxis::Y: {
                    Math::Vector3 planeNormal = getAxisDragPlaneNormal(yDir, camera);
                    hasProjectedDrag =
                        intersectRayPlane(startRay, m_state.dragStartPosition, planeNormal, dragStartPoint) &&
                        intersectRayPlane(currentRay, m_state.dragStartPosition, planeNormal, dragCurrentPoint);
                    if (hasProjectedDrag) {
                        Math::Vector3 startAxisPoint = projectPointOnAxis(dragStartPoint, m_state.dragStartPosition, yDir);
                        Math::Vector3 currentAxisPoint = projectPointOnAxis(dragCurrentPoint, m_state.dragStartPosition, yDir);
                        newPos += currentAxisPoint - startAxisPoint;
                    }
                    break;
                }
                case GizmoAxis::Z: {
                    Math::Vector3 planeNormal = getAxisDragPlaneNormal(zDir, camera);
                    hasProjectedDrag =
                        intersectRayPlane(startRay, m_state.dragStartPosition, planeNormal, dragStartPoint) &&
                        intersectRayPlane(currentRay, m_state.dragStartPosition, planeNormal, dragCurrentPoint);
                    if (hasProjectedDrag) {
                        Math::Vector3 startAxisPoint = projectPointOnAxis(dragStartPoint, m_state.dragStartPosition, zDir);
                        Math::Vector3 currentAxisPoint = projectPointOnAxis(dragCurrentPoint, m_state.dragStartPosition, zDir);
                        newPos += currentAxisPoint - startAxisPoint;
                    }
                    break;
                }
                case GizmoAxis::XY: {
                    Math::Vector3 planeNormal = safeNormalized(xDir.cross(yDir), Math::Vector3::Forward);
                    hasProjectedDrag =
                        intersectRayPlane(startRay, m_state.dragStartPosition, planeNormal, dragStartPoint) &&
                        intersectRayPlane(currentRay, m_state.dragStartPosition, planeNormal, dragCurrentPoint);
                    if (hasProjectedDrag) {
                        Math::Vector3 startPlanePoint = projectPointOnPlane(dragStartPoint, m_state.dragStartPosition, xDir, yDir);
                        Math::Vector3 currentPlanePoint = projectPointOnPlane(dragCurrentPoint, m_state.dragStartPosition, xDir, yDir);
                        newPos += currentPlanePoint - startPlanePoint;
                    }
                    break;
                }
                case GizmoAxis::YZ: {
                    Math::Vector3 planeNormal = safeNormalized(yDir.cross(zDir), Math::Vector3::Right);
                    hasProjectedDrag =
                        intersectRayPlane(startRay, m_state.dragStartPosition, planeNormal, dragStartPoint) &&
                        intersectRayPlane(currentRay, m_state.dragStartPosition, planeNormal, dragCurrentPoint);
                    if (hasProjectedDrag) {
                        Math::Vector3 startPlanePoint = projectPointOnPlane(dragStartPoint, m_state.dragStartPosition, yDir, zDir);
                        Math::Vector3 currentPlanePoint = projectPointOnPlane(dragCurrentPoint, m_state.dragStartPosition, yDir, zDir);
                        newPos += currentPlanePoint - startPlanePoint;
                    }
                    break;
                }
                case GizmoAxis::XZ: {
                    Math::Vector3 planeNormal = safeNormalized(xDir.cross(zDir), Math::Vector3::Up);
                    hasProjectedDrag =
                        intersectRayPlane(startRay, m_state.dragStartPosition, planeNormal, dragStartPoint) &&
                        intersectRayPlane(currentRay, m_state.dragStartPosition, planeNormal, dragCurrentPoint);
                    if (hasProjectedDrag) {
                        Math::Vector3 startPlanePoint = projectPointOnPlane(dragStartPoint, m_state.dragStartPosition, xDir, zDir);
                        Math::Vector3 currentPlanePoint = projectPointOnPlane(dragCurrentPoint, m_state.dragStartPosition, xDir, zDir);
                        newPos += currentPlanePoint - startPlanePoint;
                    }
                    break;
                }
                default:
                    break;
            }

            if (!hasProjectedDrag) {
                float sensitivity = 0.01f;
                switch (m_state.activeAxis) {
                    case GizmoAxis::X:
                        newPos.x += mouseDelta.x * sensitivity;
                        break;
                    case GizmoAxis::Y:
                        newPos.y -= mouseDelta.y * sensitivity;
                        break;
                    case GizmoAxis::Z:
                        newPos.z -= mouseDelta.y * sensitivity;
                        break;
                    default:
                        break;
                }
            }
            
            if (m_snapEnabled) {
                newPos.x = std::round(newPos.x / m_translateSnap) * m_translateSnap;
                newPos.y = std::round(newPos.y / m_translateSnap) * m_translateSnap;
                newPos.z = std::round(newPos.z / m_translateSnap) * m_translateSnap;
            }
            
            transform->setPosition(newPos);
            break;
        }
        
        case GizmoMode::Rotate: {
            Math::Quaternion orientation = (m_space == GizmoSpace::Local)
                ? transform->getRotation()
                : Math::Quaternion::Identity;
            Math::Vector3 rotation = m_state.dragStartValue;
            Math::Vector3 rotationAxis = Math::Vector3::Zero;

            switch (m_state.activeAxis) {
                case GizmoAxis::X:
                    rotationAxis = orientation * Math::Vector3::Right;
                    break;
                case GizmoAxis::Y:
                    rotationAxis = orientation * Math::Vector3::Up;
                    break;
                case GizmoAxis::Z:
                    rotationAxis = orientation * Math::Vector3::Forward;
                    break;
                default:
                    break;
            }

            Math::Vector3 startHit;
            Math::Vector3 currentHit;
            bool usedProjectedRotation = rotationAxis.lengthSquared() > 1e-6f &&
                intersectRayPlane(startRay, m_state.dragStartPosition, rotationAxis, startHit) &&
                intersectRayPlane(currentRay, m_state.dragStartPosition, rotationAxis, currentHit);

            if (usedProjectedRotation) {
                Math::Vector3 startVector = safeNormalized(startHit - m_state.dragStartPosition, rotationAxis);
                Math::Vector3 currentVector = safeNormalized(currentHit - m_state.dragStartPosition, rotationAxis);
                float deltaDegrees = std::atan2(
                    startVector.cross(currentVector).dot(rotationAxis),
                    Math::Clamp(startVector.dot(currentVector), -1.0f, 1.0f)
                ) * Math::RAD_TO_DEG;

                switch (m_state.activeAxis) {
                    case GizmoAxis::X:
                        rotation.x += deltaDegrees;
                        break;
                    case GizmoAxis::Y:
                        rotation.y += deltaDegrees;
                        break;
                    case GizmoAxis::Z:
                        rotation.z += deltaDegrees;
                        break;
                    default:
                        break;
                }
            } else {
                float sensitivity = 0.5f;
                switch (m_state.activeAxis) {
                    case GizmoAxis::X:
                        rotation.x += mouseDelta.y * sensitivity;
                        break;
                    case GizmoAxis::Y:
                        rotation.y += mouseDelta.x * sensitivity;
                        break;
                    case GizmoAxis::Z:
                        rotation.z += mouseDelta.x * sensitivity;
                        break;
                    default:
                        break;
                }
            }
            
            if (m_snapEnabled) {
                rotation.x = std::round(rotation.x / m_rotateSnapDegrees) * m_rotateSnapDegrees;
                rotation.y = std::round(rotation.y / m_rotateSnapDegrees) * m_rotateSnapDegrees;
                rotation.z = std::round(rotation.z / m_rotateSnapDegrees) * m_rotateSnapDegrees;
            }
            
            transform->setEulerAngles(rotation * Math::DEG_TO_RAD);
            break;
        }
        
        case GizmoMode::Scale: {
            float sensitivity = 0.01f;
            Math::Vector3 scale = m_state.dragStartValue;
            float scaleDelta = -mouseDelta.y * sensitivity;
            
            switch (m_state.activeAxis) {
                case GizmoAxis::X:
                    scale.x += scaleDelta;
                    break;
                case GizmoAxis::Y:
                    scale.y += scaleDelta;
                    break;
                case GizmoAxis::Z:
                    scale.z += scaleDelta;
                    break;
                case GizmoAxis::XYZ:
                    scale += Math::Vector3(scaleDelta, scaleDelta, scaleDelta);
                    break;
                default:
                    break;
            }
            
            // Clamp to prevent negative scale
            scale.x = std::max(scale.x, 0.01f);
            scale.y = std::max(scale.y, 0.01f);
            scale.z = std::max(scale.z, 0.01f);
            
            if (m_snapEnabled) {
                scale.x = std::round(scale.x / m_scaleSnap) * m_scaleSnap;
                scale.y = std::round(scale.y / m_scaleSnap) * m_scaleSnap;
                scale.z = std::round(scale.z / m_scaleSnap) * m_scaleSnap;
            }
            
            transform->setLocalScale(scale);
            break;
        }
    }
}

void GizmoSystem::handleMouseUp() {
    m_state.isActive = false;
    m_state.activeAxis = GizmoAxis::None;
}

GizmoAxis GizmoSystem::hitTestTranslateGizmo(const Ray& ray, const Math::Vector3& position,
                                            const Math::Quaternion& orientation, float scale) const {
    float closestPlaneRayDistance = std::numeric_limits<float>::max();
    GizmoAxis closestPlaneAxis = GizmoAxis::None;

    Math::Vector3 xDir = orientation * Math::Vector3::Right;
    Math::Vector3 yDir = orientation * Math::Vector3::Up;
    Math::Vector3 zDir = orientation * Math::Vector3::Forward;

    float planeRayDistance = 0.0f;
    if (rayHitsPlanarHandle(ray, position, xDir, yDir, scale, planeRayDistance) &&
        planeRayDistance < closestPlaneRayDistance) {
        closestPlaneRayDistance = planeRayDistance;
        closestPlaneAxis = GizmoAxis::XY;
    }

    if (rayHitsPlanarHandle(ray, position, yDir, zDir, scale, planeRayDistance) &&
        planeRayDistance < closestPlaneRayDistance) {
        closestPlaneRayDistance = planeRayDistance;
        closestPlaneAxis = GizmoAxis::YZ;
    }

    if (rayHitsPlanarHandle(ray, position, xDir, zDir, scale, planeRayDistance) &&
        planeRayDistance < closestPlaneRayDistance) {
        closestPlaneRayDistance = planeRayDistance;
        closestPlaneAxis = GizmoAxis::XZ;
    }

    if (closestPlaneAxis != GizmoAxis::None) {
        return closestPlaneAxis;
    }

    float closestDist = std::numeric_limits<float>::max();
    GizmoAxis closestAxis = GizmoAxis::None;
    
    // Test X axis
    Math::Vector3 xEnd = position + xDir * ARROW_LENGTH * scale;
    float distX = rayToLineDistance(ray, position, xEnd);
    if (distX < HIT_THRESHOLD * scale && distX < closestDist) {
        closestDist = distX;
        closestAxis = GizmoAxis::X;
    }
    
    // Test Y axis
    Math::Vector3 yEnd = position + yDir * ARROW_LENGTH * scale;
    float distY = rayToLineDistance(ray, position, yEnd);
    if (distY < HIT_THRESHOLD * scale && distY < closestDist) {
        closestDist = distY;
        closestAxis = GizmoAxis::Y;
    }
    
    // Test Z axis
    Math::Vector3 zEnd = position + zDir * ARROW_LENGTH * scale;
    float distZ = rayToLineDistance(ray, position, zEnd);
    if (distZ < HIT_THRESHOLD * scale && distZ < closestDist) {
        closestDist = distZ;
        closestAxis = GizmoAxis::Z;
    }
    
    return closestAxis;
}

GizmoAxis GizmoSystem::hitTestRotateGizmo(const Ray& ray, const Math::Vector3& position,
                                         const Math::Quaternion& orientation, float scale) const {
    float closestDist = std::numeric_limits<float>::max();
    GizmoAxis closestAxis = GizmoAxis::None;

    Math::Vector3 xNormal = orientation * Math::Vector3::Right;
    float distX = rayToCircleDistance(ray, position, xNormal, CIRCLE_RADIUS * scale);
    if (distX < kRotateHitBand * scale && distX < closestDist) {
        closestDist = distX;
        closestAxis = GizmoAxis::X;
    }

    Math::Vector3 yNormal = orientation * Math::Vector3::Up;
    float distY = rayToCircleDistance(ray, position, yNormal, CIRCLE_RADIUS * scale);
    if (distY < kRotateHitBand * scale && distY < closestDist) {
        closestDist = distY;
        closestAxis = GizmoAxis::Y;
    }

    Math::Vector3 zNormal = orientation * Math::Vector3::Forward;
    float distZ = rayToCircleDistance(ray, position, zNormal, CIRCLE_RADIUS * scale);
    if (distZ < kRotateHitBand * scale && distZ < closestDist) {
        closestDist = distZ;
        closestAxis = GizmoAxis::Z;
    }

    return closestAxis;
}

GizmoAxis GizmoSystem::hitTestScaleGizmo(const Ray& ray, const Math::Vector3& position,
                                        const Math::Quaternion& orientation, float scale) const {
    // Test center box first for uniform scale
    Math::Vector3 centerExtent(SCALE_BOX_SIZE * scale * 1.5f, SCALE_BOX_SIZE * scale * 1.5f, SCALE_BOX_SIZE * scale * 1.5f);
    AABB centerBox(position - centerExtent, position + centerExtent);
    float centerDist;
    if (SelectionSystem::rayIntersectsAABB(ray, centerBox, centerDist)) {
        return GizmoAxis::XYZ;
    }

    float closestDist = std::numeric_limits<float>::max();
    GizmoAxis closestAxis = GizmoAxis::None;

    Math::Vector3 xDir = orientation * Math::Vector3::Right;
    Math::Vector3 xCenter = position + xDir * kScaleStemLength * scale;
    Math::Vector3 boxExtent(SCALE_BOX_SIZE * scale * 1.25f);
    float hitDistance = 0.0f;
    if (SelectionSystem::rayIntersectsAABB(ray, AABB(xCenter - boxExtent, xCenter + boxExtent), hitDistance) &&
        hitDistance < closestDist) {
        closestDist = hitDistance;
        closestAxis = GizmoAxis::X;
    }

    Math::Vector3 yDir = orientation * Math::Vector3::Up;
    Math::Vector3 yCenter = position + yDir * kScaleStemLength * scale;
    if (SelectionSystem::rayIntersectsAABB(ray, AABB(yCenter - boxExtent, yCenter + boxExtent), hitDistance) &&
        hitDistance < closestDist) {
        closestDist = hitDistance;
        closestAxis = GizmoAxis::Y;
    }

    Math::Vector3 zDir = orientation * Math::Vector3::Forward;
    Math::Vector3 zCenter = position + zDir * kScaleStemLength * scale;
    if (SelectionSystem::rayIntersectsAABB(ray, AABB(zCenter - boxExtent, zCenter + boxExtent), hitDistance) &&
        hitDistance < closestDist) {
        closestDist = hitDistance;
        closestAxis = GizmoAxis::Z;
    }

    if (closestAxis != GizmoAxis::None) {
        return closestAxis;
    }

    return hitTestTranslateGizmo(ray, position, orientation, scale);
}

float GizmoSystem::rayToLineDistance(const Ray& ray, const Math::Vector3& lineStart,
                                     const Math::Vector3& lineEnd) const {
    // Calculate closest distance from ray to line segment
    Math::Vector3 lineDir = (lineEnd - lineStart).normalized();
    Math::Vector3 toStart = lineStart - ray.origin;
    
    float rayDotLine = ray.direction.dot(lineDir);
    float det = 1.0f - rayDotLine * rayDotLine;
    
    if (std::abs(det) < 1e-6f) {
        // Ray and line are parallel
        return toStart.length();
    }
    
    float rayDotToStart = ray.direction.dot(toStart);
    float lineDotToStart = lineDir.dot(toStart);
    
    float t = (lineDotToStart - rayDotLine * rayDotToStart) / det;
    float s = (rayDotToStart - rayDotLine * lineDotToStart) / det;
    
    // Clamp t to line segment
    float lineLength = (lineEnd - lineStart).length();
    t = Math::Clamp(t, 0.0f, lineLength);
    s = std::max(s, 0.0f);
    
    Math::Vector3 pointOnLine = lineStart + lineDir * t;
    Math::Vector3 pointOnRay = ray.origin + ray.direction * s;
    
    return (pointOnLine - pointOnRay).length();
}

float GizmoSystem::rayToCircleDistance(const Ray& ray, const Math::Vector3& center,
                                       const Math::Vector3& normal, float radius) const {
    Math::Vector3 hitPoint;
    if (!intersectRayPlane(ray, center, normal, hitPoint)) {
        return std::numeric_limits<float>::max();
    }

    float distFromCenter = (hitPoint - center).length();
    return std::abs(distFromCenter - radius);
}

} // namespace Crescent
