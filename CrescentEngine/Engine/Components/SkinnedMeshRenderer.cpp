#include "SkinnedMeshRenderer.hpp"
#include "../Math/Math.hpp"
#include <algorithm>

namespace Crescent {

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
        for (const auto& clip : m_Clips) {
            if (clip) {
                clip->rebindToSkeleton(*m_Skeleton);
            }
        }
    } else {
        m_BoneMatrices.clear();
        m_PrevBoneMatrices.clear();
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
        return true;
    }

    m_BlendClip = target;
    m_BlendClipIndex = index;
    m_BlendDuration = std::max(0.0f, durationSeconds);
    m_BlendElapsed = 0.0f;
    m_BlendTimeSeconds = restart ? 0.0f : m_TimeSeconds;
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

void SkinnedMeshRenderer::OnUpdate(float deltaTime) {
    if (m_DrivenByAnimator) {
        return;
    }
    if (!m_BoneMatrices.empty()) {
        m_PrevBoneMatrices = m_BoneMatrices;
    } else if (m_Skeleton) {
        m_PrevBoneMatrices.assign(m_Skeleton->getBoneCount(), Math::Matrix4x4::Identity);
    }

    if (m_Playing && m_Clip) {
        float duration = m_Clip->getDurationSeconds();
        if (duration > 0.0f) {
            m_TimeSeconds += deltaTime * m_PlaybackSpeed;
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
            m_BlendTimeSeconds += deltaTime * m_PlaybackSpeed;
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
        m_BlendElapsed = std::min(m_BlendElapsed + deltaTime, m_BlendDuration);
    }

    if (!m_Skeleton) {
        return;
    }

    const auto& bones = m_Skeleton->getBones();
    if (bones.empty()) {
        return;
    }

    if (m_BlendClip && m_BlendDuration > 0.0f) {
        float blend = (m_BlendDuration > 0.0f)
            ? Math::Clamp(m_BlendElapsed / m_BlendDuration, 0.0f, 1.0f)
            : 1.0f;
        SampleLocalPose(*m_Skeleton, m_Clip.get(), m_TimeSeconds, m_Looping, m_LocalPose);
        SampleLocalPose(*m_Skeleton, m_BlendClip.get(), m_BlendTimeSeconds, m_Looping, m_BlendPose);
        BlendLocalPose(m_LocalPose, m_BlendPose, blend, m_BlendResultPose);
        BuildSkinMatrices(*m_Skeleton, m_BlendResultPose, m_BoneMatrices);
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
        BuildSkinMatrices(*m_Skeleton, m_LocalPose, m_BoneMatrices);
    }
}

} // namespace Crescent
