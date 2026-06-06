#pragma once

#include "AudioEffect.h"
#include <cmath>
#include <algorithm>

/**
 * ADAA_Saturation
 * 1次ADAA非線形歪み。偶数次倍音を制御する Asymmetry パラメータを完全内包。
 * 追加: OutputTrim (出力ゲイントリム) / PreHPF (プリサチュレーションHPF)
 */
class ADAA_Saturation final : public AudioEffect
{
public:
    ADAA_Saturation() = default;
    ~ADAA_Saturation() override = default;

    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        this->currentSampleRate = sampleRate;
        updatePreAlpha();
        reset();
    }

    void reset() noexcept override
    {
        oldX[0]          = 0.0f; oldX[1]          = 0.0f;
        dcFilterState[0] = 0.0f; dcFilterState[1] = 0.0f;
        preFilterX1[0]   = 0.0f; preFilterX1[1]   = 0.0f;
        preFilterY1[0]   = 0.0f; preFilterY1[1]   = 0.0f;
    }

    void process(juce::AudioBuffer<float>& buffer) noexcept override
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples  = buffer.getNumSamples();

        const float drive   = currentDrive;
        const float mix     = currentMix;
        const float asym    = currentAsymmetry;
        const float alpha   = preAlpha;
        const float outGain = outputGainLinear;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            if (ch >= 2) break;

            float* channelData = buffer.getWritePointer(ch);
            float x1      = oldX[ch];
            float dcState = dcFilterState[ch];
            float px1     = preFilterX1[ch];
            float py1     = preFilterY1[ch];

            for (int s = 0; s < numSamples; ++s)
            {
                const float originalInput = channelData[s];

                // ── 1次 HPF プリフィルター ──────────────────────────────
                // y[n] = alpha * (y[n-1] + x[n] - x[n-1])
                // alpha = 1/(1 + 2π·fc/sr)  ≒ 1 at fc=20Hz (実質スルー)
                const float hpfOut = alpha * (py1 + originalInput - px1);
                px1 = originalInput;
                py1 = hpfOut;

                // ── ADAA tanh サチュレーション ──────────────────────────
                const float x0   = (hpfOut * drive) + (asym * 0.4f);
                float saturatedSample = 0.0f;
                const float diff = x0 - x1;

                if (std::abs(diff) > 1.0e-5f)
                {
                    const float F_x0 = std::log(std::cosh(x0));
                    const float F_x1 = std::log(std::cosh(x1));
                    saturatedSample  = (F_x0 - F_x1) / diff;
                }
                else
                {
                    const float mid = 0.5f * (x0 + x1);
                    saturatedSample  = std::tanh(mid);
                }

                x1 = x0;

                // ── 直流オフセット除去 ──────────────────────────────────
                dcState         = 0.995f * dcState + 0.005f * saturatedSample;
                saturatedSample -= dcState;

                // ── Dry/Wet + 出力トリム ────────────────────────────────
                channelData[s] = (originalInput * (1.0f - mix))
                               + (saturatedSample * mix * outGain);
            }

            oldX[ch]          = x1;
            dcFilterState[ch] = dcState;
            preFilterX1[ch]   = px1;
            preFilterY1[ch]   = py1;
        }
    }

    juce::String getName() const override { return "ADAA Saturation"; }
    TargetRoute getTargetRoute() const noexcept override { return route; }
    void setTargetRoute(TargetRoute newRoute) noexcept override { route = newRoute; }

    bool isActive() const noexcept override { return activeState; }
    void setActive(bool shouldBeActive) noexcept override { activeState = shouldBeActive; }

    void setMix(float newMix) noexcept override { currentMix = juce::jlimit(0.0f, 1.0f, newMix); }
    float getMix() const noexcept override { return currentMix; }

    void setDrive(float newDrive) noexcept
    {
        currentDrive = juce::jlimit(1.0f, 16.0f, newDrive);
    }

    void setAsymmetry(float newAsym) noexcept
    {
        currentAsymmetry = juce::jlimit(0.0f, 1.0f, newAsym);
    }

    /** 出力トリム: -12 〜 +12 dB */
    void setOutputTrimDb(float db) noexcept
    {
        currentOutputTrimDb = juce::jlimit(-12.0f, 12.0f, db);
        outputGainLinear    = std::pow(10.0f, currentOutputTrimDb / 20.0f);
    }

    /** プリ HPF カットオフ: 20 〜 2000 Hz (20Hz ≒ スルー) */
    void setPreCutoffHz(float hz) noexcept
    {
        currentPreCutoffHz = juce::jlimit(20.0f, 2000.0f, hz);
        updatePreAlpha();
    }

    void setIndexedParameter(int index, float value) noexcept override
    {
        if      (index == 0) setDrive(value);
        else if (index == 1) setMix(value);
        else if (index == 2) setAsymmetry(value);
        else if (index == 3) setOutputTrimDb(value);
        else if (index == 4) setPreCutoffHz(value);
    }

private:
    void updatePreAlpha() noexcept
    {
        // 1次HPF係数: alpha = 1 / (1 + 2π·fc/sr)
        preAlpha = 1.0f / (1.0f + juce::MathConstants<float>::twoPi
                           * currentPreCutoffHz / static_cast<float>(currentSampleRate));
    }

    double currentSampleRate = 44100.0;
    TargetRoute route        = TargetRoute::FullMix;
    bool activeState         = false;

    // ADAA ステート（チャンネル別）
    float oldX[2]          = { 0.0f, 0.0f };
    float dcFilterState[2] = { 0.0f, 0.0f };

    // プリ HPF ステート（チャンネル別）
    float preFilterX1[2] = { 0.0f, 0.0f };
    float preFilterY1[2] = { 0.0f, 0.0f };

    // パラメーター
    float currentDrive         = 2.0f;
    float currentMix           = 0.5f;
    float currentAsymmetry     = 0.0f;
    float currentOutputTrimDb  = 0.0f;
    float currentPreCutoffHz   = 20.0f;

    // 事前計算値
    float outputGainLinear = 1.0f;
    float preAlpha         = 1.0f;  // prepare() で正確な値に更新

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ADAA_Saturation)
};
