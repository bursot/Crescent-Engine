#pragma once

#include "../Math/Vector3.hpp"

struct ma_engine;

namespace Crescent {

class Camera;

class AudioSystem {
public:
    static AudioSystem& getInstance();

    bool initialize();
    void shutdown();
    bool isInitialized() const { return m_Initialized; }

    void updateListener(const Math::Vector3& position,
                        const Math::Vector3& forward,
                        const Math::Vector3& up);
    void updateListenerFromCamera(const Camera* camera);

    ma_engine* getEngine() const { return m_Engine; }

private:
    AudioSystem() = default;
    ~AudioSystem() = default;
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    ma_engine* m_Engine = nullptr;
    bool m_Initialized = false;
};

} // namespace Crescent
