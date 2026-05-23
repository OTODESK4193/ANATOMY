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

    // 重い分離処理を実行
    void performSeparation(const juce::AudioBuffer<float>& input,
        juce::AudioBuffer<float>& trans,
        juce::AudioBuffer<float>& tonal);

    // UIやProcessorから現在の進捗（0.0f 〜 1.0f）を取得するためのスレッド安全なゲッター
    float getProgress() const { return progress.load(); }

    // 進捗率を外部から強制リセットするためのメソッド
    void resetProgress() { progress.store(0.0f); }

private:
    int fftSize;
    juce::dsp::FFT fft;
    std::vector<float> window;
    double currentSampleRate = 44100.0;
    int vertWindow = 3;
    int horizWindow = 3;

    // スレッド間で安全に共有される進捗率フラグ
    std::atomic<float> progress{ 0.0f };

    void applyWindow(const juce::AudioBuffer<float>& input, int offset, std::vector<float>& dest);
    void applyHorizontalMedian(std::vector<std::vector<float>>& map, int windowSize);
    void applyVerticalMedian(std::vector<std::vector<float>>& map, int windowSize);
};