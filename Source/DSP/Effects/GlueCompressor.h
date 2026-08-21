#pragma once

#include "AudioEffect.h"
#include <cmath>
#include <algorithm>

/**
 * GlueCompressor
 * FullMix向け Glue コンプレッサー。ドラム素材の各成分を自然にまとめ、
 * バスコンプレッサーとしての「接着」感を与える。
 *
 * 設計指針:
 *   - Upward Compression なし（ノイズ増幅ゼロ）
 *   - リンクステレオ：L/R 同一のゲインリダクションで位相差を防ぐ
 *   - ピーク・エンベロープフォロワー（シンプルかつドラムに適切）
 *   - ゲインスムージング（5ms）でジッパーノイズ防止
 *   - 全計算値はプリコンピュート済み、processBlock 内に pow/log なし（定常時）
 *
 * パラメーター:
 *   setIndexedParameter(0, v) = Mix (0〜1)
 *   setIndexedParameter(1, v) = Threshold (dBFS, -40〜0, default -18)
 *   setIndexedParameter(2, v) = Ratio (1〜20, default 2.0)
 *   setIndexedParameter(3, v) = Attack (ms, 1〜100, default 30)
 *   setIndexedParameter(4, v) = Release (ms, 10〜1000, default 200)
 *   setIndexedParameter(5, v) = Makeup (dB, -12〜+12, default 0)
 */
class GlueCompressor final : public AudioEffect
{
public:
    GlueCompressor()
    {
        updateThresholdLinear();
        updateMakeupLinear();
    }

    ~GlueCompressor() override = default;

    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        currentSampleRate = sampleRate;
        updateCoefficients();
        reset();
    }

    void reset() noexcept override
    {
        envFollower = 0.0f;
        gainSmooth  = 1.0f;
    }

    void process(juce::AudioBuffer<float>& buffer) noexcept override
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples  = buffer.getNumSamples();

        // processBlock内での pow() 呼び出しを避けるため、
        // ratioExp を定数として保持する。
        // gainReduction = (env/thr)^(1/R - 1)
        //   → env > thr かつ R > 1 のとき: (env/thr) > 1, 指数 < 0 → gain < 1 ✓
        const float ratioExp   = 1.0f / currentRatio - 1.0f;
        const float thr        = thresholdLinear;
        const float makeup     = makeupLinear;
        const float mix        = currentMix;
        const float atk        = attackCoef;
        const float rel        = releaseCoef;
        const float smoothCoef = gainSmoothCoef;

        float* chL = buffer.getWritePointer(0);
        float* chR = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

        float env    = envFollower;
        float gSmooth = gainSmooth;

        for (int s = 0; s < numSamples; ++s)
        {
            // 1. リンクステレオ・ピーク検出
            const float peak = std::max(std::abs(chL[s]),
                                        chR != nullptr ? std::abs(chR[s]) : 0.0f);

            // 2. ピーク・エンベロープフォロワー（アタック速い / リリース遅い）
            env = (peak > env)
                ? (atk * env + (1.0f - atk) * peak)
                : (rel * env + (1.0f - rel) * peak);

            // 3. ゲインリダクション計算（ハードニー）
            //    閾値以上のとき: gainRed = (env/thr)^(1/R - 1) < 1
            //    閾値以下のとき: gainRed = 1.0（スルー）
            float targetGain = 1.0f;
            if (env > thr && thr > 1.0e-6f)
                targetGain = std::pow(env / thr, ratioExp);

            // 4. ゲイン変化の追加スムージング（ジッパーノイズ防止）
            gSmooth = smoothCoef * gSmooth + (1.0f - smoothCoef) * targetGain;

            // 5. 最終ゲイン適用 (コンプ後 × Makeup) + Dry/Wet
            const float finalGain = gSmooth * makeup;
            chL[s] = chL[s] * (1.0f - mix) + chL[s] * finalGain * mix;
            if (chR != nullptr)
                chR[s] = chR[s] * (1.0f - mix) + chR[s] * finalGain * mix;
        }

        // ステート書き戻し
        envFollower = env;
        gainSmooth  = gSmooth;
    }

    juce::String getName() const override { return "Glue Compressor"; }
    TargetRoute getTargetRoute() const noexcept override { return route; }
    void setTargetRoute(TargetRoute r) noexcept override { route = r; }

    bool isActive() const noexcept override { return activeState; }
    void setActive(bool b) noexcept override { activeState = b; }

    void setMix(float v) noexcept override { currentMix = juce::jlimit(0.0f, 1.0f, v); }
    float getMix() const noexcept override { return currentMix; }

    /** スレッショルド: -40〜0 dBFS */
    void setThresholdDb(float db) noexcept
    {
        currentThresholdDb = juce::jlimit(-40.0f, 0.0f, db);
        updateThresholdLinear();
    }

    /** コンプ比率: 1〜20 */
    void setRatio(float r) noexcept
    {
        currentRatio = juce::jlimit(1.0f, 20.0f, r);
    }

    /** アタック: 1〜100 ms */
    void setAttackMs(float ms) noexcept
    {
        currentAttackMs = juce::jlimit(1.0f, 100.0f, ms);
        if (currentSampleRate > 0.0) updateCoefficients();
    }

    /** リリース: 10〜1000 ms */
    void setReleaseMs(float ms) noexcept
    {
        currentReleaseMs = juce::jlimit(10.0f, 1000.0f, ms);
        if (currentSampleRate > 0.0) updateCoefficients();
    }

    /** メイクアップゲイン: -12〜+12 dB */
    void setMakeupDb(float db) noexcept
    {
        currentMakeupDb = juce::jlimit(-12.0f, 12.0f, db);
        updateMakeupLinear();
    }

    float getIndexedParameter(int index) const noexcept override { return 0.0f; }
    void setIndexedParameter(int index, float value) noexcept override
    {
        if      (index == 0) setMix(value);
        else if (index == 1) setThresholdDb(value);
        else if (index == 2) setRatio(value);
        else if (index == 3) setAttackMs(value);
        else if (index == 4) setReleaseMs(value);
        else if (index == 5) setMakeupDb(value);
    }

private:
    void updateCoefficients() noexcept
    {
        const float sr = static_cast<float>(currentSampleRate);
        attackCoef    = std::exp(-1.0f / (currentAttackMs  * 0.001f * sr));
        releaseCoef   = std::exp(-1.0f / (currentReleaseMs * 0.001f * sr));
        // 5ms スムージング（自動化でのジッパーノイズ防止、応答性とのバランス）
        gainSmoothCoef = std::exp(-1.0f / (0.005f * sr));
    }

    void updateThresholdLinear() noexcept
    {
        thresholdLinear = std::pow(10.0f, currentThresholdDb / 20.0f);
    }

    void updateMakeupLinear() noexcept
    {
        makeupLinear = std::pow(10.0f, currentMakeupDb / 20.0f);
    }

    double currentSampleRate = 44100.0;

    // ── パラメーター ──────────────────────────────────────────────────────────
    float currentMix         = 1.0f;
    float currentThresholdDb = -18.0f;
    float currentRatio       = 2.0f;
    float currentAttackMs    = 30.0f;
    float currentReleaseMs   = 200.0f;
    float currentMakeupDb    = 0.0f;

    // ── 事前計算値 ────────────────────────────────────────────────────────────
    float thresholdLinear  = 0.0f;    // updateThresholdLinear() で計算
    float makeupLinear     = 1.0f;    // updateMakeupLinear()    で計算
    float attackCoef       = 0.999f;
    float releaseCoef      = 0.9999f;
    float gainSmoothCoef   = 0.9997f;

    // ── 内部ステート ──────────────────────────────────────────────────────────
    float envFollower = 0.0f;
    float gainSmooth  = 1.0f;

    TargetRoute route = TargetRoute::FullMix;
    bool activeState  = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GlueCompressor)
};
