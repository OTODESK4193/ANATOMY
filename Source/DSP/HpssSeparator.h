#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include "TransientBoundaryDetector.h"

class HpssSeparator
{
public:
    HpssSeparator(int fftSizeIn);
    void prepare(double sampleRate);

    // 時間領域相補クロスフェードスプライシング（HPR-I選択案2に完全準拠）
    void performSeparation(const juce::AudioBuffer<float>& input,
        juce::AudioBuffer<float>& trans,
        juce::AudioBuffer<float>& tonal);

    float getProgress() const { return progress.load(); }
    void resetProgress() { progress.store(0.0f); }

private:
    double currentSampleRate = 44100.0;
    std::atomic<float> progress{ 0.0f };
    TransientBoundaryDetector detector;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HpssSeparator)
};