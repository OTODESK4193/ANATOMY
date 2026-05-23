#include "HpssSeparator.h"
#include <cmath>
#include <algorithm>

HpssSeparator::HpssSeparator(int) {}

void HpssSeparator::prepare(double sampleRate)
{
    currentSampleRate = sampleRate;
}

void HpssSeparator::performSeparation(const juce::AudioBuffer<float>& input,
    juce::AudioBuffer<float>& trans,
    juce::AudioBuffer<float>& tonal,
    float sensitivity,
    float clickHoldMs,
    float sustainFadeMs,
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

    float* transData = trans.getWritePointer(0);
    float* tonalData = tonal.getWritePointer(0);
    const float* srcData = input.getReadPointer(0);

    // ワンショットの絶対先頭（0ms）からHOLD時間とFADE-IN時間をサンプル数へ正確に変換
    const int holdSamples = static_cast<int>((clickHoldMs / 1000.0f) * currentSampleRate);
    const int fadeSamples = std::max(1, static_cast<int>((sustainFadeMs / 1000.0f) * currentSampleRate));

    const int nHoldEnd = holdSamples;
    const int nFadeEnd = nHoldEnd + fadeSamples;

    for (int n = 0; n < numSamples; ++n)
    {
        if (n % 2000 == 0 && callingThread != nullptr && callingThread->threadShouldExit())
            return;

        float wClick = 0.0f;

        if (n >= 0 && n < nHoldEnd)
        {
            // 絶対頭から指定ms間は原音を100%回収（Sustain側は完全無音）
            wClick = 1.0f;
        }
        else if (n >= nHoldEnd && n < nFadeEnd)
        {
            // 傾きゼロで軟着陸合流させるコサイン自乗（Hanning）フェードアウト
            float phase = (static_cast<float>(n - nHoldEnd) / static_cast<float>(fadeSamples)) * (juce::MathConstants<float>::pi * 0.5f);
            float cosVal = std::cos(phase);
            wClick = cosVal * cosVal;
        }
        else
        {
            wClick = 0.0f;
        }

        // 相補的完全再構成条件の死守
        float wSustain = 1.0f - wClick;

        transData[n] = srcData[n] * wClick;
        tonalData[n] = srcData[n] * wSustain;

        if (n % 4000 == 0)
        {
            progress.store((static_cast<float>(n) / static_cast<float>(numSamples)));
        }
    }

    progress.store(1.0f);
}