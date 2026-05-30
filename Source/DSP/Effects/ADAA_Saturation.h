#pragma once

#include "AudioEffect.h"
#include <cmath>
#include <algorithm>

/**
 * ADAA_Saturation
 * 1次ADAA非線形歪み。偶数次倍音を動的加算する Asymmetry パラメータを追加。
 */
class ADAA_Saturation final : public AudioEffect
{
public:
    ADAA_Saturation() = default;
    ~ADAA_Saturation() override = default;

    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        this->currentSampleRate = sampleRate;
        reset();
    }

    void reset() noexcept override
    {
        oldX[0] = 0.0f;
        oldX[1] = 0.0f;
        dcFilterState[0] = 0.0f;
        dcFilterState[1] = 0.0f;
    }

    void process(juce::AudioBuffer<float>& buffer) noexcept override
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        const float drive = currentDrive;
        const float mix = currentMix;
        const float asym = currentAsymmetry;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            if (ch >= 2) break;

            float* channelData = buffer.getWritePointer(ch);
            float x1 = oldX[ch];
            float dcState = dcFilterState[ch];

            for (int s = 0; s < numSamples; ++s)
            {
                const float originalInput = channelData[s];

                // 💥【高精度化：Asymmetry】入力信号へ偶数次倍音の核となる非対称直流バイアスを重畳
                const float x0 = (originalInput * drive) + (asym * 0.4f);
                float saturatedSample = 0.0f;
                const float diff = x0 - x1;

                if (std::abs(diff) > 1.0e-5f)
                {
                    const float F_x0 = std::log(std::cosh(x0));
                    const float F_x1 = std::log(std::cosh(x1));
                    saturatedSample = (F_x0 - F_x1) / diff;
                }
                else
                {
                    const float mid = 0.5f * (x0 + x1);
                    saturatedSample = std::tanh(mid);
                }

                x1 = x0;

                // 💥非対称化に伴い発生する直流オフセットを 1次洩れ積分器(HPF) で完全に除去
                dcState = 0.995f * dcState + 0.005f * saturatedSample;
                saturatedSample -= dcState;

                channelData[s] = (originalInput * (1.0f - mix)) + (saturatedSample * mix);
            }

            oldX[ch] = x1;
            dcFilterState[ch] = dcState;
        }
    }

    juce::String getName() const override { return "ADAA Saturation"; }
    TargetRoute getTargetRoute() const noexcept override { return route; }
    void setTargetRoute(TargetRoute newRoute) noexcept override { route = newRoute; }

    bool isActive() const noexcept override { return activeState; }
    void setActive(bool shouldBeActive) noexcept override { activeState = shouldBeActive; }

    void setMix(float newMix) noexcept override { currentMix = juce::jlimit(0.0f, 1.0f, newMix); }
    float getMix() const noexcept override { return currentMix; }

    void setDrive(float newDrive) noexcept { currentDrive = juce::jlimit(1.0f, 16.0f, newDrive); }
    void setAsymmetry(float newAsym) noexcept { currentAsymmetry = juce::jlimit(0.0f, 1.0f, newAsym); }

    void setIndexedParameter(int index, float value) noexcept override
    {
        if (index == 0)      setDrive(value);
        else if (index == 1) setMix(value);
        else if (index == 2) setAsymmetry(value);
    }

private:
    double currentSampleRate = 44100.0;
    TargetRoute route = TargetRoute::FullMix;
    bool activeState = false;

    float oldX[2] = { 0.0f, 0.0f };
    float dcFilterState[2] = { 0.0f, 0.0f };

    float currentDrive = 2.0f;
    float currentMix = 0.5f;
    float currentAsymmetry = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ADAA_Saturation)
};