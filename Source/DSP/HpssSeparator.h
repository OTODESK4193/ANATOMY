#pragma once
#include <juce_dsp/juce_dsp.h>
#include <vector>

class HpssSeparator {
public:
    HpssSeparator(int fftSizeIn) : fftSize(fftSizeIn), fft(static_cast<int>(log2(fftSizeIn))) {
        window.resize(fftSize);
        juce::dsp::WindowingFunction<float>::fillWindowingTables(window.data(), fftSize, juce::dsp::WindowingFunction<float>::hann);
    }

    void prepare(double sampleRate) {
        currentSampleRate = sampleRate;
        // 垂直・水平方向のメディアンフィルタ窓幅（SRに応じて調整）
        vertWindow = 17; // Transient用
        horizWindow = 17; // Tonal用
    }

    void performSeparation(const juce::AudioBuffer<float>& input,
        juce::AudioBuffer<float>& trans,
        juce::AudioBuffer<float>& tonal)
    {
        int numSamples = input.getNumSamples();
        int hopSize = fftSize / 2;
        int numFrames = (numSamples - fftSize) / hopSize;

        // 一時バッファ
        std::vector<float> fftData(fftSize * 2);
        std::vector<std::vector<float>> magnitudeMap(numFrames, std::vector<float>(fftSize / 2 + 1));

        // 1. STFT (解析)
        for (int f = 0; f < numFrames; ++f) {
            applyWindow(input, f * hopSize, fftData);
            fft.performFrequencyOnlyForwardTransform(fftData.data());

            for (int i = 0; i <= fftSize / 2; ++i)
                magnitudeMap[f][i] = fftData[i];
        }

        // 2. メディアンフィルタによる分離 (HPSS)
        std::vector<std::vector<float>> tonalMap = magnitudeMap;
        std::vector<std::vector<float>> transMap = magnitudeMap;

        // 水平方向フィルタ (Tonal成分抽出)
        applyHorizontalMedian(tonalMap, horizWindow);

        // 垂直方向フィルタ (Transient成分抽出)
        applyVerticalMedian(transMap, vertWindow);

        // マスク生成と適用
        for (int f = 0; f < numFrames; ++f) {
            for (int i = 0; i <= fftSize / 2; ++i) {
                float total = tonalMap[f][i] + transMap[f][i] + 1e-6f;
                float tonalMask = tonalMap[f][i] / total;

                // 再構成ロジックをここに実装
            }
        }
    }

private:
    void applyWindow(const juce::AudioBuffer<float>& input, int offset, std::vector<float>& dest) {
        for (int i = 0; i < fftSize; ++i)
            dest[i] = input.getSample(0, offset + i) * window[i];
    }

    void applyHorizontalMedian(std::vector<std::vector<float>>& map, int windowSize) {
        int rows = map.size();
        int cols = map[0].size();
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

    void applyVerticalMedian(std::vector<std::vector<float>>& map, int windowSize) {
        int rows = map.size();
        int cols = map[0].size();
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

    int fftSize;
    double currentSampleRate;
    juce::dsp::FFT fft;
    std::vector<float> window;
    int vertWindow, horizWindow;
};