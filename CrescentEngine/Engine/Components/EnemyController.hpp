#pragma once

#include "../Assets/AssetDatabase.hpp"
#include "../Audio/AudioSystem.hpp"
#include "../Components/Animator.hpp"
#include "../Components/CharacterController.hpp"
#include "../Components/Health.hpp"
#include "../Components/SkinnedMeshRenderer.hpp"
#include "../Components/ThirdPersonController.hpp"
#include "../Core/UUID.hpp"
#include "../ECS/Component.hpp"
#include "../ECS/Entity.hpp"
#include "../ECS/Transform.hpp"
#include "../Math/Math.hpp"
#include "../Physics/PhysicsWorld.hpp"
#include "../Scene/Scene.hpp"
#include "../Scene/SceneManager.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

namespace Crescent {

class EnemyController : public Component {
public:
    COMPONENT_TYPE(EnemyController)

    EnemyController() = default;

    float getDetectionRange() const { return m_DetectionRange; }
    void setDetectionRange(float value) { m_DetectionRange = std::max(0.5f, value); }

    float getLoseRange() const { return m_LoseRange; }
    void setLoseRange(float value) { m_LoseRange = std::max(m_DetectionRange, value); }

    float getAttackRange() const { return m_AttackRange; }
    void setAttackRange(float value) { m_AttackRange = std::max(0.1f, value); }

    float getChaseSpeed() const { return m_ChaseSpeed; }
    void setChaseSpeed(float value) { m_ChaseSpeed = std::max(0.0f, value); }

    float getRotationSmoothSpeed() const { return m_RotationSmoothSpeed; }
    void setRotationSmoothSpeed(float value) { m_RotationSmoothSpeed = std::max(0.0f, value); }

    float getAttackCooldown() const { return m_AttackCooldown; }
    void setAttackCooldown(float value) { m_AttackCooldown = std::max(0.0f, value); }

    float getAttackDamage() const { return m_AttackDamage; }
    void setAttackDamage(float value) { m_AttackDamage = std::max(0.0f, value); }

    float getAttackHitRadius() const { return m_AttackHitRadius; }
    void setAttackHitRadius(float value) { m_AttackHitRadius = std::max(0.05f, value); }

    float getAttackForwardOffset() const { return m_AttackForwardOffset; }
    void setAttackForwardOffset(float value) { m_AttackForwardOffset = value; }

    float getAttackUpOffset() const { return m_AttackUpOffset; }
    void setAttackUpOffset(float value) { m_AttackUpOffset = value; }

    int getAttackMask() const { return static_cast<int>(m_AttackMask); }
    void setAttackMask(int value) { m_AttackMask = static_cast<uint32_t>(std::max(0, value)); }

    bool getAttackHitTriggers() const { return m_AttackHitTriggers; }
    void setAttackHitTriggers(bool value) { m_AttackHitTriggers = value; }

    float getDeathDespawnDelay() const { return m_DeathDespawnDelay; }
    void setDeathDespawnDelay(float value) { m_DeathDespawnDelay = std::max(0.0f, value); }

    bool getDebugLogging() const { return m_DebugLogging; }
    void setDebugLogging(bool value) { m_DebugLogging = value; }

    void OnCreate() override {
        findDependencies();
        resolveClipMapping();
        m_PreviousHealth = m_Health ? m_Health->getCurrentHealth() : 0.0f;
        if (m_Health) {
            m_OriginalDestroyOnDeath = m_Health->getDestroyOnDeath();
            m_Health->setDestroyOnDeath(false);
        }
        if (m_Controller) {
            m_OriginalControllerMoveSpeed = m_Controller->getMoveSpeed();
            m_Controller->setUseInput(false);
        }
        if (m_Animator) {
            m_AnimatorWasEnabled = m_Animator->isEnabled();
            if (m_AnimatorWasEnabled) {
                m_Animator->setEnabled(false);
                m_DisabledAnimatorForControl = true;
            }
        }
        playStateClip(State::Idle, true, true);
    }

    void OnDestroy() override {
        if (m_Controller) {
            m_Controller->clearWorldMoveDirection();
            m_Controller->setMoveSpeed(m_OriginalControllerMoveSpeed);
        }
        if (m_Health) {
            m_Health->setDestroyOnDeath(m_OriginalDestroyOnDeath);
        }
        if (m_Animator && m_DisabledAnimatorForControl) {
            m_Animator->setEnabled(m_AnimatorWasEnabled);
        }
    }

    void OnUpdate(float deltaTime) override {
        if (!m_Entity || deltaTime <= 0.0f) {
            return;
        }
        if (SceneManager::getInstance().getViewMode() != SceneManager::ViewMode::Game) {
            return;
        }

        ensureDependencies();
        if (!m_BodyTransform || !m_Controller) {
            return;
        }

        if (m_Controller) {
            m_Controller->setUseInput(false);
            m_Controller->setMoveSpeed(m_ChaseSpeed);
        }

        if (m_AttackCooldownTimer > 0.0f) {
            m_AttackCooldownTimer = std::max(0.0f, m_AttackCooldownTimer - deltaTime);
        }

        updatePlayerTarget(deltaTime);
        updateHealthState();

        switch (m_State) {
            case State::Dead:
                updateDead(deltaTime);
                break;
            case State::Hit:
                updateHit(deltaTime);
                break;
            case State::Attack:
                updateAttack(deltaTime);
                break;
            case State::Chase:
            case State::Idle:
            default:
                updateLocomotion(deltaTime);
                break;
        }

        processClipEvents(deltaTime);
        updateAttackHitTrace(deltaTime);
        emitDebugLog();
    }

private:
    enum class State {
        Idle,
        Chase,
        Attack,
        Hit,
        Dead
    };

    struct ClipMapping {
        int idle = -1;
        int walk = -1;
        int run = -1;
        int hit = -1;
        int death = -1;
        std::vector<int> attacks;
        std::vector<int> hits;
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

    static void DecomposeTRS(const Math::Matrix4x4& matrix,
                             Math::Vector3& outPosition,
                             Math::Quaternion& outRotation,
                             Math::Vector3& outScale) {
        outPosition = Math::Vector3(matrix.m[12], matrix.m[13], matrix.m[14]);

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
            outRotation.w = 0.25f * s;
            outRotation.x = (m21 - m12) / s;
            outRotation.y = (m02 - m20) / s;
            outRotation.z = (m10 - m01) / s;
        } else if (m00 > m11 && m00 > m22) {
            float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
            outRotation.w = (m21 - m12) / s;
            outRotation.x = 0.25f * s;
            outRotation.y = (m01 + m10) / s;
            outRotation.z = (m02 + m20) / s;
        } else if (m11 > m22) {
            float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
            outRotation.w = (m02 - m20) / s;
            outRotation.x = (m01 + m10) / s;
            outRotation.y = 0.25f * s;
            outRotation.z = (m12 + m21) / s;
        } else {
            float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
            outRotation.w = (m10 - m01) / s;
            outRotation.x = (m02 + m20) / s;
            outRotation.y = (m12 + m21) / s;
            outRotation.z = 0.25f * s;
        }
        outRotation.normalize();
    }

    static void AddUniqueClipIndex(std::vector<int>& clips, int clipIndex) {
        if (clipIndex < 0) {
            return;
        }
        if (std::find(clips.begin(), clips.end(), clipIndex) == clips.end()) {
            clips.push_back(clipIndex);
        }
    }

    static float DampFactor(float speed, float deltaTime) {
        return 1.0f - std::exp(-std::max(0.0f, speed) * deltaTime);
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

    void findDependencies() {
        m_BodyTransform = m_Entity ? m_Entity->getTransform() : nullptr;
        m_Controller = m_Entity ? m_Entity->getComponent<CharacterController>() : nullptr;
        m_Health = m_Entity ? m_Entity->getComponent<Health>() : nullptr;
        m_Animator = m_BodyTransform ? findAnimatorInHierarchy(m_BodyTransform) : nullptr;
        m_SkinnedTargets.clear();
        collectSkinnedMeshes(m_BodyTransform, m_SkinnedTargets);
        m_PrimarySkinned = m_SkinnedTargets.empty() ? nullptr : m_SkinnedTargets.front();
    }

    void ensureDependencies() {
        if (!m_BodyTransform || !m_Controller || !m_Health || !m_PrimarySkinned) {
            findDependencies();
            resolveClipMapping();
        }
    }

    std::shared_ptr<AnimationClip> getClipShared(int clipIndex) const {
        if (!m_PrimarySkinned) {
            return nullptr;
        }
        const auto& clips = m_PrimarySkinned->getAnimationClips();
        if (clipIndex < 0 || clipIndex >= static_cast<int>(clips.size())) {
            return nullptr;
        }
        return clips[static_cast<size_t>(clipIndex)];
    }

    float resolveClipDuration(int clipIndex) const {
        std::shared_ptr<AnimationClip> clip = getClipShared(clipIndex);
        return clip ? std::max(0.0f, clip->getDurationSeconds()) : 0.0f;
    }

    std::string getClipName(int clipIndex) const {
        std::shared_ptr<AnimationClip> clip = getClipShared(clipIndex);
        return clip ? clip->getName() : std::string();
    }

    int findPrimaryLocomotionClipIndex(const std::vector<std::string>& includeTokens,
                                       const std::vector<std::string>& excludeTokens) const {
        if (!m_PrimarySkinned) {
            return -1;
        }
        const auto& clips = m_PrimarySkinned->getAnimationClips();
        for (size_t i = 0; i < clips.size(); ++i) {
            const auto& clip = clips[i];
            if (!clip) {
                continue;
            }
            std::string name = ToLower(clip->getName());
            if (!ContainsAllTokens(name, includeTokens)) {
                continue;
            }
            if (ContainsAnyToken(name, excludeTokens)) {
                continue;
            }
            return static_cast<int>(i);
        }
        return -1;
    }

    void resolveClipMapping() {
        m_ClipMapping = {};
        if (!m_PrimarySkinned) {
            return;
        }

        const std::vector<std::string> locomotionExclude = {"attack", "combo", "kick", "react", "block", "taunt", "jump", "turn", "disarm", "equip", "looking"};

        m_ClipMapping.idle = findPrimaryLocomotionClipIndex({"standing", "idle"}, {"attack", "combo", "kick", "react", "block", "taunt", "jump", "turn"});
        if (m_ClipMapping.idle < 0) {
            m_ClipMapping.idle = findPrimaryLocomotionClipIndex({"idle"}, {"crouch", "attack", "combo", "kick", "react", "block", "taunt", "jump", "turn"});
        }

        m_ClipMapping.walk = findPrimaryLocomotionClipIndex({"standing", "walk", "forward"}, locomotionExclude);
        if (m_ClipMapping.walk < 0) {
            m_ClipMapping.walk = findPrimaryLocomotionClipIndex({"standing", "walk"}, {"left", "right", "back", "attack", "combo", "kick", "react", "block", "taunt", "jump", "turn"});
        }
        if (m_ClipMapping.walk < 0) {
            m_ClipMapping.walk = findPrimaryLocomotionClipIndex({"walk"}, {"left", "right", "back", "attack", "combo", "kick", "react", "block", "taunt", "jump", "turn"});
        }

        m_ClipMapping.run = findPrimaryLocomotionClipIndex({"standing", "run", "forward"}, locomotionExclude);
        if (m_ClipMapping.run < 0) {
            m_ClipMapping.run = findPrimaryLocomotionClipIndex({"standing", "run"}, {"left", "right", "back", "attack", "combo", "kick", "react", "block", "taunt", "jump", "turn"});
        }
        if (m_ClipMapping.run < 0) {
            m_ClipMapping.run = findPrimaryLocomotionClipIndex({"run"}, {"left", "right", "back", "attack", "combo", "kick", "react", "block", "taunt", "jump", "turn"});
        }

        AddUniqueClipIndex(m_ClipMapping.attacks, findPrimaryLocomotionClipIndex({"standing", "melee", "combo", "attack", "ver", "1"}, {"react", "block", "taunt"}));
        AddUniqueClipIndex(m_ClipMapping.attacks, findPrimaryLocomotionClipIndex({"standing", "melee", "combo", "attack", "ver", "2"}, {"react", "block", "taunt"}));
        AddUniqueClipIndex(m_ClipMapping.attacks, findPrimaryLocomotionClipIndex({"standing", "melee", "combo", "attack", "ver", "3"}, {"react", "block", "taunt"}));
        AddUniqueClipIndex(m_ClipMapping.attacks, findPrimaryLocomotionClipIndex({"standing", "melee", "attack", "horizontal"}, {"react", "block", "taunt"}));
        AddUniqueClipIndex(m_ClipMapping.attacks, findPrimaryLocomotionClipIndex({"standing", "melee", "attack", "downward"}, {"react", "block", "taunt"}));
        AddUniqueClipIndex(m_ClipMapping.attacks, findPrimaryLocomotionClipIndex({"standing", "melee", "attack", "backhand"}, {"react", "block", "taunt"}));
        AddUniqueClipIndex(m_ClipMapping.attacks, findPrimaryLocomotionClipIndex({"standing", "melee", "attack", "360", "high"}, {"react", "block", "taunt"}));
        AddUniqueClipIndex(m_ClipMapping.attacks, findPrimaryLocomotionClipIndex({"standing", "melee", "attack", "360", "low"}, {"react", "block", "taunt"}));
        AddUniqueClipIndex(m_ClipMapping.attacks, findPrimaryLocomotionClipIndex({"standing", "melee", "run", "jump", "attack"}, {"react", "block", "taunt"}));
        AddUniqueClipIndex(m_ClipMapping.attacks, findPrimaryLocomotionClipIndex({"standing", "melee", "attack"}, {"react", "block", "taunt"}));

        AddUniqueClipIndex(m_ClipMapping.hits, findPrimaryLocomotionClipIndex({"standing", "block", "react", "large"}, {"attack", "combo", "taunt"}));
        AddUniqueClipIndex(m_ClipMapping.hits, findPrimaryLocomotionClipIndex({"standing", "react", "large", "left"}, {"attack", "combo", "taunt"}));
        AddUniqueClipIndex(m_ClipMapping.hits, findPrimaryLocomotionClipIndex({"standing", "react", "large", "right"}, {"attack", "combo", "taunt"}));
        AddUniqueClipIndex(m_ClipMapping.hits, findPrimaryLocomotionClipIndex({"standing", "react", "large", "gut"}, {"attack", "combo", "taunt"}));
        AddUniqueClipIndex(m_ClipMapping.hits, findPrimaryLocomotionClipIndex({"react"}, {"attack", "combo", "taunt"}));

        if (!m_ClipMapping.hits.empty()) {
            m_ClipMapping.hit = m_ClipMapping.hits.front();
        }

        m_ClipMapping.death = findPrimaryLocomotionClipIndex({"death"}, {"attack", "combo"});
        if (m_ClipMapping.death < 0) {
            m_ClipMapping.death = findPrimaryLocomotionClipIndex({"die"}, {"attack", "combo"});
        }
        if (m_ClipMapping.death < 0) {
            m_ClipMapping.death = findPrimaryLocomotionClipIndex({"dead"}, {"attack", "combo"});
        }
        if (m_ClipMapping.death < 0) {
            m_ClipMapping.death = findPrimaryLocomotionClipIndex({"knockdown"}, {"attack", "combo"});
        }
    }

    Entity* findPlayerEntity() const;

    void updatePlayerTarget(float deltaTime) {
        m_PlayerRefreshTimer -= deltaTime;
        bool refresh = m_PlayerRefreshTimer <= 0.0f ||
                       !m_Player ||
                       !m_Player->isActiveInHierarchy() ||
                       !m_Player->getScene() ||
                       m_Player->getScene() != (m_Entity ? m_Entity->getScene() : nullptr);
        if (refresh) {
            m_PlayerRefreshTimer = 0.35f;
            m_Player = findPlayerEntity();
            m_PlayerTransform = m_Player ? m_Player->getTransform() : nullptr;
            m_PlayerHealth = m_Player ? m_Player->getComponent<Health>() : nullptr;
        } else if (!m_PlayerTransform && m_Player) {
            m_PlayerTransform = m_Player->getTransform();
            m_PlayerHealth = m_Player->getComponent<Health>();
        }
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

    float nextAudioJitter(float amplitude) {
        static constexpr float kPattern[7] = {-1.0f, -0.45f, 0.35f, 0.9f, -0.2f, 0.6f, 0.0f};
        float jitter = kPattern[static_cast<size_t>(m_AudioVariationCounter % 7)] * amplitude;
        ++m_AudioVariationCounter;
        return jitter;
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
        return AudioBus::SFX;
    }

    bool playConfiguredEventAudio(const AnimationEvent& event) {
        std::string path = resolveEventAudioPath(event.payload);
        if (path.empty()) {
            return false;
        }
        float volume = std::max(0.0f, event.volume);
        float pitch = resolveEventPitch(event);
        AudioBus bus = resolveEventBus(event);
        std::string tag = getEventTagLower(event);
        if (!event.spatial) {
            return AudioSystem::getInstance().playOneShot(path, bus, volume, pitch);
        }
        if (ContainsAnyToken(tag, {"swing", "whoosh", "attack"})) {
            Math::Vector3 forward = m_BodyTransform ? FlattenDirection(m_BodyTransform->forward()) : Math::Vector3(0.0f, 0.0f, -1.0f);
            return AudioSystem::getInstance().playOneShot3DDirectional(
                path,
                getAudioOrigin(1.1f),
                forward,
                bus,
                volume,
                pitch,
                1.0f,
                14.0f,
                1.0f,
                1.4f,
                2.4f,
                0.35f,
                0.7f);
        }
        return AudioSystem::getInstance().playOneShot3D(
            path,
            getAudioOrigin(ContainsAnyToken(tag, {"jump"}) ? 1.0f : 1.1f),
            bus,
            volume,
            pitch,
            1.0f,
            14.0f,
            0.9f);
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
            beginAttackHitWindow();
        } else if (MatchesAnyTag(tag, {"attack_window_close", "melee_window_close", "hitbox_off", "damage_window_close"})) {
            endAttackHitWindow();
        }
        if (type == "audio" || !event.payload.empty()) {
            playConfiguredEventAudio(event);
        }
        (void)clipIndex;
    }

    void processClipEvents(float deltaTime) {
        if (m_EventClipIndex < 0) {
            return;
        }
        std::shared_ptr<AnimationClip> clip = getClipShared(m_EventClipIndex);
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

        for (const auto& evt : clip->getEvents()) {
            bool crossed = false;
            if (!m_EventClipLooping || currentTime >= previousTime) {
                crossed = evt.time > previousTime && evt.time <= currentTime;
            } else {
                crossed = evt.time > previousTime || evt.time <= currentTime;
            }
            if (crossed) {
                dispatchClipEvent(evt, m_EventClipIndex);
            }
        }

        m_EventClipPrevTime = previousTime;
        m_EventClipTime = currentTime;
    }

    bool clipHasEventToken(int clipIndex, const std::vector<std::string>& tokens) const {
        std::shared_ptr<AnimationClip> clip = getClipShared(clipIndex);
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

    void playClip(int clipIndex, bool looping, float playbackSpeed, bool restart) {
        if (clipIndex < 0 || m_SkinnedTargets.empty()) {
            return;
        }
        bool sameClip = clipIndex == m_CurrentClipIndex;
        for (SkinnedMeshRenderer* skinned : m_SkinnedTargets) {
            if (!skinned) {
                continue;
            }
            skinned->setDrivenByAnimator(false);
            skinned->setLooping(looping);
            skinned->setPlaybackSpeed(playbackSpeed);
            if (!sameClip || restart) {
                skinned->setActiveClipIndex(clipIndex);
                skinned->setTimeSeconds(0.0f);
            }
            skinned->setPlaying(true);
        }
        if (!sameClip || restart) {
            m_CurrentClipIndex = clipIndex;
            m_CurrentClipElapsed = 0.0f;
            m_CurrentClipDuration = resolveClipDuration(clipIndex);
        }
        m_CurrentClipLooping = looping;
        m_CurrentClipPlaybackSpeed = std::max(0.01f, playbackSpeed);
        configureClipEventPlayback(clipIndex, looping, m_CurrentClipPlaybackSpeed, restart || !sameClip);
    }

    void setState(State state) {
        if (m_State == state) {
            return;
        }
        m_State = state;
        m_StateElapsed = 0.0f;
    }

    void playStateClip(State desiredState, bool restart, bool force) {
        if (!force && m_State == desiredState) {
            return;
        }

        int clipIndex = -1;
        bool looping = true;
        float speed = 1.0f;

        switch (desiredState) {
            case State::Idle:
                clipIndex = m_ClipMapping.idle;
                looping = true;
                break;
            case State::Chase:
                clipIndex = m_ClipMapping.run >= 0 ? m_ClipMapping.run : m_ClipMapping.walk;
                looping = true;
                speed = m_ClipMapping.run >= 0 ? 1.1f : 1.0f;
                break;
            case State::Hit:
                if (!m_ClipMapping.hits.empty()) {
                    clipIndex = m_ClipMapping.hits[static_cast<size_t>(m_HitClipCursor % static_cast<int>(m_ClipMapping.hits.size()))];
                    m_HitClipCursor = (m_HitClipCursor + 1) % static_cast<int>(m_ClipMapping.hits.size());
                } else {
                    clipIndex = m_ClipMapping.hit;
                }
                looping = false;
                speed = 1.1f;
                break;
            case State::Attack:
                if (!m_ClipMapping.attacks.empty()) {
                    clipIndex = m_ClipMapping.attacks[static_cast<size_t>(m_AttackClipCursor % static_cast<int>(m_ClipMapping.attacks.size()))];
                    m_AttackClipCursor = (m_AttackClipCursor + 1) % static_cast<int>(m_ClipMapping.attacks.size());
                }
                looping = false;
                speed = 1.15f;
                break;
            case State::Dead:
                clipIndex = m_ClipMapping.death;
                looping = false;
                speed = 1.0f;
                break;
        }

        if (clipIndex >= 0) {
            playClip(clipIndex, looping, speed, restart || force);
        }
        setState(desiredState);

        if (desiredState == State::Attack) {
            m_AttackWindowUsesEvents = clipIndex >= 0 &&
                                       clipHasEventToken(clipIndex, {
                                           "attack_window_open", "melee_window_open", "hitbox_on", "damage_window_open",
                                           "attack_window_close", "melee_window_close", "hitbox_off", "damage_window_close"
                                       });
            endAttackHitWindow();
            m_AttackCooldownTimer = m_AttackCooldown;
        } else if (desiredState != State::Attack) {
            endAttackHitWindow();
        }
    }

    bool isCurrentClipFinished() const {
        if (m_CurrentClipLooping || m_CurrentClipDuration <= 0.0f) {
            return false;
        }
        return m_CurrentClipElapsed >= (m_CurrentClipDuration - 0.02f);
    }

    void updateHealthState() {
        if (!m_Health) {
            return;
        }
        float current = m_Health->getCurrentHealth();
        if (m_Health->isDead() || current <= 0.0f) {
            if (m_State != State::Dead) {
                playStateClip(State::Dead, true, true);
                m_DeathTimer = 0.0f;
                if (m_Controller) {
                    m_Controller->clearWorldMoveDirection();
                }
            }
            m_PreviousHealth = current;
            return;
        }

        if (current + 0.001f < m_PreviousHealth && m_State != State::Dead) {
            playStateClip(State::Hit, true, true);
            if (m_Controller) {
                m_Controller->clearWorldMoveDirection();
            }
        }

        m_PreviousHealth = current;
    }

    float horizontalDistanceToPlayer(Math::Vector3* outToPlayer = nullptr) const {
        if (!m_BodyTransform || !m_PlayerTransform) {
            if (outToPlayer) {
                *outToPlayer = Math::Vector3::Zero;
            }
            return std::numeric_limits<float>::infinity();
        }
        Math::Vector3 toPlayer = m_PlayerTransform->getPosition() - m_BodyTransform->getPosition();
        toPlayer.y = 0.0f;
        if (outToPlayer) {
            *outToPlayer = toPlayer;
        }
        return toPlayer.length();
    }

    void rotateToward(const Math::Vector3& direction, float deltaTime) {
        if (!m_BodyTransform || direction.lengthSquared() <= Math::EPSILON) {
            return;
        }
        Math::Vector3 desiredForward = FlattenDirection(direction, FlattenDirection(m_BodyTransform->forward()));
        Math::Quaternion desiredRotation = Math::Quaternion::LookRotation(desiredForward, Math::Vector3::Up);
        float t = DampFactor(m_RotationSmoothSpeed, deltaTime);
        m_BodyTransform->setRotation(Math::Quaternion::Slerp(m_BodyTransform->getRotation(), desiredRotation, t));
    }

    void updateLocomotion(float deltaTime) {
        Math::Vector3 toPlayer = Math::Vector3::Zero;
        float distance = horizontalDistanceToPlayer(&toPlayer);
        bool hasTarget = m_Player && m_PlayerTransform && distance <= m_LoseRange;
        if (!hasTarget) {
            if (m_Controller) {
                m_Controller->clearWorldMoveDirection();
            }
            playStateClip(State::Idle, false, m_State != State::Idle);
            m_CurrentClipElapsed += deltaTime * m_CurrentClipPlaybackSpeed;
            m_StateElapsed += deltaTime;
            return;
        }

        rotateToward(toPlayer, deltaTime);

        if (distance <= m_AttackRange && m_AttackCooldownTimer <= 0.0f && !m_ClipMapping.attacks.empty()) {
            if (m_Controller) {
                m_Controller->clearWorldMoveDirection();
            }
            playStateClip(State::Attack, true, true);
            m_CurrentClipElapsed += deltaTime * m_CurrentClipPlaybackSpeed;
            m_StateElapsed += deltaTime;
            return;
        }

        if (distance <= m_DetectionRange || m_State == State::Chase) {
            if (m_Controller) {
                m_Controller->setWorldMoveDirection(FlattenDirection(toPlayer, m_BodyTransform->forward()));
            }
            playStateClip(State::Chase, false, m_State != State::Chase);
        } else {
            if (m_Controller) {
                m_Controller->clearWorldMoveDirection();
            }
            playStateClip(State::Idle, false, m_State != State::Idle);
        }

        m_CurrentClipElapsed += deltaTime * m_CurrentClipPlaybackSpeed;
        m_StateElapsed += deltaTime;
    }

    void updateHit(float deltaTime) {
        Math::Vector3 toPlayer = Math::Vector3::Zero;
        horizontalDistanceToPlayer(&toPlayer);
        rotateToward(toPlayer, deltaTime);
        if (m_Controller) {
            m_Controller->clearWorldMoveDirection();
        }
        m_CurrentClipElapsed += deltaTime * m_CurrentClipPlaybackSpeed;
        m_StateElapsed += deltaTime;
        float minimumReact = 0.22f;
        if (isCurrentClipFinished() || m_StateElapsed >= std::max(minimumReact, m_CurrentClipDuration > 0.0f ? m_CurrentClipDuration : minimumReact)) {
            playStateClip(State::Idle, true, true);
        }
    }

    void updateAttack(float deltaTime) {
        Math::Vector3 toPlayer = Math::Vector3::Zero;
        horizontalDistanceToPlayer(&toPlayer);
        rotateToward(toPlayer, deltaTime);
        if (m_Controller) {
            m_Controller->clearWorldMoveDirection();
        }
        m_CurrentClipElapsed += deltaTime * m_CurrentClipPlaybackSpeed;
        m_StateElapsed += deltaTime;
        if (isCurrentClipFinished() || (m_CurrentClipDuration <= 0.0f && m_StateElapsed >= 0.7f)) {
            playStateClip(State::Idle, true, true);
        }
    }

    void updateDead(float deltaTime) {
        if (m_Controller) {
            m_Controller->clearWorldMoveDirection();
        }
        m_CurrentClipElapsed += deltaTime * m_CurrentClipPlaybackSpeed;
        m_StateElapsed += deltaTime;
        m_DeathTimer += deltaTime;
        if (m_DeathDespawnDelay <= 0.0f || m_DeathTimer >= m_DeathDespawnDelay) {
            if (Scene* scene = m_Entity ? m_Entity->getScene() : nullptr) {
                scene->destroyEntity(m_Entity);
            }
        }
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

    int resolveAttackBoneIndex(const Skeleton& skeleton) const {
        std::string boneName = resolveSkeletonBoneName(
            skeleton,
            {"mixamorig:RightHand", "RightHand", "Hand_R", "righthand", "weapon_r", "mixamorig:RightForeArm", "RightForeArm"});
        if (boneName.empty()) {
            return -1;
        }
        return skeleton.getBoneIndex(boneName);
    }

    bool computeAttackBoneWorld(Math::Matrix4x4& outBoneWorld) const {
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
        int boneIndex = resolveAttackBoneIndex(*skeleton);
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

    bool computeAttackHitCenter(Math::Vector3& outCenter) const {
        if (!m_BodyTransform) {
            return false;
        }
        Math::Vector3 bodyPos = m_BodyTransform->getPosition();
        Math::Vector3 bodyForward = FlattenDirection(m_BodyTransform->forward());

        Math::Vector3 handPos = bodyPos + Math::Vector3(0.0f, 1.0f, 0.0f);
        Math::Matrix4x4 handWorld = Math::Matrix4x4::Identity;
        if (computeAttackBoneWorld(handWorld)) {
            Math::Vector3 bonePos;
            Math::Quaternion boneRot;
            Math::Vector3 boneScale;
            DecomposeTRS(handWorld, bonePos, boneRot, boneScale);
            handPos = bonePos;
            bodyForward = FlattenDirection(boneRot * Math::Vector3::Forward, bodyForward);
        }

        outCenter = handPos +
                    (bodyForward * m_AttackForwardOffset) +
                    (Math::Vector3::Up * m_AttackUpOffset);
        return true;
    }

    void beginAttackHitWindow() {
        m_AttackHitWindowActive = true;
        m_AttackHitSweepInitialized = false;
        m_AttackVictims.clear();
    }

    void endAttackHitWindow() {
        m_AttackHitWindowActive = false;
        m_AttackHitSweepInitialized = false;
        m_AttackVictims.clear();
    }

    bool shouldUseFallbackAttackWindow() const {
        return m_State == State::Attack && !m_AttackWindowUsesEvents;
    }

    void applyAttackHitAtPoint(const Math::Vector3& point) {
        if (!m_Entity || !m_Entity->getScene()) {
            return;
        }

        std::vector<PhysicsOverlapHit> hits;
        if (m_Entity->getScene()->getPhysicsWorld()->overlapSphere(
                point,
                m_AttackHitRadius,
                hits,
                m_AttackMask,
                m_AttackHitTriggers,
                resolveDamageTarget(m_Entity)) <= 0) {
            return;
        }

        Entity* selfEntity = resolveDamageTarget(m_Entity);
        for (const auto& hit : hits) {
            Entity* target = resolveDamageTarget(hit.entity);
            if (!target || target == selfEntity) {
                continue;
            }

            UUID targetUUID = target->getUUID();
            if (m_AttackVictims.find(targetUUID) != m_AttackVictims.end()) {
                continue;
            }

            Health* health = target->getComponent<Health>();
            if (!health || health->isDead()) {
                continue;
            }

            m_AttackVictims.insert(targetUUID);
            health->applyDamage(m_AttackDamage);
        }
    }

    void updateAttackHitTrace(float deltaTime) {
        if (shouldUseFallbackAttackWindow()) {
            float normalized = m_CurrentClipDuration > 0.0f ? (m_CurrentClipElapsed / m_CurrentClipDuration) : 0.0f;
            bool shouldOpen = normalized >= 0.22f && normalized <= 0.58f;
            if (shouldOpen && !m_AttackHitWindowActive) {
                beginAttackHitWindow();
            } else if (!shouldOpen && m_AttackHitWindowActive) {
                endAttackHitWindow();
            }
        }

        if (!m_AttackHitWindowActive) {
            return;
        }

        Math::Vector3 center = Math::Vector3::Zero;
        if (!computeAttackHitCenter(center)) {
            return;
        }

        if (!m_AttackHitSweepInitialized) {
            m_AttackPreviousCenter = center;
            m_AttackHitSweepInitialized = true;
        }

        Math::Vector3 delta = center - m_AttackPreviousCenter;
        float distance = delta.length();
        int steps = std::max(1, static_cast<int>(std::ceil(distance / std::max(0.05f, m_AttackHitRadius * 0.5f))));
        for (int i = 1; i <= steps; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(steps);
            applyAttackHitAtPoint(Math::Vector3::Lerp(m_AttackPreviousCenter, center, t));
        }

        m_AttackPreviousCenter = center;
        (void)deltaTime;
    }

    void emitDebugLog() const {
        if (!m_DebugLogging || !m_BodyTransform) {
            return;
        }
        static int frameCounter = 0;
        if ((frameCounter++ % 15) != 0) {
            return;
        }

        const char* stateName = "Idle";
        switch (m_State) {
            case State::Idle: stateName = "Idle"; break;
            case State::Chase: stateName = "Chase"; break;
            case State::Attack: stateName = "Attack"; break;
            case State::Hit: stateName = "Hit"; break;
            case State::Dead: stateName = "Dead"; break;
        }

        float distance = horizontalDistanceToPlayer();
        std::cout << "[Enemy] entity=" << (m_Entity ? m_Entity->getName() : std::string("?"))
                  << " state=" << stateName
                  << " dist=" << distance
                  << " hp=" << (m_Health ? m_Health->getCurrentHealth() : 0.0f)
                  << " clip=" << getClipName(m_CurrentClipIndex)
                  << std::endl;
    }

private:
    float m_DetectionRange = 9.0f;
    float m_LoseRange = 13.0f;
    float m_AttackRange = 1.8f;
    float m_ChaseSpeed = 2.6f;
    float m_RotationSmoothSpeed = 10.0f;
    float m_AttackCooldown = 1.1f;
    float m_AttackDamage = 16.0f;
    float m_AttackHitRadius = 0.42f;
    float m_AttackForwardOffset = 0.42f;
    float m_AttackUpOffset = 0.02f;
    uint32_t m_AttackMask = PhysicsWorld::kAllLayersMask;
    bool m_AttackHitTriggers = false;
    float m_DeathDespawnDelay = 2.75f;
    bool m_DebugLogging = false;

    Transform* m_BodyTransform = nullptr;
    CharacterController* m_Controller = nullptr;
    Health* m_Health = nullptr;
    Animator* m_Animator = nullptr;
    SkinnedMeshRenderer* m_PrimarySkinned = nullptr;
    std::vector<SkinnedMeshRenderer*> m_SkinnedTargets;

    Entity* m_Player = nullptr;
    Transform* m_PlayerTransform = nullptr;
    Health* m_PlayerHealth = nullptr;
    float m_PlayerRefreshTimer = 0.0f;

    float m_OriginalControllerMoveSpeed = 0.0f;
    bool m_OriginalDestroyOnDeath = true;
    bool m_AnimatorWasEnabled = false;
    bool m_DisabledAnimatorForControl = false;

    ClipMapping m_ClipMapping;
    State m_State = State::Idle;

    int m_CurrentClipIndex = -1;
    bool m_CurrentClipLooping = true;
    float m_CurrentClipPlaybackSpeed = 1.0f;
    float m_CurrentClipElapsed = 0.0f;
    float m_CurrentClipDuration = 0.0f;
    float m_StateElapsed = 0.0f;
    float m_PreviousHealth = 0.0f;
    float m_AttackCooldownTimer = 0.0f;
    float m_DeathTimer = 0.0f;

    int m_AttackClipCursor = 0;
    int m_HitClipCursor = 0;

    int m_EventClipIndex = -1;
    float m_EventClipPrevTime = 0.0f;
    float m_EventClipTime = 0.0f;
    bool m_EventClipLooping = false;
    float m_EventClipPlaybackSpeed = 1.0f;

    bool m_AttackWindowUsesEvents = false;
    bool m_AttackHitWindowActive = false;
    bool m_AttackHitSweepInitialized = false;
    Math::Vector3 m_AttackPreviousCenter = Math::Vector3::Zero;
    std::unordered_set<UUID> m_AttackVictims;
    int m_AudioVariationCounter = 0;
};

inline Entity* EnemyController::findPlayerEntity() const {
    if (!m_Entity) {
        return nullptr;
    }
    Scene* scene = m_Entity->getScene();
    if (!scene) {
        return nullptr;
    }

    for (const auto& handle : scene->getAllEntities()) {
        Entity* entity = handle.get();
        if (!entity || entity == m_Entity || !entity->isActiveInHierarchy()) {
            continue;
        }
        if (entity->getComponent<ThirdPersonController>()) {
            return entity;
        }
    }
    return nullptr;
}

} // namespace Crescent
