#pragma once

#include "AudioEffect.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>

/**
 * OTT_Multiband
 * 分岐帯域をピンポイントシフトできる X-Over Freq パラメータを追加。
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

        for (int i = 0; i < 3; ++i)
        {
            comps[i].prepare(spec);
            comps[i].setThreshold(-12.0f);
            comps[i].setRatio(4.0f);
        }

        updateCrossoverAndDynamics();

        lowBuffer.setSize(2, maxBlockSize, false, false, true);
        midBuffer.setSize(2, maxBlockSize, false, false, true);
        highBuffer.setSize(2, maxBlockSize, false, false, true);
    }

    void reset() noexcept override
    {
        filterLow.reset();
        filterHigh.reset();
        for (auto& c : comps) c.reset();
    }

    void process(juce::AudioBuffer<float>& buffer) noexcept override
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        if (lowBuffer.getNumSamples() < numSamples) return;

        lowBuffer.copyFrom(0, 0, buffer, 0, 0, numSamples);
        if (numChannels > 1) lowBuffer.copyFrom(1, 0, buffer, 1, 0, numSamples);

        midBuffer.copyFrom(0, 0, buffer, 0, 0, numSamples);
        if (numChannels > 1) midBuffer.copyFrom(1, 0, buffer, 1, 0, numSamples);

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
            const float* src = buffer.getReadPointer(ch);
            const float* low = lowBuffer.getReadPointer(ch);
            const float* high = highBuffer.getReadPointer(ch);
            float* mid = midBuffer.getWritePointer(ch);

            for (int s = 0; s < numSamples; ++s)
            {
                mid[s] = src[s] - low[s] - high[s];
            }
        }

        juce::dsp::ProcessContextReplacing<float> ctxLow(lowBlock);
        comps[0].process(ctxLow);

        juce::dsp::AudioBlock<float> midBlock(midBuffer);
        juce::dsp::ProcessContextReplacing<float> ctxMid(midBlock);
        comps[1].process(ctxMid);

        juce::dsp::ProcessContextReplacing<float> ctxHigh(highBlock);
        comps[2].process(ctxHigh);

        const float mix = currentMix;
        const float outGainLinear = std::pow(10.0f, outGainDbParam / 20.0f);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* dest = buffer.getWritePointer(ch);
            const float* low = lowBuffer.getReadPointer(ch);
            const float* mid = midBuffer.getReadPointer(ch);
            const float* high = highBuffer.getReadPointer(ch);

            for (int s = 0; s < numSamples; ++s)
            {
                float wet = (low[s] + mid[s] + high[s]) * outGainLinear;
                dest[s] = (dest[s] * (1.0f - mix)) + (wet * mix);
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

    void setTimeMultiplier(float t) noexcept
    {
        float newT = juce::jlimit(0.1f, 10.0f, t);
        if (std::abs(timeMultiplierParam - newT) > 1.0e-4f)
        {
            timeMultiplierParam = newT;
            updateCrossoverAndDynamics();
        }
    }

    void setOutGainDb(float gainDb) noexcept { outGainDbParam = juce::jlimit(-24.0f, 24.0f, gainDb); }

    void setCrossoverFreq(float freqHz) noexcept
    {
        float newFreq = juce::jlimit(100.0f, 1000.0f, freqHz);
        if (std::abs(crossoverFreqParam - newFreq) > 1.0e-2f)
        {
            crossoverFreqParam = newFreq;
            updateCrossoverAndDynamics();
        }
    }

    void setIndexedParameter(int index, float value) noexcept override
    {
        if (index == 0)      setMix(value); // Depth/Mixの共通統合
        else if (index == 1) setTimeMultiplier(value);
        else if (index == 2) setOutGainDb(value);
        else if (index == 3) setCrossoverFreq(value);
    }

private:
    void updateCrossoverAndDynamics() noexcept
    {
        // 💥【高精度化：X-Over Freq】ドラムのキャラクターに応じてクロスオーバーを動的アライメント
        filterLow.setCutoffFrequency(crossoverFreqParam);
        filterHigh.setCutoffFrequency(crossoverFreqParam * 12.5f); // ロー準拠でハイを相関シフト

        float att = 10.0f * timeMultiplierParam;
        float rel = 100.0f * timeMultiplierParam;
        for (auto& c : comps)
        {
            c.setAttack(att);
            c.setRelease(rel);
        }
    }

    double currentSampleRate = 44100.0;
    juce::dsp::StateVariableTPTFilter<float> filterLow;
    juce::dsp::StateVariableTPTFilter<float> filterHigh;
    juce::dsp::Compressor<float> comps[3];

    juce::AudioBuffer<float> lowBuffer;
    juce::AudioBuffer<float> midBuffer;
    juce::AudioBuffer<float> highBuffer;

    float currentMix = 0.7f;
    float timeMultiplierParam = 1.0f;
    float outGainDbParam = 0.0f;
    float crossoverFreqParam = 200.0f;

    TargetRoute route = TargetRoute::FullMix;
    bool activeState = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OTT_Multiband)
};