#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <algorithm>
#include <atomic>

/**
 * TransientReplacer (インターフェース完全適合版)
 * PluginProcessorからのreset()命令に完全同調するためのダミーreset関数を敷設した
 * ワンショット過渡置換プロセッサー。
 */
class TransientReplacer
{
public:
    TransientReplacer() = default;
    ~TransientReplacer() = default;

    void loadSample(const juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        const juce::ScopedLock sl(lock);
        replacedBuffer.makeCopyOf(buffer);
        sourceSampleRate = sampleRate;

        // 初期状態はファイルの末尾をフルで指定
        float durationMs = (static_cast<float>(buffer.getNumSamples()) / static_cast<float>(sampleRate)) * 1000.0f;
        endOffsetMs.store(durationMs, std::memory_order_release);

        hasSample.store(true, std::memory_order_release);
    }

    void clearSample()
    {
        const juce::ScopedLock sl(lock);
        replacedBuffer.clear();
        hasSample.store(false, std::memory_order_release);
    }

    bool isLoaded() const noexcept { return hasSample.load(std::memory_order_acquire); }
    double getSourceSampleRate() const noexcept { return sourceSampleRate; }

    void setStartOffsetMs(float offsetMs) noexcept { startOffsetMs.store(offsetMs, std::memory_order_relaxed); }
    void setEndOffsetMs(float offsetMs) noexcept { endOffsetMs.store(offsetMs, std::memory_order_relaxed); }

    // 💥【新設】PluginProcessorからの呼び出しとインターフェースを統一するための安全なリセット関数
    void reset() noexcept {}

    float processSample(double clickReadIndex, double originalPitchRatio, float transScale,
        float clickHoldMs, float clickCurveMs, double hostSampleRate, int soloMode) noexcept
    {
        if (soloMode == 2 || !hasSample.load(std::memory_order_relaxed))
            return 0.0f;

        const int maxSamples = replacedBuffer.getNumSamples();
        if (maxSamples <= 0) return 0.0f;

        double n = (originalPitchRatio > 0.0) ? (clickReadIndex / originalPitchRatio) : clickReadIndex;

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
            return 0.0f;
        }

        double offsetSamples = (startOffsetMs.load(std::memory_order_relaxed) / 1000.0) * sourceSampleRate;
        double speedRatio = sourceSampleRate / hostSampleRate;
        double replacedSrcPos = offsetSamples + (n * speedRatio * transScale);

        // --- END位置によるクリップ判定 ---
        double endSamples = (endOffsetMs.load(std::memory_order_relaxed) / 1000.0) * sourceSampleRate;
        if (replacedSrcPos >= endSamples || replacedSrcPos >= static_cast<double>(maxSamples - 1))
            return 0.0f;

        if (replacedSrcPos < 0.0) replacedSrcPos = 0.0;

        int idx0 = static_cast<int>(replacedSrcPos);
        int idx1 = std::min(idx0 + 1, maxSamples - 1);
        float frac = static_cast<float>(replacedSrcPos - idx0);

        const float* src = replacedBuffer.getReadPointer(0);
        return (src[idx0] + frac * (src[idx1] - src[idx0])) * wClick;
    }

private:
    juce::CriticalSection lock;
    juce::AudioBuffer<float> replacedBuffer;
    double sourceSampleRate = 44100.0;
    std::atomic<float> startOffsetMs{ 0.0f };
    std::atomic<float> endOffsetMs{ 0.0f };
    std::atomic<bool> hasSample{ false };
};