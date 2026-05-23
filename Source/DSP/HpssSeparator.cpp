#include "HpssSeparator.h"

HpssSeparator::HpssSeparator(int) {}

void HpssSeparator::prepare(double sampleRate)
{
    currentSampleRate = sampleRate;
}

void HpssSeparator::performSeparation(const juce::AudioBuffer<float>& input,
    juce::AudioBuffer<float>& trans,
    juce::AudioBuffer<float>& tonal)
{
    progress.store(0.0f);
    const int numSamples = input.getNumSamples();

    trans.setSize(1, numSamples, false, false, true);
    tonal.setSize(1, numSamples, false, false, true);
    trans.clear();
    tonal.clear();

    if (numSamples <= 0)
    {
        progress.store(1.0f);
        return;
    }

    // 1. 弱努力法を用いた高精度な時間軸境界解析 (0% 〜 30%)
    progress.store(0.1f);
    auto bounds = detector.analyzeBuffer(input, currentSampleRate, 1.0f, -14.0f);
    progress.store(0.3f);

    float* transData = trans.getWritePointer(0);
    float* tonalData = tonal.getWritePointer(0);
    const float* srcData = input.getReadPointer(0);

    // オンセットが検出できなかった場合の安全なフォールバック
    if (bounds.startIndex == -1 || bounds.peakIndex == -1 || bounds.endIndex == -1)
    {
        juce::FloatVectorOperations::copy(transData, srcData, numSamples);
        progress.store(1.0f);
        return;
    }

    // 2. 代数的相補性を備えた非対称スプライシング窓の適用 (30% 〜 100%)
    const int nStart = bounds.startIndex;
    const int nPeak = bounds.peakIndex;
    const int nEnd = bounds.endIndex;

    const int riseLength = std::max(1, nPeak - nStart);
    const int fallLength = std::max(1, nEnd - nPeak);

    for (int n = 0; n < numSamples; ++n)
    {
        float wClick = 0.0f;

        if (n < nStart)
        {
            wClick = 0.0f;
        }
        else if (n >= nStart && n < nPeak)
        {
            // アタックの滑らかな立ち上がり (ハーフサインフェード)
            float phase = (static_cast<float>(n - nStart) / static_cast<float>(riseLength)) * (juce::MathConstants<float>::pi * 0.5f);
            wClick = std::sin(phase);
        }
        else if (n >= nPeak && n < nEnd)
        {
            // アタックからサスティンへの軟着陸フェードアウト
            float phase = (static_cast<float>(n - nPeak) / static_cast<float>(fallLength)) * (juce::MathConstants<float>::pi * 0.5f);
            wClick = std::cos(phase);
        }
        else
        {
            wClick = 0.0f;
        }

        // 代数的完全再構成条件の絶対死守
        float wSustain = 1.0f - wClick;

        // 元波形へ乗算積算して無損失スプリット
        transData[n] = srcData[n] * wClick;
        tonalData[n] = srcData[n] * wSustain;

        if (n % 1000 == 0)
        {
            progress.store(0.3f + (static_cast<float>(n) / static_cast<float>(numSamples)) * 0.7f);
        }
    }

    progress.store(1.0f);
}