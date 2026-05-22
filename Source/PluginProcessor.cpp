#include "PluginProcessor.h"
#include "PluginEditor.h"

AnatomyAudioProcessor::AnatomyAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    synth.addVoice(new AnatomyVoice());
}

AnatomyAudioProcessor::~AnatomyAudioProcessor() {}

void AnatomyAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    separator.prepare(sampleRate);
    synth.setCurrentPlaybackSampleRate(sampleRate);
}

void AnatomyAudioProcessor::releaseResources() {}

void AnatomyAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;
    synth.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());
}

void AnatomyAudioProcessor::startSeparation(const juce::AudioBuffer<float>& inputAudio) {
    juce::AudioBuffer<float> trans, tonal;
    separator.performSeparation(inputAudio, trans, tonal);
}

juce::AudioProcessorEditor* AnatomyAudioProcessor::createEditor() { return new AnatomyAudioProcessorEditor(*this); }
bool AnatomyAudioProcessor::hasEditor() const { return true; }
const juce::String AnatomyAudioProcessor::getName() const { return "ANATOMY"; }
bool AnatomyAudioProcessor::acceptsMidi() const { return true; }
bool AnatomyAudioProcessor::producesMidi() const { return false; }
bool AnatomyAudioProcessor::isMidiEffect() const { return false; }
double AnatomyAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int AnatomyAudioProcessor::getNumPrograms() { return 1; }
int AnatomyAudioProcessor::getCurrentProgram() { return 0; }
void AnatomyAudioProcessor::setCurrentProgram(int index) {}
const juce::String AnatomyAudioProcessor::getProgramName(int index) { return {}; }
void AnatomyAudioProcessor::changeProgramName(int index, const juce::String& newName) {}
void AnatomyAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {}
void AnatomyAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {}

// エントリーポイント
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AnatomyAudioProcessor();
}