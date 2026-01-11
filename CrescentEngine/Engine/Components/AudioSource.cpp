#include "AudioSource.hpp"
#include "../Audio/AudioSystem.hpp"
#include "../ECS/Entity.hpp"
#include "../ECS/Transform.hpp"
#include <algorithm>
#include <iostream>

#include "../../../ThirdParty/miniaudio/miniaudio.h"

namespace Crescent {

AudioSource::AudioSource() = default;

AudioSource::~AudioSource() {
    unloadSound();
}

void AudioSource::OnCreate() {
    loadSound();
}

void AudioSource::OnStart() {
    if (m_PlayOnStart) {
        play();
    }
}

void AudioSource::OnUpdate(float) {
    updateSpatial();
}

void AudioSource::OnDisable() {
    stop();
}

void AudioSource::OnDestroy() {
    unloadSound();
}

void AudioSource::setFilePath(const std::string& path) {
    if (m_FilePath == path) {
        return;
    }
    m_FilePath = path;
    loadSound();
}

void AudioSource::setVolume(float value) {
    m_Volume = std::max(0.0f, value);
    if (m_Sound && m_Loaded) {
        ma_sound_set_volume(m_Sound, m_Volume);
    }
}

void AudioSource::setPitch(float value) {
    m_Pitch = std::max(0.01f, value);
    if (m_Sound && m_Loaded) {
        ma_sound_set_pitch(m_Sound, m_Pitch);
    }
}

void AudioSource::setLooping(bool loop) {
    m_Looping = loop;
    if (m_Sound && m_Loaded) {
        ma_sound_set_looping(m_Sound, m_Looping);
    }
}

void AudioSource::setSpatial(bool value) {
    m_Spatial = value;
    if (m_Sound && m_Loaded) {
        ma_sound_set_spatialization_enabled(m_Sound, m_Spatial);
        ma_sound_set_attenuation_model(m_Sound, m_Spatial ? ma_attenuation_model_inverse : ma_attenuation_model_none);
    }
}

void AudioSource::setStreaming(bool value) {
    if (m_Stream == value) {
        return;
    }
    m_Stream = value;
    loadSound();
}

void AudioSource::setMinDistance(float value) {
    m_MinDistance = std::max(0.01f, value);
    if (m_MaxDistance < m_MinDistance) {
        m_MaxDistance = m_MinDistance;
    }
    if (m_Sound && m_Loaded) {
        ma_sound_set_min_distance(m_Sound, m_MinDistance);
        ma_sound_set_max_distance(m_Sound, m_MaxDistance);
    }
}

void AudioSource::setMaxDistance(float value) {
    m_MaxDistance = std::max(m_MinDistance, value);
    if (m_Sound && m_Loaded) {
        ma_sound_set_max_distance(m_Sound, m_MaxDistance);
    }
}

void AudioSource::setRolloff(float value) {
    m_Rolloff = std::max(0.0f, value);
    if (m_Sound && m_Loaded) {
        ma_sound_set_rolloff(m_Sound, m_Rolloff);
    }
}

bool AudioSource::isPlaying() const {
    if (!m_Sound || !m_Loaded) {
        return false;
    }
    return ma_sound_is_playing(m_Sound) == MA_TRUE;
}

void AudioSource::play() {
    if (!m_Sound || !m_Loaded) {
        if (!loadSound()) {
            return;
        }
    }
    if (!m_Sound) {
        return;
    }

    ma_sound_stop(m_Sound);
    ma_sound_seek_to_pcm_frame(m_Sound, 0);
    updateSpatial();
    ma_sound_start(m_Sound);
}

void AudioSource::stop() {
    if (!m_Sound || !m_Loaded) {
        return;
    }
    ma_sound_stop(m_Sound);
}

bool AudioSource::loadSound() {
    unloadSound();

    if (m_FilePath.empty()) {
        return false;
    }

    AudioSystem& audio = AudioSystem::getInstance();
    if (!audio.isInitialized()) {
        return false;
    }

    ma_engine* engine = audio.getEngine();
    if (!engine) {
        return false;
    }

    m_Sound = new ma_sound();
    ma_uint32 flags = m_Stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
    ma_result result = ma_sound_init_from_file(engine, m_FilePath.c_str(), flags, NULL, NULL, m_Sound);
    if (result != MA_SUCCESS) {
        std::cerr << "Failed to load audio: " << m_FilePath << " (" << result << ")" << std::endl;
        delete m_Sound;
        m_Sound = nullptr;
        return false;
    }

    m_Loaded = true;
    applySettings();
    updateSpatial();
    return true;
}

void AudioSource::unloadSound() {
    if (m_Sound && m_Loaded) {
        ma_sound_uninit(m_Sound);
    }
    delete m_Sound;
    m_Sound = nullptr;
    m_Loaded = false;
}

void AudioSource::applySettings() {
    if (!m_Sound || !m_Loaded) {
        return;
    }

    ma_sound_set_volume(m_Sound, m_Volume);
    ma_sound_set_pitch(m_Sound, m_Pitch);
    ma_sound_set_looping(m_Sound, m_Looping);
    ma_sound_set_spatialization_enabled(m_Sound, m_Spatial);
    ma_sound_set_attenuation_model(m_Sound, m_Spatial ? ma_attenuation_model_inverse : ma_attenuation_model_none);
    ma_sound_set_min_distance(m_Sound, m_MinDistance);
    ma_sound_set_max_distance(m_Sound, m_MaxDistance);
    ma_sound_set_rolloff(m_Sound, m_Rolloff);
}

void AudioSource::updateSpatial() {
    if (!m_Sound || !m_Loaded || !m_Spatial) {
        return;
    }
    if (!m_Entity) {
        return;
    }
    Transform* transform = m_Entity->getTransform();
    if (!transform) {
        return;
    }
    Math::Vector3 position = transform->getPosition();
    ma_sound_set_position(m_Sound, position.x, position.y, position.z);
}

} // namespace Crescent
