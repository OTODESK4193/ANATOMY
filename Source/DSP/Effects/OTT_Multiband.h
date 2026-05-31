#pragma once

#include "AudioEffect.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>

/**
 * OTT_Multiband (Production-Grade Noise-Controlled Edition)
 * 弱音領域でのフロアノイズ増幅を抑制する「改良型スマート・ローレベル・ゲート数理」を搭載。
 * 各帯域の役割に適合した傾斜配分初期値と、ドラムの基音に調和するクロスオーバー設計を内包した3バンドOTTエンジン。
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

        // 💥【スマート・ノイズミュート数理の閾値最適化】
        // ドラムのテール減衰特性に合わせ、作動開始の上弦の閾値を -45.0dBFS、遮断下弦を -54.0dBFS に先回りシフト
        const float noiseFloorThreshold = std::pow(10.0f, -45.0f / 20.0f);
        const float gateGripBottom = std::pow(10.0f, -54.0f / 20.0f);

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
                        // 【Downward領域】：4:1 比率で圧縮
                        float downFactor = std::pow(ratioOffset, (1.0f / 4.0f) - 1.0f);
                        gainReduction *= (1.0f - downwardAmount[b]) + (downwardAmount[b] * downFactor);
                    }
                    else
                    {
                        // 【Upward領域】：1:2 比率で引き上げ（最大＋18dBブースト）
                        float upFactor = std::pow(ratioOffset, (1.0f / 0.5f) - 1.0f);
                        upFactor = std::min(upFactor, 7.94f);

                        if (env < noiseFloorThreshold)
                        {
                            float gateFactor = (env - gateGripBottom) / (noiseFloorThreshold - gateGripBottom);
                            gateFactor = std::max(0.0f, std::min(1.0f, gateFactor));

                            // 1.0（無加工）と upFactor の間で線形補間し、無音〜弱音時のサーノイズを完全ミュート
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

    // 💥【プロダクション初期アライメント】ノイズフロアの暴れを防ぎ、パラレルコンプとして最も馴染む音楽的基準数値を初期固定
    float currentMix = 0.35f;
    float timeMultiplierParam = 1.35f;
    float lowMidFreqParam = 140.0f;
    float midHighFreqParam = 3800.0f;

    float upwardAmount[3] = { 0.60f, 0.40f, 0.15f };
    float downwardAmount[3] = { 0.75f, 0.70f, 0.60f };
    float bandGainDb[3] = { 0.0f, 0.0f, 0.0f };

    float envFollower[3];

    TargetRoute route = TargetRoute::FullMix;
    bool activeState = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OTT_Multiband)
};