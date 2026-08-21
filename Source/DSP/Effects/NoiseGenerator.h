#pragma once

#include "AudioEffect.h"
#include <random>
#include <algorithm>
#include <cmath>

/**
 * NoiseGenerator
 * 4種類の高精度ノイズ（White, Pink, Brown, Blue）を内包し、
 * 点灯式ラジオボタンUIと完全連動する打楽器特化型ノイズ発振器。
 *
 * バグ修正:
 *   - generatePink() の static 変数を除去 → pinkB[ch][3] メンバー変数化
 *   - envelope / brownAcc / lastWhite / isAttacking を全チャンネル別に修正
 *
 * 追加パラメーター:
 *   - Attack (0〜50 ms): エンベロープの立ち上がり制御
 *   - BpFreq (0〜4000 Hz): Chamberlin SVF バンドパスフィルター (Q=2.0固定)
 *     0 Hz = バイパス
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
        for (int ch = 0; ch < 2; ++ch)
        {
            envelope[ch]      = 0.0f;
            isAttacking[ch]   = false;
            pinkB[ch][0]      = pinkB[ch][1] = pinkB[ch][2] = 0.0f;
            brownAcc[ch]      = 0.0f;
            lastWhite[ch]     = 0.0f;
            svfLow[ch]        = 0.0f;
            svfBand[ch]       = 0.0f;
        }
    }

    void process(juce::AudioBuffer<float>& buffer) noexcept override
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples  = buffer.getNumSamples();
        const float mix       = currentMix;

        const float gainLinear = std::pow(10.0f, currentGainDb / 20.0f);
        const float decayCoef  = std::exp(-1.0f / (decayMs * 0.001f * static_cast<float>(currentSampleRate)));

        // アタック係数: attackMs=0 → 即時立ち上がり (attackCoef=0)
        const float attackCoef = (attackMs > 0.5f)
            ? std::exp(-1.0f / (attackMs * 0.001f * static_cast<float>(currentSampleRate)))
            : 0.0f;

        // バンドパス (Chamberlin SVF) 係数の事前計算
        const bool  bpEnabled = (bpCenterHz > 5.0f);
        // 安定上限 0.45*sr に制限
        const float bpF = bpEnabled
            ? (2.0f * std::sin(juce::MathConstants<float>::pi
                * std::min(bpCenterHz, static_cast<float>(currentSampleRate) * 0.45f)
                / static_cast<float>(currentSampleRate)))
            : 0.0f;
        const float bpDamp = 1.0f / bpQ;   // Q=2.0 → damp=0.5

        for (int ch = 0; ch < numChannels; ++ch)
        {
            if (ch >= 2) break;

            float* data         = buffer.getWritePointer(ch);
            float  envState     = envelope[ch];
            bool   attacking    = isAttacking[ch];
            float  svfLowState  = svfLow[ch];
            float  svfBandState = svfBand[ch];

            for (int i = 0; i < numSamples; ++i)
            {
                const float input = data[i];

                // ── ノイズ生成（チャンネル別ステート） ──────────────────
                float noise = 0.0f;
                if      (currentNoiseType == 0) noise = generateWhite();
                else if (currentNoiseType == 1) noise = generatePinkForCh(ch);
                else if (currentNoiseType == 2) noise = generateBrownForCh(ch);
                else if (currentNoiseType == 3) noise = generateBlueForCh(ch);

                // ── Chamberlin SVF バンドパスフィルター ─────────────────
                if (bpEnabled)
                {
                    const float hp  = noise - svfLowState - bpDamp * svfBandState;
                    svfBandState   += bpF * hp;
                    svfLowState    += bpF * svfBandState;
                    noise           = svfBandState;
                }

                // ── エンベロープ (アタック → ディケイ) ──────────────────
                if (attacking)
                {
                    if (attackCoef < 0.001f)
                    {
                        // 即時立ち上がり
                        envState  = 1.0f;
                        attacking = false;
                    }
                    else
                    {
                        // 目標値 1.0 へ向けてスムーズ収束
                        envState = attackCoef * envState + (1.0f - attackCoef) * 1.0f;
                        if (envState >= 0.999f) { envState = 1.0f; attacking = false; }
                    }
                }
                else
                {
                    envState *= decayCoef;
                }

                const float wetNoise = noise * envState * gainLinear;
                data[i] = (input * (1.0f - mix)) + ((input + wetNoise) * mix);
            }

            envelope[ch]      = envState;
            isAttacking[ch]   = attacking;
            svfLow[ch]        = svfLowState;
            svfBand[ch]       = svfBandState;
        }
    }

    /** MIDIノートオン / レコーディングトリガーで呼ばれる */
    void trigger() noexcept
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            envelope[ch]    = 0.0f;
            isAttacking[ch] = true;
        }
    }

    juce::String getName() const override { return "Noise Generator"; }
    TargetRoute getTargetRoute() const noexcept override { return route; }
    void setTargetRoute(TargetRoute r) noexcept override { route = r; }

    bool isActive() const noexcept override { return activeState; }
    void setActive(bool shouldBeActive) noexcept override { activeState = shouldBeActive; }

    void setMix(float newMix) noexcept override { currentMix = juce::jlimit(0.0f, 1.0f, newMix); }
    float getMix() const noexcept override { return currentMix; }

    void setDecay(float ms) noexcept     { decayMs        = std::max(1.0f, ms); }
    void setNoiseType(int type) noexcept { currentNoiseType = juce::jlimit(0, 3, type); }
    void setGainDb(float gainDb) noexcept { currentGainDb  = juce::jlimit(-60.0f, 0.0f, gainDb); }

    /** アタック 0〜50 ms (0 = 即時) */
    void setAttack(float ms) noexcept    { attackMs       = std::max(0.0f, ms); }

    /** バンドパス中心周波数 0〜4000 Hz (0 = バイパス) */
    void setBpCenterHz(float hz) noexcept { bpCenterHz    = std::max(0.0f, hz); }

    /** バンドパス Q 値 0.1〜10 (固定 2.0 がデフォルト) */
    void setBpQ(float q) noexcept        { bpQ            = juce::jlimit(0.1f, 10.0f, q); }

    float getIndexedParameter(int index) const noexcept override { return 0.0f; }
    void setIndexedParameter(int index, float value) noexcept override
    {
        if      (index == 0) setDecay(value);
        else if (index == 1) setMix(value);
        else if (index == 2) setNoiseType(static_cast<int>(value));
        else if (index == 3) setGainDb(value);
        else if (index == 4) setAttack(value);
        else if (index == 5) setBpCenterHz(value);
        else if (index == 6) setBpQ(value);
    }

private:
    // ── ノイズ生成（チャンネル別） ────────────────────────────────────────

    float generateWhite()
    {
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        return dist(gen);
    }

    /** Pink noise: Paul Kellett 3段フィルター法（チャンネル別ステート） */
    float generatePinkForCh(int ch)
    {
        const float white = generateWhite();
        pinkB[ch][0] = 0.998f * pinkB[ch][0] + white * 0.0555179f;
        pinkB[ch][1] = 0.993f * pinkB[ch][1] + white * 0.0750759f;
        pinkB[ch][2] = 0.969f * pinkB[ch][2] + white * 0.1538520f;
        return (pinkB[ch][0] + pinkB[ch][1] + pinkB[ch][2] + white * 0.05362f) * 0.25f;
    }

    /** Brown (Red) noise: 1次リーキー積分（チャンネル別） */
    float generateBrownForCh(int ch)
    {
        const float white = generateWhite();
        brownAcc[ch] = (brownAcc[ch] + (0.02f * white)) / 1.02f;
        return brownAcc[ch] * 3.5f;
    }

    /** Blue noise: 1次差分（チャンネル別） */
    float generateBlueForCh(int ch)
    {
        const float white = generateWhite();
        const float blue  = white - lastWhite[ch];
        lastWhite[ch]     = white;
        return blue * 0.5f;
    }

    // ─────────────────────────────────────────────────────────────────────

    double currentSampleRate = 44100.0;
    std::random_device rd;
    std::mt19937 gen;

    TargetRoute route  = TargetRoute::Transient;
    bool activeState   = false;

    // ── チャンネル別ステート ──────────────────────────────────────────────
    float envelope[2]    = { 0.0f, 0.0f };
    bool  isAttacking[2] = { false, false };

    float pinkB[2][3]    = {};          // Pink noise フィルターバンク
    float brownAcc[2]    = { 0.0f, 0.0f };
    float lastWhite[2]   = { 0.0f, 0.0f };

    // Chamberlin SVF ステート
    float svfLow[2]      = { 0.0f, 0.0f };
    float svfBand[2]     = { 0.0f, 0.0f };

    // ── パラメーター ──────────────────────────────────────────────────────
    float decayMs        = 100.0f;
    float currentMix     = 0.3f;
    float currentGainDb  = 0.0f;
    int   currentNoiseType = 0;
    float attackMs       = 0.0f;
    float bpCenterHz     = 0.0f;   // 0 = バイパス
    float bpQ            = 2.0f;   // デフォルト Q=2 (打楽器向け適正値)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoiseGenerator)
};
