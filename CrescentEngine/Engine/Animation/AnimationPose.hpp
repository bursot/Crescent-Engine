#pragma once

#include "Skeleton.hpp"
#include "AnimationClip.hpp"
#include "../Math/Math.hpp"
#include <vector>

namespace Crescent {

struct AnimationLocalPose {
    std::vector<Math::Vector3> positions;
    std::vector<Math::Quaternion> rotations;
    std::vector<Math::Vector3> scales;

    void resize(size_t count) {
        positions.resize(count);
        rotations.resize(count);
        scales.resize(count);
    }
};

void SampleLocalPose(const Skeleton& skeleton,
                     const AnimationClip* clip,
                     float timeSeconds,
                     bool looping,
                     AnimationLocalPose& outPose);

void BlendLocalPose(const AnimationLocalPose& a,
                    const AnimationLocalPose& b,
                    float t,
                    AnimationLocalPose& outPose);

void BuildSkinMatrices(const Skeleton& skeleton,
                       const AnimationLocalPose& pose,
                       std::vector<Math::Matrix4x4>& outMatrices);

void BuildGlobalPose(const Skeleton& skeleton,
                     const AnimationLocalPose& pose,
                     std::vector<Math::Matrix4x4>& outMatrices);

} // namespace Crescent
