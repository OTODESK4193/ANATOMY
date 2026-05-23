#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include "ThreadSafeSamplerSound.h"

class ThreadSafeSamplerVoice : public juce::SynthesiserVoice
{
public:
    ThreadSafeSamplerVoice() = default;
    ~ThreadSafeSamplerVoice() override = default;

    bool canPlaySound(juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<ThreadSafeSamplerSound*>(sound) != nullptr;
    }

    void startNote(int midiNoteNumber, float velocity,
        juce::SynthesiserSound* sound, int) override
    {
        if (auto* samplerSound = dynamic_cast<ThreadSafeSamplerSound*>(sound))
        {
            activeData = samplerSound->getSampleData();
            if (activeData != nullptr)
            {
                triggerVelocity = velocity;
                clickReadIndex = 0;
                sustainReadIndex = 0;
                isActive = true;
            }
            else
            {
                clearCurrentNote();
            }
        }
    }

    void stopNote(float, bool allowTailOff) override
    {
        if (!allowTailOff)
        {
            clearCurrentNote();
            activeData = nullptr;
            isActive = false;
        }
    }

    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}

    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
        int startSample, int numSamples) override
    {
        if (!isActive || activeData == nullptr) return;

        auto localData = activeData;
        if (localData == nullptr) return;

        const auto& click = localData->getClickBuffer();
        const auto& sustain = localData->getSustainBuffer();

        const int clickSamples = click.getNumSamples();
        const int sustainSamples = sustain.getNumSamples();

        float* outL = outputBuffer.getWritePointer(0, startSample);
        float* outR = outputBuffer.getNumChannels() > 1 ? outputBuffer.getWritePointer(1, startSample) : nullptr;

        double pitchRatio = 1.0;
        if (auto currentSampleRate = getSampleRate(); currentSampleRate > 0 && activeData->getSampleRate() > 0)
        {
            pitchRatio = activeData->getSampleRate() / currentSampleRate;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            float clickVal = 0.0f;
            float sustainVal = 0.0f;

            int cIdx = static_cast<int>(clickReadIndex);
            if (cIdx < clickSamples)
            {
                clickVal = click.getReadPointer(0)[cIdx] * triggerVelocity;
                clickReadIndex += pitchRatio;
            }

            int sIdx = static_cast<int>(sustainReadIndex);
            if (sIdx < sustainSamples)
            {
                sustainVal = sustain.getReadPointer(0)[sIdx] * triggerVelocity;
                sustainReadIndex += pitchRatio;
            }

            const float mixedVal = clickVal + sustainVal;

            if (outL != nullptr) outL[i] += mixedVal;
            if (outR != nullptr) outR[i] += mixedVal;

            if (static_cast<int>(clickReadIndex) >= clickSamples && static_cast<int>(sustainReadIndex) >= sustainSamples)
            {
                clearCurrentNote();
                activeData = nullptr;
                isActive = false;
                break;
            }
        }
    }

private:
    std::shared_ptr<SharedSampleData> activeData{ nullptr };
    bool isActive{ false };
    double clickReadIndex = 0.0;
    double sustainReadIndex = 0.0;
    float triggerVelocity = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThreadSafeSamplerVoice)
};