#include "AnimationPose.hpp"
#include <algorithm>
#include <cmath>

namespace Crescent {
namespace {

static void DecomposeTRS(const Math::Matrix4x4& matrix,
                         Math::Vector3& outPos,
                         Math::Quaternion& outRot,
                         Math::Vector3& outScale) {
    outPos = Math::Vector3(matrix.m[12], matrix.m[13], matrix.m[14]);

    Math::Vector3 col0(matrix.m[0], matrix.m[1], matrix.m[2]);
    Math::Vector3 col1(matrix.m[4], matrix.m[5], matrix.m[6]);
    Math::Vector3 col2(matrix.m[8], matrix.m[9], matrix.m[10]);

    outScale = Math::Vector3(col0.length(), col1.length(), col2.length());
    if (outScale.x == 0.0f) outScale.x = 1.0f;
    if (outScale.y == 0.0f) outScale.y = 1.0f;
    if (outScale.z == 0.0f) outScale.z = 1.0f;

    Math::Vector3 r0 = col0 / outScale.x;
    Math::Vector3 r1 = col1 / outScale.y;
    Math::Vector3 r2 = col2 / outScale.z;

    float m00 = r0.x, m01 = r1.x, m02 = r2.x;
    float m10 = r0.y, m11 = r1.y, m12 = r2.y;
    float m20 = r0.z, m21 = r1.z, m22 = r2.z;

    float trace = m00 + m11 + m22;
    if (trace > 0.0f) {
        float s = std::sqrt(trace + 1.0f) * 2.0f;
        outRot.w = 0.25f * s;
        outRot.x = (m21 - m12) / s;
        outRot.y = (m02 - m20) / s;
        outRot.z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        outRot.w = (m21 - m12) / s;
        outRot.x = 0.25f * s;
        outRot.y = (m01 + m10) / s;
        outRot.z = (m02 + m20) / s;
    } else if (m11 > m22) {
        float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        outRot.w = (m02 - m20) / s;
        outRot.x = (m01 + m10) / s;
        outRot.y = 0.25f * s;
        outRot.z = (m12 + m21) / s;
    } else {
        float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        outRot.w = (m10 - m01) / s;
        outRot.x = (m02 + m20) / s;
        outRot.y = (m12 + m21) / s;
        outRot.z = 0.25f * s;
    }
    outRot.normalize();
}

static Math::Vector3 SampleVectorKeys(const std::vector<VectorKeyframe>& keys,
                                      float time,
                                      const Math::Vector3& fallback) {
    if (keys.empty()) {
        return fallback;
    }
    if (keys.size() == 1) {
        return keys[0].value;
    }
    if (time <= keys.front().time) {
        return keys.front().value;
    }
    if (time >= keys.back().time) {
        return keys.back().value;
    }
    auto upper = std::upper_bound(keys.begin(), keys.end(), time,
                                  [](float value, const VectorKeyframe& key) {
                                      return value < key.time;
                                  });
    size_t index = 0;
    if (upper != keys.begin()) {
        index = static_cast<size_t>(std::distance(keys.begin(), upper) - 1);
    }
    const auto& a = keys[index];
    const auto& b = keys[index + 1];
    float span = b.time - a.time;
    float t = (span > 0.0f) ? (time - a.time) / span : 0.0f;
    return Math::Vector3::Lerp(a.value, b.value, t);
}

static Math::Quaternion SampleRotationKeys(const std::vector<QuaternionKeyframe>& keys,
                                           float time,
                                           const Math::Quaternion& fallback) {
    if (keys.empty()) {
        return fallback;
    }
    if (keys.size() == 1) {
        return keys[0].value;
    }
    if (time <= keys.front().time) {
        return keys.front().value;
    }
    if (time >= keys.back().time) {
        return keys.back().value;
    }
    auto upper = std::upper_bound(keys.begin(), keys.end(), time,
                                  [](float value, const QuaternionKeyframe& key) {
                                      return value < key.time;
                                  });
    size_t index = 0;
    if (upper != keys.begin()) {
        index = static_cast<size_t>(std::distance(keys.begin(), upper) - 1);
    }
    const auto& a = keys[index];
    const auto& b = keys[index + 1];
    float span = b.time - a.time;
    float t = (span > 0.0f) ? (time - a.time) / span : 0.0f;
    return Math::Quaternion::Slerp(a.value, b.value, t);
}

static float ResolveClipTimeTicks(const AnimationClip* clip, float timeSeconds, bool looping) {
    if (!clip) {
        return 0.0f;
    }
    float ticksPerSecond = clip->getTicksPerSecond();
    if (ticksPerSecond <= 0.0f) {
        ticksPerSecond = 25.0f;
    }
    float timeTicks = timeSeconds * ticksPerSecond;
    float durationTicks = clip->getDurationTicks();
    if (durationTicks > 0.0f) {
        if (looping) {
            timeTicks = std::fmod(timeTicks, durationTicks);
            if (timeTicks < 0.0f) {
                timeTicks += durationTicks;
            }
        } else {
            timeTicks = std::clamp(timeTicks, 0.0f, durationTicks);
        }
    }
    return timeTicks;
}

} // namespace

void SampleLocalPose(const Skeleton& skeleton,
                     const AnimationClip* clip,
                     float timeSeconds,
                     bool looping,
                     AnimationLocalPose& outPose) {
    const auto& bones = skeleton.getBones();
    if (bones.empty()) {
        outPose.resize(0);
        return;
    }

    outPose.resize(bones.size());
    float timeTicks = ResolveClipTimeTicks(clip, timeSeconds, looping);

    for (size_t i = 0; i < bones.size(); ++i) {
        const Bone& bone = bones[i];
        Math::Vector3 basePos;
        Math::Quaternion baseRot;
        Math::Vector3 baseScale;
        DecomposeTRS(bone.localBind, basePos, baseRot, baseScale);

        const AnimationChannel* channel = clip ? clip->findChannelByBoneIndex(static_cast<int>(i)) : nullptr;
        if (channel) {
            outPose.positions[i] = SampleVectorKeys(channel->positionKeys, timeTicks, basePos);
            outPose.rotations[i] = SampleRotationKeys(channel->rotationKeys, timeTicks, baseRot);
            outPose.scales[i] = SampleVectorKeys(channel->scaleKeys, timeTicks, baseScale);
        } else {
            outPose.positions[i] = basePos;
            outPose.rotations[i] = baseRot;
            outPose.scales[i] = baseScale;
        }
    }
}

void BlendLocalPose(const AnimationLocalPose& a,
                    const AnimationLocalPose& b,
                    float t,
                    AnimationLocalPose& outPose) {
    size_t count = std::min({a.positions.size(), b.positions.size(), a.rotations.size(), b.rotations.size(), a.scales.size(), b.scales.size()});
    if (count == 0) {
        outPose.resize(0);
        return;
    }
    float blend = Math::Clamp(t, 0.0f, 1.0f);
    outPose.resize(count);
    for (size_t i = 0; i < count; ++i) {
        outPose.positions[i] = Math::Vector3::Lerp(a.positions[i], b.positions[i], blend);
        outPose.rotations[i] = Math::Quaternion::Slerp(a.rotations[i], b.rotations[i], blend);
        outPose.scales[i] = Math::Vector3::Lerp(a.scales[i], b.scales[i], blend);
    }
}

void BuildSkinMatrices(const Skeleton& skeleton,
                       const AnimationLocalPose& pose,
                       std::vector<Math::Matrix4x4>& outMatrices) {
    const auto& bones = skeleton.getBones();
    size_t boneCount = bones.size();
    if (boneCount == 0) {
        outMatrices.clear();
        return;
    }
    const Math::Matrix4x4& globalInverse = skeleton.getGlobalInverse();

    std::vector<Math::Matrix4x4> localPose(boneCount);
    std::vector<Math::Matrix4x4> globalPose(boneCount);

    for (size_t i = 0; i < boneCount; ++i) {
        localPose[i] = Math::Matrix4x4::TRS(pose.positions[i], pose.rotations[i], pose.scales[i]);
    }

    for (size_t i = 0; i < boneCount; ++i) {
        const Bone& bone = bones[i];
        if (bone.parentIndex < 0) {
            globalPose[i] = localPose[i];
        } else {
            size_t parentIndex = static_cast<size_t>(bone.parentIndex);
            globalPose[i] = globalPose[parentIndex] * localPose[i];
        }
    }

    outMatrices.resize(boneCount);
    for (size_t i = 0; i < boneCount; ++i) {
        outMatrices[i] = globalInverse * globalPose[i] * bones[i].inverseBind;
    }
}

void BuildGlobalPose(const Skeleton& skeleton,
                     const AnimationLocalPose& pose,
                     std::vector<Math::Matrix4x4>& outMatrices) {
    const auto& bones = skeleton.getBones();
    size_t boneCount = bones.size();
    if (boneCount == 0) {
        outMatrices.clear();
        return;
    }

    std::vector<Math::Matrix4x4> localPose(boneCount);
    outMatrices.resize(boneCount);

    for (size_t i = 0; i < boneCount; ++i) {
        localPose[i] = Math::Matrix4x4::TRS(pose.positions[i], pose.rotations[i], pose.scales[i]);
    }

    for (size_t i = 0; i < boneCount; ++i) {
        const Bone& bone = bones[i];
        if (bone.parentIndex < 0) {
            outMatrices[i] = localPose[i];
        } else {
            size_t parentIndex = static_cast<size_t>(bone.parentIndex);
            outMatrices[i] = outMatrices[parentIndex] * localPose[i];
        }
    }
}

} // namespace Crescent
