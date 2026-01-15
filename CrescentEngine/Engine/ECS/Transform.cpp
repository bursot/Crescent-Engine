#include "Transform.hpp"
#include "Entity.hpp"
#include <algorithm>
#include <cmath>

namespace Crescent {

Transform::Transform()
    : m_LocalPosition(Math::Vector3::Zero)
    , m_LocalRotation(Math::Quaternion::Identity)
    , m_LocalEulerAngles(Math::Vector3::Zero)
    , m_LocalScale(Math::Vector3::One)
    , m_WorldMatrix(Math::Matrix4x4::Identity)
    , m_IsDirty(true)
    , m_PrevWorldMatrix(Math::Matrix4x4::Identity)
    , m_HasPrevWorldMatrix(false)
    , m_Parent(nullptr) {
}

void Transform::setLocalPosition(const Math::Vector3& position) {
    m_LocalPosition = position;
    markDirty();
}

void Transform::setLocalRotation(const Math::Quaternion& rotation) {
    m_LocalRotation = rotation;
    m_LocalEulerAngles = rotation.toEulerAngles();
    markDirty();
}

void Transform::setLocalEulerAngles(const Math::Vector3& euler) {
    m_LocalEulerAngles = euler;
    m_LocalRotation = Math::Quaternion::FromEulerAngles(euler);
    markDirty();
}

void Transform::setLocalScale(const Math::Vector3& scale) {
    m_LocalScale = scale;
    markDirty();
}

void Transform::setPosition(const Math::Vector3& position) {
    if (m_Parent) {
        // Convert world position to local position
        Math::Matrix4x4 parentWorldInv = m_Parent->getWorldMatrix().inversed();
        m_LocalPosition = parentWorldInv.transformPoint(position);
    } else {
        m_LocalPosition = position;
    }
    markDirty();
}

void Transform::setRotation(const Math::Quaternion& rotation) {
    if (m_Parent) {
        // Convert world rotation to local rotation
        Math::Quaternion parentRotInv = m_Parent->getRotation().inverse();
        m_LocalRotation = parentRotInv * rotation;
    } else {
        m_LocalRotation = rotation;
    }
    m_LocalEulerAngles = m_LocalRotation.toEulerAngles();
    markDirty();
}

void Transform::setEulerAngles(const Math::Vector3& euler) {
    setRotation(Math::Quaternion::FromEulerAngles(euler));
}

Math::Vector3 Transform::getPosition() const {
    if (m_Parent) {
        return m_Parent->getWorldMatrix().transformPoint(m_LocalPosition);
    }
    return m_LocalPosition;
}

Math::Quaternion Transform::getRotation() const {
    if (m_Parent) {
        return m_Parent->getRotation() * m_LocalRotation;
    }
    return m_LocalRotation;
}

Math::Vector3 Transform::getEulerAngles() const {
    return getRotation().toEulerAngles();
}

Math::Vector3 Transform::getScale() const {
    if (m_Parent) {
        Math::Vector3 parentScale = m_Parent->getScale();
        return Math::Vector3(
            m_LocalScale.x * parentScale.x,
            m_LocalScale.y * parentScale.y,
            m_LocalScale.z * parentScale.z
        );
    }
    return m_LocalScale;
}

Math::Matrix4x4 Transform::getLocalMatrix() const {
    return Math::Matrix4x4::TRS(m_LocalPosition, m_LocalRotation, m_LocalScale);
}

Math::Matrix4x4 Transform::getWorldMatrix() const {
    if (m_IsDirty) {
        updateWorldMatrix();
    }
    return m_WorldMatrix;
}

Math::Matrix4x4 Transform::getPreviousWorldMatrix() const {
    if (!m_HasPrevWorldMatrix) {
        return getWorldMatrix();
    }
    return m_PrevWorldMatrix;
}

void Transform::capturePreviousWorldMatrix() {
    m_PrevWorldMatrix = getWorldMatrix();
    m_HasPrevWorldMatrix = true;
}

void Transform::updateWorldMatrix() const {
    if (m_Parent) {
        m_WorldMatrix = m_Parent->getWorldMatrix() * getLocalMatrix();
    } else {
        m_WorldMatrix = getLocalMatrix();
    }
    m_IsDirty = false;
}

Math::Vector3 Transform::forward() const {
    return getRotation() * Math::Vector3::Forward;
}

Math::Vector3 Transform::back() const {
    return getRotation() * Math::Vector3::Back;
}

Math::Vector3 Transform::up() const {
    return getRotation() * Math::Vector3::Up;
}

Math::Vector3 Transform::down() const {
    return getRotation() * Math::Vector3::Down;
}

Math::Vector3 Transform::right() const {
    return getRotation() * Math::Vector3::Right;
}

Math::Vector3 Transform::left() const {
    return getRotation() * Math::Vector3::Left;
}

void Transform::translate(const Math::Vector3& translation, bool relativeToSelf) {
    if (relativeToSelf) {
        m_LocalPosition += m_LocalRotation * translation;
    } else {
        m_LocalPosition += translation;
    }
    markDirty();
}

void Transform::rotate(const Math::Quaternion& rotation, bool relativeToSelf) {
    if (relativeToSelf) {
        m_LocalRotation = m_LocalRotation * rotation;
    } else {
        m_LocalRotation = rotation * m_LocalRotation;
    }
    markDirty();
}

void Transform::rotateAround(const Math::Vector3& point, const Math::Vector3& axis, float angle) {
    Math::Quaternion rotation = Math::Quaternion::FromAxisAngle(axis, angle);
    Math::Vector3 worldPos = getPosition();
    Math::Vector3 diff = worldPos - point;
    diff = rotation * diff;
    setPosition(point + diff);
    rotate(rotation, false);
}

void Transform::lookAt(const Math::Vector3& target, const Math::Vector3& up) {
    Math::Vector3 worldPos = getPosition();
    Math::Vector3 forward = (target - worldPos).normalized();
    
    if (forward.lengthSquared() < 0.001f) return;
    
    Math::Quaternion lookRotation = Math::Quaternion::LookRotation(forward, up);
    setRotation(lookRotation);
}

void Transform::setParent(Transform* parent, bool worldPositionStays) {
    if (m_Parent == parent) return;
    
    // Store world transform if needed
    Math::Vector3 worldPos;
    Math::Quaternion worldRot;
    Math::Vector3 worldScale;
    
    if (worldPositionStays) {
        worldPos = getPosition();
        worldRot = getRotation();
        worldScale = getScale();
    }
    
    // Remove from old parent
    if (m_Parent) {
        m_Parent->removeChild(this);
    }
    
    // Set new parent
    m_Parent = parent;
    
    // Add to new parent
    if (m_Parent) {
        m_Parent->addChild(this);
        
        if (worldPositionStays) {
            // Convert world transform to local
            setPosition(worldPos);
            setRotation(worldRot);
            // Note: scale is not perfectly preserved in all cases with non-uniform parent scales
            Math::Vector3 parentScale = m_Parent->getScale();
            auto safeDivide = [](float numerator, float denominator) {
                constexpr float kMinScale = 1e-6f;
                if (std::abs(denominator) <= kMinScale) {
                    return 0.0f;
                }
                return numerator / denominator;
            };
            m_LocalScale = Math::Vector3(
                safeDivide(worldScale.x, parentScale.x),
                safeDivide(worldScale.y, parentScale.y),
                safeDivide(worldScale.z, parentScale.z)
            );
        }
    } else {
        if (worldPositionStays) {
            m_LocalPosition = worldPos;
            m_LocalRotation = worldRot;
            m_LocalScale = worldScale;
        }
    }
    
    markDirty();
}

Transform* Transform::getChild(int index) const {
    if (index >= 0 && index < static_cast<int>(m_Children.size())) {
        return m_Children[index];
    }
    return nullptr;
}

bool Transform::isChildOf(const Transform* parent) const {
    Transform* current = m_Parent;
    while (current) {
        if (current == parent) return true;
        current = current->m_Parent;
    }
    return false;
}

Transform* Transform::find(const std::string& name) const {
    for (Transform* child : m_Children) {
        if (child->getEntity() && child->getEntity()->getName() == name) {
            return child;
        }
        
        // Recursive search
        Transform* result = child->find(name);
        if (result) return result;
    }
    return nullptr;
}

void Transform::markDirty() {
    m_IsDirty = true;
    markChildrenDirty();
}

void Transform::markChildrenDirty() {
    for (Transform* child : m_Children) {
        child->markDirty();
    }
}

void Transform::addChild(Transform* child) {
    auto it = std::find(m_Children.begin(), m_Children.end(), child);
    if (it == m_Children.end()) {
        m_Children.push_back(child);
    }
}

void Transform::removeChild(Transform* child) {
    auto it = std::find(m_Children.begin(), m_Children.end(), child);
    if (it != m_Children.end()) {
        m_Children.erase(it);
    }
}

void Transform::OnCreate() {
    // Transform is automatically added to all entities
}

void Transform::OnDestroy() {
    // Remove from parent
    if (m_Parent) {
        m_Parent->removeChild(this);
        m_Parent = nullptr;
    }
    
    // Detach children
    for (Transform* child : m_Children) {
        child->m_Parent = nullptr;
    }
    m_Children.clear();
}

} // namespace Crescent
