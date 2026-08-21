#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <atomic>
#include "TransientReplacer.h" // calculateFadeGain ヘルパーを共有

class TonalReplacer
{
public:
    TonalReplacer() = default;
    ~TonalReplacer() = default;

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

    void reset() noexcept { tapAPhase = 0.5f; }

    float processSample(double sustainReadIndex, double originalPitchRatio, float tonalScale,
        float clickHoldMs, float clickCurveMs, double hostSampleRate, int soloMode) noexcept
    {
        if (soloMode == 1 || !hasSample.load(std::memory_order_relaxed))
            return 0.0f;

        const int maxSamples = replacedBuffer.getNumSamples();
        if (maxSamples <= 0) return 0.0f;

        double n = (originalPitchRatio > 0.0) ? (sustainReadIndex / originalPitchRatio) : sustainReadIndex;
        if (n < 0.0) return 0.0f;

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

        const float* src = replacedBuffer.getReadPointer(0);
        double startMs = startOffsetMs.load(std::memory_order_relaxed);
        double endMs = endOffsetMs.load(std::memory_order_relaxed);
        double offsetSamples = (startMs / 1000.0) * sourceSampleRate;
        double speedRatio = sourceSampleRate / hostSampleRate;
        double baseTimelinePos = offsetSamples + (n * speedRatio);

        auto readSourceInterpolated = [src, maxSamples, this, endMs](double timelinePos, float delay) noexcept -> float
            {
                double srcPos = timelinePos - delay;

                // --- END位置によるクリップ判定 ---
                double endSamples = (endMs / 1000.0) * sourceSampleRate;
                if (srcPos >= endSamples || srcPos >= static_cast<double>(maxSamples - 1))
                    return 0.0f;

                if (srcPos < 0.0) srcPos = 0.0;

                int idx0 = static_cast<int>(srcPos);
                float frac = static_cast<float>(srcPos - idx0);

                int idxM1 = std::max(0, idx0 - 1);
                int idx1  = std::min(idx0 + 1, maxSamples - 1);
                int idx2  = std::min(idx0 + 2, maxSamples - 1);

                const float ym1 = src[idxM1], y0 = src[idx0], y1 = src[idx1], y2 = src[idx2];
                const float c0 = y0;
                const float c1 = 0.5f * (y1 - ym1);
                const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
                const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
                return ((c3 * frac + c2) * frac + c1) * frac + c0;
            };

        float sampleA = readSourceInterpolated(baseTimelinePos, delayA);
        float sampleB = readSourceInterpolated(baseTimelinePos, delayB);

        // --- Start/End フェードイン・フェードアウト計算 ---
        float fadeGain = 1.0f;
        double currentTimelineMs = (baseTimelinePos / sourceSampleRate) * 1000.0;
        double fromStartMs = currentTimelineMs - startMs;
        double toEndMs = endMs - currentTimelineMs;

        float fInMs = fadeInMs.load(std::memory_order_relaxed);
        float fOutMs = fadeOutMs.load(std::memory_order_relaxed);

        if (fInMs > 0.001f && fromStartMs < fInMs)
        {
            float prog = static_cast<float>(fromStartMs / fInMs);
            fadeGain *= calculateFadeGain(prog, fadeInTension.load(std::memory_order_relaxed));
        }
        else if (fromStartMs < 1.5)
        {
            fadeGain *= static_cast<float>(std::max(0.0, fromStartMs / 1.5));
        }

        if (fOutMs > 0.001f && toEndMs < fOutMs)
        {
            float prog = static_cast<float>(toEndMs / fOutMs);
            fadeGain *= calculateFadeGain(prog, fadeOutTension.load(std::memory_order_relaxed));
        }
        else if (toEndMs < 1.5)
        {
            fadeGain *= static_cast<float>(std::max(0.0, toEndMs / 1.5));
        }

        return ((sampleA * weightA) + (sampleB * weightB)) * fadeGain;
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
    float tapAPhase = 0.5f;
    std::atomic<bool> hasSample{ false };
};