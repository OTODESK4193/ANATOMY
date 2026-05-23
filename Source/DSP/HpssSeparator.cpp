#include "HpssSeparator.h"
#include <immintrin.h>
#include <cmath>

HpssSeparator::HpssSeparator(int fftSizeIn)
    : fftSize(fftSizeIn),
    fft(static_cast<int>(std::log2(fftSizeIn)))
{
    window.resize(fftSize);
    juce::dsp::WindowingFunction<float>::fillWindowingTables(window.data(), fftSize, juce::dsp::WindowingFunction<float>::hann);
}

void HpssSeparator::prepare(double sampleRate) {
    currentSampleRate = sampleRate;

    // 【ワンショットサンプル特化設定】
    // 短い打楽器サンプルの全帯域アタック（垂直スパイク）を極めて鋭く検出するため、周波数軸は狭く設定
    vertWindow = 5;
    // ワンショットの減衰・コード余韻（水平成分）を滑らかに抽出するため、時間軸は広く設定
    horizWindow = 15;
}

void HpssSeparator::performSeparation(const juce::AudioBuffer<float>& input,
    juce::AudioBuffer<float>& trans,
    juce::AudioBuffer<float>& tonal)
{
    progress.store(0.0f);
    const int numSamples = input.getNumSamples();
    const int hopSize = fftSize / 2;
    const int numFrames = (numSamples - fftSize) / hopSize;
    const int numBins = fftSize / 2 + 1;

    if (numFrames <= 0) {
        progress.store(1.0f);
        return;
    }

    std::vector<std::vector<float>> complexFFTMap(numFrames, std::vector<float>(fftSize * 2, 0.0f));
    std::vector<std::vector<float>> magnitudeMap(numFrames, std::vector<float>(numBins, 0.0f));

    // フェーズ1: 窓掛け + 正確なJUCEリアルパッキング解析
    for (int f = 0; f < numFrames; ++f) {
        applyWindow(input, f * hopSize, complexFFTMap[f]);
        fft.performRealOnlyForwardTransform(complexFFTMap[f].data());

        // JUCE仕様: インデックス0がDC実部、インデックス1がナイキスト実部
        magnitudeMap[f][0] = std::abs(complexFFTMap[f][0]);
        magnitudeMap[f][numBins - 1] = std::abs(complexFFTMap[f][1]);

        // インデックス2以降から[Real, Imag]のペアを正しく抽出
        for (int b = 1; b < numBins - 1; ++b) {
            float re = complexFFTMap[f][2 * b];
            float im = complexFFTMap[f][2 * b + 1];
            magnitudeMap[f][b] = std::sqrt(re * re + im * im);
        }

        progress.store(((float)f / (float)numFrames) * 0.20f);
    }

    std::vector<std::vector<float>> tonalMap = magnitudeMap;
    std::vector<std::vector<float>> transMap = magnitudeMap;

    // フェーズ2: 水平メディアンフィルタ (20% 〜 50%)
    applyHorizontalMedian(tonalMap, horizWindow);
    progress.store(0.50f);

    // フェーズ3: 垂直メディアンフィルタ (50% 〜 80%)
    applyVerticalMedian(transMap, vertWindow);
    progress.store(0.80f);

    trans.setSize(1, numSamples, false, false, true);
    tonal.setSize(1, numSamples, false, false, true);
    trans.clear();
    tonal.clear();

    float* transOut = trans.getWritePointer(0);
    float* tonalOut = tonal.getWritePointer(0);

    std::vector<float> ifftBufferTrans(fftSize * 2, 0.0f);
    std::vector<float> ifftBufferTonal(fftSize * 2, 0.0f);

    // フェーズ4: JUCEパッキングに完全準拠したAVX2複素マスク適用 + iFFT
    for (int f = 0; f < numFrames; ++f) {
        int offset = f * hopSize;
        std::fill(ifftBufferTrans.begin(), ifftBufferTrans.end(), 0.0f);
        std::fill(ifftBufferTonal.begin(), ifftBufferTonal.end(), 0.0f);

        // 1. 特殊な独立インデックスである DC (0) と ナイキスト (numBins - 1) を個別にマスク演算
        float t0 = tonalMap[f][0];
        float p0 = transMap[f][0];
        float sum0 = t0 + p0 + 1e-6f;
        ifftBufferTonal[0] = complexFFTMap[f][0] * (t0 / sum0);
        ifftBufferTrans[0] = complexFFTMap[f][0] * (p0 / sum0);

        float tN = tonalMap[f][numBins - 1];
        float pN = transMap[f][numBins - 1];
        float sumN = tN + pN + 1e-6f;
        ifftBufferTonal[1] = complexFFTMap[f][1] * (tN / sumN);
        ifftBufferTrans[1] = complexFFTMap[f][1] * (pN / sumN);

        // 2. [Real, Imag]のペアが連続するインデックス1〜(numBins-2)の区間のみをAVX2で高速ループ
        int b = 1;
        for (; b <= numBins - 9; b += 8) {
            __m256 h = _mm256_loadu_ps(&tonalMap[f][b]);
            __m256 p = _mm256_loadu_ps(&transMap[f][b]);

            __m256 sum = _mm256_add_ps(h, p);
            __m256 eps = _mm256_set1_ps(1e-6f);
            sum = _mm256_add_ps(sum, eps);

            __m256 maskTonal = _mm256_div_ps(h, sum);
            __m256 maskTrans = _mm256_div_ps(p, sum);

            alignas(32) float tMask[8];
            alignas(32) float pMask[8];
            _mm256_storeu_ps(tMask, maskTonal);
            _mm256_storeu_ps(pMask, maskTrans);

            alignas(32) float tMaskComplex[16];
            alignas(32) float pMaskComplex[16];
            for (int i = 0; i < 8; ++i) {
                tMaskComplex[2 * i] = tMask[i];
                tMaskComplex[2 * i + 1] = tMask[i];
                pMaskComplex[2 * i] = pMask[i];
                pMaskComplex[2 * i + 1] = pMask[i];
            }

            // インデックス「2 * b」から正しくロードすることで実部・虚部へのマスク適用を完全整合
            __m256 c0 = _mm256_loadu_ps(&complexFFTMap[f][2 * b]);
            __m256 c1 = _mm256_loadu_ps(&complexFFTMap[f][2 * b + 8]);

            __m256 mCompTonal0 = _mm256_loadu_ps(&tMaskComplex[0]);
            __m256 mCompTonal1 = _mm256_loadu_ps(&tMaskComplex[8]);
            __m256 mCompTrans0 = _mm256_loadu_ps(&pMaskComplex[0]);
            __m256 mCompTrans1 = _mm256_loadu_ps(&pMaskComplex[8]);

            _mm256_storeu_ps(&ifftBufferTonal[2 * b], _mm256_mul_ps(c0, mCompTonal0));
            _mm256_storeu_ps(&ifftBufferTonal[2 * b + 8], _mm256_mul_ps(c1, mCompTonal1));

            _mm256_storeu_ps(&ifftBufferTrans[2 * b], _mm256_mul_ps(c0, mCompTrans0));
            _mm256_storeu_ps(&ifftBufferTrans[2 * b + 8], _mm256_mul_ps(c1, mCompTrans1));
        }

        // 3. マスクループの端数処理
        for (; b < numBins - 1; ++b) {
            float t = tonalMap[f][b];
            float p = transMap[f][b];
            float sum = t + p + 1e-6f;
            float maskTonal = t / sum;
            float maskTrans = p / sum;

            ifftBufferTonal[2 * b] = complexFFTMap[f][2 * b] * maskTonal;
            ifftBufferTonal[2 * b + 1] = complexFFTMap[f][2 * b + 1] * maskTonal;

            ifftBufferTrans[2 * b] = complexFFTMap[f][2 * b] * maskTrans;
            ifftBufferTrans[2 * b + 1] = complexFFTMap[f][2 * b + 1] * maskTrans;
        }

        // 逆リアルFFTを実行して正確に時間ドメインへ復元
        fft.performRealOnlyInverseTransform(ifftBufferTonal.data());
        fft.performRealOnlyInverseTransform(ifftBufferTrans.data());

        // 逆窓掛け(Overlap-Add)
        for (int i = 0; i < fftSize; ++i) {
            if (offset + i < numSamples) {
                tonalOut[offset + i] += ifftBufferTonal[i] * window[i] * 0.5f;
                transOut[offset + i] += ifftBufferTrans[i] * window[i] * 0.5f;
            }
        }

        progress.store(0.80f + (((float)f / (float)numFrames) * 0.20f));
    }
    progress.store(1.0f);
}

void HpssSeparator::applyWindow(const juce::AudioBuffer<float>& input, int offset, std::vector<float>& dest) {
    const float* src = input.getReadPointer(0);
    int i = 0;
    for (; i <= fftSize - 8; i += 8) {
        __m256 s = _mm256_loadu_ps(src + offset + i);
        __m256 w = _mm256_loadu_ps(window.data() + i);
        _mm256_storeu_ps(dest.data() + i, _mm256_mul_ps(s, w));
    }
    for (; i < fftSize; ++i) {
        dest[i] = src[offset + i] * window[i];
    }
    std::fill(dest.begin() + fftSize, dest.end(), 0.0f);
}

void HpssSeparator::applyHorizontalMedian(std::vector<std::vector<float>>& map, int windowSize) {
    const int rows = static_cast<int>(map.size());
    const int cols = static_cast<int>(map[0].size());
    std::vector<std::vector<float>> tempMap = map;

    for (int c = 0; c <= cols - 8; c += 8) {
        for (int r = 0; r < rows; ++r) {
            std::vector<__m256> vWin;
            for (int i = -windowSize / 2; i <= windowSize / 2; ++i) {
                int idx = std::clamp(r + i, 0, rows - 1);
                vWin.push_back(_mm256_loadu_ps(&tempMap[idx][c]));
            }
            const size_t wSize = vWin.size();
            for (size_t m = 0; m < wSize; ++m) {
                for (size_t n = m + 1; n < wSize; ++n) {
                    __m256 minV = _mm256_min_ps(vWin[m], vWin[n]);
                    __m256 maxV = _mm256_max_ps(vWin[m], vWin[n]);
                    vWin[m] = minV;
                    vWin[n] = maxV;
                }
            }
            _mm256_storeu_ps(&map[r][c], vWin[wSize / 2]);
        }
        progress.store(0.20f + (((float)c / (float)cols) * 0.30f));
    }

    int remainderStart = (cols / 8) * 8;
    for (int c = remainderStart; c < cols; ++c) {
        for (int r = 0; r < rows; ++r) {
            std::vector<float> windowValues;
            for (int i = -windowSize / 2; i <= windowSize / 2; ++i) {
                int idx = std::clamp(r + i, 0, rows - 1);
                windowValues.push_back(tempMap[idx][c]);
            }
            std::nth_element(windowValues.begin(), windowValues.begin() + windowValues.size() / 2, windowValues.end());
            map[r][c] = windowValues[windowValues.size() / 2];
        }
    }
}

void HpssSeparator::applyVerticalMedian(std::vector<std::vector<float>>& map, int windowSize) {
    const int rows = static_cast<int>(map.size());
    const int cols = static_cast<int>(map[0].size());
    std::vector<std::vector<float>> tempMap = map;

    for (int r = 0; r < rows; ++r) {
        int c = 0;
        for (; c <= cols - 8; c += 8) {
            std::vector<__m256> vWin;
            for (int i = -windowSize / 2; i <= windowSize / 2; ++i) {
                std::vector<float> dynamicWin(8);
                for (int lane = 0; lane < 8; ++lane) {
                    int idx = std::clamp(c + lane + i, 0, cols - 1);
                    dynamicWin[lane] = tempMap[r][idx];
                }
                vWin.push_back(_mm256_loadu_ps(dynamicWin.data()));
            }
            const size_t wSize = vWin.size();
            for (size_t m = 0; m < wSize; ++m) {
                for (size_t n = m + 1; n < wSize; ++n) {
                    __m256 minV = _mm256_min_ps(vWin[m], vWin[n]);
                    __m256 maxV = _mm256_max_ps(vWin[m], vWin[n]);
                    vWin[m] = minV;
                    vWin[n] = maxV;
                }
            }
            _mm256_storeu_ps(&map[r][c], vWin[wSize / 2]);
        }
        for (; c < cols; ++c) {
            std::vector<float> windowValues;
            for (int i = -windowSize / 2; i <= windowSize / 2; ++i) {
                int idx = std::clamp(c + i, 0, cols - 1);
                windowValues.push_back(tempMap[r][idx]);
            }
            std::nth_element(windowValues.begin(), windowValues.begin() + windowValues.size() / 2, windowValues.end());
            map[r][c] = windowValues[windowValues.size() / 2];
        }
        progress.store(0.50f + (((float)r / (float)rows) * 0.30f));
    }
}