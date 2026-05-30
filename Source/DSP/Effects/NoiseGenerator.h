#pragma once

#include "AudioEffect.h"
#include <random>
#include <algorithm>

/**
 * NoiseGenerator
 * 💥【高精度化：独立音量ノブ追加】オーバーライド指定不備を修正し、独立した Gain 制御数理を敷設。
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

        // 💥独立した出力ゲインのリニア変換（dB換算）
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
                float noise = (isPink ? generatePink() : generateWhite());

                // トリガーエンベロープと音量ゲインを重畳してノイズ成分を成形
                float wetNoise = noise * envState * gainLinear;
                envState *= decayCoef;

                data[i] = (input * (1.0f - mix)) + ((input + wetNoise) * mix);
            }

            if (ch == 0)
                envelope = envState;
        }
    }

    // 💥【修正完了】基底クラスの純粋仮想関数ではないため override キーワードを除去し完全開通
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
    void setGainDb(float gainDb) noexcept { currentGainDb = juce::jlimit(-60.0f, 0.0f, gainDb); } // 新設

    void setIndexedParameter(int index, float value) noexcept override
    {
        if (index == 0)      setDecay(value);
        else if (index == 1) setMix(value);
        else if (index == 2) setPink(value > 0.5f);
        else if (index == 3) setGainDb(value); // 新設
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
    float currentGainDb = 0.0f; // 新設
    bool isPink = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoiseGenerator)
};