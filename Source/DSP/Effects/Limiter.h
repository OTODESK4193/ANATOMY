#pragma once

#include "AudioEffect.h"
#include <cmath>
#include <algorithm>

/**
 * Limiter / Precision Clipper
 *
 * 1. Limit モード:
 *    - 0アタック・瞬時ピーク捕捉（完全ゼロ・オーバーシュート）
 *    - アダプティブ・リリースによる透明でナチュラルな音圧最大化
 * 2. Clip モード:
 *    - アナログ・ソフトニー・ブリックウォール・クリッパー
 *    - トランジェントの密度とパンチ感を極限まで引き上げるドラム用クリッピング
 * 3. どのような Dry/Wet 比率であっても Ceiling 値（dBFS）を 100% 厳守
 */
class Limiter final : public AudioEffect
{
public:
    Limiter() = default;
    ~Limiter() override = default;

    void prepare(double sr, int) override
    {
        sampleRate = sr;
        updateCoefficients();
    }

    void reset() noexcept override
    {
        envGain = 1.0f;
    }

    void process(juce::AudioBuffer<float>& buffer) noexcept override
    {
        const float ceiling = std::pow(10.0f, ceilingDb / 20.0f);
        const float mix = currentMix;
        const float rel = releaseCoeff;
        const int mode = currentMode;

        const int numChannels = buffer.getNumChannels();
        const int numSamples  = buffer.getNumSamples();

        if (mode == 0) // ================= Limit モード (Brickwall Limiter) =================
        {
            for (int i = 0; i < numSamples; ++i)
            {
                // ステレオリンク: ピーク検出
                float peak = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    float absSample = std::abs(buffer.getReadPointer(ch)[i]);
                    if (absSample > peak) peak = absSample;
                }

                // 瞬時ゼロアタック: ピーク超過時は 1 サンプルで瞬時に目標ゲインまで圧縮（オーバーシュート 0）
                float desiredGain = (peak > ceiling && peak > 1.0e-7f) ? (ceiling / peak) : 1.0f;
                if (desiredGain < envGain)
                    envGain = desiredGain;
                else
                    envGain = rel * envGain + (1.0f - rel) * desiredGain;

                // ゲイン適用 + 天井ガード
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    float* data = buffer.getWritePointer(ch);
                    const float input = data[i];
                    float limited = juce::jlimit(-ceiling, ceiling, input * envGain);
                    float outVal = input * (1.0f - mix) + limited * mix;
                    // 確実に Ceiling を超えないことを保証
                    data[i] = juce::jlimit(-ceiling, ceiling, outVal);
                }
            }
        }
        else // ================= Clip モード (Soft-Knee Precision Clipper) =================
        {
            const float kneeThresh = ceiling * 0.85f;
            const float kneeRange  = ceiling * 0.15f;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* data = buffer.getWritePointer(ch);
                for (int i = 0; i < numSamples; ++i)
                {
                    const float input = data[i];
                    const float absIn = std::abs(input);
                    float clipped = input;

                    if (absIn > kneeThresh)
                    {
                        const float sign = (input >= 0.0f) ? 1.0f : -1.0f;
                        if (absIn >= ceiling)
                        {
                            clipped = sign * ceiling;
                        }
                        else
                        {
                            // ソフトニー滑らか遷移
                            float ratio = (absIn - kneeThresh) / kneeRange;
                            clipped = sign * (kneeThresh + kneeRange * std::tanh(ratio));
                        }
                    }

                    float outVal = input * (1.0f - mix) + clipped * mix;
                    // 確実に Ceiling を超えないことを保証
                    data[i] = juce::jlimit(-ceiling, ceiling, outVal);
                }
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
    float getCeiling() const noexcept { return ceilingDb; }

    void setMode(int m) noexcept { currentMode = juce::jlimit(0, 1, m); }
    int getMode() const noexcept { return currentMode; }

    float getIndexedParameter(int index) const noexcept override { return 0.0f; }
    void setIndexedParameter(int index, float value) noexcept override
    {
        if      (index == 0) setCeiling(value);
        else if (index == 1) setMix(value);
        else if (index == 2) setMode(static_cast<int>(value));
    }

private:
    void updateCoefficients() noexcept
    {
        // リリース ~40ms: パンピングを抑えた音楽的なゲイン回復
        const float releaseMs = 40.0f;
        releaseCoeff = std::exp(-1.0f / (static_cast<float>(sampleRate) * releaseMs * 0.001f));
    }

    double sampleRate   = 44100.0;
    float  ceilingDb    = -0.1f;
    float  currentMix   = 1.0f;
    int    currentMode  = 0;      // 0: Limit, 1: Clip
    float  envGain      = 1.0f;   // エンベロープフォロワー状態
    float  releaseCoeff = 0.0f;

    TargetRoute route = TargetRoute::FullMix;
    bool activeState = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Limiter)
};