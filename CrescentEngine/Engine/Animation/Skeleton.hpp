#pragma once

#include "../Math/Math.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <limits>

namespace Crescent {

struct Bone {
    std::string name;
    int parentIndex;
    Math::Matrix4x4 localBind;
    Math::Matrix4x4 inverseBind;

    Bone()
        : name("")
        , parentIndex(-1)
        , localBind(Math::Matrix4x4::Identity)
        , inverseBind(Math::Matrix4x4::Identity) {}
};

class Skeleton {
public:
    static constexpr uint32_t InvalidIndex = std::numeric_limits<uint32_t>::max();

    Skeleton();

    uint32_t addBone(const std::string& name,
                     int parentIndex,
                     const Math::Matrix4x4& localBind,
                     const Math::Matrix4x4& inverseBind);

    uint32_t getBoneCount() const { return static_cast<uint32_t>(m_Bones.size()); }
    uint32_t getRootIndex() const { return m_RootIndex; }
    void setRootIndex(uint32_t index) { m_RootIndex = index; }

    const std::vector<Bone>& getBones() const { return m_Bones; }
    const Bone* getBone(uint32_t index) const;
    int getBoneIndex(const std::string& name) const;

private:
    std::vector<Bone> m_Bones;
    std::unordered_map<std::string, uint32_t> m_BoneLookup;
    uint32_t m_RootIndex;
};

} // namespace Crescent
