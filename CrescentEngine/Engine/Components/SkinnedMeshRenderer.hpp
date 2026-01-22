#pragma once

#include "../ECS/Component.hpp"
#include "../Rendering/Mesh.hpp"
#include "../Rendering/Material.hpp"
#include "../Animation/Skeleton.hpp"
#include "../Animation/AnimationClip.hpp"
#include "../Animation/AnimationPose.hpp"
#include <vector>
#include <memory>

namespace Crescent {

class SkinnedMeshRenderer : public Component {
public:
    SkinnedMeshRenderer();
    virtual ~SkinnedMeshRenderer() = default;

    COMPONENT_TYPE(SkinnedMeshRenderer)

    // Mesh
    std::shared_ptr<Mesh> getMesh() const { return m_Mesh; }
    void setMesh(std::shared_ptr<Mesh> mesh);

    // Skeleton
    std::shared_ptr<Skeleton> getSkeleton() const { return m_Skeleton; }
    void setSkeleton(std::shared_ptr<Skeleton> skeleton);

    // Materials
    void setMaterial(std::shared_ptr<Material> material);
    void setMaterial(uint32_t index, std::shared_ptr<Material> material);
    std::shared_ptr<Material> getMaterial(uint32_t index = 0) const;
    const std::vector<std::shared_ptr<Material>>& getMaterials() const { return m_Materials; }

    // Animation
    std::shared_ptr<AnimationClip> getAnimationClip() const { return m_Clip; }
    void setAnimationClip(std::shared_ptr<AnimationClip> clip);
    const std::vector<std::shared_ptr<AnimationClip>>& getAnimationClips() const { return m_Clips; }
    void setAnimationClips(const std::vector<std::shared_ptr<AnimationClip>>& clips);
    int getActiveClipIndex() const { return m_ActiveClipIndex; }
    bool setActiveClipIndex(int index);
    bool crossFadeToClip(int index, float durationSeconds, bool restart = true);
    void applyBoneMatrices(const std::vector<Math::Matrix4x4>& matrices);

    bool isDrivenByAnimator() const { return m_DrivenByAnimator; }
    void setDrivenByAnimator(bool driven) { m_DrivenByAnimator = driven; }

    bool isPlaying() const { return m_Playing; }
    void setPlaying(bool playing) { m_Playing = playing; }
    bool isLooping() const { return m_Looping; }
    void setLooping(bool looping) { m_Looping = looping; }
    float getPlaybackSpeed() const { return m_PlaybackSpeed; }
    void setPlaybackSpeed(float speed) { m_PlaybackSpeed = speed; }
    float getTimeSeconds() const { return m_TimeSeconds; }
    void setTimeSeconds(float time) { m_TimeSeconds = time; }

    bool getRootMotionEnabled() const { return m_RootMotionEnabled; }
    void setRootMotionEnabled(bool enabled) {
        m_RootMotionEnabled = enabled;
        m_RootMotionValid = false;
    }
    bool getApplyRootMotionPosition() const { return m_RootMotionApplyPosition; }
    void setApplyRootMotionPosition(bool value) { m_RootMotionApplyPosition = value; }
    bool getApplyRootMotionRotation() const { return m_RootMotionApplyRotation; }
    void setApplyRootMotionRotation(bool value) { m_RootMotionApplyRotation = value; }

    const std::vector<Math::Matrix4x4>& getBoneMatrices() const { return m_BoneMatrices; }
    void setBoneMatrices(const std::vector<Math::Matrix4x4>& matrices) { m_BoneMatrices = matrices; }
    const std::vector<Math::Matrix4x4>& getPreviousBoneMatrices() const { return m_PrevBoneMatrices; }

    void OnUpdate(float deltaTime) override;

private:
    void applyRootMotion(AnimationLocalPose& pose, float sampleTime);

    std::shared_ptr<Mesh> m_Mesh;
    std::shared_ptr<Skeleton> m_Skeleton;
    std::vector<std::shared_ptr<Material>> m_Materials;
    std::shared_ptr<AnimationClip> m_Clip;
    std::vector<std::shared_ptr<AnimationClip>> m_Clips;
    std::vector<Math::Matrix4x4> m_BoneMatrices;
    std::vector<Math::Matrix4x4> m_PrevBoneMatrices;
    std::shared_ptr<AnimationClip> m_BlendClip;
    float m_BlendTimeSeconds;
    float m_BlendDuration;
    float m_BlendElapsed;
    int m_BlendClipIndex;
    float m_SmoothedDelta = 0.0f;
    float m_AnimAccumulator = 0.0f;
    bool m_HasPose = false;
    AnimationLocalPose m_LocalPose;
    AnimationLocalPose m_BlendPose;
    AnimationLocalPose m_BlendResultPose;
    AnimationLocalPose m_PrevPose;
    AnimationLocalPose m_CurrentPose;
    AnimationLocalPose m_RenderPose;

    bool m_Playing;
    bool m_Looping;
    float m_PlaybackSpeed;
    float m_TimeSeconds;
    int m_ActiveClipIndex;
    bool m_DrivenByAnimator;
    bool m_RootMotionEnabled = false;
    bool m_RootMotionApplyPosition = true;
    bool m_RootMotionApplyRotation = false;
    bool m_RootMotionValid = false;
    float m_PrevRootTime = 0.0f;
    Math::Vector3 m_PrevRootPos = Math::Vector3::Zero;
    Math::Quaternion m_PrevRootRot = Math::Quaternion::Identity;
};

} // namespace Crescent
