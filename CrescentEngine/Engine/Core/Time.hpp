#pragma once

#include <algorithm>
#include <cstdint>

namespace Crescent {

class Time {
public:
    static void reset() {
        s_DeltaTime = 0.0f;
        s_UnscaledDeltaTime = 0.0f;
        s_Time = 0.0f;
        s_UnscaledTime = 0.0f;
        s_FrameCount = 0;
    }

    static void update(float unscaledDeltaTime) {
        s_UnscaledDeltaTime = std::max(0.0f, unscaledDeltaTime);
        float scale = s_Paused ? 0.0f : s_TimeScale;
        s_DeltaTime = s_UnscaledDeltaTime * scale;
        s_UnscaledTime += s_UnscaledDeltaTime;
        s_Time += s_DeltaTime;
        s_FrameCount++;
    }

    static float deltaTime() { return s_DeltaTime; }
    static float unscaledDeltaTime() { return s_UnscaledDeltaTime; }

    static float time() { return s_Time; }
    static float unscaledTime() { return s_UnscaledTime; }

    static float fixedDeltaTime() { return s_FixedDeltaTime; }
    static void setFixedDeltaTime(float deltaTime) {
        s_FixedDeltaTime = std::max(0.0001f, deltaTime);
    }

    static float timeScale() { return s_TimeScale; }
    static void setTimeScale(float scale) {
        s_TimeScale = std::max(0.0f, scale);
    }

    static bool isPaused() { return s_Paused; }
    static void setPaused(bool paused) { s_Paused = paused; }

    static uint64_t frameCount() { return s_FrameCount; }

private:
    inline static float s_DeltaTime = 0.0f;
    inline static float s_UnscaledDeltaTime = 0.0f;
    inline static float s_Time = 0.0f;
    inline static float s_UnscaledTime = 0.0f;
    inline static float s_FixedDeltaTime = 1.0f / 60.0f;
    inline static float s_TimeScale = 1.0f;
    inline static bool s_Paused = false;
    inline static uint64_t s_FrameCount = 0;
};

} // namespace Crescent
