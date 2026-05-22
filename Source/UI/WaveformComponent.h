#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>

class WaveformComponent : public juce::Component
{
public:
    WaveformComponent();
    void paint(juce::Graphics& g) override;
    void setBuffer(const juce::AudioBuffer<float>& buffer);

private:
    juce::AudioBuffer<float> internalBuffer;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformComponent)
};