#pragma once

#include <algorithm>

namespace Crescent {

class FramePacer {
public:
    struct Result {
        float scaledDelta = 0.0f;
        float fixedStep = 0.0f;
        int fixedSteps = 0;
        float alpha = 0.0f;
    };

    float prepareDelta(float unscaledDelta) {
        float clamped = std::clamp(unscaledDelta, 0.0f, m_maxDelta);
        if (m_smoothing > 0.0f) {
            if (m_smoothedDelta == 0.0f) {
                m_smoothedDelta = clamped;
            } else {
                m_smoothedDelta += (clamped - m_smoothedDelta) * m_smoothing;
            }
            clamped = m_smoothedDelta;
        }
        return clamped;
    }

    Result advance(float scaledDelta, float fixedStep) {
        Result result;
        result.scaledDelta = std::max(0.0f, scaledDelta);
        if (fixedStep <= 0.0f) {
            return result;
        }
        result.fixedStep = fixedStep;
        m_accumulator += result.scaledDelta;
        float maxAccumulator = result.fixedStep * m_maxAccumulatorMultiplier;
        if (m_accumulator > maxAccumulator) {
            m_accumulator = maxAccumulator;
        }
        int steps = 0;
        while (m_accumulator >= result.fixedStep && steps < m_maxSteps) {
            m_accumulator -= result.fixedStep;
            steps++;
        }
        result.fixedSteps = steps;
        result.alpha = result.fixedStep > 0.0f ? (m_accumulator / result.fixedStep) : 0.0f;
        return result;
    }

    void reset() {
        m_accumulator = 0.0f;
        m_smoothedDelta = 0.0f;
    }

    void setMaxDelta(float maxDelta) { m_maxDelta = std::max(0.0f, maxDelta); }
    void setMaxSteps(int maxSteps) { m_maxSteps = std::max(1, maxSteps); }
    void setMaxAccumulatorMultiplier(float multiplier) {
        m_maxAccumulatorMultiplier = std::max(1.0f, multiplier);
    }
    void setSmoothing(float smoothing) { m_smoothing = std::clamp(smoothing, 0.0f, 1.0f); }

private:
    float m_accumulator = 0.0f;
    float m_maxDelta = 0.1f;
    int m_maxSteps = 8;
    float m_maxAccumulatorMultiplier = 4.0f;
    float m_smoothing = 0.0f;
    float m_smoothedDelta = 0.0f;
};

} // namespace Crescent
