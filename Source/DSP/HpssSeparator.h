#pragma once
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <algorithm>
#include <cmath>

class HpssSeparator {
public:
    HpssSeparator(int fftSizeIn);
    void prepare(double sampleRate);
    void performSeparation(const juce::AudioBuffer<float>& input,
        juce::AudioBuffer<float>& trans,
        juce::AudioBuffer<float>& tonal);

private:
    void applyWindow(const juce::AudioBuffer<float>& input, int offset, std::vector<float>& dest);
    void applyHorizontalMedian(std::vector<std::vector<float>>& map, int windowSize);
    void applyVerticalMedian(std::vector<std::vector<float>>& map, int windowSize);

    int fftSize;
    double currentSampleRate;
    juce::dsp::FFT fft;
    std::vector<float> window;
    int vertWindow, horizWindow;
};