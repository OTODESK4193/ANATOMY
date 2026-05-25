#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <algorithm>
#include <atomic>

/**
 * TransientReplacer (Header-Only DSP Module)
 * * ProcessorやVoiceStateを一切汚さずに、サンプルの差し替えと元の切れ味への同期を実行します。
 */
class TransientReplacer
{
public:
    TransientReplacer() = default;
    ~TransientReplacer() = default;

    /**
     * ブラウザ側から新しいWavが確定された時に呼ばれる安全なデータロード関数
     */
    void loadSample(const juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        const juce::ScopedLock sl(lock);
        replacedBuffer.makeCopyOf(buffer);
        sourceSampleRate = sampleRate;
        hasSample.store(true, std::memory_order_release);
    }

    /**
     * 差し替えを解除して元のTransientに戻すためのクリア関数
     */
    void clearSample()
    {
        const juce::ScopedLock sl(lock);
        replacedBuffer.clear();
        hasSample.store(false, std::memory_order_release);
    }

    bool isLoaded() const noexcept { return hasSample.load(std::memory_order_acquire); }

    /**
     * UIのStartノブからダイレクトにミリ秒単位で再生開始位置を受け取る関数
     */
    void setStartOffsetMs(float offsetMs) noexcept
    {
        startOffsetMs.store(offsetMs, std::memory_order_relaxed);
    }

    /**
     * 核心ロジック：元の長さ・フェードに完全に型抜きしながらリサンプリング発音
     */
    float processSample(double clickReadIndex, double originalPitchRatio, float transScale,
        float clickHoldMs, float clickCurveMs, double hostSampleRate) noexcept
    {
        if (!hasSample.load(std::memory_order_relaxed)) return 0.0f;

        // 音声スレッドの安全確保（サイズチェック）
        const int maxSamples = replacedBuffer.getNumSamples();
        if (maxSamples <= 0) return 0.0f;

        // 1. 現在の再生進捗から、ノートONからの「純粋なホスト経過サンプル数 n」を逆算
        double n = (originalPitchRatio > 0.0) ? (clickReadIndex / originalPitchRatio) : clickReadIndex;

        // 2. 元音のHold / Fadeノブの設定値と寸分の狂いもない「窓関数」をリアルタイム生成
        float wClick = 0.0f;
        double elapsedMs = (n / hostSampleRate) * 1000.0;

        if (elapsedMs < clickHoldMs)
        {
            wClick = 1.0f;
        }
        else if (elapsedMs < (clickHoldMs + clickCurveMs))
        {
            if (clickCurveMs > 0.0f)
            {
                double fadePhase = ((elapsedMs - clickHoldMs) / clickCurveMs) * (juce::MathConstants<double>::pi * 0.5);
                double cosVal = std::cos(fadePhase);
                wClick = static_cast<float>(cosVal * cosVal);
            }
        }
        else
        {
            return 0.0f; // 元音のTransientの寿命が尽きたら100%完全消音（ボケの根絶）
        }

        // 3. 差し替えサンプルの再生位置を計算（Start位置オフセット ＋ 経過時間 × ピッチ比）
        double offsetSamples = (startOffsetMs.load(std::memory_order_relaxed) / 1000.0) * sourceSampleRate;
        double speedRatio = sourceSampleRate / hostSampleRate;
        double replacedSrcPos = offsetSamples + (n * speedRatio * transScale);

        if (replacedSrcPos < 0.0) replacedSrcPos = 0.0;
        if (replacedSrcPos >= static_cast<double>(maxSamples - 1)) return 0.0f;

        // 4. 高品質な線形補間
        int idx0 = static_cast<int>(replacedSrcPos);
        int idx1 = std::min(idx0 + 1, maxSamples - 1);
        float frac = static_cast<float>(replacedSrcPos - idx0);

        const float* src = replacedBuffer.getReadPointer(0);
        float sampleVal = src[idx0] + frac * (src[idx1] - src[idx0]);

        // 元音の切れ味（窓）を正確に掛け合わせて出力
        return sampleVal * wClick;
    }

private:
    juce::CriticalSection lock;
    juce::AudioBuffer<float> replacedBuffer;
    double sourceSampleRate = 44100.0;

    std::atomic<float> startOffsetMs{ 0.0f };
    std::atomic<bool> hasSample{ false };
};