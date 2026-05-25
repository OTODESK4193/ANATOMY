#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <algorithm>

AnatomyAudioProcessor::AnatomyAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    juce::Thread("AnatomyTimeDomainThread"),
    apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    activeVoice.reset();
    for (int i = 0; i < maxReleasingVoices; ++i)
        releasingVoices[i].reset();

    apvts.addParameterListener("clickLength", this);
    apvts.addParameterListener("clickCurve", this);
}

AnatomyAudioProcessor::~AnatomyAudioProcessor()
{
    apvts.removeParameterListener("clickLength", this);
    apvts.removeParameterListener("clickCurve", this);

    signalThreadShouldExit();
    stopThread(4000);

    SharedSampleData* oldData = masterSampleData.exchange(nullptr);
    if (oldData != nullptr)
        delete oldData;
}

juce::AudioProcessorValueTreeState::ParameterLayout AnatomyAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("clickLength", 1), "Click Hold (ms)", 0.0f, 50.0f, 2.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("clickCurve", 1), "Sustain Fade-In (ms)", 1.0f, 100.0f, 15.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("transPitch", 1), "Transient Pitch (st)", -12.0f, 12.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("tonalPitch", 1), "Sustain Pitch (st)", -12.0f, 12.0f, 0.0f));

    return { params.begin(), params.end() };
}

void AnatomyAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;
    separator.prepare(sampleRate);

    float rampSamples = static_cast<float>(std::max(1.0, 0.0015 * sampleRate));
    releaseFactor = std::exp(std::log(0.001f) / rampSamples);

    activeVoice.reallocateShifters(sampleRate);
    for (int i = 0; i < maxReleasingVoices; ++i)
    {
        releasingVoices[i].reallocateShifters(sampleRate);
    }
}

void AnatomyAudioProcessor::releaseResources() {}

void AnatomyAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    SharedSampleData* rawPtr = masterSampleData.load(std::memory_order_acquire);
    const SharedSampleData* currentDataSnapshot = rawPtr;

    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();

        if (msg.isNoteOn())
        {
            if (activeVoice.isActive)
            {
                int slotToUse = 0;
                for (int i = 0; i < maxReleasingVoices; ++i)
                {
                    if (!releasingVoices[i].isActive) { slotToUse = i; break; }
                }

                releasingVoices[slotToUse].sampleData = activeVoice.sampleData;
                releasingVoices[slotToUse].clickReadIndex = activeVoice.clickReadIndex;
                releasingVoices[slotToUse].sustainReadIndex = activeVoice.sustainReadIndex;
                releasingVoices[slotToUse].pitchRatio = activeVoice.pitchRatio;
                releasingVoices[slotToUse].triggerVelocity = activeVoice.triggerVelocity;
                releasingVoices[slotToUse].releaseGain = activeVoice.releaseGain;
                releasingVoices[slotToUse].currentMidiNote = activeVoice.currentMidiNote;
                releasingVoices[slotToUse].isActive = true;
                releasingVoices[slotToUse].isReleasing = true;

                std::swap(releasingVoices[slotToUse].transShifter, activeVoice.transShifter);
                std::swap(releasingVoices[slotToUse].tonalShifter, activeVoice.tonalShifter);

                activeVoice.resetProcessing();
            }

            if (currentDataSnapshot != nullptr)
            {
                activeVoice.sampleData = currentDataSnapshot;
                activeVoice.clickReadIndex = 0.0;
                activeVoice.sustainReadIndex = 0.0;
                activeVoice.triggerVelocity = msg.getFloatVelocity();
                activeVoice.isActive = true;
                activeVoice.isReleasing = false;
                activeVoice.releaseGain = 1.0f;
                activeVoice.currentMidiNote = msg.getNoteNumber();
                activeVoice.pitchRatio = (activeVoice.sampleData->getSampleRate() > 0.0 && currentSampleRate > 0.0)
                    ? (activeVoice.sampleData->getSampleRate() / currentSampleRate) : 1.0;

                activeVoice.resetProcessing();
            }
        }
        else if (msg.isNoteOff())
        {
            if (activeVoice.isActive && activeVoice.currentMidiNote == msg.getNoteNumber())
            {
                activeVoice.isReleasing = true;
            }
        }
    }

    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float mixedL = 0.0f;
        float mixedR = 0.0f;

        if (activeVoice.isActive)
        {
            float vL = 0.0f, vR = 0.0f;
            generateVoiceSample(activeVoice, vL, vR);
            mixedL += vL;
            mixedR += vR;

            if (activeVoice.isReleasing)
            {
                activeVoice.releaseGain *= releaseFactor;
                if (activeVoice.releaseGain <= 0.001f) activeVoice.reset();
            }
        }

        for (int i = 0; i < maxReleasingVoices; ++i)
        {
            if (releasingVoices[i].isActive)
            {
                float vL = 0.0f, vR = 0.0f;
                generateVoiceSample(releasingVoices[i], vL, vR);
                mixedL += vL;
                mixedR += vR;

                releasingVoices[i].releaseGain *= releaseFactor;
                if (releasingVoices[i].releaseGain <= 0.001f) releasingVoices[i].reset();
            }
        }

        outL[sample] += mixedL;
        if (outR != nullptr) outR[sample] += mixedR;
    }
}

void AnatomyAudioProcessor::generateVoiceSample(VoiceState& voice, float& outL, float& outR) noexcept
{
    // 呼び出し元のローカル変数への初期化保証
    outL = 0.0f;
    outR = 0.0f;

    if (voice.sampleData == nullptr) return;

    const auto& click = voice.sampleData->getClickBuffer();
    const auto& sustain = voice.sampleData->getSustainBuffer();
    const int clickLen = click.getNumSamples();
    const int sustainLen = sustain.getNumSamples();

    float transPitchVal = apvts.getRawParameterValue("transPitch")->load();
    float tonalPitchVal = apvts.getRawParameterValue("tonalPitch")->load();
    float transScale = std::pow(2.0f, transPitchVal / 12.0f);
    float tonalScale = std::pow(2.0f, tonalPitchVal / 12.0f);

    float shiftedClick = 0.0f;
    float shiftedSustain = 0.0f;

    int cIdx = static_cast<int>(voice.clickReadIndex);
    int sIdx = static_cast<int>(voice.sustainReadIndex);

    if (voice.transShifter && cIdx < clickLen)
    {
        shiftedClick = voice.transShifter->processSample(click, cIdx, transScale);
        voice.clickReadIndex += voice.pitchRatio;
    }

    if (voice.tonalShifter && sIdx < sustainLen)
    {
        shiftedSustain = voice.tonalShifter->processSample(sustain, sIdx, tonalScale);
        voice.sustainReadIndex += voice.pitchRatio;
    }

    float finalSample = (shiftedClick + shiftedSustain) * voice.triggerVelocity * voice.releaseGain;

    // 【最重要】モノラルからステレオへの複製による 4dB の音量オーバーを相殺する補正係数 (-4dB ≒ 0.63倍)
    finalSample *= 0.63f;

    outL = finalSample;
    outR = finalSample;

    bool clickFinished = (static_cast<int>(voice.clickReadIndex) >= clickLen);
    bool sustainFinished = (static_cast<int>(voice.sustainReadIndex) >= sustainLen);

    if (clickFinished && sustainFinished)
    {
        voice.reset();
    }
}


void AnatomyAudioProcessor::parameterChanged(const juce::String&, float)
{
    needsReanalysis.store(true);
}

void AnatomyAudioProcessor::startSeparation(const juce::AudioBuffer<float>& inputAudio, double sourceSampleRate)
{
    if (isThreadRunning())
        stopThread(2000);

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

    float clickHold = apvts.getRawParameterValue("clickLength")->load();
    float sustainFade = apvts.getRawParameterValue("clickCurve")->load();

    separator.performSeparation(inputBufferThread, localTrans, localTonal, clickHold, sustainFade, this);

    if (threadShouldExit()) return;

    {
        const juce::ScopedLock sl(lock);
        transBufferThread.makeCopyOf(localTrans);
        tonalBufferThread.makeCopyOf(localTonal);

        transBufferUI.makeCopyOf(localTrans);
        tonalBufferUI.makeCopyOf(localTonal);
    }

    updateActiveSampleData();
}

void AnatomyAudioProcessor::setSoloMode(int mode)
{
    if (currentSoloMode != mode)
    {
        currentSoloMode = mode;
        updateActiveSampleData();
    }
}

void AnatomyAudioProcessor::updateActiveSampleData()
{
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

    SharedSampleData* newData = new SharedSampleData(std::move(activeClick), std::move(activeSustain), fileSampleRate);

    SharedSampleData* oldData = masterSampleData.exchange(newData, std::memory_order_acq_rel);
    if (oldData != nullptr)
    {
        delete oldData;
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

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AnatomyAudioProcessor();
}