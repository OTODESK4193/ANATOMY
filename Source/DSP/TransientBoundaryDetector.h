#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>
#include <algorithm>

class TransientBoundaryDetector
{
public:
    TransientBoundaryDetector() = default;
    ~TransientBoundaryDetector() = default;

    struct Boundaries
    {
        int startIndex = -1; // ルックアヘッド補正済みのオンセット開始点
        int peakIndex = -1;  // アタックの最大ピーク点
        int endIndex = -1;   // トランジェント（アタック）の終端点
    };

    Boundaries analyzeBuffer(const juce::AudioBuffer<float>& buffer,
        double sampleRate,
        float lookAheadMs = 1.5f,
        float decayThresholdDb = -18.0f)
    {
        Boundaries bounds;
        const int numSamples = buffer.getNumSamples();
        if (numSamples <= 0) return bounds;
        const float* channelData = buffer.getReadPointer(0);

        // 1. エネルギーエンベロープの構築 (高速非対称リーキーインテグレータ)
        std::vector<float> energyEnvelope(numSamples, 0.0f);
        float currentEnv = 0.0f;
        const float alphaAttack = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.0002)));
        const float alphaRelease = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.015)));

        for (int i = 0; i < numSamples; ++i)
        {
            float absVal = std::abs(channelData[i]);
            if (absVal > currentEnv)
                currentEnv = absVal * alphaAttack + currentEnv * (1.0f - alphaAttack);
            else
                currentEnv = absVal * alphaRelease + currentEnv * (1.0f - alphaRelease);
            energyEnvelope[i] = currentEnv;
        }

        // 2. 最大アタックピークの検索
        float maxEnergy = 0.0f;
        int maxEnergyIndex = 0;
        for (int i = 0; i < numSamples; ++i)
        {
            if (energyEnvelope[i] > maxEnergy)
            {
                maxEnergy = energyEnvelope[i];
                maxEnergyIndex = i;
            }
        }

        if (maxEnergy < 1e-4f)
            return bounds;

        // 3. 弱努力法（Weakest Effort Method）によるオンセット開始点検出
        const int numThresholds = 10;
        std::vector<float> thresholds(numThresholds);
        std::vector<int> crossingIndices(numThresholds, -1);

        for (int j = 0; j < numThresholds; ++j)
        {
            thresholds[j] = maxEnergy * (static_cast<float>(j) / static_cast<float>(numThresholds));
        }

        for (int j = 1; j < numThresholds; ++j)
        {
            for (int i = 0; i < maxEnergyIndex; ++i)
            {
                if (energyEnvelope[i] >= thresholds[j])
                {
                    crossingIndices[j] = i;
                    break;
                }
            }
        }

        std::vector<int> efforts;
        for (int j = 1; j < numThresholds - 1; ++j)
        {
            if (crossingIndices[j] != -1 && crossingIndices[j + 1] != -1)
            {
                efforts.push_back(crossingIndices[j + 1] - crossingIndices[j]);
            }
        }

        float avgEffort = 0.0f;
        if (!efforts.empty())
        {
            float sum = 0.0f;
            for (int effort : efforts) sum += static_cast<float>(effort);
            avgEffort = sum / static_cast<float>(efforts.size());
        }

        const float thresholdM = 0.8f;
        int detectedStartIndex = 0;
        for (size_t k = 0; k < efforts.size(); ++k)
        {
            if (static_cast<float>(efforts[k]) < thresholdM * avgEffort)
            {
                detectedStartIndex = crossingIndices[k + 1];
                break;
            }
        }

        const int lookAheadSamples = static_cast<int>((lookAheadMs / 1000.0f) * sampleRate);
        bounds.startIndex = std::max(0, detectedStartIndex - lookAheadSamples);
        bounds.peakIndex = maxEnergyIndex;

        // 4. トランジェント終了点（サスティン領域への移行境界）の同定
        const float endThresholdFactor = std::pow(10.0f, decayThresholdDb / 20.0f);
        const float endThresholdVal = maxEnergy * endThresholdFactor;
        bounds.endIndex = numSamples - 1;

        for (int i = bounds.peakIndex; i < numSamples; ++i)
        {
            if (energyEnvelope[i] <= endThresholdVal)
            {
                bounds.endIndex = i;
                break;
            }
        }

        return bounds;
    }
};