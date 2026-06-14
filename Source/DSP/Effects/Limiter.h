#pragma once

#include "AudioEffect.h"
#include <cmath>
#include <algorithm>

/**
 * Limiter — 0遅延フィードフォワード・ピークリミッター
 *
 * 旧実装はハードクリッパー（jlimit）のみだったが、以下に改良:
 *   - フィードフォワード方式: ピーク検出 → ゲイン計算 → エンベロープ平滑化 → 乗算
 *   - 超高速アタック（~0.1ms）で瞬時に制限しつつ、滑らかなリリースでポンピングを抑制
 *   - 最終段にハードクリップ（安全弁）を残し、デジタルオーバー完全排除
 *   - 0サンプル遅延を維持（ルックアヘッドなし = トランジェントタイミング不変）
 */
class Limiter final : public AudioEffect
{
public:
    Limiter() = default;
    ~Limiter() override = default;

    void prepare(double sr, int) override
    {
        sampleRate = sr;
        // アタック ~0.1ms / リリース ~50ms のエンベロープ係数を計算
        updateCoefficients();
    }

    void reset() noexcept override
    {
        envGain = 1.0f;
    }

    void process(juce::AudioBuffer<float>& buffer) noexcept override
    {
        const float ceiling = std::pow(10.0f, ceilingDb / 20.0f);
        const float invCeiling = (ceiling > 1e-12f) ? (1.0f / ceiling) : 1e12f;
        const float mix = currentMix;
        const float att = attackCoeff;
        const float rel = releaseCoeff;

        const int numChannels = buffer.getNumChannels();
        const int numSamples  = buffer.getNumSamples();

        for (int i = 0; i < numSamples; ++i)
        {
            // ステレオリンク: 全チャンネルのピークを検出
            float peak = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float absSample = std::abs(buffer.getReadPointer(ch)[i]);
                if (absSample > peak) peak = absSample;
            }

            // フィードフォワード: 必要なゲインリダクションを計算
            float desiredGain = (peak > ceiling) ? (ceiling / peak) : 1.0f;

            // エンベロープフォロワー（ゲインが下がる=アタック / 上がる=リリース）
            if (desiredGain < envGain)
                envGain = att * envGain + (1.0f - att) * desiredGain;  // 高速アタック
            else
                envGain = rel * envGain + (1.0f - rel) * desiredGain;  // 緩やかなリリース

            // ゲイン適用 + 安全弁ハードクリップ
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* data = buffer.getWritePointer(ch);
                const float input = data[i];
                float limited = juce::jlimit(-ceiling, ceiling, input * envGain);
                data[i] = input * (1.0f - mix) + limited * mix;
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
    void updateCoefficients() noexcept
    {
        // アタック ~0.1ms: 超高速でピークを捕捉
        const float attackMs  = 0.1f;
        // リリース ~50ms: 滑らかなゲイン回復
        const float releaseMs = 50.0f;

        attackCoeff  = std::exp(-1.0f / (static_cast<float>(sampleRate) * attackMs  * 0.001f));
        releaseCoeff = std::exp(-1.0f / (static_cast<float>(sampleRate) * releaseMs * 0.001f));
    }

    double sampleRate   = 44100.0;
    float  ceilingDb    = -0.1f;
    float  currentMix   = 1.0f;
    float  envGain      = 1.0f;   // エンベロープフォロワー状態
    float  attackCoeff  = 0.0f;
    float  releaseCoeff = 0.0f;

    TargetRoute route = TargetRoute::FullMix;
    bool activeState = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Limiter)
};