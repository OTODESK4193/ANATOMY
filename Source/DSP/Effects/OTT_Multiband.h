#pragma once

#include "AudioEffect.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>

/**
 * OTT_Multiband
 * ZDFクロスオーバーを用いた完全ゼロレイテンシー・3バンド・コンプレッサー。
 * オーディオスレッドでの動的メモリ確保を100%排除した事前展開バッファ構造。
 */
class OTT_Multiband final : public AudioEffect
{
public:
    OTT_Multiband()
    {
        // 帯域分割用ZDF（トポロジー保存）フィルターのタイプ設定
        filterLow.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        filterHigh.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    }

    ~OTT_Multiband() override = default;

    void prepare(double sampleRate, int maxBlockSize) override
    {
        currentSampleRate = sampleRate;

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (maxBlockSize);
        spec.numChannels = 2;

        filterLow.prepare(spec);
        filterHigh.prepare(spec);

        // クロスオーバー遮断周波数の初期設定
        filterLow.setCutoffFrequency(200.0f);
        filterHigh.setCutoffFrequency(2500.0f);

        // 各帯域コンプレッサーの初期設定
        for (int i = 0; i < 3; ++i)
        {
            comps[i].prepare(spec);
            comps[i].setThreshold(-12.0f);
            comps[i].setRatio(4.0f);
            comps[i].setAttack(10.0f);
            comps[i].setRelease(100.0f);
        }

        // 💥リアルタイム安全性を死守するため、一時分離バッファを事前最大サイズで確保
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

        // 各一時バッファへ入力を高速コピー
        lowBuffer.copyFrom(0, 0, buffer, 0, 0, numSamples);
        if (numChannels > 1) lowBuffer.copyFrom(1, 0, buffer, 1, 0, numSamples);

        midBuffer.copyFrom(0, 0, buffer, 0, 0, numSamples);
        if (numChannels > 1) midBuffer.copyFrom(1, 0, buffer, 1, 0, numSamples);

        highBuffer.copyFrom(0, 0, buffer, 0, 0, numSamples);
        if (numChannels > 1) highBuffer.copyFrom(1, 0, buffer, 1, 0, numSamples);

        // 💥【修正解決】AudioBlockとProcessContextReplacingを正しく段付け構築
        juce::dsp::AudioBlock<float> lowBlock(lowBuffer);
        juce::dsp::ProcessContextReplacing<float> lowContext(lowBlock);
        filterLow.process(lowContext);

        juce::dsp::AudioBlock<float> highBlock(highBuffer);
        juce::dsp::ProcessContextReplacing<float> highContext(highBlock);
        filterHigh.process(highContext);

        // Mid = FullMix原音 - Low成分 - High成分 による完全再構築（Perfect Reconstruction）
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

        // 3帯域個別のダイナミクス・コンプレッション実行
        juce::dsp::ProcessContextReplacing<float> ctxLow(lowBlock);
        comps[0].process(ctxLow);

        juce::dsp::AudioBlock<float> midBlock(midBuffer);
        juce::dsp::ProcessContextReplacing<float> ctxMid(midBlock);
        comps[1].process(ctxMid);

        juce::dsp::ProcessContextReplacing<float> ctxHigh(highBlock);
        comps[2].process(ctxHigh);

        // 帯域の再結合と深度ミックス（Depthブレンド）
        const float depth = depthParam;
        for (int ch = 0; ch < numChannels; ++ch)
        {
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

    void setDepth(float d) noexcept { depthParam = juce::jlimit(0.0f, 1.0f, d); }

private:
    double currentSampleRate = 44100.0;
    juce::dsp::StateVariableTPTFilter<float> filterLow;
    juce::dsp::StateVariableTPTFilter<float> filterHigh;
    juce::dsp::Compressor<float> comps[3];

    juce::AudioBuffer<float> lowBuffer;
    juce::AudioBuffer<float> midBuffer;
    juce::AudioBuffer<float> highBuffer;

    float depthParam = 0.7f;
    TargetRoute route = TargetRoute::FullMix;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OTT_Multiband)
};