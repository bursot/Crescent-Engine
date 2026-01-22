#include "SkinnedMeshRenderer.hpp"
#include "../ECS/Entity.hpp"
#include "../ECS/Transform.hpp"
#include "../Math/Math.hpp"
#include "../Core/Time.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>

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

static std::string ToLower(const std::string& value) {
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lowered;
}

static bool ContainsToken(const std::string& value, const std::vector<std::string>& tokens) {
    for (const auto& token : tokens) {
        if (!token.empty() && value.find(token) != std::string::npos) {
            return true;
        }
    }
    return false;
}

static int FindBoneByTokens(const std::vector<Bone>& bones,
                            const std::vector<std::string>& tokens,
                            int parentFilter) {
    for (size_t i = 0; i < bones.size(); ++i) {
        if (parentFilter >= 0 && bones[i].parentIndex != parentFilter) {
            continue;
        }
        std::string name = ToLower(bones[i].name);
        if (ContainsToken(name, tokens)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

static int ResolveRootMotionBoneIndex(const Skeleton& skeleton,
                                      const AnimationLocalPose* pose) {
    const auto& bones = skeleton.getBones();
    if (bones.empty()) {
        return -1;
    }
    int root = static_cast<int>(skeleton.getRootIndex());
    if (root < 0 || root >= static_cast<int>(bones.size())) {
        return -1;
    }
    std::string rootName = ToLower(bones[root].name);
    bool rootMoves = false;
    if (pose && root < static_cast<int>(pose->positions.size())) {
        Math::Vector3 basePos;
        Math::Quaternion baseRot;
        Math::Vector3 baseScale;
        DecomposeTRS(bones[root].localBind, basePos, baseRot, baseScale);
        rootMoves = (pose->positions[root] - basePos).length() > 0.0005f;
    }
    bool genericRoot = !rootMoves && (rootName.empty() || ContainsToken(rootName, {"root", "armature", "scene"}));
    if (genericRoot) {
        int direct = FindBoneByTokens(bones, {"hips", "pelvis", "hip"}, root);
        if (direct >= 0) {
            return direct;
        }
        int any = FindBoneByTokens(bones, {"hips", "pelvis", "hip"}, -1);
        if (any >= 0) {
            return any;
        }
    }
    return root;
}

static Math::Matrix4x4 BuildGlobalPose(const Skeleton& skeleton,
                                       const AnimationLocalPose& pose,
                                       int boneIndex) {
    Math::Matrix4x4 global = Math::Matrix4x4::Identity;
    if (boneIndex < 0) {
        return global;
    }
    const auto& bones = skeleton.getBones();
    std::vector<int> chain;
    for (int idx = boneIndex; idx >= 0; idx = bones[idx].parentIndex) {
        chain.push_back(idx);
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        int idx = *it;
        global = global * Math::Matrix4x4::TRS(pose.positions[idx], pose.rotations[idx], pose.scales[idx]);
    }
    return global;
}

} // namespace

SkinnedMeshRenderer::SkinnedMeshRenderer()
    : m_Playing(false)
    , m_Looping(true)
    , m_PlaybackSpeed(1.0f)
    , m_TimeSeconds(0.0f)
    , m_ActiveClipIndex(-1)
    , m_BlendTimeSeconds(0.0f)
    , m_BlendDuration(0.0f)
    , m_BlendElapsed(0.0f)
    , m_BlendClipIndex(-1)
    , m_DrivenByAnimator(false) {
}

void SkinnedMeshRenderer::setMesh(std::shared_ptr<Mesh> mesh) {
    m_Mesh = mesh;
}

void SkinnedMeshRenderer::setSkeleton(std::shared_ptr<Skeleton> skeleton) {
    m_Skeleton = skeleton;
    if (m_Skeleton) {
        m_BoneMatrices.assign(m_Skeleton->getBoneCount(), Math::Matrix4x4::Identity);
        m_PrevBoneMatrices = m_BoneMatrices;
        m_RootMotionValid = false;
        m_PrevRootTime = 0.0f;
        for (const auto& clip : m_Clips) {
            if (clip) {
                clip->rebindToSkeleton(*m_Skeleton);
            }
        }
    } else {
        m_BoneMatrices.clear();
        m_PrevBoneMatrices.clear();
        m_RootMotionValid = false;
        m_PrevRootTime = 0.0f;
    }
}

void SkinnedMeshRenderer::setAnimationClip(std::shared_ptr<AnimationClip> clip) {
    m_Clip = clip;
    m_TimeSeconds = 0.0f;
    m_ActiveClipIndex = -1;
    m_BlendClip.reset();
    m_BlendDuration = 0.0f;
    m_BlendElapsed = 0.0f;
    m_BlendClipIndex = -1;
    m_RootMotionValid = false;
    m_PrevRootTime = 0.0f;
    if (clip && !m_Clips.empty()) {
        for (size_t i = 0; i < m_Clips.size(); ++i) {
            if (m_Clips[i] == clip) {
                m_ActiveClipIndex = static_cast<int>(i);
                break;
            }
        }
    }
}

void SkinnedMeshRenderer::setAnimationClips(const std::vector<std::shared_ptr<AnimationClip>>& clips) {
    m_Clips = clips;
    m_BlendClip.reset();
    m_BlendDuration = 0.0f;
    m_BlendElapsed = 0.0f;
    m_BlendClipIndex = -1;
    m_RootMotionValid = false;
    m_PrevRootTime = 0.0f;
    if (m_Skeleton) {
        for (const auto& clip : m_Clips) {
            if (clip) {
                clip->rebindToSkeleton(*m_Skeleton);
            }
        }
    }
    if (m_Clips.empty()) {
        m_Clip.reset();
        m_ActiveClipIndex = -1;
        return;
    }
    if (m_ActiveClipIndex < 0 || m_ActiveClipIndex >= static_cast<int>(m_Clips.size())) {
        m_ActiveClipIndex = 0;
    }
    m_Clip = m_Clips[static_cast<size_t>(m_ActiveClipIndex)];
    m_TimeSeconds = 0.0f;
}

bool SkinnedMeshRenderer::setActiveClipIndex(int index) {
    if (index < 0 || index >= static_cast<int>(m_Clips.size())) {
        return false;
    }
    m_ActiveClipIndex = index;
    m_Clip = m_Clips[static_cast<size_t>(index)];
    m_TimeSeconds = 0.0f;
    m_BlendClip.reset();
    m_BlendDuration = 0.0f;
    m_BlendElapsed = 0.0f;
    m_BlendClipIndex = -1;
    m_RootMotionValid = false;
    m_PrevRootTime = 0.0f;
    return true;
}

bool SkinnedMeshRenderer::crossFadeToClip(int index, float durationSeconds, bool restart) {
    if (index < 0 || index >= static_cast<int>(m_Clips.size())) {
        return false;
    }
    std::shared_ptr<AnimationClip> target = m_Clips[static_cast<size_t>(index)];
    if (!target) {
        return false;
    }
    if (!m_Clip || durationSeconds <= 0.0f || m_Clip == target) {
        m_Clip = target;
        m_ActiveClipIndex = index;
        if (restart) {
            m_TimeSeconds = 0.0f;
        }
        m_BlendClip.reset();
        m_BlendDuration = 0.0f;
        m_BlendElapsed = 0.0f;
        m_BlendClipIndex = -1;
        m_RootMotionValid = false;
        m_PrevRootTime = 0.0f;
        return true;
    }

    m_BlendClip = target;
    m_BlendClipIndex = index;
    m_BlendDuration = std::max(0.0f, durationSeconds);
    m_BlendElapsed = 0.0f;
    m_BlendTimeSeconds = restart ? 0.0f : m_TimeSeconds;
    m_RootMotionValid = false;
    m_PrevRootTime = 0.0f;
    return true;
}

void SkinnedMeshRenderer::setMaterial(std::shared_ptr<Material> material) {
    if (m_Materials.empty()) {
        m_Materials.push_back(material);
    } else {
        m_Materials[0] = material;
    }
}

void SkinnedMeshRenderer::setMaterial(uint32_t index, std::shared_ptr<Material> material) {
    if (index >= m_Materials.size()) {
        m_Materials.resize(index + 1);
    }
    m_Materials[index] = material;
}

std::shared_ptr<Material> SkinnedMeshRenderer::getMaterial(uint32_t index) const {
    if (index >= m_Materials.size()) {
        return nullptr;
    }
    return m_Materials[index];
}

void SkinnedMeshRenderer::applyBoneMatrices(const std::vector<Math::Matrix4x4>& matrices) {
    if (!m_BoneMatrices.empty()) {
        m_PrevBoneMatrices = m_BoneMatrices;
    } else if (m_Skeleton) {
        m_PrevBoneMatrices.assign(m_Skeleton->getBoneCount(), Math::Matrix4x4::Identity);
    }
    m_BoneMatrices = matrices;
}

void SkinnedMeshRenderer::applyRootMotion(AnimationLocalPose& pose, float sampleTime) {
    if (!m_RootMotionEnabled || !m_Skeleton) {
        return;
    }
    if (pose.positions.empty()) {
        return;
    }
    int motionIdx = ResolveRootMotionBoneIndex(*m_Skeleton, &pose);
    if (motionIdx < 0 || motionIdx >= static_cast<int>(pose.positions.size())) {
        return;
    }
    const auto& bones = m_Skeleton->getBones();
    if (motionIdx >= static_cast<int>(bones.size())) {
        return;
    }

    Math::Matrix4x4 global = BuildGlobalPose(*m_Skeleton, pose, motionIdx);
    Math::Vector3 currentPos;
    Math::Quaternion currentRot;
    Math::Vector3 currentScale;
    DecomposeTRS(global, currentPos, currentRot, currentScale);

    if (!m_RootMotionValid || sampleTime < m_PrevRootTime) {
        m_PrevRootPos = currentPos;
        m_PrevRootRot = currentRot;
        m_RootMotionValid = true;
    } else {
        Math::Vector3 deltaPos = currentPos - m_PrevRootPos;
        Math::Quaternion deltaRot = currentRot * m_PrevRootRot.inverse();
        Transform* transform = getEntity() ? getEntity()->getTransform() : nullptr;
        if (transform) {
            if (m_RootMotionApplyPosition) {
                transform->translate(deltaPos, true);
            }
            if (m_RootMotionApplyRotation) {
                transform->rotate(deltaRot, true);
            }
        }
        m_PrevRootPos = currentPos;
        m_PrevRootRot = currentRot;
    }
    m_PrevRootTime = sampleTime;

    Math::Vector3 basePos;
    Math::Quaternion baseRot;
    Math::Vector3 baseScale;
    DecomposeTRS(bones[motionIdx].localBind, basePos, baseRot, baseScale);
    pose.positions[motionIdx] = basePos;
    pose.rotations[motionIdx] = baseRot;
    pose.scales[motionIdx] = baseScale;
}

void SkinnedMeshRenderer::OnUpdate(float deltaTime) {
    if (m_DrivenByAnimator) {
        return;
    }

    if (!m_BoneMatrices.empty()) {
        m_PrevBoneMatrices = m_BoneMatrices;
    } else if (m_Skeleton) {
        m_PrevBoneMatrices.assign(m_Skeleton->getBoneCount(), Math::Matrix4x4::Identity);
    }

    constexpr float kMaxAnimDelta = 0.05f;
    constexpr float kAnimSmoothing = 0.2f;
    constexpr int kMaxAnimSteps = 5;
    float animDelta = std::clamp(deltaTime, 0.0f, kMaxAnimDelta);
    if (m_SmoothedDelta <= 0.0f) {
        m_SmoothedDelta = animDelta;
    } else {
        m_SmoothedDelta += (animDelta - m_SmoothedDelta) * kAnimSmoothing;
    }
    animDelta = m_SmoothedDelta;

    float fixedStep = std::max(0.0001f, Time::fixedDeltaTime());
    m_AnimAccumulator += animDelta;
    float maxAccumulator = fixedStep * static_cast<float>(kMaxAnimSteps);
    if (m_AnimAccumulator > maxAccumulator) {
        m_AnimAccumulator = maxAccumulator;
    }

    if (!m_Skeleton) {
        return;
    }

    const auto& bones = m_Skeleton->getBones();
    if (bones.empty()) {
        return;
    }

    auto stepAnimation = [&](float stepDelta) {
        if (m_Playing && m_Clip) {
            float duration = m_Clip->getDurationSeconds();
            if (duration > 0.0f) {
                m_TimeSeconds += stepDelta * m_PlaybackSpeed;
                if (m_Looping) {
                    while (m_TimeSeconds >= duration) {
                        m_TimeSeconds -= duration;
                    }
                } else if (m_TimeSeconds > duration) {
                    m_TimeSeconds = duration;
                    if (!m_BlendClip) {
                        m_Playing = false;
                    }
                }
            }
        }
        if (m_Playing && m_BlendClip) {
            float duration = m_BlendClip->getDurationSeconds();
            if (duration > 0.0f) {
                m_BlendTimeSeconds += stepDelta * m_PlaybackSpeed;
                if (m_Looping) {
                    while (m_BlendTimeSeconds >= duration) {
                        m_BlendTimeSeconds -= duration;
                    }
                } else if (m_BlendTimeSeconds > duration) {
                    m_BlendTimeSeconds = duration;
                }
            }
        }
        if (m_Playing && m_BlendClip && m_BlendDuration > 0.0f) {
            m_BlendElapsed = std::min(m_BlendElapsed + stepDelta, m_BlendDuration);
        }

        AnimationLocalPose* outputPose = nullptr;
        if (m_BlendClip && m_BlendDuration > 0.0f) {
            float blend = (m_BlendDuration > 0.0f)
                ? Math::Clamp(m_BlendElapsed / m_BlendDuration, 0.0f, 1.0f)
                : 1.0f;
            SampleLocalPose(*m_Skeleton, m_Clip.get(), m_TimeSeconds, m_Looping, m_LocalPose);
            SampleLocalPose(*m_Skeleton, m_BlendClip.get(), m_BlendTimeSeconds, m_Looping, m_BlendPose);
            BlendLocalPose(m_LocalPose, m_BlendPose, blend, m_BlendResultPose);
            applyRootMotion(m_BlendResultPose, m_TimeSeconds);
            outputPose = &m_BlendResultPose;
            if (blend >= 0.999f) {
                m_Clip = m_BlendClip;
                m_ActiveClipIndex = m_BlendClipIndex;
                m_TimeSeconds = m_BlendTimeSeconds;
                m_BlendClip.reset();
                m_BlendDuration = 0.0f;
                m_BlendElapsed = 0.0f;
                m_BlendClipIndex = -1;
            }
        } else {
            SampleLocalPose(*m_Skeleton, m_Clip.get(), m_TimeSeconds, m_Looping, m_LocalPose);
            applyRootMotion(m_LocalPose, m_TimeSeconds);
            outputPose = &m_LocalPose;
        }

        if (outputPose) {
            if (!m_HasPose) {
                m_CurrentPose = *outputPose;
                m_PrevPose = m_CurrentPose;
                m_HasPose = true;
            } else {
                m_PrevPose = m_CurrentPose;
                m_CurrentPose = *outputPose;
            }
        }
    };

    int steps = 0;
    while (m_AnimAccumulator >= fixedStep && steps < kMaxAnimSteps) {
        m_AnimAccumulator -= fixedStep;
        stepAnimation(fixedStep);
        steps++;
    }
    if (!m_HasPose) {
        stepAnimation(0.0f);
    }

    float alpha = fixedStep > 0.0f ? (m_AnimAccumulator / fixedStep) : 0.0f;
    if (m_HasPose) {
        BlendLocalPose(m_PrevPose, m_CurrentPose, alpha, m_RenderPose);
        BuildSkinMatrices(*m_Skeleton, m_RenderPose, m_BoneMatrices);
    }
}

} // namespace Crescent
