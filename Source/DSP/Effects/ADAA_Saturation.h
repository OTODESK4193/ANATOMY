#pragma once

#include "AudioEffect.h"
#include "../Saturation.h"   // ngk::processSaturationSampleADAA / SaturationState
#include <cmath>
#include <algorithm>

/**
 * ADAA_Saturation (Multi-Algorithm Edition)
 * NEXT GEN KICK の Master サチュレーションと同一アルゴリズム群を内蔵。
 * Type で 10 種（BitCrush を除く）を選択でき、Drive / Mix / Trim / Pre-HPF を持つ。
 *
 * Type(コンボ index) → ngk サチュレーション種別 の対応:
 *   0 Soft Tanh, 1 Hard Clip, 2 Triode, 3 Tape, 4 Transformer,
 *   5 JFET, 6 BJT, 7 Wavefold, 8 Exciter, 9 Cubic
 *   （ngk 内部 type の Bitcrush(8) を除外し詰めたもの）
 */
class ADAA_Saturation final : public AudioEffect
{
public:
    // コンボ index → ngk 内部 type（Bitcrush=8 を除外）
    static int comboToNgkType(int combo) noexcept
    {
        static const int map[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 9, 10 };
        return map[juce::jlimit(0, 9, combo)];
    }

    ADAA_Saturation() = default;
    ~ADAA_Saturation() override = default;

    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        this->currentSampleRate = sampleRate;
        // DCブロッカー係数をSR連動に（44.1kで従来の0.995＝時定数約4.52msと一致。高SRでも同じ低域カットに保つ）
        dcCoef = std::exp(-1.0f / (0.004523f * static_cast<float>(sampleRate)));
        updatePreAlpha();
        reset();
    }

    void reset() noexcept override
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            satState[ch].reset();
            dcFilterState[ch] = 0.0f;
            preFilterX1[ch]   = 0.0f;
            preFilterY1[ch]   = 0.0f;
        }
    }

    void process(juce::AudioBuffer<float>& buffer) noexcept override
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples  = buffer.getNumSamples();

        const float drive   = currentDrive;
        const float mix     = currentMix;
        const float alpha   = preAlpha;
        const float outGain = outputGainLinear;
        const int   ngkType = comboToNgkType(currentType);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            if (ch >= 2) break;

            float* channelData = buffer.getWritePointer(ch);
            float dcState = dcFilterState[ch];
            float px1     = preFilterX1[ch];
            float py1     = preFilterY1[ch];

            for (int s = 0; s < numSamples; ++s)
            {
                const float originalInput = channelData[s];

                // ── 1次 HPF プリフィルター ──────────────────────────────
                const float hpfOut = alpha * (py1 + originalInput - px1);
                px1 = originalInput;
                py1 = hpfOut;

                // ── Master と同一アルゴリズムのサチュレーション ─────────
                float sat = ngk::processSaturationSampleADAA(hpfOut, ngkType, drive, satState[ch]);

                // ── 直流オフセット除去 ──────────────────────────────────
                dcState = dcCoef * dcState + (1.0f - dcCoef) * sat;
                sat    -= dcState;

                // ── Dry/Wet + 出力トリム ────────────────────────────────
                channelData[s] = (originalInput * (1.0f - mix)) + (sat * mix * outGain);
            }

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

    void setDrive(float newDrive) noexcept { currentDrive = juce::jlimit(1.0f, 16.0f, newDrive); }
    void setType(int comboIndex) noexcept { currentType = juce::jlimit(0, 9, comboIndex); }

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
        else if (index == 2) setType(static_cast<int>(value));
        else if (index == 3) setOutputTrimDb(value);
        else if (index == 4) setPreCutoffHz(value);
    }
    float getIndexedParameter(int index) const noexcept override
    {
        switch (index) {
        case 0: return currentDrive; case 1: return currentMix; case 2: return (float)currentType;
        case 3: return currentOutputTrimDb; case 4: return currentPreCutoffHz; default: return 0.0f;
        }
    }

private:
    void updatePreAlpha() noexcept
    {
        preAlpha = 1.0f / (1.0f + juce::MathConstants<float>::twoPi
                           * currentPreCutoffHz / static_cast<float>(currentSampleRate));
    }

    double currentSampleRate = 44100.0;
    TargetRoute route        = TargetRoute::FullMix;
    bool activeState         = false;

    ngk::SaturationState satState[2];
    float dcFilterState[2] = { 0.0f, 0.0f };
    float dcCoef = 0.995f;   // DCブロッカー係数（prepareでSR連動に設定）
    float preFilterX1[2]   = { 0.0f, 0.0f };
    float preFilterY1[2]   = { 0.0f, 0.0f };

    // パラメーター
    float currentDrive        = 2.0f;
    float currentMix          = 0.5f;
    int   currentType         = 0;
    float currentOutputTrimDb = 0.0f;
    float currentPreCutoffHz  = 20.0f;

    float outputGainLinear = 1.0f;
    float preAlpha         = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ADAA_Saturation)
};
