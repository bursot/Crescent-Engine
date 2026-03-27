#pragma once

#include "../ECS/Component.hpp"
#include "../ECS/Entity.hpp"
#include "../ECS/Transform.hpp"
#include "../Math/Math.hpp"
#include "../Scene/SceneCommands.hpp"
#include "CharacterController.hpp"
#include "SkinnedMeshRenderer.hpp"
#include <algorithm>
#include <cctype>
#include <functional>
#include <string>

namespace Crescent {

class BoneAttachment : public Component {
public:
    BoneAttachment() = default;
    virtual ~BoneAttachment() = default;

    COMPONENT_TYPE(BoneAttachment)

    const std::string& getBoneName() const { return m_BoneName; }
    void setBoneName(const std::string& boneName) { m_BoneName = boneName; }

    const std::string& getSourceEntityUUID() const { return m_SourceEntityUUID; }
    void setSourceEntityUUID(const std::string& uuid) { m_SourceEntityUUID = uuid; }

    const Math::Vector3& getPositionOffset() const { return m_PositionOffset; }
    void setPositionOffset(const Math::Vector3& value) { m_PositionOffset = value; }

    const Math::Vector3& getRotationOffsetDegrees() const { return m_RotationOffsetDegrees; }
    void setRotationOffsetDegrees(const Math::Vector3& value) { m_RotationOffsetDegrees = value; }

    const Math::Vector3& getScaleOffset() const { return m_ScaleOffset; }
    void setScaleOffset(const Math::Vector3& value) { m_ScaleOffset = value; }

    bool getInheritBoneScale() const { return m_InheritBoneScale; }
    void setInheritBoneScale(bool value) { m_InheritBoneScale = value; }

    std::string getResolvedSourceEntityName() const {
        if (const SkinnedMeshRenderer* skinned = resolveSourceRenderer()) {
            if (Entity* source = skinned->getEntity()) {
                return source->getName();
            }
        }
        return "";
    }

    void OnUpdate(float) override { syncAttachment(); }
    void OnEditorUpdate(float) override { syncAttachment(); }

private:
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

    static std::string Lowercase(const std::string& value) {
        std::string lowered = value;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return lowered;
    }

    static std::string NormalizeBoneName(const std::string& value) {
        std::string lowered = Lowercase(value);
        std::string normalized;
        normalized.reserve(lowered.size());
        for (char c : lowered) {
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                normalized.push_back(c);
            }
        }
        return normalized;
    }

    static const SkinnedMeshRenderer* FindSkinnedInHierarchy(Transform* root, const Transform* excludedSubtreeRoot) {
        if (!root || root == excludedSubtreeRoot) {
            return nullptr;
        }

        if (Entity* entity = root->getEntity()) {
            if (auto* skinned = entity->getComponent<SkinnedMeshRenderer>()) {
                return skinned;
            }
        }

        for (Transform* child : root->getChildren()) {
            if (const SkinnedMeshRenderer* skinned = FindSkinnedInHierarchy(child, excludedSubtreeRoot)) {
                return skinned;
            }
        }

        return nullptr;
    }

    const SkinnedMeshRenderer* resolveSourceRenderer() const {
        if (!m_Entity) {
            return nullptr;
        }

        if (!m_SourceEntityUUID.empty()) {
            if (Scene* scene = m_Entity->getScene()) {
                if (Entity* sourceEntity = SceneCommands::getEntityByUUID(scene, m_SourceEntityUUID)) {
                    if (auto* skinned = sourceEntity->getComponent<SkinnedMeshRenderer>()) {
                        return skinned;
                    }
                    if (Transform* sourceTransform = sourceEntity->getTransform()) {
                        if (const SkinnedMeshRenderer* skinned = FindSkinnedInHierarchy(sourceTransform, nullptr)) {
                            return skinned;
                        }
                    }
                }
            }
        }

        Transform* selfTransform = m_Entity->getTransform();
        Transform* cursor = selfTransform;
        while (cursor) {
            Entity* owner = cursor->getEntity();
            if (owner && owner != m_Entity) {
                if (auto* skinned = owner->getComponent<SkinnedMeshRenderer>()) {
                    return skinned;
                }

                if (const SkinnedMeshRenderer* skinned = FindSkinnedInHierarchy(cursor, selfTransform)) {
                    return skinned;
                }
            }
            cursor = cursor->getParent();
        }

        if (Scene* scene = m_Entity->getScene()) {
            for (const auto& entityHandle : scene->getAllEntities()) {
                Entity* candidate = entityHandle.get();
                if (!candidate || candidate == m_Entity) {
                    continue;
                }
                if (!candidate->getComponent<CharacterController>()) {
                    continue;
                }
                if (auto* skinned = candidate->getComponent<SkinnedMeshRenderer>()) {
                    return skinned;
                }
                if (Transform* candidateTransform = candidate->getTransform()) {
                    if (const SkinnedMeshRenderer* skinned = FindSkinnedInHierarchy(candidateTransform, nullptr)) {
                        return skinned;
                    }
                }
            }
        }

        return m_Entity->getComponent<SkinnedMeshRenderer>();
    }

    int resolveBoneIndex(const Skeleton& skeleton) const {
        if (m_BoneName.empty()) {
            return -1;
        }

        int exact = skeleton.getBoneIndex(m_BoneName);
        if (exact >= 0) {
            return exact;
        }

        const std::string needle = Lowercase(m_BoneName);
        const std::string normalizedNeedle = NormalizeBoneName(m_BoneName);
        const auto& bones = skeleton.getBones();
        for (size_t i = 0; i < bones.size(); ++i) {
            if (Lowercase(bones[i].name) == needle) {
                return static_cast<int>(i);
            }
            if (!normalizedNeedle.empty() && NormalizeBoneName(bones[i].name) == normalizedNeedle) {
                return static_cast<int>(i);
            }
        }

        if (normalizedNeedle == "righthand" || normalizedNeedle == "lefthand") {
            const bool wantRight = normalizedNeedle == "righthand";
            for (size_t i = 0; i < bones.size(); ++i) {
                std::string normalized = NormalizeBoneName(bones[i].name);
                if ((wantRight && normalized.find("righthand") != std::string::npos) ||
                    (!wantRight && normalized.find("lefthand") != std::string::npos)) {
                    return static_cast<int>(i);
                }
            }
        }

        return -1;
    }

    void syncAttachment() {
        if (!m_Entity) {
            return;
        }

        const SkinnedMeshRenderer* skinned = resolveSourceRenderer();
        if (!skinned || !skinned->isEnabled()) {
            return;
        }

        Entity* sourceEntity = skinned->getEntity();
        if (!sourceEntity || !sourceEntity->getTransform()) {
            return;
        }

        std::shared_ptr<Skeleton> skeleton = skinned->getSkeleton();
        if (!skeleton) {
            return;
        }

        int boneIndex = resolveBoneIndex(*skeleton);
        if (boneIndex < 0) {
            return;
        }

        const auto& boneMatrices = skinned->getBoneMatrices();
        if (boneIndex >= static_cast<int>(boneMatrices.size())) {
            return;
        }

        const auto& bones = skeleton->getBones();
        if (boneIndex >= static_cast<int>(bones.size())) {
            return;
        }

        Math::Matrix4x4 sourceWorld = sourceEntity->getTransform()->getWorldMatrix();
        Math::Matrix4x4 meshToSkeleton = skeleton->getGlobalInverse().inversed();
        Math::Matrix4x4 bindGlobal = bones[static_cast<size_t>(boneIndex)].inverseBind.inversed();
        Math::Matrix4x4 boneLocalToMesh = meshToSkeleton * boneMatrices[static_cast<size_t>(boneIndex)] * bindGlobal;
        Math::Matrix4x4 boneWorld = sourceWorld * boneLocalToMesh;

        Math::Vector3 rotationRadians = m_RotationOffsetDegrees * Math::DEG_TO_RAD;
        Math::Quaternion offsetRotation = Math::Quaternion::FromEulerAngles(rotationRadians);
        Math::Vector3 offsetScale = m_InheritBoneScale ? m_ScaleOffset : Math::Vector3::One;
        Math::Matrix4x4 finalWorldMatrix = boneWorld * Math::Matrix4x4::TRS(m_PositionOffset, offsetRotation, offsetScale);

        Transform* transform = m_Entity->getTransform();
        if (!transform) {
            return;
        }

        Math::Matrix4x4 parentWorldInverse = Math::Matrix4x4::Identity;
        if (Transform* parent = transform->getParent()) {
            parentWorldInverse = parent->getWorldMatrix().inversed();
        }

        Math::Matrix4x4 localMatrix = parentWorldInverse * finalWorldMatrix;
        Math::Vector3 localPosition;
        Math::Quaternion localRotation;
        Math::Vector3 localScale;
        DecomposeTRS(localMatrix, localPosition, localRotation, localScale);

        transform->setLocalPosition(localPosition);
        transform->setLocalRotation(localRotation);
        transform->setLocalScale(m_InheritBoneScale ? localScale : m_ScaleOffset);
    }

    std::string m_BoneName = "mixamorig:RightHand";
    std::string m_SourceEntityUUID;
    Math::Vector3 m_PositionOffset = Math::Vector3::Zero;
    Math::Vector3 m_RotationOffsetDegrees = Math::Vector3::Zero;
    Math::Vector3 m_ScaleOffset = Math::Vector3::One;
    bool m_InheritBoneScale = false;
};

} // namespace Crescent
