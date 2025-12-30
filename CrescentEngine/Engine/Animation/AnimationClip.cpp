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
}

void AnimationClip::addEvent(const AnimationEvent& event) {
    m_Events.push_back(event);
}

AnimationChannel* AnimationClip::findChannelByBoneIndex(int boneIndex) {
    for (auto& channel : m_Channels) {
        if (channel.boneIndex == boneIndex) {
            return &channel;
        }
    }
    return nullptr;
}

const AnimationChannel* AnimationClip::findChannelByBoneIndex(int boneIndex) const {
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
}

} // namespace Crescent
