#pragma once

#include "AudioEffect.h"

class Limiter final : public AudioEffect
{
public:
    void prepare(double sr, int) override { sampleRate = sr; }
    void reset() noexcept override { envelope = 0.0f; }

    void process(juce::AudioBuffer<float>& buffer) noexcept override
    {
        const float ceilingLinear = std::pow(10.0f, ceilingDb / 20.0f);
        const float releaseCoef = std::exp(-1.0f / (0.0015f * sampleRate)); // 1.5ms release

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            float* data = buffer.getWritePointer(ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                float absSample = std::abs(data[i]);
                if (absSample > envelope) envelope = absSample;
                else envelope = envelope * releaseCoef + absSample * (1.0f - releaseCoef);

                if (envelope > ceilingLinear)
                    data[i] *= (ceilingLinear / envelope);
            }
        }
    }

    void setCeiling(float db) noexcept { ceilingDb = db; }
    juce::String getName() const override { return "Limiter"; }
    TargetRoute getTargetRoute() const noexcept override { return route; }
    void setTargetRoute(TargetRoute r) noexcept override { route = r; }

private:
    double sampleRate = 44100.0;
    float envelope = 0.0f;
    float ceilingDb = -0.1f;
    TargetRoute route = TargetRoute::FullMix;
};