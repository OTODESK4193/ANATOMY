#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/HpssSeparator.h"
#include "DSP/AnatomyVoice.h"

class AnatomyAudioProcessor : public juce::AudioProcessor
{
public:
    AnatomyAudioProcessor();
    ~AnatomyAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    void startSeparation(const juce::AudioBuffer<float>& inputAudio);

private:
    HpssSeparator separator{ 2048 };
    juce::Synthesiser synth;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnatomyAudioProcessor)
};