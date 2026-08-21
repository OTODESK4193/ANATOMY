#pragma once

#include "AudioEffect.h"
#include <cmath>
#include <algorithm>

/**
 * TransientShaper (High-Precision Dual-Branch Dynamic Architecture)
 * 1. ステレオリンク高精度デュアル・エンベロープ追従（Fast / Slow）
 * 2. 差分エネルギー抽出＋アンチエイリアシング・ゲインスムージング（低域歪みの根絶）
 * 3. アタックブースト時のソフトサチュレーション・プロテクション（パンチ感を最大化）
 */
class TransientShaper final : public AudioEffect
{
public:
    TransientShaper() = default;
    ~TransientShaper() override = default;

    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        sr = (float)sampleRate;
        // 追従係数（高精度 1 極フィルター係数）
        fastAtk = onePole(0.5f);    // 0.5ms (超高速過渡スナップ検出)
        fastRel = onePole(25.0f);   // 25ms
        slowAtk = onePole(15.0f);   // 15ms
        slowRel = onePole(180.0f);  // 180ms
        gainSmoothCoeff = onePole(0.8f); // 0.8ms (低域クリック歪み防止のスムージング)

        reset();
    }

    void reset() noexcept override
    {
        fastEnv = 0.0f;
        slowEnv = 0.0f;
        smoothedGain = 1.0f;
    }

    void process(juce::AudioBuffer<float>& buffer) noexcept override
    {
        const int numCh = juce::jmin(2, buffer.getNumChannels());
        const int n = buffer.getNumSamples();
        const float mix = currentMix;
        const float atkAmt = currentAttack;   // -1.0 .. +1.0
        const float susAmt = currentSustain;  // -1.0 .. +1.0

        if (std::abs(atkAmt) < 0.001f && std::abs(susAmt) < 0.001f)
            return;

        float fe = fastEnv;
        float se = slowEnv;
        float sg = smoothedGain;

        const float* r0 = buffer.getReadPointer(0);
        const float* r1 = (numCh > 1) ? buffer.getReadPointer(1) : r0;
        float* w0 = buffer.getWritePointer(0);
        float* w1 = (numCh > 1) ? buffer.getWritePointer(1) : nullptr;

        for (int s = 0; s < n; ++s)
        {
            // ステレオリンクによる左右音像のふらつき防止
            const float x0 = r0[s];
            const float x1 = r1[s];
            const float rect = std::max(std::abs(x0), std::abs(x1));

            // デュアル・エンベロープ追従
            fe += (rect > fe ? fastAtk : fastRel) * (rect - fe);
            se += (rect > se ? slowAtk : slowRel) * (rect - se);

            // 過渡（Attack）と余韻（Sustain）のエネルギー分離
            const float delta = fe - se;
            float targetGainDb = 0.0f;

            if (delta > 0.0f && se > 1.0e-5f)
            {
                // アタック区間: 立ち上がり比率から滑らかにゲイン計算
                float attackRatio = delta / (se + 0.05f);
                targetGainDb = atkAmt * 18.0f * std::min(2.0f, attackRatio);
            }
            else if (se > 1.0e-5f)
            {
                // サステイン区間: 余韻比率から滑らかにゲイン計算
                float sustainRatio = se / (fe + 0.05f);
                targetGainDb = susAmt * 12.0f * std::min(2.0f, sustainRatio);
            }

            targetGainDb = juce::jlimit(-24.0f, 24.0f, targetGainDb);
            const float targetLinear = std::pow(10.0f, targetGainDb / 20.0f);

            // ゲインのスムージング（チャタリング歪みを完全排除）
            sg += gainSmoothCoeff * (targetLinear - sg);

            // チャンネル毎に出力計算 + ソフトサチュレーション
            auto processSampleOut = [sg, mix, atkAmt](float x) noexcept -> float
            {
                float processed = x * sg;
                // アタックブースト時のアナログライクなソフトクリッピング保護
                if (atkAmt > 0.0f && std::abs(processed) > 0.8f)
                {
                    float sign = (processed >= 0.0f) ? 1.0f : -1.0f;
                    float absP = std::abs(processed);
                    processed = sign * (0.8f + 0.2f * std::tanh((absP - 0.8f) / 0.2f));
                }
                return x * (1.0f - mix) + processed * mix;
            };

            w0[s] = processSampleOut(x0);
            if (w1 != nullptr) w1[s] = processSampleOut(x1);
        }

        fastEnv = fe;
        slowEnv = se;
        smoothedGain = sg;
    }

    juce::String getName() const override { return "Transient Shaper"; }
    TargetRoute getTargetRoute() const noexcept override { return route; }
    void setTargetRoute(TargetRoute r) noexcept override { route = r; }

    bool isActive() const noexcept override { return activeState; }
    void setActive(bool b) noexcept override { activeState = b; }

    void setMix(float m) noexcept override { currentMix = juce::jlimit(0.0f, 1.0f, m); }
    float getMix() const noexcept override { return currentMix; }

    void setAttack(float a) noexcept { currentAttack = juce::jlimit(-1.0f, 1.0f, a); }
    void setSustain(float s) noexcept { currentSustain = juce::jlimit(-1.0f, 1.0f, s); }

    void setIndexedParameter(int index, float value) noexcept override
    {
        if      (index == 0) setAttack(value);
        else if (index == 1) setMix(value);
        else if (index == 2) setSustain(value);
    }
    float getIndexedParameter(int index) const noexcept override
    {
        switch (index) {
        case 0: return currentAttack; case 1: return currentMix; case 2: return currentSustain; default: return 0.0f;
        }
    }

private:
    float onePole(float ms) const noexcept
    {
        return 1.0f - std::exp(-1.0f / (juce::jmax(0.01f, ms) * 0.001f * sr));
    }

    float sr = 44100.0f;
    TargetRoute route = TargetRoute::FullMix;
    bool activeState = false;

    float fastAtk = 0.5f, fastRel = 0.1f, slowAtk = 0.2f, slowRel = 0.05f;
    float gainSmoothCoeff = 0.1f;
    float fastEnv = 0.0f;
    float slowEnv = 0.0f;
    float smoothedGain = 1.0f;

    float currentMix = 1.0f;
    float currentAttack = 0.0f;
    float currentSustain = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransientShaper)
};
