#pragma once

#include "Component.hpp"
#include "../Math/Math.hpp"
#include <vector>

namespace Crescent {

// Transform component - handles position, rotation, scale, and parent-child hierarchy
class Transform : public Component {
public:
    Transform();
    virtual ~Transform() = default;
    
    COMPONENT_TYPE(Transform)
    
    // Local transform (relative to parent)
    void setLocalPosition(const Math::Vector3& position);
    void setLocalRotation(const Math::Quaternion& rotation);
    void setLocalEulerAngles(const Math::Vector3& euler);
    void setLocalScale(const Math::Vector3& scale);
    
    const Math::Vector3& getLocalPosition() const { return m_LocalPosition; }
    const Math::Quaternion& getLocalRotation() const { return m_LocalRotation; }
    Math::Vector3 getLocalEulerAngles() const { return m_LocalEulerAngles; }
    const Math::Vector3& getLocalScale() const { return m_LocalScale; }
    
    // World transform (absolute)
    void setPosition(const Math::Vector3& position);
    void setRotation(const Math::Quaternion& rotation);
    void setEulerAngles(const Math::Vector3& euler);
    
    Math::Vector3 getPosition() const;
    Math::Quaternion getRotation() const;
    Math::Vector3 getEulerAngles() const;
    Math::Vector3 getScale() const;
    
    // Transform matrices
    Math::Matrix4x4 getLocalMatrix() const;
    Math::Matrix4x4 getWorldMatrix() const;
    Math::Matrix4x4 getPreviousWorldMatrix() const;
    void capturePreviousWorldMatrix();
    bool hasPreviousWorldMatrix() const { return m_HasPrevWorldMatrix; }
    
    // Transform directions
    Math::Vector3 forward() const;
    Math::Vector3 back() const;
    Math::Vector3 up() const;
    Math::Vector3 down() const;
    Math::Vector3 right() const;
    Math::Vector3 left() const;
    
    // Transform operations
    void translate(const Math::Vector3& translation, bool relativeToSelf = true);
    void rotate(const Math::Quaternion& rotation, bool relativeToSelf = true);
    void rotateAround(const Math::Vector3& point, const Math::Vector3& axis, float angle);
    void lookAt(const Math::Vector3& target, const Math::Vector3& up = Math::Vector3::Up);
    
    // Hierarchy
    Transform* getParent() const { return m_Parent; }
    void setParent(Transform* parent, bool worldPositionStays = true);
    
    const std::vector<Transform*>& getChildren() const { return m_Children; }
    int getChildCount() const { return static_cast<int>(m_Children.size()); }
    Transform* getChild(int index) const;
    
    bool isChildOf(const Transform* parent) const;
    Transform* find(const std::string& name) const;
    
    // Mark for update
    void markDirty();
    bool isDirty() const { return m_IsDirty; }
    
    // Lifecycle
    void OnCreate() override;
    void OnDestroy() override;
    
private:
    void updateWorldMatrix() const;
    void addChild(Transform* child);
    void removeChild(Transform* child);
    void markChildrenDirty();
    
private:
    // Local transform
    Math::Vector3 m_LocalPosition;
    Math::Quaternion m_LocalRotation;
    Math::Vector3 m_LocalEulerAngles;
    Math::Vector3 m_LocalScale;
    
    // Cached world matrix
    mutable Math::Matrix4x4 m_WorldMatrix;
    mutable bool m_IsDirty;
    Math::Matrix4x4 m_PrevWorldMatrix;
    bool m_HasPrevWorldMatrix;
    
    // Hierarchy
    Transform* m_Parent;
    std::vector<Transform*> m_Children;
};

} // namespace Crescent
