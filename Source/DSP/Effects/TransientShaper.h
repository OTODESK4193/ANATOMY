#pragma once

#include "AudioEffect.h"
#include <cmath>
#include <algorithm>

/**
 * TransientShaper
 * 速い/遅い2つのエンベロープ追従の差分でトランジェント(立ち上がり)と
 * サステイン(余韻)を検出し、それぞれを増減させる。
 * キックの「アタックの張り」「ボディの伸び」を独立して調整できる。
 *
 *  Attack  : -1..+1  (アタック成分の増減。+でパンチUP)
 *  Sustain : -1..+1  (サステイン成分の増減。+で余韻UP)
 *  Mix     :  0..1   (Dry/Wet)
 */
class TransientShaper final : public AudioEffect
{
public:
    TransientShaper() = default;
    ~TransientShaper() override = default;

    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        sr = (float)sampleRate;
        // 追従係数（時間→1極係数）
        fastAtk = onePole(1.0f);    // 1ms
        fastRel = onePole(30.0f);   // 30ms
        slowAtk = onePole(20.0f);   // 20ms
        slowRel = onePole(150.0f);  // 150ms
        reset();
    }

    void reset() noexcept override
    {
        for (int c = 0; c < 2; ++c) { fastEnv[c] = 0.0f; slowEnv[c] = 0.0f; }
    }

    void process(juce::AudioBuffer<float>& buffer) noexcept override
    {
        const int numCh = juce::jmin(2, buffer.getNumChannels());
        const int n = buffer.getNumSamples();
        const float mix = currentMix;
        const float atkAmt = currentAttack;   // -1..1
        const float susAmt = currentSustain;  // -1..1

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* d = buffer.getWritePointer(ch);
            float fe = fastEnv[ch];
            float se = slowEnv[ch];

            for (int s = 0; s < n; ++s)
            {
                const float x = d[s];
                const float rect = std::abs(x);

                // 2つのエンベロープ追従
                fe += (rect > fe ? fastAtk : fastRel) * (rect - fe);
                se += (rect > se ? slowAtk : slowRel) * (rect - se);

                // 差分(dB): 正=アタック区間 / 負=サステイン区間
                const float ratio = (se > 1.0e-6f) ? (fe / se) : 1.0f;
                const float transDb = 20.0f * std::log10(std::max(1.0e-6f, ratio));

                float gainDb = 0.0f;
                if (transDb > 0.0f) gainDb = atkAmt * 12.0f * (transDb / 6.0f); // アタック側
                else                gainDb = susAmt * 12.0f * (-transDb / 6.0f); // サステイン側
                gainDb = juce::jlimit(-18.0f, 18.0f, gainDb);

                const float wet = x * std::pow(10.0f, gainDb / 20.0f);
                d[s] = x * (1.0f - mix) + wet * mix;
            }

            fastEnv[ch] = fe;
            slowEnv[ch] = se;
        }
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
    float fastEnv[2] = { 0.0f, 0.0f };
    float slowEnv[2] = { 0.0f, 0.0f };

    float currentMix = 1.0f;
    float currentAttack = 0.0f;
    float currentSustain = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransientShaper)
};
