#pragma once

#include "../ECS/Component.hpp"
#include <string>
#include <vector>

namespace Crescent {

class HLODProxy : public Component {
public:
    HLODProxy();
    virtual ~HLODProxy() = default;

    COMPONENT_TYPE(HLODProxy)

    void setSourceUuids(const std::vector<std::string>& uuids) { m_SourceUuids = uuids; }
    const std::vector<std::string>& getSourceUuids() const { return m_SourceUuids; }

    void addSourceUuid(const std::string& uuid) { m_SourceUuids.push_back(uuid); }

    void setLodStart(float start) { m_LodStart = start; }
    float getLodStart() const { return m_LodStart; }

    void setLodEnd(float end) { m_LodEnd = end; }
    float getLodEnd() const { return m_LodEnd; }

    void setEnabled(bool enabled) { m_Enabled = enabled; }
    bool isEnabled() const { return m_Enabled; }

private:
    std::vector<std::string> m_SourceUuids;
    float m_LodStart;
    float m_LodEnd;
    bool m_Enabled;
};

} // namespace Crescent
