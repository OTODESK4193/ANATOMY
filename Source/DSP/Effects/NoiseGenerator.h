#pragma once

#include "AudioEffect.h"
#include <random>
#include <algorithm>

/**
 * NoiseGenerator
 * ドラムのトリガーに同期した Env Decay の高精度化、および Mix 規格の追加。
 */
class NoiseGenerator final : public AudioEffect
{
public:
    NoiseGenerator() : rd(), gen(rd()) {}
    ~NoiseGenerator() override = default;

    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        this->currentSampleRate = sampleRate;
        reset();
    }

    void reset() noexcept override
    {
        envelope = 0.0f;
    }

    void process(juce::AudioBuffer<float>& buffer) noexcept override
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();
        const float mix = currentMix;

        const float decayCoef = std::exp(-1.0f / (decayMs * 0.001f * static_cast<float>(currentSampleRate)));

        for (int ch = 0; ch < numChannels; ++ch)
        {
            if (ch >= 2) break;

            float* data = buffer.getWritePointer(ch);
            float envState = envelope;

            for (int i = 0; i < numSamples; ++i)
            {
                float input = data[i];
                float noise = (isPink ? generatePink() : generateWhite());

                float wetNoise = noise * envState;
                envState *= decayCoef;

                data[i] = (input * (1.0f - mix)) + ((input + wetNoise) * mix);
            }

            if (ch == 0)
                envelope = envState;
        }
    }

    void trigger() noexcept { envelope = 1.0f; }

    juce::String getName() const override { return "Noise Generator"; }
    TargetRoute getTargetRoute() const noexcept override { return route; }
    void setTargetRoute(TargetRoute r) noexcept override { route = r; }

    bool isActive() const noexcept override { return activeState; }
    void setActive(bool shouldBeActive) noexcept override { activeState = shouldBeActive; }

    void setMix(float newMix) noexcept override { currentMix = juce::jlimit(0.0f, 1.0f, newMix); }
    float getMix() const noexcept override { return currentMix; }

    void setDecay(float ms) noexcept { decayMs = std::max(1.0f, ms); }
    void setPink(bool pink) noexcept { isPink = pink; }

    void setIndexedParameter(int index, float value) noexcept override
    {
        if (index == 0)      setDecay(value);
        else if (index == 1) setMix(value);
        else if (index == 2) setPink(value > 0.5f);
    }

private:
    float generateWhite()
    {
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        return dist(gen);
    }

    float generatePink()
    {
        static float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
        float white = generateWhite();
        b0 = 0.998f * b0 + white * 0.0555179f;
        b1 = 0.993f * b1 + white * 0.0750759f;
        b2 = 0.969f * b2 + white * 0.1538520f;
        return (b0 + b1 + b2 + white * 0.05362f) * 0.25f;
    }

    double currentSampleRate = 44100.0;
    std::random_device rd;
    std::mt19937 gen;

    TargetRoute route = TargetRoute::Transient;
    bool activeState = false;

    float envelope = 0.0f;
    float decayMs = 100.0f;
    float currentMix = 0.3f;
    bool isPink = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoiseGenerator)
};