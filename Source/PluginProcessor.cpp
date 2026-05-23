#include "PluginProcessor.h"
#include "PluginEditor.h"

AnatomyAudioProcessor::AnatomyAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    juce::Thread("AnatomyTimeDomainThread"),
    apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    auto* voice = new AnatomyVoice();
    synth.addVoice(voice);

    auto* sound = new AnatomySound();
    synth.addSound(sound);
    samplerSound = sound;

    apvts.addParameterListener("sensitivity", this);
    apvts.addParameterListener("clickLength", this);
    apvts.addParameterListener("clickCurve", this);
    apvts.addParameterListener("lookAhead", this);
}

AnatomyAudioProcessor::~AnatomyAudioProcessor()
{
    apvts.removeParameterListener("sensitivity", this);
    apvts.removeParameterListener("clickLength", this);
    apvts.removeParameterListener("clickCurve", this);
    apvts.removeParameterListener("lookAhead", this);

    signalThreadShouldExit();
    stopThread(4000);
}

juce::AudioProcessorValueTreeState::ParameterLayout AnatomyAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("sensitivity", 1), "Attack Sensitivity", 0.1f, 2.0f, 0.8f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("clickLength", 1), "Click Hold (ms)", 0.0f, 50.0f, 2.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("clickCurve", 1), "Sustain Fade-In (ms)", 1.0f, 100.0f, 15.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("lookAhead", 1), "Pre-Attack Lookahead (ms)", 0.0f, 5.0f, 1.5f));

    return { params.begin(), params.end() };
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

void AnatomyAudioProcessor::parameterChanged(const juce::String&, float)
{
    needsReanalysis.store(true);
}

void AnatomyAudioProcessor::startSeparation(const juce::AudioBuffer<float>& inputAudio, double sourceSampleRate)
{
    {
        const juce::ScopedLock sl(lock);
        if (&rawInputBuffer != &inputAudio)
        {
            rawInputBuffer.makeCopyOf(inputAudio);
        }
        inputBufferThread.makeCopyOf(inputAudio);
        fileSampleRate = sourceSampleRate;
    }
    needsReanalysis.store(true);
}

void AnatomyAudioProcessor::handleAsyncReanalysis()
{
    if (!needsReanalysis.load()) return;

    if (isThreadRunning())
    {
        signalThreadShouldExit();
    }
    else
    {
        const juce::ScopedLock sl(lock);
        if (rawInputBuffer.getNumSamples() > 0)
        {
            inputBufferThread.makeCopyOf(rawInputBuffer);
            needsReanalysis.store(false);
            startThread();
        }
        else
        {
            needsReanalysis.store(false);
        }
    }
}

void AnatomyAudioProcessor::run()
{
    juce::AudioBuffer<float> localTrans, localTonal;

    float sensitivity = apvts.getRawParameterValue("sensitivity")->load();
    float clickHold = apvts.getRawParameterValue("clickLength")->load();
    float sustainFade = apvts.getRawParameterValue("clickCurve")->load();
    float lookAhead = apvts.getRawParameterValue("lookAhead")->load();

    separator.performSeparation(inputBufferThread, localTrans, localTonal,
        sensitivity, clickHold, sustainFade, lookAhead, this);

    if (threadShouldExit()) return;

    {
        const juce::ScopedLock sl(lock);
        transBufferThread.makeCopyOf(localTrans);
        tonalBufferThread.makeCopyOf(localTonal);

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

    auto newData = std::make_shared<SharedSampleData>(std::move(activeClick), std::move(activeSustain), fileSampleRate);
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

void AnatomyAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void AnatomyAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xmlState = getXmlFromBinary(data, sizeInBytes))
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

#if defined(_MSC_VER)
#pragma comment(linker, "/export:?createPluginFilter@@YAPEAVAudioProcessor@juce@@XZ")
#endif

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AnatomyAudioProcessor();
}