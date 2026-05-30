#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "DSP/Effects/ADAA_Saturation.h"
#include "DSP/Effects/BitCrusher.h"
#include "DSP/Effects/NoiseGenerator.h"
#include "DSP/Effects/OTT_Multiband.h"
#include "DSP/Effects/Limiter.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <algorithm>

AnatomyAudioProcessor::AnatomyAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    juce::Thread("AnatomyTimeDomainThread"),
    offlineMixRenderer(*this),
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

    auto instantiatePool = [](std::unique_ptr<AudioEffect>* pool, TargetRoute route) {
        pool[0] = std::make_unique<ADAA_Saturation>();
        pool[1] = std::make_unique<BitCrusher>();
        pool[2] = std::make_unique<NoiseGenerator>();
        pool[3] = std::make_unique<OTT_Multiband>();
        pool[4] = std::make_unique<Limiter>();
        for (int i = 0; i < 5; ++i) pool[i]->setTargetRoute(route);
        };

    instantiatePool(transientPool, TargetRoute::Transient);
    instantiatePool(tonalPool, TargetRoute::Tonal);
    instantiatePool(fullMixPool, TargetRoute::FullMix);

    apvts.addParameterListener("clickLength", this);
    apvts.addParameterListener("clickCurve", this);

    juce::StringArray ottParams{ "transOttDepth", "transOttTime", "transOttLowMidXOver", "transOttMidHighXOver", "tonalOttDepth", "tonalOttTime", "tonalOttLowMidXOver", "tonalOttMidHighXOver", "fullOttDepth", "fullOttTime", "fullOttLowMidXOver", "fullOttMidHighXOver", "transPitch", "tonalPitch", "transMixGain", "tonalMixGain" };
    for (const auto& pid : ottParams) apvts.addParameterListener(pid, this);

    offlineMixRenderer.startThread();
}

AnatomyAudioProcessor::~AnatomyAudioProcessor()
{
    apvts.removeParameterListener("clickLength", this);
    apvts.removeParameterListener("clickCurve", this);

    juce::StringArray ottParams{ "transOttDepth", "transOttTime", "transOttLowMidXOver", "transOttMidHighXOver", "tonalOttDepth", "tonalOttTime", "tonalOttLowMidXOver", "tonalOttMidHighXOver", "fullOttDepth", "fullOttTime", "fullOttLowMidXOver", "fullOttMidHighXOver", "transPitch", "tonalPitch", "transMixGain", "tonalMixGain" };
    for (const auto& pid : ottParams) apvts.removeParameterListener(pid, this);

    signalThreadShouldExit();
    stopThread(4000);

    for (auto* oldData : garbageBin) if (oldData != nullptr) delete oldData;
    garbageBin.clear();

    for (auto* oldFxSnapshot : fxGarbageBin) if (oldFxSnapshot != nullptr) delete oldFxSnapshot;
    fxGarbageBin.clear();

    SharedSampleData* oldData = masterSampleData.exchange(nullptr, std::memory_order_acq_rel);
    if (oldData != nullptr) delete oldData;
}

juce::AudioProcessorValueTreeState::ParameterLayout AnatomyAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "clickLength", 1 }, "Click Hold (ms)", 0.0f, 50.0f, 2.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "clickCurve", 1 }, "Sustain Fade-In (ms)", 1.0f, 100.0f, 15.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "transPitch", 1 }, "Transient Pitch (st)", -12.0f, 12.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "tonalPitch", 1 }, "Sustain Pitch (st)", -12.0f, 12.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "sustainRelease", 1 }, "Sustain Release (ms)", 10.0f, 5000.0f, 500.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "transMixGain", 1 }, "Transient Mix Gain (dB)", -60.0f, 6.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "tonalMixGain", 1 }, "Tonal Mix Gain (dB)", -60.0f, 6.0f, 0.0f));

    juce::StringArray prefixes{ "trans", "tonal", "full" };
    for (const auto& pre : prefixes)
    {
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "SatDrive", 1 }, pre + " Saturation Drive", 1.0f, 16.0f, 2.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "SatMix", 1 }, pre + " Saturation Mix", 0.0f, 1.0f, 0.5f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "SatAsym", 1 }, pre + " Saturation Asymmetry", 0.0f, 1.0f, 0.0f));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "BcBits", 1 }, pre + " Bitcrusher Bits", 2.0f, 24.0f, 8.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "BcDown", 1 }, pre + " Bitcrusher Downsample", 1.0f, 32.0f, 4.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "BcMix", 1 }, pre + " Bitcrusher Mix", 0.0f, 1.0f, 0.3f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "BcJitter", 1 }, pre + " Bitcrusher Jitter", 0.0f, 1.0f, 0.0f));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "NsDecay", 1 }, pre + " Noise Decay (ms)", 1.0f, 1000.0f, 100.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "NsMix", 1 }, pre + " Noise Mix", 0.0f, 1.0f, 0.3f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "NsType", 1 }, pre + " Noise Type", 0.0f, 3.0f, 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "NsGain", 1 }, pre + " Noise Gain (dB)", -60.0f, 0.0f, 0.0f));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "OttDepth", 1 }, pre + " OTT Depth", 0.0f, 1.0f, 0.7f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "OttTime", 1 }, pre + " OTT Time Multiplier", 0.1f, 10.0f, 1.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "OttLowMidXOver", 1 }, pre + " OTT Low/Mid X-Over", 40.0f, 1000.0f, 200.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "OttMidHighXOver", 1 }, pre + " OTT Mid/High X-Over", 1000.0f, 15000.0f, 2500.0f));

        juce::StringArray bands{ "Low", "Mid", "High" };
        for (const auto& b : bands)
        {
            params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "Ott" + b + "Up", 1 }, pre + " OTT " + b + " Upward Comp", 0.0f, 1.0f, 1.0f));
            params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "Ott" + b + "Down", 1 }, pre + " OTT " + b + " Downward Comp", 0.0f, 1.0f, 1.0f));
            params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "Ott" + b + "Gain", 1 }, pre + " OTT " + b + " Band Gain", -24.0f, 24.0f, 0.0f));
        }

        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "LimCeil", 1 }, pre + " Limiter Ceiling (dB)", -24.0f, 0.0f, -0.1f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "LimMix", 1 }, pre + " Limiter Mix", 0.0f, 1.0f, 1.0f));
    }

    return { params.begin(), params.end() };
}

void AnatomyAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    separator.prepare(sampleRate);

    float rampSamples = static_cast<float> (std::max(1.0, 0.0015 * sampleRate));
    releaseFactor = std::exp(std::log(0.001f) / rampSamples);

    activeVoice.reallocateShifters(sampleRate);
    for (int i = 0; i < maxReleasingVoices; ++i) releasingVoices[i].reallocateShifters(sampleRate);

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
    auto syncLane = [this](std::unique_ptr<AudioEffect>* pool, const juce::String& pre) noexcept {
        if (auto* sat = dynamic_cast<ADAA_Saturation*>(pool[0].get())) {
            sat->setDrive(apvts.getRawParameterValue(pre + "SatDrive")->load());
            sat->setMix(apvts.getRawParameterValue(pre + "SatMix")->load());
            sat->setAsymmetry(apvts.getRawParameterValue(pre + "SatAsym")->load());
        }
        if (auto* bc = dynamic_cast<BitCrusher*>(pool[1].get())) {
            bc->setBits(apvts.getRawParameterValue(pre + "BcBits")->load());
            bc->setDownsample(apvts.getRawParameterValue(pre + "BcDown")->load());
            bc->setMix(apvts.getRawParameterValue(pre + "BcMix")->load());
            bc->setJitter(apvts.getRawParameterValue(pre + "BcJitter")->load());
        }
        if (auto* ns = dynamic_cast<NoiseGenerator*>(pool[2].get())) {
            ns->setDecay(apvts.getRawParameterValue(pre + "NsDecay")->load());
            ns->setMix(apvts.getRawParameterValue(pre + "NsMix")->load());
            ns->setNoiseType(static_cast<int>(apvts.getRawParameterValue(pre + "NsType")->load()));
            ns->setGainDb(apvts.getRawParameterValue(pre + "NsGain")->load());
        }
        if (auto* ott = dynamic_cast<OTT_Multiband*>(pool[3].get())) {
            ott->setMix(apvts.getRawParameterValue(pre + "OttDepth")->load());
            ott->setTimeMultiplier(apvts.getRawParameterValue(pre + "OttTime")->load());
            ott->setLowMidXOver(apvts.getRawParameterValue(pre + "OttLowMidXOver")->load());
            ott->setMidHighXOver(apvts.getRawParameterValue(pre + "OttMidHighXOver")->load());
            for (int b = 0; b < 3; ++b) {
                juce::String bName = (b == 0) ? "Low" : ((b == 1) ? "Mid" : "High");
                ott->setBandUpward(b, apvts.getRawParameterValue(pre + "Ott" + bName + "Up")->load());
                ott->setBandDownward(b, apvts.getRawParameterValue(pre + "Ott" + bName + "Down")->load());
                ott->setBandGainDb(b, apvts.getRawParameterValue(pre + "Ott" + bName + "Gain")->load());
            }
        }
        if (auto* lim = dynamic_cast<Limiter*>(pool[4].get())) {
            lim->setCeiling(apvts.getRawParameterValue(pre + "LimCeil")->load());
            lim->setMix(apvts.getRawParameterValue(pre + "LimMix")->load());
        }
        };
    syncLane(transientPool, "trans");
    syncLane(tonalPool, "tonal");
    syncLane(fullMixPool, "full");
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

    offlineMixRenderer.triggerRender();
}

void AnatomyAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    float* outL = buffer.getWritePointer(0);
    float* outR = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    bool isBeforeMode = beforeAfterBypasser.julesIsBeforeBypassed();

    if (transientBlockBuffer.getNumSamples() < numSamples || transientBlockBuffer.getNumChannels() < numChannels) return;

    synchronizePoolParameters();
    transientBlockBuffer.clear();
    tonalBlockBuffer.clear();

    SharedSampleData* currentDataSnapshot = masterSampleData.load(std::memory_order_acquire);

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
                for (int i = 0; i < maxReleasingVoices; ++i) { if (!releasingVoices[i].isActive) { slotToUse = i; break; } }
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

                float transStartSamples = (transStartOffsetMs / 1000.0f) * static_cast<float>(fileSampleRate);
                float tonalStartSamples = (tonalStartOffsetMs / 1000.0f) * static_cast<float>(fileSampleRate);

                activeVoice.clickReadIndex = transStartSamples;
                activeVoice.sustainReadIndex = tonalStartSamples;

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
            if (activeVoice.isActive && activeVoice.currentMidiNote == msg.getNoteNumber()) activeVoice.isReleasing = true;
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
    float muteFactor = std::exp(std::log(0.001f) / std::max(1.0f, static_cast<float>(0.0015 * currentSampleRate)));

    float* transL = transientBlockBuffer.getWritePointer(0);
    float* transR = numChannels > 1 ? transientBlockBuffer.getWritePointer(1) : nullptr;
    float* tonalL = tonalBlockBuffer.getWritePointer(0);
    float* tonalR = numChannels > 1 ? tonalBlockBuffer.getWritePointer(1) : nullptr;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float mixedTransL = 0.0f, mixedTransR = 0.0f;
        float mixedTonalL = 0.0f, mixedTonalR = 0.0f;

        float transEndSamples = (transEndOffsetMs / 1000.0f) * static_cast<float>(fileSampleRate);
        float tonalEndSamples = (tonalEndOffsetMs / 1000.0f) * static_cast<float>(fileSampleRate);

        if (activeVoice.isActive)
        {
            float vTransL = 0.0f, vTransR = 0.0f; float vTonalL = 0.0f, vTonalR = 0.0f;
            generateVoiceSample(activeVoice, vTransL, vTransR, vTonalL, vTonalR, clickHold, clickCurve, transScale, tonalScale);

            mixedTransL += vTransL * activeMuteGain; mixedTransR += vTransR * activeMuteGain;
            mixedTonalL += vTonalL * activeMuteGain; mixedTonalR += vTonalR * activeMuteGain;

            if (activeVoice.isReleasing) activeVoice.releaseGain *= dynamicReleaseFactor;
            if (activeIsMuting) activeMuteGain *= muteFactor;

            bool reachEnd = (activeVoice.clickReadIndex >= transEndSamples || activeVoice.sustainReadIndex >= tonalEndSamples);
            if (reachEnd || activeVoice.releaseGain <= 0.001f || activeMuteGain <= 0.001f)
            {
                activeVoice.reset(); activeIsMuting = false; activeMuteGain = 1.0f;
            }
        }

        for (int i = 0; i < maxReleasingVoices; ++i)
        {
            if (releasingVoices[i].isActive)
            {
                float vTransL = 0.0f, vTransR = 0.0f; float vTonalL = 0.0f, vTonalR = 0.0f;
                generateVoiceSample(releasingVoices[i], vTransL, vTransR, vTonalL, vTonalR, clickHold, clickCurve, transScale, tonalScale);

                mixedTransL += vTransL * releasingMuteGain[i]; mixedTransR += vTransR * releasingMuteGain[i];
                mixedTonalL += vTonalL * releasingMuteGain[i]; mixedTonalR += vTonalR * releasingMuteGain[i];

                if (releasingVoices[i].isReleasing) releasingVoices[i].releaseGain *= dynamicReleaseFactor;
                if (releasingIsMuting[i]) releasingMuteGain[i] *= muteFactor;

                bool reachEnd = (releasingVoices[i].clickReadIndex >= transEndSamples || releasingVoices[i].sustainReadIndex >= tonalEndSamples);
                if (reachEnd || releasingVoices[i].releaseGain <= 0.001f || releasingMuteGain[i] <= 0.001f)
                {
                    releasingVoices[i].reset(); releasingIsMuting[i] = false; releasingMuteGain[i] = 1.0f;
                }
            }
        }

        transL[sample] = mixedTransL; if (transR != nullptr) transR[sample] = mixedTransR;
        tonalL[sample] = mixedTonalL; if (tonalR != nullptr) tonalR[sample] = mixedTonalR;
    }

    if (isBeforeMode)
    {
        for (int sample = 0; sample < numSamples; ++sample)
        {
            outL[sample] = transL[sample] + tonalL[sample];
            if (outR != nullptr && transR != nullptr && tonalR != nullptr)
                outR[sample] = transR[sample] + tonalR[sample];
        }
        return;
    }

    transientChain.process(transientBlockBuffer);
    tonalChain.process(tonalBlockBuffer);

    const float transLinearGain = std::pow(10.0f, apvts.getRawParameterValue("transMixGain")->load() / 20.0f);
    const float tonalLinearGain = std::pow(10.0f, apvts.getRawParameterValue("tonalMixGain")->load() / 20.0f);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        outL[sample] = (transL[sample] * transLinearGain) + (tonalL[sample] * tonalLinearGain);
        if (outR != nullptr && transR != nullptr && tonalR != nullptr)
        {
            outR[sample] = (transR[sample] * transLinearGain) + (tonalR[sample] * tonalLinearGain);
        }
    }

    fullMixChain.process(buffer);
}

void AnatomyAudioProcessor::generateVoiceSample(VoiceState& voice,
    float& outTransL, float& outTransR, float& outTonalL, float& outTonalR,
    float clickHold, float clickCurve, float transScale, float tonalScale) noexcept
{
    outTransL = 0.0f; outTransR = 0.0f; outTonalL = 0.0f; outTonalR = 0.0f;
    if (voice.sampleData == nullptr) return;

    const auto& click = voice.sampleData->getClickBuffer();
    const auto& sustain = voice.sampleData->getSustainBuffer();
    const int clickLen = click.getNumSamples();
    const int sustainLen = sustain.getNumSamples();

    float shiftedClick = 0.0f; float shiftedSustain = 0.0f;
    int cIdx = static_cast<int> (voice.clickReadIndex);
    int sIdx = static_cast<int> (voice.sustainReadIndex);

    if (beforeAfterBypasser.julesIsBeforeBypassed())
    {
        if (cIdx < clickLen) shiftedClick = click.getSample(0, cIdx);
        if (sIdx < sustainLen) shiftedSustain = sustain.getSample(0, sIdx);
        voice.clickReadIndex += voice.pitchRatio;
        voice.sustainReadIndex += voice.pitchRatio;
    }
    else
    {
        if (customTransientReplacer.isLoaded())
        {
            shiftedClick = customTransientReplacer.processSample(voice.clickReadIndex, voice.pitchRatio, transScale, clickHold, clickCurve, currentSampleRate, currentSoloMode);
            voice.clickReadIndex += voice.pitchRatio;
        }
        else if (voice.transShifter && cIdx < clickLen)
        {
            if (currentSoloMode != 2) shiftedClick = voice.transShifter->processSample(click, cIdx, transScale);
            voice.clickReadIndex += voice.pitchRatio;
        }

        if (customTonalReplacer.isLoaded())
        {
            shiftedSustain = customTonalReplacer.processSample(voice.sustainReadIndex, voice.pitchRatio, tonalScale, clickHold, clickCurve, currentSampleRate, currentSoloMode);
            voice.sustainReadIndex += voice.pitchRatio;
        }
        else if (voice.tonalShifter && sIdx < sustainLen)
        {
            if (currentSoloMode != 1) shiftedSustain = voice.tonalShifter->processSample(sustain, sIdx, tonalScale);
            voice.sustainReadIndex += voice.pitchRatio;
        }
    }

    float finalClick = shiftedClick * voice.triggerVelocity * voice.releaseGain * 0.63f;
    float finalSustain = shiftedSustain * voice.triggerVelocity * voice.releaseGain * 0.63f;

    outTransL = finalClick; outTransR = finalClick;
    outTonalL = finalSustain; outTonalR = finalSustain;
}

void AnatomyAudioProcessor::parameterChanged(const juce::String&, float)
{
    needsReanalysis.store(true, std::memory_order_release);
    offlineMixRenderer.triggerRender();
}

void AnatomyAudioProcessor::setOffsetsFromUI(bool isTransient, float startMs, float endMs) noexcept
{
    if (isTransient)
    {
        transStartOffsetMs = startMs;
        transEndOffsetMs = endMs;
        customTransientReplacer.setStartOffsetMs(startMs);
        customTransientReplacer.setEndOffsetMs(endMs);
    }
    else
    {
        tonalStartOffsetMs = startMs;
        tonalEndOffsetMs = endMs;
        customTonalReplacer.setStartOffsetMs(startMs);
        customTonalReplacer.setEndOffsetMs(endMs);
    }
    offlineMixRenderer.triggerRender();
}

void AnatomyAudioProcessor::startSeparation(const juce::AudioBuffer<float>& inputAudio, double sourceSampleRate)
{
    cleanUpGarbageBin();
    if (isThreadRunning()) stopThread(2000);
    {
        const juce::ScopedLock sl(lock);
        if (&rawInputBuffer != &inputAudio) rawInputBuffer.makeCopyOf(inputAudio);
        inputBufferThread.makeCopyOf(inputAudio);
        fileSampleRate = sourceSampleRate;
    }
    needsReanalysis.store(true, std::memory_order_release);
}

void AnatomyAudioProcessor::handleAsyncReanalysis()
{
    if (isAnalysisFinished.exchange(false, std::memory_order_acq_rel)) updateActiveSampleData();
    if (!isThreadRunning()) cleanUpGarbageBin();
    if (!needsReanalysis.load(std::memory_order_acquire)) return;

    if (isThreadRunning()) signalThreadShouldExit();
    else
    {
        const juce::ScopedLock sl(lock);
        if (rawInputBuffer.getNumSamples() > 0)
        {
            inputBufferThread.makeCopyOf(rawInputBuffer);
            needsReanalysis.store(false, std::memory_order_release);
            startThread();
        }
        else needsReanalysis.store(false, std::memory_order_release);
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

    double durationMs = (static_cast<double>(transBufferThread.getNumSamples()) / fileSampleRate) * 1000.0;
    transStartOffsetMs = 0.0f;
    transEndOffsetMs = static_cast<float>(durationMs);
    tonalStartOffsetMs = 0.0f;
    tonalEndOffsetMs = static_cast<float>(durationMs);

    offlineMixRenderer.triggerRender();
}

void AnatomyAudioProcessor::setSoloMode(int mode)
{
    if (currentSoloMode != mode)
    {
        currentSoloMode = mode;
        updateActiveSampleData();
        offlineMixRenderer.triggerRender();
    }
}

void AnatomyAudioProcessor::updateActiveSampleData()
{
    const int numSamples = transBufferThread.getNumSamples();
    if (numSamples == 0) return;

    juce::AudioBuffer<float> activeClick(1, numSamples);
    juce::AudioBuffer<float> activeSustain(1, numSamples);
    activeClick.clear(); activeSustain.clear();

    if (currentSoloMode == 0)
    {
        activeClick.copyFrom(0, 0, transBufferThread, 0, 0, numSamples);
        activeSustain.copyFrom(0, 0, tonalBufferThread, 0, 0, numSamples);
    }
    else if (currentSoloMode == 1) activeClick.copyFrom(0, 0, transBufferThread, 0, 0, numSamples);
    else if (currentSoloMode == 2) activeSustain.copyFrom(0, 0, tonalBufferThread, 0, 0, numSamples);

    SharedSampleData* newData = new SharedSampleData(std::move(activeClick), std::move(activeSustain), fileSampleRate);
    SharedSampleData* oldData = masterSampleData.exchange(newData, std::memory_order_acq_rel);
    if (oldData != nullptr) garbageBin.push_back(oldData);
}

void AnatomyAudioProcessor::cleanUpGarbageBin()
{
    auto it = garbageBin.begin();
    while (it != garbageBin.end())
    {
        SharedSampleData* oldData = *it; bool isStillReferencedByVoice = false;
        if (activeVoice.isActive && activeVoice.sampleData == oldData) isStillReferencedByVoice = true;
        for (int i = 0; i < maxReleasingVoices; ++i) { if (releasingVoices[i].isActive && releasingVoices[i].sampleData == oldData) isStillReferencedByVoice = true; }

        if (!isStillReferencedByVoice) { if (oldData != nullptr) delete oldData; it = garbageBin.erase(it); }
        else ++it;
    }
    for (auto* oldFxSnapshot : fxGarbageBin) if (oldFxSnapshot != nullptr) delete oldFxSnapshot;
    fxGarbageBin.clear();
}

// 💥【仕様刷新】各レーン固有のエフェクトが適用された100%加工済みの結果を、ディスクへオンデマンド書き出し
juce::File AnatomyAudioProcessor::createTemporaryWavForExport(int laneIndex)
{
    juce::File tempDir = juce::File::getSpecialLocation(juce::File::SpecialLocationType::tempDirectory);
    juce::File targetWavFile = tempDir.getChildFile("ANATOMY_Export_" + juce::String(juce::Random::getSystemRandom().nextInt64()) + ".wav");

    juce::AudioBuffer<float> exportSource;
    juce::AudioBuffer<float> dummyTrans, dummyTonal;
    std::vector<float> dummyRatios;

    // オフラインレンダラーが裏で完成させた、FX適用済みの各完成バッファをダイレクト吸い上げ
    if (laneIndex == 1)      offlineMixRenderer.getRenderedResults(dummyTrans, exportSource, dummyTonal, dummyRatios); // Transient FX通過済
    else if (laneIndex == 2) offlineMixRenderer.getRenderedResults(dummyTrans, dummyTrans, exportSource, dummyRatios); // Tonal FX通過済
    else                     offlineMixRenderer.getRenderedResults(exportSource, dummyTrans, dummyTonal, dummyRatios); // FullMix Master FX通過済

    if (exportSource.getNumSamples() == 0) return {};

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(
        new juce::FileOutputStream(targetWavFile), fileSampleRate, exportSource.getNumChannels(), 24, {}, 0));

    if (writer != nullptr)
    {
        writer->writeFromAudioSampleBuffer(exportSource, 0, exportSource.getNumSamples());
        writer.reset();
        return targetWavFile;
    }

    return {};
}

// 💥【大改造】脳内レンダリング内で、Transient と Tonal に対してもエフェクトを焼き込みマウント
void OfflineMixRenderer::executeRender()
{
    juce::AudioBuffer<float> localTrans, localTonal;
    double sr = 44100.0;

    {
        const juce::ScopedLock sl(processor.lock);
        localTrans.makeCopyOf(processor.transBufferThread);
        localTonal.makeCopyOf(processor.tonalBufferThread);
        sr = processor.fileSampleRate;
    }

    const int numSamples = localTrans.getNumSamples();
    if (numSamples == 0) return;

    juce::AudioBuffer<float> workTrans(2, numSamples);
    juce::AudioBuffer<float> workTonal(2, numSamples);
    workTrans.clear(); workTonal.clear();

    for (int ch = 0; ch < 2; ++ch)
    {
        int srcCh = std::min(ch, localTrans.getNumChannels() - 1);
        workTrans.copyFrom(ch, 0, localTrans, srcCh, 0, numSamples);

        int srcTonalCh = std::min(ch, localTonal.getNumChannels() - 1);
        workTonal.copyFrom(ch, 0, localTonal, srcTonalCh, 0, numSamples);
    }

    float transPitch = processor.apvts.getRawParameterValue("transPitch")->load();
    float tonalPitch = processor.apvts.getRawParameterValue("tonalPitch")->load();

    auto applyPitch = [](juce::AudioBuffer<float>& buf, float semitones) {
        if (std::abs(semitones) < 0.01f) return;
        float ratio = std::pow(2.0f, semitones / 12.0f);
        juce::AudioBuffer<float> copy(buf);
        buf.clear();
        int maxSamples = buf.getNumSamples();
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            float* dest = buf.getWritePointer(ch);
            const float* src = copy.getReadPointer(ch);
            for (int s = 0; s < maxSamples; ++s)
            {
                double srcIdx = s * ratio;
                if (srcIdx < maxSamples) dest[s] = src[static_cast<int>(srcIdx)];
            }
        }
        };
    applyPitch(workTrans, transPitch);
    applyPitch(workTonal, tonalPitch);

    // 各レーン固有のEffectChain（エフェクト）を完全通過！
    processor.transientChain.process(workTrans);
    processor.tonalChain.process(workTonal);

    float transGain = std::pow(10.0f, processor.apvts.getRawParameterValue("transMixGain")->load() / 20.0f);
    float tonalGain = std::pow(10.0f, processor.apvts.getRawParameterValue("tonalMixGain")->load() / 20.0f);

    juce::AudioBuffer<float> outputMix(2, numSamples);
    std::vector<float> ratios(numSamples, 0.5f);

    for (int s = 0; s < numSamples; ++s)
    {
        float tL = workTrans.getSample(0, s) * transGain;
        float tR = workTrans.getSample(1, s) * transGain;
        float oL = workTonal.getSample(0, s) * tonalGain;
        float oR = workTonal.getSample(1, s) * tonalGain;

        outputMix.setSample(0, s, tL + oL);
        outputMix.setSample(1, s, tR + oR);

        float tEnergy = (tL * tL) + (tR * tR);
        float oEnergy = (oL * oL) + (oR * oR);
        float sum = tEnergy + oEnergy;

        if (sum > 1.0e-6f) ratios[s] = tEnergy / sum;
        else               ratios[s] = 0.5f;
    }

    // マスター FullMix エフェクトチェーンの通過
    processor.fullMixChain.process(outputMix);

    {
        // 💥ロック下で3レーンすべてのFX通過後バッファを鉄壁退避
        const juce::ScopedLock sl(renderLock);
        renderedFullMix.makeCopyOf(outputMix);
        renderedTransient.makeCopyOf(workTrans);
        renderedTonal.makeCopyOf(workTonal);
        componentRatios = std::move(ratios);
    }

    juce::MessageManager::callAsync([&processor = this->processor]() {
        if (auto* editor = processor.getActiveEditor())
            editor->repaint();
        });
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