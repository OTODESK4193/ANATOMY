#include "HpssSeparator.h"

HpssSeparator::HpssSeparator(int fftSizeIn) : fftSize(fftSizeIn), fft(static_cast<int>(log2(fftSizeIn))) {
    window.resize(fftSize);
    juce::dsp::WindowingFunction<float>::fillWindowingTables(window.data(), fftSize, juce::dsp::WindowingFunction<float>::hann);
}

void HpssSeparator::prepare(double sampleRate) {
    currentSampleRate = sampleRate;
    // SRに応じてフィルタサイズを動的調整 (20ms分を基準)
    vertWindow = static_cast<int>(0.02 * sampleRate);
    horizWindow = static_cast<int>(0.02 * sampleRate);
}

void HpssSeparator::performSeparation(const juce::AudioBuffer<float>& input,
    juce::AudioBuffer<float>& trans,
    juce::AudioBuffer<float>& tonal)
{
    const int numSamples = input.getNumSamples();
    const int hopSize = fftSize / 2;
    const int numFrames = (numSamples - fftSize) / hopSize;

    std::vector<float> fftData(fftSize * 2, 0.0f);
    std::vector<std::vector<float>> magnitudeMap(numFrames, std::vector<float>(fftSize / 2 + 1));

    for (int f = 0; f < numFrames; ++f) {
        applyWindow(input, f * hopSize, fftData);
        fft.performFrequencyOnlyForwardTransform(fftData.data());

        // SIMD最適化: FloatVectorOperationsを使用して高速コピー
        juce::FloatVectorOperations::copy(magnitudeMap[f].data(), fftData.data(), fftSize / 2 + 1);
    }

    std::vector<std::vector<float>> tonalMap = magnitudeMap;
    std::vector<std::vector<float>> transMap = magnitudeMap;

    applyHorizontalMedian(tonalMap, horizWindow);
    applyVerticalMedian(transMap, vertWindow);

    trans.setSize(1, numSamples, false, false, true);
    tonal.setSize(1, numSamples, false, false, true);
    trans.clear(); tonal.clear();

    // 再構成ロジック (現状はマスク計算のプレースホルダー)
    for (int f = 0; f < numFrames; ++f) {
        for (int i = 0; i <= fftSize / 2; ++i) {
            float t = tonalMap[f][i];
            float tr = transMap[f][i];
            float mask = t / (t + tr + 1e-6f);
        }
    }
}

void HpssSeparator::applyWindow(const juce::AudioBuffer<float>& input, int offset, std::vector<float>& dest) {
    const float* src = input.getReadPointer(0);
    for (int i = 0; i < fftSize; ++i) dest[i] = src[offset + i] * window[i];
    std::fill(dest.begin() + fftSize, dest.end(), 0.0f);
}

void HpssSeparator::applyHorizontalMedian(std::vector<std::vector<float>>& map, int windowSize) {
    int rows = (int)map.size();
    int cols = (int)map[0].size();
    std::vector<float> temp(rows);
    for (int c = 0; c < cols; ++c) {
        for (int r = 0; r < rows; ++r) {
            std::vector<float> windowValues;
            for (int i = -windowSize / 2; i <= windowSize / 2; ++i) {
                int idx = std::clamp(r + i, 0, rows - 1);
                windowValues.push_back(map[idx][c]);
            }
            std::nth_element(windowValues.begin(), windowValues.begin() + windowValues.size() / 2, windowValues.end());
            temp[r] = windowValues[windowValues.size() / 2];
        }
        for (int r = 0; r < rows; ++r) map[r][c] = temp[r];
    }
}

void HpssSeparator::applyVerticalMedian(std::vector<std::vector<float>>& map, int windowSize) {
    int rows = (int)map.size();
    int cols = (int)map[0].size();
    std::vector<float> temp(cols);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            std::vector<float> windowValues;
            for (int i = -windowSize / 2; i <= windowSize / 2; ++i) {
                int idx = std::clamp(c + i, 0, cols - 1);
                windowValues.push_back(map[r][idx]);
            }
            std::nth_element(windowValues.begin(), windowValues.begin() + windowValues.size() / 2, windowValues.end());
            temp[c] = windowValues[windowValues.size() / 2];
        }
        for (int c = 0; c < cols; ++c) map[r][c] = temp[c];
    }
}