#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include "AnatomySound.h"

class AnatomyVoice : public juce::SynthesiserVoice
{
public:
    AnatomyVoice() = default;
    ~AnatomyVoice() override = default;

    bool canPlaySound(juce::SynthesiserSound* sound) override;
    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound* sound, int currentPitchWheelPosition) override;
    void stopNote(float velocity, bool allowTailOff) override;
    void pitchWheelMoved(int newPitchWheelValue) override;
    void controllerMoved(int controllerNumber, int newControllerValue) override;
    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override;

private:
    std::shared_ptr<SharedSampleData> activeData{ nullptr };
    bool isActive{ false };
    double clickReadIndex = 0.0;
    double sustainReadIndex = 0.0;
    double pitchRatio = 1.0;
    float triggerVelocity = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnatomyVoice)
};