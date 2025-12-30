#pragma once

#include "../Math/Math.hpp"
#include <string>
#include <vector>

namespace Crescent {

struct AnimationEvent {
    float time = 0.0f;
    std::string name;
};

struct VectorKeyframe {
    float time;
    Math::Vector3 value;
};

struct QuaternionKeyframe {
    float time;
    Math::Quaternion value;
};

struct AnimationChannel {
    std::string boneName;
    int boneIndex;
    std::vector<VectorKeyframe> positionKeys;
    std::vector<QuaternionKeyframe> rotationKeys;
    std::vector<VectorKeyframe> scaleKeys;

    AnimationChannel()
        : boneName("")
        , boneIndex(-1) {}
};

class AnimationClip {
public:
    AnimationClip();

    const std::string& getName() const { return m_Name; }
    void setName(const std::string& name) { m_Name = name; }

    float getDurationTicks() const { return m_DurationTicks; }
    void setDurationTicks(float duration) { m_DurationTicks = duration; }

    float getTicksPerSecond() const { return m_TicksPerSecond; }
    void setTicksPerSecond(float tps) { m_TicksPerSecond = tps; }

    float getDurationSeconds() const;

    const std::vector<AnimationChannel>& getChannels() const { return m_Channels; }
    void addChannel(const AnimationChannel& channel);

    AnimationChannel* findChannelByBoneIndex(int boneIndex);
    const AnimationChannel* findChannelByBoneIndex(int boneIndex) const;

    const std::vector<AnimationEvent>& getEvents() const { return m_Events; }
    void addEvent(const AnimationEvent& event);
    void clearEvents() { m_Events.clear(); }

    void rebindToSkeleton(const class Skeleton& skeleton);

private:
    std::string m_Name;
    float m_DurationTicks;
    float m_TicksPerSecond;
    std::vector<AnimationChannel> m_Channels;
    std::vector<AnimationEvent> m_Events;
};

} // namespace Crescent
