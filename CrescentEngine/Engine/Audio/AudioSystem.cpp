#include "AudioSystem.hpp"
#include "../Components/Camera.hpp"
#include "../ECS/Entity.hpp"
#include "../ECS/Transform.hpp"
#include <iostream>

#define MINIAUDIO_IMPLEMENTATION
#include "../../../ThirdParty/miniaudio/miniaudio.h"

namespace Crescent {

AudioSystem& AudioSystem::getInstance() {
    static AudioSystem instance;
    return instance;
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

    m_Initialized = true;
    return true;
}

void AudioSystem::shutdown() {
    if (!m_Initialized) {
        return;
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

} // namespace Crescent
