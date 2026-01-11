#include "Skeleton.hpp"

namespace Crescent {

Skeleton::Skeleton()
    : m_RootIndex(InvalidIndex)
    , m_GlobalInverse(Math::Matrix4x4::Identity) {
}

uint32_t Skeleton::addBone(const std::string& name,
                           int parentIndex,
                           const Math::Matrix4x4& localBind,
                           const Math::Matrix4x4& inverseBind) {
    uint32_t index = static_cast<uint32_t>(m_Bones.size());
    Bone bone;
    bone.name = name;
    bone.parentIndex = parentIndex;
    bone.localBind = localBind;
    bone.inverseBind = inverseBind;
    m_Bones.push_back(bone);
    m_BoneLookup[name] = index;
    if (m_RootIndex == InvalidIndex && parentIndex < 0) {
        m_RootIndex = index;
    }
    return index;
}

const Bone* Skeleton::getBone(uint32_t index) const {
    if (index >= m_Bones.size()) {
        return nullptr;
    }
    return &m_Bones[index];
}

int Skeleton::getBoneIndex(const std::string& name) const {
    auto it = m_BoneLookup.find(name);
    if (it == m_BoneLookup.end()) {
        return -1;
    }
    return static_cast<int>(it->second);
}

} // namespace Crescent
