#pragma once

#include "AudioEffect.h"
#include <cmath>
#include <algorithm>

/**
 * Limiter
 * 核心制約5（0ms絶対先頭原点）を完璧に死守する「0ms遅延型・超高速アトミック・ハードクリッパー」。
 */
class Limiter final : public AudioEffect
{
public:
    Limiter() = default;
    ~Limiter() override = default;

    void prepare(double sr, int) override
    {
        sampleRate = sr;
    }

    void reset() noexcept override {}

    void process(juce::AudioBuffer<float>& buffer) noexcept override
    {
        const float ceilingLinear = std::pow(10.0f, ceilingDb / 20.0f);
        const float mix = currentMix;

        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* data = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
            {
                const float input = data[i];
                float processed = juce::jlimit(-ceilingLinear, ceilingLinear, input);

                data[i] = (input * (1.0f - mix)) + (processed * mix);
            }
        }
    }

    juce::String getName() const override { return "Limiter"; }
    TargetRoute getTargetRoute() const noexcept override { return route; }
    void setTargetRoute(TargetRoute r) noexcept override { route = r; }

    bool isActive() const noexcept override { return activeState; }
    void setActive(bool shouldBeActive) noexcept override { activeState = shouldBeActive; }

    void setMix(float newMix) noexcept override { currentMix = juce::jlimit(0.0f, 1.0f, newMix); }
    float getMix() const noexcept override { return currentMix; }

    void setCeiling(float db) noexcept { ceilingDb = db; }

    void setIndexedParameter(int index, float value) noexcept override
    {
        if (index == 0)      setCeiling(value);
        else if (index == 1) setMix(value);
    }

private:
    double sampleRate = 44100.0;
    float ceilingDb = -0.1f;
    float currentMix = 1.0f;

    TargetRoute route = TargetRoute::FullMix;
    bool activeState = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Limiter)
};