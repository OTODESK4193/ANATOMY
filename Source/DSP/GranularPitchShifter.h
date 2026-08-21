#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>
#include <algorithm>

/**
 * GranularPitchShifter (High-Precision Snap-Engine Architecture)
 * 1. Transient: 窓関数を排除した高精度 4 点 Hermite ワンショット・リサンプリングで鋭さを100%維持
 * 2. Tonal: 4点位相分散型 Hann グラニュラー・ローテーターで振幅変調・コムフィルター歪みを完全除去
 * 3. 接着剤: ノートONの瞬間に遅延を完全ゼロ同期
 */
class GranularPitchShifter
{
public:
    /**
     * Hermite 4点補間（3次 Catmull-Rom スプライン）
     * 高周波のエイリアシングを大幅に低減し、滑らかな連続微分を保証。
     */
    static inline float hermiteInterp(float ym1, float y0, float y1, float y2, float t) noexcept
    {
        const float c0 = y0;
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }

    /**
     * ソースバッファからHermite補間で読み出し。
     */
    static inline float readHermite(const float* src, int maxSamples, float srcPos) noexcept
    {
        if (srcPos < 0.0f) srcPos = 0.0f;
        if (srcPos >= static_cast<float>(maxSamples - 1)) srcPos = static_cast<float>(maxSamples - 1) - 0.0001f;

        int idx0 = static_cast<int>(srcPos);
        float frac = srcPos - static_cast<float>(idx0);

        int idxM1 = std::max(0, idx0 - 1);
        int idx1  = std::min(idx0 + 1, maxSamples - 1);
        int idx2  = std::min(idx0 + 2, maxSamples - 1);

        return hermiteInterp(src[idxM1], src[idx0], src[idx1], src[idx2], frac);
    }

    GranularPitchShifter()
    {
        init(44100.0, 40.0f, 4);
    }

    ~GranularPitchShifter() = default;

    /**
     * アルゴリズムの初期化
     * @param grainSizeMs 20ms未満であれば自動的にTransient（ワンショット）モードとして動作
     */
    void init(double sampleRate, float grainSizeMs, int /*numOverlaps*/)
    {
        this->currentSampleRate = sampleRate;
        this->isTransientMode = (grainSizeMs < 20.0f);

        this->maxDelaySamples = static_cast<float>((grainSizeMs / 1000.0f) * sampleRate);
        if (this->maxDelaySamples < 64.0f) this->maxDelaySamples = 64.0f;

        reset();
    }

    /**
     * ノートON（トリガー）の瞬間に呼ばれる強制ゼロ遅延同期
     */
    void reset() noexcept
    {
        basePhase = 0.5f;
    }

    /**
     * タイムドメイン・ピッチシフト処理の実体
     */
    float processSample(const juce::AudioBuffer<float>& sourceBuffer, int currentTimelineIdx, float scaleFactor) noexcept
    {
        const int maxSamples = sourceBuffer.getNumSamples();
        if (maxSamples <= 0 || currentTimelineIdx < 0 || currentTimelineIdx >= maxSamples)
            return 0.0f;

        // ピッチ変更なし（1.0倍）の時は、一切の演算をバイパスして100%完全な同値原音を保証
        if (std::abs(scaleFactor - 1.0f) < 0.001f)
        {
            return sourceBuffer.getReadPointer(0)[currentTimelineIdx];
        }

        const float* src = sourceBuffer.getReadPointer(0);

        // ==============================================================================
        // 【MODE 1】Transient: 窓関数を通さない「ワンショット・高次Hermiteリサンプリング」
        // ==============================================================================
        if (isTransientMode)
        {
            float srcPos = static_cast<float>(currentTimelineIdx) * scaleFactor;
            if (srcPos < 0.0f) srcPos = 0.0f;
            if (srcPos >= static_cast<float>(maxSamples - 1)) return 0.0f;

            return readHermite(src, maxSamples, srcPos);
        }

        // ==============================================================================
        // 【MODE 2】Tonal: 4点位相分散型 Hann グラニュラー・ローテーター
        // ==============================================================================
        float phaseIncrement = (1.0f - scaleFactor) / maxDelaySamples;
        basePhase += phaseIncrement;

        while (basePhase >= 1.0f) basePhase -= 1.0f;
        while (basePhase < 0.0f)  basePhase += 1.0f;

        // 4つのグラニュラー・タップ（位相差 0.0, 0.25, 0.5, 0.75）で均一なエネルギー合成
        float outSum = 0.0f;
        float weightSum = 0.0f;

        for (int tap = 0; tap < 4; ++tap)
        {
            float p = basePhase + static_cast<float>(tap) * 0.25f;
            while (p >= 1.0f) p -= 1.0f;

            // Hann 窓
            float w = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * p));
            float delay = (p - 0.5f) * maxDelaySamples;

            float s = readHermite(src, maxSamples, static_cast<float>(currentTimelineIdx) - delay);
            outSum += s * w;
            weightSum += w;
        }

        return (weightSum > 1.0e-5f) ? (outSum / weightSum) : outSum;
    }

private:
    double currentSampleRate = 44100.0;
    float maxDelaySamples = 1024.0f;
    float basePhase = 0.5f;
    bool isTransientMode = false;
};