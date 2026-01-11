#pragma once

#include "../ECS/Component.hpp"
#include <string>

struct ma_sound;

namespace Crescent {

class AudioSource : public Component {
public:
    AudioSource();
    ~AudioSource() override;

    COMPONENT_TYPE(AudioSource)

    const std::string& getFilePath() const { return m_FilePath; }
    void setFilePath(const std::string& path);

    float getVolume() const { return m_Volume; }
    void setVolume(float value);

    float getPitch() const { return m_Pitch; }
    void setPitch(float value);

    bool isLooping() const { return m_Looping; }
    void setLooping(bool loop);

    bool getPlayOnStart() const { return m_PlayOnStart; }
    void setPlayOnStart(bool value) { m_PlayOnStart = value; }

    bool isSpatial() const { return m_Spatial; }
    void setSpatial(bool value);

    bool isStreaming() const { return m_Stream; }
    void setStreaming(bool value);

    float getMinDistance() const { return m_MinDistance; }
    void setMinDistance(float value);

    float getMaxDistance() const { return m_MaxDistance; }
    void setMaxDistance(float value);

    float getRolloff() const { return m_Rolloff; }
    void setRolloff(float value);

    bool isPlaying() const;
    void play();
    void stop();

    void OnCreate() override;
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnDisable() override;
    void OnDestroy() override;

private:
    bool loadSound();
    void unloadSound();
    void applySettings();
    void updateSpatial();

    std::string m_FilePath;
    float m_Volume = 1.0f;
    float m_Pitch = 1.0f;
    bool m_Looping = false;
    bool m_PlayOnStart = false;
    bool m_Spatial = true;
    bool m_Stream = false;
    float m_MinDistance = 1.0f;
    float m_MaxDistance = 50.0f;
    float m_Rolloff = 1.0f;

    ma_sound* m_Sound = nullptr;
    bool m_Loaded = false;
};

} // namespace Crescent
