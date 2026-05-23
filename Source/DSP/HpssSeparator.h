#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <atomic>

class HpssSeparator
{
public:
    HpssSeparator(int fftSizeIn);
    void prepare(double sampleRate);

    // HPR-I（多解像度・反復型分離）アルゴリズムによる完全解剖
    void performSeparation(const juce::AudioBuffer<float>& input,
        juce::AudioBuffer<float>& trans,
        juce::AudioBuffer<float>& tonal);

    float getProgress() const { return progress.load(); }
    void resetProgress() { progress.store(0.0f); }

private:
    // 多解像度用デュアルFFTプロセッサ
    juce::dsp::FFT fftLarge; // 4096 (Tonal用)
    juce::dsp::FFT fftSmall; // 256 (Transient用)

    std::vector<float> windowLarge;
    std::vector<float> windowSmall;

    double currentSampleRate = 44100.0;
    std::atomic<float> progress{ 0.0f };

    // 高速化のためのAVX2/スカラーハイブリッド・メディアンフィルタ
    void applyHorizontalMedianLarge(std::vector<std::vector<float>>& map, int windowSize);
    void applyVerticalMedianSmall(std::vector<std::vector<float>>& map, int windowSize);
};