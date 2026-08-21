#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <algorithm>
#include <atomic>

inline float calculateFadeGain(float normalizedProgress, float tension) noexcept
{
    float p = juce::jlimit(0.0f, 1.0f, normalizedProgress);
    if (std::abs(tension) < 0.01f)
        return p;
    if (tension > 0.0f)
    {
        float expVal = 1.0f + tension * 3.0f;
        return std::pow(p, expVal);
    }
    else
    {
        float expVal = 1.0f - tension * 3.0f;
        return 1.0f - std::pow(1.0f - p, expVal);
    }
}

/**
 * TransientReplacer
 * ワンショット過渡置換プロセッサー（インタラクティブFadeIn/FadeOut対応版）
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

        float durationMs = (static_cast<float>(buffer.getNumSamples()) / static_cast<float>(sampleRate)) * 1000.0f;
        endOffsetMs.store(durationMs, std::memory_order_release);
        fadeInMs.store(0.0f, std::memory_order_release);
        fadeOutMs.store(0.0f, std::memory_order_release);

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
    void setFadeInMs(float ms) noexcept { fadeInMs.store(ms, std::memory_order_relaxed); }
    void setFadeOutMs(float ms) noexcept { fadeOutMs.store(ms, std::memory_order_relaxed); }
    void setFadeInTension(float t) noexcept { fadeInTension.store(t, std::memory_order_relaxed); }
    void setFadeOutTension(float t) noexcept { fadeOutTension.store(t, std::memory_order_relaxed); }

    float getFadeInMs() const noexcept { return fadeInMs.load(std::memory_order_relaxed); }
    float getFadeOutMs() const noexcept { return fadeOutMs.load(std::memory_order_relaxed); }
    float getFadeInTension() const noexcept { return fadeInTension.load(std::memory_order_relaxed); }
    float getFadeOutTension() const noexcept { return fadeOutTension.load(std::memory_order_relaxed); }

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

        double startMs = startOffsetMs.load(std::memory_order_relaxed);
        double endMs = endOffsetMs.load(std::memory_order_relaxed);
        double offsetSamples = (startMs / 1000.0) * sourceSampleRate;
        double speedRatio = sourceSampleRate / hostSampleRate;
        double replacedSrcPos = offsetSamples + (n * speedRatio * transScale);

        double endSamples = (endMs / 1000.0) * sourceSampleRate;
        if (replacedSrcPos >= endSamples || replacedSrcPos >= static_cast<double>(maxSamples - 1))
            return 0.0f;

        if (replacedSrcPos < 0.0) replacedSrcPos = 0.0;

        // --- Start/End フェードイン・フェードアウト計算 ---
        float fadeGain = 1.0f;
        double currentTimelineMs = (replacedSrcPos / sourceSampleRate) * 1000.0;
        double fromStartMs = currentTimelineMs - startMs;
        double toEndMs = endMs - currentTimelineMs;

        float fInMs = fadeInMs.load(std::memory_order_relaxed);
        float fOutMs = fadeOutMs.load(std::memory_order_relaxed);

        if (fInMs > 0.001f && fromStartMs < fInMs)
        {
            float prog = static_cast<float>(fromStartMs / fInMs);
            fadeGain *= calculateFadeGain(prog, fadeInTension.load(std::memory_order_relaxed));
        }

        if (fOutMs > 0.001f && toEndMs < fOutMs)
        {
            float prog = static_cast<float>(toEndMs / fOutMs);
            fadeGain *= calculateFadeGain(prog, fadeOutTension.load(std::memory_order_relaxed));
        }

        int idx0 = static_cast<int>(replacedSrcPos);
        int idx1 = std::min(idx0 + 1, maxSamples - 1);
        float frac = static_cast<float>(replacedSrcPos - idx0);

        const float* src = replacedBuffer.getReadPointer(0);
        return (src[idx0] + frac * (src[idx1] - src[idx0])) * wClick * fadeGain;
    }

private:
    juce::CriticalSection lock;
    juce::AudioBuffer<float> replacedBuffer;
    double sourceSampleRate = 44100.0;
    std::atomic<float> startOffsetMs{ 0.0f };
    std::atomic<float> endOffsetMs{ 0.0f };
    std::atomic<float> fadeInMs{ 0.0f };
    std::atomic<float> fadeOutMs{ 0.0f };
    std::atomic<float> fadeInTension{ 0.0f };
    std::atomic<float> fadeOutTension{ 0.0f };
    std::atomic<bool> hasSample{ false };
};