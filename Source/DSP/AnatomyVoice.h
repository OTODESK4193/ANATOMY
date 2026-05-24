#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
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
    SharedSampleData::Ptr activeData{ nullptr };
    bool isActive{ false };
    double clickReadIndex = 0.0;
    double sustainReadIndex = 0.0;
    double pitchRatio = 1.0;
    float triggerVelocity = 0.0f;

    // 【Simpler Gateモード専用】即座に滑らかに消音するためのリリースレジスタ
    bool isReleasing{ false };
    float releaseGain = 1.0f;
    float releaseStep = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnatomyVoice)
};