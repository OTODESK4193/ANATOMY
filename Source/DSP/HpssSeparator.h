#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <atomic>
#include <cmath>

/**
 * HpssSeparator (cos² Crossfade Edition)
 *
 * タイムドメインの cos² クロスフェードによるトランジェント/トーナル分離。
 *
 * 分離ロジック:
 *   holdMs 区間:  trans = input,  tonal = 0
 *   fadeMs 区間:  trans = input * cos²(θ),  tonal = input * sin²(θ)
 *   それ以降:     trans = 0,  tonal = input
 *
 * cos²(θ) + sin²(θ) = 1 が常に保証されるため、
 * trans + tonal = input（パーフェクトリコンストラクション）。
 */
class HpssSeparator
{
public:
    HpssSeparator(int /*baseFftSize*/) {}

    void prepare(double sampleRate)
    {
        currentSampleRate = sampleRate;
    }

    void performSeparation(const juce::AudioBuffer<float>& input,
        juce::AudioBuffer<float>& trans,
        juce::AudioBuffer<float>& tonal,
        float clickHoldMs,
        float sustainFadeMs,
        juce::Thread* callingThread);

    float getProgress() const { return progress.load(); }
    void resetProgress() { progress.store(0.0f); }

private:
    double currentSampleRate = 44100.0;
    std::atomic<float> progress{ 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HpssSeparator)
};
