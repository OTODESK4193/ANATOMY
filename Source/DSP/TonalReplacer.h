#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <atomic>

/**
 * TonalReplacer (Header-Only DSP Module)
 * * 1. ノートONのGate/Releaseに完全追従
 * 2. 指定したSTART位置から、ワープのない「Centered型・連続位相グラニュラー」を駆動
 * 3. 元音のHPSSフェードイン窓を完全移植してアタックのハミ出しを根絶
 */
class TonalReplacer
{
public:
    TonalReplacer() = default;
    ~TonalReplacer() = default;

    /**
     * ブラウザ側から新しいWavが確定された時に呼ばれるデータロード関数
     */
    void loadSample(const juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        const juce::ScopedLock sl(lock);
        replacedBuffer.makeCopyOf(buffer);
        sourceSampleRate = sampleRate;
        hasSample.store(true, std::memory_order_release);
    }

    /**
     * 差し替えを解除して元のTonal（原音サステイン）に戻すためのクリア関数
     */
    void clearSample()
    {
        const juce::ScopedLock sl(lock);
        replacedBuffer.clear();
        hasSample.store(false, std::memory_order_release);
    }

    bool isLoaded() const noexcept { return hasSample.load(std::memory_order_acquire); }

    /**
     * UIのStartノブからミリ秒単位で再生開始位置を受け取る関数
     */
    void setStartOffsetMs(float offsetMs) noexcept
    {
        startOffsetMs.store(offsetMs, std::memory_order_relaxed);
    }

    /**
     * ノートON（トリガー）の瞬間に呼ばれるグラニュラー位相リセット【接着剤】
     */
    void reset() noexcept
    {
        tapAPhase = 0.5f; // 開始時は遅延ゼロの完全同期ポイントにロック
    }

    /**
     * 核心ロジック：Gate/Releaseに追従しながら、Start位置基準で連続グラニュラー伸縮を実行
     */
    float processSample(double sustainReadIndex, double originalPitchRatio, float tonalScale,
        float clickHoldMs, float clickCurveMs, double hostSampleRate) noexcept
    {
        if (!hasSample.load(std::memory_order_relaxed)) return 0.0f;

        const int maxSamples = replacedBuffer.getNumSamples();
        if (maxSamples <= 0) return 0.0f;

        // 1. ノートONからのホスト純粋経過サンプル数 n を逆算
        double n = (originalPitchRatio > 0.0) ? (sustainReadIndex / originalPitchRatio) : sustainReadIndex;

        // 2. 元音のHPSS分離と完全に同期するサステイン・フェードイン窓（wSustain）のリアルタイム移植
        float wSustain = 1.0f;
        double elapsedMs = (n / hostSampleRate) * 1000.0;

        if (elapsedMs < clickHoldMs)
        {
            wSustain = 0.0f;
        }
        else if (elapsedMs < (clickHoldMs + clickCurveMs))
        {
            if (clickCurveMs > 0.0f)
            {
                double fadePhase = ((elapsedMs - clickHoldMs) / clickCurveMs) * (juce::MathConstants<double>::pi * 0.5);
                double cosVal = std::cos(fadePhase);
                wSustain = 1.0f - static_cast<float>(cosVal * cosVal);
            }
        }

        // 3. Tonal専用：ポインタをワープさせない「40ms固定・無限連続走行グラニュラー」
        float maxDelaySamples = static_cast<float>((40.0f / 1000.0f) * hostSampleRate);
        if (maxDelaySamples < 64.0f) maxDelaySamples = 64.0f;

        float phaseIncrement = (1.0f - tonalScale) / maxDelaySamples;
        tapAPhase += phaseIncrement;

        while (tapAPhase >= 1.0f) tapAPhase -= 1.0f;
        while (tapAPhase < 0.0f)  tapAPhase += 1.0f;

        float tapBPhase = tapAPhase + 0.5f;
        if (tapBPhase >= 1.0f) tapBPhase -= 1.0f;

        float delayA = (tapAPhase - 0.5f) * maxDelaySamples;
        float delayB = (tapBPhase - 0.5f) * maxDelaySamples;

        auto getHannWeight = [](float phase) noexcept -> float {
            return 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * phase));
            };

        float weightA = getHannWeight(tapAPhase);
        float weightB = getHannWeight(tapBPhase);

        // 4. 【核心】ユーザーが指定したSTART位置を「不動の故郷（基準）」とし、そこから等速時間軸を展開
        const float* src = replacedBuffer.getReadPointer(0);
        double offsetSamples = (startOffsetMs.load(std::memory_order_relaxed) / 1000.0) * sourceSampleRate;
        double speedRatio = sourceSampleRate / hostSampleRate;

        // 仮想的な再生の基準線（Start位置 ＋ ノートONからの経過時間）
        double baseTimelinePos = offsetSamples + (n * speedRatio);

        auto readSourceInterpolated = [src, maxSamples](double timelinePos, float delay) noexcept -> float
            {
                double srcPos = timelinePos - delay;

                if (srcPos < 0.0) srcPos = 0.0;
                if (srcPos >= static_cast<double>(maxSamples - 1)) return 0.0f;

                int idx0 = static_cast<int>(srcPos);
                int idx1 = std::min(idx0 + 1, maxSamples - 1);
                float frac = static_cast<float>(srcPos - idx0);

                return src[idx0] + frac * (src[idx1] - src[idx0]);
            };

        float sampleA = readSourceInterpolated(baseTimelinePos, delayA);
        float sampleB = readSourceInterpolated(baseTimelinePos, delayB);

        // クロスフェード結合し、元のフェードイン窓を乗算して出力（リリース音量はプロセッサ側が追従）
        return ((sampleA * weightA) + (sampleB * weightB)) * wSustain;
    }

private:
    juce::CriticalSection lock;
    juce::AudioBuffer<float> replacedBuffer;
    double sourceSampleRate = 44100.0;

    std::atomic<float> startOffsetMs{ 0.0f };
    std::atomic<bool> hasSample{ false };
    float tapAPhase = 0.5f;
};