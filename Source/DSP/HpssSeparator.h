#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <atomic>
#include "TransientBoundaryDetector.h"

class HpssSeparator
{
public:
    HpssSeparator(int fftSizeIn);
    void prepare(double sampleRate);

    // 絶対先頭基準 HOLD + SMOOTH FADE モデルの分離メソッド
    void performSeparation(const juce::AudioBuffer<float>& input,
        juce::AudioBuffer<float>& trans,
        juce::AudioBuffer<float>& tonal,
        float sensitivity,
        float clickHoldMs,
        float sustainFadeMs,
        float lookAheadMs,
        juce::Thread* callingThread);

    float getProgress() const { return progress.load(); }
    void resetProgress() { progress.store(0.0f); }

private:
    double currentSampleRate = 44100.0;
    std::atomic<float> progress{ 0.0f };
    TransientBoundaryDetector detector;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HpssSeparator)
};