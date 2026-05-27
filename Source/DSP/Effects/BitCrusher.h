#pragma once

#include "AudioEffect.h"
#include <cmath>
#include <algorithm>

/**
 * BitCrusher
 * ゼロレイテンシー・高精度ビットクラッシャー。
 * ビットリダクションとダウンサンプリングのデジタルグリッチ数理をカプセル化。
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
        const int downsampleFactor = std::max(1, static_cast<int> (currentDownsample));
        const float mix = currentMix;

        // クオンタイズ用の解像度スケールを事前算出
        // 例: 8bit なら 2^7 = 128
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

                // 1. サンプルレート・リダクション（ダウンサンプリング・ホールド）
                if (count % downsampleFactor == 0)
                {
                    // 2. ビット深度リダクション（振幅の階段状クオンタイズ）
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

                // ドライ/ウェットの線形クロスフェード
                channelData[s] = (input * (1.0f - mix)) + (processed * mix);
            }

            lastSample[ch] = lastSmp;
            holdCounter[ch] = count % 96000; // カウンターのオーバーフロー防止マージン
        }
    }

    juce::String getName() const override { return "Bitcrusher"; }

    TargetRoute getTargetRoute() const noexcept override { return route; }
    void setTargetRoute(TargetRoute newRoute) noexcept override { route = newRoute; }

    void setBits(float newBits) noexcept { currentBits = juce::jlimit(2.0f, 24.0f, newBits); }
    void setDownsample(float newDownsample) noexcept { currentDownsample = juce::jlimit(1.0f, 32.0f, newDownsample); }
    void setMix(float newMix) noexcept { currentMix = juce::jlimit(0.0f, 1.0f, newMix); }

private:
    double currentSampleRate = 44100.0;
    TargetRoute route = TargetRoute::FullMix;

    float lastSample[2] = { 0.0f, 0.0f };
    int holdCounter[2] = { 0, 0 };

    float currentBits = 8.0f;
    float currentDownsample = 4.0f;
    float currentMix = 0.3f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BitCrusher)
};