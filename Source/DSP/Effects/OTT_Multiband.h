#pragma once

#include "AudioEffect.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>

/**
 * OTT_Multiband (Phase 4 Master Noise-Free Edition)
 * 無音時や弱音時のフロアノイズが爆発的に持ち上がるのを自動的に検知し、
 * Upward Dynamicsの適用量を分子レベルで自動減衰遮断させる「スマート・ローレベル・ゲート数理」を搭載。
 * 初期Mix値を使いやすい 35% へリチューニングした、ノイズフリー型最高級3バンドOTTエンジン。
 */
class OTT_Multiband final : public AudioEffect
{
public:
    OTT_Multiband()
    {
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

        lowBuffer.copyFrom(0, 0, buffer, 0, 0, numSamples);
        if (numChannels > 1) lowBuffer.copyFrom(1, 0, buffer, 1, 0, numSamples);

        highBuffer.copyFrom(0, 0, buffer, 0, 0, numSamples);
        if (numChannels > 1) highBuffer.copyFrom(1, 0, buffer, 1, 0, numSamples);

        juce::dsp::AudioBlock<float> lowBlock(lowBuffer);
        juce::dsp::ProcessContextReplacing<float> lowContext(lowBlock);
        filterLow.process(lowContext);

        juce::dsp::AudioBlock<float> highBlock(highBuffer);
        juce::dsp::ProcessContextReplacing<float> highContext(highBlock);
        filterHigh.process(highContext);

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

        const float timeMultiplier = std::max(0.1f, timeMultiplierParam);
        const float attackCoef = std::exp(-1.0f / (0.010f * timeMultiplier * static_cast<float>(currentSampleRate)));
        const float releaseCoef = std::exp(-1.0f / (0.100f * timeMultiplier * static_cast<float>(currentSampleRate)));

        const float ottThreshold = std::pow(10.0f, -30.0f / 20.0f); // -30 dBFS 固定内部しきい値

        // 💥【ノイズ対策核心数理：ローレベル・スマートゲート限界値設定】
        // 聴感上、サーノイズや吸気音が爆発的に持ち上がり始める閾値（-54dBFS）を検知リミッターに設定
        const float noiseFloorThreshold = std::pow(10.0f, -54.0f / 20.0f);
        const float gateGripBottom = std::pow(10.0f, -66.0f / 20.0f); // この下は完全遮断スリープ

        float* bandPtrs[3] = { lowBuffer.getWritePointer(0), midBuffer.getWritePointer(0), highBuffer.getWritePointer(0) };
        float* bandPtrsR[3] = { numChannels > 1 ? lowBuffer.getWritePointer(1) : nullptr,
                                numChannels > 1 ? midBuffer.getWritePointer(1) : nullptr,
                                numChannels > 1 ? highBuffer.getWritePointer(1) : nullptr };

        for (int b = 0; b < 3; ++b)
        {
            float* dataL = bandPtrs[b];
            float* dataR = bandPtrsR[b];

            const float bandGainLinear = std::pow(10.0f, bandGainDb[b] / 20.0f);

            for (int s = 0; s < numSamples; ++s)
            {
                float absL = std::abs(dataL[s]);
                float absR = dataR != nullptr ? std::abs(dataR[s]) : 0.0f;
                float currentPeak = std::max(absL, absR);

                float& env = envFollower[b];
                if (currentPeak > env) env = attackCoef * env + (1.0f - attackCoef) * currentPeak;
                else                   env = releaseCoef * env + (1.0f - releaseCoef) * currentPeak;

                float gainReduction = 1.0f;

                if (env > 1.0e-5f)
                {
                    float ratioOffset = env / ottThreshold;

                    if (ratioOffset > 1.0f)
                    {
                        // 【Downward領域】：4:1 比率で叩く
                        float downFactor = std::pow(ratioOffset, (1.0f / 4.0f) - 1.0f);
                        gainReduction *= (1.0f - downwardAmount[b]) + (downwardAmount[b] * downFactor);
                    }
                    else
                    {
                        // 【Upward領域】：1:2 比率で強制引き上げ（最大＋18dBブースト）
                        float upFactor = std::pow(ratioOffset, (1.0f / 0.5f) - 1.0f);
                        upFactor = std::min(upFactor, 7.94f);

                        // 💥【スマート・ノイズミュート数理のインジェクション】
                        // 信号のエネルギーがノイズフロアに近づくにつれて、Upwardのブースト適用量を
                        // 自動的になめらかに 1.0（等倍＝ブースト無し）へ向けて減衰遮断させる！
                        if (env < noiseFloorThreshold)
                        {
                            float gateFactor = (env - gateGripBottom) / (noiseFloorThreshold - gateGripBottom);
                            gateFactor = std::max(0.0f, std::min(1.0f, gateFactor)); // 0.0 〜 1.0 にクランプ

                            // 1.0（無加工）と upFactor の間で線形補間し、無音時のサーノイズを完全スリープ
                            upFactor = 1.0f + (upFactor - 1.0f) * gateFactor;
                        }

                        gainReduction *= (1.0f - upwardAmount[b]) + (upwardAmount[b] * upFactor);
                    }
                }

                float finalScalar = gainReduction * bandGainLinear;
                dataL[s] *= finalScalar;
                if (dataR != nullptr) dataR[s] *= finalScalar;
            }
        }

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

    void setLowMidXOver(float freq) noexcept { lowMidFreqParam = juce::jlimit(40.0f, 1000.0f, freq); updateCrossoverFilters(); }
    void setMidHighXOver(float freq) noexcept { midHighFreqParam = juce::jlimit(1000.0f, 15000.0f, freq); updateCrossoverFilters(); }

    void setOutGainDb(float) noexcept {}
    void setCrossoverFreq(float f) noexcept { setLowMidXOver(f); }

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

    // 💥【汎用デフォルト変更】挿入時のノイズ事故を防ぐため、初期ウェット量を 35% にリチューニング
    float currentMix = 0.35f;
    float timeMultiplierParam = 1.0f;
    float lowMidFreqParam = 200.0f;
    float midHighFreqParam = 2500.0f;

    float upwardAmount[3] = { 1.0f, 1.0f, 1.0f };
    float downwardAmount[3] = { 1.0f, 1.0f, 1.0f };
    float bandGainDb[3] = { 0.0f, 0.0f, 0.0f };

    float envFollower[3];

    TargetRoute route = TargetRoute::FullMix;
    bool activeState = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OTT_Multiband)
};