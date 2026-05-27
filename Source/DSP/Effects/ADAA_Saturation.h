#pragma once

#include "AudioEffect.h"
#include <cmath>
#include <algorithm>

/**
 * ADAA_Saturation
 * 1次アンチデリバティブ・アンチエイリアシング（ADAA）搭載サチュレーター。
 * 完全ゼロレイテンシーかつ高品質な非線形歪みを生成します。
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
    }

    void process(juce::AudioBuffer<float>& buffer) noexcept override
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        // 内部パラメータの一括ローカルロード（アトミック等へのアクセス削減）
        const float drive = currentDrive;
        const float mix = currentMix;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            if (ch >= 2) break; // ステレオ（最大2ch）仕様の厳守

            float* channelData = buffer.getWritePointer(ch);
            float x1 = oldX[ch];

            for (int s = 0; s < numSamples; ++s)
            {
                const float x0 = channelData[s] * drive;
                float saturatedSample = 0.0f;

                const float diff = x0 - x1;

                // 分母がゼロになる（連続するサンプルが同値）場合の破綻を数学的に隔離
                if (std::abs(diff) > 1.0e-5f)
                {
                    // ADAA公式: y = (F(x0) - F(x1)) / (x0 - x1)
                    // F(x) = ln(cosh(x))
                    const float F_x0 = std::log(std::cosh(x0));
                    const float F_x1 = std::log(std::cosh(x1));
                    saturatedSample = (F_x0 - F_x1) / diff;
                }
                else
                {
                    // 差分が極小の場合は中点での tanh(x) で近似して滑らかに補間
                    const float mid = 0.5f * (x0 + x1);
                    saturatedSample = std::tanh(mid);
                }

                x1 = x0; // 状態変数の更新

                // ドライ/ウェットの線形クロスフェード
                channelData[s] = (channelData[s] * (1.0f - mix)) + (saturatedSample * mix);
            }

            oldX[ch] = x1;
        }
    }

    juce::String getName() const override { return "ADAA Saturation"; }

    TargetRoute getTargetRoute() const noexcept override { return route; }
    void setTargetRoute(TargetRoute newRoute) noexcept override { route = newRoute; }

    // パラメータ変更用公開メソッド（UIメインスレッド用）
    void setDrive(float newDrive) noexcept { currentDrive = juce::jlimit(1.0f, 16.0f, newDrive); }
    void setMix(float newMix) noexcept { currentMix = juce::jlimit(0.0f, 1.0f, newMix); }

private:
    double currentSampleRate = 44100.0;
    TargetRoute route = TargetRoute::FullMix;

    float oldX[2] = { 0.0f, 0.0f }; // チャンネルごとの過去入力レジスタ
    float currentDrive = 2.0f;
    float currentMix = 0.5f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ADAA_Saturation)
};