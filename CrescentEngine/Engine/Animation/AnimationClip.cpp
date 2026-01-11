#include "AnimationClip.hpp"
#include "Skeleton.hpp"

namespace Crescent {

AnimationClip::AnimationClip()
    : m_Name("")
    , m_DurationTicks(0.0f)
    , m_TicksPerSecond(25.0f) {
}

float AnimationClip::getDurationSeconds() const {
    if (m_TicksPerSecond <= 0.0f) {
        return 0.0f;
    }
    return m_DurationTicks / m_TicksPerSecond;
}

void AnimationClip::addChannel(const AnimationChannel& channel) {
    m_Channels.push_back(channel);
    m_ChannelIndexByBone.clear();
}

void AnimationClip::addEvent(const AnimationEvent& event) {
    m_Events.push_back(event);
}

AnimationChannel* AnimationClip::findChannelByBoneIndex(int boneIndex) {
    if (!m_ChannelIndexByBone.empty()) {
        if (boneIndex >= 0 && boneIndex < static_cast<int>(m_ChannelIndexByBone.size())) {
            int index = m_ChannelIndexByBone[static_cast<size_t>(boneIndex)];
            if (index >= 0 && index < static_cast<int>(m_Channels.size())) {
                return &m_Channels[static_cast<size_t>(index)];
            }
            return nullptr;
        }
    }
    for (auto& channel : m_Channels) {
        if (channel.boneIndex == boneIndex) {
            return &channel;
        }
    }
    return nullptr;
}

const AnimationChannel* AnimationClip::findChannelByBoneIndex(int boneIndex) const {
    if (!m_ChannelIndexByBone.empty()) {
        if (boneIndex >= 0 && boneIndex < static_cast<int>(m_ChannelIndexByBone.size())) {
            int index = m_ChannelIndexByBone[static_cast<size_t>(boneIndex)];
            if (index >= 0 && index < static_cast<int>(m_Channels.size())) {
                return &m_Channels[static_cast<size_t>(index)];
            }
            return nullptr;
        }
    }
    for (const auto& channel : m_Channels) {
        if (channel.boneIndex == boneIndex) {
            return &channel;
        }
    }
    return nullptr;
}

void AnimationClip::rebindToSkeleton(const Skeleton& skeleton) {
    for (auto& channel : m_Channels) {
        if (!channel.boneName.empty()) {
            channel.boneIndex = skeleton.getBoneIndex(channel.boneName);
        }
    }
    m_ChannelIndexByBone.assign(skeleton.getBoneCount(), -1);
    for (size_t i = 0; i < m_Channels.size(); ++i) {
        int boneIndex = m_Channels[i].boneIndex;
        if (boneIndex >= 0 && boneIndex < static_cast<int>(m_ChannelIndexByBone.size())) {
            m_ChannelIndexByBone[static_cast<size_t>(boneIndex)] = static_cast<int>(i);
        }
    }
}

} // namespace Crescent
