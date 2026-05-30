#pragma once

#include "AudioEffect.h"
#include <random>
#include <algorithm>

/**
 * NoiseGenerator
 * 4種類の高精度ノイズ（White, Pink, Brown, Blue）を内包し、
 * 点灯式ラジオボタンUIと完全連動する打楽器特化型ノイズ発振器。
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
        lastWhiteSample = 0.0f;
        brownAccumulator = 0.0f;
    }

    void process(juce::AudioBuffer<float>& buffer) noexcept override
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();
        const float mix = currentMix;

        const float gainLinear = std::pow(10.0f, currentGainDb / 20.0f);
        const float decayCoef = std::exp(-1.0f / (decayMs * 0.001f * static_cast<float>(currentSampleRate)));

        for (int ch = 0; ch < numChannels; ++ch)
        {
            if (ch >= 2) break;

            float* data = buffer.getWritePointer(ch);
            float envState = envelope;

            for (int i = 0; i < numSamples; ++i)
            {
                float input = data[i];

                // 💥 選択されたノイズタイプ（0=White, 1=Pink, 2=Brown, 3=Blue）に応じて数理動的切り替え
                float noise = 0.0f;
                if (currentNoiseType == 0)      noise = generateWhite();
                else if (currentNoiseType == 1) noise = generatePink();
                else if (currentNoiseType == 2) noise = generateBrown();
                else if (currentNoiseType == 3) noise = generateBlue();

                float wetNoise = noise * envState * gainLinear;
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
    void setNoiseType(int type) noexcept { currentNoiseType = juce::jlimit(0, 3, type); }
    void setGainDb(float gainDb) noexcept { currentGainDb = juce::jlimit(-60.0f, 0.0f, gainDb); }

    void setIndexedParameter(int index, float value) noexcept override
    {
        if (index == 0)      setDecay(value);
        else if (index == 1) setMix(value);
        else if (index == 2) setNoiseType(static_cast<int>(value));
        else if (index == 3) setGainDb(value);
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

    float generateBrown()
    {
        float white = generateWhite();
        // 1次リーキー積分による赤色化方程式
        brownAccumulator = (brownAccumulator + (0.02f * white)) / 1.02f;
        return brownAccumulator * 3.5f; // 聴感上の音量補正
    }

    float generateBlue()
    {
        float white = generateWhite();
        // 1次差分による青色化方程式
        float blue = white - lastWhiteSample;
        lastWhiteSample = white;
        return blue * 0.5f;
    }

    double currentSampleRate = 44100.0;
    std::random_device rd;
    std::mt19937 gen;

    TargetRoute route = TargetRoute::Transient;
    bool activeState = false;

    float envelope = 0.0f;
    float decayMs = 100.0f;
    float currentMix = 0.3f;
    float currentGainDb = 0.0f;
    int currentNoiseType = 0;

    float lastWhiteSample = 0.0f;
    float brownAccumulator = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoiseGenerator)
};