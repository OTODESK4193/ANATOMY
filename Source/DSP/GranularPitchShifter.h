#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>
#include <algorithm>

class GranularPitchShifter
{
public:
    GranularPitchShifter()
    {
        init(44100.0, 40.0f, 4);
    }

    ~GranularPitchShifter() = default;

    void init(double sampleRate, float grainSizeMs, int numOverlaps)
    {
        this->currentSampleRate = sampleRate;
        this->overlaps = std::max(2, numOverlaps);

        this->grainSizeSamples = static_cast<int>((grainSizeMs / 1000.0f) * sampleRate);
        if (this->grainSizeSamples < 32) this->grainSizeSamples = 32;

        this->grainInterval = this->grainSizeSamples / this->overlaps;
        if (this->grainInterval < 1) this->grainInterval = 1;

        grains.resize(this->overlaps);

        window.resize(this->grainSizeSamples);
        for (int i = 0; i < this->grainSizeSamples; ++i)
        {
            window[i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * i / (this->grainSizeSamples - 1)));
        }

        reset();
    }

    void reset() noexcept
    {
        sampleCounter = 0;
        for (auto& g : grains)
        {
            g.isActive = false;
            g.readIndex = 0.0;
            g.centerTimelinePos = 0;
        }
    }

    float processSample(const juce::AudioBuffer<float>& sourceBuffer, int currentTimelineIdx, float pitchRatio) noexcept
    {
        const int maxSamples = sourceBuffer.getNumSamples();
        if (maxSamples <= 0 || currentTimelineIdx < 0 || currentTimelineIdx >= maxSamples)
            return 0.0f;

        if (std::abs(pitchRatio - 1.0f) < 0.001f)
        {
            return sourceBuffer.getReadPointer(0)[currentTimelineIdx];
        }

        const float* src = sourceBuffer.getReadPointer(0);

        if (sampleCounter % grainInterval == 0)
        {
            for (auto& g : grains)
            {
                if (!g.isActive)
                {
                    g.isActive = true;
                    g.readIndex = 0.0;
                    g.centerTimelinePos = currentTimelineIdx;
                    break;
                }
            }
        }
        sampleCounter++;

        float outputSample = 0.0f;

        for (auto& g : grains)
        {
            if (!g.isActive) continue;

            int writeIdx = static_cast<int>(g.readIndex);

            if (writeIdx >= grainSizeSamples - 1)
            {
                g.isActive = false;
                continue;
            }

            float grainOffset = static_cast<float>(writeIdx - grainSizeSamples / 2);
            float sourceOffset = grainOffset * pitchRatio;
            float sourcePos = static_cast<float>(g.centerTimelinePos) + sourceOffset;

            int srcIdx = static_cast<int>(sourcePos);

            if (srcIdx >= 0 && srcIdx < maxSamples - 1)
            {
                float frac = sourcePos - static_cast<float>(srcIdx);
                float s0 = src[srcIdx];
                float s1 = src[srcIdx + 1];
                outputSample += (s0 + frac * (s1 - s0)) * window[writeIdx];
            }

            g.readIndex += 1.0;
        }

        return outputSample;
    }

private:
    double currentSampleRate = 44100.0;
    int grainSizeSamples = 1024;
    int grainInterval = 256;
    int overlaps = 4;
    int sampleCounter = 0;
    std::vector<float> window;

    struct Grain
    {
        Grain() : isActive(false), readIndex(0.0), centerTimelinePos(0) {}
        bool isActive;
        double readIndex;
        int centerTimelinePos;
    };
    std::vector<Grain> grains;
};