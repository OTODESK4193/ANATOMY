#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <algorithm>
#include <atomic>

inline float calculateFadeGain(float normalizedProgress, float tension) noexcept
{
    float p = juce::jlimit(0.0f, 1.0f, normalizedProgress);
    if (std::abs(tension) < 0.005f)
        return p;

    // tension > 0: 上ドラッグで急峻 (上に凸 / 対数カーブ / 立ち上がり急激)
    if (tension > 0.0f)
    {
        float expVal = 1.0f + tension * 4.0f;
        return 1.0f - std::pow(1.0f - p, expVal);
    }
    // tension < 0: 下ドラッグでなだらか (下に凹 / 指数カーブ / 立ち上がり緩やか)
    else
    {
        float expVal = 1.0f - tension * 4.0f;
        return std::pow(p, expVal);
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
    void setPitchSemitones(float st) noexcept { pitchSemitones.store(st, std::memory_order_relaxed); }
    void setMixGainDb(float gainDb) noexcept { mixGainDb.store(gainDb, std::memory_order_relaxed); }
    void setClickHoldMs(float holdMs) noexcept { clickHoldMs.store(holdMs, std::memory_order_relaxed); }

    void setFadeInMs(float ms) noexcept { fadeInMs.store(ms, std::memory_order_relaxed); }
    void setFadeOutMs(float ms) noexcept { fadeOutMs.store(ms, std::memory_order_relaxed); }
    void setFadeInTension(float t) noexcept { fadeInTension.store(t, std::memory_order_relaxed); }
    void setFadeOutTension(float t) noexcept { fadeOutTension.store(t, std::memory_order_relaxed); }

    float getFadeInMs() const noexcept { return fadeInMs.load(std::memory_order_relaxed); }
    float getFadeOutMs() const noexcept { return fadeOutMs.load(std::memory_order_relaxed); }
    float getFadeInTension() const noexcept { return fadeInTension.load(std::memory_order_relaxed); }
    float getFadeOutTension() const noexcept { return fadeOutTension.load(std::memory_order_relaxed); }

    void setFade(float inMs, float outMs, float inTension, float outTension) noexcept
    {
        fadeInMs.store(inMs, std::memory_order_relaxed);
        fadeOutMs.store(outMs, std::memory_order_relaxed);
        fadeInTension.store(inTension, std::memory_order_relaxed);
        fadeOutTension.store(outTension, std::memory_order_relaxed);
    }

    void getFade(float& inMs, float& outMs, float& inTension, float& outTension) const noexcept
    {
        inMs = fadeInMs.load(std::memory_order_relaxed);
        outMs = fadeOutMs.load(std::memory_order_relaxed);
        inTension = fadeInTension.load(std::memory_order_relaxed);
        outTension = fadeOutTension.load(std::memory_order_relaxed);
    }

    float getStartOffsetMs() const noexcept { return startOffsetMs.load(std::memory_order_relaxed); }
    float getEndOffsetMs() const noexcept { return endOffsetMs.load(std::memory_order_relaxed); }

    void reset() noexcept {}

    float processSample(double clickReadIndex, double /*pitchRatio*/, float transScale,
                        float clickHold, float /*clickCurve*/, double /*hostSampleRate*/, int soloMode) noexcept
    {
        if (soloMode == 2 || !hasSample.load(std::memory_order_relaxed))
            return 0.0f;

        const int maxSamples = replacedBuffer.getNumSamples();
        if (maxSamples == 0) return 0.0f;

        double startSmpl = (static_cast<double>(startOffsetMs.load(std::memory_order_relaxed)) / 1000.0) * sourceSampleRate;
        double endSmpl = (static_cast<double>(endOffsetMs.load(std::memory_order_relaxed)) / 1000.0) * sourceSampleRate;
        double holdSmpl = (static_cast<double>(clickHold) / 1000.0) * sourceSampleRate;

        double effectiveEnd = (holdSmpl > 0.0) ? std::min(endSmpl, startSmpl + holdSmpl) : endSmpl;
        double readPos = startSmpl + clickReadIndex;

        if (readPos >= effectiveEnd || readPos >= maxSamples - 1)
            return 0.0f;

        int index0 = static_cast<int>(readPos);
        int index1 = std::min(index0 + 1, maxSamples - 1);
        float frac = static_cast<float>(readPos - index0);

        // フェードゲイン計算
        double fInSmpl = (static_cast<double>(fadeInMs.load(std::memory_order_relaxed)) / 1000.0) * sourceSampleRate;
        double fOutSmpl = (static_cast<double>(fadeOutMs.load(std::memory_order_relaxed)) / 1000.0) * sourceSampleRate;

        float fadeGain = 1.0f;
        if (fInSmpl > 1.0 && clickReadIndex < fInSmpl)
        {
            float prog = static_cast<float>(clickReadIndex / fInSmpl);
            fadeGain *= calculateFadeGain(prog, fadeInTension.load(std::memory_order_relaxed));
        }
        double remSmpl = effectiveEnd - readPos;
        if (fOutSmpl > 1.0 && remSmpl < fOutSmpl)
        {
            float prog = static_cast<float>(remSmpl / fOutSmpl);
            fadeGain *= calculateFadeGain(prog, fadeOutTension.load(std::memory_order_relaxed));
        }

        const float* src = replacedBuffer.getReadPointer(0);
        float raw = (src[index0] * (1.0f - frac) + src[index1] * frac) * transScale * fadeGain;
        return raw;
    }

private:
    juce::CriticalSection lock;
    juce::AudioBuffer<float> replacedBuffer;
    double sourceSampleRate = 44100.0;

    std::atomic<bool> hasSample { false };
    std::atomic<float> startOffsetMs { 0.0f };
    std::atomic<float> endOffsetMs { 0.0f };
    std::atomic<float> pitchSemitones { 0.0f };
    std::atomic<float> mixGainDb { 0.0f };
    std::atomic<float> clickHoldMs { 10.0f };

    std::atomic<float> fadeInMs { 0.0f };
    std::atomic<float> fadeOutMs { 0.0f };
    std::atomic<float> fadeInTension { 0.0f };
    std::atomic<float> fadeOutTension { 0.0f };
};