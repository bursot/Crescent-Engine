#pragma once

#include "../ECS/Component.hpp"
#include "../Math/Math.hpp"
#include <string>

namespace Crescent {

class IKConstraint : public Component {
public:
    IKConstraint();
    virtual ~IKConstraint() = default;

    COMPONENT_TYPE(IKConstraint)

    const std::string& getRootBone() const { return m_RootBone; }
    void setRootBone(const std::string& name) { m_RootBone = name; }

    const std::string& getMidBone() const { return m_MidBone; }
    void setMidBone(const std::string& name) { m_MidBone = name; }

    const std::string& getEndBone() const { return m_EndBone; }
    void setEndBone(const std::string& name) { m_EndBone = name; }

    const Math::Vector3& getTargetPosition() const { return m_TargetPosition; }
    void setTargetPosition(const Math::Vector3& position) { m_TargetPosition = position; }

    bool getTargetInWorld() const { return m_TargetInWorld; }
    void setTargetInWorld(bool world) { m_TargetInWorld = world; }

    float getWeight() const { return m_Weight; }
    void setWeight(float weight) { m_Weight = Math::Clamp(weight, 0.0f, 1.0f); }

private:
    std::string m_RootBone;
    std::string m_MidBone;
    std::string m_EndBone;
    Math::Vector3 m_TargetPosition;
    bool m_TargetInWorld;
    float m_Weight;
};

} // namespace Crescent
