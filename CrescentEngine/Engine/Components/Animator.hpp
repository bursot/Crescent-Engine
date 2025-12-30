#pragma once

#include "../ECS/Component.hpp"
#include "../Components/SkinnedMeshRenderer.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace Crescent {

enum class AnimatorParameterType : uint8_t {
    Float,
    Int,
    Bool,
    Trigger
};

enum class AnimatorConditionOp : uint8_t {
    IfTrue,
    IfFalse,
    Greater,
    Less,
    GreaterEqual,
    LessEqual,
    Equal,
    NotEqual
};

enum class AnimatorStateType : uint8_t {
    Clip,
    BlendTree
};

enum class AnimatorBlendTreeType : uint8_t {
    Blend1D
};

struct AnimatorParameter {
    std::string name;
    AnimatorParameterType type = AnimatorParameterType::Float;
    float floatValue = 0.0f;
    int intValue = 0;
    bool boolValue = false;
    bool triggerValue = false;
};

struct AnimatorCondition {
    std::string parameter;
    AnimatorConditionOp op = AnimatorConditionOp::IfTrue;
    float threshold = 0.0f;
    int intThreshold = 0;
    bool boolThreshold = false;
};

struct AnimatorTransition {
    int fromState = -1; // -1 = any
    int toState = -1;
    float duration = 0.25f;
    bool hasExitTime = false;
    float exitTime = 0.9f; // normalized
    bool fixedDuration = true;
    std::vector<AnimatorCondition> conditions;
};

struct AnimatorBlendMotion {
    int clipIndex = -1;
    float threshold = 0.0f;
};

struct AnimatorBlendTree {
    std::string name;
    AnimatorBlendTreeType type = AnimatorBlendTreeType::Blend1D;
    std::string parameter;
    std::vector<AnimatorBlendMotion> motions;
};

struct AnimatorState {
    std::string name;
    int clipIndex = -1;
    float speed = 1.0f;
    bool loop = true;
    AnimatorStateType type = AnimatorStateType::Clip;
    int blendTreeIndex = -1;
};

class Animator : public Component {
public:
    Animator();
    virtual ~Animator() = default;

    COMPONENT_TYPE(Animator)

    void setStates(const std::vector<AnimatorState>& states);
    const std::vector<AnimatorState>& getStates() const { return m_States; }

    void setTransitions(const std::vector<AnimatorTransition>& transitions) { m_Transitions = transitions; }
    const std::vector<AnimatorTransition>& getTransitions() const { return m_Transitions; }

    void setBlendTrees(const std::vector<AnimatorBlendTree>& trees) { m_BlendTrees = trees; }
    const std::vector<AnimatorBlendTree>& getBlendTrees() const { return m_BlendTrees; }

    void setParameters(const std::vector<AnimatorParameter>& params) { m_Parameters = params; }
    const std::vector<AnimatorParameter>& getParameters() const { return m_Parameters; }

    bool setParameterFloat(const std::string& name, float value);
    bool setParameterInt(const std::string& name, int value);
    bool setParameterBool(const std::string& name, bool value);
    bool setTrigger(const std::string& name);

    int getCurrentStateIndex() const { return m_CurrentStateIndex; }
    bool setCurrentStateIndex(int index, float blendDurationSeconds = -1.0f, bool restart = true);
    bool setCurrentStateByName(const std::string& name, float blendDurationSeconds = -1.0f, bool restart = true);

    float getDefaultBlendDuration() const { return m_DefaultBlendDuration; }
    void setDefaultBlendDuration(float seconds);

    bool getAutoPlay() const { return m_AutoPlay; }
    void setAutoPlay(bool enabled) { m_AutoPlay = enabled; }

    bool getRootMotionEnabled() const { return m_RootMotionEnabled; }
    void setRootMotionEnabled(bool enabled) { m_RootMotionEnabled = enabled; }
    bool getApplyRootMotionPosition() const { return m_RootMotionApplyPosition; }
    void setApplyRootMotionPosition(bool enabled) { m_RootMotionApplyPosition = enabled; }
    bool getApplyRootMotionRotation() const { return m_RootMotionApplyRotation; }
    void setApplyRootMotionRotation(bool enabled) { m_RootMotionApplyRotation = enabled; }

    const std::vector<AnimationEvent>& getFiredEvents() const { return m_FiredEvents; }
    void clearFiredEvents() { m_FiredEvents.clear(); }

    void OnCreate() override;
    void OnUpdate(float deltaTime) override;

private:
    bool ApplyState(int index, float blendDurationSeconds, bool restart);
    bool EvaluateTransitions(float deltaTime);
    bool CheckTransition(const AnimatorTransition& transition, float normalizedTime);
    void EvaluateStatePose(const AnimatorState& state,
                           const std::vector<std::shared_ptr<AnimationClip>>& clips,
                           const Skeleton& skeleton,
                           float timeSeconds,
                           AnimationLocalPose& outPose) const;
    float ResolveStateDuration(const AnimatorState& state,
                               const std::vector<std::shared_ptr<AnimationClip>>& clips) const;
    float GetParameterFloat(const std::string& name, float fallback) const;
    void ResetTriggers();
    SkinnedMeshRenderer* ResolveSkinned() const;
    SkinnedMeshRenderer* ResolveSkinnedWithTargets(std::vector<SkinnedMeshRenderer*>& outTargets) const;

    std::vector<AnimatorState> m_States;
    std::vector<AnimatorTransition> m_Transitions;
    std::vector<AnimatorBlendTree> m_BlendTrees;
    std::vector<AnimatorParameter> m_Parameters;
    int m_CurrentStateIndex;
    int m_NextStateIndex;
    bool m_AutoPlay;
    float m_DefaultBlendDuration;
    bool m_NeedsApply;

    bool m_InTransition;
    float m_TransitionElapsed;
    float m_TransitionDuration;
    float m_StateTime;
    float m_NextStateTime;
    float m_PrevStateTime;

    bool m_RootMotionEnabled;
    bool m_RootMotionApplyPosition;
    bool m_RootMotionApplyRotation;
    bool m_RootMotionValid;
    float m_PrevRootTime;
    Math::Vector3 m_PrevRootPos;
    Math::Quaternion m_PrevRootRot;
    AnimationLocalPose m_StatePose;
    AnimationLocalPose m_NextPose;
    AnimationLocalPose m_BlendPose;
    std::vector<Math::Matrix4x4> m_WorkMatrices;
    std::vector<AnimationEvent> m_FiredEvents;
};

} // namespace Crescent
