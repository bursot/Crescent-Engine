#include "IKConstraint.hpp"

namespace Crescent {

IKConstraint::IKConstraint()
    : m_RootBone("")
    , m_MidBone("")
    , m_EndBone("")
    , m_TargetPosition(0.0f, 0.0f, 0.0f)
    , m_TargetInWorld(true)
    , m_Weight(1.0f) {
}

} // namespace Crescent
