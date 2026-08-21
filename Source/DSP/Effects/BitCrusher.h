#pragma once

#include "AudioEffect.h"
#include <cmath>
#include <algorithm>
#include <cstdint>

/**
 * BitCrusher
 * ビットリダクション、ダウンサンプリング、時間軸ジッターを内包したグリッチモジュール。
 *
 * RT安全設計:
 *   std::mt19937 / std::random_device はシステムコールやメモリ確保を行う可能性があるため、
 *   オーディオスレッドでは使用不可。xorshift32 による決定論的PRNGに置換。
 */
class BitCrusher final : public AudioEffect
{
public:
    BitCrusher() = default;
    ~BitCrusher() override = default;

    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        this->currentSampleRate = sampleRate;
        reset();
    }

    void reset() noexcept override
    {
        lastSample[0] = 0.0f;
        lastSample[1] = 0.0f;
        holdCounter[0] = 0;
        holdCounter[1] = 0;
    }

    void process(juce::AudioBuffer<float>& buffer) noexcept override
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        const float bits = currentBits;
        const float baseDownsample = currentDownsample;
        const float jitter = currentJitter;
        const float mix = currentMix;

        const float quantScale = std::pow(2.0f, bits - 1.0f);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            if (ch >= 2) break;

            float* channelData = buffer.getWritePointer(ch);
            float lastSmp = lastSample[ch];
            int count = holdCounter[ch];

            for (int s = 0; s < numSamples; ++s)
            {
                float input = channelData[s];
                float processed = input;

                float noiseComponent = nextRandomBipolar() * jitter * (baseDownsample * 0.5f);
                int dynamicFactor = std::max(1, static_cast<int>(std::round(baseDownsample + noiseComponent)));

                if (count % dynamicFactor == 0)
                {
                    if (bits < 24.0f)
                    {
                        processed = std::round(processed * quantScale) / quantScale;
                    }
                    lastSmp = processed;
                }
                else
                {
                    processed = lastSmp;
                }

                count++;

                channelData[s] = (input * (1.0f - mix)) + (processed * mix);
            }

            lastSample[ch] = lastSmp;
            holdCounter[ch] = count % 96000;
        }
    }

    juce::String getName() const override { return "Bitcrusher"; }
    TargetRoute getTargetRoute() const noexcept override { return route; }
    void setTargetRoute(TargetRoute newRoute) noexcept override { route = newRoute; }

    bool isActive() const noexcept override { return activeState; }
    void setActive(bool shouldBeActive) noexcept override { activeState = shouldBeActive; }

    void setMix(float newMix) noexcept override { currentMix = juce::jlimit(0.0f, 1.0f, newMix); }
    float getMix() const noexcept override { return currentMix; }

    void setBits(float newBits) noexcept { currentBits = juce::jlimit(2.0f, 24.0f, newBits); }
    void setDownsample(float newDownsample) noexcept { currentDownsample = juce::jlimit(1.0f, 32.0f, newDownsample); }
    void setJitter(float newJitter) noexcept { currentJitter = juce::jlimit(0.0f, 1.0f, newJitter); }

    float getIndexedParameter(int index) const noexcept override { return 0.0f; }
    void setIndexedParameter(int index, float value) noexcept override
    {
        if (index == 0)      setBits(value);
        else if (index == 1) setDownsample(value);
        else if (index == 2) setMix(value);
        else if (index == 3) setJitter(value);
    }

private:
    double currentSampleRate = 44100.0;
    TargetRoute route = TargetRoute::FullMix;
    bool activeState = false;

    float lastSample[2] = { 0.0f, 0.0f };
    int holdCounter[2] = { 0, 0 };

    // RT安全な xorshift32 PRNG（メモリ確保・システムコール一切なし）
    uint32_t rngState = 0x12345678u;

    float nextRandomBipolar() noexcept
    {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        // [0, 1) → [-1, 1)
        return (static_cast<float>(rngState) / static_cast<float>(0xFFFFFFFFu)) * 2.0f - 1.0f;
    }

    float currentBits = 8.0f;
    float currentDownsample = 4.0f;
    float currentMix = 0.3f;
    float currentJitter = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BitCrusher)
};