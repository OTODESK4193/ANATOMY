#pragma once

#include "AudioEffect.h"
#include <random>

class NoiseGenerator final : public AudioEffect
{
public:
    NoiseGenerator() : rd(), gen(rd()) {}

    void prepare(double sampleRate, int) override { this->currentSampleRate = sampleRate; reset(); }
    void reset() noexcept override { envelope = 0.0f; }

    void process(juce::AudioBuffer<float>& buffer) noexcept override
    {
        const int numSamples = buffer.getNumSamples();
        const float decay = std::exp(-1.0f / (decayMs * 0.001f * currentSampleRate));

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            float* data = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
            {
                float noise = (isPink ? generatePink() : generateWhite());
                data[i] += noise * envelope;
                envelope *= decay;
            }
        }
    }

    void trigger() noexcept { envelope = 1.0f; }
    void setDecay(float ms) noexcept { decayMs = std::max(1.0f, ms); }
    void setPink(bool pink) noexcept { isPink = pink; }

    juce::String getName() const override { return "Noise Generator"; }
    TargetRoute getTargetRoute() const noexcept override { return route; }
    void setTargetRoute(TargetRoute r) noexcept override { route = r; }

private:
    float generateWhite() { std::uniform_real_distribution<float> dist(-1.0f, 1.0f); return dist(gen); }
    float generatePink() {
        // Voss-McCartney approximation
        static float b0 = 0, b1 = 0, b2 = 0;
        float white = generateWhite();
        b0 = 0.998f * b0 + white * 0.0555179f;
        b1 = 0.993f * b1 + white * 0.0750759f;
        b2 = 0.969f * b2 + white * 0.1538520f;
        return (b0 + b1 + b2 + white * 0.05362f) * 0.25f;
    }

    double currentSampleRate = 44100.0;
    std::random_device rd;
    std::mt19937 gen;
    float envelope = 0.0f;
    float decayMs = 100.0f;
    bool isPink = false;
    TargetRoute route = TargetRoute::Transient;
};