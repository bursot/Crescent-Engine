#pragma once

#include "../Math/Vector3.hpp"
#include <string>
#include <vector>

struct ma_engine;
struct ma_sound;

namespace Crescent {

class Camera;
enum class AudioBus {
    Master = 0,
    SFX,
    Vocal,
    Music,
    Ambience,
    UI
};

class AudioSystem {
public:
    static AudioSystem& getInstance();
    static const char* audioBusToString(AudioBus bus);
    static AudioBus audioBusFromString(const std::string& value);

    bool initialize();
    void shutdown();
    bool isInitialized() const { return m_Initialized; }

    void updateListener(const Math::Vector3& position,
                        const Math::Vector3& forward,
                        const Math::Vector3& up);
    void updateListenerFromCamera(const Camera* camera);
    void setBusVolume(AudioBus bus, float volume);
    float getBusVolume(AudioBus bus) const;
    bool playOneShot(const std::string& filePath,
                     float volume = 1.0f,
                     float pitch = 1.0f);
    bool playOneShot(const std::string& filePath,
                     AudioBus bus,
                     float volume,
                     float pitch);
    bool playOneShot3D(const std::string& filePath,
                       const Math::Vector3& position,
                       float volume = 1.0f,
                       float pitch = 1.0f,
                       float minDistance = 1.0f,
                       float maxDistance = 18.0f,
                       float rolloff = 1.0f);
    bool playOneShot3D(const std::string& filePath,
                       const Math::Vector3& position,
                       AudioBus bus,
                       float volume,
                       float pitch,
                       float minDistance,
                       float maxDistance,
                       float rolloff);
    bool playOneShot3DDirectional(const std::string& filePath,
                                  const Math::Vector3& position,
                                  const Math::Vector3& direction,
                                  float volume = 1.0f,
                                  float pitch = 1.0f,
                                  float minDistance = 1.0f,
                                  float maxDistance = 18.0f,
                                  float rolloff = 1.0f,
                                  float innerConeRadians = 6.283185f,
                                  float outerConeRadians = 6.283185f,
                                  float outerGain = 1.0f,
                                  float directionalAttenuationFactor = 0.0f);
    bool playOneShot3DDirectional(const std::string& filePath,
                                  const Math::Vector3& position,
                                  const Math::Vector3& direction,
                                  AudioBus bus,
                                  float volume,
                                  float pitch,
                                  float minDistance,
                                  float maxDistance,
                                  float rolloff,
                                  float innerConeRadians,
                                  float outerConeRadians,
                                  float outerGain,
                                  float directionalAttenuationFactor);

    ma_engine* getEngine() const { return m_Engine; }
    ma_sound* getBusGroup(AudioBus bus) const;

private:
    void cleanupFinishedOneShots();

    AudioSystem() = default;
    ~AudioSystem() = default;
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    ma_engine* m_Engine = nullptr;
    ma_sound* m_MasterGroup = nullptr;
    ma_sound* m_SfxGroup = nullptr;
    ma_sound* m_VocalGroup = nullptr;
    ma_sound* m_MusicGroup = nullptr;
    ma_sound* m_AmbienceGroup = nullptr;
    ma_sound* m_UIGroup = nullptr;
    std::vector<ma_sound*> m_ActiveOneShots;
    bool m_Initialized = false;
};

} // namespace Crescent
