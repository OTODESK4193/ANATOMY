#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <atomic>

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

    void reset() noexcept { tapAPhase = 0.5f; }

    float processSample(double sustainReadIndex, double originalPitchRatio, float tonalScale,
        float clickHoldMs, float clickCurveMs, double hostSampleRate, int soloMode) noexcept
    {
        if (soloMode == 1 || !hasSample.load(std::memory_order_relaxed))
            return 0.0f;

        const int maxSamples = replacedBuffer.getNumSamples();
        if (maxSamples <= 0) return 0.0f;

        double n = (originalPitchRatio > 0.0) ? (sustainReadIndex / originalPitchRatio) : sustainReadIndex;

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
        double offsetSamples = (startOffsetMs.load(std::memory_order_relaxed) / 1000.0) * sourceSampleRate;
        double speedRatio = sourceSampleRate / hostSampleRate;
        double baseTimelinePos = offsetSamples + (n * speedRatio);

        auto readSourceInterpolated = [src, maxSamples, this](double timelinePos, float delay) noexcept -> float
            {
                double srcPos = timelinePos - delay;

                // --- END位置によるクリップ判定 ---
                double endSamples = (endOffsetMs.load(std::memory_order_relaxed) / 1000.0) * sourceSampleRate;
                if (srcPos >= endSamples || srcPos >= static_cast<double>(maxSamples - 1))
                    return 0.0f;

                if (srcPos < 0.0) srcPos = 0.0;

                int idx0 = static_cast<int>(srcPos);
                int idx1 = std::min(idx0 + 1, maxSamples - 1);
                float frac = static_cast<float>(srcPos - idx0);

                return src[idx0] + frac * (src[idx1] - src[idx0]);
            };

        float sampleA = readSourceInterpolated(baseTimelinePos, delayA);
        float sampleB = readSourceInterpolated(baseTimelinePos, delayB);

        return ((sampleA * weightA) + (sampleB * weightB)) * wSustain;
    }

private:
    juce::CriticalSection lock;
    juce::AudioBuffer<float> replacedBuffer;
    double sourceSampleRate = 44100.0;
    std::atomic<float> startOffsetMs{ 0.0f };
    std::atomic<float> endOffsetMs{ 0.0f };
    float tapAPhase = 0.5f;
    std::atomic<bool> hasSample{ false };
};