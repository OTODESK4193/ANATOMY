#include "HpssSeparator.h"
#include <immintrin.h>
#include <cmath>
#include <algorithm>

HpssSeparator::HpssSeparator(int /*fftSizeIn*/)
    : fftLarge(12), // 2^12 = 4096
    fftSmall(8)   // 2^8  = 256
{
    // 大窓（4096）の Hann窓生成
    windowLarge.resize(4096);
    juce::dsp::WindowingFunction<float>::fillWindowingTables(windowLarge.data(), 4096, juce::dsp::WindowingFunction<float>::hann);

    // 小窓（256）の Hann窓生成
    windowSmall.resize(256);
    juce::dsp::WindowingFunction<float>::fillWindowingTables(windowSmall.data(), 256, juce::dsp::WindowingFunction<float>::hann);
}

void HpssSeparator::prepare(double sampleRate) {
    currentSampleRate = sampleRate;
}

void HpssSeparator::performSeparation(const juce::AudioBuffer<float>& input,
    juce::AudioBuffer<float>& trans,
    juce::AudioBuffer<float>& tonal)
{
    progress.store(0.0f);
    const int numSamples = input.getNumSamples();
    const float* inputSrc = input.getReadPointer(0);

    if (numSamples < 4096) {
        trans.makeCopyOf(input);
        tonal.clear();
        progress.store(1.0f);
        return;
    }

    // =================================================================
    // STAGE 1: 大窓（4096）による高解像度 TONAL 抽出パス (0% 〜 50%)
    // =================================================================
    const int hopLarge = 2048; // 50% 重複
    const int framesLarge = (numSamples - 4096) / hopLarge + 1;
    const int binsLarge = 2049;

    std::vector<std::vector<float>> complexLarge(framesLarge, std::vector<float>(4096 * 2, 0.0f));
    std::vector<std::vector<float>> magLarge(framesLarge, std::vector<float>(binsLarge, 0.0f));

    // 1-1. 大窓 Forward FFT とマグニチュード計算
    for (int f = 0; f < framesLarge; ++f) {
        float* dest = complexLarge[f].data();
        const float* srcOffset = inputSrc + (f * hopLarge);

        int i = 0;
        for (; i <= 4096 - 8; i += 8) {
            __m256 s = _mm256_loadu_ps(srcOffset + i);
            __m256 w = _mm256_loadu_ps(windowLarge.data() + i);
            _mm256_storeu_ps(dest + i, _mm256_mul_ps(s, w));
        }
        for (; i < 4096; ++i) dest[i] = srcOffset[i] * windowLarge[i];

        fftLarge.performRealOnlyForwardTransform(dest);

        magLarge[f][0] = std::abs(dest[0]);
        magLarge[f][binsLarge - 1] = std::abs(dest[1]);
        for (int b = 1; b < binsLarge - 1; ++b) {
            float re = dest[2 * b];
            float im = dest[2 * b + 1];
            magLarge[f][b] = std::sqrt(re * re + im * im);
        }
        progress.store(((float)f / (float)framesLarge) * 0.15f);
    }

    // 1-2. 水平メディアンフィルタ適用（持続音テイルの隔離：窓幅11フレーム）
    std::vector<std::vector<float>> tonalMagLarge = magLarge;
    applyHorizontalMedianLarge(tonalMagLarge, 11);
    progress.store(0.30f);

    // 1-3. ウィーナーマスク適用と大窓 iFFT + Overlap-Add 合成
    tonal.setSize(1, numSamples, false, false, true);
    tonal.clear();
    float* tonalOut = tonal.getWritePointer(0);

    std::vector<float> ifftBufferLarge(4096 * 2, 0.0f);

    for (int f = 0; f < framesLarge; ++f) {
        int offset = f * hopLarge;
        std::fill(ifftBufferLarge.begin(), ifftBufferLarge.end(), 0.0f);

        float t0 = tonalMagLarge[f][0];
        float o0 = magLarge[f][0] + 1e-6f;
        ifftBufferLarge[0] = complexLarge[f][0] * (t0 / o0);

        float tN = tonalMagLarge[f][binsLarge - 1];
        float oN = magLarge[f][binsLarge - 1] + 1e-6f;
        ifftBufferLarge[1] = complexLarge[f][1] * (tN / oN);

        int b = 1;
        for (; b <= binsLarge - 9; b += 8) {
            __m256 h = _mm256_loadu_ps(&tonalMagLarge[f][b]);
            __m256 o = _mm256_loadu_ps(&magLarge[f][b]);
            __m256 sum = _mm256_add_ps(o, _mm256_set1_ps(1e-6f));
            __m256 mask = _mm256_div_ps(h, sum);

            alignas(32) float m[8];
            _mm256_storeu_ps(m, mask);

            alignas(32) float mComp[16];
            for (int i = 0; i < 8; ++i) {
                mComp[2 * i] = m[i]; mComp[2 * i + 1] = m[i];
            }

            __m256 mc0 = _mm256_loadu_ps(&mComp[0]);
            __m256 mc1 = _mm256_loadu_ps(&mComp[8]);
            __m256 c0 = _mm256_loadu_ps(&complexLarge[f][2 * b]);
            __m256 c1 = _mm256_loadu_ps(&complexLarge[f][2 * b + 8]);

            _mm256_storeu_ps(&ifftBufferLarge[2 * b], _mm256_mul_ps(c0, mc0));
            _mm256_storeu_ps(&ifftBufferLarge[2 * b + 8], _mm256_mul_ps(c1, mc1));
        }
        for (; b < binsLarge - 1; ++b) {
            float mask = tonalMagLarge[f][b] / (magLarge[f][b] + 1e-6f);
            ifftBufferLarge[2 * b] = complexLarge[f][2 * b] * mask;
            ifftBufferLarge[2 * b + 1] = complexLarge[f][2 * b + 1] * mask;
        }

        fftLarge.performRealOnlyInverseTransform(ifftBufferLarge.data());

        // Hann窓50%重複の定常自乗和ゲイン=0.75およびFFTサイズ(4096)の正規化を統合
        const float normLarge = 1.0f / (4096.0f * 0.75f);
        for (int i = 0; i < 4096; ++i) {
            if (offset + i < numSamples) {
                tonalOut[offset + i] += ifftBufferLarge[i] * windowLarge[i] * normLarge;
            }
        }
        progress.store(0.30f + (((float)f / (float)framesLarge) * 0.20f));
    }
    progress.store(0.50f);

    // =================================================================
    // STAGE 2: 残余信号（Residual）の算出
    // =================================================================
    juce::AudioBuffer<float> residual(1, numSamples);
    float* residualOut = residual.getWritePointer(0);
    for (int i = 0; i < numSamples; ++i) {
        residualOut[i] = inputSrc[i] - tonalOut[i];
    }

    // =================================================================
    // STAGE 3: 小窓（256）による高解像度 TRANSIENT 抽出パス (50% 〜 100%)
    // =================================================================
    const int hopSmall = 128; // 50% 重複
    const int framesSmall = (numSamples - 256) / hopSmall + 1;
    const int binsSmall = 129;

    if (framesSmall <= 0) {
        trans.makeCopyOf(residual);
        progress.store(1.0f);
        return;
    }

    std::vector<std::vector<float>> complexSmall(framesSmall, std::vector<float>(256 * 2, 0.0f));
    std::vector<std::vector<float>> magSmall(framesSmall, std::vector<float>(binsSmall, 0.0f));

    // 3-1. 小窓 Forward FFT とマグニチュード計算
    for (int f = 0; f < framesSmall; ++f) {
        float* dest = complexSmall[f].data();
        const float* srcOffset = residualOut + (f * hopSmall);

        for (int i = 0; i < 256; ++i) dest[i] = srcOffset[i] * windowSmall[i];

        fftSmall.performRealOnlyForwardTransform(dest);

        magSmall[f][0] = std::abs(dest[0]);
        magSmall[f][binsSmall - 1] = std::abs(dest[1]);
        for (int b = 1; b < binsSmall - 1; ++b) {
            magSmall[f][b] = std::sqrt(dest[2 * b] * dest[2 * b] + dest[2 * b + 1] * dest[2 * b + 1]);
        }
        progress.store(0.50f + (((float)f / (float)framesSmall) * 0.15f));
    }

    // 3-2. 垂直メディアンフィルタ適用（アタック縦スパイクの過激隔離：窓幅7ビン）
    std::vector<std::vector<float>> transMagSmall = magSmall;
    applyVerticalMedianSmall(transMagSmall, 7);
    progress.store(0.80f);

    // 3-3. ウィーナーマスク適用と小窓 iFFT + Overlap-Add 合成
    trans.setSize(1, numSamples, false, false, true);
    trans.clear();
    float* transOut = trans.getWritePointer(0);

    std::vector<float> ifftBufferSmall(256 * 2, 0.0f);

    for (int f = 0; f < framesSmall; ++f) {
        int offset = f * hopSmall;
        std::fill(ifftBufferSmall.begin(), ifftBufferSmall.end(), 0.0f);

        float p0 = transMagSmall[f][0];
        float r0 = magSmall[f][0] + 1e-6f;
        ifftBufferSmall[0] = complexSmall[f][0] * (p0 / r0);

        float pN = transMagSmall[f][binsSmall - 1];
        float rN = magSmall[f][binsSmall - 1] + 1e-6f;
        ifftBufferSmall[1] = complexSmall[f][1] * (pN / rN);

        for (int b = 1; b < binsSmall - 1; ++b) {
            float mask = transMagSmall[f][b] / (magSmall[f][b] + 1e-6f);
            ifftBufferSmall[2 * b] = complexSmall[f][2 * b] * mask;
            ifftBufferSmall[2 * b + 1] = complexSmall[f][2 * b + 1] * mask;
        }

        fftSmall.performRealOnlyInverseTransform(ifftBufferSmall.data());

        // 小窓（256）用の完全正規化係数
        const float normSmall = 1.0f / (256.0f * 0.75f);
        for (int i = 0; i < 256; ++i) {
            if (offset + i < numSamples) {
                transOut[offset + i] += ifftBufferSmall[i] * windowSmall[i] * normSmall;
            }
        }
        progress.store(0.80f + (((float)f / (float)framesSmall) * 0.20f));
    }
    progress.store(1.0f); // 完了
}

void HpssSeparator::applyHorizontalMedianLarge(std::vector<std::vector<float>>& map, int windowSize) {
    const int rows = static_cast<int>(map.size());
    const int cols = static_cast<int>(map[0].size());
    std::vector<std::vector<float>> tempMap = map;

    // 大窓（ビン数2049）の莫大な並列計算をAVX2で超高速化
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

void HpssSeparator::applyVerticalMedianSmall(std::vector<std::vector<float>>& map, int windowSize) {
    const int rows = static_cast<int>(map.size());
    const int cols = static_cast<int>(map[0].size());
    std::vector<std::vector<float>> tempMap = map;

    // 小窓の周波数軸方向（binsSmall=129）に対する垂直メディアンフィルタ
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            std::vector<float> windowValues;
            for (int i = -windowSize / 2; i <= windowSize / 2; ++i) {
                int idx = std::clamp(c + i, 0, cols - 1);
                windowValues.push_back(tempMap[r][idx]);
            }
            std::nth_element(windowValues.begin(), windowValues.begin() + windowValues.size() / 2, windowValues.end());
            map[r][c] = windowValues[windowValues.size() / 2];
        }
    }
}