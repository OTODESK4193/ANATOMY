#include "PluginProcessor.h"
#include "PluginEditor.h"

AnatomyAudioProcessor::AnatomyAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    juce::Thread("AnatomyTimeDomainThread")
{
    auto* voice = new ThreadSafeSamplerVoice();
    synth.addVoice(voice);

    auto* sound = new ThreadSafeSamplerSound();
    synth.addSound(sound);
    samplerSound = sound;
}

AnatomyAudioProcessor::~AnatomyAudioProcessor()
{
    stopThread(4000);
}

void AnatomyAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    separator.prepare(sampleRate);
    synth.setCurrentPlaybackSampleRate(sampleRate);
}

void AnatomyAudioProcessor::releaseResources() {}

void AnatomyAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const juce::ScopedLock sl(lock);
    synth.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());
}

void AnatomyAudioProcessor::startSeparation(const juce::AudioBuffer<float>& inputAudio)
{
    if (isThreadRunning())
        stopThread(2000);

    inputBufferThread.makeCopyOf(inputAudio);
    originalBufferThread.makeCopyOf(inputAudio);

    separator.resetProgress();
    startThread();
}

void AnatomyAudioProcessor::run()
{
    juce::AudioBuffer<float> localTrans, localTonal;
    separator.performSeparation(inputBufferThread, localTrans, localTonal);

    if (threadShouldExit()) return;

    {
        const juce::ScopedLock sl(lock);
        transBufferThread.makeCopyOf(localTrans);
        tonalBufferThread.makeCopyOf(localTonal);

        originalBufferUI.makeCopyOf(originalBufferThread);
        transBufferUI.makeCopyOf(localTrans);
        tonalBufferUI.makeCopyOf(localTonal);
    }

    updateSynthSound();
}

void AnatomyAudioProcessor::setSoloMode(int mode)
{
    if (currentSoloMode != mode)
    {
        currentSoloMode = mode;
        updateSynthSound();
    }
}

void AnatomyAudioProcessor::updateSynthSound()
{
    const juce::ScopedLock sl(lock);
    if (samplerSound == nullptr) return;

    const int numSamples = transBufferThread.getNumSamples();
    if (numSamples == 0) return;

    juce::AudioBuffer<float> activeClick(1, numSamples);
    juce::AudioBuffer<float> activeSustain(1, numSamples);
    activeClick.clear();
    activeSustain.clear();

    if (currentSoloMode == 0)
    {
        activeClick.copyFrom(0, 0, transBufferThread, 0, 0, numSamples);
        activeSustain.copyFrom(0, 0, tonalBufferThread, 0, 0, numSamples);
    }
    else if (currentSoloMode == 1)
    {
        activeClick.copyFrom(0, 0, transBufferThread, 0, 0, numSamples);
    }
    else if (currentSoloMode == 2)
    {
        activeSustain.copyFrom(0, 0, tonalBufferThread, 0, 0, numSamples);
    }

    // 修正：C++20の std::atomic<std::shared_ptr> に100%適合するよう std::make_shared でインスタンス化
    auto newData = std::make_shared<SharedSampleData>(std::move(activeClick), std::move(activeSustain), getSampleRate());
    samplerSound->updateSampleData(newData);
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
void AnatomyAudioProcessor::setCurrentProgram(int) {}
const juce::String AnatomyAudioProcessor::getProgramName(int) { return {}; }
void AnatomyAudioProcessor::changeProgramName(int, const juce::String&) {}
void AnatomyAudioProcessor::getStateInformation(juce::MemoryBlock&) {}
void AnatomyAudioProcessor::setStateInformation(const void*, int) {}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AnatomyAudioProcessor();
}