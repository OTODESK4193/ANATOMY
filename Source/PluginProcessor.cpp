#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "DSP/Effects/ADAA_Saturation.h"
#include "DSP/Effects/BitCrusher.h"
#include "DSP/Effects/NoiseGenerator.h"
#include "DSP/Effects/OTT_Multiband.h"
#include "DSP/Effects/Limiter.h"
#include <cmath>
#include <algorithm>

AnatomyAudioProcessor::AnatomyAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    juce::Thread("AnatomyTimeDomainThread"),
    apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    activeVoice.reset();
    activeIsMuting = false;
    activeMuteGain = 1.0f;

    for (int i = 0; i < maxReleasingVoices; ++i)
    {
        releasingVoices[i].reset();
        releasingIsMuting[i] = false;
        releasingMuteGain[i] = 1.0f;
    }

    auto instantiatePool = [](std::unique_ptr<AudioEffect>* pool) {
        pool[0] = std::make_unique<ADAA_Saturation>();
        pool[1] = std::make_unique<BitCrusher>();
        pool[2] = std::make_unique<NoiseGenerator>();
        pool[3] = std::make_unique<OTT_Multiband>();
        pool[4] = std::make_unique<Limiter>();
        };

    instantiatePool(transientPool);
    instantiatePool(tonalPool);
    instantiatePool(fullMixPool);

    apvts.addParameterListener("clickLength", this);
    apvts.addParameterListener("clickCurve", this);
}

AnatomyAudioProcessor::~AnatomyAudioProcessor()
{
    apvts.removeParameterListener("clickLength", this);
    apvts.removeParameterListener("clickCurve", this);

    signalThreadShouldExit();
    stopThread(4000);

    for (auto* oldData : garbageBin)
    {
        if (oldData != nullptr)
            delete oldData;
    }
    garbageBin.clear();

    for (auto* oldFxSnapshot : fxGarbageBin)
    {
        if (oldFxSnapshot != nullptr)
            delete oldFxSnapshot;
    }
    fxGarbageBin.clear();

    SharedSampleData* oldData = masterSampleData.exchange(nullptr, std::memory_order_acq_rel);
    if (oldData != nullptr)
    {
        delete oldData;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout AnatomyAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("clickLength", 1), "Click Hold (ms)", 0.0f, 50.0f, 2.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("clickCurve", 1), "Sustain Fade-In (ms)", 1.0f, 100.0f, 15.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("transPitch", 1), "Transient Pitch (st)", -12.0f, 12.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("tonalPitch", 1), "Sustain Pitch (st)", -12.0f, 12.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("sustainRelease", 1), "Sustain Release (ms)", 10.0f, 5000.0f, 500.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("satDrive", 1), "Saturation Drive", 1.0f, 16.0f, 2.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("satMix", 1), "Saturation Mix", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("satAsym", 1), "Saturation Asymmetry", 0.0f, 1.0f, 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("bcBits", 1), "Bitcrusher Bits", 2.0f, 24.0f, 8.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("bcDown", 1), "Bitcrusher Downsample", 1.0f, 32.0f, 4.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("bcMix", 1), "Bitcrusher Mix", 0.0f, 1.0f, 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("bcJitter", 1), "Bitcrusher Jitter", 0.0f, 1.0f, 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("nsDecay", 1), "Noise Decay (ms)", 1.0f, 1000.0f, 100.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("nsMix", 1), "Noise Mix", 0.0f, 1.0f, 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID("nsPink", 1), "Noise Type Pink", false));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("ottDepth", 1), "OTT Depth", 0.0f, 1.0f, 0.7f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("ottTime", 1), "OTT Time Multiplier", 0.1f, 10.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("ottOutGain", 1), "OTT OutGain (dB)", -24.0f, 24.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("ottXOver", 1), "OTT Crossover Freq", 100.0f, 1000.0f, 200.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("limCeil", 1), "Limiter Ceiling (dB)", -24.0f, 0.0f, -0.1f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("limMix", 1), "Limiter Mix", 0.0f, 1.0f, 1.0f));

    return { params.begin(), params.end() };
}

void AnatomyAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    separator.prepare(sampleRate);

    float rampSamples = static_cast<float> (std::max(1.0, 0.0015 * sampleRate));
    releaseFactor = std::exp(std::log(0.001f) / rampSamples);

    activeVoice.reallocateShifters(sampleRate);
    for (int i = 0; i < maxReleasingVoices; ++i)
    {
        releasingVoices[i].reallocateShifters(sampleRate);
    }

    const int safetyBufferSize = std::max(4096, samplesPerBlock * 2);
    transientBlockBuffer.setSize(2, safetyBufferSize, false, false, true);
    tonalBlockBuffer.setSize(2, safetyBufferSize, false, false, true);

    for (int i = 0; i < 5; ++i)
    {
        transientPool[i]->prepare(sampleRate, safetyBufferSize);
        tonalPool[i]->prepare(sampleRate, safetyBufferSize);
        fullMixPool[i]->prepare(sampleRate, safetyBufferSize);
    }

    transientChain.prepare(sampleRate, safetyBufferSize);
    tonalChain.prepare(sampleRate, safetyBufferSize);
    fullMixChain.prepare(sampleRate, safetyBufferSize);
}

void AnatomyAudioProcessor::releaseResources() {}

void AnatomyAudioProcessor::synchronizePoolParameters() noexcept
{
    const float satDrive = apvts.getRawParameterValue("satDrive")->load();
    const float satMix = apvts.getRawParameterValue("satMix")->load();
    const float satAsym = apvts.getRawParameterValue("satAsym")->load();

    const float bcBits = apvts.getRawParameterValue("bcBits")->load();
    const float bcDown = apvts.getRawParameterValue("bcDown")->load();
    const float bcMix = apvts.getRawParameterValue("bcMix")->load();
    const float bcJitter = apvts.getRawParameterValue("bcJitter")->load();

    const float nsDecay = apvts.getRawParameterValue("nsDecay")->load();
    const float nsMix = apvts.getRawParameterValue("nsMix")->load();
    const bool nsPink = apvts.getRawParameterValue("nsPink")->load() > 0.5f;

    const float ottDepth = apvts.getRawParameterValue("ottDepth")->load();
    const float ottTime = apvts.getRawParameterValue("ottTime")->load();
    const float ottOutGain = apvts.getRawParameterValue("ottOutGain")->load();
    const float ottXOver = apvts.getRawParameterValue("ottXOver")->load();

    const float limCeil = apvts.getRawParameterValue("limCeil")->load();
    const float limMix = apvts.getRawParameterValue("limMix")->load();

    auto syncInstance = [&](AudioEffect* fx) noexcept {
        if (fx == nullptr) return;
        if (auto* sat = dynamic_cast<ADAA_Saturation*> (fx)) {
            sat->setDrive(satDrive);
            sat->setMix(satMix);
            sat->setAsymmetry(satAsym);
        }
        else if (auto* bc = dynamic_cast<BitCrusher*> (fx)) {
            bc->setBits(bcBits);
            bc->setDownsample(bcDown);
            bc->setMix(bcMix);
            bc->setJitter(bcJitter);
        }
        else if (auto* ns = dynamic_cast<NoiseGenerator*> (fx)) {
            ns->setDecay(nsDecay);
            ns->setMix(nsMix);
            ns->setPink(nsPink);
        }
        else if (auto* ott = dynamic_cast<OTT_Multiband*> (fx)) {
            ott->setMix(ottDepth);
            ott->setTimeMultiplier(ottTime);
            ott->setOutGainDb(ottOutGain);
            ott->setCrossoverFreq(ottXOver);
        }
        else if (auto* lim = dynamic_cast<Limiter*> (fx)) {
            lim->setCeiling(limCeil);
            lim->setMix(limMix);
        }
        };

    for (int i = 0; i < 5; ++i)
    {
        syncInstance(transientPool[i].get());
        syncInstance(tonalPool[i].get());
        syncInstance(fullMixPool[i].get());
    }
}

void AnatomyAudioProcessor::updateRouteOrder(TargetRoute route, const std::vector<int>& activeEffectIndices)
{
    std::vector<AudioEffect*> sortedFX;
    sortedFX.reserve(activeEffectIndices.size());

    for (const int idx : activeEffectIndices)
    {
        if (idx < 0 || idx >= 5) continue;

        if (route == TargetRoute::Transient)     sortedFX.push_back(transientPool[idx].get());
        else if (route == TargetRoute::Tonal)    sortedFX.push_back(tonalPool[idx].get());
        else if (route == TargetRoute::FullMix)  sortedFX.push_back(fullMixPool[idx].get());
    }

    if (route == TargetRoute::Transient)     transientChain.updateChain(sortedFX, fxGarbageBin);
    else if (route == TargetRoute::Tonal)    tonalChain.updateChain(sortedFX, fxGarbageBin);
    else if (route == TargetRoute::FullMix)  fullMixChain.updateChain(sortedFX, fxGarbageBin);
}
void AnatomyAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (transientBlockBuffer.getNumSamples() < numSamples ||
        transientBlockBuffer.getNumChannels() < numChannels ||
        tonalBlockBuffer.getNumSamples() < numSamples ||
        tonalBlockBuffer.getNumChannels() < numChannels)
    {
        return;
    }

    if (std::abs(getSampleRate() - currentSampleRate) > 0.001 && getSampleRate() > 0.0)
    {
        prepareToPlay(getSampleRate(), getBlockSize());
    }

    synchronizePoolParameters();

    transientBlockBuffer.clear();
    tonalBlockBuffer.clear();

    SharedSampleData* rawPtr = masterSampleData.load(std::memory_order_acquire);
    const SharedSampleData* currentDataSnapshot = rawPtr;

    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();

        if (msg.isNoteOn())
        {
            if (auto* nsTrans = dynamic_cast<NoiseGenerator*>(transientPool[2].get())) nsTrans->trigger();
            if (auto* nsTonal = dynamic_cast<NoiseGenerator*>(tonalPool[2].get())) nsTonal->trigger();
            if (auto* nsFull = dynamic_cast<NoiseGenerator*>(fullMixPool[2].get()))  nsFull->trigger();

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

                releasingIsMuting[slotToUse] = true;
                releasingMuteGain[slotToUse] = activeMuteGain;

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

                activeIsMuting = false;
                activeMuteGain = 1.0f;

                activeVoice.resetProcessing();
                customTonalReplacer.reset();
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

    float clickHold = apvts.getRawParameterValue("clickLength")->load();
    float clickCurve = apvts.getRawParameterValue("clickCurve")->load();
    float transPitchVal = apvts.getRawParameterValue("transPitch")->load();
    float tonalPitchVal = apvts.getRawParameterValue("tonalPitch")->load();
    float relMs = apvts.getRawParameterValue("sustainRelease")->load();

    float transScale = std::pow(2.0f, transPitchVal / 12.0f);
    float tonalScale = std::pow(2.0f, tonalPitchVal / 12.0f);

    float rampSamples = static_cast<float> ((relMs / 1000.0f) * currentSampleRate);
    float dynamicReleaseFactor = std::exp(std::log(0.001f) / std::max(1.0f, rampSamples));

    float muteRampSamples = static_cast<float> (0.0015 * currentSampleRate);
    float muteFactor = std::exp(std::log(0.001f) / std::max(1.0f, muteRampSamples));

    float* transL = transientBlockBuffer.getWritePointer(0);
    float* transR = numChannels > 1 ? transientBlockBuffer.getWritePointer(1) : nullptr;
    float* tonalL = tonalBlockBuffer.getWritePointer(0);
    float* tonalR = numChannels > 1 ? tonalBlockBuffer.getWritePointer(1) : nullptr;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float mixedTransL = 0.0f, mixedTransR = 0.0f;
        float mixedTonalL = 0.0f, mixedTonalR = 0.0f;

        if (activeVoice.isActive)
        {
            float vTransL = 0.0f, vTransR = 0.0f;
            float vTonalL = 0.0f, vTonalR = 0.0f;
            generateVoiceSample(activeVoice, vTransL, vTransR, vTonalL, vTonalR, clickHold, clickCurve, transScale, tonalScale);

            mixedTransL += vTransL * activeMuteGain;
            mixedTransR += vTransR * activeMuteGain;
            mixedTonalL += vTonalL * activeMuteGain;
            mixedTonalR += vTonalR * activeMuteGain;

            if (activeVoice.isReleasing)
            {
                activeVoice.releaseGain *= dynamicReleaseFactor;
            }
            if (activeIsMuting)
            {
                activeMuteGain *= muteFactor;
            }

            if (activeVoice.releaseGain <= 0.001f || activeMuteGain <= 0.001f)
            {
                activeVoice.reset();
                activeIsMuting = false;
                activeMuteGain = 1.0f;
            }
        }

        for (int i = 0; i < maxReleasingVoices; ++i)
        {
            if (releasingVoices[i].isActive)
            {
                float vTransL = 0.0f, vTransR = 0.0f;
                float vTonalL = 0.0f, vTonalR = 0.0f;
                generateVoiceSample(releasingVoices[i], vTransL, vTransR, vTonalL, vTonalR, clickHold, clickCurve, transScale, tonalScale);

                mixedTransL += vTransL * releasingMuteGain[i];
                mixedTransR += vTransR * releasingMuteGain[i];
                mixedTonalL += vTonalL * releasingMuteGain[i];
                mixedTonalR += vTonalR * releasingMuteGain[i];

                if (releasingVoices[i].isReleasing)
                {
                    releasingVoices[i].releaseGain *= dynamicReleaseFactor;
                }
                if (releasingIsMuting[i])
                {
                    releasingMuteGain[i] *= muteFactor;
                }

                if (releasingVoices[i].releaseGain <= 0.001f || releasingMuteGain[i] <= 0.001f)
                {
                    releasingVoices[i].reset();
                    releasingIsMuting[i] = false;
                    releasingMuteGain[i] = 1.0f;
                }
            }
        }

        transL[sample] = mixedTransL;
        if (transR != nullptr) transR[sample] = mixedTransR;
        tonalL[sample] = mixedTonalL;
        if (tonalR != nullptr) tonalR[sample] = mixedTonalR;
    }

    transientChain.process(transientBlockBuffer);
    tonalChain.process(tonalBlockBuffer);

    float* outL = buffer.getWritePointer(0);
    float* outR = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        outL[sample] = transL[sample] + tonalL[sample];
        if (outR != nullptr && transR != nullptr && tonalR != nullptr)
        {
            outR[sample] = transR[sample] + tonalR[sample];
        }
    }

    fullMixChain.process(buffer);
}

void AnatomyAudioProcessor::generateVoiceSample(VoiceState& voice,
    float& outTransL, float& outTransR,
    float& outTonalL, float& outTonalR,
    float clickHold, float clickCurve,
    float transScale, float tonalScale) noexcept
{
    outTransL = 0.0f; outTransR = 0.0f;
    outTonalL = 0.0f; outTonalR = 0.0f;

    if (voice.sampleData == nullptr) return;

    const auto& click = voice.sampleData->getClickBuffer();
    const auto& sustain = voice.sampleData->getSustainBuffer();
    const int clickLen = click.getNumSamples();
    const int sustainLen = sustain.getNumSamples();

    float shiftedClick = 0.0f;
    float shiftedSustain = 0.0f;

    int cIdx = static_cast<int> (voice.clickReadIndex);
    int sIdx = static_cast<int> (voice.sustainReadIndex);

    if (customTransientReplacer.isLoaded())
    {
        shiftedClick = customTransientReplacer.processSample(
            voice.clickReadIndex, voice.pitchRatio, transScale,
            clickHold, clickCurve, currentSampleRate, currentSoloMode);

        voice.clickReadIndex += voice.pitchRatio;
    }
    else if (voice.transShifter && cIdx < clickLen)
    {
        if (currentSoloMode != 2)
            shiftedClick = voice.transShifter->processSample(click, cIdx, transScale);

        voice.clickReadIndex += voice.pitchRatio;
    }

    if (customTonalReplacer.isLoaded())
    {
        shiftedSustain = customTonalReplacer.processSample(
            voice.sustainReadIndex, voice.pitchRatio, tonalScale,
            clickHold, clickCurve, currentSampleRate, currentSoloMode);

        voice.sustainReadIndex += voice.pitchRatio;
    }
    else if (voice.tonalShifter && sIdx < sustainLen)
    {
        if (currentSoloMode != 1)
            shiftedSustain = voice.tonalShifter->processSample(sustain, sIdx, tonalScale);

        voice.sustainReadIndex += voice.pitchRatio;
    }

    float finalClick = shiftedClick * voice.triggerVelocity * voice.releaseGain * 0.63f;
    float finalSustain = shiftedSustain * voice.triggerVelocity * voice.releaseGain * 0.63f;

    outTransL = finalClick;
    outTransR = finalClick;
    outTonalL = finalSustain;
    outTonalR = finalSustain;

    bool isCustomActive = customTransientReplacer.isLoaded() || customTonalReplacer.isLoaded();

    if (isCustomActive)
    {
        if (voice.isReleasing && voice.releaseGain <= 0.001f)
        {
            voice.reset();
        }
    }
    else
    {
        bool clickFinished = (static_cast<int> (voice.clickReadIndex) >= clickLen);
        bool sustainFinished = (static_cast<int> (voice.sustainReadIndex) >= sustainLen);

        if (clickFinished && sustainFinished)
        {
            voice.reset();
        }
    }
}

void AnatomyAudioProcessor::parameterChanged(const juce::String&, float)
{
    needsReanalysis.store(true, std::memory_order_release);
}

void AnatomyAudioProcessor::startSeparation(const juce::AudioBuffer<float>& inputAudio, double sourceSampleRate)
{
    cleanUpGarbageBin();

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
    needsReanalysis.store(true, std::memory_order_release);
}

void AnatomyAudioProcessor::handleAsyncReanalysis()
{
    if (isAnalysisFinished.exchange(false, std::memory_order_acq_rel))
    {
        updateActiveSampleData();
    }

    if (!isThreadRunning())
    {
        cleanUpGarbageBin();
    }

    if (!needsReanalysis.load(std::memory_order_acquire)) return;

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
            needsReanalysis.store(false, std::memory_order_release);
            startThread();
        }
        else
        {
            needsReanalysis.store(false, std::memory_order_release);
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

    isAnalysisFinished.store(true, std::memory_order_release);
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
        garbageBin.push_back(oldData);
    }
}

void AnatomyAudioProcessor::cleanUpGarbageBin()
{
    auto it = garbageBin.begin();
    while (it != garbageBin.end())
    {
        SharedSampleData* oldData = *it;
        bool isStillReferencedByVoice = false;

        if (activeVoice.isActive && activeVoice.sampleData == oldData)
        {
            isStillReferencedByVoice = true;
        }

        for (int i = 0; i < maxReleasingVoices; ++i)
        {
            if (releasingVoices[i].isActive && releasingVoices[i].sampleData == oldData)
            {
                isStillReferencedByVoice = true;
            }
        }

        if (!isStillReferencedByVoice)
        {
            if (oldData != nullptr)
            {
                delete oldData;
            }
            it = garbageBin.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (auto* oldFxSnapshot : fxGarbageBin)
    {
        if (oldFxSnapshot != nullptr)
            delete oldFxSnapshot;
    }
    fxGarbageBin.clear();
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