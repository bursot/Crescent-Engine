#include "Animator.hpp"
#include "../ECS/Entity.hpp"
#include "../ECS/Transform.hpp"
#include "../Animation/AnimationPose.hpp"
#include "../Components/IKConstraint.hpp"
#include "../Core/Time.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace Crescent {
namespace {

const AnimatorParameter* FindParam(const std::vector<AnimatorParameter>& params, const std::string& name) {
    auto it = std::find_if(params.begin(), params.end(), [&](const AnimatorParameter& p) {
        return p.name == name;
    });
    return (it != params.end()) ? &(*it) : nullptr;
}

AnimatorParameter* FindParamMutable(std::vector<AnimatorParameter>& params, const std::string& name) {
    auto it = std::find_if(params.begin(), params.end(), [&](const AnimatorParameter& p) {
        return p.name == name;
    });
    return (it != params.end()) ? &(*it) : nullptr;
}

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

static void CollectEvents(const AnimationClip* clip,
                          float prevTime,
                          float currentTime,
                          bool looping,
                          std::vector<AnimationEvent>& outEvents) {
    if (!clip) return;
    const auto& events = clip->getEvents();
    if (events.empty()) return;
    float duration = clip->getDurationSeconds();
    if (duration <= 0.0f) return;

    if (!looping || currentTime >= prevTime) {
        for (const auto& evt : events) {
            if (evt.time > prevTime && evt.time <= currentTime) {
                outEvents.push_back(evt);
            }
        }
    } else {
        for (const auto& evt : events) {
            if (evt.time > prevTime || evt.time <= currentTime) {
                outEvents.push_back(evt);
            }
        }
    }
}

static void ApplyTwoBoneIK(const Skeleton& skeleton,
                           AnimationLocalPose& pose,
                           int rootIdx,
                           int midIdx,
                           int endIdx,
                           const Math::Vector3& targetWorld,
                           float weight) {
    if (rootIdx < 0 || midIdx < 0 || endIdx < 0) {
        return;
    }
    if (rootIdx >= static_cast<int>(pose.positions.size()) ||
        midIdx >= static_cast<int>(pose.positions.size()) ||
        endIdx >= static_cast<int>(pose.positions.size())) {
        return;
    }
    float w = Math::Clamp(weight, 0.0f, 1.0f);
    if (w <= 0.0001f) {
        return;
    }

    std::vector<Math::Matrix4x4> globals;
    for (int iter = 0; iter < 4; ++iter) {
        BuildGlobalPose(skeleton, pose, globals);

        auto applyBone = [&](int boneIdx) {
            const Math::Matrix4x4& boneM = globals[static_cast<size_t>(boneIdx)];
            const Math::Matrix4x4& endM = globals[static_cast<size_t>(endIdx)];
            Math::Vector3 bonePos(boneM.m[12], boneM.m[13], boneM.m[14]);
            Math::Vector3 endPos(endM.m[12], endM.m[13], endM.m[14]);
            Math::Vector3 toEnd = (endPos - bonePos).normalized();
            Math::Vector3 toTarget = (targetWorld - bonePos).normalized();
            float cosAngle = Math::Clamp(toEnd.dot(toTarget), -1.0f, 1.0f);
            if (cosAngle > 0.9995f) {
                return;
            }
            Math::Vector3 axis = toEnd.cross(toTarget);
            if (axis.length() < 0.0001f) {
                return;
            }
            float angle = std::acos(cosAngle) * w;
            Math::Quaternion delta = Math::Quaternion::FromAxisAngle(axis.normalized(), angle);

            Math::Vector3 dummyPos;
            Math::Quaternion boneRot;
            Math::Vector3 dummyScale;
            DecomposeTRS(boneM, dummyPos, boneRot, dummyScale);

            Math::Quaternion parentRot = Math::Quaternion::Identity;
            int parentIdx = skeleton.getBones()[static_cast<size_t>(boneIdx)].parentIndex;
            if (parentIdx >= 0) {
                const Math::Matrix4x4& parentM = globals[static_cast<size_t>(parentIdx)];
                DecomposeTRS(parentM, dummyPos, parentRot, dummyScale);
            }

            Math::Quaternion newGlobal = delta * boneRot;
            Math::Quaternion newLocal = parentRot.inverse() * newGlobal;
            newLocal.normalize();
            pose.rotations[static_cast<size_t>(boneIdx)] = newLocal;
        };

        applyBone(midIdx);
        applyBone(rootIdx);
    }
}

static void CollectSkinnedTargets(Entity* entity,
                                  const Animator* owner,
                                  std::vector<SkinnedMeshRenderer*>& out) {
    if (!entity) {
        return;
    }
    Animator* otherAnimator = entity->getComponent<Animator>();
    if (otherAnimator && otherAnimator != owner) {
        return;
    }
    if (auto* skinned = entity->getComponent<SkinnedMeshRenderer>()) {
        out.push_back(skinned);
    }
    Transform* transform = entity->getTransform();
    if (!transform) {
        return;
    }
    for (Transform* child : transform->getChildren()) {
        if (!child) {
            continue;
        }
        CollectSkinnedTargets(child->getEntity(), owner, out);
    }
}

} // namespace

Animator::Animator()
    : m_CurrentStateIndex(-1)
    , m_NextStateIndex(-1)
    , m_AutoPlay(false)
    , m_DefaultBlendDuration(0.25f)
    , m_NeedsApply(false)
    , m_InTransition(false)
    , m_TransitionElapsed(0.0f)
    , m_TransitionDuration(0.0f)
    , m_StateTime(0.0f)
    , m_NextStateTime(0.0f)
    , m_PrevStateTime(0.0f)
    , m_RootMotionEnabled(false)
    , m_RootMotionApplyPosition(true)
    , m_RootMotionApplyRotation(false)
    , m_RootMotionValid(false)
    , m_PrevRootTime(0.0f)
    , m_PrevRootPos(Math::Vector3(0.0f))
    , m_PrevRootRot(Math::Quaternion::Identity)
    , m_SmoothedDelta(0.0f) {
}

void Animator::setStates(const std::vector<AnimatorState>& states) {
    m_States = states;
    if (m_States.empty()) {
        m_CurrentStateIndex = -1;
        m_NeedsApply = false;
        return;
    }
    if (m_CurrentStateIndex < 0 || m_CurrentStateIndex >= static_cast<int>(m_States.size())) {
        m_CurrentStateIndex = 0;
        m_NeedsApply = true;
        m_StateTime = 0.0f;
        m_PrevStateTime = 0.0f;
    }
}

bool Animator::setParameterFloat(const std::string& name, float value) {
    if (AnimatorParameter* param = FindParamMutable(m_Parameters, name)) {
        param->floatValue = value;
        return true;
    }
    return false;
}

bool Animator::setParameterInt(const std::string& name, int value) {
    if (AnimatorParameter* param = FindParamMutable(m_Parameters, name)) {
        param->intValue = value;
        return true;
    }
    return false;
}

bool Animator::setParameterBool(const std::string& name, bool value) {
    if (AnimatorParameter* param = FindParamMutable(m_Parameters, name)) {
        param->boolValue = value;
        return true;
    }
    return false;
}

bool Animator::setTrigger(const std::string& name) {
    if (AnimatorParameter* param = FindParamMutable(m_Parameters, name)) {
        param->triggerValue = true;
        return true;
    }
    return false;
}

bool Animator::setCurrentStateIndex(int index, float blendDurationSeconds, bool restart) {
    if (index < 0 || index >= static_cast<int>(m_States.size())) {
        return false;
    }
    float blend = (blendDurationSeconds >= 0.0f) ? blendDurationSeconds : m_DefaultBlendDuration;
    if (!ApplyState(index, blend, restart)) {
        m_CurrentStateIndex = index;
        m_NeedsApply = true;
        return true;
    }
    m_CurrentStateIndex = index;
    m_NeedsApply = false;
    return true;
}

bool Animator::setCurrentStateByName(const std::string& name, float blendDurationSeconds, bool restart) {
    auto it = std::find_if(m_States.begin(), m_States.end(), [&](const AnimatorState& state) {
        return state.name == name;
    });
    if (it == m_States.end()) {
        return false;
    }
    int index = static_cast<int>(std::distance(m_States.begin(), it));
    return setCurrentStateIndex(index, blendDurationSeconds, restart);
}

void Animator::setDefaultBlendDuration(float seconds) {
    m_DefaultBlendDuration = std::max(0.0f, seconds);
}

void Animator::OnCreate() {
    std::vector<SkinnedMeshRenderer*> targets;
    SkinnedMeshRenderer* skinned = ResolveSkinnedWithTargets(targets);
    if (skinned) {
        for (auto* target : targets) {
            if (target) {
                target->setDrivenByAnimator(true);
            }
        }
    }
    if (m_AutoPlay && m_CurrentStateIndex >= 0) {
        if (skinned) {
            skinned->setPlaying(true);
        }
        ApplyState(m_CurrentStateIndex, 0.0f, true);
    }
}

void Animator::OnUpdate(float deltaTime) {
    std::vector<SkinnedMeshRenderer*> targets;
    SkinnedMeshRenderer* skinned = ResolveSkinnedWithTargets(targets);
    if (!skinned || m_States.empty()) {
        return;
    }
    if (!skinned->getSkeleton()) {
        return;
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

    const Skeleton& skeleton = *skinned->getSkeleton();
    const auto& clips = skinned->getAnimationClips();
    float fixedStep = std::max(0.0001f, Time::fixedDeltaTime());
    m_AnimAccumulator += animDelta;
    float maxAccumulator = fixedStep * static_cast<float>(kMaxAnimSteps);
    if (m_AnimAccumulator > maxAccumulator) {
        m_AnimAccumulator = maxAccumulator;
    }

    auto stepAnimation = [&](float stepDelta) {
        if (m_CurrentStateIndex < 0 || m_CurrentStateIndex >= static_cast<int>(m_States.size())) {
            m_CurrentStateIndex = 0;
        }
        const auto& state = m_States[static_cast<size_t>(m_CurrentStateIndex)];

        bool playing = skinned->isPlaying();
        float speed = state.speed * skinned->getPlaybackSpeed();
        float duration = ResolveStateDuration(state, clips);

        m_PrevStateTime = m_StateTime;
        if (playing && duration > 0.0f) {
            m_StateTime += stepDelta * speed;
            if (state.loop && skinned->isLooping()) {
                while (m_StateTime >= duration) {
                    m_StateTime -= duration;
                }
            } else {
                m_StateTime = std::min(m_StateTime, duration);
            }
        }

        if (m_InTransition && m_NextStateIndex >= 0 && m_NextStateIndex < static_cast<int>(m_States.size())) {
            const auto& nextState = m_States[static_cast<size_t>(m_NextStateIndex)];
            float nextDuration = ResolveStateDuration(nextState, clips);
            float nextSpeed = nextState.speed * skinned->getPlaybackSpeed();
            if (playing && nextDuration > 0.0f) {
                m_NextStateTime += stepDelta * nextSpeed;
                if (nextState.loop && skinned->isLooping()) {
                    while (m_NextStateTime >= nextDuration) {
                        m_NextStateTime -= nextDuration;
                    }
                } else {
                    m_NextStateTime = std::min(m_NextStateTime, nextDuration);
                }
            }
            m_TransitionElapsed = std::min(m_TransitionElapsed + stepDelta, m_TransitionDuration);
        } else if (!m_InTransition) {
            EvaluateTransitions(stepDelta);
        }

        AnimationLocalPose* outputPose = &m_StatePose;
        if (m_InTransition && m_TransitionDuration > 0.0f &&
            m_NextStateIndex >= 0 && m_NextStateIndex < static_cast<int>(m_States.size())) {
            const auto& nextState = m_States[static_cast<size_t>(m_NextStateIndex)];
            EvaluateStatePose(state, clips, skeleton, m_StateTime, m_StatePose);
            EvaluateStatePose(nextState, clips, skeleton, m_NextStateTime, m_NextPose);
            float t = Math::Clamp(m_TransitionElapsed / m_TransitionDuration, 0.0f, 1.0f);
            BlendLocalPose(m_StatePose, m_NextPose, t, m_BlendPose);
            outputPose = &m_BlendPose;
            if (t >= 0.999f) {
                m_CurrentStateIndex = m_NextStateIndex;
                m_StateTime = m_NextStateTime;
                m_InTransition = false;
                m_TransitionDuration = 0.0f;
                m_TransitionElapsed = 0.0f;
                m_NextStateIndex = -1;
                m_RootMotionValid = false;
            }
        } else {
            EvaluateStatePose(state, clips, skeleton, m_StateTime, m_StatePose);
            outputPose = &m_StatePose;
        }

        // Root motion extraction (modifies pose)
        if (m_RootMotionEnabled && outputPose) {
            int motionIdx = ResolveRootMotionBoneIndex(skeleton, outputPose);
            if (motionIdx >= 0 && motionIdx < static_cast<int>(outputPose->positions.size())) {
                Math::Vector3 basePos;
                Math::Quaternion baseRot;
                Math::Vector3 baseScale;
                DecomposeTRS(skeleton.getBones()[motionIdx].localBind, basePos, baseRot, baseScale);
                Math::Matrix4x4 global = BuildGlobalPose(skeleton, *outputPose, motionIdx);
                Math::Vector3 currentPos;
                Math::Quaternion currentRot;
                Math::Vector3 currentScale;
                DecomposeTRS(global, currentPos, currentRot, currentScale);

                if (!m_RootMotionValid || m_StateTime < m_PrevRootTime) {
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
                m_PrevRootTime = m_StateTime;
                outputPose->positions[motionIdx] = basePos;
                outputPose->rotations[motionIdx] = baseRot;
                outputPose->scales[motionIdx] = baseScale;
            }
        }

        if (outputPose) {
            Entity* entity = getEntity();
            IKConstraint* ik = entity ? entity->getComponent<IKConstraint>() : nullptr;
            if (ik && ik->isEnabled()) {
                int rootIdx = skeleton.getBoneIndex(ik->getRootBone());
                int midIdx = skeleton.getBoneIndex(ik->getMidBone());
                int endIdx = skeleton.getBoneIndex(ik->getEndBone());
                Math::Vector3 target = ik->getTargetPosition();
                if (!ik->getTargetInWorld() && entity) {
                    target = entity->getTransform()->getWorldMatrix().transformPoint(target);
                }
                ApplyTwoBoneIK(skeleton, *outputPose, rootIdx, midIdx, endIdx, target, ik->getWeight());
            }
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

        // Fire events
        const AnimationClip* activeClip = nullptr;
        if (state.type == AnimatorStateType::Clip && state.clipIndex >= 0 && state.clipIndex < static_cast<int>(clips.size())) {
            activeClip = clips[static_cast<size_t>(state.clipIndex)].get();
        }
        if (activeClip) {
            CollectEvents(activeClip, m_PrevStateTime, m_StateTime, state.loop && skinned->isLooping(), m_FiredEvents);
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
        BuildSkinMatrices(skeleton, m_RenderPose, m_WorkMatrices);
        for (auto* target : targets) {
            if (target) {
                target->applyBoneMatrices(m_WorkMatrices);
            }
        }
    }

    ResetTriggers();
    skinned->setTimeSeconds(m_StateTime);
}

bool Animator::ApplyState(int index, float blendDurationSeconds, bool restart) {
    if (index < 0 || index >= static_cast<int>(m_States.size())) {
        return false;
    }
    if (blendDurationSeconds > 0.0f && index != m_CurrentStateIndex) {
        m_InTransition = true;
        m_NextStateIndex = index;
        m_TransitionDuration = blendDurationSeconds;
        m_TransitionElapsed = 0.0f;
        m_NextStateTime = restart ? 0.0f : m_StateTime;
        m_RootMotionValid = false;
        return true;
    }
    m_CurrentStateIndex = index;
    if (restart) {
        m_StateTime = 0.0f;
        m_PrevStateTime = 0.0f;
        m_RootMotionValid = false;
    }
    return true;
}

bool Animator::EvaluateTransitions(float deltaTime) {
    (void)deltaTime;
    if (m_Transitions.empty()) {
        return false;
    }
    if (m_CurrentStateIndex < 0 || m_CurrentStateIndex >= static_cast<int>(m_States.size())) {
        return false;
    }
    const auto& state = m_States[static_cast<size_t>(m_CurrentStateIndex)];
    SkinnedMeshRenderer* skinned = ResolveSkinned();
    if (!skinned) return false;
    float duration = ResolveStateDuration(state, skinned->getAnimationClips());
    float normalizedTime = (duration > 0.0f) ? (m_StateTime / duration) : 0.0f;

    for (const auto& transition : m_Transitions) {
        if (transition.toState < 0 || transition.toState >= static_cast<int>(m_States.size())) {
            continue;
        }
        if (transition.fromState != -1 && transition.fromState != m_CurrentStateIndex) {
            continue;
        }
        if (!CheckTransition(transition, normalizedTime)) {
            continue;
        }
        float durationSeconds = transition.duration;
        if (!transition.fixedDuration && duration > 0.0f) {
            durationSeconds = duration * transition.duration;
        }
        ApplyState(transition.toState, durationSeconds, true);
        return true;
    }
    return false;
}

bool Animator::CheckTransition(const AnimatorTransition& transition, float normalizedTime) {
    if (transition.hasExitTime && normalizedTime < transition.exitTime) {
        return false;
    }
    for (const auto& cond : transition.conditions) {
        const AnimatorParameter* param = FindParam(m_Parameters, cond.parameter);
        if (!param) return false;
        auto numericValue = [&](const AnimatorParameter& p) {
            switch (p.type) {
                case AnimatorParameterType::Int: return static_cast<float>(p.intValue);
                case AnimatorParameterType::Bool: return p.boolValue ? 1.0f : 0.0f;
                case AnimatorParameterType::Trigger: return p.triggerValue ? 1.0f : 0.0f;
                case AnimatorParameterType::Float:
                default: return p.floatValue;
            }
        };
        auto numericThreshold = [&](const AnimatorCondition& c, const AnimatorParameter& p) {
            switch (p.type) {
                case AnimatorParameterType::Int: return static_cast<float>(c.intThreshold);
                case AnimatorParameterType::Bool: return c.boolThreshold ? 1.0f : 0.0f;
                case AnimatorParameterType::Trigger: return c.boolThreshold ? 1.0f : 0.0f;
                case AnimatorParameterType::Float:
                default: return c.threshold;
            }
        };
        switch (cond.op) {
            case AnimatorConditionOp::IfTrue:
                if (param->type == AnimatorParameterType::Trigger) {
                    if (!param->triggerValue) return false;
                } else if (!param->boolValue) {
                    return false;
                }
                break;
            case AnimatorConditionOp::IfFalse:
                if (param->type == AnimatorParameterType::Trigger) {
                    if (param->triggerValue) return false;
                } else if (param->boolValue) {
                    return false;
                }
                break;
            case AnimatorConditionOp::Greater:
                if (numericValue(*param) <= numericThreshold(cond, *param)) return false;
                break;
            case AnimatorConditionOp::Less:
                if (numericValue(*param) >= numericThreshold(cond, *param)) return false;
                break;
            case AnimatorConditionOp::GreaterEqual:
                if (numericValue(*param) < numericThreshold(cond, *param)) return false;
                break;
            case AnimatorConditionOp::LessEqual:
                if (numericValue(*param) > numericThreshold(cond, *param)) return false;
                break;
            case AnimatorConditionOp::Equal:
                if (param->type == AnimatorParameterType::Int) {
                    if (param->intValue != cond.intThreshold) return false;
                } else if (param->type == AnimatorParameterType::Bool) {
                    if (param->boolValue != cond.boolThreshold) return false;
                } else {
                    if (std::abs(param->floatValue - cond.threshold) > 0.0001f) return false;
                }
                break;
            case AnimatorConditionOp::NotEqual:
                if (param->type == AnimatorParameterType::Int) {
                    if (param->intValue == cond.intThreshold) return false;
                } else if (param->type == AnimatorParameterType::Bool) {
                    if (param->boolValue == cond.boolThreshold) return false;
                } else {
                    if (std::abs(param->floatValue - cond.threshold) <= 0.0001f) return false;
                }
                break;
        }
    }
    return true;
}

void Animator::EvaluateStatePose(const AnimatorState& state,
                                 const std::vector<std::shared_ptr<AnimationClip>>& clips,
                                 const Skeleton& skeleton,
                                 float timeSeconds,
                                 AnimationLocalPose& outPose) const {
    if (state.type == AnimatorStateType::BlendTree) {
        if (state.blendTreeIndex < 0 || state.blendTreeIndex >= static_cast<int>(m_BlendTrees.size())) {
            SampleLocalPose(skeleton, nullptr, timeSeconds, state.loop, outPose);
            return;
        }
        const AnimatorBlendTree& tree = m_BlendTrees[static_cast<size_t>(state.blendTreeIndex)];
        if (tree.motions.empty()) {
            SampleLocalPose(skeleton, nullptr, timeSeconds, state.loop, outPose);
            return;
        }

        float paramValue = GetParameterFloat(tree.parameter, 0.0f);
        std::vector<AnimatorBlendMotion> motions = tree.motions;
        std::sort(motions.begin(), motions.end(), [](const AnimatorBlendMotion& a, const AnimatorBlendMotion& b) {
            return a.threshold < b.threshold;
        });

        if (motions.size() == 1) {
            int idx = motions[0].clipIndex;
            const AnimationClip* clip = (idx >= 0 && idx < static_cast<int>(clips.size())) ? clips[static_cast<size_t>(idx)].get() : nullptr;
            SampleLocalPose(skeleton, clip, timeSeconds, state.loop, outPose);
            return;
        }

        size_t upper = 0;
        for (size_t i = 0; i < motions.size(); ++i) {
            if (paramValue <= motions[i].threshold) {
                upper = i;
                break;
            }
            upper = i;
        }
        if (upper == 0) {
            int idx = motions[0].clipIndex;
            const AnimationClip* clip = (idx >= 0 && idx < static_cast<int>(clips.size())) ? clips[static_cast<size_t>(idx)].get() : nullptr;
            SampleLocalPose(skeleton, clip, timeSeconds, state.loop, outPose);
            return;
        }
        size_t lower = upper - 1;
        const AnimatorBlendMotion& a = motions[lower];
        const AnimatorBlendMotion& b = motions[upper];
        float span = b.threshold - a.threshold;
        float t = (span > 0.0f) ? (paramValue - a.threshold) / span : 0.0f;
        const AnimationClip* clipA = (a.clipIndex >= 0 && a.clipIndex < static_cast<int>(clips.size()))
            ? clips[static_cast<size_t>(a.clipIndex)].get() : nullptr;
        const AnimationClip* clipB = (b.clipIndex >= 0 && b.clipIndex < static_cast<int>(clips.size()))
            ? clips[static_cast<size_t>(b.clipIndex)].get() : nullptr;
        AnimationLocalPose poseA;
        AnimationLocalPose poseB;
        SampleLocalPose(skeleton, clipA, timeSeconds, state.loop, poseA);
        SampleLocalPose(skeleton, clipB, timeSeconds, state.loop, poseB);
        BlendLocalPose(poseA, poseB, t, outPose);
        return;
    }

    const AnimationClip* clip = nullptr;
    if (state.clipIndex >= 0 && state.clipIndex < static_cast<int>(clips.size())) {
        clip = clips[static_cast<size_t>(state.clipIndex)].get();
    }
    SampleLocalPose(skeleton, clip, timeSeconds, state.loop, outPose);
}

float Animator::ResolveStateDuration(const AnimatorState& state,
                                     const std::vector<std::shared_ptr<AnimationClip>>& clips) const {
    if (state.type == AnimatorStateType::BlendTree) {
        float longest = 0.0f;
        if (state.blendTreeIndex >= 0 && state.blendTreeIndex < static_cast<int>(m_BlendTrees.size())) {
            const auto& tree = m_BlendTrees[static_cast<size_t>(state.blendTreeIndex)];
            for (const auto& motion : tree.motions) {
                if (motion.clipIndex >= 0 && motion.clipIndex < static_cast<int>(clips.size())) {
                    if (clips[static_cast<size_t>(motion.clipIndex)]) {
                        longest = std::max(longest, clips[static_cast<size_t>(motion.clipIndex)]->getDurationSeconds());
                    }
                }
            }
        }
        return longest;
    }
    if (state.clipIndex >= 0 && state.clipIndex < static_cast<int>(clips.size()) && clips[static_cast<size_t>(state.clipIndex)]) {
        return clips[static_cast<size_t>(state.clipIndex)]->getDurationSeconds();
    }
    return 0.0f;
}

float Animator::GetParameterFloat(const std::string& name, float fallback) const {
    const AnimatorParameter* param = FindParam(m_Parameters, name);
    if (!param) return fallback;
    switch (param->type) {
        case AnimatorParameterType::Float: return param->floatValue;
        case AnimatorParameterType::Int: return static_cast<float>(param->intValue);
        case AnimatorParameterType::Bool: return param->boolValue ? 1.0f : 0.0f;
        case AnimatorParameterType::Trigger: return param->triggerValue ? 1.0f : 0.0f;
    }
    return fallback;
}

void Animator::ResetTriggers() {
    for (auto& param : m_Parameters) {
        if (param.type == AnimatorParameterType::Trigger) {
            param.triggerValue = false;
        }
    }
}

SkinnedMeshRenderer* Animator::ResolveSkinned() const {
    std::vector<SkinnedMeshRenderer*> dummy;
    return ResolveSkinnedWithTargets(dummy);
}

SkinnedMeshRenderer* Animator::ResolveSkinnedWithTargets(std::vector<SkinnedMeshRenderer*>& outTargets) const {
    outTargets.clear();
    Entity* entity = getEntity();
    if (!entity) {
        return nullptr;
    }
    SkinnedMeshRenderer* selfSkinned = entity->getComponent<SkinnedMeshRenderer>();
    if (selfSkinned) {
        outTargets.push_back(selfSkinned);
        return selfSkinned;
    }
    CollectSkinnedTargets(entity, this, outTargets);
    if (outTargets.empty()) {
        return nullptr;
    }
    return outTargets.front();
}

} // namespace Crescent
