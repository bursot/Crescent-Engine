#include "Component.hpp"

namespace Crescent {

void Component::setEnabled(bool enabled) {
    if (m_Enabled == enabled) return;
    
    m_Enabled = enabled;
    
    if (m_Enabled) {
        OnEnable();
    } else {
        OnDisable();
    }
}

} // namespace Crescent
