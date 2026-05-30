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

    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "clickLength", 1 }, "Click Hold (ms)", 0.0f, 50.0f, 10.0f));  // 初期値を 10.0 ms へ
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "clickCurve", 1 }, "Sustain Fade-In (ms)", 1.0f, 100.0f, 5.0f)); // 初期値を 5.0 ms へ
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
        float transHoldSamples = (clickHold / 1000.0f) * static_cast<float>(fileSampleRate);

        if (activeVoice.isActive)
        {
            float vTransL = 0.0f, vTransR = 0.0f; float vTonalL = 0.0f, vTonalR = 0.0f;
            generateVoiceSample(activeVoice, vTransL, vTransR, vTonalL, vTonalR, clickHold, clickCurve, transScale, tonalScale);

            mixedTransL += vTransL * activeMuteGain; mixedTransR += vTransR * activeMuteGain;
            mixedTonalL += vTonalL * activeMuteGain; mixedTonalR += vTonalR * activeMuteGain;

            if (activeVoice.isReleasing) activeVoice.releaseGain *= dynamicReleaseFactor;
            if (activeIsMuting) activeMuteGain *= muteFactor;

            // 💥【核心修正③】無音化バグ完全粉砕。相対経過時間（clickReadIndex）が純粋にHold上限に達したかでTransientゲートを切断
            bool transTimeUp = (activeVoice.clickReadIndex >= transHoldSamples);
            double exactClick = ((transStartOffsetMs / 1000.0f) * fileSampleRate) + activeVoice.clickReadIndex;
            if (transTimeUp || exactClick >= transEndSamples)
            {
                mixedTransL = 0.0f; mixedTransR = 0.0f;
            }

            double exactSustainIdx = ((tonalStartOffsetMs / 1000.0f) * fileSampleRate) + activeVoice.sustainReadIndex;
            if (exactSustainIdx >= tonalEndSamples || activeVoice.releaseGain <= 0.001f || activeMuteGain <= 0.001f)
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

                bool transTimeUp = (releasingVoices[i].clickReadIndex >= transHoldSamples);
                double exactClick = ((transStartOffsetMs / 1000.0f) * fileSampleRate) + releasingVoices[i].clickReadIndex;
                if (transTimeUp || exactClick >= transEndSamples)
                {
                    mixedTransL = 0.0f; mixedTransR = 0.0f;
                }

                double exactSustainIdx = ((tonalStartOffsetMs / 1000.0f) * fileSampleRate) + releasingVoices[i].sustainReadIndex;
                if (exactSustainIdx >= tonalEndSamples || releasingVoices[i].releaseGain <= 0.001f || releasingMuteGain[i] <= 0.001f)
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
            outR[sample] = (transL[sample] * transLinearGain) + (tonalR[sample] * tonalLinearGain);
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

    // 💥【核心修正①】BeforeバイパスON時はカスタムバッファを強制無視！大元の分離スレッド生波形を参照
    bool isBefore = beforeAfterBypasser.julesIsBeforeBypassed();
    const auto& click = (isBefore || customTransBuffer.getNumSamples() == 0) ? transBufferThread : customTransBuffer;
    const auto& sustain = (isBefore || customTonalBuffer.getNumSamples() == 0) ? tonalBufferThread : customTonalBuffer;

    float transStartSamples = (transStartOffsetMs / 1000.0f) * static_cast<float>(fileSampleRate);
    float transEndSamples = (transEndOffsetMs / 1000.0f) * static_cast<float>(fileSampleRate);
    float transHoldSamples = (clickHold / 1000.0f) * static_cast<float>(fileSampleRate);

    float tonalStartSamples = (tonalStartOffsetMs / 1000.0f) * static_cast<float>(fileSampleRate);
    float tonalEndSamples = (tonalEndOffsetMs / 1000.0f) * static_cast<float>(fileSampleRate);

    double exactClickIdx = transStartSamples + voice.clickReadIndex;
    double exactSustainIdx = tonalStartSamples + voice.sustainReadIndex;

    int cIdx = static_cast<int>(exactClickIdx);
    int sIdx = static_cast<int>(exactSustainIdx);

    bool transGateOpen = (voice.clickReadIndex < transHoldSamples) && (exactClickIdx < transEndSamples);
    bool tonalGateOpen = (exactSustainIdx < tonalEndSamples);

    float shiftedClick = 0.0f;
    float shiftedSustain = 0.0f;

    if (isBefore)
    {
        if (transGateOpen && cIdx < click.getNumSamples())   shiftedClick = click.getSample(0, cIdx);
        if (tonalGateOpen && sIdx < sustain.getNumSamples()) shiftedSustain = sustain.getSample(0, sIdx);
        voice.clickReadIndex += voice.pitchRatio;
        voice.sustainReadIndex += voice.pitchRatio;
    }
    else
    {
        if (transGateOpen)
        {
            if (customTransBuffer.getNumSamples() > 0)
                shiftedClick = customTransientReplacer.processSample(exactClickIdx, voice.pitchRatio, transScale, clickHold, clickCurve, currentSampleRate, currentSoloMode);
            else if (voice.transShifter && cIdx < click.getNumSamples() && currentSoloMode != 2)
                shiftedClick = voice.transShifter->processSample(click, cIdx, transScale);
        }
        voice.clickReadIndex += voice.pitchRatio;

        if (tonalGateOpen)
        {
            if (customTonalBuffer.getNumSamples() > 0)
                shiftedSustain = customTonalReplacer.processSample(exactSustainIdx, voice.pitchRatio, tonalScale, clickHold, clickCurve, currentSampleRate, currentSoloMode);
            else if (voice.tonalShifter && sIdx < sustain.getNumSamples() && currentSoloMode != 1)
                shiftedSustain = voice.tonalShifter->processSample(sustain, sIdx, tonalScale);
        }
        voice.sustainReadIndex += voice.pitchRatio;
    }

    // 💥【音響数理修正⑥：金属質コームフィルター音の完全消去】
    // クロスフェード進捗を等熱量（Constant-Power）カーブへ刷新し、合算時の過渡期の熱量窪みを完全平坦化
    float transFade = 1.0f;
    float tonalFade = 1.0f;
    float currentMs = (voice.sustainReadIndex / fileSampleRate) * 1000.0f;

    if (currentMs < clickCurve && clickCurve > 0.0f)
    {
        float progress = currentMs / clickCurve;
        float angle = progress * juce::MathConstants<float>::halfPi;
        transFade = std::cos(angle);
        tonalFade = std::sin(angle);
    }

    float finalClick = shiftedClick * voice.triggerVelocity * voice.releaseGain * transFade * 0.63f;
    float finalSustain = shiftedSustain * voice.triggerVelocity * voice.releaseGain * tonalFade * 0.63f;

    outTransL = finalClick; outTransR = finalClick;
    outTonalL = finalSustain; outTonalR = finalSustain;
}

// 💥【ポップノイズ遮断数理⑦：ゼロクロス・自動シニカルスナップ探索】
int AnatomyAudioProcessor::snapToZeroCrossing(const juce::AudioBuffer<float>& buffer, int targetSample) noexcept
{
    const int totalSamples = buffer.getNumSamples();
    if (totalSamples <= 0 || buffer.getNumChannels() <= 0) return targetSample;

    const float* data = buffer.getReadPointer(0);
    int bestSample = targetSample;
    float minAbsVal = 1e9f;
    const int searchRange = 150; // 前後150サンプルの局所的な窓から絶対値最小（ゼロクロス）の特異点を発見

    int startRange = std::max(0, targetSample - searchRange);
    int endRange = std::min(totalSamples - 1, targetSample + searchRange);

    for (int s = startRange; s <= endRange; ++s)
    {
        float absVal = std::abs(data[s]);
        if (absVal < minAbsVal)
        {
            minAbsVal = absVal;
            bestSample = s;
        }
    }
    return bestSample;
}

void AnatomyAudioProcessor::parameterChanged(const juce::String&, float)
{
    needsReanalysis.store(true, std::memory_order_release);
    offlineMixRenderer.triggerRender();
}

void AnatomyAudioProcessor::setOffsetsFromUI(bool isTransient, float startMs, float endMs) noexcept
{
    const juce::ScopedLock sl(lock);

    // 現在アクティブなバッファから安全にゼロクロスを探索してノブと波形へ直流フィードバック
    const juce::AudioBuffer<float>& activeTrans = (customTransBuffer.getNumSamples() > 0) ? customTransBuffer : transBufferThread;
    const juce::AudioBuffer<float>& activeTonal = (customTonalBuffer.getNumSamples() > 0) ? customTonalBuffer : tonalBufferThread;

    int startSmp = static_cast<int>((startMs / 1000.0f) * fileSampleRate);

    if (isTransient)
    {
        startSmp = snapToZeroCrossing(activeTrans, startSmp);
        transStartOffsetMs = (static_cast<float>(startSmp) / fileSampleRate) * 1000.0f;
        transEndOffsetMs = endMs;
        customTransientReplacer.setStartOffsetMs(transStartOffsetMs);
        customTransientReplacer.setEndOffsetMs(endMs);
    }
    else
    {
        startSmp = snapToZeroCrossing(activeTonal, startSmp);
        tonalStartOffsetMs = (static_cast<float>(startSmp) / fileSampleRate) * 1000.0f;
        tonalEndOffsetMs = endMs;
        customTonalReplacer.setStartOffsetMs(tonalStartOffsetMs);
        customTonalReplacer.setEndOffsetMs(endMs);
    }
    offlineMixRenderer.triggerRender();
}

void AnatomyAudioProcessor::storeCustomSampleFromUI(bool isTransient, const juce::AudioBuffer<float>& newBuffer, double sr) noexcept
{
    const juce::ScopedLock sl(lock);
    if (isTransient) customTransBuffer.makeCopyOf(newBuffer);
    else             customTonalBuffer.makeCopyOf(newBuffer);
    fileSampleRate = sr;
    updateActiveSampleData();
    offlineMixRenderer.triggerRender();
}

void AnatomyAudioProcessor::clearCustomSampleFromUI(bool isTransient) noexcept
{
    const juce::ScopedLock sl(lock);
    if (isTransient) customTransBuffer.setSize(0, 0);
    else             customTonalBuffer.setSize(0, 0);

    const juce::AudioBuffer<float>& activeTrans = (customTransBuffer.getNumSamples() > 0) ? customTransBuffer : transBufferThread;
    const juce::AudioBuffer<float>& activeTonal = (customTonalBuffer.getNumSamples() > 0) ? customTonalBuffer : tonalBufferThread;

    if (isTransient) transEndOffsetMs = (static_cast<float>(activeTrans.getNumSamples()) / static_cast<float>(fileSampleRate)) * 1000.0f;
    else             tonalEndOffsetMs = (static_cast<float>(activeTonal.getNumSamples()) / static_cast<float>(fileSampleRate)) * 1000.0f;

    updateActiveSampleData();
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
        customTransBuffer.setSize(0, 0);
        customTonalBuffer.setSize(0, 0);
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
    const juce::AudioBuffer<float>& transSrc = (customTransBuffer.getNumSamples() > 0) ? customTransBuffer : transBufferThread;
    const juce::AudioBuffer<float>& tonalSrc = (customTonalBuffer.getNumSamples() > 0) ? customTonalBuffer : tonalBufferThread;

    const int transSamples = transSrc.getNumSamples();
    const int tonalSamples = tonalSrc.getNumSamples();
    const int maxSamples = std::max(transSamples, tonalSamples);

    if (maxSamples == 0) return;

    juce::AudioBuffer<float> activeClick(1, maxSamples);
    juce::AudioBuffer<float> activeSustain(1, maxSamples);
    activeClick.clear(); activeSustain.clear();

    if (currentSoloMode == 0)
    {
        if (transSamples > 0) activeClick.copyFrom(0, 0, transSrc, 0, 0, transSamples);
        if (tonalSamples > 0) activeSustain.copyFrom(0, 0, tonalSrc, 0, 0, tonalSamples);
    }
    else if (currentSoloMode == 1)
    {
        if (transSamples > 0) activeClick.copyFrom(0, 0, transSrc, 0, 0, transSamples);
    }
    else if (currentSoloMode == 2)
    {
        if (tonalSamples > 0) activeSustain.copyFrom(0, 0, tonalSrc, 0, 0, tonalSamples);
    }

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

juce::File AnatomyAudioProcessor::createTemporaryWavForExport(int laneIndex)
{
    juce::File tempDir = juce::File::getSpecialLocation(juce::File::SpecialLocationType::tempDirectory);
    juce::File targetWavFile = tempDir.getChildFile("ANATOMY_Export_" + juce::String(juce::Random::getSystemRandom().nextInt64()) + ".wav");

    juce::AudioBuffer<float> localTrans, localTonal;
    double sr = 44100.0;
    {
        const juce::ScopedLock sl(lock);
        localTrans.makeCopyOf(customTransBuffer.getNumSamples() > 0 ? customTransBuffer : transBufferThread);
        localTonal.makeCopyOf(customTonalBuffer.getNumSamples() > 0 ? customTonalBuffer : tonalBufferThread);
        sr = fileSampleRate;
    }

    const int transSamples = localTrans.getNumSamples();
    const int tonalSamples = localTonal.getNumSamples();
    const int maxSamples = std::max(transSamples, tonalSamples);
    if (maxSamples == 0) return {};

    juce::AudioBuffer<float> workTrans(2, maxSamples);
    juce::AudioBuffer<float> workTonal(2, maxSamples);
    workTrans.clear(); workTonal.clear();

    float transStart = transStartOffsetMs;
    float transEnd = transEndOffsetMs;
    float tonalStart = tonalStartOffsetMs;
    float tonalEnd = tonalEndOffsetMs;
    float clickHold = apvts.getRawParameterValue("clickLength")->load();

    int tStartSmp = static_cast<int>((transStart / 1000.0) * sr);
    int tEndSmp = static_cast<int>((transEnd / 1000.0) * sr);
    int tHoldSmp = static_cast<int>((clickHold / 1000.0) * sr);
    int oStartSmp = static_cast<int>((tonalStart / 1000.0) * sr);
    int oEndSmp = static_cast<int>((tonalEnd / 1000.0) * sr);

    // 💥【エクスポート同期】0ms地点からの頭出しシークレイヤー合算
    for (int s = 0; s < maxSamples; ++s)
    {
        int exactClick = tStartSmp + s;
        int exactSustain = oStartSmp + s;

        if (s < tHoldSmp && exactClick < tEndSmp && exactClick < transSamples && transSamples > 0)
        {
            for (int ch = 0; ch < 2; ++ch)
                workTrans.setSample(ch, s, localTrans.getSample(std::min(ch, localTrans.getNumChannels() - 1), exactClick));
        }
        if (exactSustain < oEndSmp && exactSustain < tonalSamples && tonalSamples > 0)
        {
            for (int ch = 0; ch < 2; ++ch)
                workTonal.setSample(ch, s, localTonal.getSample(std::min(ch, localTonal.getNumChannels() - 1), exactSustain));
        }
    }

    float transPitch = apvts.getRawParameterValue("transPitch")->load();
    float tonalPitch = apvts.getRawParameterValue("tonalPitch")->load();

    auto applyPitchToExport = [](juce::AudioBuffer<float>& buf, float semitones) {
        if (std::abs(semitones) < 0.01f) return;
        float ratio = std::pow(2.0f, semitones / 12.0f);
        juce::AudioBuffer<float> copy(buf); buf.clear();
        int maxS = buf.getNumSamples();
        for (int ch = 0; ch < buf.getNumChannels(); ++ch) {
            float* dest = buf.getWritePointer(ch); const float* src = copy.getReadPointer(ch);
            for (int s = 0; s < maxS; ++s) {
                double srcIdx = s * ratio;
                if (srcIdx < maxS) dest[s] = src[static_cast<int>(srcIdx)];
            }
        }
        };
    applyPitchToExport(workTrans, transPitch);
    applyPitchToExport(workTonal, tonalPitch);

    transientChain.process(workTrans);
    tonalChain.process(workTonal);

    float transGain = std::pow(10.0f, apvts.getRawParameterValue("transMixGain")->load() / 20.0f);
    float tonalGain = std::pow(10.0f, apvts.getRawParameterValue("tonalMixGain")->load() / 20.0f);
    workTrans.applyGain(transGain);
    workTonal.applyGain(tonalGain);

    juce::AudioBuffer<float> finalBufferToTrim;
    if (laneIndex == 1)
    {
        finalBufferToTrim.makeCopyOf(workTrans);
    }
    else if (laneIndex == 2)
    {
        finalBufferToTrim.makeCopyOf(workTonal);
    }
    else
    {
        finalBufferToTrim.setSize(2, maxSamples);
        for (int ch = 0; ch < 2; ++ch) {
            for (int s = 0; s < maxSamples; ++s)
                finalBufferToTrim.setSample(ch, s, workTrans.getSample(ch, s) + workTonal.getSample(ch, s));
        }
        fullMixChain.process(finalBufferToTrim);
    }

    int exportSamples = maxSamples;
    if (laneIndex == 1)      exportSamples = std::min(maxSamples, tHoldSmp);
    else if (laneIndex == 2) exportSamples = std::max(0, oEndSmp - oStartSmp);

    exportSamples = juce::jlimit(1, maxSamples, exportSamples);

    juce::AudioBuffer<float> trimmedBuffer(finalBufferToTrim.getNumChannels(), exportSamples);
    for (int ch = 0; ch < trimmedBuffer.getNumChannels(); ++ch)
    {
        trimmedBuffer.copyFrom(ch, 0, finalBufferToTrim, ch, 0, exportSamples);
    }

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(
        new juce::FileOutputStream(targetWavFile), sr, trimmedBuffer.getNumChannels(), 24, {}, 0));

    if (writer != nullptr)
    {
        writer->writeFromAudioSampleBuffer(trimmedBuffer, 0, trimmedBuffer.getNumSamples());
        writer.reset();
        return targetWavFile;
    }

    return {};
}

// 💥【大改造核心数理：1段目のみのゲート頭出し合算 ＆ 下段全容表示のOfflineMixRenderer】
void OfflineMixRenderer::executeRender()
{
    juce::AudioBuffer<float> localTrans, localTonal;
    double sr = 44100.0;

    {
        const juce::ScopedLock sl(processor.lock);
        localTrans.makeCopyOf(processor.customTransBuffer.getNumSamples() > 0 ? processor.customTransBuffer : processor.transBufferThread);
        localTonal.makeCopyOf(processor.customTonalBuffer.getNumSamples() > 0 ? processor.customTonalBuffer : processor.tonalBufferThread);
        sr = processor.fileSampleRate;
    }

    const int transSamples = localTrans.getNumSamples();
    const int tonalSamples = localTonal.getNumSamples();
    const int maxSamples = std::max(transSamples, tonalSamples);
    if (maxSamples == 0) return;

    if (localTrans.getNumChannels() <= 0 || localTonal.getNumChannels() <= 0) return;

    juce::AudioBuffer<float> workTrans(2, maxSamples);
    juce::AudioBuffer<float> workTonal(2, maxSamples);
    workTrans.clear(); workTonal.clear();

    // 💥【検証③・解決】下の2枚のディスプレイ用ワークには「全長の生波形」を100%一切削らずにストレート転送！
    for (int ch = 0; ch < 2; ++ch)
    {
        if (transSamples > 0) workTrans.copyFrom(ch, 0, localTrans, std::min(ch, localTrans.getNumChannels() - 1), 0, transSamples);
        if (tonalSamples > 0) workTonal.copyFrom(ch, 0, localTonal, std::min(ch, localTonal.getNumChannels() - 1), 0, tonalSamples);
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

    processor.transientChain.process(workTrans);
    processor.tonalChain.process(workTonal);

    float transGain = std::pow(10.0f, processor.apvts.getRawParameterValue("transMixGain")->load() / 20.0f);
    float tonalGain = std::pow(10.0f, processor.apvts.getRawParameterValue("tonalMixGain")->load() / 20.0f);

    juce::AudioBuffer<float> outputMix(2, maxSamples);
    std::vector<float> ratios(maxSamples, 0.5f);

    float transStart = processor.transStartOffsetMs;
    float transEnd = processor.transEndOffsetMs;
    float tonalStart = processor.tonalStartOffsetMs;
    float tonalEnd = processor.tonalEndOffsetMs;
    float clickHold = processor.apvts.getRawParameterValue("clickLength")->load();

    int tStartSmp = static_cast<int>((transStart / 1000.0) * sr);
    int tEndSmp = static_cast<int>((transEnd / 1000.0) * sr);
    int tHoldSmp = static_cast<int>((clickHold / 1000.0) * sr);
    int oStartSmp = static_cast<int>((tonalStart / 1000.0) * sr);
    int oEndSmp = static_cast<int>((tonalEnd / 1000.0) * sr);

    // 💥【核心マウント：1段目FullMixのみ、0ms地点から時間軸を完全レイヤー合算！】
    for (int s = 0; s < maxSamples; ++s)
    {
        float tL = 0.0f, tR = 0.0f;
        float oL = 0.0f, oR = 0.0f;

        int exactClick = tStartSmp + s;
        int exactSustain = oStartSmp + s;

        if (s < tHoldSmp && exactClick < tEndSmp && exactClick < transSamples)
        {
            tL = workTrans.getSample(0, exactClick) * transGain;
            tR = workTrans.getSample(1, exactClick) * transGain;
        }
        if (exactSustain < oEndSmp && exactSustain < tonalSamples)
        {
            oL = workTonal.getSample(0, exactSustain) * tonalGain;
            oR = workTonal.getSample(1, exactSustain) * tonalGain;
        }

        outputMix.setSample(0, s, tL + oL);
        outputMix.setSample(1, s, tR + oR);

        float tEnergy = (tL * tL) + (tR * tR);
        float oEnergy = (oL * oL) + (oR * oR);
        float sum = tEnergy + oEnergy;

        if (sum > 1.0e-6f) ratios[s] = tEnergy / sum;
        else               ratios[s] = 0.5f;
    }

    processor.fullMixChain.process(outputMix);

    {
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

juce::AudioProcessorEditor* AnatomyAudioProcessor::createEditor()
{
    return new AnatomyAudioProcessorEditor(*this);
}

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