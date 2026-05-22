#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

class AnatomyVoice : public juce::SynthesiserVoice
{
public:
    bool canPlaySound(juce::SynthesiserSound*) override { return true; }
    void startNote(int, float, juce::SynthesiserSound*, int) override {}
    void stopNote(float, bool) override { clearCurrentNote(); }
    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}
    void renderNextBlock(juce::AudioBuffer<float>&, int, int) override {}
};