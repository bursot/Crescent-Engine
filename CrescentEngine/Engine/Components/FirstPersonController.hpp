#pragma once

#include "../ECS/Component.hpp"
#include "../ECS/Entity.hpp"
#include "../ECS/Transform.hpp"
#include "../Input/InputManager.hpp"
#include "../Math/Math.hpp"
#include "../Scene/Scene.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Physics/PhysicsWorld.hpp"
#include "../Components/CharacterController.hpp"
#include "../Components/Camera.hpp"
#include "../Components/SkinnedMeshRenderer.hpp"
#include "../Components/Animator.hpp"
#include "../Components/AudioSource.hpp"
#include "../Components/Light.hpp"
#include "../Components/MeshRenderer.hpp"
#include "../Core/Engine.hpp"
#include "../Renderer/Renderer.hpp"
#include "../Rendering/Material.hpp"
#include "../Rendering/Mesh.hpp"
#include "../Rendering/Texture.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace Crescent {

class FirstPersonController : public Component {
public:
    COMPONENT_TYPE(FirstPersonController)

    FirstPersonController()
        : m_MouseSensitivity(0.002f)
        , m_InvertY(false)
        , m_RequireLookButton(true)
        , m_LookButton(MouseButton::Right)
        , m_MinPitch(-89.0f)
        , m_MaxPitch(89.0f)
        , m_WalkSpeed(5.0f)
        , m_SprintMultiplier(1.6f)
        , m_EnableSprint(true)
        , m_EnableCrouch(true)
        , m_CrouchHeight(1.2f)
        , m_CrouchEyeHeight(1.0f)
        , m_CrouchSpeed(6.0f)
        , m_EyeHeight(1.6f)
        , m_UseEyeHeight(true)
        , m_DriveCharacterController(true)
        , m_FireCooldown(0.12f)
        , m_MuzzleRotationOffset(Math::Quaternion::FromEulerAngles(
              Math::Vector3(90.0f * Math::DEG_TO_RAD, 0.0f, 0.0f))) {
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

    float getWalkSpeed() const { return m_WalkSpeed; }
    void setWalkSpeed(float value) { m_WalkSpeed = std::max(0.0f, value); }

    float getSprintMultiplier() const { return m_SprintMultiplier; }
    void setSprintMultiplier(float value) { m_SprintMultiplier = std::max(1.0f, value); }

    bool getEnableSprint() const { return m_EnableSprint; }
    void setEnableSprint(bool value) { m_EnableSprint = value; }

    bool getEnableCrouch() const { return m_EnableCrouch; }
    void setEnableCrouch(bool value) { m_EnableCrouch = value; }

    float getCrouchHeight() const { return m_CrouchHeight; }
    void setCrouchHeight(float value) { m_CrouchHeight = std::max(0.2f, value); }

    float getCrouchEyeHeight() const { return m_CrouchEyeHeight; }
    void setCrouchEyeHeight(float value) { m_CrouchEyeHeight = std::max(0.0f, value); }

    float getCrouchSpeed() const { return m_CrouchSpeed; }
    void setCrouchSpeed(float value) { m_CrouchSpeed = std::max(0.0f, value); }

    float getEyeHeight() const { return m_EyeHeight; }
    void setEyeHeight(float value) { m_EyeHeight = std::max(0.0f, value); }

    bool getUseEyeHeight() const { return m_UseEyeHeight; }
    void setUseEyeHeight(bool value) { m_UseEyeHeight = value; }

    bool getDriveCharacterController() const { return m_DriveCharacterController; }
    void setDriveCharacterController(bool value) { m_DriveCharacterController = value; }

    float getFireCooldown() const { return m_FireCooldown; }
    void setFireCooldown(float value) { m_FireCooldown = std::max(0.0f, value); }

    const std::string& getMuzzleTexturePath() const { return m_MuzzleTexturePath; }
    void setMuzzleTexturePath(const std::string& path) {
        if (m_MuzzleTexturePath == path) {
            return;
        }
        m_MuzzleTexturePath = path;
        loadMuzzleTexture();
    }

    void OnCreate() override {
        findDependencies();
        initializeLook();
        refreshAnimationTargets();
        if (m_Controller) {
            m_Controller->setUseInput(false);
            m_StandingHeight = m_Controller->getHeight();
            m_CurrentHeight = m_StandingHeight;
            m_HeightsInitialized = true;
        }
        m_CurrentEyeHeight = m_EyeHeight;
        if (m_CameraTransform && m_CameraTransform != m_BodyTransform && m_UseEyeHeight) {
            m_CameraTransform->setLocalPosition(Math::Vector3(0.0f, m_CurrentEyeHeight, 0.0f));
        }
        if (m_Controller && m_DriveCharacterController) {
            m_Controller->setEnabled(false);
        }
        m_FireCooldownTimer = 0.0f;
        loadMuzzleTexture();
    }

    void OnDestroy() override {
        if (m_Controller && m_DriveCharacterController) {
            m_Controller->setEnabled(true);
        }
        m_MuzzleLight = nullptr;
        m_MuzzleMesh = nullptr;
        m_MuzzleMaterial.reset();
        m_MuzzleAnchor = nullptr;
        m_MuzzleTransform = nullptr;
        m_MuzzleTexture.reset();
        m_MuzzleInitialized = false;
    }

    void OnUpdate(float deltaTime) override {
        if (!m_Entity || deltaTime <= 0.0f) {
            return;
        }
        if (SceneManager::getInstance().getViewMode() != SceneManager::ViewMode::Game) {
            return;
        }

        if (m_FireCooldownTimer > 0.0f) {
            m_FireCooldownTimer = std::max(0.0f, m_FireCooldownTimer - deltaTime);
        }

        if (!m_CameraTransform || !m_BodyTransform || !m_Controller) {
            findDependencies();
            if (!m_CameraTransform || !m_BodyTransform) {
                return;
            }
        }

        if (m_Controller) {
            if (m_DriveCharacterController && m_Controller->isEnabled()) {
                m_Controller->setEnabled(false);
            } else if (!m_DriveCharacterController && !m_Controller->isEnabled()) {
                m_Controller->setEnabled(true);
            }
        }

        InputManager& input = InputManager::getInstance();
        bool lookAllowed = !m_RequireLookButton || input.isMouseButtonPressed(m_LookButton);
        if (lookAllowed) {
            Math::Vector2 mouseDelta = input.getMouseDelta();
            float yawDelta = mouseDelta.x * m_MouseSensitivity;
            float pitchDelta = mouseDelta.y * m_MouseSensitivity * (m_InvertY ? -1.0f : 1.0f);

            m_Yaw -= yawDelta;
            m_Pitch -= pitchDelta;
            m_Pitch = Math::Clamp(m_Pitch, m_MinPitch * Math::DEG_TO_RAD, m_MaxPitch * Math::DEG_TO_RAD);
        }

        applyRotation();
        applyCameraHeight(deltaTime, input);
        driveCharacter(deltaTime, input);
        updateAnimation(deltaTime, input);
        updateMuzzleFlash(deltaTime);
    }

private:
    enum class AnimAction {
        None,
        Idle,
        Walk,
        Run,
        Fire,
        Reload
    };

    struct ClipMapping {
        int idle = -1;
        int walk = -1;
        int run = -1;
        int fire = -1;
        int reload = -1;
    };

    struct AnimatorMapping {
        int idleState = -1;
        int walkState = -1;
        int runState = -1;
        int fireState = -1;
        int reloadState = -1;
        int idleClip = -1;
        int walkClip = -1;
        int runClip = -1;
        int fireClip = -1;
        int reloadClip = -1;
    };

    static std::string ToLower(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    static bool ContainsAllTokens(const std::string& haystack, const std::vector<std::string>& tokens) {
        for (const auto& token : tokens) {
            if (token.empty()) {
                continue;
            }
            if (haystack.find(token) == std::string::npos) {
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
            std::string name = ToLower(clip->getName());
            if (ContainsAllTokens(name, tokens)) {
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

    void refreshAnimationTargets() {
        m_Animator = nullptr;
        m_AnimTargets.clear();
        m_AnimatorParamsResolved = false;
        m_AnimatorHasMoveParam = false;
        m_AnimatorHasFireParam = false;
        m_AnimatorHasReloadParam = false;
        m_ActionElapsed = 0.0f;
        m_ActionDuration = 0.0f;
        m_SpeedParamName.clear();
        m_MovingParamName.clear();
        m_FireParamName.clear();
        m_ReloadParamName.clear();
        m_AnimInitialized = true;
        m_AnimState = AnimAction::None;
        m_ActionClipIndex = -1;
        
        if (!m_Entity) {
            return;
        }

        m_Animator = findAnimatorInHierarchy(m_Entity->getTransform());
        if (m_Animator) {
            resolveAnimatorMapping();
            resolveAnimatorParameters();
            m_PrimarySkinned = findSkinnedInHierarchy(m_Animator->getEntity() ? m_Animator->getEntity()->getTransform() : nullptr);
            return;
        }

        collectSkinnedMeshes(m_Entity->getTransform(), m_AnimTargets);
        if (m_AnimTargets.empty()) {
            return;
        }
        resolveClipMapping();
        m_PrimarySkinned = m_AnimTargets.front();
    }

    void collectSkinnedMeshes(Transform* root, std::vector<SkinnedMeshRenderer*>& out) {
        if (!root) {
            return;
        }
        Entity* entity = root->getEntity();
        if (entity) {
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
        Entity* entity = root->getEntity();
        if (entity) {
            if (auto* animator = entity->getComponent<Animator>()) {
                return animator;
            }
        }
        for (Transform* child : root->getChildren()) {
            if (auto* animator = findAnimatorInHierarchy(child)) {
                return animator;
            }
        }
        return nullptr;
    }

    SkinnedMeshRenderer* findSkinnedInHierarchy(Transform* root) {
        if (!root) {
            return nullptr;
        }
        Entity* entity = root->getEntity();
        if (entity) {
            if (auto* skinned = entity->getComponent<SkinnedMeshRenderer>()) {
                return skinned;
            }
        }
        for (Transform* child : root->getChildren()) {
            if (auto* skinned = findSkinnedInHierarchy(child)) {
                return skinned;
            }
        }
        return nullptr;
    }

    void resolveClipMapping() {
        m_ClipMapping = ClipMapping();
        if (m_AnimTargets.empty()) {
            return;
        }
        const auto& clips = m_AnimTargets.front()->getAnimationClips();
        m_ClipMapping.idle = FindClipIndexAny(clips, {"idle", "stand"});
        m_ClipMapping.walk = FindClipIndexAny(clips, {"walk"});
        m_ClipMapping.run = FindClipIndexAny(clips, {"run", "sprint"});
        m_ClipMapping.fire = FindClipIndexAny(clips, {"fire", "shoot"});

        int reloadFull = FindClipIndex(clips, {"reload", "full"});
        int reloadEasy = FindClipIndex(clips, {"reload", "easy"});
        int reload = FindClipIndexAny(clips, {"reload"});
        if (reloadFull >= 0) {
            m_ClipMapping.reload = reloadFull;
        } else if (reload >= 0) {
            m_ClipMapping.reload = reload;
        } else {
            m_ClipMapping.reload = reloadEasy;
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
            if (m_AnimatorMapping.idleState < 0 && (name.find("idle") != std::string::npos || name.find("stand") != std::string::npos)) {
                m_AnimatorMapping.idleState = static_cast<int>(i);
                m_AnimatorMapping.idleClip = state.clipIndex;
            }
            if (m_AnimatorMapping.walkState < 0 && name.find("walk") != std::string::npos) {
                m_AnimatorMapping.walkState = static_cast<int>(i);
                m_AnimatorMapping.walkClip = state.clipIndex;
            }
            if (m_AnimatorMapping.runState < 0 && (name.find("run") != std::string::npos || name.find("sprint") != std::string::npos)) {
                m_AnimatorMapping.runState = static_cast<int>(i);
                m_AnimatorMapping.runClip = state.clipIndex;
            }
            if (m_AnimatorMapping.fireState < 0 && (name.find("fire") != std::string::npos || name.find("shoot") != std::string::npos)) {
                m_AnimatorMapping.fireState = static_cast<int>(i);
                m_AnimatorMapping.fireClip = state.clipIndex;
            }
            if (m_AnimatorMapping.reloadState < 0 && name.find("reload") != std::string::npos) {
                m_AnimatorMapping.reloadState = static_cast<int>(i);
                m_AnimatorMapping.reloadClip = state.clipIndex;
            }
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
            if (m_SpeedParamName.empty() && (name.find("speed") != std::string::npos || name.find("move") != std::string::npos)) {
                m_SpeedParamName = param.name;
            }
            if (m_MovingParamName.empty() && (name.find("moving") != std::string::npos || name.find("walk") != std::string::npos)) {
                m_MovingParamName = param.name;
            }
            if (m_FireParamName.empty() && (name.find("fire") != std::string::npos || name.find("shoot") != std::string::npos)) {
                m_FireParamName = param.name;
            }
            if (m_ReloadParamName.empty() && name.find("reload") != std::string::npos) {
                m_ReloadParamName = param.name;
            }
        }
        m_AnimatorHasMoveParam = !m_SpeedParamName.empty() || !m_MovingParamName.empty();
        m_AnimatorHasFireParam = !m_FireParamName.empty();
        m_AnimatorHasReloadParam = !m_ReloadParamName.empty();
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

    bool isActionClipFinished() const {
        if (m_ActionClipIndex < 0) {
            return true;
        }
        float duration = m_ActionDuration;
        if (duration <= 0.0f) {
            return true;
        }
        return m_ActionElapsed >= (duration - 0.005f);
    }

    void setTargetsClip(int clipIndex, int stateIndex, bool looping, bool restart) {
        if (clipIndex < 0) {
            return;
        }
        if (m_Animator) {
            if (stateIndex >= 0 && m_Animator->getCurrentStateIndex() != stateIndex) {
                m_Animator->setCurrentStateIndex(stateIndex, 0.05f, true);
            }
            if (m_PrimarySkinned) {
                m_PrimarySkinned->setLooping(looping);
                m_PrimarySkinned->setPlaying(true);
                if (restart) {
                    m_PrimarySkinned->setTimeSeconds(0.0f);
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
                skinned->setActiveClipIndex(clipIndex);
            } else if (restart) {
                skinned->setTimeSeconds(0.0f);
            }
            skinned->setLooping(looping);
            skinned->setPlaying(true);
        }
    }

    void startAction(AnimAction action, int clipIndex, int stateIndex, bool looping, bool restart) {
        if (clipIndex < 0) {
            return;
        }
        if (action == m_AnimState && clipIndex == m_ActionClipIndex) {
            return;
        }
        m_AnimState = action;
        m_ActionClipIndex = clipIndex;
        m_ActionStateIndex = stateIndex;
        m_ActionElapsed = 0.0f;
        m_ActionDuration = resolveClipDuration(clipIndex);
        setTargetsClip(clipIndex, stateIndex, looping, restart);
    }

    void updateAnimation(float deltaTime, InputManager& input) {
        if (!m_AnimInitialized || (!m_Animator && m_AnimTargets.empty())) {
            refreshAnimationTargets();
        }
        if (!m_Animator && m_AnimTargets.empty()) {
            return;
        }
        if (m_AnimState == AnimAction::Fire || m_AnimState == AnimAction::Reload) {
            m_ActionElapsed += deltaTime;
        }

        float moveX = 0.0f;
        float moveY = 0.0f;
        if (input.isKeyPressed(KeyCode::A)) moveX -= 1.0f;
        if (input.isKeyPressed(KeyCode::D)) moveX += 1.0f;
        if (input.isKeyPressed(KeyCode::W)) moveY += 1.0f;
        if (input.isKeyPressed(KeyCode::S)) moveY -= 1.0f;
        bool isMoving = (moveX * moveX + moveY * moveY) > 0.01f;
        bool wantsSprint = isMoving && m_EnableSprint && input.isKeyPressed(KeyCode::Shift);

        bool leftDown = input.isMouseButtonPressed(MouseButton::Left);
        bool fireTriggered = leftDown && !m_LeftMouseWasDown;
        m_LeftMouseWasDown = leftDown;
        bool fireAllowed = fireTriggered && m_FireCooldownTimer <= 0.0f;
        if (fireAllowed && m_FireCooldown > 0.0f) {
            m_FireCooldownTimer = m_FireCooldown;
        }

        bool reloadTriggered = input.isKeyDown(KeyCode::R);

        if (fireAllowed) {
            if (!m_FireAudio && m_Entity) {
                Transform* audioRoot = m_BodyTransform ? m_BodyTransform : m_Entity->getTransform();
                resolveAudioSources(audioRoot);
            }
            if (m_FireAudio) {
                m_FireAudio->play();
            }
            triggerMuzzleFlash();
        }

        if (reloadTriggered) {
            if (!m_ReloadAudio && m_Entity) {
                Transform* audioRoot = m_BodyTransform ? m_BodyTransform : m_Entity->getTransform();
                resolveAudioSources(audioRoot);
            }
            if (m_ReloadAudio) {
                m_ReloadAudio->play();
            }
        }

        if (m_Animator) {
            resolveAnimatorParameters();
            if (!m_SpeedParamName.empty()) {
                float speedValue = isMoving ? (wantsSprint ? 2.0f : 1.0f) : 0.0f;
                m_Animator->setParameterFloat(m_SpeedParamName, speedValue);
            }
            if (!m_MovingParamName.empty()) {
                m_Animator->setParameterBool(m_MovingParamName, isMoving);
            }
            if (fireAllowed && !m_FireParamName.empty()) {
                m_Animator->setTrigger(m_FireParamName);
            }
            if (reloadTriggered && !m_ReloadParamName.empty()) {
                m_Animator->setTrigger(m_ReloadParamName);
            }
        }

        if (reloadTriggered && (!m_Animator || !m_AnimatorHasReloadParam)) {
            int clipIndex = m_Animator ? m_AnimatorMapping.reloadClip : m_ClipMapping.reload;
            int stateIndex = m_Animator ? m_AnimatorMapping.reloadState : -1;
            if (clipIndex >= 0) {
                startAction(AnimAction::Reload, clipIndex, stateIndex, false, true);
                return;
            }
        }

        if (fireAllowed && (!m_Animator || !m_AnimatorHasFireParam)) {
            int clipIndex = m_Animator ? m_AnimatorMapping.fireClip : m_ClipMapping.fire;
            int stateIndex = m_Animator ? m_AnimatorMapping.fireState : -1;
            if (clipIndex >= 0) {
                startAction(AnimAction::Fire, clipIndex, stateIndex, false, true);
                return;
            }
        }

        if ((m_AnimState == AnimAction::Fire || m_AnimState == AnimAction::Reload) && !isActionClipFinished()) {
            return;
        }

        if (m_Animator && m_AnimatorHasMoveParam) {
            if (m_AnimState != AnimAction::Fire && m_AnimState != AnimAction::Reload) {
                return;
            }
        }

        AnimAction movementAction = AnimAction::Idle;
        int clipIndex = m_Animator ? m_AnimatorMapping.idleClip : m_ClipMapping.idle;
        int stateIndex = m_Animator ? m_AnimatorMapping.idleState : -1;
        if (isMoving) {
            if (wantsSprint && (m_Animator ? m_AnimatorMapping.runClip >= 0 : m_ClipMapping.run >= 0)) {
                movementAction = AnimAction::Run;
                clipIndex = m_Animator ? m_AnimatorMapping.runClip : m_ClipMapping.run;
                stateIndex = m_Animator ? m_AnimatorMapping.runState : -1;
            } else {
                movementAction = AnimAction::Walk;
                clipIndex = m_Animator ? m_AnimatorMapping.walkClip : m_ClipMapping.walk;
                stateIndex = m_Animator ? m_AnimatorMapping.walkState : -1;
            }
        }

        if (clipIndex >= 0) {
            startAction(movementAction, clipIndex, stateIndex, true, false);
        }
    }

    void findDependencies() {
        m_Controller = nullptr;
        m_BodyTransform = nullptr;
        m_CameraTransform = nullptr;
        m_FireAudio = nullptr;
        m_ReloadAudio = nullptr;

        if (!m_Entity) {
            return;
        }

        Entity* bodyEntity = resolveBodyEntity();
        if (bodyEntity) {
            m_Controller = bodyEntity->getComponent<CharacterController>();
            m_BodyTransform = bodyEntity->getTransform();
        } else {
            m_BodyTransform = m_Entity->getTransform();
        }

        if (!m_BodyTransform) {
            return;
        }

        if (m_Controller && !m_HeightsInitialized) {
            m_Controller->setUseInput(false);
            m_StandingHeight = m_Controller->getHeight();
            m_CurrentHeight = m_StandingHeight;
            m_CurrentEyeHeight = m_EyeHeight;
            m_HeightsInitialized = true;
        }

        if (m_Entity->getComponent<Camera>()) {
            m_CameraTransform = m_Entity->getTransform();
            Transform* audioRoot = m_BodyTransform ? m_BodyTransform : m_Entity->getTransform();
            resolveAudioSources(audioRoot);
            return;
        }

        m_CameraTransform = findCameraTransform(m_BodyTransform);
        Transform* audioRoot = m_BodyTransform ? m_BodyTransform : m_Entity->getTransform();
        resolveAudioSources(audioRoot);
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

    Transform* findCameraTransform(Transform* root) const {
        if (!root) {
            return nullptr;
        }
        std::vector<Transform*> stack;
        stack.reserve(8);
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

    static bool ContainsAnyToken(const std::string& haystack, const std::vector<std::string>& tokens) {
        for (const auto& token : tokens) {
            if (token.empty()) {
                continue;
            }
            if (haystack.find(token) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    Transform* findTransformByTokens(Transform* root, const std::vector<std::string>& tokens) const {
        if (!root) {
            return nullptr;
        }
        Entity* entity = root->getEntity();
        if (entity) {
            std::string name = ToLower(entity->getName());
            if (ContainsAnyToken(name, tokens)) {
                return root;
            }
        }
        for (Transform* child : root->getChildren()) {
            if (auto* found = findTransformByTokens(child, tokens)) {
                return found;
            }
        }
        return nullptr;
    }

    AudioSource* findAudioSourceInHierarchy(Transform* root) const {
        if (!root) {
            return nullptr;
        }
        Entity* entity = root->getEntity();
        if (entity) {
            if (auto* audio = entity->getComponent<AudioSource>()) {
                return audio;
            }
        }
        for (Transform* child : root->getChildren()) {
            if (auto* audio = findAudioSourceInHierarchy(child)) {
                return audio;
            }
        }
        return nullptr;
    }

    void collectAudioSources(Transform* root, std::vector<AudioSource*>& out) const {
        if (!root) {
            return;
        }
        Entity* entity = root->getEntity();
        if (entity) {
            if (auto* audio = entity->getComponent<AudioSource>()) {
                out.push_back(audio);
            }
        }
        for (Transform* child : root->getChildren()) {
            collectAudioSources(child, out);
        }
    }

    AudioSource* findAudioSourceByTokens(Transform* root, const std::vector<std::string>& tokens) const {
        if (!root) {
            return nullptr;
        }
        Entity* entity = root->getEntity();
        if (entity) {
            if (auto* audio = entity->getComponent<AudioSource>()) {
                std::string name = ToLower(entity->getName());
                std::string path = ToLower(audio->getFilePath());
                if (ContainsAnyToken(name, tokens) || ContainsAnyToken(path, tokens)) {
                    return audio;
                }
            }
        }
        for (Transform* child : root->getChildren()) {
            if (auto* audio = findAudioSourceByTokens(child, tokens)) {
                return audio;
            }
        }
        return nullptr;
    }

    void resolveAudioSources(Transform* root) {
        std::vector<AudioSource*> sources;
        collectAudioSources(root, sources);
        m_FireAudio = findAudioSourceByTokens(root, {"fire", "shoot"});
        m_ReloadAudio = findAudioSourceByTokens(root, {"reload"});
        if (!m_FireAudio) {
            if (!sources.empty()) {
                m_FireAudio = sources.front();
            } else {
                m_FireAudio = findAudioSourceInHierarchy(root);
            }
        }
        if (!m_ReloadAudio) {
            for (auto* audio : sources) {
                if (audio && audio != m_FireAudio) {
                    m_ReloadAudio = audio;
                    break;
                }
            }
        }
    }

    void ensureMuzzleFlash() {
        if (m_MuzzleInitialized) {
            return;
        }
        if (!m_Entity) {
            return;
        }
        Transform* root = m_BodyTransform ? m_BodyTransform : m_Entity->getTransform();
        if (!root) {
            return;
        }

        m_MuzzleAnchor = findTransformByTokens(root, {"muzzle", "flash", "barrel", "fire"});
        if (!m_MuzzleAnchor) {
            m_MuzzleAnchor = m_CameraTransform ? m_CameraTransform : root;
        }

        Entity* anchorEntity = m_MuzzleAnchor ? m_MuzzleAnchor->getEntity() : nullptr;
        Scene* scene = anchorEntity ? anchorEntity->getScene() : nullptr;
        if (!scene) {
            return;
        }

        Entity* flashEntity = scene->createEntity("MuzzleFlash");
        Transform* flashTransform = flashEntity->getTransform();
        flashTransform->setParent(m_MuzzleAnchor, false);
        m_MuzzleTransform = flashTransform;

        float forwardOffset = (m_MuzzleAnchor == m_CameraTransform || m_MuzzleAnchor == root) ? -0.28f : 0.0f;
        flashTransform->setLocalPosition(Math::Vector3(0.0f, 0.0f, forwardOffset));

        m_MuzzleMaterial = Material::CreateEmissive(m_MuzzleColor, 0.0f);
        m_MuzzleMaterial->setRenderMode(Material::RenderMode::Transparent);
        m_MuzzleMaterial->setTwoSided(true);
        m_MuzzleMaterial->setAlbedo(Math::Vector4(0.0f, 0.0f, 0.0f, 1.0f));
        m_MuzzleMaterial->setAlpha(0.0f);

        if (!m_MuzzleTexture && !m_MuzzleTexturePath.empty()) {
            loadMuzzleTexture();
        }

        if (!m_MuzzleTexture) {
            if (auto* renderer = Engine::getInstance().getRenderer()) {
                if (auto* loader = renderer->getTextureLoader()) {
                    const int size = 64;
                    std::vector<unsigned char> rgba(size * size * 4);
                    for (int y = 0; y < size; ++y) {
                        for (int x = 0; x < size; ++x) {
                            float fx = (static_cast<float>(x) / (size - 1)) * 2.0f - 1.0f;
                            float fy = (static_cast<float>(y) / (size - 1)) * 2.0f - 1.0f;
                            float dist = std::sqrt(fx * fx + fy * fy);
                            float falloff = std::max(0.0f, 1.0f - dist);
                            float intensity = falloff * falloff;
                            unsigned char value = static_cast<unsigned char>(Math::Clamp(intensity, 0.0f, 1.0f) * 255.0f);
                            size_t idx = static_cast<size_t>((y * size + x) * 4);
                            rgba[idx + 0] = value;
                            rgba[idx + 1] = value;
                            rgba[idx + 2] = value;
                            rgba[idx + 3] = value;
                        }
                    }
                    m_MuzzleTexture = loader->createTextureFromRGBA8("muzzle_flash_fallback", rgba.data(), size, size, false, false);
                }
            }
        }
        if (m_MuzzleTexture) {
            m_MuzzleMaterial->setAlbedoTexture(m_MuzzleTexture);
            m_MuzzleMaterial->setEmissionTexture(m_MuzzleTexture);
        }

        m_MuzzleMesh = flashEntity->addComponent<MeshRenderer>();
        m_MuzzleMesh->setMesh(Mesh::CreatePlane(0.07f, 0.045f, 1, 1));
        m_MuzzleMesh->setCastShadows(false);
        m_MuzzleMesh->setReceiveShadows(false);
        m_MuzzleMesh->setMaterial(m_MuzzleMaterial);
        m_MuzzleMesh->setEnabled(false);

        flashTransform->setLocalRotation(m_MuzzleRotationOffset);
        m_MuzzleBaseScale = Math::Vector3(1.0f, 1.0f, 1.0f);
        flashTransform->setLocalScale(m_MuzzleBaseScale);

        m_MuzzleLight = flashEntity->addComponent<Light>();
        m_MuzzleLight->setType(Light::Type::Point);
        m_MuzzleLight->setColor(m_MuzzleColor);
        m_MuzzleLight->setRange(m_MuzzleLightRange);
        m_MuzzleLight->setIntensity(0.0f);
        m_MuzzleLight->setCastShadows(false);

        m_MuzzleInitialized = true;
        applyMuzzleFlash(0.0f);
    }

    void loadMuzzleTexture() {
        m_MuzzleTexture.reset();
        if (m_MuzzleTexturePath.empty()) {
            return;
        }
        if (auto* renderer = Engine::getInstance().getRenderer()) {
            if (auto* loader = renderer->getTextureLoader()) {
                m_MuzzleTexture = loader->loadTexture(m_MuzzleTexturePath, true, true);
            }
        }
        if (m_MuzzleMaterial && m_MuzzleTexture) {
            m_MuzzleMaterial->setAlbedoTexture(m_MuzzleTexture);
            m_MuzzleMaterial->setEmissionTexture(m_MuzzleTexture);
        }
    }

    void triggerMuzzleFlash() {
        ensureMuzzleFlash();
        if (!m_MuzzleInitialized) {
            return;
        }
        m_MuzzleTimer = m_MuzzleDuration;
        applyMuzzleFlash(1.0f);
    }

    void applyMuzzleFlash(float normalized) {
        float ramp = normalized * normalized;
        if (m_MuzzleLight) {
            m_MuzzleLight->setIntensity(m_MuzzleLightIntensity * ramp);
        }
        if (m_MuzzleMaterial) {
            m_MuzzleMaterial->setEmissionStrength(m_MuzzleEmissionStrength * ramp);
            m_MuzzleMaterial->setAlpha(0.45f * ramp);
        }
        if (!m_MuzzleMesh || !m_MuzzleMesh->getEntity()) {
            return;
        }
        if (normalized <= 0.0f) {
            m_MuzzleMesh->setEnabled(false);
            return;
        }
        orientMuzzleFlash();
        m_MuzzleMesh->setEnabled(true);
        float scale = 0.65f + ramp * 0.9f;
        m_MuzzleMesh->getEntity()->getTransform()->setLocalScale(m_MuzzleBaseScale * scale);
    }

    void updateMuzzleFlash(float deltaTime) {
        if (!m_MuzzleInitialized) {
            return;
        }
        if (m_MuzzleTimer <= 0.0f) {
            applyMuzzleFlash(0.0f);
            return;
        }
        m_MuzzleTimer = std::max(0.0f, m_MuzzleTimer - deltaTime);
        float normalized = (m_MuzzleDuration > 0.0f) ? (m_MuzzleTimer / m_MuzzleDuration) : 0.0f;
        applyMuzzleFlash(normalized);
    }

    void orientMuzzleFlash() {
        if (!m_MuzzleTransform) {
            return;
        }
        Transform* camera = m_CameraTransform ? m_CameraTransform : m_BodyTransform;
        if (!camera) {
            return;
        }
        Math::Vector3 flashPos = m_MuzzleTransform->getPosition();
        Math::Vector3 toCamera = camera->getPosition() - flashPos;
        if (toCamera.lengthSquared() < 0.0001f) {
            return;
        }
        Math::Quaternion lookRot = Math::Quaternion::LookRotation(toCamera, camera->up());
        m_MuzzleTransform->setRotation(lookRot * m_MuzzleRotationOffset);
    }

    void initializeLook() {
        if (!m_BodyTransform) {
            return;
        }
        Math::Vector3 forward = m_BodyTransform->forward();
        m_Yaw = std::atan2(forward.x, -forward.z);
        float horizontalLength = std::sqrt(forward.x * forward.x + forward.z * forward.z);
        m_Pitch = std::atan2(-forward.y, horizontalLength);
    }

    void applyRotation() {
        if (!m_BodyTransform || !m_CameraTransform) {
            return;
        }

        Math::Quaternion yawQuat = Math::Quaternion::FromAxisAngle(Math::Vector3::Up, m_Yaw);
        if (m_CameraTransform == m_BodyTransform) {
            Math::Quaternion pitchQuat = Math::Quaternion::FromAxisAngle(Math::Vector3::Right, m_Pitch);
            m_BodyTransform->setRotation(pitchQuat * yawQuat);
            return;
        }

        m_BodyTransform->setRotation(yawQuat);
        Math::Quaternion pitchQuat = Math::Quaternion::FromAxisAngle(Math::Vector3::Right, m_Pitch);
        m_CameraTransform->setLocalRotation(pitchQuat);
    }

    void applyCameraHeight(float deltaTime, InputManager& input) {
        if (!m_Controller || !m_CameraTransform || !m_UseEyeHeight) {
            return;
        }

        bool crouch = m_EnableCrouch && input.isKeyPressed(KeyCode::Control);
        float targetHeight = crouch ? m_CrouchHeight : m_StandingHeight;
        float targetEye = crouch ? m_CrouchEyeHeight : m_EyeHeight;

        float nextHeight = Math::MoveTowards(m_CurrentHeight, targetHeight, m_CrouchSpeed * deltaTime);
        float nextEye = Math::MoveTowards(m_CurrentEyeHeight, targetEye, m_CrouchSpeed * deltaTime);

        if (m_CurrentHeight != nextHeight) {
            m_CurrentHeight = nextHeight;
            m_Controller->setHeight(m_CurrentHeight);
        }
        if (m_CurrentEyeHeight != nextEye && m_CameraTransform != m_BodyTransform) {
            m_CurrentEyeHeight = nextEye;
            m_CameraTransform->setLocalPosition(Math::Vector3(0.0f, m_CurrentEyeHeight, 0.0f));
        }
    }

    void driveCharacter(float deltaTime, InputManager& input) {
        if (!m_Controller) {
            return;
        }

        float speed = m_WalkSpeed;
        if (m_EnableSprint && input.isKeyPressed(KeyCode::Shift)) {
            speed *= m_SprintMultiplier;
        }
        m_Controller->setMoveSpeed(speed);

        float x = 0.0f;
        float y = 0.0f;
        if (input.isKeyPressed(KeyCode::A)) x -= 1.0f;
        if (input.isKeyPressed(KeyCode::D)) x += 1.0f;
        if (input.isKeyPressed(KeyCode::W)) y += 1.0f;
        if (input.isKeyPressed(KeyCode::S)) y -= 1.0f;

        Math::Vector2 move(x, y);
        if (move.lengthSquared() > 1.0f) {
            move.normalize();
        }

        m_Controller->setMoveInput(move);
        if (input.isKeyDown(KeyCode::Space)) {
            m_Controller->requestJump();
        }

        if (m_DriveCharacterController) {
            m_Controller->OnUpdate(deltaTime);
        }
    }

private:
    float m_MouseSensitivity;
    bool m_InvertY;
    bool m_RequireLookButton;
    MouseButton m_LookButton;
    float m_MinPitch;
    float m_MaxPitch;
    float m_WalkSpeed;
    float m_SprintMultiplier;
    bool m_EnableSprint;
    bool m_EnableCrouch;
    float m_CrouchHeight;
    float m_CrouchEyeHeight;
    float m_CrouchSpeed;
    float m_EyeHeight;
    bool m_UseEyeHeight;
    bool m_DriveCharacterController;
    float m_FireCooldown;
    float m_FireCooldownTimer = 0.0f;

    CharacterController* m_Controller = nullptr;
    Transform* m_BodyTransform = nullptr;
    Transform* m_CameraTransform = nullptr;
    AudioSource* m_FireAudio = nullptr;
    AudioSource* m_ReloadAudio = nullptr;
    Animator* m_Animator = nullptr;
    SkinnedMeshRenderer* m_PrimarySkinned = nullptr;
    std::vector<SkinnedMeshRenderer*> m_AnimTargets;
    ClipMapping m_ClipMapping;
    AnimatorMapping m_AnimatorMapping;
    AnimAction m_AnimState = AnimAction::None;
    int m_ActionClipIndex = -1;
    int m_ActionStateIndex = -1;
    float m_ActionElapsed = 0.0f;
    float m_ActionDuration = 0.0f;
    bool m_LeftMouseWasDown = false;
    bool m_AnimInitialized = false;
    bool m_AnimatorParamsResolved = false;
    bool m_AnimatorHasMoveParam = false;
    bool m_AnimatorHasFireParam = false;
    bool m_AnimatorHasReloadParam = false;
    std::string m_SpeedParamName;
    std::string m_MovingParamName;
    std::string m_FireParamName;
    std::string m_ReloadParamName;

    float m_Yaw = 0.0f;
    float m_Pitch = 0.0f;
    float m_StandingHeight = 2.0f;
    float m_CurrentHeight = 2.0f;
    float m_CurrentEyeHeight = 1.6f;
    bool m_HeightsInitialized = false;

    Light* m_MuzzleLight = nullptr;
    MeshRenderer* m_MuzzleMesh = nullptr;
    Transform* m_MuzzleTransform = nullptr;
    std::shared_ptr<Material> m_MuzzleMaterial;
    std::shared_ptr<Texture2D> m_MuzzleTexture;
    std::string m_MuzzleTexturePath;
    Transform* m_MuzzleAnchor = nullptr;
    Math::Vector3 m_MuzzleBaseScale = Math::Vector3(1.0f, 1.0f, 1.0f);
    Math::Quaternion m_MuzzleRotationOffset;
    float m_MuzzleTimer = 0.0f;
    float m_MuzzleDuration = 0.06f;
    float m_MuzzleLightIntensity = 6.5f;
    float m_MuzzleLightRange = 2.0f;
    float m_MuzzleEmissionStrength = 18.0f;
    Math::Vector3 m_MuzzleColor = Math::Vector3(1.0f, 0.8f, 0.5f);
    bool m_MuzzleInitialized = false;
};

} // namespace Crescent
