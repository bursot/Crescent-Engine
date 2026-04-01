#pragma once

#include "../ECS/Component.hpp"
#include "../ECS/Entity.hpp"
#include "../ECS/Transform.hpp"
#include "../Input/InputManager.hpp"
#include "../Math/Math.hpp"
#include "../Scene/Scene.hpp"
#include "../Scene/SceneCommands.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Assets/AssetDatabase.hpp"
#include "../Audio/AudioSystem.hpp"
#include "../Physics/PhysicsWorld.hpp"
#include "../Components/CharacterController.hpp"
#include "../Components/Camera.hpp"
#include "../Components/SkinnedMeshRenderer.hpp"
#include "../Components/Animator.hpp"
#include "../Components/Health.hpp"
#include "../Components/IKConstraint.hpp"
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

namespace Crescent {

class ThirdPersonController : public Component {
public:
    COMPONENT_TYPE(ThirdPersonController)

    ThirdPersonController()
        : m_MouseSensitivity(0.0025f)
        , m_InvertY(false)
        , m_RequireLookButton(true)
        , m_LookButton(MouseButton::Right)
        , m_MinPitch(-35.0f)
        , m_MaxPitch(70.0f)
        , m_PivotHeight(1.6f)
        , m_LookAhead(0.15f)
        , m_ShoulderOffset(0.35f)
        , m_AimShoulderOffset(0.78f)
        , m_CameraDistance(3.6f)
        , m_AimCameraDistance(2.15f)
        , m_AimLookAheadDistance(8.0f)
        , m_AimLookHorizontalOffset(0.58f)
        , m_AimPivotHeightOffset(0.08f)
        , m_MinDistance(1.1f)
        , m_MaxDistance(6.5f)
        , m_ZoomSpeed(0.45f)
        , m_CameraCollisionRadius(0.18f)
        , m_PositionSmoothSpeed(10.0f)
        , m_RotationSmoothSpeed(12.0f)
        , m_CameraSmoothSpeed(14.0f)
        , m_WalkSpeed(2.2f)
        , m_RunSpeed(4.4f)
        , m_SprintSpeed(6.1f)
        , m_EnableSprint(true)
        , m_DriveCharacterController(true)
        , m_UseCharacterFireAnimation(false)
        , m_DebugLogging(false) {
    }

    float getMouseSensitivity() const { return m_MouseSensitivity; }
    void setMouseSensitivity(float value) { m_MouseSensitivity = std::max(0.0f, value); }

    bool getInvertY() const { return m_InvertY; }
    void setInvertY(bool value) { m_InvertY = value; }

    bool getRequireLookButton() const { return m_RequireLookButton; }
    void setRequireLookButton(bool value) { m_RequireLookButton = value; }

    MouseButton getLookButton() const { return m_LookButton; }
    void setLookButton(MouseButton button) { m_LookButton = button; }

    float getMinPitch() const { return m_MinPitch; }
    void setMinPitch(float value) { m_MinPitch = value; }

    float getMaxPitch() const { return m_MaxPitch; }
    void setMaxPitch(float value) { m_MaxPitch = value; }

    float getPivotHeight() const { return m_PivotHeight; }
    void setPivotHeight(float value) { m_PivotHeight = std::max(0.0f, value); }

    float getLookAhead() const { return m_LookAhead; }
    void setLookAhead(float value) { m_LookAhead = std::max(0.0f, value); }

    float getShoulderOffset() const { return m_ShoulderOffset; }
    void setShoulderOffset(float value) { m_ShoulderOffset = value; }

    Math::Vector3 getWeaponGripPositionOffset() const { return m_WeaponGripPositionOffset; }
    void setWeaponGripPositionOffset(const Math::Vector3& value) { m_WeaponGripPositionOffset = value; }

    Math::Vector3 getWeaponGripRotationOffsetDegrees() const { return m_WeaponGripRotationOffsetDegrees; }
    void setWeaponGripRotationOffsetDegrees(const Math::Vector3& value) { m_WeaponGripRotationOffsetDegrees = value; }

    Math::Vector3 getWeaponSupportHandOffset() const { return m_WeaponSupportHandOffset; }
    void setWeaponSupportHandOffset(const Math::Vector3& value) { m_WeaponSupportHandOffset = value; }

    float getCameraDistance() const { return m_CameraDistance; }
    void setCameraDistance(float value) {
        m_CameraDistance = std::max(0.1f, value);
        m_TargetCameraDistance = m_CameraDistance;
    }

    float getMinDistance() const { return m_MinDistance; }
    void setMinDistance(float value) { m_MinDistance = std::max(0.1f, value); }

    float getMaxDistance() const { return m_MaxDistance; }
    void setMaxDistance(float value) { m_MaxDistance = std::max(m_MinDistance, value); }

    float getZoomSpeed() const { return m_ZoomSpeed; }
    void setZoomSpeed(float value) { m_ZoomSpeed = std::max(0.0f, value); }

    float getCameraCollisionRadius() const { return m_CameraCollisionRadius; }
    void setCameraCollisionRadius(float value) { m_CameraCollisionRadius = std::max(0.0f, value); }

    float getPositionSmoothSpeed() const { return m_PositionSmoothSpeed; }
    void setPositionSmoothSpeed(float value) { m_PositionSmoothSpeed = std::max(0.0f, value); }

    float getRotationSmoothSpeed() const { return m_RotationSmoothSpeed; }
    void setRotationSmoothSpeed(float value) { m_RotationSmoothSpeed = std::max(0.0f, value); }

    float getCameraSmoothSpeed() const { return m_CameraSmoothSpeed; }
    void setCameraSmoothSpeed(float value) { m_CameraSmoothSpeed = std::max(0.0f, value); }

    float getWalkSpeed() const { return m_WalkSpeed; }
    void setWalkSpeed(float value) { m_WalkSpeed = std::max(0.0f, value); }

    float getRunSpeed() const { return m_RunSpeed; }
    void setRunSpeed(float value) { m_RunSpeed = std::max(0.0f, value); }

    float getSprintSpeed() const { return m_SprintSpeed; }
    void setSprintSpeed(float value) { m_SprintSpeed = std::max(0.0f, value); }

    bool getEnableSprint() const { return m_EnableSprint; }
    void setEnableSprint(bool value) { m_EnableSprint = value; }

    bool getDriveCharacterController() const { return m_DriveCharacterController; }
    void setDriveCharacterController(bool value) { m_DriveCharacterController = value; }

    float getMeleeHitDamage() const { return m_MeleeHitDamage; }
    void setMeleeHitDamage(float value) { m_MeleeHitDamage = std::max(0.0f, value); }

    float getMeleeHitRadius() const { return m_MeleeHitRadius; }
    void setMeleeHitRadius(float value) { m_MeleeHitRadius = std::max(0.05f, value); }

    float getMeleeHitForwardOffset() const { return m_MeleeHitForwardOffset; }
    void setMeleeHitForwardOffset(float value) { m_MeleeHitForwardOffset = value; }

    float getMeleeHitUpOffset() const { return m_MeleeHitUpOffset; }
    void setMeleeHitUpOffset(float value) { m_MeleeHitUpOffset = value; }

    int getMeleeHitMask() const { return static_cast<int>(m_MeleeHitMask); }
    void setMeleeHitMask(int value) { m_MeleeHitMask = static_cast<uint32_t>(std::max(0, value)); }

    bool getMeleeHitTriggers() const { return m_MeleeHitTriggers; }
    void setMeleeHitTriggers(bool value) { m_MeleeHitTriggers = value; }

    bool getDebugLogging() const { return m_DebugLogging; }
    void setDebugLogging(bool value) { m_DebugLogging = value; }

    void OnCreate() override {
        findDependencies();
        initializeOrbitFromCurrentCamera();
        refreshAnimationTargets();
        if (m_Controller) {
            m_Controller->setUseInput(false);
            if (m_DriveCharacterController) {
                m_Controller->setEnabled(false);
            }
        }
    }

    void OnDestroy() override {
        if (m_Controller && m_DriveCharacterController) {
            m_Controller->setEnabled(true);
        }
    }

    void OnUpdate(float deltaTime) override {
        if (!m_Entity || deltaTime <= 0.0f) {
            return;
        }
        if (SceneManager::getInstance().getViewMode() != SceneManager::ViewMode::Game) {
            return;
        }

        if (!m_Controller || !m_BodyTransform) {
            findDependencies();
            if (!m_Controller || !m_BodyTransform) {
                return;
            }
        }
        if (!m_CameraTransform) {
            findDependencies();
        }

        if (m_Controller) {
            m_Controller->setUseInput(false);
            if (m_DriveCharacterController && m_Controller->isEnabled()) {
                m_Controller->setEnabled(false);
            } else if (!m_DriveCharacterController && !m_Controller->isEnabled()) {
                m_Controller->setEnabled(true);
            }
        }
        disableRootMotion();

        InputManager& input = InputManager::getInstance();
        updateOrbit(deltaTime, input);
        driveCharacter(deltaTime, input);
        syncWeaponSupportHandIK();
        updateAnimation(deltaTime);
        updateMeleeHitTrace(deltaTime);
        syncNamedWeaponProp();
        updateCamera(deltaTime);
        emitDebugLog(deltaTime, input);
    }

private:
    enum class AnimAction {
        None,
        Idle,
        AimIdle,
        Walk,
        Backpedal,
        BackpedalLeft,
        BackpedalRight,
        StrafeLeft,
        StrafeRight,
        StrafeWalkLeft,
        StrafeWalkRight,
        TurnLeft,
        TurnRight,
        TurnLeftSharp,
        TurnRightSharp,
        Run,
        Sprint,
        Equip,
        Disarm,
        Fire,
        Reload,
        Jump,
        Fall
    };

    struct ClipMapping {
        int idle = -1;
        int aimIdle = -1;
        int walk = -1;
        int armedWalk = -1;
        int backpedal = -1;
        int armedBackpedal = -1;
        int armedBackpedalRun = -1;
        int backpedalLeft = -1;
        int backpedalRight = -1;
        int strafeLeft = -1;
        int strafeRight = -1;
        int armedStrafeLeft = -1;
        int armedStrafeRight = -1;
        int strafeWalkLeft = -1;
        int strafeWalkRight = -1;
        int turnLeft = -1;
        int turnRight = -1;
        int turnLeftSharp = -1;
        int turnRightSharp = -1;
        int run = -1;
        int armedRun = -1;
        int sprint = -1;
        int equip = -1;
        int disarm = -1;
        int unarmedJump = -1;
        int unarmedJumpRunning = -1;
        int standingJump = -1;
        int fire = -1;
        int reload = -1;
        int jump = -1;
        int fall = -1;
    };

    struct AnimatorMapping {
        int idleState = -1;
        int aimIdleState = -1;
        int walkState = -1;
        int armedWalkState = -1;
        int backpedalState = -1;
        int armedBackpedalState = -1;
        int armedBackpedalRunState = -1;
        int backpedalLeftState = -1;
        int backpedalRightState = -1;
        int strafeLeftState = -1;
        int strafeRightState = -1;
        int armedStrafeLeftState = -1;
        int armedStrafeRightState = -1;
        int strafeWalkLeftState = -1;
        int strafeWalkRightState = -1;
        int turnLeftState = -1;
        int turnRightState = -1;
        int turnLeftSharpState = -1;
        int turnRightSharpState = -1;
        int runState = -1;
        int armedRunState = -1;
        int sprintState = -1;
        int fireState = -1;
        int reloadState = -1;
        int jumpState = -1;
        int fallState = -1;
        int idleClip = -1;
        int aimIdleClip = -1;
        int walkClip = -1;
        int armedWalkClip = -1;
        int backpedalClip = -1;
        int armedBackpedalClip = -1;
        int armedBackpedalRunClip = -1;
        int backpedalLeftClip = -1;
        int backpedalRightClip = -1;
        int strafeLeftClip = -1;
        int strafeRightClip = -1;
        int armedStrafeLeftClip = -1;
        int armedStrafeRightClip = -1;
        int strafeWalkLeftClip = -1;
        int strafeWalkRightClip = -1;
        int turnLeftClip = -1;
        int turnRightClip = -1;
        int turnLeftSharpClip = -1;
        int turnRightSharpClip = -1;
        int runClip = -1;
        int armedRunClip = -1;
        int sprintClip = -1;
        int fireClip = -1;
        int reloadClip = -1;
        int jumpClip = -1;
        int fallClip = -1;
    };

    static std::string ToLower(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    static bool ContainsAllTokens(const std::string& haystack, const std::vector<std::string>& tokens) {
        for (const auto& token : tokens) {
            if (!token.empty() && haystack.find(token) == std::string::npos) {
                return false;
            }
        }
        return true;
    }

    static int FindClipIndex(const std::vector<std::shared_ptr<AnimationClip>>& clips,
                             const std::vector<std::string>& tokens) {
        for (size_t i = 0; i < clips.size(); ++i) {
            const auto& clip = clips[i];
            if (!clip) {
                continue;
            }
            if (ContainsAllTokens(ToLower(clip->getName()), tokens)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    static int FindClipIndexAny(const std::vector<std::shared_ptr<AnimationClip>>& clips,
                                const std::vector<std::string>& tokens) {
        for (size_t i = 0; i < clips.size(); ++i) {
            const auto& clip = clips[i];
            if (!clip) {
                continue;
            }
            std::string name = ToLower(clip->getName());
            for (const auto& token : tokens) {
                if (!token.empty() && name.find(token) != std::string::npos) {
                    return static_cast<int>(i);
                }
            }
        }
        return -1;
    }

    static int FindLocomotionClipIndex(const std::vector<std::shared_ptr<AnimationClip>>& clips,
                                       const std::vector<std::string>& includeTokens,
                                       const std::vector<std::string>& excludeTokens) {
        for (size_t i = 0; i < clips.size(); ++i) {
            const auto& clip = clips[i];
            if (!clip) {
                continue;
            }
            std::string name = ToLower(clip->getName());
            if (!ContainsAllTokens(name, includeTokens)) {
                continue;
            }
            bool excluded = false;
            for (const auto& token : excludeTokens) {
                if (!token.empty() && name.find(token) != std::string::npos) {
                    excluded = true;
                    break;
                }
            }
            if (!excluded) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    static int FindDirectionalClipIndex(const std::vector<std::shared_ptr<AnimationClip>>& clips,
                                        const std::vector<std::vector<std::string>>& tokenSets,
                                        const std::vector<std::string>& excludeTokens) {
        for (const auto& tokens : tokenSets) {
            int index = FindLocomotionClipIndex(clips, tokens, excludeTokens);
            if (index >= 0) {
                return index;
            }
        }
        return -1;
    }

    static float DampFactor(float speed, float deltaTime) {
        return 1.0f - std::exp(-std::max(0.0f, speed) * deltaTime);
    }

    static bool ContainsAnyToken(const std::string& haystack, const std::vector<std::string>& tokens) {
        for (const auto& token : tokens) {
            if (!token.empty() && haystack.find(token) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    static bool MatchesAnyTag(const std::string& tag, const std::initializer_list<const char*>& options) {
        for (const char* option : options) {
            if (option && tag == option) {
                return true;
            }
        }
        return false;
    }

    Math::Vector3 getAudioOrigin(float heightOffset = 1.0f) const {
        if (m_BodyTransform) {
            return m_BodyTransform->getPosition() + Math::Vector3(0.0f, heightOffset, 0.0f);
        }
        if (m_Entity && m_Entity->getTransform()) {
            return m_Entity->getTransform()->getPosition() + Math::Vector3(0.0f, heightOffset, 0.0f);
        }
        return Math::Vector3::Zero;
    }

    float nextAudioJitter(float amplitude) {
        static constexpr float kPattern[7] = {-1.0f, -0.45f, 0.35f, 0.9f, -0.2f, 0.6f, 0.0f};
        float jitter = kPattern[static_cast<size_t>(m_AudioVariationCounter % 7)] * amplitude;
        ++m_AudioVariationCounter;
        return jitter;
    }

    std::string resolveEventAudioPath(const std::string& payload) const {
        if (payload.empty()) {
            return {};
        }
        std::filesystem::path candidate(payload);
        if (candidate.is_absolute() && std::filesystem::exists(candidate)) {
            return candidate.string();
        }

        const std::string& rootPath = AssetDatabase::getInstance().getRootPath();
        if (!rootPath.empty()) {
            std::filesystem::path rooted = std::filesystem::path(rootPath) / candidate;
            if (std::filesystem::exists(rooted)) {
                return rooted.string();
            }
        }
        return std::filesystem::exists(candidate) ? candidate.string() : std::string();
    }

    static std::string getEventTypeLower(const AnimationEvent& event) {
        if (!event.eventType.empty()) {
            return ToLower(event.eventType);
        }
        if (!event.payload.empty()) {
            return "audio";
        }
        return {};
    }

    static std::string getEventTagLower(const AnimationEvent& event) {
        if (!event.eventTag.empty()) {
            return ToLower(event.eventTag);
        }
        return ToLower(event.name);
    }

    float resolveEventPitch(const AnimationEvent& event) {
        float minPitch = std::max(0.01f, std::min(event.pitchMin, event.pitchMax));
        float maxPitch = std::max(minPitch, std::max(event.pitchMin, event.pitchMax));
        float center = (minPitch + maxPitch) * 0.5f;
        float amplitude = (maxPitch - minPitch) * 0.5f;
        return std::max(0.01f, center + nextAudioJitter(amplitude));
    }

    AudioBus resolveEventBus(const AnimationEvent& event) const {
        std::string tag = getEventTagLower(event);
        if (ContainsAnyToken(tag, {"vocal", "grunt", "man", "voice"})) {
            return AudioBus::Vocal;
        }
        if (ContainsAnyToken(tag, {"ui"})) {
            return AudioBus::UI;
        }
        if (ContainsAnyToken(tag, {"music"})) {
            return AudioBus::Music;
        }
        if (ContainsAnyToken(tag, {"ambience", "ambient"})) {
            return AudioBus::Ambience;
        }
        return AudioBus::SFX;
    }

    bool playConfiguredEventAudio(const AnimationEvent& event,
                                  const std::string& fallbackPath,
                                  bool directional) {
        std::string path = resolveEventAudioPath(event.payload);
        if (path.empty()) {
            path = fallbackPath;
        }
        if (path.empty()) {
            return false;
        }

        float volume = std::max(0.0f, event.volume);
        float pitch = resolveEventPitch(event);
        AudioBus bus = resolveEventBus(event);
        if (!event.spatial) {
            return AudioSystem::getInstance().playOneShot(path, bus, volume, pitch);
        }
        if (directional) {
            Math::Vector3 forward = m_BodyTransform ? FlattenDirection(m_BodyTransform->forward()) : Math::Vector3(0.0f, 0.0f, -1.0f);
            return AudioSystem::getInstance().playOneShot3DDirectional(
                path,
                getAudioOrigin(1.1f),
                forward,
                bus,
                volume,
                pitch,
                1.2f,
                18.0f,
                1.0f,
                1.65f,
                2.75f,
                0.35f,
                0.8f);
        }
        return AudioSystem::getInstance().playOneShot3D(
            path,
            getAudioOrigin(ContainsAnyToken(ToLower(event.name), {"jump"}) ? 1.0f : 1.15f),
            bus,
            volume,
            pitch,
            1.0f,
            15.0f,
            0.9f);
    }

    bool playSpatialOneShot(const std::string& path,
                            float baseVolume,
                            float basePitch,
                            float minDistance,
                            float maxDistance,
                            float rolloff,
                            float heightOffset = 1.0f,
                            float pitchJitter = 0.04f,
                            float volumeJitter = 0.08f) {
        float pitch = std::max(0.01f, basePitch + nextAudioJitter(pitchJitter));
        float volume = std::max(0.0f, baseVolume + nextAudioJitter(volumeJitter));
        return AudioSystem::getInstance().playOneShot3D(
            path,
            getAudioOrigin(heightOffset),
            volume,
            pitch,
            minDistance,
            maxDistance,
            rolloff);
    }

    bool playDirectionalWhooshOneShot(const std::string& path,
                                      float baseVolume,
                                      float basePitch,
                                      float minDistance,
                                      float maxDistance,
                                      float rolloff) {
        float pitch = std::max(0.01f, basePitch + nextAudioJitter(0.035f));
        float volume = std::max(0.0f, baseVolume + nextAudioJitter(0.06f));
        Math::Vector3 forward = m_BodyTransform ? FlattenDirection(m_BodyTransform->forward()) : Math::Vector3(0.0f, 0.0f, -1.0f);
        return AudioSystem::getInstance().playOneShot3DDirectional(
            path,
            getAudioOrigin(1.1f),
            forward,
            volume,
            pitch,
            minDistance,
            maxDistance,
            rolloff,
            1.65f,
            2.75f,
            0.35f,
            0.8f);
    }

    static void AddUniqueClipIndex(std::vector<int>& clips, int clipIndex) {
        if (clipIndex < 0) {
            return;
        }
        if (std::find(clips.begin(), clips.end(), clipIndex) == clips.end()) {
            clips.push_back(clipIndex);
        }
    }

    bool isMeleeAttackClipIndex(int clipIndex) const {
        return std::find(m_MeleeAttackClips.begin(), m_MeleeAttackClips.end(), clipIndex) != m_MeleeAttackClips.end();
    }

    bool isMeleeAttackActive() const {
        return m_AnimState == AnimAction::Fire && isMeleeAttackClipIndex(m_ActionClipIndex) && !isActionClipFinished();
    }

    bool isQueuedMeleeChainReady() const {
        if (!(m_AnimState == AnimAction::Fire && isMeleeAttackClipIndex(m_ActionClipIndex))) {
            return false;
        }
        if (m_ActionDuration <= 0.0f) {
            return true;
        }
        return m_ActionElapsed >= (m_ActionDuration * 0.82f);
    }

    void ensureMeleeAttackAudioResolved() {
        if (m_MeleeAttackAudioResolved) {
            return;
        }
        m_MeleeAttackAudioResolved = true;

        const std::string& rootPath = AssetDatabase::getInstance().getRootPath();
        if (rootPath.empty() || !std::filesystem::exists(rootPath)) {
            return;
        }

        std::error_code error;
        std::filesystem::recursive_directory_iterator it(
            rootPath,
            std::filesystem::directory_options::skip_permission_denied,
            error);
        std::filesystem::recursive_directory_iterator end;

        for (; it != end; it.increment(error)) {
            if (error) {
                error.clear();
                continue;
            }

            const std::filesystem::directory_entry& entry = *it;
            if (!entry.is_regular_file()) {
                continue;
            }

            std::string extension = ToLower(entry.path().extension().string());
            if (extension != ".wav" && extension != ".mp3" && extension != ".ogg" && extension != ".flac") {
                continue;
            }

            std::string stem = ToLower(entry.path().stem().string());
            if (!ContainsAnyToken(stem, {"attack", "swing", "axe"})) {
                continue;
            }

            std::string path = entry.path().string();
            if (ContainsAnyToken(stem, {"man"})) {
                m_MeleeAttackVocalAll.push_back(path);
                if (ContainsAnyToken(stem, {"light", "simple"})) {
                    m_MeleeAttackVocalLight.push_back(path);
                }
                if (ContainsAnyToken(stem, {"medium"})) {
                    m_MeleeAttackVocalMedium.push_back(path);
                }
                if (ContainsAnyToken(stem, {"heavy"})) {
                    m_MeleeAttackVocalHeavy.push_back(path);
                }
                continue;
            }

            m_MeleeAttackAudioAll.push_back(path);
            if (ContainsAnyToken(stem, {"light", "simple"})) {
                m_MeleeAttackAudioLight.push_back(path);
            }
            if (ContainsAnyToken(stem, {"medium"})) {
                m_MeleeAttackAudioMedium.push_back(path);
            }
            if (ContainsAnyToken(stem, {"heavy"})) {
                m_MeleeAttackAudioHeavy.push_back(path);
            }
        }
    }

    const std::string* chooseMeleeAttackAudioPath(int clipIndex) {
        ensureMeleeAttackAudioResolved();

        auto chooseFromBucket = [](std::vector<std::string>& bucket, int& cursor) -> const std::string* {
            if (bucket.empty()) {
                return nullptr;
            }
            const std::string& path = bucket[static_cast<size_t>(cursor % static_cast<int>(bucket.size()))];
            cursor = (cursor + 1) % static_cast<int>(bucket.size());
            return &path;
        };

        std::string clipName = ToLower(getClipName(clipIndex));
        if (ContainsAnyToken(clipName, {"360", "downward", "run jump"}) && !m_MeleeAttackAudioHeavy.empty()) {
            return chooseFromBucket(m_MeleeAttackAudioHeavy, m_MeleeAttackAudioHeavyCursor);
        }
        if (ContainsAnyToken(clipName, {"horizontal", "backhand", "ver. 2", "ver 2"}) && !m_MeleeAttackAudioMedium.empty()) {
            return chooseFromBucket(m_MeleeAttackAudioMedium, m_MeleeAttackAudioMediumCursor);
        }
        if (!m_MeleeAttackAudioLight.empty()) {
            return chooseFromBucket(m_MeleeAttackAudioLight, m_MeleeAttackAudioLightCursor);
        }
        if (!m_MeleeAttackAudioMedium.empty()) {
            return chooseFromBucket(m_MeleeAttackAudioMedium, m_MeleeAttackAudioMediumCursor);
        }
        if (!m_MeleeAttackAudioHeavy.empty()) {
            return chooseFromBucket(m_MeleeAttackAudioHeavy, m_MeleeAttackAudioHeavyCursor);
        }
        return chooseFromBucket(m_MeleeAttackAudioAll, m_MeleeAttackAudioCursor);
    }

    std::shared_ptr<AnimationClip> getPrimaryClipShared(int clipIndex) const {
        if (!m_PrimarySkinned) {
            return nullptr;
        }
        const auto& clips = m_PrimarySkinned->getAnimationClips();
        if (clipIndex < 0 || clipIndex >= static_cast<int>(clips.size())) {
            return nullptr;
        }
        return clips[static_cast<size_t>(clipIndex)];
    }

    bool clipHasEventToken(int clipIndex, const std::vector<std::string>& tokens) const {
        std::shared_ptr<AnimationClip> clip = getPrimaryClipShared(clipIndex);
        if (!clip) {
            return false;
        }
        for (const auto& event : clip->getEvents()) {
            std::string tag = getEventTagLower(event);
            if (ContainsAnyToken(tag, tokens)) {
                return true;
            }
        }
        return false;
    }

    bool clipHasAudioEvents(int clipIndex) const {
        std::shared_ptr<AnimationClip> clip = getPrimaryClipShared(clipIndex);
        if (!clip) {
            return false;
        }
        for (const auto& event : clip->getEvents()) {
            std::string type = getEventTypeLower(event);
            std::string tag = getEventTagLower(event);
            if (type == "audio" || !event.payload.empty() ||
                ContainsAnyToken(tag, {"foot", "step", "swing", "whoosh", "attack", "vocal", "grunt", "man", "jump", "impact", "hit"})) {
                return true;
            }
        }
        return false;
    }

    const std::string* chooseMeleeAttackVocalPath(int clipIndex) {
        ensureMeleeAttackAudioResolved();

        auto chooseFromBucket = [](std::vector<std::string>& bucket, int& cursor) -> const std::string* {
            if (bucket.empty()) {
                return nullptr;
            }
            const std::string& path = bucket[static_cast<size_t>(cursor % static_cast<int>(bucket.size()))];
            cursor = (cursor + 1) % static_cast<int>(bucket.size());
            return &path;
        };

        std::string clipName = ToLower(getClipName(clipIndex));
        if (ContainsAnyToken(clipName, {"360", "downward", "run jump"}) && !m_MeleeAttackVocalHeavy.empty()) {
            return chooseFromBucket(m_MeleeAttackVocalHeavy, m_MeleeAttackVocalHeavyCursor);
        }
        if (ContainsAnyToken(clipName, {"horizontal", "backhand", "ver. 2", "ver 2"}) && !m_MeleeAttackVocalMedium.empty()) {
            return chooseFromBucket(m_MeleeAttackVocalMedium, m_MeleeAttackVocalMediumCursor);
        }
        if (!m_MeleeAttackVocalLight.empty()) {
            return chooseFromBucket(m_MeleeAttackVocalLight, m_MeleeAttackVocalLightCursor);
        }
        if (!m_MeleeAttackVocalMedium.empty()) {
            return chooseFromBucket(m_MeleeAttackVocalMedium, m_MeleeAttackVocalMediumCursor);
        }
        if (!m_MeleeAttackVocalHeavy.empty()) {
            return chooseFromBucket(m_MeleeAttackVocalHeavy, m_MeleeAttackVocalHeavyCursor);
        }
        return chooseFromBucket(m_MeleeAttackVocalAll, m_MeleeAttackVocalCursor);
    }

    const std::string* chooseJumpAudioPath() {
        ensureJumpAudioResolved();
        if (m_JumpAudioPaths.empty()) {
            return nullptr;
        }
        const std::string& path = m_JumpAudioPaths[static_cast<size_t>(m_JumpAudioCursor % static_cast<int>(m_JumpAudioPaths.size()))];
        m_JumpAudioCursor = (m_JumpAudioCursor + 1) % static_cast<int>(m_JumpAudioPaths.size());
        return &path;
    }

    const std::string* chooseJumpVocalPath() {
        ensureJumpAudioResolved();
        if (m_JumpVocalPaths.empty()) {
            return nullptr;
        }
        const std::string& path = m_JumpVocalPaths[static_cast<size_t>(m_JumpVocalCursor % static_cast<int>(m_JumpVocalPaths.size()))];
        m_JumpVocalCursor = (m_JumpVocalCursor + 1) % static_cast<int>(m_JumpVocalPaths.size());
        return &path;
    }

    void playJumpBodyAudio() {
        if (const std::string* path = chooseJumpAudioPath()) {
            playSpatialOneShot(*path, 0.72f, 1.0f, 1.0f, 16.0f, 0.9f, 1.0f, 0.03f, 0.05f);
        }
    }

    void playJumpVocalAudio() {
        if (const std::string* path = chooseJumpVocalPath()) {
            playSpatialOneShot(*path, 0.58f, 1.0f, 1.0f, 14.0f, 0.8f, 1.45f, 0.03f, 0.04f);
        }
    }

    void queueMeleeAttackAudio(int clipIndex) {
        (void)clipIndex;
    }

    void flushMeleeAttackAudio() {
        m_PendingMeleeAttackAudio = false;
        m_PendingMeleeAttackAudioClipIndex = -1;
        m_PendingMeleeAttackAudioTriggerTime = 0.0f;
    }

    void ensureJumpAudioResolved() {
        if (m_JumpAudioResolved) {
            return;
        }
        m_JumpAudioResolved = true;

        const std::string& rootPath = AssetDatabase::getInstance().getRootPath();
        if (rootPath.empty() || !std::filesystem::exists(rootPath)) {
            return;
        }

        std::error_code error;
        std::filesystem::recursive_directory_iterator it(
            rootPath,
            std::filesystem::directory_options::skip_permission_denied,
            error);
        std::filesystem::recursive_directory_iterator end;

        for (; it != end; it.increment(error)) {
            if (error) {
                error.clear();
                continue;
            }

            const std::filesystem::directory_entry& entry = *it;
            if (!entry.is_regular_file()) {
                continue;
            }

            std::string extension = ToLower(entry.path().extension().string());
            if (extension != ".wav" && extension != ".mp3" && extension != ".ogg" && extension != ".flac") {
                continue;
            }

            std::string stem = ToLower(entry.path().stem().string());
            if (!ContainsAnyToken(stem, {"jump"})) {
                continue;
            }

            std::string path = entry.path().string();
            if (ContainsAnyToken(stem, {"man"})) {
                m_JumpVocalPaths.push_back(path);
            } else {
                m_JumpAudioPaths.push_back(path);
            }
        }
    }

    void playJumpAudio() {
        return;
    }

    void playFootstepAudioOneShot() {
        ensureFootstepAudioResolved();
        if (m_FootstepAudioPaths.empty()) {
            return;
        }
        const std::string& footstepPath = m_FootstepAudioPaths[static_cast<size_t>(m_FootstepAudioCursor % static_cast<int>(m_FootstepAudioPaths.size()))];
        m_FootstepAudioCursor = (m_FootstepAudioCursor + 1) % static_cast<int>(m_FootstepAudioPaths.size());
        playSpatialOneShot(footstepPath, 0.42f, 1.0f, 0.8f, 10.0f, 1.15f, 0.15f, 0.05f, 0.06f);
    }

    void ensureFootstepAudioResolved() {
        if (m_FootstepAudioResolved) {
            return;
        }
        m_FootstepAudioResolved = true;

        const std::string& rootPath = AssetDatabase::getInstance().getRootPath();
        if (rootPath.empty() || !std::filesystem::exists(rootPath)) {
            return;
        }

        std::error_code error;
        std::filesystem::recursive_directory_iterator it(
            rootPath,
            std::filesystem::directory_options::skip_permission_denied,
            error);
        std::filesystem::recursive_directory_iterator end;

        for (; it != end; it.increment(error)) {
            if (error) {
                error.clear();
                continue;
            }

            const std::filesystem::directory_entry& entry = *it;
            if (!entry.is_regular_file()) {
                continue;
            }

            std::string extension = ToLower(entry.path().extension().string());
            if (extension != ".wav" && extension != ".mp3" && extension != ".ogg" && extension != ".flac") {
                continue;
            }

            std::string stem = ToLower(entry.path().stem().string());
            if (!ContainsAnyToken(stem, {"footstep", "foot", "step"})) {
                continue;
            }
            if (ContainsAnyToken(stem, {"attack", "swing", "axe"})) {
                continue;
            }

            m_FootstepAudioPaths.push_back(entry.path().string());
        }
    }

    void updateFootstepAudio(float deltaTime, bool grounded, bool moving, bool running, bool rising, bool falling) {
        (void)deltaTime;
        (void)grounded;
        (void)moving;
        (void)running;
        (void)rising;
        (void)falling;
        m_FootstepWasActive = false;
    }

    void configureClipEventPlayback(int clipIndex, bool looping, float playbackSpeed, bool restart) {
        if (clipIndex < 0) {
            m_EventClipIndex = -1;
            m_EventClipPrevTime = 0.0f;
            m_EventClipTime = 0.0f;
            m_EventClipLooping = false;
            m_EventClipPlaybackSpeed = 1.0f;
            return;
        }

        if (clipIndex != m_EventClipIndex || restart) {
            m_EventClipIndex = clipIndex;
            m_EventClipPrevTime = 0.0f;
            m_EventClipTime = 0.0f;
        }
        m_EventClipLooping = looping;
        m_EventClipPlaybackSpeed = std::max(0.01f, playbackSpeed);
    }

    void dispatchClipEvent(const AnimationEvent& event, int clipIndex) {
        std::string type = getEventTypeLower(event);
        std::string tag = getEventTagLower(event);
        if (MatchesAnyTag(tag, {"attack_window_open", "melee_window_open", "hitbox_on", "damage_window_open"})) {
            beginMeleeHitWindow();
        } else if (MatchesAnyTag(tag, {"attack_window_close", "melee_window_close", "hitbox_off", "damage_window_close"})) {
            endMeleeHitWindow();
        }
        if (type == "audio" || !event.payload.empty()) {
            bool directional = ContainsAnyToken(tag, {"swing", "whoosh", "attack"});
            playConfiguredEventAudio(event, {}, directional);
        }
        (void)clipIndex;
    }

    void processClipEvents(float deltaTime) {
        if (m_EventClipIndex < 0) {
            return;
        }

        std::shared_ptr<AnimationClip> clip = getPrimaryClipShared(m_EventClipIndex);
        if (!clip || clip->getEvents().empty()) {
            return;
        }

        float duration = clip->getDurationSeconds();
        if (duration <= 0.0f) {
            return;
        }

        float previousTime = m_EventClipTime;
        float currentTime = previousTime + (deltaTime * m_EventClipPlaybackSpeed);
        if (m_EventClipLooping) {
            while (currentTime >= duration) {
                currentTime -= duration;
            }
        } else {
            currentTime = std::min(currentTime, duration);
        }

        std::vector<AnimationEvent> firedEvents;
        const auto& events = clip->getEvents();
        if (!m_EventClipLooping || currentTime >= previousTime) {
            for (const auto& evt : events) {
                if (evt.time > previousTime && evt.time <= currentTime) {
                    firedEvents.push_back(evt);
                }
            }
        } else {
            for (const auto& evt : events) {
                if (evt.time > previousTime || evt.time <= currentTime) {
                    firedEvents.push_back(evt);
                }
            }
        }

        m_EventClipPrevTime = previousTime;
        m_EventClipTime = currentTime;

        for (const auto& evt : firedEvents) {
            dispatchClipEvent(evt, m_EventClipIndex);
        }
    }

    void processAnimatorFiredEvents(Animator* animator) {
        if (!animator) {
            return;
        }
        const auto& fired = animator->getFiredEvents();
        if (fired.empty()) {
            return;
        }
        int clipIndex = m_ActionClipIndex >= 0 ? m_ActionClipIndex : m_EventClipIndex;
        for (const auto& evt : fired) {
            dispatchClipEvent(evt, clipIndex);
        }
        animator->clearFiredEvents();
    }

    static bool shouldRestartActionOnEntry(AnimAction action) {
        switch (action) {
            case AnimAction::TurnLeft:
            case AnimAction::TurnRight:
            case AnimAction::TurnLeftSharp:
            case AnimAction::TurnRightSharp:
            case AnimAction::Equip:
            case AnimAction::Disarm:
            case AnimAction::Fire:
            case AnimAction::Reload:
            case AnimAction::Jump:
            case AnimAction::Fall:
                return true;
            default:
                return false;
        }
    }

    static bool isBlockingOneShotAction(AnimAction action) {
        switch (action) {
            case AnimAction::Fire:
            case AnimAction::Reload:
                return true;
            default:
                return false;
        }
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

    static std::string FileStem(std::string path) {
        size_t slash = path.find_last_of("/\\");
        if (slash != std::string::npos) {
            path = path.substr(slash + 1);
        }
        size_t dot = path.find_last_of('.');
        if (dot != std::string::npos) {
            path = path.substr(0, dot);
        }
        return ToLower(path);
    }

    Entity* findEntityByNameInsensitive(const std::string& desiredName) const {
        if (!m_Entity) {
            return nullptr;
        }
        Scene* scene = m_Entity->getScene();
        if (!scene) {
            return nullptr;
        }
        std::string desired = ToLower(desiredName);
        for (const auto& handle : scene->getAllEntities()) {
            Entity* entity = handle.get();
            if (entity && ToLower(entity->getName()) == desired) {
                return entity;
            }
        }
        return nullptr;
    }

    static Math::Vector3 FlattenDirection(const Math::Vector3& value,
                                          const Math::Vector3& fallback = Math::Vector3(0.0f, 0.0f, -1.0f)) {
        Math::Vector3 flat(value.x, 0.0f, value.z);
        if (flat.lengthSquared() <= Math::EPSILON) {
            return fallback;
        }
        return flat.normalized();
    }

    void collectSkinnedMeshes(Transform* root, std::vector<SkinnedMeshRenderer*>& out) {
        if (!root) {
            return;
        }
        if (Entity* entity = root->getEntity()) {
            if (auto* skinned = entity->getComponent<SkinnedMeshRenderer>()) {
                out.push_back(skinned);
            }
        }
        for (Transform* child : root->getChildren()) {
            collectSkinnedMeshes(child, out);
        }
    }

    Animator* findAnimatorInHierarchy(Transform* root) {
        if (!root) {
            return nullptr;
        }
        if (Entity* entity = root->getEntity()) {
            if (auto* animator = entity->getComponent<Animator>()) {
                return animator;
            }
        }
        for (Transform* child : root->getChildren()) {
            if (Animator* animator = findAnimatorInHierarchy(child)) {
                return animator;
            }
        }
        return nullptr;
    }

    Transform* findCameraTransform(Transform* root) const {
        if (!root) {
            return nullptr;
        }
        std::vector<Transform*> stack;
        stack.push_back(root);
        while (!stack.empty()) {
            Transform* node = stack.back();
            stack.pop_back();
            if (!node) {
                continue;
            }
            if (node->getEntity() && node->getEntity()->getComponent<Camera>()) {
                return node;
            }
            const auto& children = node->getChildren();
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                stack.push_back(*it);
            }
        }
        return nullptr;
    }

    Entity* resolveBodyEntity() const {
        Entity* current = m_Entity;
        Transform* cursor = m_Entity ? m_Entity->getTransform() : nullptr;
        while (current) {
            if (current->getComponent<CharacterController>()) {
                return current;
            }
            if (!cursor) {
                break;
            }
            Transform* parent = cursor->getParent();
            if (!parent) {
                break;
            }
            cursor = parent;
            current = parent->getEntity();
        }
        return nullptr;
    }

    void findDependencies() {
        Transform* previousBodyTransform = m_BodyTransform;
        m_Controller = nullptr;
        m_BodyTransform = nullptr;
        m_CameraTransform = nullptr;

        if (!m_Entity) {
            return;
        }

        Entity* bodyEntity = resolveBodyEntity();
        if (bodyEntity) {
            m_Controller = bodyEntity->getComponent<CharacterController>();
            m_BodyTransform = bodyEntity->getTransform();
        } else {
            m_BodyTransform = m_Entity->getTransform();
            m_Controller = m_Entity->getComponent<CharacterController>();
        }

        if (!m_BodyTransform) {
            return;
        }

        if (m_Entity->getComponent<Camera>()) {
            m_CameraTransform = m_Entity->getTransform();
        } else {
            m_CameraTransform = findCameraTransform(m_Entity->getTransform());
            if (!m_CameraTransform) {
                m_CameraTransform = findCameraTransform(m_BodyTransform);
            }
            if (!m_CameraTransform) {
                if (Camera* main = Camera::getMainCamera()) {
                    m_CameraTransform = main->getEntity() ? main->getEntity()->getTransform() : nullptr;
                }
            }
        }

        if (m_CameraTransform && m_CameraTransform->getEntity()) {
            if (auto* camera = m_CameraTransform->getEntity()->getComponent<Camera>()) {
                Camera::setMainCamera(camera);
            }
        }

        if (previousBodyTransform != m_BodyTransform) {
            m_BodyOrientationInitialized = false;
        }
    }

    void refreshAnimationTargets() {
        m_Animator = nullptr;
        m_PrimarySkinned = nullptr;
        m_AnimTargets.clear();
        m_AnimatorParamsResolved = false;
        m_SpeedParamName.clear();
        m_MovingParamName.clear();
        m_GroundedParamName.clear();
        m_SprintParamName.clear();
        m_MoveXParamName.clear();
        m_MoveYParamName.clear();
        m_TurnParamName.clear();
        m_VerticalSpeedParamName.clear();
        m_JumpParamName.clear();
        m_AnimState = AnimAction::None;

        if (!m_Entity) {
            return;
        }

        m_Animator = findAnimatorInHierarchy(m_Entity->getTransform());
        collectSkinnedMeshes(m_Entity->getTransform(), m_AnimTargets);
        if (m_AnimTargets.empty() && m_BodyTransform && m_BodyTransform != m_Entity->getTransform()) {
            collectSkinnedMeshes(m_BodyTransform, m_AnimTargets);
        }
        if (!m_AnimTargets.empty()) {
            m_PrimarySkinned = m_AnimTargets.front();
        }
        ensureLocomotionClipSources();
        disableRootMotion();
        resolveClipMapping();
        if (m_Animator) {
            resolveAnimatorMapping();
            resolveAnimatorParameters();
        }
    }

    void ensureLocomotionClipSources() {
        if (!m_PrimarySkinned) {
            return;
        }

        auto skeleton = m_PrimarySkinned->getSkeleton();
        if (!skeleton) {
            return;
        }

        const auto& clips = m_PrimarySkinned->getAnimationClips();
        const auto& sources = m_PrimarySkinned->getAnimationClipSources();
        if (sources.empty()) {
            return;
        }

        bool hasEmbeddedMultiClipSource = clips.size() > sources.size();
        bool hasContainerAnimationSource = false;
        for (const auto& source : sources) {
            if (source.path.empty()) {
                continue;
            }
            std::string ext = ToLower(std::filesystem::path(source.path).extension().string());
            if (ext == ".glb" || ext == ".gltf") {
                hasContainerAnimationSource = true;
                break;
            }
        }
        if (hasEmbeddedMultiClipSource || (hasContainerAnimationSource && clips.size() >= 4)) {
            return;
        }

        std::vector<std::filesystem::path> sourceDirs;
        std::vector<std::string> knownStems;
        for (const auto& source : sources) {
            if (source.path.empty()) {
                continue;
            }
            std::filesystem::path path(source.path);
            sourceDirs.push_back(path.parent_path());
            knownStems.push_back(FileStem(path.string()));
        }

        static const std::vector<std::string> kExpectedFiles = {
            "idle.fbx",
            "walking.fbx",
            "running.fbx",
            "jump.fbx",
            "left strafe.fbx",
            "right strafe.fbx",
            "left strafe walking.fbx",
            "right strafe walking.fbx",
            "left turn.fbx",
            "right turn.fbx",
            "left turn 90.fbx",
            "right turn 90.fbx"
        };

        bool addedAny = false;
        for (const auto& dir : sourceDirs) {
            if (dir.empty()) {
                continue;
            }
            for (const auto& fileName : kExpectedFiles) {
                std::string stem = FileStem(fileName);
                if (std::find(knownStems.begin(), knownStems.end(), stem) != knownStems.end()) {
                    continue;
                }

                std::filesystem::path candidate = dir / fileName;
                if (!std::filesystem::exists(candidate)) {
                    continue;
                }

                auto clips = SceneCommands::importAnimationClipsForSkeleton(candidate.string(), *skeleton);
                if (clips.empty()) {
                    continue;
                }

                AnimationClipSource source;
                source.path = candidate.string();
                source.clips = std::move(clips);
                m_PrimarySkinned->addAnimationClipSource(source);
                knownStems.push_back(stem);
                addedAny = true;
            }
        }

        (void)addedAny;
    }

    void disableRootMotion() {
        if (m_Animator) {
            m_Animator->setRootMotionEnabled(false);
            m_Animator->setApplyRootMotionPosition(false);
            m_Animator->setApplyRootMotionRotation(false);
        }
        for (auto* skinned : m_AnimTargets) {
            if (!skinned) {
                continue;
            }
            skinned->setRootMotionEnabled(false);
            skinned->setApplyRootMotionPosition(false);
            skinned->setApplyRootMotionRotation(false);
        }
    }

    int resolveWeaponBoneIndex(const Skeleton& skeleton) const {
        if (m_WeaponBoneName.empty()) {
            return -1;
        }

        int exact = skeleton.getBoneIndex(m_WeaponBoneName);
        if (exact >= 0) {
            return exact;
        }

        std::string desired = ToLower(m_WeaponBoneName);
        std::string normalizedDesired;
        for (char c : desired) {
            if (std::isalnum(static_cast<unsigned char>(c))) {
                normalizedDesired.push_back(c);
            }
        }

        const auto& bones = skeleton.getBones();
        for (size_t i = 0; i < bones.size(); ++i) {
            std::string boneLower = ToLower(bones[i].name);
            if (boneLower == desired) {
                return static_cast<int>(i);
            }
            std::string normalizedBone;
            for (char c : boneLower) {
                if (std::isalnum(static_cast<unsigned char>(c))) {
                    normalizedBone.push_back(c);
                }
            }
            if (!normalizedDesired.empty() && normalizedBone == normalizedDesired) {
                return static_cast<int>(i);
            }
        }

        return -1;
    }

    std::string resolveSkeletonBoneName(const Skeleton& skeleton,
                                        const std::vector<std::string>& candidates) const {
        const auto& bones = skeleton.getBones();
        for (const auto& candidate : candidates) {
            std::string desired = ToLower(candidate);
            std::string normalizedDesired;
            for (char c : desired) {
                if (std::isalnum(static_cast<unsigned char>(c))) {
                    normalizedDesired.push_back(c);
                }
            }

            for (const auto& bone : bones) {
                std::string boneLower = ToLower(bone.name);
                if (boneLower == desired) {
                    return bone.name;
                }
                std::string normalizedBone;
                for (char c : boneLower) {
                    if (std::isalnum(static_cast<unsigned char>(c))) {
                        normalizedBone.push_back(c);
                    }
                }
                if (!normalizedDesired.empty() && normalizedBone == normalizedDesired) {
                    return bone.name;
                }
            }
        }
        return {};
    }

    bool computeWeaponBoneWorld(Math::Matrix4x4& outBoneWorld) const {
        if (!m_PrimarySkinned || !m_PrimarySkinned->isEnabled()) {
            return false;
        }

        Entity* sourceEntity = m_PrimarySkinned->getEntity();
        if (!sourceEntity || !sourceEntity->getTransform()) {
            return false;
        }

        std::shared_ptr<Skeleton> skeleton = m_PrimarySkinned->getSkeleton();
        if (!skeleton) {
            return false;
        }

        int boneIndex = resolveWeaponBoneIndex(*skeleton);
        if (boneIndex < 0) {
            return false;
        }

        const auto& boneMatrices = m_PrimarySkinned->getBoneMatrices();
        if (boneIndex >= static_cast<int>(boneMatrices.size())) {
            return false;
        }

        const auto& bones = skeleton->getBones();
        if (boneIndex >= static_cast<int>(bones.size())) {
            return false;
        }

        Math::Matrix4x4 sourceWorld = sourceEntity->getTransform()->getWorldMatrix();
        Math::Matrix4x4 meshToSkeleton = skeleton->getGlobalInverse().inversed();
        Math::Matrix4x4 bindGlobal = bones[static_cast<size_t>(boneIndex)].inverseBind.inversed();
        Math::Matrix4x4 boneLocalToMesh = meshToSkeleton * boneMatrices[static_cast<size_t>(boneIndex)] * bindGlobal;
        outBoneWorld = sourceWorld * boneLocalToMesh;
        return true;
    }

    bool computeDesiredWeaponWorld(Math::Matrix4x4& outWorld) {
        Math::Matrix4x4 boneWorld = Math::Matrix4x4::Identity;
        if (!computeWeaponBoneWorld(boneWorld)) {
            return false;
        }

        Math::Vector3 bonePos;
        Math::Quaternion boneRot;
        Math::Vector3 boneScale;
        DecomposeTRS(boneWorld, bonePos, boneRot, boneScale);
        boneWorld = Math::Matrix4x4::TRS(bonePos, boneRot, Math::Vector3::One);

        if (!m_WeaponPropBindingInitialized && m_WeaponPropEntity && m_WeaponPropEntity->getTransform()) {
            Math::Vector3 weaponScale;
            Math::Vector3 unusedPos;
            Math::Quaternion unusedRot;
            DecomposeTRS(m_WeaponPropEntity->getTransform()->getWorldMatrix(), unusedPos, unusedRot, weaponScale);
            m_WeaponPropScale = weaponScale;
            m_WeaponPropBindingInitialized = true;
        }

        outWorld = boneWorld * Math::Matrix4x4::TRS(
            m_WeaponGripPositionOffset,
            Math::Quaternion::FromEulerAngles(m_WeaponGripRotationOffsetDegrees * Math::DEG_TO_RAD),
            m_WeaponPropScale
        );
        return true;
    }

    IKConstraint* resolveWeaponSupportHandIK() {
        if (!m_Animator || !m_PrimarySkinned) {
            return nullptr;
        }

        Entity* animatorEntity = m_Animator->getEntity();
        if (!animatorEntity) {
            return nullptr;
        }

        IKConstraint* ik = animatorEntity->getComponent<IKConstraint>();
        if (!ik) {
            ik = animatorEntity->addComponent<IKConstraint>();
        }
        if (!ik) {
            return nullptr;
        }

        std::shared_ptr<Skeleton> skeleton = m_PrimarySkinned->getSkeleton();
        if (!skeleton) {
            return ik;
        }

        if (ik->getRootBone().empty()) {
            ik->setRootBone(resolveSkeletonBoneName(*skeleton, {"mixamorig:LeftArm", "LeftArm", "LeftShoulder"}));
        }
        if (ik->getMidBone().empty()) {
            ik->setMidBone(resolveSkeletonBoneName(*skeleton, {"mixamorig:LeftForeArm", "LeftForeArm", "LeftLowerArm"}));
        }
        if (ik->getEndBone().empty()) {
            ik->setEndBone(resolveSkeletonBoneName(*skeleton, {"mixamorig:LeftHand", "LeftHand"}));
        }

        ik->setTargetInWorld(true);
        return ik;
    }

    void syncWeaponSupportHandIK() {
        IKConstraint* ik = resolveWeaponSupportHandIK();
        if (!ik) {
            return;
        }

        if (!m_AttachNamedWeaponProp || !m_IsStrafeMode) {
            ik->setWeight(0.0f);
            return;
        }

        if (!m_WeaponPropEntity || ToLower(m_WeaponPropEntity->getName()) != ToLower(m_WeaponPropName)) {
            m_WeaponPropEntity = findEntityByNameInsensitive(m_WeaponPropName);
            m_WeaponPropBindingInitialized = false;
        }

        if (!m_WeaponPropEntity) {
            ik->setWeight(0.0f);
            return;
        }

        Math::Matrix4x4 weaponWorld = Math::Matrix4x4::Identity;
        if (!computeDesiredWeaponWorld(weaponWorld)) {
            ik->setWeight(0.0f);
            return;
        }

        ik->setTargetPosition(weaponWorld.transformPoint(m_WeaponSupportHandOffset));
        ik->setWeight(1.0f);
    }

    void syncNamedWeaponProp() {
        if (!m_AttachNamedWeaponProp || !m_Entity || !m_PrimarySkinned) {
            return;
        }

        if (!m_WeaponPropEntity || ToLower(m_WeaponPropEntity->getName()) != ToLower(m_WeaponPropName)) {
            m_WeaponPropEntity = findEntityByNameInsensitive(m_WeaponPropName);
            m_WeaponPropBindingInitialized = false;
        }

        if (!m_WeaponPropEntity || m_WeaponPropEntity == m_Entity) {
            return;
        }

        const bool weaponVisible = m_IsStrafeMode;
        if (m_WeaponPropEntity->isActiveSelf() != weaponVisible) {
            m_WeaponPropEntity->setActive(weaponVisible);
        }
        if (!weaponVisible) {
            return;
        }

        Transform* weaponTransform = m_WeaponPropEntity->getTransform();
        if (!weaponTransform) {
            return;
        }

        Math::Matrix4x4 finalWorld = Math::Matrix4x4::Identity;
        if (!computeDesiredWeaponWorld(finalWorld)) {
            return;
        }
        Math::Vector3 worldPos;
        Math::Quaternion worldRot;
        Math::Vector3 worldScale;
        DecomposeTRS(finalWorld, worldPos, worldRot, worldScale);

        weaponTransform->setPosition(worldPos);
        weaponTransform->setRotation(worldRot);
        weaponTransform->setLocalScale(worldScale);
    }

    Math::Vector3 getMovementCameraForward() const {
        return FlattenDirection(computeCameraForward());
    }

    static float YawFromDirection(const Math::Vector3& direction) {
        return std::atan2(direction.x, -direction.z);
    }

    static float NormalizeRadians(float radians) {
        return std::atan2(std::sin(radians), std::cos(radians));
    }

    static float MoveTowardsAngleRadians(float current, float target, float maxDelta) {
        float delta = NormalizeRadians(target - current);
        if (std::abs(delta) <= maxDelta) {
            return target;
        }
        return NormalizeRadians(current + ((delta > 0.0f) ? maxDelta : -maxDelta));
    }

    Math::Vector3 getBodyPlanarForward() const {
        if (!m_BodyTransform) {
            return Math::Vector3(0.0f, 0.0f, -1.0f);
        }
        return FlattenDirection(m_BodyTransform->forward());
    }

    void initializeBodyOrientation() {
        if (!m_BodyTransform) {
            return;
        }
        Math::Vector3 facing = FlattenDirection(m_BodyTransform->forward(), getMovementCameraForward());
        m_BodyYaw = YawFromDirection(facing);
        Math::Quaternion yawRotation = Math::Quaternion::FromAxisAngle(Math::Vector3::Up, -m_BodyYaw);
        m_BodyRotationOffset = yawRotation.inverse() * m_BodyTransform->getRotation();
        m_BodyOrientationInitialized = true;
    }

    void applyBodyYaw(float yaw) {
        if (!m_BodyTransform) {
            return;
        }
        if (!m_BodyOrientationInitialized) {
            initializeBodyOrientation();
        }
        m_BodyYaw = NormalizeRadians(yaw);
        Math::Quaternion yawRotation = Math::Quaternion::FromAxisAngle(Math::Vector3::Up, -m_BodyYaw);
        m_BodyTransform->setRotation(yawRotation * m_BodyRotationOffset);
    }

    Math::Vector3 sweepFallbackMove(const Math::Vector3& startPosition,
                                    const Math::Vector3& move) const {
        if (!m_Controller || !m_BodyTransform) {
            return startPosition + move;
        }

        Scene* scene = m_Entity ? m_Entity->getScene() : nullptr;
        PhysicsWorld* physics = scene ? scene->getPhysicsWorld() : nullptr;
        if (!physics) {
            return startPosition + move;
        }

        float distance = move.length();
        if (distance <= Math::EPSILON) {
            return startPosition;
        }

        Math::Vector3 dir = move / distance;
        PhysicsRaycastHit hit{};
        if (!physics->capsuleCast(startPosition,
                                  m_Controller->getRadius(),
                                  m_Controller->getHeight(),
                                  dir,
                                  distance + m_Controller->getSkinWidth(),
                                  hit,
                                  m_Controller->getCollisionMask(),
                                  false,
                                  m_BodyTransform->getEntity())) {
            return startPosition + move;
        }

        float travel = std::max(0.0f, hit.distance - m_Controller->getSkinWidth());
        return startPosition + dir * travel;
    }

    static const char* AnimActionName(AnimAction action) {
        switch (action) {
            case AnimAction::Idle: return "Idle";
            case AnimAction::AimIdle: return "AimIdle";
            case AnimAction::Walk: return "Walk";
            case AnimAction::Backpedal: return "Backpedal";
            case AnimAction::BackpedalLeft: return "BackpedalLeft";
            case AnimAction::BackpedalRight: return "BackpedalRight";
            case AnimAction::StrafeLeft: return "StrafeLeft";
            case AnimAction::StrafeRight: return "StrafeRight";
            case AnimAction::StrafeWalkLeft: return "StrafeWalkLeft";
            case AnimAction::StrafeWalkRight: return "StrafeWalkRight";
            case AnimAction::TurnLeft: return "TurnLeft";
            case AnimAction::TurnRight: return "TurnRight";
            case AnimAction::TurnLeftSharp: return "TurnLeftSharp";
            case AnimAction::TurnRightSharp: return "TurnRightSharp";
            case AnimAction::Run: return "Run";
            case AnimAction::Sprint: return "Sprint";
            case AnimAction::Equip: return "Equip";
            case AnimAction::Disarm: return "Disarm";
            case AnimAction::Fire: return "Fire";
            case AnimAction::Reload: return "Reload";
            case AnimAction::Jump: return "Jump";
            case AnimAction::Fall: return "Fall";
            default: return "None";
        }
    }

    std::string getClipName(int clipIndex) const {
        if (!m_PrimarySkinned) {
            return "none";
        }
        const auto& clips = m_PrimarySkinned->getAnimationClips();
        if (clipIndex < 0 || clipIndex >= static_cast<int>(clips.size()) || !clips[static_cast<size_t>(clipIndex)]) {
            return "none";
        }
        return clips[static_cast<size_t>(clipIndex)]->getName();
    }

    uint32_t makeInputMask(InputManager& input) const {
        uint32_t mask = 0;
        if (input.isKeyPressed(KeyCode::W)) mask |= 1u << 0;
        if (input.isKeyPressed(KeyCode::A)) mask |= 1u << 1;
        if (input.isKeyPressed(KeyCode::S)) mask |= 1u << 2;
        if (input.isKeyPressed(KeyCode::D)) mask |= 1u << 3;
        if (input.isKeyPressed(KeyCode::Shift)) mask |= 1u << 4;
        if (input.isKeyPressed(KeyCode::Space)) mask |= 1u << 5;
        if (input.isMouseButtonPressed(m_LookButton)) mask |= 1u << 6;
        if (m_CombatMode) mask |= 1u << 7;
        return mask;
    }

    void emitDebugLog(float deltaTime, InputManager& input) {
        if (!m_DebugLogging || !m_Controller || !m_BodyTransform) {
            return;
        }

        m_DebugLogTimer += deltaTime;
        uint32_t inputMask = makeInputMask(input);
        bool inputChanged = inputMask != m_LastDebugInputMask;
        bool clipChanged = (m_ActionClipIndex != m_LastDebugClipIndex) || (m_AnimState != m_LastDebugAnimState);
        bool shouldLog = inputChanged || clipChanged || m_DebugLogTimer >= 0.2f;
        if (!shouldLog) {
            return;
        }

        m_DebugLogTimer = 0.0f;
        m_LastDebugInputMask = inputMask;
        m_LastDebugClipIndex = m_ActionClipIndex;
        m_LastDebugAnimState = m_AnimState;

        Math::Vector3 velocity = m_Controller->getVelocity();
        Math::Vector3 horizontalVelocity(velocity.x, 0.0f, velocity.z);
        Math::Vector3 bodyForward = m_BodyTransform->forward();
        bodyForward.y = 0.0f;
        if (bodyForward.lengthSquared() <= Math::EPSILON) {
            bodyForward = Math::Vector3(0.0f, 0.0f, -1.0f);
        } else {
            bodyForward.normalize();
        }

        float bodyYawDeg = NormalizeRadians(YawFromDirection(bodyForward)) * Math::RAD_TO_DEG;
        float camYawDeg = NormalizeRadians(m_Yaw) * Math::RAD_TO_DEG;
        float camPitchDeg = m_Pitch * Math::RAD_TO_DEG;
        Math::Vector3 position = m_BodyTransform->getPosition();

        std::cout << "[TPC] keys="
                  << ((inputMask & (1u << 0)) ? "W" : "-")
                  << ((inputMask & (1u << 1)) ? "A" : "-")
                  << ((inputMask & (1u << 2)) ? "S" : "-")
                  << ((inputMask & (1u << 3)) ? "D" : "-")
                  << ((inputMask & (1u << 4)) ? "+Shift" : "")
                  << ((inputMask & (1u << 5)) ? "+Space" : "")
                  << ((inputMask & (1u << 6)) ? "+Look" : "")
                  << " move=(" << m_LastMoveInput.x << "," << m_LastMoveInput.y << ")"
                  << " drive=" << m_ForwardDriveAmount
                  << " speed=" << horizontalVelocity.length()
                  << " vel=(" << velocity.x << "," << velocity.y << "," << velocity.z << ")"
                  << " grounded=" << (m_Controller->isGrounded() ? "1" : "0")
                  << " pos=(" << position.x << "," << position.y << "," << position.z << ")"
                  << " bodyYaw=" << bodyYawDeg
                  << " camYaw=" << camYawDeg
                  << " camPitch=" << camPitchDeg
                  << " action=" << AnimActionName(m_AnimState)
                  << " clip=" << getClipName(m_ActionClipIndex)
                  << std::endl;
    }

    std::string getPrimaryClipSearchText(size_t clipIndex) const {
        if (!m_PrimarySkinned) {
            return {};
        }

        const auto& clips = m_PrimarySkinned->getAnimationClips();
        if (clipIndex >= clips.size() || !clips[clipIndex]) {
            return {};
        }

        std::string text = ToLower(clips[clipIndex]->getName());
        const auto& sources = m_PrimarySkinned->getAnimationClipSources();
        for (const auto& source : sources) {
            for (const auto& sourceClip : source.clips) {
                if (sourceClip == clips[clipIndex]) {
                    std::string stem = FileStem(source.path);
                    if (!stem.empty()) {
                        if (!text.empty()) {
                            text += ' ';
                        }
                        text += stem;
                    }
                    return text;
                }
            }
        }
        return text;
    }

    int findPrimaryClipIndexAny(const std::vector<std::string>& tokens) const {
        if (!m_PrimarySkinned) {
            return -1;
        }
        const auto& clips = m_PrimarySkinned->getAnimationClips();
        for (size_t i = 0; i < clips.size(); ++i) {
            if (!clips[i]) {
                continue;
            }
            std::string text = getPrimaryClipSearchText(i);
            for (const auto& token : tokens) {
                if (!token.empty() && text.find(token) != std::string::npos) {
                    return static_cast<int>(i);
                }
            }
        }
        return -1;
    }

    int findPrimaryLocomotionClipIndex(const std::vector<std::string>& includeTokens,
                                       const std::vector<std::string>& excludeTokens) const {
        if (!m_PrimarySkinned) {
            return -1;
        }
        const auto& clips = m_PrimarySkinned->getAnimationClips();
        for (size_t i = 0; i < clips.size(); ++i) {
            if (!clips[i]) {
                continue;
            }
            std::string text = getPrimaryClipSearchText(i);
            if (!ContainsAllTokens(text, includeTokens)) {
                continue;
            }
            bool excluded = false;
            for (const auto& token : excludeTokens) {
                if (!token.empty() && text.find(token) != std::string::npos) {
                    excluded = true;
                    break;
                }
            }
            if (!excluded) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    int findPrimaryDirectionalClipIndex(const std::vector<std::vector<std::string>>& tokenSets,
                                        const std::vector<std::string>& excludeTokens) const {
        for (const auto& tokens : tokenSets) {
            int index = findPrimaryLocomotionClipIndex(tokens, excludeTokens);
            if (index >= 0) {
                return index;
            }
        }
        return -1;
    }

    static bool IsLikelyMotionBoneName(const std::string& boneNameLower) {
        return boneNameLower == "root" ||
               boneNameLower == "hips" ||
               boneNameLower == "hip" ||
               boneNameLower == "pelvis" ||
               boneNameLower.find("root") != std::string::npos ||
               boneNameLower.find("hips") != std::string::npos ||
               boneNameLower.find("pelvis") != std::string::npos;
    }

    int findLikelyJumpClipIndex() const {
        if (!m_PrimarySkinned) {
            return -1;
        }

        const auto& clips = m_PrimarySkinned->getAnimationClips();
        float bestScore = 0.12f;
        int bestIndex = -1;

        for (size_t i = 0; i < clips.size(); ++i) {
            const auto& clip = clips[i];
            if (!clip) {
                continue;
            }

            float verticalRange = 0.0f;
            float planarRange = 0.0f;
            bool hasMotionChannel = false;

            for (const auto& channel : clip->getChannels()) {
                std::string boneName = ToLower(channel.boneName);
                if (!IsLikelyMotionBoneName(boneName) || channel.positionKeys.empty()) {
                    continue;
                }

                hasMotionChannel = true;
                float minY = channel.positionKeys.front().value.y;
                float maxY = minY;
                float minX = channel.positionKeys.front().value.x;
                float maxX = minX;
                float minZ = channel.positionKeys.front().value.z;
                float maxZ = minZ;
                for (const auto& key : channel.positionKeys) {
                    minY = std::min(minY, key.value.y);
                    maxY = std::max(maxY, key.value.y);
                    minX = std::min(minX, key.value.x);
                    maxX = std::max(maxX, key.value.x);
                    minZ = std::min(minZ, key.value.z);
                    maxZ = std::max(maxZ, key.value.z);
                }
                verticalRange = std::max(verticalRange, maxY - minY);
                planarRange = std::max(planarRange, std::max(maxX - minX, maxZ - minZ));
            }

            if (!hasMotionChannel) {
                continue;
            }

            float duration = std::max(0.01f, clip->getDurationSeconds());
            float score = verticalRange - (planarRange * 0.25f) - (duration * 0.02f);
            if (score > bestScore) {
                bestScore = score;
                bestIndex = static_cast<int>(i);
            }
        }

        return bestIndex;
    }

    void resolveClipMapping() {
        m_ClipMapping = ClipMapping();
        m_MeleeAttackClips.clear();
        m_MeleeAttackCursor = 0;
        if (!m_PrimarySkinned) {
            return;
        }
        const std::vector<std::string> genericIdleExclude = {"gun", "rifle", "aim", "reload", "fire", "looking", "taunt", "crouch", "block", "react", "attack", "combo", "equip", "disarm", "jump"};
        const std::vector<std::string> genericWalkExclude = {"strafe", "turn", "left", "right", "back", "backward", "reverse", "gun", "rifle", "aim", "reload", "fire", "start", "stop", "attack", "combo", "react", "block", "taunt", "crouch", "jump"};
        const std::vector<std::string> genericDirectionalExclude = {"gun", "rifle", "aim", "reload", "fire", "start", "stop", "attack", "combo", "react", "block", "taunt", "crouch", "jump"};
        const std::vector<std::string> meleeStandingExclude = {"looking", "taunt", "react", "block", "equip", "disarm", "attack", "combo", "kick", "jump", "turn"};

        m_ClipMapping.idle = findPrimaryLocomotionClipIndex({"unarmed", "idle"}, genericIdleExclude);
        if (m_ClipMapping.idle < 0) {
            m_ClipMapping.idle = findPrimaryLocomotionClipIndex({"idle"}, genericIdleExclude);
        }
        if (m_ClipMapping.idle < 0) {
            m_ClipMapping.idle = findPrimaryLocomotionClipIndex({"stand"}, genericIdleExclude);
        }
        if (m_ClipMapping.idle < 0) {
            m_ClipMapping.idle = findPrimaryClipIndexAny({"idle", "stand"});
        }
        m_ClipMapping.aimIdle = findPrimaryLocomotionClipIndex({"rifle", "aiming", "idle", "gun"}, {"start", "stop", "fire", "reload"});
        if (m_ClipMapping.aimIdle < 0) {
            m_ClipMapping.aimIdle = findPrimaryLocomotionClipIndex({"aim", "idle", "gun"}, {"start", "stop", "fire", "reload"});
        }
        if (m_ClipMapping.aimIdle < 0) {
            m_ClipMapping.aimIdle = findPrimaryLocomotionClipIndex({"standing", "block", "idle"}, {"react", "taunt"});
        }
        if (m_ClipMapping.aimIdle < 0) {
            m_ClipMapping.aimIdle = findPrimaryLocomotionClipIndex({"standing", "idle"}, meleeStandingExclude);
        }
        m_ClipMapping.walk = findPrimaryLocomotionClipIndex({"walking"}, genericWalkExclude);
        if (m_ClipMapping.walk < 0) {
            m_ClipMapping.walk = findPrimaryLocomotionClipIndex({"unarmed", "walk", "forward"}, genericWalkExclude);
        }
        if (m_ClipMapping.walk < 0) {
            m_ClipMapping.walk = findPrimaryLocomotionClipIndex({"unarmed", "walk"}, genericWalkExclude);
        }
        if (m_ClipMapping.walk < 0) {
            m_ClipMapping.walk = findPrimaryLocomotionClipIndex({"walk"}, genericWalkExclude);
        }
        if (m_ClipMapping.walk < 0) {
            m_ClipMapping.walk = findPrimaryClipIndexAny({"walk"});
        }
        m_ClipMapping.armedWalk = findPrimaryLocomotionClipIndex({"walking", "gun"}, {"strafe", "turn", "back", "backward", "reverse", "start", "stop"});
        if (m_ClipMapping.armedWalk < 0) {
            m_ClipMapping.armedWalk = findPrimaryLocomotionClipIndex({"walking", "with", "gun"}, {"strafe", "turn", "back", "backward", "reverse", "start", "stop"});
        }
        if (m_ClipMapping.armedWalk < 0) {
            m_ClipMapping.armedWalk = findPrimaryLocomotionClipIndex({"standing", "walk", "forward"}, meleeStandingExclude);
        }
        if (m_ClipMapping.armedWalk < 0) {
            m_ClipMapping.armedWalk = findPrimaryLocomotionClipIndex({"standing", "walk"}, {"left", "right", "back", "turn", "attack", "combo", "react", "block", "taunt", "jump"});
        }
        if (m_ClipMapping.armedWalk < 0) {
            m_ClipMapping.armedWalk = m_ClipMapping.walk;
        }
        m_ClipMapping.backpedal = findPrimaryDirectionalClipIndex(
            {{"unarmed", "walk", "back"}, {"walk", "back"}, {"backward"}, {"backpedal"}, {"reverse"}},
            {"strafe", "turn", "left", "right", "gun", "rifle", "aim", "reload", "fire", "start", "stop"});
        if (m_ClipMapping.backpedal < 0) {
            m_ClipMapping.backpedal = m_ClipMapping.walk;
        }
        m_ClipMapping.armedBackpedal = findPrimaryLocomotionClipIndex({"walking", "backwards", "gun"}, {"start", "stop"});
        if (m_ClipMapping.armedBackpedal < 0) {
            m_ClipMapping.armedBackpedal = findPrimaryLocomotionClipIndex({"back", "gun"}, {"run", "start", "stop"});
        }
        if (m_ClipMapping.armedBackpedal < 0) {
            m_ClipMapping.armedBackpedal = findPrimaryLocomotionClipIndex({"standing", "walk", "back"}, {"attack", "combo", "react", "block", "taunt", "jump"});
        }
        if (m_ClipMapping.armedBackpedal < 0) {
            m_ClipMapping.armedBackpedal = m_ClipMapping.backpedal;
        }
        m_ClipMapping.armedBackpedalRun = findPrimaryLocomotionClipIndex({"run", "backwards", "gun"}, {"start", "stop"});
        if (m_ClipMapping.armedBackpedalRun < 0) {
            m_ClipMapping.armedBackpedalRun = findPrimaryLocomotionClipIndex({"standing", "run", "back"}, {"attack", "combo", "react", "block", "taunt", "jump"});
        }
        if (m_ClipMapping.armedBackpedalRun < 0) {
            m_ClipMapping.armedBackpedalRun = m_ClipMapping.armedBackpedal;
        }
        m_ClipMapping.backpedalLeft = findPrimaryDirectionalClipIndex(
            {{"back", "left"}, {"left", "back"}, {"backward", "left"}, {"reverse", "left"}},
            genericDirectionalExclude);
        m_ClipMapping.backpedalRight = findPrimaryDirectionalClipIndex(
            {{"back", "right"}, {"right", "back"}, {"backward", "right"}, {"reverse", "right"}},
            genericDirectionalExclude);
        m_ClipMapping.strafeWalkLeft = findPrimaryDirectionalClipIndex(
            {{"left", "strafe", "walking"}, {"left", "strafe", "walk"}},
            genericDirectionalExclude);
        m_ClipMapping.strafeWalkRight = findPrimaryDirectionalClipIndex(
            {{"right", "strafe", "walking"}, {"right", "strafe", "walk"}},
            genericDirectionalExclude);
        m_ClipMapping.strafeLeft = findPrimaryDirectionalClipIndex(
            {{"left", "strafe"}, {"strafe", "left"}},
            genericDirectionalExclude);
        m_ClipMapping.strafeRight = findPrimaryDirectionalClipIndex(
            {{"right", "strafe"}, {"strafe", "right"}},
            genericDirectionalExclude);
        m_ClipMapping.armedStrafeLeft = findPrimaryDirectionalClipIndex(
            {{"strafe", "left", "gun"}, {"left", "gun"}},
            {"walk", "walking", "turn", "start", "stop", "back", "backward", "reverse"});
        m_ClipMapping.armedStrafeRight = findPrimaryDirectionalClipIndex(
            {{"strafe", "right", "gun"}, {"right", "gun"}},
            {"walk", "walking", "turn", "start", "stop", "back", "backward", "reverse"});
        if (m_ClipMapping.armedStrafeLeft < 0) {
            m_ClipMapping.armedStrafeLeft = findPrimaryLocomotionClipIndex({"standing", "walk", "left"}, {"attack", "combo", "react", "block", "taunt", "jump"});
        }
        if (m_ClipMapping.armedStrafeRight < 0) {
            m_ClipMapping.armedStrafeRight = findPrimaryLocomotionClipIndex({"standing", "walk", "right"}, {"attack", "combo", "react", "block", "taunt", "jump"});
        }
        if (m_ClipMapping.armedStrafeLeft < 0) {
            m_ClipMapping.armedStrafeLeft = m_ClipMapping.strafeLeft;
        }
        if (m_ClipMapping.armedStrafeRight < 0) {
            m_ClipMapping.armedStrafeRight = m_ClipMapping.strafeRight;
        }
        m_ClipMapping.turnLeft = findPrimaryDirectionalClipIndex(
            {{"left", "turn"}, {"turn", "left"}},
            {"strafe", "gun", "rifle", "aim", "reload", "fire"});
        m_ClipMapping.turnRight = findPrimaryDirectionalClipIndex(
            {{"right", "turn"}, {"turn", "right"}},
            {"strafe", "gun", "rifle", "aim", "reload", "fire"});
        m_ClipMapping.turnLeftSharp = findPrimaryDirectionalClipIndex(
            {{"left", "turn", "90"}, {"turn", "left", "90"}},
            {"strafe", "gun", "rifle", "aim", "reload", "fire"});
        m_ClipMapping.turnRightSharp = findPrimaryDirectionalClipIndex(
            {{"right", "turn", "90"}, {"turn", "right", "90"}},
            {"strafe", "gun", "rifle", "aim", "reload", "fire"});
        if (m_ClipMapping.backpedalLeft < 0) {
            m_ClipMapping.backpedalLeft = m_ClipMapping.strafeWalkLeft >= 0 ? m_ClipMapping.strafeWalkLeft : m_ClipMapping.strafeLeft;
        }
        if (m_ClipMapping.backpedalRight < 0) {
            m_ClipMapping.backpedalRight = m_ClipMapping.strafeWalkRight >= 0 ? m_ClipMapping.strafeWalkRight : m_ClipMapping.strafeRight;
        }
        if (m_ClipMapping.turnLeftSharp < 0) {
            m_ClipMapping.turnLeftSharp = m_ClipMapping.turnLeft;
        }
        if (m_ClipMapping.turnRightSharp < 0) {
            m_ClipMapping.turnRightSharp = m_ClipMapping.turnRight;
        }
        m_ClipMapping.run = findPrimaryLocomotionClipIndex({"running"}, genericWalkExclude);
        if (m_ClipMapping.run < 0) {
            m_ClipMapping.run = findPrimaryLocomotionClipIndex({"unarmed", "run", "forward"}, genericWalkExclude);
        }
        if (m_ClipMapping.run < 0) {
            m_ClipMapping.run = findPrimaryLocomotionClipIndex({"unarmed", "run"}, genericWalkExclude);
        }
        if (m_ClipMapping.run < 0) {
            m_ClipMapping.run = findPrimaryLocomotionClipIndex({"run"}, genericWalkExclude);
        }
        if (m_ClipMapping.run < 0) {
            m_ClipMapping.run = findPrimaryClipIndexAny({"run"});
        }
        m_ClipMapping.armedRun = findPrimaryLocomotionClipIndex({"run", "gun"}, {"back", "backward", "reverse", "start", "stop", "strafe"});
        if (m_ClipMapping.armedRun < 0) {
            m_ClipMapping.armedRun = findPrimaryLocomotionClipIndex({"rifle", "run", "gun"}, {"back", "backward", "reverse", "start", "stop", "strafe"});
        }
        if (m_ClipMapping.armedRun < 0) {
            m_ClipMapping.armedRun = findPrimaryLocomotionClipIndex({"standing", "run", "forward"}, meleeStandingExclude);
        }
        if (m_ClipMapping.armedRun < 0) {
            m_ClipMapping.armedRun = findPrimaryLocomotionClipIndex({"standing", "run"}, {"back", "left", "right", "turn", "attack", "combo", "react", "block", "taunt", "jump"});
        }
        if (m_ClipMapping.armedRun < 0) {
            m_ClipMapping.armedRun = m_ClipMapping.run;
        }
        m_ClipMapping.sprint = findPrimaryClipIndexAny({"sprint"});
        if (m_ClipMapping.sprint < 0) {
            m_ClipMapping.sprint = m_ClipMapping.run;
        }
        m_ClipMapping.equip = findPrimaryLocomotionClipIndex({"unarmed", "equip", "over", "shoulder"}, {"disarm"});
        if (m_ClipMapping.equip < 0) {
            m_ClipMapping.equip = findPrimaryLocomotionClipIndex({"unarmed", "equip", "underarm"}, {"disarm"});
        }
        if (m_ClipMapping.equip < 0) {
            m_ClipMapping.equip = findPrimaryClipIndexAny({"equip"});
        }
        m_ClipMapping.disarm = findPrimaryLocomotionClipIndex({"standing", "disarm", "over", "shoulder"}, {"equip"});
        if (m_ClipMapping.disarm < 0) {
            m_ClipMapping.disarm = findPrimaryLocomotionClipIndex({"standing", "disarm", "underarm"}, {"equip"});
        }
        if (m_ClipMapping.disarm < 0) {
            m_ClipMapping.disarm = findPrimaryClipIndexAny({"disarm"});
        }
        m_ClipMapping.fire = findPrimaryClipIndexAny({"firing", "rifle", "gun"});
        if (m_ClipMapping.fire < 0) {
            m_ClipMapping.fire = findPrimaryLocomotionClipIndex({"fire", "gun"}, {"reload"});
        }
        if (m_ClipMapping.fire < 0) {
            m_ClipMapping.fire = findPrimaryLocomotionClipIndex({"standing", "melee", "attack"}, {"kick", "combo", "react", "block", "taunt"});
        }
        AddUniqueClipIndex(m_MeleeAttackClips, findPrimaryLocomotionClipIndex({"standing", "melee", "combo", "attack", "ver", "1"}, {"react", "block", "taunt"}));
        AddUniqueClipIndex(m_MeleeAttackClips, findPrimaryLocomotionClipIndex({"standing", "melee", "combo", "attack", "ver", "2"}, {"react", "block", "taunt"}));
        AddUniqueClipIndex(m_MeleeAttackClips, findPrimaryLocomotionClipIndex({"standing", "melee", "combo", "attack", "ver", "3"}, {"react", "block", "taunt"}));
        AddUniqueClipIndex(m_MeleeAttackClips, findPrimaryLocomotionClipIndex({"standing", "melee", "attack", "kick", "ver", "1"}, {"react", "block", "taunt"}));
        AddUniqueClipIndex(m_MeleeAttackClips, findPrimaryLocomotionClipIndex({"standing", "melee", "attack", "kick", "ver", "2"}, {"react", "block", "taunt"}));
        AddUniqueClipIndex(m_MeleeAttackClips, findPrimaryLocomotionClipIndex({"standing", "melee", "attack", "horizontal"}, {"react", "block", "taunt"}));
        AddUniqueClipIndex(m_MeleeAttackClips, findPrimaryLocomotionClipIndex({"standing", "melee", "attack", "downward"}, {"react", "block", "taunt"}));
        AddUniqueClipIndex(m_MeleeAttackClips, findPrimaryLocomotionClipIndex({"standing", "melee", "attack", "backhand"}, {"react", "block", "taunt"}));
        AddUniqueClipIndex(m_MeleeAttackClips, findPrimaryLocomotionClipIndex({"standing", "melee", "attack", "360", "high"}, {"react", "block", "taunt"}));
        AddUniqueClipIndex(m_MeleeAttackClips, findPrimaryLocomotionClipIndex({"standing", "melee", "attack", "360", "low"}, {"react", "block", "taunt"}));
        AddUniqueClipIndex(m_MeleeAttackClips, findPrimaryLocomotionClipIndex({"standing", "melee", "run", "jump", "attack"}, {"react", "block", "taunt"}));
        if (m_ClipMapping.fire < 0 && !m_MeleeAttackClips.empty()) {
            m_ClipMapping.fire = m_MeleeAttackClips.front();
        }
        m_ClipMapping.reload = findPrimaryLocomotionClipIndex({"reload", "gun"}, {"fire"});
        if (m_ClipMapping.reload < 0) {
            m_ClipMapping.reload = findPrimaryLocomotionClipIndex({"standing", "block", "idle"}, {"react"});
        }
        m_ClipMapping.unarmedJump = findPrimaryLocomotionClipIndex({"unarmed", "jump"}, {"running"});
        m_ClipMapping.unarmedJumpRunning = findPrimaryLocomotionClipIndex({"unarmed", "jump", "running"}, {});
        m_ClipMapping.standingJump = findPrimaryLocomotionClipIndex({"standing", "jump"}, {});
        m_ClipMapping.jump = findPrimaryClipIndexAny({"jump", "hop"});
        if (m_ClipMapping.jump < 0) {
            m_ClipMapping.jump = findLikelyJumpClipIndex();
        }
        if (m_ClipMapping.unarmedJump < 0) {
            m_ClipMapping.unarmedJump = m_ClipMapping.jump;
        }
        if (m_ClipMapping.unarmedJumpRunning < 0) {
            m_ClipMapping.unarmedJumpRunning = m_ClipMapping.unarmedJump;
        }
        if (m_ClipMapping.standingJump < 0) {
            m_ClipMapping.standingJump = m_ClipMapping.jump;
        }
        m_ClipMapping.fall = findPrimaryClipIndexAny({"fall", "air"});
        if (m_ClipMapping.fall < 0) {
            m_ClipMapping.fall = m_ClipMapping.jump;
        }
    }

    void resolveAnimatorMapping() {
        m_AnimatorMapping = AnimatorMapping();
        if (!m_Animator) {
            return;
        }
        const auto& states = m_Animator->getStates();
        for (size_t i = 0; i < states.size(); ++i) {
            const auto& state = states[i];
            std::string name = ToLower(state.name);
            bool isArmedState = name.find("gun") != std::string::npos ||
                                name.find("rifle") != std::string::npos ||
                                name.find("aim") != std::string::npos ||
                                name.find("reload") != std::string::npos ||
                                name.find("fire") != std::string::npos;
            bool isStartStopState = name.find("start") != std::string::npos || name.find("stop") != std::string::npos;
            bool isBackwardState = name.find("back") != std::string::npos || name.find("reverse") != std::string::npos;

            if (m_AnimatorMapping.idleState < 0 && !isArmedState &&
                (name.find("idle") != std::string::npos || name.find("stand") != std::string::npos)) {
                m_AnimatorMapping.idleState = static_cast<int>(i);
                m_AnimatorMapping.idleClip = state.clipIndex;
            }
            if (m_AnimatorMapping.aimIdleState < 0 && isArmedState &&
                name.find("idle") != std::string::npos) {
                m_AnimatorMapping.aimIdleState = static_cast<int>(i);
                m_AnimatorMapping.aimIdleClip = state.clipIndex;
            }
            if (m_AnimatorMapping.walkState < 0 && !isArmedState && !isStartStopState && !isBackwardState &&
                name.find("walk") != std::string::npos &&
                name.find("strafe") == std::string::npos &&
                name.find("turn") == std::string::npos &&
                name.find("left") == std::string::npos &&
                name.find("right") == std::string::npos) {
                m_AnimatorMapping.walkState = static_cast<int>(i);
                m_AnimatorMapping.walkClip = state.clipIndex;
            }
            if (m_AnimatorMapping.armedWalkState < 0 && isArmedState && !isStartStopState && !isBackwardState &&
                name.find("walk") != std::string::npos &&
                name.find("strafe") == std::string::npos &&
                name.find("turn") == std::string::npos &&
                name.find("left") == std::string::npos &&
                name.find("right") == std::string::npos) {
                m_AnimatorMapping.armedWalkState = static_cast<int>(i);
                m_AnimatorMapping.armedWalkClip = state.clipIndex;
            }
            if (m_AnimatorMapping.backpedalState < 0 &&
                !isArmedState &&
                (name.find("back") != std::string::npos || name.find("reverse") != std::string::npos)) {
                m_AnimatorMapping.backpedalState = static_cast<int>(i);
                m_AnimatorMapping.backpedalClip = state.clipIndex;
            }
            if (m_AnimatorMapping.armedBackpedalState < 0 &&
                isArmedState && isBackwardState && name.find("run") == std::string::npos) {
                m_AnimatorMapping.armedBackpedalState = static_cast<int>(i);
                m_AnimatorMapping.armedBackpedalClip = state.clipIndex;
            }
            if (m_AnimatorMapping.armedBackpedalRunState < 0 &&
                isArmedState && isBackwardState && name.find("run") != std::string::npos) {
                m_AnimatorMapping.armedBackpedalRunState = static_cast<int>(i);
                m_AnimatorMapping.armedBackpedalRunClip = state.clipIndex;
            }
            if (m_AnimatorMapping.backpedalLeftState < 0 &&
                !isArmedState &&
                ((name.find("back") != std::string::npos || name.find("reverse") != std::string::npos) &&
                 name.find("left") != std::string::npos)) {
                m_AnimatorMapping.backpedalLeftState = static_cast<int>(i);
                m_AnimatorMapping.backpedalLeftClip = state.clipIndex;
            }
            if (m_AnimatorMapping.backpedalRightState < 0 &&
                !isArmedState &&
                ((name.find("back") != std::string::npos || name.find("reverse") != std::string::npos) &&
                 name.find("right") != std::string::npos)) {
                m_AnimatorMapping.backpedalRightState = static_cast<int>(i);
                m_AnimatorMapping.backpedalRightClip = state.clipIndex;
            }
            if (m_AnimatorMapping.strafeWalkLeftState < 0 &&
                name.find("strafe") != std::string::npos &&
                name.find("left") != std::string::npos &&
                name.find("walk") != std::string::npos) {
                m_AnimatorMapping.strafeWalkLeftState = static_cast<int>(i);
                m_AnimatorMapping.strafeWalkLeftClip = state.clipIndex;
            }
            if (m_AnimatorMapping.strafeWalkRightState < 0 &&
                name.find("strafe") != std::string::npos &&
                name.find("right") != std::string::npos &&
                name.find("walk") != std::string::npos) {
                m_AnimatorMapping.strafeWalkRightState = static_cast<int>(i);
                m_AnimatorMapping.strafeWalkRightClip = state.clipIndex;
            }
            if (m_AnimatorMapping.strafeLeftState < 0 &&
                ((name.find("strafe") != std::string::npos && name.find("left") != std::string::npos) ||
                 (name.find("left") != std::string::npos && name.find("turn") == std::string::npos))) {
                m_AnimatorMapping.strafeLeftState = static_cast<int>(i);
                m_AnimatorMapping.strafeLeftClip = state.clipIndex;
            }
            if (m_AnimatorMapping.armedStrafeLeftState < 0 && isArmedState &&
                name.find("strafe") != std::string::npos && name.find("left") != std::string::npos) {
                m_AnimatorMapping.armedStrafeLeftState = static_cast<int>(i);
                m_AnimatorMapping.armedStrafeLeftClip = state.clipIndex;
            }
            if (m_AnimatorMapping.strafeRightState < 0 &&
                ((name.find("strafe") != std::string::npos && name.find("right") != std::string::npos) ||
                 (name.find("right") != std::string::npos && name.find("turn") == std::string::npos))) {
                m_AnimatorMapping.strafeRightState = static_cast<int>(i);
                m_AnimatorMapping.strafeRightClip = state.clipIndex;
            }
            if (m_AnimatorMapping.armedStrafeRightState < 0 && isArmedState &&
                name.find("strafe") != std::string::npos && name.find("right") != std::string::npos) {
                m_AnimatorMapping.armedStrafeRightState = static_cast<int>(i);
                m_AnimatorMapping.armedStrafeRightClip = state.clipIndex;
            }
            if (m_AnimatorMapping.turnLeftState < 0 && !isArmedState &&
                name.find("turn") != std::string::npos && name.find("left") != std::string::npos) {
                m_AnimatorMapping.turnLeftState = static_cast<int>(i);
                m_AnimatorMapping.turnLeftClip = state.clipIndex;
            }
            if (m_AnimatorMapping.turnRightState < 0 && !isArmedState &&
                name.find("turn") != std::string::npos && name.find("right") != std::string::npos) {
                m_AnimatorMapping.turnRightState = static_cast<int>(i);
                m_AnimatorMapping.turnRightClip = state.clipIndex;
            }
            if (m_AnimatorMapping.turnLeftSharpState < 0 && !isArmedState &&
                name.find("turn") != std::string::npos && name.find("left") != std::string::npos &&
                name.find("90") != std::string::npos) {
                m_AnimatorMapping.turnLeftSharpState = static_cast<int>(i);
                m_AnimatorMapping.turnLeftSharpClip = state.clipIndex;
            }
            if (m_AnimatorMapping.turnRightSharpState < 0 && !isArmedState &&
                name.find("turn") != std::string::npos && name.find("right") != std::string::npos &&
                name.find("90") != std::string::npos) {
                m_AnimatorMapping.turnRightSharpState = static_cast<int>(i);
                m_AnimatorMapping.turnRightSharpClip = state.clipIndex;
            }
            if (m_AnimatorMapping.runState < 0 && !isArmedState && !isBackwardState &&
                name.find("run") != std::string::npos) {
                m_AnimatorMapping.runState = static_cast<int>(i);
                m_AnimatorMapping.runClip = state.clipIndex;
            }
            if (m_AnimatorMapping.armedRunState < 0 && isArmedState && !isBackwardState &&
                name.find("run") != std::string::npos) {
                m_AnimatorMapping.armedRunState = static_cast<int>(i);
                m_AnimatorMapping.armedRunClip = state.clipIndex;
            }
            if (m_AnimatorMapping.sprintState < 0 && !isArmedState && name.find("sprint") != std::string::npos) {
                m_AnimatorMapping.sprintState = static_cast<int>(i);
                m_AnimatorMapping.sprintClip = state.clipIndex;
            }
            if (m_AnimatorMapping.fireState < 0 && isArmedState &&
                (name.find("fire") != std::string::npos || name.find("shoot") != std::string::npos)) {
                m_AnimatorMapping.fireState = static_cast<int>(i);
                m_AnimatorMapping.fireClip = state.clipIndex;
            }
            if (m_AnimatorMapping.reloadState < 0 && isArmedState &&
                name.find("reload") != std::string::npos) {
                m_AnimatorMapping.reloadState = static_cast<int>(i);
                m_AnimatorMapping.reloadClip = state.clipIndex;
            }
            if (m_AnimatorMapping.jumpState < 0 && name.find("jump") != std::string::npos) {
                m_AnimatorMapping.jumpState = static_cast<int>(i);
                m_AnimatorMapping.jumpClip = state.clipIndex;
            }
            if (m_AnimatorMapping.fallState < 0 && (name.find("fall") != std::string::npos || name.find("air") != std::string::npos)) {
                m_AnimatorMapping.fallState = static_cast<int>(i);
                m_AnimatorMapping.fallClip = state.clipIndex;
            }
        }
        if (m_AnimatorMapping.sprintState < 0) {
            m_AnimatorMapping.sprintState = m_AnimatorMapping.runState;
            m_AnimatorMapping.sprintClip = m_AnimatorMapping.runClip;
        }
        if (m_AnimatorMapping.backpedalState < 0) {
            m_AnimatorMapping.backpedalState = m_AnimatorMapping.walkState;
            m_AnimatorMapping.backpedalClip = m_AnimatorMapping.walkClip;
        }
        if (m_AnimatorMapping.aimIdleState < 0) {
            m_AnimatorMapping.aimIdleState = m_AnimatorMapping.idleState;
            m_AnimatorMapping.aimIdleClip = m_AnimatorMapping.idleClip;
        }
        if (m_AnimatorMapping.armedWalkState < 0) {
            m_AnimatorMapping.armedWalkState = m_AnimatorMapping.walkState;
            m_AnimatorMapping.armedWalkClip = m_AnimatorMapping.walkClip;
        }
        if (m_AnimatorMapping.armedRunState < 0) {
            m_AnimatorMapping.armedRunState = m_AnimatorMapping.runState;
            m_AnimatorMapping.armedRunClip = m_AnimatorMapping.runClip;
        }
        if (m_AnimatorMapping.armedBackpedalState < 0) {
            m_AnimatorMapping.armedBackpedalState = m_AnimatorMapping.backpedalState;
            m_AnimatorMapping.armedBackpedalClip = m_AnimatorMapping.backpedalClip;
        }
        if (m_AnimatorMapping.armedBackpedalRunState < 0) {
            m_AnimatorMapping.armedBackpedalRunState = m_AnimatorMapping.armedBackpedalState;
            m_AnimatorMapping.armedBackpedalRunClip = m_AnimatorMapping.armedBackpedalClip;
        }
        if (m_AnimatorMapping.backpedalLeftState < 0) {
            m_AnimatorMapping.backpedalLeftState = m_AnimatorMapping.strafeWalkLeftState;
            m_AnimatorMapping.backpedalLeftClip = m_AnimatorMapping.strafeWalkLeftClip;
        }
        if (m_AnimatorMapping.backpedalRightState < 0) {
            m_AnimatorMapping.backpedalRightState = m_AnimatorMapping.strafeWalkRightState;
            m_AnimatorMapping.backpedalRightClip = m_AnimatorMapping.strafeWalkRightClip;
        }
        if (m_AnimatorMapping.strafeWalkLeftState < 0) {
            m_AnimatorMapping.strafeWalkLeftState = m_AnimatorMapping.strafeLeftState;
            m_AnimatorMapping.strafeWalkLeftClip = m_AnimatorMapping.strafeLeftClip;
        }
        if (m_AnimatorMapping.strafeWalkRightState < 0) {
            m_AnimatorMapping.strafeWalkRightState = m_AnimatorMapping.strafeRightState;
            m_AnimatorMapping.strafeWalkRightClip = m_AnimatorMapping.strafeRightClip;
        }
        if (m_AnimatorMapping.armedStrafeLeftState < 0) {
            m_AnimatorMapping.armedStrafeLeftState = m_AnimatorMapping.strafeLeftState;
            m_AnimatorMapping.armedStrafeLeftClip = m_AnimatorMapping.strafeLeftClip;
        }
        if (m_AnimatorMapping.armedStrafeRightState < 0) {
            m_AnimatorMapping.armedStrafeRightState = m_AnimatorMapping.strafeRightState;
            m_AnimatorMapping.armedStrafeRightClip = m_AnimatorMapping.strafeRightClip;
        }
        if (m_AnimatorMapping.turnLeftSharpState < 0) {
            m_AnimatorMapping.turnLeftSharpState = m_AnimatorMapping.turnLeftState;
            m_AnimatorMapping.turnLeftSharpClip = m_AnimatorMapping.turnLeftClip;
        }
        if (m_AnimatorMapping.turnRightSharpState < 0) {
            m_AnimatorMapping.turnRightSharpState = m_AnimatorMapping.turnRightState;
            m_AnimatorMapping.turnRightSharpClip = m_AnimatorMapping.turnRightClip;
        }
        if (m_AnimatorMapping.fallState < 0) {
            m_AnimatorMapping.fallState = m_AnimatorMapping.jumpState;
            m_AnimatorMapping.fallClip = m_AnimatorMapping.jumpClip;
        }
    }

    void resolveAnimatorParameters() {
        if (!m_Animator || m_AnimatorParamsResolved) {
            return;
        }
        m_AnimatorParamsResolved = true;
        const auto& params = m_Animator->getParameters();
        for (const auto& param : params) {
            std::string name = ToLower(param.name);
            if (m_SpeedParamName.empty() && (name.find("speed") != std::string::npos || name == "move")) {
                m_SpeedParamName = param.name;
            }
            if (m_MovingParamName.empty() && (name.find("moving") != std::string::npos || name.find("move") != std::string::npos)) {
                m_MovingParamName = param.name;
            }
            if (m_GroundedParamName.empty() && (name.find("ground") != std::string::npos || name.find("land") != std::string::npos)) {
                m_GroundedParamName = param.name;
            }
            if (m_SprintParamName.empty() && (name.find("sprint") != std::string::npos || name.find("run") != std::string::npos)) {
                m_SprintParamName = param.name;
            }
            if (m_MoveXParamName.empty() &&
                (name.find("strafe") != std::string::npos || name.find("horizontal") != std::string::npos ||
                 name.find("movex") != std::string::npos || name.find("inputx") != std::string::npos)) {
                m_MoveXParamName = param.name;
            }
            if (m_MoveYParamName.empty() &&
                (name.find("forward") != std::string::npos || name.find("movey") != std::string::npos ||
                 name.find("inputy") != std::string::npos || name.find("locomotiony") != std::string::npos)) {
                m_MoveYParamName = param.name;
            }
            if (m_TurnParamName.empty() && (name.find("turn") != std::string::npos || name.find("yaw") != std::string::npos)) {
                m_TurnParamName = param.name;
            }
            if (m_VerticalSpeedParamName.empty() && (name.find("vertical") != std::string::npos || name.find("fall") != std::string::npos || name.find("air") != std::string::npos)) {
                m_VerticalSpeedParamName = param.name;
            }
            if (m_JumpParamName.empty() && name.find("jump") != std::string::npos) {
                m_JumpParamName = param.name;
            }
        }
    }

    float resolveClipDuration(int clipIndex) const {
        if (!m_PrimarySkinned) {
            return 0.0f;
        }
        const auto& clips = m_PrimarySkinned->getAnimationClips();
        if (clipIndex < 0 || clipIndex >= static_cast<int>(clips.size())) {
            return 0.0f;
        }
        const auto& clip = clips[static_cast<size_t>(clipIndex)];
        return clip ? clip->getDurationSeconds() : 0.0f;
    }

    void setTargetsClip(int clipIndex, int stateIndex, bool looping, bool restart, float playbackSpeed = 1.0f, float blendDurationSeconds = 0.0f) {
        if (clipIndex < 0) {
            return;
        }
        if (m_Animator && m_Animator->isEnabled() && stateIndex >= 0) {
            if (m_Animator->getCurrentStateIndex() != stateIndex) {
                float blendDuration = blendDurationSeconds > 0.0f ? blendDurationSeconds : (looping ? 0.12f : 0.0f);
                m_Animator->setCurrentStateIndex(stateIndex, blendDuration, restart);
            }
            for (auto* skinned : m_AnimTargets) {
                if (!skinned) {
                    continue;
                }
                skinned->setPlaybackSpeed(std::max(0.01f, playbackSpeed));
                skinned->setLooping(looping);
                skinned->setPlaying(true);
                if (restart) {
                    skinned->setTimeSeconds(0.0f);
                }
            }
            return;
        }

        for (auto* skinned : m_AnimTargets) {
            if (!skinned) {
                continue;
            }
            const auto& clips = skinned->getAnimationClips();
            if (clipIndex < 0 || clipIndex >= static_cast<int>(clips.size())) {
                continue;
            }
            if (skinned->getActiveClipIndex() != clipIndex) {
                if (blendDurationSeconds > 0.0f) {
                    skinned->crossFadeToClip(clipIndex, blendDurationSeconds, restart);
                } else {
                    skinned->setActiveClipIndex(clipIndex);
                }
            } else if (restart) {
                skinned->setTimeSeconds(0.0f);
            }
            skinned->setPlaybackSpeed(std::max(0.01f, playbackSpeed));
            skinned->setLooping(looping);
            skinned->setPlaying(true);
        }
    }

    void startAction(AnimAction action, int clipIndex, int stateIndex, bool looping, bool restart) {
        if (clipIndex < 0) {
            return;
        }
        if (m_AnimState == action && clipIndex == m_ActionClipIndex && !restart) {
            return;
        }
        m_AnimState = action;
        m_ActionClipIndex = clipIndex;
        m_ActionDuration = resolveClipDuration(clipIndex);
        m_ActionElapsed = 0.0f;
        m_MeleeWindowUsesEvents = action == AnimAction::Fire &&
                                  isMeleeAttackClipIndex(clipIndex) &&
                                  clipHasEventToken(clipIndex, {
                                      "attack_window_open", "melee_window_open", "hitbox_on", "damage_window_open",
                                      "attack_window_close", "melee_window_close", "hitbox_off", "damage_window_close"
                                  });
        endMeleeHitWindow();
        float playbackSpeed = resolveActionPlaybackSpeed(action, clipIndex);
        float blendDuration = 0.0f;
        if (action == AnimAction::Equip || action == AnimAction::Disarm) {
            playbackSpeed = 1.2f;
            blendDuration = 0.05f;
        } else if (action == AnimAction::Fire && isMeleeAttackClipIndex(clipIndex)) {
            blendDuration = 0.06f;
        }
        m_ActionPlaybackSpeed = playbackSpeed;
        configureClipEventPlayback(clipIndex, looping, playbackSpeed, restart);
        setTargetsClip(clipIndex, stateIndex, looping, restart, playbackSpeed, blendDuration);
    }

    float resolveActionPlaybackSpeed(AnimAction action, int clipIndex) const {
        if (action == AnimAction::Jump) {
            return 1.75f;
        }
        if (action == AnimAction::Fall) {
            return 1.3f;
        }
        if (action == AnimAction::Equip || action == AnimAction::Disarm) {
            return 1.2f;
        }
        if (action == AnimAction::Fire && isMeleeAttackClipIndex(clipIndex)) {
            return 1.85f;
        }
        return 1.0f;
    }

    Entity* resolveDamageTarget(Entity* entity) const {
        Entity* cursor = entity;
        while (cursor) {
            if (cursor->getComponent<Health>()) {
                return cursor;
            }
            Transform* transform = cursor->getTransform();
            Transform* parent = transform ? transform->getParent() : nullptr;
            cursor = parent ? parent->getEntity() : nullptr;
        }
        return nullptr;
    }

    bool computeMeleeHitCenter(Math::Vector3& outCenter) const {
        Math::Matrix4x4 handWorld = Math::Matrix4x4::Identity;
        Math::Vector3 handPos = Math::Vector3::Zero;
        if (computeWeaponBoneWorld(handWorld)) {
            handPos = handWorld.transformPoint(Math::Vector3::Zero);
        } else if (m_BodyTransform) {
            handPos = m_BodyTransform->getPosition() + Math::Vector3(0.0f, 1.0f, 0.0f);
        } else {
            return false;
        }

        Math::Vector3 bodyForward = getBodyPlanarForward();
        outCenter = handPos + (bodyForward * m_MeleeHitForwardOffset) + (Math::Vector3::Up * m_MeleeHitUpOffset);
        return true;
    }

    void beginMeleeHitWindow() {
        m_MeleeHitWindowActive = true;
        m_MeleeHitSweepInitialized = false;
        m_MeleeHitVictims.clear();
    }

    void endMeleeHitWindow() {
        m_MeleeHitWindowActive = false;
        m_MeleeHitSweepInitialized = false;
        m_MeleeHitVictims.clear();
    }

    bool shouldUseFallbackMeleeWindow() const {
        return m_AnimState == AnimAction::Fire &&
               isMeleeAttackClipIndex(m_ActionClipIndex) &&
               !m_MeleeWindowUsesEvents;
    }

    void applyMeleeHitAtPoint(const Math::Vector3& center) {
        Scene* scene = m_Entity ? m_Entity->getScene() : nullptr;
        PhysicsWorld* physics = scene ? scene->getPhysicsWorld() : nullptr;
        if (!physics) {
            return;
        }

        std::vector<PhysicsOverlapHit> hits;
        if (physics->overlapSphere(center,
                                   m_MeleeHitRadius,
                                   hits,
                                   m_MeleeHitMask,
                                   m_MeleeHitTriggers,
                                   resolveBodyEntity()) <= 0) {
            return;
        }

        Entity* selfEntity = resolveBodyEntity();
        for (const auto& hit : hits) {
            Entity* target = resolveDamageTarget(hit.entity);
            if (!target || target == selfEntity) {
                continue;
            }
            UUID targetUUID = target->getUUID();
            if (m_MeleeHitVictims.find(targetUUID) != m_MeleeHitVictims.end()) {
                continue;
            }
            Health* health = target->getComponent<Health>();
            if (!health || health->isDead()) {
                continue;
            }
            m_MeleeHitVictims.insert(targetUUID);
            health->applyDamage(m_MeleeHitDamage);
        }
    }

    void updateMeleeHitTrace(float deltaTime) {
        (void)deltaTime;
        bool meleeAttackPlaying = m_AnimState == AnimAction::Fire &&
                                  isMeleeAttackClipIndex(m_ActionClipIndex) &&
                                  !isActionClipFinished();
        if (!meleeAttackPlaying) {
            endMeleeHitWindow();
            return;
        }

        if (shouldUseFallbackMeleeWindow()) {
            float normalized = m_ActionDuration > 0.0f ? Math::Clamp(m_ActionElapsed / m_ActionDuration, 0.0f, 1.0f) : 0.0f;
            bool shouldOpen = normalized >= 0.18f && normalized <= 0.52f;
            if (shouldOpen && !m_MeleeHitWindowActive) {
                beginMeleeHitWindow();
            } else if (!shouldOpen && m_MeleeHitWindowActive) {
                endMeleeHitWindow();
            }
        }

        if (!m_MeleeHitWindowActive) {
            return;
        }

        Math::Vector3 center = Math::Vector3::Zero;
        if (!computeMeleeHitCenter(center)) {
            return;
        }

        if (!m_MeleeHitSweepInitialized) {
            m_MeleeHitPreviousCenter = center;
            m_MeleeHitSweepInitialized = true;
            applyMeleeHitAtPoint(center);
            return;
        }

        Math::Vector3 delta = center - m_MeleeHitPreviousCenter;
        float distance = delta.length();
        int steps = std::max(1, static_cast<int>(std::ceil(distance / std::max(0.05f, m_MeleeHitRadius * 0.5f))));
        for (int i = 1; i <= steps; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(steps);
            applyMeleeHitAtPoint(Math::Vector3::Lerp(m_MeleeHitPreviousCenter, center, t));
        }
        m_MeleeHitPreviousCenter = center;
    }

    bool isActionClipFinished() const {
        if (m_ActionDuration <= 0.0f) {
            return true;
        }
        return m_ActionElapsed >= std::max(0.05f, m_ActionDuration - 0.01f);
    }

    void setManualAirborneClipOverride(bool enabled) {
        if (!m_Animator || m_ManualAirborneClipOverride == enabled) {
            return;
        }

        m_ManualAirborneClipOverride = enabled;
        m_Animator->setEnabled(!enabled);
        for (auto* skinned : m_AnimTargets) {
            if (!skinned) {
                continue;
            }
            skinned->setDrivenByAnimator(!enabled);
        }
    }

    void initializeOrbitFromCurrentCamera() {
        if (!m_BodyTransform || !m_CameraTransform) {
            m_TargetCameraDistance = m_CameraDistance;
            return;
        }
        Math::Vector3 pivot = m_BodyTransform->getPosition() + Math::Vector3(0.0f, m_PivotHeight, 0.0f);
        Math::Vector3 toCamera = m_CameraTransform->getPosition() - pivot;
        float dist = toCamera.length();
        if (dist > 0.001f) {
            m_CameraDistance = dist;
            m_TargetCameraDistance = dist;
            Math::Vector3 forward = (pivot - m_CameraTransform->getPosition()).normalized();
            float horizontalLength = std::sqrt(forward.x * forward.x + forward.z * forward.z);
            m_Yaw = std::atan2(forward.x, -forward.z);
            m_Pitch = std::atan2(-forward.y, horizontalLength);
            m_CameraPosition = m_CameraTransform->getPosition();
            m_CameraRotation = m_CameraTransform->getRotation();
            m_CameraInitialized = true;
            return;
        }
        m_TargetCameraDistance = m_CameraDistance;
    }

    Math::Vector3 computeCameraForward() const {
        float cosPitch = std::cos(m_Pitch);
        return Math::Vector3(
            std::sin(m_Yaw) * cosPitch,
            -std::sin(m_Pitch),
            -std::cos(m_Yaw) * cosPitch
        ).normalized();
    }

    void updateOrbit(float deltaTime, InputManager& input) {
        bool lookAllowed = !m_RequireLookButton || input.isMouseButtonPressed(m_LookButton);
        if (lookAllowed) {
            Math::Vector2 mouseDelta = input.getMouseDelta();
            m_Yaw += mouseDelta.x * m_MouseSensitivity;
            float pitchDelta = mouseDelta.y * m_MouseSensitivity * (m_InvertY ? -1.0f : 1.0f);
            m_Pitch -= pitchDelta;
        }
        m_Pitch = Math::Clamp(m_Pitch, m_MinPitch * Math::DEG_TO_RAD, m_MaxPitch * Math::DEG_TO_RAD);

        float scroll = input.getMouseScrollDelta();
        if (std::abs(scroll) > 0.0001f) {
            m_TargetCameraDistance = Math::Clamp(m_TargetCameraDistance - scroll * m_ZoomSpeed,
                                                 m_MinDistance, m_MaxDistance);
        }
        m_CameraDistance = Math::Lerp(m_CameraDistance, m_TargetCameraDistance, DampFactor(m_CameraSmoothSpeed, deltaTime));
    }

    void driveCharacter(float deltaTime, InputManager& input) {
        if (!m_Controller || !m_BodyTransform) {
            return;
        }
        if (!m_BodyOrientationInitialized) {
            initializeBodyOrientation();
        }

        m_JumpPressedThisFrame = false;
        if (m_JumpPhysicsPending) {
            m_JumpPhysicsDelayTimer = std::max(0.0f, m_JumpPhysicsDelayTimer - deltaTime);
            if (m_JumpPhysicsDelayTimer <= 0.0f) {
                m_Controller->requestJump();
                m_JumpPhysicsPending = false;
            }
        }

        float moveX = 0.0f;
        float moveY = 0.0f;
        if (input.isKeyPressed(KeyCode::A)) moveX -= 1.0f;
        if (input.isKeyPressed(KeyCode::D)) moveX += 1.0f;
        if (input.isKeyPressed(KeyCode::W)) moveY += 1.0f;
        if (input.isKeyPressed(KeyCode::S)) moveY -= 1.0f;

        Math::Vector2 moveInput(moveX, moveY);
        if (moveInput.lengthSquared() > 1.0f) {
            moveInput.normalize();
        }

        if (isMeleeAttackActive()) {
            moveInput = Math::Vector2::Zero;
        }

        bool isMoving = moveInput.lengthSquared() > 0.0001f;
        bool hasBackwardIntent = moveInput.y < -0.2f;
        bool wantsRun = isMoving && m_EnableSprint && input.isKeyPressed(KeyCode::Shift) && !hasBackwardIntent;
        if (input.isKeyDown(KeyCode::Q) && !isBlockingOneShotAction(m_AnimState)) {
            m_CombatMode = !m_CombatMode;
        }
        bool strafeMode = m_CombatMode;
        m_IsMovingDesired = isMoving;
        m_WantsSprintDesired = wantsRun;
        m_IsStrafeMode = strafeMode;
        m_LastMoveInput = moveInput;

        float baseSpeed = 0.0f;
        if (isMoving) {
            baseSpeed = hasBackwardIntent ? (m_WalkSpeed * 0.9f) : m_WalkSpeed;
            if (wantsRun) {
                baseSpeed = m_RunSpeed;
            }
        }

        Math::Vector3 cameraForward = getMovementCameraForward();
        Math::Vector3 cameraRight = cameraForward.cross(Math::Vector3::Up).normalized();

        Math::Vector3 desiredMove = (cameraRight * moveInput.x) + (cameraForward * moveInput.y);
        float desiredMagnitude = desiredMove.length();
        float driveScale = Math::Clamp(moveInput.length(), 0.0f, 1.0f);
        float forwardAmount = driveScale;
        m_IsPivotTurnDesired = false;
        m_PivotTurnDirection = 0.0f;
        m_PivotTurnMagnitude = 0.0f;
        if (desiredMagnitude > Math::EPSILON) {
            desiredMove /= desiredMagnitude;
            float desiredYaw = strafeMode ? YawFromDirection(cameraForward) : YawFromDirection(desiredMove);
            float currentYaw = m_BodyYaw;
            float yawDelta = NormalizeRadians(desiredYaw - currentYaw);
            m_PivotTurnMagnitude = std::abs(yawDelta);
            bool pivotTurn = !strafeMode && moveInput.y < -0.2f && std::abs(yawDelta) > 0.65f;
            if (pivotTurn) {
                m_IsPivotTurnDesired = true;
                m_PivotTurnDirection = yawDelta < 0.0f ? -1.0f : 1.0f;
                forwardAmount = 0.0f;
                driveScale = 0.0f;
            }
            float turnSpeed = pivotTurn ? std::max(7.5f, m_RotationSmoothSpeed * 2.2f)
                                        : std::max(1.5f, m_RotationSmoothSpeed);
            float nextYaw = MoveTowardsAngleRadians(currentYaw, desiredYaw, turnSpeed * deltaTime);
            applyBodyYaw(nextYaw);
        } else {
            m_Controller->clearWorldMoveDirection();
            if (strafeMode) {
                float desiredYaw = YawFromDirection(cameraForward);
                float currentYaw = m_BodyYaw;
                float yawDelta = NormalizeRadians(desiredYaw - currentYaw);
                m_PivotTurnMagnitude = std::abs(yawDelta);
                if (m_PivotTurnMagnitude > 0.12f) {
                    m_IsPivotTurnDesired = true;
                    m_PivotTurnDirection = yawDelta < 0.0f ? -1.0f : 1.0f;
                    float turnSpeed = std::max(2.2f, m_RotationSmoothSpeed * 1.35f);
                    float nextYaw = MoveTowardsAngleRadians(currentYaw, desiredYaw, turnSpeed * deltaTime);
                    applyBodyYaw(nextYaw);
                } else {
                    m_IsPivotTurnDesired = false;
                    m_PivotTurnDirection = 0.0f;
                    m_PivotTurnMagnitude = 0.0f;
                }
            }
        }

        if (!isMoving || desiredMagnitude <= Math::EPSILON) {
            forwardAmount = 0.0f;
            driveScale = 0.0f;
        }
        m_ForwardDriveAmount = forwardAmount;
        float desiredSpeed = baseSpeed * driveScale;
        m_Controller->setMoveSpeed(desiredSpeed);
        m_Controller->setMoveInput(Math::Vector2::Zero);
        if (desiredMagnitude > Math::EPSILON && !m_IsPivotTurnDesired) {
            m_Controller->setWorldMoveDirection(desiredMove);
        } else {
            m_Controller->clearWorldMoveDirection();
        }
        if (input.isKeyDown(KeyCode::Space)) {
            m_JumpPressedThisFrame = true;
            int jumpClipIndex = m_IsStrafeMode
                ? m_ClipMapping.standingJump
                : (wantsRun ? m_ClipMapping.unarmedJumpRunning : m_ClipMapping.unarmedJump);
            if (jumpClipIndex < 0) {
                jumpClipIndex = (m_ClipMapping.jump >= 0) ? m_ClipMapping.jump : m_ClipMapping.fall;
            }
            int jumpStateIndex = (m_Animator && m_AnimatorMapping.jumpState >= 0) ? m_AnimatorMapping.jumpState : -1;
            if (jumpStateIndex < 0 && m_Animator && m_AnimatorMapping.fallState >= 0) {
                jumpStateIndex = m_AnimatorMapping.fallState;
            }
            float jumpClipDuration = resolveClipDuration(jumpClipIndex);
            float visualJumpWindow = jumpClipDuration > 0.0f
                ? Math::Clamp(jumpClipDuration * 0.85f, 0.35f, 0.95f)
                : 0.45f;
            m_JumpAnimationLockTimer = std::max(m_JumpAnimationLockTimer, std::min(0.32f, visualJumpWindow));
            m_JumpVisualOverrideTimer = std::max(m_JumpVisualOverrideTimer, visualJumpWindow);
            if (jumpClipIndex >= 0) {
                if (m_Animator && jumpStateIndex < 0) {
                    setManualAirborneClipOverride(true);
                }
                startAction(AnimAction::Jump, jumpClipIndex, jumpStateIndex, false, true);
                playJumpAudio();
            }
            if (m_Controller->isGrounded()) {
                m_JumpPhysicsPending = true;
                m_JumpPhysicsDelayTimer = 0.06f;
            } else {
                m_Controller->requestJump();
            }
        }

        Math::Vector3 preMovePosition = m_BodyTransform->getPosition();
        if (m_DriveCharacterController) {
            m_Controller->OnUpdate(deltaTime);
        }

        if (desiredMagnitude > Math::EPSILON && desiredSpeed > Math::EPSILON) {
            Math::Vector3 postMovePosition = m_BodyTransform->getPosition();
            Math::Vector3 actualDelta = postMovePosition - preMovePosition;
            Math::Vector3 actualHorizontal(actualDelta.x, 0.0f, actualDelta.z);
            float expectedDistance = desiredSpeed * deltaTime;
            if (actualHorizontal.length() < expectedDistance * 0.2f) {
                Math::Vector3 fallbackMove = desiredMove * expectedDistance;
                Math::Vector3 fallbackPosition = sweepFallbackMove(postMovePosition, fallbackMove);
                Math::Vector3 fallbackDelta = fallbackPosition - postMovePosition;
                Math::Vector3 fallbackHorizontal(fallbackDelta.x, 0.0f, fallbackDelta.z);
                if (fallbackHorizontal.lengthSquared() > Math::EPSILON) {
                    m_BodyTransform->setPosition(fallbackPosition);
                    Math::Vector3 velocity = m_Controller->getVelocity();
                    velocity.x = fallbackDelta.x / deltaTime;
                    velocity.z = fallbackDelta.z / deltaTime;
                    m_Controller->setVelocity(velocity);
                }
            }
        }
    }

    void updateAnimation(float deltaTime) {
        if ((!m_Animator && m_AnimTargets.empty()) || !m_Controller) {
            if (!m_AnimInitialized) {
                refreshAnimationTargets();
                m_AnimInitialized = true;
            }
            if (!m_Animator && m_AnimTargets.empty()) {
                return;
            }
        }
        if (!m_AnimInitialized) {
            refreshAnimationTargets();
            m_AnimInitialized = true;
        }

        Math::Vector3 velocity = m_Controller->getVelocity();
        Math::Vector3 horizontalVelocity(velocity.x, 0.0f, velocity.z);
        float horizontalSpeed = horizontalVelocity.length();
        bool grounded = m_Controller->isGrounded();
        InputManager& input = InputManager::getInstance();
        if (isBlockingOneShotAction(m_AnimState)) {
            m_ActionElapsed += deltaTime * std::max(0.01f, m_ActionPlaybackSpeed);
        } else {
            m_ActionElapsed = 0.0f;
            m_ActionPlaybackSpeed = 1.0f;
            m_PendingMeleeAttackAudio = false;
            m_PendingMeleeAttackAudioClipIndex = -1;
            m_PendingMeleeAttackAudioTriggerTime = 0.0f;
        }
        flushMeleeAttackAudio();
        if (m_JumpAnimationLockTimer > 0.0f) {
            m_JumpAnimationLockTimer = std::max(0.0f, m_JumpAnimationLockTimer - deltaTime);
        }
        if (m_JumpVisualOverrideTimer > 0.0f) {
            m_JumpVisualOverrideTimer = std::max(0.0f, m_JumpVisualOverrideTimer - deltaTime);
        }
        bool jumpTakeoff = m_JumpPressedThisFrame || (m_PreviousGrounded && !grounded && velocity.y > -0.05f);
        bool landedThisFrame = !m_PreviousGrounded && grounded;
        if (jumpTakeoff) {
            m_JumpAnimationLockTimer = std::max(m_JumpAnimationLockTimer, 0.18f);
        }
        if (landedThisFrame && velocity.y <= 0.1f) {
            m_JumpAnimationLockTimer = 0.0f;
            m_JumpVisualOverrideTimer = 0.0f;
        }
        bool jumpLocked = m_JumpAnimationLockTimer > 0.0f;
        bool moving = m_IsMovingDesired || horizontalSpeed > 0.2f;
        bool running = moving && m_WantsSprintDesired && m_LastMoveInput.y > -0.1f;
        bool hasBackwardIntent = m_LastMoveInput.y < -0.2f;
        bool hasForwardIntent = m_LastMoveInput.y > 0.2f;
        bool hasLateralIntent = std::abs(m_LastMoveInput.x) > 0.35f;
        bool hasJumpClip = m_ClipMapping.jump >= 0 || m_ClipMapping.fall >= 0;
        bool forceJumpVisual = hasJumpClip && !grounded && (m_JumpVisualOverrideTimer > 0.0f || jumpTakeoff || m_AnimState == AnimAction::Jump);
        bool jumpVisualActive = forceJumpVisual || (!grounded && (velocity.y > 0.05f || jumpLocked));
        bool rising = jumpVisualActive;
        bool falling = !jumpVisualActive && !grounded && velocity.y < -0.15f && !jumpLocked;
        updateFootstepAudio(deltaTime, grounded, moving, running, rising, falling);
        bool firePressed = input.isMouseButtonPressed(MouseButton::Left);
        bool fireTriggered = m_IsStrafeMode && firePressed && !m_LeftMouseWasDown;
        m_LeftMouseWasDown = firePressed;
        bool reloadTriggered = m_IsStrafeMode && input.isKeyDown(KeyCode::R);
        bool hasAnimatorAirborneState = m_Animator &&
            (m_AnimatorMapping.jumpState >= 0 || m_AnimatorMapping.fallState >= 0);
        bool needsManualAirborneOverride = m_Animator && (forceJumpVisual || (hasJumpClip && !hasAnimatorAirborneState));

        if (needsManualAirborneOverride) {
            setManualAirborneClipOverride(jumpVisualActive || falling);
        } else if (m_ManualAirborneClipOverride && grounded &&
                   !isBlockingOneShotAction(m_AnimState)) {
            setManualAirborneClipOverride(false);
        }

        Animator* activeAnimator = (m_Animator && m_Animator->isEnabled() && !m_ManualAirborneClipOverride) ? m_Animator : nullptr;
        if (activeAnimator) {
            processAnimatorFiredEvents(activeAnimator);
        } else {
            processClipEvents(deltaTime);
        }

        if (activeAnimator) {
            resolveAnimatorParameters();
            if (!m_SpeedParamName.empty()) {
                float denom = std::max(0.01f, m_RunSpeed);
                activeAnimator->setParameterFloat(m_SpeedParamName, Math::Clamp(horizontalSpeed / denom, 0.0f, 1.5f));
            }
            if (!m_MovingParamName.empty()) {
                activeAnimator->setParameterBool(m_MovingParamName, moving);
            }
            if (!m_GroundedParamName.empty()) {
                activeAnimator->setParameterBool(m_GroundedParamName, grounded);
            }
            if (!m_SprintParamName.empty()) {
                activeAnimator->setParameterBool(m_SprintParamName, running);
            }
            if (!m_MoveXParamName.empty()) {
                activeAnimator->setParameterFloat(m_MoveXParamName, m_LastMoveInput.x);
            }
            if (!m_MoveYParamName.empty()) {
                activeAnimator->setParameterFloat(m_MoveYParamName, m_LastMoveInput.y);
            }
            if (!m_TurnParamName.empty()) {
                float turnAmount = 0.0f;
                if (m_LastMoveInput.lengthSquared() > 0.0001f) {
                    Math::Vector3 cameraForward = getMovementCameraForward();
                    Math::Vector3 cameraRight = cameraForward.cross(Math::Vector3::Up).normalized();
                    Math::Vector3 desiredMove = (cameraRight * m_LastMoveInput.x) + (cameraForward * m_LastMoveInput.y);
                    if (desiredMove.lengthSquared() > Math::EPSILON) {
                        desiredMove.normalize();
                        turnAmount = Math::Clamp(NormalizeRadians(YawFromDirection(desiredMove) - m_BodyYaw) / Math::HALF_PI, -1.0f, 1.0f);
                    }
                }
                activeAnimator->setParameterFloat(m_TurnParamName, turnAmount);
            }
            if (!m_VerticalSpeedParamName.empty()) {
                activeAnimator->setParameterFloat(m_VerticalSpeedParamName, velocity.y);
            }
            if (!m_JumpParamName.empty()) {
                if (jumpTakeoff) {
                    if (!activeAnimator->setTrigger(m_JumpParamName)) {
                        activeAnimator->setParameterBool(m_JumpParamName, true);
                    }
                } else if (grounded) {
                    activeAnimator->setParameterBool(m_JumpParamName, false);
                } else if (rising || falling) {
                    activeAnimator->setParameterBool(m_JumpParamName, true);
                }
            }
        }

        if (grounded && m_JumpVisualOverrideTimer <= 0.0f && velocity.y <= 0.05f) {
            m_JumpAnimationLockTimer = 0.0f;
            m_JumpVisualOverrideTimer = 0.0f;
        }

        bool hasDirectionalAnimator = activeAnimator &&
            (!m_MoveXParamName.empty() || !m_MoveYParamName.empty() || !m_TurnParamName.empty());

        if (!rising && !falling) {
            if (reloadTriggered) {
                int reloadClipIndex = activeAnimator && m_AnimatorMapping.reloadClip >= 0 ? m_AnimatorMapping.reloadClip : m_ClipMapping.reload;
                int reloadStateIndex = activeAnimator ? m_AnimatorMapping.reloadState : -1;
                if (reloadClipIndex >= 0) {
                    if (m_Animator) {
                        setManualAirborneClipOverride(true);
                    }
                    startAction(AnimAction::Reload, reloadClipIndex, reloadStateIndex, false, true);
                    return;
                }
            }

            bool useMeleeAttack = m_IsStrafeMode && !m_MeleeAttackClips.empty();
            bool meleeAttackActive = m_AnimState == AnimAction::Fire && isMeleeAttackClipIndex(m_ActionClipIndex);
            if (fireTriggered && useMeleeAttack) {
                if (meleeAttackActive && !isActionClipFinished()) {
                    m_QueuedMeleeAttack = true;
                } else {
                    int fireClipIndex = m_MeleeAttackClips[static_cast<size_t>(m_MeleeAttackCursor % m_MeleeAttackClips.size())];
                    m_MeleeAttackCursor = (m_MeleeAttackCursor + 1) % static_cast<int>(m_MeleeAttackClips.size());
                    if (fireClipIndex >= 0) {
                        if (m_Animator) {
                            setManualAirborneClipOverride(true);
                        }
                        startAction(AnimAction::Fire, fireClipIndex, -1, false, true);
                        queueMeleeAttackAudio(fireClipIndex);
                        m_QueuedMeleeAttack = false;
                        return;
                    }
                }
            } else if (fireTriggered && m_UseCharacterFireAnimation) {
                int fireClipIndex = activeAnimator && m_AnimatorMapping.fireClip >= 0 ? m_AnimatorMapping.fireClip : m_ClipMapping.fire;
                int fireStateIndex = activeAnimator ? m_AnimatorMapping.fireState : -1;
                if (fireClipIndex >= 0) {
                    if (m_Animator) {
                        setManualAirborneClipOverride(true);
                    }
                    startAction(AnimAction::Fire, fireClipIndex, fireStateIndex, false, true);
                    return;
                }
            }

            if (meleeAttackActive && m_QueuedMeleeAttack && isQueuedMeleeChainReady()) {
                int fireClipIndex = m_MeleeAttackClips[static_cast<size_t>(m_MeleeAttackCursor % m_MeleeAttackClips.size())];
                m_MeleeAttackCursor = (m_MeleeAttackCursor + 1) % static_cast<int>(m_MeleeAttackClips.size());
                if (fireClipIndex >= 0) {
                    if (m_Animator) {
                        setManualAirborneClipOverride(true);
                    }
                    startAction(AnimAction::Fire, fireClipIndex, -1, false, true);
                    queueMeleeAttackAudio(fireClipIndex);
                    m_QueuedMeleeAttack = false;
                    return;
                }
            }

            if (isBlockingOneShotAction(m_AnimState) && !isActionClipFinished()) {
                return;
            }

            if (isBlockingOneShotAction(m_AnimState) &&
                isActionClipFinished() && m_ManualAirborneClipOverride && grounded) {
                setManualAirborneClipOverride(false);
                m_QueuedMeleeAttack = false;
            }
        }

        AnimAction movementAction = AnimAction::Idle;
        int clipIndex = (m_IsStrafeMode && ((activeAnimator && m_AnimatorMapping.aimIdleClip >= 0) || m_ClipMapping.aimIdle >= 0))
            ? (activeAnimator && m_AnimatorMapping.aimIdleClip >= 0 ? m_AnimatorMapping.aimIdleClip : m_ClipMapping.aimIdle)
            : (activeAnimator && m_AnimatorMapping.idleClip >= 0 ? m_AnimatorMapping.idleClip : m_ClipMapping.idle);
        int stateIndex = (m_IsStrafeMode && activeAnimator && m_AnimatorMapping.aimIdleState >= 0)
            ? m_AnimatorMapping.aimIdleState
            : (activeAnimator ? m_AnimatorMapping.idleState : -1);
        if (m_IsStrafeMode) {
            movementAction = AnimAction::AimIdle;
        }

        if (rising) {
            movementAction = AnimAction::Jump;
            clipIndex = activeAnimator && m_AnimatorMapping.jumpClip >= 0 ? m_AnimatorMapping.jumpClip : m_ClipMapping.jump;
            stateIndex = activeAnimator ? m_AnimatorMapping.jumpState : -1;
            if (clipIndex < 0) {
                clipIndex = activeAnimator && m_AnimatorMapping.fallClip >= 0 ? m_AnimatorMapping.fallClip : m_ClipMapping.fall;
                stateIndex = activeAnimator ? m_AnimatorMapping.fallState : -1;
            }
        } else if (falling) {
            movementAction = AnimAction::Fall;
            clipIndex = activeAnimator && m_AnimatorMapping.fallClip >= 0 ? m_AnimatorMapping.fallClip : m_ClipMapping.fall;
            stateIndex = activeAnimator ? m_AnimatorMapping.fallState : -1;
            if (clipIndex < 0) {
                movementAction = AnimAction::Jump;
                clipIndex = activeAnimator && m_AnimatorMapping.jumpClip >= 0 ? m_AnimatorMapping.jumpClip : m_ClipMapping.jump;
                stateIndex = activeAnimator ? m_AnimatorMapping.jumpState : -1;
            }
        } else if ((moving || (m_IsStrafeMode && m_IsPivotTurnDesired)) && !hasDirectionalAnimator) {
            bool useSharpTurn = m_PivotTurnMagnitude > 0.95f;
            if (m_IsPivotTurnDesired && m_PivotTurnDirection < 0.0f &&
                       ((activeAnimator && (useSharpTurn ? m_AnimatorMapping.turnLeftSharpClip >= 0 : m_AnimatorMapping.turnLeftClip >= 0)) ||
                        (useSharpTurn ? m_ClipMapping.turnLeftSharp >= 0 : m_ClipMapping.turnLeft >= 0))) {
                movementAction = AnimAction::TurnLeft;
                clipIndex = useSharpTurn
                    ? (activeAnimator && m_AnimatorMapping.turnLeftSharpClip >= 0 ? m_AnimatorMapping.turnLeftSharpClip : m_ClipMapping.turnLeftSharp)
                    : (activeAnimator && m_AnimatorMapping.turnLeftClip >= 0 ? m_AnimatorMapping.turnLeftClip : m_ClipMapping.turnLeft);
                stateIndex = useSharpTurn
                    ? (activeAnimator ? m_AnimatorMapping.turnLeftSharpState : -1)
                    : (activeAnimator ? m_AnimatorMapping.turnLeftState : -1);
            } else if (m_IsPivotTurnDesired && m_PivotTurnDirection > 0.0f &&
                       ((activeAnimator && (useSharpTurn ? m_AnimatorMapping.turnRightSharpClip >= 0 : m_AnimatorMapping.turnRightClip >= 0)) ||
                        (useSharpTurn ? m_ClipMapping.turnRightSharp >= 0 : m_ClipMapping.turnRight >= 0))) {
                movementAction = AnimAction::TurnRight;
                clipIndex = useSharpTurn
                    ? (activeAnimator && m_AnimatorMapping.turnRightSharpClip >= 0 ? m_AnimatorMapping.turnRightSharpClip : m_ClipMapping.turnRightSharp)
                    : (activeAnimator && m_AnimatorMapping.turnRightClip >= 0 ? m_AnimatorMapping.turnRightClip : m_ClipMapping.turnRight);
                stateIndex = useSharpTurn
                    ? (activeAnimator ? m_AnimatorMapping.turnRightSharpState : -1)
                    : (activeAnimator ? m_AnimatorMapping.turnRightState : -1);
            } else if (m_IsStrafeMode && hasBackwardIntent && m_LastMoveInput.x < -0.2f &&
                       ((activeAnimator && m_AnimatorMapping.backpedalLeftClip >= 0) || m_ClipMapping.backpedalLeft >= 0)) {
                movementAction = AnimAction::BackpedalLeft;
                clipIndex = activeAnimator && m_AnimatorMapping.backpedalLeftClip >= 0 ? m_AnimatorMapping.backpedalLeftClip : m_ClipMapping.backpedalLeft;
                stateIndex = activeAnimator ? m_AnimatorMapping.backpedalLeftState : -1;
            } else if (m_IsStrafeMode && hasBackwardIntent && m_LastMoveInput.x > 0.2f &&
                       ((activeAnimator && m_AnimatorMapping.backpedalRightClip >= 0) || m_ClipMapping.backpedalRight >= 0)) {
                movementAction = AnimAction::BackpedalRight;
                clipIndex = activeAnimator && m_AnimatorMapping.backpedalRightClip >= 0 ? m_AnimatorMapping.backpedalRightClip : m_ClipMapping.backpedalRight;
                stateIndex = activeAnimator ? m_AnimatorMapping.backpedalRightState : -1;
            } else if (m_IsStrafeMode && hasBackwardIntent &&
                       ((activeAnimator && m_AnimatorMapping.backpedalClip >= 0) || m_ClipMapping.backpedal >= 0)) {
                movementAction = AnimAction::Backpedal;
                bool useArmedRunBackpedal = running &&
                    ((activeAnimator && m_AnimatorMapping.armedBackpedalRunClip >= 0) || m_ClipMapping.armedBackpedalRun >= 0);
                clipIndex = useArmedRunBackpedal
                    ? (activeAnimator && m_AnimatorMapping.armedBackpedalRunClip >= 0 ? m_AnimatorMapping.armedBackpedalRunClip : m_ClipMapping.armedBackpedalRun)
                    : (activeAnimator && m_AnimatorMapping.armedBackpedalClip >= 0 ? m_AnimatorMapping.armedBackpedalClip : m_ClipMapping.armedBackpedal);
                stateIndex = useArmedRunBackpedal
                    ? (activeAnimator ? m_AnimatorMapping.armedBackpedalRunState : -1)
                    : (activeAnimator ? m_AnimatorMapping.armedBackpedalState : -1);
            } else if (m_IsStrafeMode && m_LastMoveInput.x < -0.2f &&
                       ((activeAnimator && ((running && m_AnimatorMapping.strafeLeftClip >= 0) || m_AnimatorMapping.strafeWalkLeftClip >= 0)) ||
                        ((running && m_ClipMapping.strafeLeft >= 0) || m_ClipMapping.strafeWalkLeft >= 0))) {
                movementAction = running ? AnimAction::StrafeLeft : AnimAction::StrafeWalkLeft;
                clipIndex = activeAnimator && m_AnimatorMapping.armedStrafeLeftClip >= 0 ? m_AnimatorMapping.armedStrafeLeftClip : m_ClipMapping.armedStrafeLeft;
                stateIndex = activeAnimator ? m_AnimatorMapping.armedStrafeLeftState : -1;
            } else if (m_IsStrafeMode && m_LastMoveInput.x > 0.2f &&
                       ((activeAnimator && ((running && m_AnimatorMapping.strafeRightClip >= 0) || m_AnimatorMapping.strafeWalkRightClip >= 0)) ||
                        ((running && m_ClipMapping.strafeRight >= 0) || m_ClipMapping.strafeWalkRight >= 0))) {
                movementAction = running ? AnimAction::StrafeRight : AnimAction::StrafeWalkRight;
                clipIndex = activeAnimator && m_AnimatorMapping.armedStrafeRightClip >= 0 ? m_AnimatorMapping.armedStrafeRightClip : m_ClipMapping.armedStrafeRight;
                stateIndex = activeAnimator ? m_AnimatorMapping.armedStrafeRightState : -1;
            } else if (running &&
                       ((activeAnimator && m_AnimatorMapping.runClip >= 0) || m_ClipMapping.run >= 0)) {
                movementAction = AnimAction::Run;
                clipIndex = m_IsStrafeMode
                    ? (activeAnimator && m_AnimatorMapping.armedRunClip >= 0 ? m_AnimatorMapping.armedRunClip : m_ClipMapping.armedRun)
                    : (activeAnimator && m_AnimatorMapping.runClip >= 0 ? m_AnimatorMapping.runClip : m_ClipMapping.run);
                stateIndex = m_IsStrafeMode
                    ? (activeAnimator ? m_AnimatorMapping.armedRunState : -1)
                    : (activeAnimator ? m_AnimatorMapping.runState : -1);
            } else {
                movementAction = AnimAction::Walk;
                clipIndex = m_IsStrafeMode
                    ? (activeAnimator && m_AnimatorMapping.armedWalkClip >= 0 ? m_AnimatorMapping.armedWalkClip : m_ClipMapping.armedWalk)
                    : (activeAnimator && m_AnimatorMapping.walkClip >= 0 ? m_AnimatorMapping.walkClip : m_ClipMapping.walk);
                stateIndex = m_IsStrafeMode
                    ? (activeAnimator ? m_AnimatorMapping.armedWalkState : -1)
                    : (activeAnimator ? m_AnimatorMapping.walkState : -1);
                if (hasBackwardIntent && m_ClipMapping.walk < 0 && m_ClipMapping.idle >= 0) {
                    movementAction = AnimAction::Idle;
                    clipIndex = m_ClipMapping.idle;
                    stateIndex = activeAnimator ? m_AnimatorMapping.idleState : -1;
                }
            }
        }

        bool isTurnAction = movementAction == AnimAction::TurnLeft ||
                            movementAction == AnimAction::TurnRight ||
                            movementAction == AnimAction::TurnLeftSharp ||
                            movementAction == AnimAction::TurnRightSharp;

        if (isTurnAction && clipIndex >= 0) {
            bool restartAction = shouldRestartActionOnEntry(movementAction) && (movementAction != m_AnimState || clipIndex != m_ActionClipIndex);
            startAction(movementAction, clipIndex, stateIndex, false, restartAction);
        } else if (!hasDirectionalAnimator && clipIndex >= 0) {
            bool restartAction = shouldRestartActionOnEntry(movementAction) && (movementAction != m_AnimState);
            startAction(movementAction, clipIndex, stateIndex, movementAction != AnimAction::Jump && movementAction != AnimAction::Fall, restartAction);
        } else if (hasDirectionalAnimator && (rising || falling) && clipIndex >= 0) {
            bool restartAction = shouldRestartActionOnEntry(movementAction) && (movementAction != m_AnimState);
            startAction(movementAction, clipIndex, stateIndex, false, restartAction);
        } else if (hasDirectionalAnimator && clipIndex >= 0) {
            bool looping = movementAction != AnimAction::Jump && movementAction != AnimAction::Fall &&
                           movementAction != AnimAction::Fire && movementAction != AnimAction::Reload;
            bool restartEvents = (clipIndex != m_EventClipIndex);
            configureClipEventPlayback(clipIndex,
                                       looping,
                                       resolveActionPlaybackSpeed(movementAction, clipIndex),
                                       restartEvents);
        }

        m_PreviousGrounded = grounded;
    }

    void updateCamera(float deltaTime) {
        if (!m_BodyTransform || !m_CameraTransform) {
            return;
        }

        if (m_CameraTransform->getEntity()) {
            if (auto* camera = m_CameraTransform->getEntity()->getComponent<Camera>()) {
                Camera::setMainCamera(camera);
            }
        }

        Scene* scene = m_Entity ? m_Entity->getScene() : nullptr;
        PhysicsWorld* physics = scene ? scene->getPhysicsWorld() : nullptr;

        float targetShoulderOffset = m_IsStrafeMode ? m_AimShoulderOffset : m_ShoulderOffset;
        float targetDistance = m_IsStrafeMode ? m_AimCameraDistance : m_CameraDistance;
        float targetPivotHeight = m_PivotHeight + (m_IsStrafeMode ? m_AimPivotHeightOffset : 0.0f);
        Math::Vector3 pivot = m_BodyTransform->getPosition() + Math::Vector3(0.0f, targetPivotHeight, 0.0f);
        Math::Vector3 forward = computeCameraForward();
        Math::Vector3 right = forward.cross(Math::Vector3::Up).normalized();
        Math::Vector3 shoulderPivot = pivot + right * targetShoulderOffset;
        Math::Vector3 desiredPosition = shoulderPivot - forward * targetDistance;

        if (physics && m_CameraCollisionRadius > 0.0f) {
            Math::Vector3 castVector = desiredPosition - shoulderPivot;
            float castDistance = castVector.length();
            if (castDistance > 0.001f) {
                PhysicsRaycastHit hit{};
                Math::Vector3 castDir = castVector / castDistance;
                const Entity* ignore = m_BodyTransform ? m_BodyTransform->getEntity() : m_Entity;
                if (physics->sphereCast(shoulderPivot, m_CameraCollisionRadius, castDir, castDistance, hit,
                                        PhysicsWorld::kAllLayersMask, false, ignore)) {
                    desiredPosition = shoulderPivot + castDir * std::max(0.0f, hit.distance - m_CameraCollisionRadius * 1.1f);
                }
            }
        }

        Math::Vector3 lookTarget = m_IsStrafeMode
            ? (pivot + right * m_AimLookHorizontalOffset + forward * m_AimLookAheadDistance)
            : (pivot + forward * m_LookAhead);
        Math::Quaternion desiredRotation = Math::Quaternion::LookRotation((lookTarget - desiredPosition).normalized(), Math::Vector3::Up);

        if (!m_CameraInitialized) {
            m_CameraPosition = desiredPosition;
            m_CameraRotation = desiredRotation;
            m_CameraInitialized = true;
        } else {
            float posT = DampFactor(m_PositionSmoothSpeed, deltaTime);
            float rotT = DampFactor(m_CameraSmoothSpeed, deltaTime);
            m_CameraPosition = Math::Vector3::Lerp(m_CameraPosition, desiredPosition, posT);
            m_CameraRotation = Math::Quaternion::Slerp(m_CameraRotation, desiredRotation, rotT);
        }

        m_CameraTransform->setPosition(m_CameraPosition);
        m_CameraTransform->setRotation(m_CameraRotation);
    }

private:
    float m_MouseSensitivity;
    bool m_InvertY;
    bool m_RequireLookButton;
    MouseButton m_LookButton;
    float m_MinPitch;
    float m_MaxPitch;
    float m_PivotHeight;
    float m_LookAhead;
    float m_ShoulderOffset;
    float m_AimShoulderOffset;
    float m_CameraDistance;
    float m_AimCameraDistance;
    float m_AimLookAheadDistance;
    float m_AimLookHorizontalOffset;
    float m_AimPivotHeightOffset;
    float m_TargetCameraDistance = 3.6f;
    float m_MinDistance;
    float m_MaxDistance;
    float m_ZoomSpeed;
    float m_CameraCollisionRadius;
    float m_PositionSmoothSpeed;
    float m_RotationSmoothSpeed;
    float m_CameraSmoothSpeed;
    float m_WalkSpeed;
    float m_RunSpeed;
    float m_SprintSpeed;
    bool m_EnableSprint;
    bool m_DriveCharacterController;
    float m_MeleeHitDamage = 34.0f;
    float m_MeleeHitRadius = 0.42f;
    float m_MeleeHitForwardOffset = 0.34f;
    float m_MeleeHitUpOffset = 0.02f;
    uint32_t m_MeleeHitMask = PhysicsWorld::kAllLayersMask;
    bool m_MeleeHitTriggers = false;
    bool m_UseCharacterFireAnimation;
    bool m_DebugLogging;

    CharacterController* m_Controller = nullptr;
    Transform* m_BodyTransform = nullptr;
    Transform* m_CameraTransform = nullptr;
    Animator* m_Animator = nullptr;
    SkinnedMeshRenderer* m_PrimarySkinned = nullptr;
    std::vector<SkinnedMeshRenderer*> m_AnimTargets;
    ClipMapping m_ClipMapping;
    AnimatorMapping m_AnimatorMapping;
    bool m_AnimInitialized = false;
    bool m_AnimatorParamsResolved = false;
    std::string m_SpeedParamName;
    std::string m_MovingParamName;
    std::string m_GroundedParamName;
    std::string m_SprintParamName;
    std::string m_MoveXParamName;
    std::string m_MoveYParamName;
    std::string m_TurnParamName;
    std::string m_VerticalSpeedParamName;
    std::string m_JumpParamName;
    AnimAction m_AnimState = AnimAction::None;
    int m_ActionClipIndex = -1;
    float m_ActionDuration = 0.0f;
    float m_ActionElapsed = 0.0f;
    float m_ActionPlaybackSpeed = 1.0f;
    bool m_PreviousGrounded = true;

    float m_Yaw = 0.0f;
    float m_Pitch = -8.0f * Math::DEG_TO_RAD;
    bool m_CameraInitialized = false;
    Math::Vector3 m_CameraPosition = Math::Vector3::Zero;
    Math::Quaternion m_CameraRotation = Math::Quaternion::Identity;
    bool m_IsMovingDesired = false;
    bool m_WantsSprintDesired = false;
    bool m_CombatMode = false;
    bool m_IsStrafeMode = false;
    bool m_IsPivotTurnDesired = false;
    float m_PivotTurnDirection = 0.0f;
    float m_PivotTurnMagnitude = 0.0f;
    bool m_ManualAirborneClipOverride = false;
    bool m_JumpPressedThisFrame = false;
    bool m_JumpPhysicsPending = false;
    float m_JumpPhysicsDelayTimer = 0.0f;
    float m_JumpAnimationLockTimer = 0.0f;
    float m_JumpVisualOverrideTimer = 0.0f;
    float m_ForwardDriveAmount = 0.0f;
    bool m_LeftMouseWasDown = false;
    bool m_BodyOrientationInitialized = false;
    float m_BodyYaw = 0.0f;
    Math::Quaternion m_BodyRotationOffset = Math::Quaternion::Identity;
    bool m_AttachNamedWeaponProp = true;
    std::string m_WeaponPropName = "low poly";
    std::string m_WeaponBoneName = "mixamorig:RightHand";
    Entity* m_WeaponPropEntity = nullptr;
    bool m_WeaponPropBindingInitialized = false;
    Math::Vector3 m_WeaponGripPositionOffset = Math::Vector3(0.06f, -0.14f, -0.10f);
    Math::Vector3 m_WeaponGripRotationOffsetDegrees = Math::Vector3(-95.0f, 5.0f, -100.0f);
    Math::Vector3 m_WeaponSupportHandOffset = Math::Vector3(0.20f, -0.02f, 0.02f);
    Math::Vector3 m_WeaponPropScale = Math::Vector3::One;
    std::vector<int> m_MeleeAttackClips;
    int m_MeleeAttackCursor = 0;
    bool m_QueuedMeleeAttack = false;
    bool m_MeleeWindowUsesEvents = false;
    bool m_MeleeHitWindowActive = false;
    bool m_MeleeHitSweepInitialized = false;
    Math::Vector3 m_MeleeHitPreviousCenter = Math::Vector3::Zero;
    std::unordered_set<UUID> m_MeleeHitVictims;
    int m_EventClipIndex = -1;
    float m_EventClipPrevTime = 0.0f;
    float m_EventClipTime = 0.0f;
    bool m_EventClipLooping = false;
    float m_EventClipPlaybackSpeed = 1.0f;
    int m_AudioVariationCounter = 0;
    bool m_MeleeAttackAudioResolved = false;
    std::vector<std::string> m_MeleeAttackAudioAll;
    std::vector<std::string> m_MeleeAttackAudioLight;
    std::vector<std::string> m_MeleeAttackAudioMedium;
    std::vector<std::string> m_MeleeAttackAudioHeavy;
    std::vector<std::string> m_MeleeAttackVocalAll;
    std::vector<std::string> m_MeleeAttackVocalLight;
    std::vector<std::string> m_MeleeAttackVocalMedium;
    std::vector<std::string> m_MeleeAttackVocalHeavy;
    int m_MeleeAttackAudioCursor = 0;
    int m_MeleeAttackAudioLightCursor = 0;
    int m_MeleeAttackAudioMediumCursor = 0;
    int m_MeleeAttackAudioHeavyCursor = 0;
    int m_MeleeAttackVocalCursor = 0;
    int m_MeleeAttackVocalLightCursor = 0;
    int m_MeleeAttackVocalMediumCursor = 0;
    int m_MeleeAttackVocalHeavyCursor = 0;
    bool m_PendingMeleeAttackAudio = false;
    int m_PendingMeleeAttackAudioClipIndex = -1;
    float m_PendingMeleeAttackAudioTriggerTime = 0.0f;
    bool m_JumpAudioResolved = false;
    std::vector<std::string> m_JumpAudioPaths;
    std::vector<std::string> m_JumpVocalPaths;
    int m_JumpAudioCursor = 0;
    int m_JumpVocalCursor = 0;
    bool m_FootstepAudioResolved = false;
    std::vector<std::string> m_FootstepAudioPaths;
    int m_FootstepAudioCursor = 0;
    bool m_FootstepWasActive = false;
    float m_FootstepTimer = 0.68f;
    Math::Vector2 m_LastMoveInput = Math::Vector2::Zero;
    float m_DebugLogTimer = 0.0f;
    uint32_t m_LastDebugInputMask = 0;
    int m_LastDebugClipIndex = -999;
    AnimAction m_LastDebugAnimState = AnimAction::None;
};

} // namespace Crescent
