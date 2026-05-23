#include "HpssSeparator.h"

HpssSeparator::HpssSeparator(int) {}

void HpssSeparator::prepare(double sampleRate)
{
    currentSampleRate = sampleRate;
}

void HpssSeparator::performSeparation(const juce::AudioBuffer<float>& input,
    juce::AudioBuffer<float>& trans,
    juce::AudioBuffer<float>& tonal,
    float sensitivity,
    float clickLengthMs,
    float clickCurve,
    float lookAheadMs,
    juce::Thread* callingThread)
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

    if (callingThread != nullptr && callingThread->threadShouldExit()) return;

    progress.store(0.1f);
    auto bounds = detector.analyzeBuffer(input, currentSampleRate, sensitivity, lookAheadMs);
    progress.store(0.3f);

    if (callingThread != nullptr && callingThread->threadShouldExit()) return;

    float* transData = trans.getWritePointer(0);
    float* tonalData = tonal.getWritePointer(0);
    const float* srcData = input.getReadPointer(0);

    if (bounds.startIndex == -1 || bounds.peakIndex == -1)
    {
        juce::FloatVectorOperations::copy(transData, srcData, numSamples);
        progress.store(1.0f);
        return;
    }

    const int nStart = bounds.startIndex;
    const int nPeak = bounds.peakIndex;

    const int riseLength = std::max(1, nPeak - nStart);
    const int fallLength = std::max(1, static_cast<int>((clickLengthMs / 1000.0f) * currentSampleRate));
    const int nEnd = nPeak + fallLength;

    for (int n = 0; n < numSamples; ++n)
    {
        if (n % 2000 == 0 && callingThread != nullptr && callingThread->threadShouldExit())
            return;

        float wClick = 0.0f;

        if (n < nStart)
        {
            wClick = 0.0f;
        }
        else if (n >= nStart && n < nPeak)
        {
            float phase = (static_cast<float>(n - nStart) / static_cast<float>(riseLength)) * (juce::MathConstants<float>::pi * 0.5f);
            wClick = std::sin(phase);
        }
        else if (n >= nPeak && n < nEnd)
        {
            float phase = (static_cast<float>(n - nPeak) / static_cast<float>(fallLength)) * (juce::MathConstants<float>::pi * 0.5f);

            // 【新設】コサイン減衰窓を clickCurve で累乗変形。
            // curve値を1.0より小さく（左に回す）すると凸型になり、ピーク直後の二次アタックエネルギーを長くClick側にホールドします
            wClick = std::pow(std::cos(phase), clickCurve);
        }
        else
        {
            wClick = 0.0f;
        }

        // 代数的完全再構成（総和1.0f）をミリサンプルの極限まで死守
        float wSustain = 1.0f - wClick;

        transData[n] = srcData[n] * wClick;
        tonalData[n] = srcData[n] * wSustain;

        if (n % 4000 == 0)
        {
            progress.store(0.3f + (static_cast<float>(n) / static_cast<float>(numSamples)) * 0.7f);
        }
    }

    progress.store(1.0f);
}