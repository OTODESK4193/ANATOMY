#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "DSP/Effects/ADAA_Saturation.h"
#include "DSP/Effects/BitCrusher.h"
#include "DSP/Effects/NoiseGenerator.h"
#include "DSP/Effects/OTT_Multiband.h"
#include "DSP/Effects/GlueCompressor.h"
#include "DSP/Effects/Limiter.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <algorithm>

// 型の定義は PluginEditor.h 側で一元管理されているため、再定義（structやenum）は行わない。
// ここでは、オーディオスレッドとUI間で共有される実体（Entity）の定義のみを記述する。
namespace ExportRecordingCore
{
    Lane lanes[3]; // 0: Full, 1: Trans, 2: Tonal
}

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
        pool[4] = std::make_unique<GlueCompressor>();
        pool[5] = std::make_unique<Limiter>();
        for (int i = 0; i < 6; ++i) pool[i]->setTargetRoute(route);
        };

    instantiatePool(transientPool, TargetRoute::Transient);
    instantiatePool(tonalPool, TargetRoute::Tonal);
    instantiatePool(fullMixPool, TargetRoute::FullMix);

    initParamCache();

    apvts.addParameterListener("clickLength", this);
    apvts.addParameterListener("clickCurve", this);

    juce::StringArray ottParams{ "transOttDepth", "transOttTime", "transOttLowMidXOver", "transOttMidHighXOver",
                                 "tonalOttDepth", "tonalOttTime", "tonalOttLowMidXOver", "tonalOttMidHighXOver",
                                 "fullOttDepth", "fullOttTime", "fullOttLowMidXOver", "fullOttMidHighXOver",
                                 "transPitch", "tonalPitch", "transMixGain", "tonalMixGain",
                                 "tonalDelay" };
    for (const auto& pid : ottParams) apvts.addParameterListener(pid, this);

    synchronizePoolParameters();
    offlineMixRenderer.startThread();
}

AnatomyAudioProcessor::~AnatomyAudioProcessor()
{
    apvts.removeParameterListener("clickLength", this);
    apvts.removeParameterListener("clickCurve", this);

    juce::StringArray ottParams{ "transOttDepth", "transOttTime", "transOttLowMidXOver", "transOttMidHighXOver",
                                 "tonalOttDepth", "tonalOttTime", "tonalOttLowMidXOver", "tonalOttMidHighXOver",
                                 "fullOttDepth", "fullOttTime", "fullOttLowMidXOver", "fullOttMidHighXOver",
                                 "transPitch", "tonalPitch", "transMixGain", "tonalMixGain",
                                 "tonalDelay" };
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

    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "clickLength", 1 }, "Click Hold (ms)", 0.0f, 50.0f, 10.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "clickCurve", 1 }, "Sustain Fade-In (ms)", 1.0f, 100.0f, 5.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "transPitch", 1 }, "Transient Pitch (st)", -12.0f, 12.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "tonalPitch", 1 }, "Sustain Pitch (st)", -12.0f, 12.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "sustainRelease", 1 }, "Sustain Release (ms)", 10.0f, 5000.0f, 500.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "transMixGain", 1 }, "Transient Mix Gain (dB)", -60.0f, 6.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "tonalMixGain", 1 }, "Tonal Mix Gain (dB)", -60.0f, 6.0f, 0.0f));

    // Tonal の再生開始位置を前後にずらす（負=前、正=後ろ）
    // symmetricSkew=true, skew=0.3 → ±50ms付近がスライダー中央の広い範囲を占め微調整しやすい
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "tonalDelay", 1 }, "Tonal Offset (ms)",
        juce::NormalisableRange<float>(-500.0f, 500.0f, 0.1f, 0.3f, true),
        0.0f));

    juce::StringArray prefixes{ "trans", "tonal", "full" };
    for (const auto& pre : prefixes)
    {
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "SatDrive", 1 }, pre + " Saturation Drive", 1.0f, 16.0f, 2.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "SatMix", 1 }, pre + " Saturation Mix", 0.0f, 1.0f, 0.5f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "SatAsym", 1 }, pre + " Saturation Asymmetry", 0.0f, 1.0f, 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "SatTrim", 1 }, pre + " Saturation Output Trim (dB)", -12.0f, 12.0f, 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "SatPre", 1 }, pre + " Saturation Pre HPF (Hz)", 20.0f, 2000.0f, 20.0f));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "BcBits", 1 }, pre + " Bitcrusher Bits", 2.0f, 24.0f, 8.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "BcDown", 1 }, pre + " Bitcrusher Downsample", 1.0f, 32.0f, 4.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "BcMix", 1 }, pre + " Bitcrusher Mix", 0.0f, 1.0f, 0.3f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "BcJitter", 1 }, pre + " Bitcrusher Jitter", 0.0f, 1.0f, 0.0f));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "NsDecay", 1 }, pre + " Noise Decay (ms)", 1.0f, 1000.0f, 100.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "NsMix", 1 }, pre + " Noise Mix", 0.0f, 1.0f, 0.3f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "NsType", 1 }, pre + " Noise Type", 0.0f, 3.0f, 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "NsGain", 1 }, pre + " Noise Gain (dB)", -60.0f, 0.0f, 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "NsAttack", 1 }, pre + " Noise Attack (ms)", 0.0f, 50.0f, 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "NsBpFreq", 1 }, pre + " Noise BP Freq (Hz)", 0.0f, 4000.0f, 0.0f));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "OttDepth", 1 }, pre + " OTT Depth", 0.0f, 1.0f, 0.35f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "OttTime", 1 }, pre + " OTT Time Multiplier", 0.1f, 10.0f, 1.35f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "OttLowMidXOver", 1 }, pre + " OTT Low/Mid X-Over", 40.0f, 1000.0f, 140.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "OttMidHighXOver", 1 }, pre + " OTT Mid/High X-Over", 1000.0f, 15000.0f, 3800.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "OttGateFloor", 1 }, pre + " OTT Gate Floor (dBFS)", -70.0f, -20.0f, -45.0f));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "GlueDepth", 1 }, pre + " Glue Mix",              0.0f,   1.0f,    1.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "GlueThr",   1 }, pre + " Glue Threshold (dBFS)", -40.0f, 0.0f,  -18.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "GlueRatio", 1 }, pre + " Glue Ratio",              1.0f, 20.0f,    2.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "GlueAtk",   1 }, pre + " Glue Attack (ms)",         1.0f, 100.0f,  30.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "GlueRel",   1 }, pre + " Glue Release (ms)",        10.0f, 1000.0f, 200.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "GlueMkp",   1 }, pre + " Glue Makeup (dB)",        -12.0f,  12.0f,   0.0f));

        juce::StringArray bands{ "Low", "Mid", "High" };
        for (const auto& b : bands)
        {
            float defUp = (b == "Low") ? 0.60f : ((b == "Mid") ? 0.40f : 0.15f);
            float defDown = (b == "Low") ? 0.75f : ((b == "Mid") ? 0.70f : 0.60f);

            params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "Ott" + b + "Up", 1 }, pre + " OTT " + b + " Upward Comp", 0.0f, 1.0f, defUp));
            params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "Ott" + b + "Down", 1 }, pre + " OTT " + b + " Downward Comp", 0.0f, 1.0f, defDown));
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

    for (int i = 0; i < 6; ++i)
    {
        transientPool[i]->prepare(sampleRate, safetyBufferSize);
        tonalPool[i]->prepare(sampleRate, safetyBufferSize);
        fullMixPool[i]->prepare(sampleRate, safetyBufferSize);
    }

    transientChain.prepare(sampleRate, safetyBufferSize);
    tonalChain.prepare(sampleRate, safetyBufferSize);
    fullMixChain.prepare(sampleRate, safetyBufferSize);

    // SmoothedValue: ~5ms のランプタイムでジッパーノイズを排除
    smoothedTransGain.reset(sampleRate, 0.005);
    smoothedTonalGain.reset(sampleRate, 0.005);
}

void AnatomyAudioProcessor::releaseResources() {}

void AnatomyAudioProcessor::initParamCache()
{
    // エフェクト型ポインタとAPVTSパラメータポインタを一括キャッシュ。
    // コンストラクタで1回だけ呼ばれ、processBlock毎のdynamic_cast/ハッシュ検索を完全排除。
    std::unique_ptr<AudioEffect>* pools[3] = { transientPool, tonalPool, fullMixPool };
    const juce::String prefixes[3] = { "trans", "tonal", "full" };
    const juce::String bandNames[3] = { "Low", "Mid", "High" };

    for (int i = 0; i < 3; ++i)
    {
        auto& c = cachedLanes[i];
        const auto& pre = prefixes[i];
        auto* pool = pools[i];

        c.sat  = static_cast<ADAA_Saturation*>(pool[0].get());
        c.bc   = static_cast<BitCrusher*>(pool[1].get());
        c.ns   = static_cast<NoiseGenerator*>(pool[2].get());
        c.ott  = static_cast<OTT_Multiband*>(pool[3].get());
        c.glue = static_cast<GlueCompressor*>(pool[4].get());
        c.lim  = static_cast<Limiter*>(pool[5].get());

        c.satDrive = apvts.getRawParameterValue(pre + "SatDrive");
        c.satMix   = apvts.getRawParameterValue(pre + "SatMix");
        c.satAsym  = apvts.getRawParameterValue(pre + "SatAsym");
        c.satTrim  = apvts.getRawParameterValue(pre + "SatTrim");
        c.satPre   = apvts.getRawParameterValue(pre + "SatPre");

        c.bcBits   = apvts.getRawParameterValue(pre + "BcBits");
        c.bcDown   = apvts.getRawParameterValue(pre + "BcDown");
        c.bcMix    = apvts.getRawParameterValue(pre + "BcMix");
        c.bcJitter = apvts.getRawParameterValue(pre + "BcJitter");

        c.nsDecay  = apvts.getRawParameterValue(pre + "NsDecay");
        c.nsMix    = apvts.getRawParameterValue(pre + "NsMix");
        c.nsType   = apvts.getRawParameterValue(pre + "NsType");
        c.nsGain   = apvts.getRawParameterValue(pre + "NsGain");
        c.nsAttack = apvts.getRawParameterValue(pre + "NsAttack");
        c.nsBpFreq = apvts.getRawParameterValue(pre + "NsBpFreq");

        c.ottDepth      = apvts.getRawParameterValue(pre + "OttDepth");
        c.ottTime       = apvts.getRawParameterValue(pre + "OttTime");
        c.ottLowMidXOver = apvts.getRawParameterValue(pre + "OttLowMidXOver");
        c.ottMidHighXOver = apvts.getRawParameterValue(pre + "OttMidHighXOver");
        c.ottGateFloor  = apvts.getRawParameterValue(pre + "OttGateFloor");
        for (int b = 0; b < 3; ++b)
        {
            c.ottBandUp[b]   = apvts.getRawParameterValue(pre + "Ott" + bandNames[b] + "Up");
            c.ottBandDown[b] = apvts.getRawParameterValue(pre + "Ott" + bandNames[b] + "Down");
            c.ottBandGain[b] = apvts.getRawParameterValue(pre + "Ott" + bandNames[b] + "Gain");
        }

        c.glueDepth = apvts.getRawParameterValue(pre + "GlueDepth");
        c.glueThr   = apvts.getRawParameterValue(pre + "GlueThr");
        c.glueRatio = apvts.getRawParameterValue(pre + "GlueRatio");
        c.glueAtk   = apvts.getRawParameterValue(pre + "GlueAtk");
        c.glueRel   = apvts.getRawParameterValue(pre + "GlueRel");
        c.glueMkp   = apvts.getRawParameterValue(pre + "GlueMkp");

        c.limCeil = apvts.getRawParameterValue(pre + "LimCeil");
        c.limMix  = apvts.getRawParameterValue(pre + "LimMix");
    }
}

void AnatomyAudioProcessor::synchronizePoolParameters() noexcept
{
    // キャッシュ済みポインタからの直接ロード。dynamic_cast・ハッシュ検索ゼロ。
    for (int i = 0; i < 3; ++i)
    {
        const auto& c = cachedLanes[i];

        c.sat->setDrive(c.satDrive->load());
        c.sat->setMix(c.satMix->load());
        c.sat->setAsymmetry(c.satAsym->load());
        c.sat->setOutputTrimDb(c.satTrim->load());
        c.sat->setPreCutoffHz(c.satPre->load());

        c.bc->setBits(c.bcBits->load());
        c.bc->setDownsample(c.bcDown->load());
        c.bc->setMix(c.bcMix->load());
        c.bc->setJitter(c.bcJitter->load());

        c.ns->setDecay(c.nsDecay->load());
        c.ns->setMix(c.nsMix->load());
        c.ns->setNoiseType(static_cast<int>(c.nsType->load()));
        c.ns->setGainDb(c.nsGain->load());
        c.ns->setAttack(c.nsAttack->load());
        c.ns->setBpCenterHz(c.nsBpFreq->load());

        c.ott->setMix(c.ottDepth->load());
        c.ott->setTimeMultiplier(c.ottTime->load());
        c.ott->setLowMidXOver(c.ottLowMidXOver->load());
        c.ott->setMidHighXOver(c.ottMidHighXOver->load());
        c.ott->setGateFloorDb(c.ottGateFloor->load());
        for (int b = 0; b < 3; ++b)
        {
            c.ott->setBandUpward(b, c.ottBandUp[b]->load());
            c.ott->setBandDownward(b, c.ottBandDown[b]->load());
            c.ott->setBandGainDb(b, c.ottBandGain[b]->load());
        }

        c.glue->setMix(c.glueDepth->load());
        c.glue->setThresholdDb(c.glueThr->load());
        c.glue->setRatio(c.glueRatio->load());
        c.glue->setAttackMs(c.glueAtk->load());
        c.glue->setReleaseMs(c.glueRel->load());
        c.glue->setMakeupDb(c.glueMkp->load());

        c.lim->setCeiling(c.limCeil->load());
        c.lim->setMix(c.limMix->load());
    }
}

void AnatomyAudioProcessor::updateRouteOrder(TargetRoute route, const std::vector<int>& activeEffectIndices)
{
    std::vector<AudioEffect*> sortedFX;
    sortedFX.reserve(activeEffectIndices.size());
    for (const int idx : activeEffectIndices)
    {
        if (idx < 0 || idx >= 6) continue;
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

    for (int l = 0; l < 3; ++l)
    {
        if (ExportRecordingCore::lanes[l].state.load() == ExportRecordingCore::State::Request)
        {
            ExportRecordingCore::lanes[l].buffer.setSize(2, static_cast<int>(6.0 * currentSampleRate), false, false, true);
            ExportRecordingCore::lanes[l].buffer.clear();
            ExportRecordingCore::lanes[l].writePos = 0;
            ExportRecordingCore::lanes[l].sampleCounter = 0;
            ExportRecordingCore::lanes[l].noteOffSample = static_cast<int>(0.4 * currentSampleRate);
            ExportRecordingCore::lanes[l].isNoteOffTriggered = false;
            ExportRecordingCore::lanes[l].state.store(ExportRecordingCore::State::Recording);

            if (auto* nsTrans = dynamic_cast<NoiseGenerator*>(transientPool[2].get())) nsTrans->trigger();
            if (auto* nsTonal = dynamic_cast<NoiseGenerator*>(tonalPool[2].get())) nsTonal->trigger();
            if (auto* nsFull = dynamic_cast<NoiseGenerator*>(fullMixPool[2].get()))  nsFull->trigger();

            if (activeVoice.isActive) activeVoice.resetProcessing();
            SharedSampleData* currentDataSnapshot = masterSampleData.load(std::memory_order_acquire);
            if (currentDataSnapshot != nullptr)
            {
                activeVoice.sampleData = currentDataSnapshot;
                activeVoice.clickReadIndex = 0.0;
                {
                    float tdMs = apvts.getRawParameterValue("tonalDelay")->load();
                    double tdSmp = -(static_cast<double>(tdMs) / 1000.0) * fileSampleRate;
                    activeVoice.sustainReadIndex = tdSmp;
                }
                activeVoice.triggerVelocity = 1.0f;
                activeVoice.isActive = true;
                activeVoice.isReleasing = false;
                activeVoice.releaseGain = 1.0f;
                activeVoice.currentMidiNote = 60;
                activeVoice.pitchRatio = (activeVoice.sampleData->getSampleRate() > 0.0 && currentSampleRate > 0.0)
                    ? (activeVoice.sampleData->getSampleRate() / currentSampleRate) : 1.0;
                activeIsMuting = false;
                activeMuteGain = 1.0f;
                activeVoice.resetProcessing();
                customTransientReplacer.reset();
                customTonalReplacer.reset();
            }
        }
    }

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

                // tonalDelay: 正=後ろにずらす(遅延)、負=前にずらす(先行)
                // sustainReadIndex を負にすると readInterpolated が 0 を返し遅延になる。
                // 正にするとバッファの先を読み、先行になる。
                float tonalDelayMs = apvts.getRawParameterValue("tonalDelay")->load();
                double tonalOffsetSamples = -(static_cast<double>(tonalDelayMs) / 1000.0) * fileSampleRate;
                activeVoice.sustainReadIndex = tonalOffsetSamples;

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
                customTransientReplacer.reset();
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
        for (int l = 0; l < 3; ++l)
        {
            if (ExportRecordingCore::lanes[l].state.load() == ExportRecordingCore::State::Recording)
            {
                if (!ExportRecordingCore::lanes[l].isNoteOffTriggered &&
                    ExportRecordingCore::lanes[l].sampleCounter == ExportRecordingCore::lanes[l].noteOffSample)
                {
                    if (activeVoice.isActive && activeVoice.currentMidiNote == 60)
                    {
                        activeVoice.isReleasing = true;
                    }
                    ExportRecordingCore::lanes[l].isNoteOffTriggered = true;
                }
                ExportRecordingCore::lanes[l].sampleCounter++;
            }
        }

        float mixedTransL = 0.0f, mixedTransR = 0.0f;
        float mixedTonalL = 0.0f, mixedTonalR = 0.0f;

        float transEndSamples = (transEndOffsetMs / 1000.0f) * static_cast<float>(fileSampleRate);
        float tonalEndSamples = (tonalEndOffsetMs / 1000.0f) * static_cast<float>(fileSampleRate);
        // 分離エンジンのcos²フェード区間（clickCurve ms）を含める。
        // holdだけでゲートすると、フェード区間のトランジェント成分が消失し
        // trans+tonal ≠ original になる。
        float transHoldSamples = ((clickHold + clickCurve) / 1000.0f) * static_cast<float>(fileSampleRate);

        if (activeVoice.isActive)
        {
            float vTransL = 0.0f, vTransR = 0.0f; float vTonalL = 0.0f, vTonalR = 0.0f;
            generateVoiceSample(activeVoice, vTransL, vTransR, vTonalL, vTonalR, clickHold, clickCurve, transScale, tonalScale, currentSampleRate);

            if (activeVoice.isReleasing) activeVoice.releaseGain *= dynamicReleaseFactor;
            if (activeIsMuting) activeMuteGain *= muteFactor;

            if (!isBeforeMode)
            {
                bool transTimeUp = (activeVoice.clickReadIndex >= transHoldSamples);
                double exactClick = ((transStartOffsetMs / 1000.0f) * fileSampleRate) + activeVoice.clickReadIndex;
                if (transTimeUp || exactClick >= transEndSamples)
                {
                    vTransL = 0.0f; vTransR = 0.0f;
                }

                double exactSustainIdx = ((tonalStartOffsetMs / 1000.0f) * fileSampleRate) + activeVoice.sustainReadIndex;
                if (exactSustainIdx >= tonalEndSamples || activeVoice.releaseGain <= 0.001f || activeMuteGain <= 0.001f)
                {
                    activeVoice.reset(); activeIsMuting = false; activeMuteGain = 1.0f;
                }
            }
            else
            {
                double rawProgress = activeVoice.clickReadIndex;
                if (rawProgress >= rawInputBuffer.getNumSamples())
                {
                    activeVoice.reset(); activeIsMuting = false; activeMuteGain = 1.0f;
                }
            }

            mixedTransL += vTransL * activeMuteGain; mixedTransR += vTransR * activeMuteGain;
            mixedTonalL += vTonalL * activeMuteGain; mixedTonalR += vTonalR * activeMuteGain;
        }

        for (int i = 0; i < maxReleasingVoices; ++i)
        {
            if (releasingVoices[i].isActive)
            {
                float vTransL = 0.0f, vTransR = 0.0f; float vTonalL = 0.0f, vTonalR = 0.0f;
                generateVoiceSample(releasingVoices[i], vTransL, vTransR, vTonalL, vTonalR, clickHold, clickCurve, transScale, tonalScale, currentSampleRate);

                if (releasingVoices[i].isReleasing) releasingVoices[i].releaseGain *= dynamicReleaseFactor;
                if (releasingIsMuting[i]) releasingMuteGain[i] *= muteFactor;

                if (!isBeforeMode)
                {
                    bool transTimeUp = (releasingVoices[i].clickReadIndex >= transHoldSamples);
                    double exactClick = ((transStartOffsetMs / 1000.0f) * fileSampleRate) + releasingVoices[i].clickReadIndex;
                    if (transTimeUp || exactClick >= transEndSamples)
                    {
                        vTransL = 0.0f; vTransR = 0.0f;
                    }

                    double exactSustainIdx = ((tonalStartOffsetMs / 1000.0f) * fileSampleRate) + releasingVoices[i].sustainReadIndex;
                    if (exactSustainIdx >= tonalEndSamples || releasingVoices[i].releaseGain <= 0.001f || releasingMuteGain[i] <= 0.001f)
                    {
                        releasingVoices[i].reset(); releasingIsMuting[i] = false; releasingMuteGain[i] = 1.0f;
                    }
                }
                else
                {
                    double rawProgress = releasingVoices[i].clickReadIndex;
                    if (rawProgress >= rawInputBuffer.getNumSamples())
                    {
                        releasingVoices[i].reset(); releasingIsMuting[i] = false; releasingMuteGain[i] = 1.0f;
                    }
                }

                mixedTransL += vTransL * releasingMuteGain[i]; mixedTransR += vTransR * releasingMuteGain[i];
                mixedTonalL += vTonalL * releasingMuteGain[i]; mixedTonalR += vTonalR * releasingMuteGain[i];
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

    // ⑥ SmoothedValue によるジッパーノイズ防止
    {
        const float transTargetGain = (currentSoloMode == 2) ? 0.0f : std::pow(10.0f, apvts.getRawParameterValue("transMixGain")->load() / 20.0f);
        const float tonalTargetGain = (currentSoloMode == 1) ? 0.0f : std::pow(10.0f, apvts.getRawParameterValue("tonalMixGain")->load() / 20.0f);

        smoothedTransGain.setTargetValue(transTargetGain);
        smoothedTonalGain.setTargetValue(tonalTargetGain);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const float tg = smoothedTransGain.getNextValue();
            const float ng = smoothedTonalGain.getNextValue();

            outL[sample] = (transL[sample] * tg) + (tonalL[sample] * ng);
            if (outR != nullptr && transR != nullptr && tonalR != nullptr)
            {
                outR[sample] = (transR[sample] * tg) + (tonalR[sample] * ng);
            }
        }
    }

    fullMixChain.process(buffer);

    for (int l = 0; l < 3; ++l)
    {
        if (ExportRecordingCore::lanes[l].state.load() == ExportRecordingCore::State::Recording)
        {
            int wPos = ExportRecordingCore::lanes[l].writePos;
            int space = ExportRecordingCore::lanes[l].buffer.getNumSamples() - wPos;
            int toWrite = std::min(numSamples, space);

            if (toWrite > 0)
            {
                const juce::AudioBuffer<float>* srcBuf = nullptr;
                if (l == 0) srcBuf = &buffer;
                if (l == 1) srcBuf = &transientBlockBuffer;
                if (l == 2) srcBuf = &tonalBlockBuffer;

                for (int ch = 0; ch < 2; ++ch)
                {
                    int srcCh = std::min(ch, srcBuf->getNumChannels() - 1);
                    ExportRecordingCore::lanes[l].buffer.copyFrom(ch, wPos, *srcBuf, srcCh, 0, toWrite);
                }
                ExportRecordingCore::lanes[l].writePos += toWrite;
            }

            bool isSilent = true;
            const juce::AudioBuffer<float>* checkBuf = (l == 0) ? &buffer : ((l == 1) ? &transientBlockBuffer : &tonalBlockBuffer);
            for (int ch = 0; ch < 2; ++ch)
            {
                int srcCh = std::min(ch, checkBuf->getNumChannels() - 1);
                const float* ptr = checkBuf->getReadPointer(srcCh);
                for (int s = 0; s < numSamples; ++s)
                {
                    if (std::abs(ptr[s]) > 1e-4f) { isSilent = false; break; }
                }
                if (!isSilent) break;
            }

            bool shouldStop = false;
            if (l == 1)
            {
                float transHoldSamples = (clickHold / 1000.0f) * static_cast<float>(fileSampleRate);
                if (activeVoice.clickReadIndex >= transHoldSamples && isSilent) shouldStop = true;
                if (!activeVoice.isActive && isSilent) shouldStop = true;
            }
            else
            {
                if (ExportRecordingCore::lanes[l].isNoteOffTriggered && isSilent && !activeVoice.isActive) shouldStop = true;
                if (!activeVoice.isActive && isSilent) shouldStop = true;
            }

            if (ExportRecordingCore::lanes[l].writePos >= static_cast<int>(5.5 * currentSampleRate)) shouldStop = true;

            if (shouldStop)
            {
                // RT安全: ファイルI/Oをオーディオスレッドから排除。
                // PendingWrite に遷移し、メッセージスレッド（timerCallback）で書き出す。
                ExportRecordingCore::lanes[l].state.store(ExportRecordingCore::State::PendingWrite);
            }
        }
    }
}

void AnatomyAudioProcessor::generateVoiceSample(VoiceState& voice,
    float& outTransL, float& outTransR, float& outTonalL, float& outTonalR,
    float clickHold, float clickCurve, float transScale, float tonalScale, double hostSampleRate) noexcept
{
    outTransL = 0.0f; outTransR = 0.0f; outTonalL = 0.0f; outTonalR = 0.0f;
    if (voice.sampleData == nullptr) return;

    bool isBefore = beforeAfterBypasser.julesIsBeforeBypassed();
    bool hasCustomTrans = (customTransBuffer.getNumSamples() > 0);
    bool hasCustomTonal = (customTonalBuffer.getNumSamples() > 0);

    float transStartSamples = (transStartOffsetMs / 1000.0f) * static_cast<float>(fileSampleRate);
    float transEndSamples = (transEndOffsetMs / 1000.0f) * static_cast<float>(fileSampleRate);
    // cos²フェード区間を含めたトランジェント完全持続時間
    float transHoldSamples = ((clickHold + clickCurve) / 1000.0f) * static_cast<float>(fileSampleRate);

    float tonalStartSamples = (tonalStartOffsetMs / 1000.0f) * static_cast<float>(fileSampleRate);
    float tonalEndSamples = (tonalEndOffsetMs / 1000.0f) * static_cast<float>(fileSampleRate);

    double exactClickIdx = transStartSamples + voice.clickReadIndex;
    double exactSustainIdx = tonalStartSamples + voice.sustainReadIndex;

    int cIdx = static_cast<int>(exactClickIdx);
    int sIdx = static_cast<int>(exactSustainIdx);

    if (isBefore || (!hasCustomTrans && !hasCustomTonal && std::abs(transScale - 1.0f) < 0.01f && std::abs(tonalScale - 1.0f) < 0.01f))
    {
        // pitchRatio != 1.0 の場合（サンプルレート変換時）、整数切り捨てではなく
        // 線形補間を使用してエイリアシングアーティファクトとピッチ感の変化を低減。
        auto readInterpolated = [](const juce::AudioBuffer<float>& buf, int ch, double idx) noexcept -> float
            {
                const int maxSmp = buf.getNumSamples();
                if (maxSmp <= 0 || idx < 0.0) return 0.0f;
                if (idx >= static_cast<double>(maxSmp - 1)) return 0.0f;
                const int i0 = static_cast<int>(idx);
                const int i1 = i0 + 1;
                const float frac = static_cast<float>(idx - static_cast<double>(i0));
                const float* data = buf.getReadPointer(ch);
                return data[i0] + frac * (data[i1] - data[i0]);
            };

        if (currentSoloMode != 2 && transBufferThread.getNumSamples() > 0 && exactClickIdx < static_cast<double>(transBufferThread.getNumSamples() - 1))
        {
            outTransL = readInterpolated(transBufferThread, 0, exactClickIdx);
            outTransR = transBufferThread.getNumChannels() > 1
                ? readInterpolated(transBufferThread, 1, exactClickIdx)
                : outTransL;
        }
        if (currentSoloMode != 1 && tonalBufferThread.getNumSamples() > 0 && exactSustainIdx < static_cast<double>(tonalBufferThread.getNumSamples() - 1))
        {
            outTonalL = readInterpolated(tonalBufferThread, 0, exactSustainIdx);
            outTonalR = tonalBufferThread.getNumChannels() > 1
                ? readInterpolated(tonalBufferThread, 1, exactSustainIdx)
                : outTonalL;
        }
        voice.clickReadIndex += voice.pitchRatio;
        voice.sustainReadIndex += voice.pitchRatio;
        return;
    }

    const auto& click = (!hasCustomTrans) ? transBufferThread : customTransBuffer;
    const auto& sustain = (!hasCustomTonal) ? tonalBufferThread : customTonalBuffer;

    bool transGateOpen = (voice.clickReadIndex < transHoldSamples) && (exactClickIdx < transEndSamples);
    bool tonalGateOpen = (exactSustainIdx < tonalEndSamples);

    float shiftedClick = 0.0f;
    float shiftedSustain = 0.0f;

    if (transGateOpen && currentSoloMode != 2)
    {
        if (hasCustomTrans)
            shiftedClick = customTransientReplacer.processSample(voice.clickReadIndex, voice.pitchRatio, transScale, clickHold, clickCurve, hostSampleRate, currentSoloMode);
        else if (voice.transShifter && cIdx < click.getNumSamples())
            shiftedClick = voice.transShifter->processSample(click, cIdx, transScale);
    }
    voice.clickReadIndex += voice.pitchRatio;

    if (tonalGateOpen && currentSoloMode != 1 && exactSustainIdx >= 0.0)
    {
        if (hasCustomTonal)
            shiftedSustain = customTonalReplacer.processSample(voice.sustainReadIndex, voice.pitchRatio, tonalScale, clickHold, clickCurve, hostSampleRate, currentSoloMode);
        else if (voice.tonalShifter && sIdx >= 0 && sIdx < sustain.getNumSamples())
            shiftedSustain = voice.tonalShifter->processSample(sustain, sIdx, tonalScale);
    }
    voice.sustainReadIndex += voice.pitchRatio;

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

    float finalClick = shiftedClick * voice.triggerVelocity * voice.releaseGain * transFade;
    float finalSustain = shiftedSustain * voice.triggerVelocity * voice.releaseGain * tonalFade;

    outTransL = finalClick; outTransR = finalClick;
    outTonalL = finalSustain; outTonalR = finalSustain;
}

int AnatomyAudioProcessor::snapToZeroCrossing(const juce::AudioBuffer<float>& buffer, int targetSample) noexcept
{
    const int totalSamples = buffer.getNumSamples();
    if (totalSamples <= 0 || buffer.getNumChannels() <= 0) return targetSample;

    const float* data = buffer.getReadPointer(0);
    int bestSample = targetSample;
    float minAbsVal = 1e9f;
    const int searchRange = 150;

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
    if (isTransient)
    {
        customTransBuffer.makeCopyOf(newBuffer);
        float durationMs = (static_cast<float>(newBuffer.getNumSamples()) / static_cast<float>(sr)) * 1000.0f;
        transStartOffsetMs = 0.0f;
        transEndOffsetMs = durationMs;
        customTransientReplacer.setStartOffsetMs(0.0f);
        customTransientReplacer.setEndOffsetMs(durationMs);
    }
    else
    {
        customTonalBuffer.makeCopyOf(newBuffer);
        float durationMs = (static_cast<float>(newBuffer.getNumSamples()) / static_cast<float>(sr)) * 1000.0f;
        tonalStartOffsetMs = 0.0f;
        tonalEndOffsetMs = durationMs;
        customTonalReplacer.setStartOffsetMs(0.0f);
        customTonalReplacer.setEndOffsetMs(durationMs);
    }
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

    if (isTransient)
    {
        transStartOffsetMs = 0.0f;
        transEndOffsetMs = (static_cast<float>(activeTrans.getNumSamples()) / static_cast<float>(fileSampleRate)) * 1000.0f;
        customTransientReplacer.setStartOffsetMs(0.0f);
        customTransientReplacer.setEndOffsetMs(transEndOffsetMs);
    }
    else
    {
        tonalStartOffsetMs = 0.0f;
        tonalEndOffsetMs = (static_cast<float>(activeTonal.getNumSamples()) / static_cast<float>(fileSampleRate)) * 1000.0f;
        customTonalReplacer.setStartOffsetMs(0.0f);
        customTonalReplacer.setEndOffsetMs(tonalEndOffsetMs);
    }

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

        transStartOffsetMs = 0.0f;
        transEndOffsetMs = 0.0f;
        tonalStartOffsetMs = 0.0f;
        tonalEndOffsetMs = 0.0f;
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

    // 【核心修正】分離エンジンには fileSampleRate を使用する。
    // prepare() は prepareToPlay() から hostSampleRate で呼ばれるが、
    // inputBufferThread のサンプルデータは fileSampleRate で記録されている。
    // hostSR と fileSR が異なる場合（例: host=48000, file=44100）、
    // cos² クロスフェード境界が processBlock のゲート位置とずれ、
    // trans+tonal ≠ input となるエネルギー欠損区間が発生する。
    separator.prepare(fileSampleRate);
    separator.performSeparation(inputBufferThread, localTrans, localTonal, clickHold, sustainFade, this);
    if (threadShouldExit()) return;

    {
        const juce::ScopedLock sl(lock);
        transBufferThread.makeCopyOf(localTrans);
        tonalBufferThread.makeCopyOf(localTonal);
        transBufferUI.makeCopyOf(localTrans);
        tonalBufferUI.makeCopyOf(localTonal);

        double durationMs = (static_cast<double>(transBufferThread.getNumSamples()) / fileSampleRate) * 1000.0;

        if (transEndOffsetMs <= 0.0f)
        {
            transStartOffsetMs = 0.0f;
            transEndOffsetMs = static_cast<float>(durationMs);
        }
        if (tonalEndOffsetMs <= 0.0f)
        {
            tonalStartOffsetMs = 0.0f;
            tonalEndOffsetMs = static_cast<float>(durationMs);
        }
    }
    isAnalysisFinished.store(true, std::memory_order_release);

    offlineMixRenderer.triggerRender();
}

void AnatomyAudioProcessor::setSoloMode(int mode)
{
    if (currentSoloMode.load(std::memory_order_acquire) != mode)
    {
        currentSoloMode.store(mode, std::memory_order_release);
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

void AnatomyAudioProcessor::flushPendingExports()
{
    // メッセージスレッドから呼ばれる。PendingWrite状態のレーンをWAVに書き出す。
    for (int l = 0; l < 3; ++l)
    {
        if (ExportRecordingCore::lanes[l].state.load() != ExportRecordingCore::State::PendingWrite)
            continue;

        juce::File tempDir = juce::File::getSpecialLocation(juce::File::SpecialLocationType::tempDirectory);
        ExportRecordingCore::lanes[l].file = tempDir.getChildFile(
            "ANATOMY_Export_" + juce::String(juce::Random::getSystemRandom().nextInt64()) + ".wav");

        int finalLength = ExportRecordingCore::lanes[l].writePos;
        juce::AudioBuffer<float> trimmed(2, finalLength);
        for (int ch = 0; ch < 2; ++ch)
            trimmed.copyFrom(ch, 0, ExportRecordingCore::lanes[l].buffer, ch, 0, finalLength);

        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(
            new juce::FileOutputStream(ExportRecordingCore::lanes[l].file),
            currentSampleRate, 2, 24, {}, 0));

        if (writer != nullptr)
        {
            writer->writeFromAudioSampleBuffer(trimmed, 0, finalLength);
            writer.reset();
            ExportRecordingCore::lanes[l].state.store(ExportRecordingCore::State::Ready);
        }
        else
        {
            ExportRecordingCore::lanes[l].state.store(ExportRecordingCore::State::Idle);
        }
    }
}

juce::File AnatomyAudioProcessor::createTemporaryWavForExport(int laneIndex)
{
    if (laneIndex >= 0 && laneIndex < 3)
    {
        if (ExportRecordingCore::lanes[laneIndex].state.load() == ExportRecordingCore::State::Ready)
        {
            return ExportRecordingCore::lanes[laneIndex].file;
        }
    }
    return {};
}

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

    // ⚠️ RACE CONDITION FIX:
    // OfflineMixRenderer（バックグラウンドスレッド）と processBlock（オーディオスレッド）が
    // 同一のエフェクトインスタンス（OTT_Multibandの StateVariableTPTFilter 等）を同時に呼ぶと
    // フィルター内部ステートが競合し、フィルターが発振して爆音を引き起こす。
    // プレビュー波形はドライ信号（HPSS分離後）を表示する。分離品質の確認として十分な情報を提供。
    // processor.transientChain.process(workTrans);  // DISABLED: スレッド競合→爆音
    // processor.tonalChain.process(workTonal);      // DISABLED: スレッド競合→爆音

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
    // cos²フェード区間を含めた完全なトランジェント持続サンプル数
    float sustainFade = processor.apvts.getRawParameterValue("clickCurve")->load();
    int tHoldSmp = static_cast<int>(((clickHold + sustainFade) / 1000.0) * sr);
    int oStartSmp = static_cast<int>((tonalStart / 1000.0) * sr);
    int oEndSmp = static_cast<int>((tonalEnd / 1000.0) * sr);

    // tonalDelay: 正=後ろにずらす → 読み位置を前にずらす(負のオフセット)
    float tonalDelayMs = processor.apvts.getRawParameterValue("tonalDelay")->load();
    int tonalOffsetSmp = static_cast<int>(-(tonalDelayMs / 1000.0) * sr);

    for (int s = 0; s < maxSamples; ++s)
    {
        float tL = 0.0f, tR = 0.0f;
        float oL = 0.0f, oR = 0.0f;

        int exactClick = tStartSmp + s;
        int exactSustain = oStartSmp + s + tonalOffsetSmp;

        if (s < tHoldSmp && exactClick < tEndSmp && exactClick < transSamples)
        {
            tL = workTrans.getSample(0, exactClick) * transGain;
            tR = workTrans.getSample(1, exactClick) * transGain;
        }
        if (exactSustain >= 0 && exactSustain < oEndSmp && exactSustain < tonalSamples)
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

    // processor.fullMixChain.process(outputMix);  // DISABLED: スレッド競合→爆音（上記参照）

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

// ── バイナリシリアライズ・ヘルパー ────────────────────────────────
// AudioBuffer を MemoryOutputStream に書き出す: [channels:int32][samples:int32][float data...]
static void writeBufferToStream(juce::MemoryOutputStream& out, const juce::AudioBuffer<float>& buf)
{
    int ch = buf.getNumChannels();
    int ns = buf.getNumSamples();
    out.writeInt(ch);
    out.writeInt(ns);
    for (int c = 0; c < ch; ++c)
        out.write(buf.getReadPointer(c), sizeof(float) * static_cast<size_t>(ns));
}

// MemoryInputStream から AudioBuffer を読み出す
static juce::AudioBuffer<float> readBufferFromStream(juce::MemoryInputStream& in)
{
    int ch = in.readInt();
    int ns = in.readInt();
    if (ch <= 0 || ch > 2 || ns <= 0 || ns > 48000 * 60 * 5) // 安全制限: 最大5分
        return {};

    juce::AudioBuffer<float> buf(ch, ns);
    for (int c = 0; c < ch; ++c)
        in.read(buf.getWritePointer(c), sizeof(float) * static_cast<size_t>(ns));
    return buf;
}

void AnatomyAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream out(destData, false);

    // 1. APVTS パラメータ状態を XML → バイナリブロックとして先頭に格納
    juce::MemoryBlock apvtsBlock;
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, apvtsBlock);

    out.writeInt(static_cast<int>(apvtsBlock.getSize()));
    out.write(apvtsBlock.getData(), apvtsBlock.getSize());

    // 2. マジックナンバーでオーディオデータセクション開始を明示
    out.writeInt(0x414E4154); // "ANAT"

    // 3. fileSampleRate
    out.writeDouble(fileSampleRate);

    // 4. メインの入力バッファ（分離前のオリジナル音声）
    {
        const juce::ScopedLock sl(lock);
        writeBufferToStream(out, inputBufferThread);
    }

    // 5. カスタム差し替えバッファ（存在する場合のみ）
    bool hasCustomTrans = (customTransBuffer.getNumSamples() > 0);
    bool hasCustomTonal = (customTonalBuffer.getNumSamples() > 0);
    out.writeBool(hasCustomTrans);
    if (hasCustomTrans) writeBufferToStream(out, customTransBuffer);
    out.writeBool(hasCustomTonal);
    if (hasCustomTonal) writeBufferToStream(out, customTonalBuffer);

    // 6. START/END オフセット
    out.writeFloat(transStartOffsetMs);
    out.writeFloat(transEndOffsetMs);
    out.writeFloat(tonalStartOffsetMs);
    out.writeFloat(tonalEndOffsetMs);
}

void AnatomyAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::MemoryInputStream in(data, static_cast<size_t>(sizeInBytes), false);

    // 1. APVTS ブロック読み出し
    int apvtsSize = in.readInt();
    if (apvtsSize > 0 && apvtsSize < sizeInBytes)
    {
        juce::MemoryBlock apvtsBlock(static_cast<size_t>(apvtsSize));
        in.read(apvtsBlock.getData(), static_cast<size_t>(apvtsSize));
        if (auto xmlState = getXmlFromBinary(apvtsBlock.getData(), static_cast<int>(apvtsBlock.getSize())))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
    }

    // 2. マジックナンバー確認（オーディオデータが無い旧セーブとの互換性）
    if (in.isExhausted()) return;
    int magic = in.readInt();
    if (magic != 0x414E4154) return; // "ANAT" でなければオーディオ無し

    // 3. fileSampleRate
    fileSampleRate = in.readDouble();
    if (fileSampleRate <= 0.0) fileSampleRate = 44100.0;

    // 4. メイン入力バッファ復元
    auto restoredInput = readBufferFromStream(in);

    // 5. カスタムバッファ復元
    juce::AudioBuffer<float> restoredCustomTrans, restoredCustomTonal;
    if (!in.isExhausted())
    {
        bool hasCustomTrans = in.readBool();
        if (hasCustomTrans) restoredCustomTrans = readBufferFromStream(in);
    }
    if (!in.isExhausted())
    {
        bool hasCustomTonal = in.readBool();
        if (hasCustomTonal) restoredCustomTonal = readBufferFromStream(in);
    }

    // 6. START/END オフセット復元
    if (!in.isExhausted())
    {
        transStartOffsetMs = in.readFloat();
        transEndOffsetMs = in.readFloat();
        tonalStartOffsetMs = in.readFloat();
        tonalEndOffsetMs = in.readFloat();
    }

    // 7. バッファを設定し、分離を再実行
    if (restoredInput.getNumSamples() > 0)
    {
        {
            const juce::ScopedLock sl(lock);
            inputBufferThread.makeCopyOf(restoredInput);
            rawInputBuffer.makeCopyOf(restoredInput);
            if (restoredCustomTrans.getNumSamples() > 0)
                customTransBuffer.makeCopyOf(restoredCustomTrans);
            if (restoredCustomTonal.getNumSamples() > 0)
                customTonalBuffer.makeCopyOf(restoredCustomTonal);
        }

        // バックグラウンドスレッドで分離処理を再実行
        // 既にスレッドが走っている場合は停止してから再起動
        if (isThreadRunning())
        {
            signalThreadShouldExit();
            waitForThreadToExit(2000);
        }
        needsReanalysis.store(false, std::memory_order_release);
        startThread(juce::Thread::Priority::normal);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AnatomyAudioProcessor(); }