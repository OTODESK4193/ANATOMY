#pragma once

#include "AudioEffect.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>

/**
 * OTT_Multiband (Phase 2 Master Edition)
 * 既製品コンプレッサーを完全排除し、Upward（上方）/ Downward（下方）Dynamics数理、
 * およびLow/Mid/High独立帯域ゲイン回路を完全内包した、自作3バンドOTTエンジン。
 */
class OTT_Multiband final : public AudioEffect
{
public:
    OTT_Multiband()
    {
        // リンクウィッツ・ライリー型（Linkwitz-Riley）クラスに近い特性を持つ
        // 状態変数（State Variable）フィルターをローパス・ハイパスに設定
        filterLow.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        filterHigh.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    }

    ~OTT_Multiband() override = default;

    void prepare(double sampleRate, int maxBlockSize) override
    {
        currentSampleRate = sampleRate;

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32>(maxBlockSize);
        spec.numChannels = 2;

        filterLow.prepare(spec);
        filterHigh.prepare(spec);

        updateCrossoverFilters();

        lowBuffer.setSize(2, maxBlockSize, false, false, true);
        midBuffer.setSize(2, maxBlockSize, false, false, true);
        highBuffer.setSize(2, maxBlockSize, false, false, true);

        // 各帯域のエンベロープフォロワー履歴の初期化
        for (int b = 0; b < 3; ++b)
        {
            envFollower[b] = 0.0f;
        }
    }

    void reset() noexcept override
    {
        filterLow.reset();
        filterHigh.reset();
        for (int b = 0; b < 3; ++b)
        {
            envFollower[b] = 0.0f;
        }
    }

    void process(juce::AudioBuffer<float>& buffer) noexcept override
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        if (lowBuffer.getNumSamples() < numSamples) return;

        // クロスオーバー分岐のために元信号を各一時バッファへ高速射影
        lowBuffer.copyFrom(0, 0, buffer, 0, 0, numSamples);
        if (numChannels > 1) lowBuffer.copyFrom(1, 0, buffer, 1, 0, numSamples);

        highBuffer.copyFrom(0, 0, buffer, 0, 0, numSamples);
        if (numChannels > 1) highBuffer.copyFrom(1, 0, buffer, 1, 0, numSamples);

        // 1. ゼロディレイ・フィードバック（ZDF）フィルターによる3帯域の数学的分離
        juce::dsp::AudioBlock<float> lowBlock(lowBuffer);
        juce::dsp::ProcessContextReplacing<float> lowContext(lowBlock);
        filterLow.process(lowContext); // Lowパス通過 ➡ Low帯域の確定

        juce::dsp::AudioBlock<float> highBlock(highBuffer);
        juce::dsp::ProcessContextReplacing<float> highContext(highBlock);
        filterHigh.process(highContext); // Highパス通過 ➡ High帯域の確定

        // Mid帯域は「全帯域の元信号 - Low信号 - High信号」という完全減算による位相反転相殺数理で抽出
        for (int ch = 0; ch < numChannels; ++ch)
        {
            if (ch >= 2) break;
            const float* src = buffer.getReadPointer(ch);
            const float* low = lowBuffer.getReadPointer(ch);
            const float* high = highBuffer.getReadPointer(ch);
            float* mid = midBuffer.getWritePointer(ch);

            for (int s = 0; s < numSamples; ++s)
            {
                mid[s] = src[s] - low[s] - high[s];
            }
        }

        // 時定数アライメントの計算
        const float timeMultiplier = std::max(0.1f, timeMultiplierParam);
        // 基準アタックタイム: 10ms, リリースタイム: 100ms を係数化
        const float attackCoef = std::exp(-1.0f / (0.010f * timeMultiplier * static_cast<float>(currentSampleRate)));
        const float releaseCoef = std::exp(-1.0f / (0.100f * timeMultiplier * static_cast<float>(currentSampleRate)));

        // 本物OTT規格の固定内部しきい値（-30 dBFS）をリニア換算
        const float ottThreshold = std::pow(10.0f, -30.0f / 20.0f);

        float* bandPtrs[3] = { lowBuffer.getWritePointer(0), midBuffer.getWritePointer(0), highBuffer.getWritePointer(0) };
        float* bandPtrsR[3] = { numChannels > 1 ? lowBuffer.getWritePointer(1) : nullptr,
                                numChannels > 1 ? midBuffer.getWritePointer(1) : nullptr,
                                numChannels > 1 ? highBuffer.getWritePointer(1) : nullptr };

        // 2. 独自開発マルチバンド「Upward / Downward」同軸複合Dynamicsエンジン駆動
        for (int b = 0; b < 3; ++b)
        {
            float* dataL = bandPtrs[b];
            float* dataR = bandPtrsR[b];

            // 独立帯域最終Gainのリニア換算
            const float bandGainLinear = std::pow(10.0f, bandGainDb[b] / 20.0f);

            for (int s = 0; s < numSamples; ++s)
            {
                // ステレオリンク対応のピークサイドチェイン検出
                float absL = std::abs(dataL[s]);
                float absR = dataR != nullptr ? std::abs(dataR[s]) : 0.0f;
                float currentPeak = std::max(absL, absR);

                // エンベロープフォロワーの弾道追従
                float& env = envFollower[b];
                if (currentPeak > env) env = attackCoef * env + (1.0f - attackCoef) * currentPeak;
                else                   env = releaseCoef * env + (1.0f - releaseCoef) * currentPeak;

                // 核心数理：Upward / Downward 同軸圧縮アルゴリズム
                float gainReduction = 1.0f;

                if (env > 1.0e-5f)
                {
                    // 基準しきい値に対する現入力の比率を検出
                    float ratioOffset = env / ottThreshold;

                    if (ratioOffset > 1.0f)
                    {
                        // 【Downward領域】：音がしきい値を超えたので、比率 4:1 で叩く
                        float downFactor = std::pow(ratioOffset, (1.0f / 4.0f) - 1.0f);
                        // ユーザー設定の％（0.0〜1.0）で線形適用量をブレンド
                        gainReduction *= (1.0f - downwardAmount[b]) + (downwardAmount[b] * downFactor);
                    }
                    else
                    {
                        // 【Upward領域】：音がしきい値より小さいので、比率 1:2 で強制引き上げ
                        float upFactor = std::pow(ratioOffset, (1.0f / 0.5f) - 1.0f);
                        // 最大 ＋18dB までの上方ブースト天井ガード
                        upFactor = std::min(upFactor, 7.94f);
                        // ユーザー設定の％（0.0〜1.0）で線形適用量をブレンド
                        gainReduction *= (1.0f - upwardAmount[b]) + (upwardAmount[b] * upFactor);
                    }
                }

                // ダイナミクス適用と帯域ゲインの最終乗算
                float finalScalar = gainReduction * bandGainLinear;
                dataL[s] *= finalScalar;
                if (dataR != nullptr) dataR[s] *= finalScalar;
            }
        }

        // 3. 全帯域の最終再合算、およびウェット深度（Mix）のブレンド
        const float depth = currentMix;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            if (ch >= 2) break;
            float* dest = buffer.getWritePointer(ch);
            const float* low = lowBuffer.getReadPointer(ch);
            const float* mid = midBuffer.getReadPointer(ch);
            const float* high = highBuffer.getReadPointer(ch);

            for (int s = 0; s < numSamples; ++s)
            {
                float wet = low[s] + mid[s] + high[s];
                dest[s] = (dest[s] * (1.0f - depth)) + (wet * depth);
            }
        }
    }

    juce::String getName() const override { return "OTT Multiband"; }
    TargetRoute getTargetRoute() const noexcept override { return route; }
    void setTargetRoute(TargetRoute r) noexcept override { route = r; }

    bool isActive() const noexcept override { return activeState; }
    void setActive(bool shouldBeActive) noexcept override { activeState = shouldBeActive; }

    void setMix(float newMix) noexcept override { currentMix = juce::jlimit(0.0f, 1.0f, newMix); }
    float getMix() const noexcept override { return currentMix; }

    void setTimeMultiplier(float t) noexcept { timeMultiplierParam = juce::jlimit(0.1f, 10.0f, t); }

    // 💥【新設】分離独立型2ツマミ・クロスオーバー入力インターフェース
    void setLowMidXOver(float freq) noexcept { lowMidFreqParam = juce::jlimit(40.0f, 1000.0f, freq); updateCrossoverFilters(); }
    void setMidHighXOver(float freq) noexcept { midHighFreqParam = juce::jlimit(1000.0f, 15000.0f, freq); updateCrossoverFilters(); }

    // 旧プロセッサ側から叩かれるダミー互換セッター（内部結合の安全確保用）
    void setOutGainDb(float) noexcept {}
    void setCrossoverFreq(float f) noexcept { setLowMidXOver(f); }

    // 💥【新設】9パラメータ一斉制御セッター
    void setBandUpward(int bandIdx, float pct) noexcept { if (bandIdx >= 0 && bandIdx < 3) upwardAmount[bandIdx] = juce::jlimit(0.0f, 1.0f, pct); }
    void setBandDownward(int bandIdx, float pct) noexcept { if (bandIdx >= 0 && bandIdx < 3) downwardAmount[bandIdx] = juce::jlimit(0.0f, 1.0f, pct); }
    void setBandGainDb(int bandIdx, float db) noexcept { if (bandIdx >= 0 && bandIdx < 3) bandGainDb[bandIdx] = juce::jlimit(-24.0f, 24.0f, db); }

    void setIndexedParameter(int index, float value) noexcept override
    {
        if (index == 0)      setMix(value);
        else if (index == 1) setTimeMultiplier(value);
    }

private:
    void updateCrossoverFilters() noexcept
    {
        filterLow.setCutoffFrequency(lowMidFreqParam);
        filterHigh.setCutoffFrequency(midHighFreqParam);
    }

    double currentSampleRate = 44100.0;

    juce::dsp::StateVariableTPTFilter<float> filterLow;
    juce::dsp::StateVariableTPTFilter<float> filterHigh;

    juce::AudioBuffer<float> lowBuffer;
    juce::AudioBuffer<float> midBuffer;
    juce::AudioBuffer<float> highBuffer;

    float currentMix = 0.7f;
    float timeMultiplierParam = 1.0f;
    float lowMidFreqParam = 200.0f;      // 初期値 200 Hz
    float midHighFreqParam = 2500.0f;    // 初期値 2.5 kHz

    // 3バンド分配配列パラメータコンテナ [0=Low, 1=Mid, 2=High]
    float upwardAmount[3] = { 1.0f, 1.0f, 1.0f };
    float downwardAmount[3] = { 1.0f, 1.0f, 1.0f };
    float bandGainDb[3] = { 0.0f, 0.0f, 0.0f };

    float envFollower[3];

    TargetRoute route = TargetRoute::FullMix;
    bool activeState = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OTT_Multiband)
};