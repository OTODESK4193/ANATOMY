#include "PluginProcessor.h"
#include "PluginEditor.h"

AnatomyAudioProcessor::AnatomyAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    juce::Thread("AnatomyHpssThread")
{
    synth.addVoice(new AnatomyVoice());
}

AnatomyAudioProcessor::~AnatomyAudioProcessor()
{
    stopThread(4000);
}

void AnatomyAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    separator.prepare(sampleRate);
    synth.setCurrentPlaybackSampleRate(sampleRate);
}

void AnatomyAudioProcessor::releaseResources() {}

void AnatomyAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;

    buffer.clear();

    const juce::ScopedLock sl(lock);
    synth.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());
}

void AnatomyAudioProcessor::startSeparation(const juce::AudioBuffer<float>& inputAudio) {
    if (isThreadRunning())
        stopThread(2000);

    inputBufferThread.makeCopyOf(inputAudio);

    // エディタ表示（2段目）用として、生の原音を永続バッファへ退避
    originalBufferThread.makeCopyOf(inputAudio);

    separator.resetProgress();
    startThread();
}

void AnatomyAudioProcessor::run() {
    juce::AudioBuffer<float> localTrans, localTonal;

    separator.performSeparation(inputBufferThread, localTrans, localTonal);

    if (threadShouldExit()) return;

    {
        const juce::ScopedLock sl(lock);
        transBufferThread.makeCopyOf(localTrans);
        tonalBufferThread.makeCopyOf(localTonal);
    }

    // 分離直後のバッファ状態に基づき、シンセサイザーのサウンドを初期充填
    updateSynthSound();
}

void AnatomyAudioProcessor::setSoloMode(int mode) {
    if (currentSoloMode != mode) {
        currentSoloMode = mode;
        updateSynthSound();
    }
}

void AnatomyAudioProcessor::updateSynthSound() {
    const juce::ScopedLock sl(lock);
    synth.clearSounds();

    const int numSamples = transBufferThread.getNumSamples();
    if (numSamples == 0) return;

    if (currentSoloMode == 0) // Original Mode: Transient と Tonal を両方フルで足して鳴らす
    {
        synth.addSound(new AnatomySound(transBufferThread, tonalBufferThread));
    }
    else if (currentSoloMode == 1) // Transient Solo: Tonal 側を無音（ゼロ）バッファにして充填
    {
        juce::AudioBuffer<float> zeroTonal(1, numSamples);
        zeroTonal.clear();
        synth.addSound(new AnatomySound(transBufferThread, zeroTonal));
    }
    else if (currentSoloMode == 2) // Tonal Solo: Transient 側を無音（ゼロ）バッファにして充填
    {
        juce::AudioBuffer<float> zeroTrans(1, numSamples);
        zeroTrans.clear();
        synth.addSound(new AnatomySound(zeroTrans, tonalBufferThread));
    }
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

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AnatomyAudioProcessor();
}