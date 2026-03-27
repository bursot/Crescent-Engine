#include "AudioSystem.hpp"
#include "../Components/Camera.hpp"
#include "../ECS/Entity.hpp"
#include "../ECS/Transform.hpp"
#include <algorithm>
#include <iostream>

#define MINIAUDIO_IMPLEMENTATION
#include "../../../ThirdParty/miniaudio/miniaudio.h"

namespace Crescent {

AudioSystem& AudioSystem::getInstance() {
    static AudioSystem instance;
    return instance;
}

void AudioSystem::cleanupFinishedOneShots() {
    if (m_ActiveOneShots.empty()) {
        return;
    }

    auto it = m_ActiveOneShots.begin();
    while (it != m_ActiveOneShots.end()) {
        ma_sound* sound = *it;
        if (!sound) {
            it = m_ActiveOneShots.erase(it);
            continue;
        }

        if (ma_sound_is_playing(sound) == MA_TRUE) {
            ++it;
            continue;
        }

        ma_sound_uninit(sound);
        delete sound;
        it = m_ActiveOneShots.erase(it);
    }
}

bool AudioSystem::initialize() {
    if (m_Initialized) {
        return true;
    }

    if (!m_Engine) {
        m_Engine = new ma_engine();
    }

    ma_engine_config config = ma_engine_config_init();
    config.listenerCount = 1;

    ma_result result = ma_engine_init(&config, m_Engine);
    if (result != MA_SUCCESS) {
        std::cerr << "Failed to initialize audio engine (" << result << ")" << std::endl;
        delete m_Engine;
        m_Engine = nullptr;
        return false;
    }

    if (!m_SfxGroup) {
        m_SfxGroup = new ma_sound_group();
    }
    result = ma_sound_group_init(m_Engine, 0, NULL, m_SfxGroup);
    if (result != MA_SUCCESS) {
        std::cerr << "Failed to initialize SFX group (" << result << ")" << std::endl;
        ma_engine_uninit(m_Engine);
        delete m_Engine;
        m_Engine = nullptr;
        delete m_SfxGroup;
        m_SfxGroup = nullptr;
        return false;
    }
    ma_sound_group_set_volume(m_SfxGroup, 0.45f);

    m_Initialized = true;
    return true;
}

void AudioSystem::shutdown() {
    if (!m_Initialized) {
        return;
    }

    for (ma_sound* sound : m_ActiveOneShots) {
        if (!sound) {
            continue;
        }
        ma_sound_uninit(sound);
        delete sound;
    }
    m_ActiveOneShots.clear();

    if (m_SfxGroup) {
        ma_sound_group_uninit(m_SfxGroup);
        delete m_SfxGroup;
        m_SfxGroup = nullptr;
    }

    if (m_Engine) {
        ma_engine_uninit(m_Engine);
        delete m_Engine;
        m_Engine = nullptr;
    }

    m_Initialized = false;
}

void AudioSystem::updateListener(const Math::Vector3& position,
                                 const Math::Vector3& forward,
                                 const Math::Vector3& up) {
    if (!m_Initialized || !m_Engine) {
        return;
    }

    cleanupFinishedOneShots();

    ma_engine_listener_set_position(m_Engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(m_Engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(m_Engine, 0, up.x, up.y, up.z);
}

void AudioSystem::updateListenerFromCamera(const Camera* camera) {
    if (!camera) {
        return;
    }

    const Entity* entity = camera->getEntity();
    if (!entity) {
        return;
    }

    const Transform* transform = entity->getTransform();
    if (!transform) {
        return;
    }

    updateListener(transform->getPosition(), transform->forward(), transform->up());
}

bool AudioSystem::playOneShot(const std::string& filePath,
                              float volume,
                              float pitch) {
    if (!m_Initialized || !m_Engine || filePath.empty()) {
        return false;
    }

    cleanupFinishedOneShots();

    ma_sound* sound = new ma_sound();
    ma_result result = ma_sound_init_from_file(m_Engine,
                                               filePath.c_str(),
                                               MA_SOUND_FLAG_DECODE,
                                               m_SfxGroup,
                                               nullptr,
                                               sound);
    if (result != MA_SUCCESS) {
        delete sound;
        return false;
    }

    ma_sound_set_spatialization_enabled(sound, MA_FALSE);
    ma_sound_set_volume(sound, std::max(0.0f, volume));
    ma_sound_set_pitch(sound, std::max(0.01f, pitch));

    result = ma_sound_start(sound);
    if (result != MA_SUCCESS) {
        ma_sound_uninit(sound);
        delete sound;
        return false;
    }

    m_ActiveOneShots.push_back(sound);
    return true;
}

bool AudioSystem::playOneShot3D(const std::string& filePath,
                                const Math::Vector3& position,
                                float volume,
                                float pitch,
                                float minDistance,
                                float maxDistance,
                                float rolloff) {
    if (!m_Initialized || !m_Engine || filePath.empty()) {
        return false;
    }

    cleanupFinishedOneShots();

    ma_sound* sound = new ma_sound();
    ma_result result = ma_sound_init_from_file(m_Engine,
                                               filePath.c_str(),
                                               MA_SOUND_FLAG_DECODE,
                                               m_SfxGroup,
                                               nullptr,
                                               sound);
    if (result != MA_SUCCESS) {
        delete sound;
        return false;
    }

    ma_sound_set_spatialization_enabled(sound, MA_TRUE);
    ma_sound_set_attenuation_model(sound, ma_attenuation_model_inverse);
    ma_sound_set_position(sound, position.x, position.y, position.z);
    ma_sound_set_min_distance(sound, std::max(0.01f, minDistance));
    ma_sound_set_max_distance(sound, std::max(minDistance, maxDistance));
    ma_sound_set_rolloff(sound, std::max(0.0f, rolloff));
    ma_sound_set_volume(sound, std::max(0.0f, volume));
    ma_sound_set_pitch(sound, std::max(0.01f, pitch));

    result = ma_sound_start(sound);
    if (result != MA_SUCCESS) {
        ma_sound_uninit(sound);
        delete sound;
        return false;
    }

    m_ActiveOneShots.push_back(sound);
    return true;
}

bool AudioSystem::playOneShot3DDirectional(const std::string& filePath,
                                           const Math::Vector3& position,
                                           const Math::Vector3& direction,
                                           float volume,
                                           float pitch,
                                           float minDistance,
                                           float maxDistance,
                                           float rolloff,
                                           float innerConeRadians,
                                           float outerConeRadians,
                                           float outerGain,
                                           float directionalAttenuationFactor) {
    if (!m_Initialized || !m_Engine || filePath.empty()) {
        return false;
    }

    cleanupFinishedOneShots();

    ma_sound* sound = new ma_sound();
    ma_result result = ma_sound_init_from_file(m_Engine,
                                               filePath.c_str(),
                                               MA_SOUND_FLAG_DECODE,
                                               m_SfxGroup,
                                               nullptr,
                                               sound);
    if (result != MA_SUCCESS) {
        delete sound;
        return false;
    }

    Math::Vector3 dir = direction.lengthSquared() > Math::EPSILON
        ? direction.normalized()
        : Math::Vector3(0.0f, 0.0f, -1.0f);

    ma_sound_set_spatialization_enabled(sound, MA_TRUE);
    ma_sound_set_attenuation_model(sound, ma_attenuation_model_inverse);
    ma_sound_set_position(sound, position.x, position.y, position.z);
    ma_sound_set_direction(sound, dir.x, dir.y, dir.z);
    ma_sound_set_cone(sound, innerConeRadians, outerConeRadians, outerGain);
    ma_sound_set_directional_attenuation_factor(sound, std::max(0.0f, directionalAttenuationFactor));
    ma_sound_set_min_distance(sound, std::max(0.01f, minDistance));
    ma_sound_set_max_distance(sound, std::max(minDistance, maxDistance));
    ma_sound_set_rolloff(sound, std::max(0.0f, rolloff));
    ma_sound_set_volume(sound, std::max(0.0f, volume));
    ma_sound_set_pitch(sound, std::max(0.01f, pitch));

    result = ma_sound_start(sound);
    if (result != MA_SUCCESS) {
        ma_sound_uninit(sound);
        delete sound;
        return false;
    }

    m_ActiveOneShots.push_back(sound);
    return true;
}

} // namespace Crescent
