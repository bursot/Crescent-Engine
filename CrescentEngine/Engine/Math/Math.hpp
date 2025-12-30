#pragma once

// Central math header - includes all math types

#include "Vector2.hpp"
#include "Vector3.hpp"
#include "Vector4.hpp"
#include "Quaternion.hpp"
#include "Matrix4x4.hpp"

namespace Crescent {
namespace Math {

// Common math constants
constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 6.28318530717958647692f;
constexpr float HALF_PI = 1.57079632679489661923f;
constexpr float DEG_TO_RAD = 0.01745329251994329577f;
constexpr float RAD_TO_DEG = 57.2957795130823208768f;
constexpr float EPSILON = 1e-6f;

// Utility functions
inline float Clamp(float value, float min, float max) {
    return (value < min) ? min : (value > max) ? max : value;
}

inline float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

inline float InverseLerp(float a, float b, float value) {
    return (value - a) / (b - a);
}

inline float Smoothstep(float edge0, float edge1, float x) {
    x = Clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

inline bool Approximately(float a, float b, float epsilon = EPSILON) {
    return std::abs(a - b) < epsilon;
}

inline float Sign(float value) {
    return (value > 0.0f) ? 1.0f : (value < 0.0f) ? -1.0f : 0.0f;
}

inline float Repeat(float t, float length) {
    return Clamp(t - std::floor(t / length) * length, 0.0f, length);
}

inline float PingPong(float t, float length) {
    t = Repeat(t, length * 2.0f);
    return length - std::abs(t - length);
}

inline float DeltaAngle(float current, float target) {
    float delta = Repeat((target - current), 360.0f);
    if (delta > 180.0f)
        delta -= 360.0f;
    return delta;
}

inline float MoveTowards(float current, float target, float maxDelta) {
    if (std::abs(target - current) <= maxDelta)
        return target;
    return current + Sign(target - current) * maxDelta;
}

inline float SmoothDamp(float current, float target, float& currentVelocity, 
                       float smoothTime, float maxSpeed, float deltaTime) {
    smoothTime = std::max(0.0001f, smoothTime);
    float omega = 2.0f / smoothTime;
    float x = omega * deltaTime;
    float exp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    float change = current - target;
    float originalTo = target;
    
    float maxChange = maxSpeed * smoothTime;
    change = Clamp(change, -maxChange, maxChange);
    target = current - change;
    
    float temp = (currentVelocity + omega * change) * deltaTime;
    currentVelocity = (currentVelocity - omega * temp) * exp;
    float output = target + (change + temp) * exp;
    
    if (originalTo - current > 0.0f == output > originalTo) {
        output = originalTo;
        currentVelocity = (output - originalTo) / deltaTime;
    }
    
    return output;
}

} // namespace Math
} // namespace Crescent
