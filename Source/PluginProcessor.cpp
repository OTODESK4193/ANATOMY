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
    Lane lanes[4]; // 0: Full, 1: Trans, 2: Tonal, 3: Layer
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
        pool[6] = std::make_unique<TransientShaper>();
        for (int i = 0; i < 7; ++i) pool[i]->setTargetRoute(route);
    };

    instantiatePool(transientPool, TargetRoute::Transient);
    instantiatePool(tonalPool, TargetRoute::Tonal);
    instantiatePool(fullMixPool, TargetRoute::FullMix);
    instantiatePool(layerPool, TargetRoute::Layer);

    initParamCache();

    apvts.addParameterListener("clickLength", this);
    apvts.addParameterListener("clickCurve", this);

    juce::StringArray ottParams{ "transOttDepth", "transOttTime", "transOttLowMidXOver", "transOttMidHighXOver",
                                 "tonalOttDepth", "tonalOttTime", "tonalOttLowMidXOver", "tonalOttMidHighXOver",
                                 "fullOttDepth", "fullOttTime", "fullOttLowMidXOver", "fullOttMidHighXOver",
                                 "layerOttDepth", "layerOttTime", "layerOttLowMidXOver", "layerOttMidHighXOver",
                                 "transPitch", "tonalPitch", "layerPitch", "transMixGain", "tonalMixGain",
                                 "layerGain", "layerOffset", "tonalDelay" };
    for (const auto& pid : ottParams) apvts.addParameterListener(pid, this);

    synchronizePoolParameters();
    offlineMixRenderer.startThread();
}

AnatomyAudioProcessor::~AnatomyAudioProcessor()
{
    // 1. バックグラウンドスレッドを最優先で安全停止
    offlineMixRenderer.signalThreadShouldExit();
    offlineMixRenderer.notify();
    offlineMixRenderer.stopThread(3000);

    signalThreadShouldExit();
    stopThread(3000);

    for (int l = 0; l < 4; ++l)
        ExportRecordingCore::lanes[l].state.store(ExportRecordingCore::State::Idle);

    activeVoice.reset();
    for (int i = 0; i < maxReleasingVoices; ++i)
        releasingVoices[i].reset();

    // 2. パラメータリスナーの解除
    apvts.removeParameterListener("clickLength", this);
    apvts.removeParameterListener("clickCurve", this);

    juce::StringArray ottParams{ "transOttDepth", "transOttTime", "transOttLowMidXOver", "transOttMidHighXOver",
                                 "tonalOttDepth", "tonalOttTime", "tonalOttLowMidXOver", "tonalOttMidHighXOver",
                                 "fullOttDepth", "fullOttTime", "fullOttLowMidXOver", "fullOttMidHighXOver",
                                 "layerOttDepth", "layerOttTime", "layerOttLowMidXOver", "layerOttMidHighXOver",
                                 "transPitch", "tonalPitch", "layerPitch", "transMixGain", "tonalMixGain",
                                 "layerGain", "layerOffset", "tonalDelay" };
    for (const auto& pid : ottParams) apvts.removeParameterListener(pid, this);

    // 3. メモリ解放
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
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "layerPitch", 1 }, "Layer Pitch (st)", -12.0f, 12.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "sustainRelease", 1 }, "Sustain Release (ms)", 10.0f, 5000.0f, 500.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "transMixGain", 1 }, "Transient Mix Gain (dB)", -60.0f, 6.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "tonalMixGain", 1 }, "Tonal Mix Gain (dB)", -60.0f, 6.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "layerGain", 1 }, "Layer Mix Gain (dB)", -60.0f, 6.0f, 0.0f));

    // Tonal の再生開始位置を前後にずらす（負=前、正=後ろ）
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "tonalDelay", 1 }, "Tonal Offset (ms)",
        juce::NormalisableRange<float>(-500.0f, 500.0f, 0.1f, 0.5f, true),
        0.0f));

    // Layer の再生開始位置オフセット（0〜500ms）
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "layerOffset", 1 }, "Layer Offset (ms)",
        juce::NormalisableRange<float>(0.0f, 500.0f, 0.1f, 0.5f),
        0.0f));

    juce::StringArray prefixes{ "trans", "tonal", "full", "layer" };
    for (const auto& pre : prefixes)
    {
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "SatDrive", 1 }, pre + " Saturation Drive", 1.0f, 16.0f, 2.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "SatMix", 1 }, pre + " Saturation Mix", 0.0f, 1.0f, 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{ pre + "SatType", 1 }, pre + " Saturation Type",
            juce::StringArray{ "Soft Tanh", "Hard Clip", "Triode", "Tape", "Transformer", "JFET", "BJT", "Wavefold", "Exciter", "Cubic" }, 0));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "SatTrim", 1 }, pre + " Saturation Output Trim (dB)", -12.0f, 12.0f, 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "SatPre", 1 }, pre + " Saturation Pre HPF (Hz)", 20.0f, 2000.0f, 20.0f));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "BcBits", 1 }, pre + " Bitcrusher Bits", 2.0f, 24.0f, 8.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "BcDown", 1 }, pre + " Bitcrusher Downsample", 1.0f, 32.0f, 4.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "BcMix", 1 }, pre + " Bitcrusher Mix", 0.0f, 1.0f, 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "BcJitter", 1 }, pre + " Bitcrusher Jitter", 0.0f, 1.0f, 0.0f));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "NsDecay", 1 }, pre + " Noise Decay (ms)", 1.0f, 1000.0f, 100.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "NsMix", 1 }, pre + " Noise Mix", 0.0f, 1.0f, 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "NsType", 1 }, pre + " Noise Type", 0.0f, 3.0f, 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "NsGain", 1 }, pre + " Noise Gain (dB)", -60.0f, 0.0f, 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "NsAttack", 1 }, pre + " Noise Attack (ms)", 0.0f, 50.0f, 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "NsBpFreq", 1 }, pre + " Noise BP Freq (Hz)", 0.0f, 4000.0f, 0.0f));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "OttDepth", 1 }, pre + " OTT Depth", 0.0f, 1.0f, 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "OttTime", 1 }, pre + " OTT Time Multiplier", 0.1f, 10.0f, 1.35f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "OttLowMidXOver", 1 }, pre + " OTT Low/Mid X-Over", 40.0f, 1000.0f, 140.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "OttMidHighXOver", 1 }, pre + " OTT Mid/High X-Over", 1000.0f, 15000.0f, 3800.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "OttGateFloor", 1 }, pre + " OTT Gate Floor (dBFS)", -70.0f, -20.0f, -45.0f));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "GlueDepth", 1 }, pre + " Glue Mix",              0.0f,   1.0f,    0.0f));
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

            params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "Ott" + b + "Up", 1 }, pre + " OTT " + b + " Upward Comp", 0.0f, 100.0f, defUp * 100.0f));
            params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "Ott" + b + "Down", 1 }, pre + " OTT " + b + " Downward Comp", 0.0f, 100.0f, defDown * 100.0f));
            params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "Ott" + b + "Gain", 1 }, pre + " OTT " + b + " Band Gain", -24.0f, 24.0f, 0.0f));
        }

        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "LimGain", 1 }, pre + " Limiter In Gain (dB)", 0.0f, 24.0f, 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "LimCeil", 1 }, pre + " Limiter Ceiling (dB)", -24.0f, 0.0f, -0.1f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "LimMix", 1 }, pre + " Limiter Mix", 0.0f, 1.0f, 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{ pre + "LimMode", 1 }, pre + " Limiter Mode", juce::StringArray{ "Limit", "Clip" }, 0));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "TsAttack",  1 }, pre + " Transient Attack",  -1.0f, 1.0f, 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "TsSustain", 1 }, pre + " Transient Sustain", -1.0f, 1.0f, 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ pre + "TsMix",     1 }, pre + " Transient Shaper Mix", 0.0f, 1.0f, 0.0f));
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
    layerBlockBuffer.setSize(2, safetyBufferSize, false, false, true);

    for (int i = 0; i < 7; ++i)
    {
        transientPool[i]->prepare(sampleRate, safetyBufferSize);
        tonalPool[i]->prepare(sampleRate, safetyBufferSize);
        fullMixPool[i]->prepare(sampleRate, safetyBufferSize);
        layerPool[i]->prepare(sampleRate, safetyBufferSize);
    }

    transientChain.prepare(sampleRate, safetyBufferSize);
    tonalChain.prepare(sampleRate, safetyBufferSize);
    fullMixChain.prepare(sampleRate, safetyBufferSize);
    layerChain.prepare(sampleRate, safetyBufferSize);

    // SmoothedValue: ~5ms のランプタイムでジッパーノイズを排除
    smoothedTransGain.reset(sampleRate, 0.005);
    smoothedTonalGain.reset(sampleRate, 0.005);
    smoothedLayerGain.reset(sampleRate, 0.005);

    if (!offlineMixRenderer.isThreadRunning())
        offlineMixRenderer.startThread();
}

void AnatomyAudioProcessor::releaseResources()
{
    activeVoice.reset();
    for (int i = 0; i < maxReleasingVoices; ++i)
        releasingVoices[i].reset();

    for (int l = 0; l < 4; ++l)
        ExportRecordingCore::lanes[l].state.store(ExportRecordingCore::State::Idle);
}

bool AnatomyAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void AnatomyAudioProcessor::initParamCache()
{
    // エフェクト型ポインタとAPVTSパラメータポインタを一括キャッシュ。
    // コンストラクタで1回だけ呼ばれ、processBlock毎のdynamic_cast/ハッシュ検索を完全排除。
    std::unique_ptr<AudioEffect>* pools[4] = { transientPool, tonalPool, fullMixPool, layerPool };
    const juce::String prefixes[4] = { "trans", "tonal", "full", "layer" };
    const juce::String bandNames[3] = { "Low", "Mid", "High" };

    for (int i = 0; i < 4; ++i)
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
        c.ts   = static_cast<TransientShaper*>(pool[6].get());

        c.satDrive = apvts.getRawParameterValue(pre + "SatDrive");
        c.satMix   = apvts.getRawParameterValue(pre + "SatMix");
        c.satType  = apvts.getRawParameterValue(pre + "SatType");
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

        c.limGain = apvts.getRawParameterValue(pre + "LimGain");
        c.limCeil = apvts.getRawParameterValue(pre + "LimCeil");
        c.limMix  = apvts.getRawParameterValue(pre + "LimMix");
        c.limMode = apvts.getRawParameterValue(pre + "LimMode");

        c.tsAttack  = apvts.getRawParameterValue(pre + "TsAttack");
        c.tsSustain = apvts.getRawParameterValue(pre + "TsSustain");
        c.tsMix     = apvts.getRawParameterValue(pre + "TsMix");
    }
}

void AnatomyAudioProcessor::synchronizePoolParameters() noexcept
{
    // キャッシュ済みポインタからの直接ロード。dynamic_cast・ハッシュ検索ゼロ。
    for (int i = 0; i < 4; ++i)
    {
        const auto& c = cachedLanes[i];

        if (c.sat && c.satDrive)
        {
            c.sat->setDrive(c.satDrive->load());
            if (c.satMix) c.sat->setMix(c.satMix->load());
            if (c.satType) c.sat->setType(static_cast<int>(c.satType->load()));
            if (c.satTrim) c.sat->setOutputTrimDb(c.satTrim->load());
            if (c.satPre) c.sat->setPreCutoffHz(c.satPre->load());
        }

        if (c.bc && c.bcBits)
        {
            c.bc->setBits(c.bcBits->load());
            if (c.bcDown) c.bc->setDownsample(c.bcDown->load());
            if (c.bcMix) c.bc->setMix(c.bcMix->load());
            if (c.bcJitter) c.bc->setJitter(c.bcJitter->load());
        }

        if (c.ns && c.nsDecay)
        {
            c.ns->setDecay(c.nsDecay->load());
            if (c.nsMix) c.ns->setMix(c.nsMix->load());
            if (c.nsType) c.ns->setNoiseType(static_cast<int>(c.nsType->load()));
            if (c.nsGain) c.ns->setGainDb(c.nsGain->load());
            if (c.nsAttack) c.ns->setAttack(c.nsAttack->load());
            if (c.nsBpFreq) c.ns->setBpCenterHz(c.nsBpFreq->load());
        }

        if (c.ott && c.ottDepth)
        {
            c.ott->setMix(c.ottDepth->load());
            if (c.ottTime) c.ott->setTimeMultiplier(c.ottTime->load());
            if (c.ottLowMidXOver) c.ott->setLowMidXOver(c.ottLowMidXOver->load());
            if (c.ottMidHighXOver) c.ott->setMidHighXOver(c.ottMidHighXOver->load());
            if (c.ottGateFloor) c.ott->setGateFloorDb(c.ottGateFloor->load());
            for (int b = 0; b < 3; ++b)
            {
                if (c.ottBandUp[b]) c.ott->setBandUpward(b, c.ottBandUp[b]->load());
                if (c.ottBandDown[b]) c.ott->setBandDownward(b, c.ottBandDown[b]->load());
                if (c.ottBandGain[b]) c.ott->setBandGainDb(b, c.ottBandGain[b]->load());
            }
        }

        if (c.glue && c.glueDepth)
        {
            c.glue->setMix(c.glueDepth->load());
            if (c.glueThr) c.glue->setThresholdDb(c.glueThr->load());
            if (c.glueRatio) c.glue->setRatio(c.glueRatio->load());
            if (c.glueAtk) c.glue->setAttackMs(c.glueAtk->load());
            if (c.glueRel) c.glue->setReleaseMs(c.glueRel->load());
            if (c.glueMkp) c.glue->setMakeupDb(c.glueMkp->load());
        }

        if (c.lim && c.limCeil)
        {
            if (c.limGain) c.lim->setInputGain(c.limGain->load());
            c.lim->setCeiling(c.limCeil->load());
            if (c.limMix) c.lim->setMix(c.limMix->load());
            if (c.limMode) c.lim->setMode(static_cast<int>(c.limMode->load()));
        }

        if (c.ts && c.tsMix)
        {
            if (c.tsAttack) c.ts->setAttack(c.tsAttack->load());
            if (c.tsSustain) c.ts->setSustain(c.tsSustain->load());
            c.ts->setMix(c.tsMix->load());
        }
    }
}

void AnatomyAudioProcessor::updateRouteOrder(TargetRoute route, const std::vector<int>& activeEffectIndices)
{
    // エフェクト処理順を保持（エディタ再構築時の復元用）
    if (route == TargetRoute::Transient)     transEffectOrder = activeEffectIndices;
    else if (route == TargetRoute::Tonal)    tonalEffectOrder = activeEffectIndices;
    else if (route == TargetRoute::FullMix)  fullMixEffectOrder = activeEffectIndices;
    else if (route == TargetRoute::Layer)    layerEffectOrder = activeEffectIndices;

    std::vector<AudioEffect*> sortedFX;
    sortedFX.reserve(activeEffectIndices.size());
    for (const int idx : activeEffectIndices)
    {
        if (idx < 0 || idx >= 7) continue;
        if (route == TargetRoute::Transient)     sortedFX.push_back(transientPool[idx].get());
        else if (route == TargetRoute::Tonal)    sortedFX.push_back(tonalPool[idx].get());
        else if (route == TargetRoute::FullMix)  sortedFX.push_back(fullMixPool[idx].get());
        else if (route == TargetRoute::Layer)    sortedFX.push_back(layerPool[idx].get());
    }
    if (route == TargetRoute::Transient)     transientChain.updateChain(sortedFX, fxGarbageBin);
    else if (route == TargetRoute::Tonal)    tonalChain.updateChain(sortedFX, fxGarbageBin);
    else if (route == TargetRoute::FullMix)  fullMixChain.updateChain(sortedFX, fxGarbageBin);
    else if (route == TargetRoute::Layer)    layerChain.updateChain(sortedFX, fxGarbageBin);

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

            for (int i = 0; i < 4; ++i)
                if (cachedLanes[i].ns != nullptr) cachedLanes[i].ns->trigger();

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

                    float ldMs = apvts.getRawParameterValue("layerOffset")->load();
                    double ldSmp = -(static_cast<double>(ldMs) / 1000.0) * fileSampleRate;
                    activeVoice.layerReadIndex = ldSmp;
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
                customLayerReplacer.reset();
            }
        }
    }

    synchronizePoolParameters();
    transientBlockBuffer.clear();
    tonalBlockBuffer.clear();
    layerBlockBuffer.clear();

    SharedSampleData* currentDataSnapshot = masterSampleData.load(std::memory_order_acquire);

    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn())
        {
            for (int i = 0; i < 4; ++i)
                if (cachedLanes[i].ns != nullptr) cachedLanes[i].ns->trigger();

            if (activeVoice.isActive)
            {
                int slotToUse = 0;
                for (int i = 0; i < maxReleasingVoices; ++i) { if (!releasingVoices[i].isActive) { slotToUse = i; break; } }
                releasingVoices[slotToUse].sampleData = activeVoice.sampleData;
                releasingVoices[slotToUse].clickReadIndex = activeVoice.clickReadIndex;
                releasingVoices[slotToUse].sustainReadIndex = activeVoice.sustainReadIndex;
                releasingVoices[slotToUse].layerReadIndex = activeVoice.layerReadIndex;
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
                float tonalDelayMs = apvts.getRawParameterValue("tonalDelay")->load();
                double tonalOffsetSamples = -(static_cast<double>(tonalDelayMs) / 1000.0) * fileSampleRate;
                activeVoice.sustainReadIndex = tonalOffsetSamples;

                // layerOffset: 正=後ろにずらす(遅延)
                float layerOffsetMs = apvts.getRawParameterValue("layerOffset")->load();
                double layerOffsetSamples = -(static_cast<double>(layerOffsetMs) / 1000.0) * fileSampleRate;
                activeVoice.layerReadIndex = layerOffsetSamples;

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
                customLayerReplacer.reset();
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
    float* layerL = layerBlockBuffer.getWritePointer(0);
    float* layerR = numChannels > 1 ? layerBlockBuffer.getWritePointer(1) : nullptr;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        for (int l = 0; l < 4; ++l)
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
        float mixedLayerL = 0.0f, mixedLayerR = 0.0f;

        float transEndSamples = (transEndOffsetMs / 1000.0f) * static_cast<float>(fileSampleRate);
        float tonalEndSamples = (tonalEndOffsetMs / 1000.0f) * static_cast<float>(fileSampleRate);
        // 分離エンジンのcos²フェード区間（clickCurve ms）を含める。
        // holdだけでゲートすると、フェード区間のトランジェント成分が消失し
        // trans+tonal ≠ original になる。
        float transHoldSamples = ((clickHold + clickCurve) / 1000.0f) * static_cast<float>(fileSampleRate);

        if (activeVoice.isActive)
        {
            float vTransL = 0.0f, vTransR = 0.0f; float vTonalL = 0.0f, vTonalR = 0.0f;
            float vLayerL = 0.0f, vLayerR = 0.0f;
            generateVoiceSample(activeVoice, vTransL, vTransR, vTonalL, vTonalR, vLayerL, vLayerR, clickHold, clickCurve, transScale, tonalScale, currentSampleRate);

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
                bool tonalDone = (exactSustainIdx >= tonalEndSamples);
                
                double exactLayerIdx = ((layerStartOffsetMs / 1000.0f) * fileSampleRate) + activeVoice.layerReadIndex;
                float layerEndSamples = (layerEndOffsetMs > 0.0f) ? ((layerEndOffsetMs / 1000.0f) * static_cast<float>(fileSampleRate)) : static_cast<float>(customLayerBuffer.getNumSamples());
                bool layerDone = (customLayerBuffer.getNumSamples() == 0) || (exactLayerIdx >= layerEndSamples);

                if ((tonalDone && layerDone) || activeVoice.releaseGain <= 0.001f || activeMuteGain <= 0.001f)
                {
                    activeVoice.reset(); activeIsMuting = false; activeMuteGain = 1.0f;
                }
            }
            else
            {
                // Before モード: ソロ状態に応じて終了判定を切り替え
                int soloMode = currentSoloMode.load(std::memory_order_acquire);
                if (soloMode == 0)
                {
                    // ソロなし: rawInputBuffer (完全原音) の長さで終了
                    if (activeVoice.clickReadIndex >= rawInputBuffer.getNumSamples())
                    {
                        activeVoice.reset(); activeIsMuting = false; activeMuteGain = 1.0f;
                    }
                }
                else
                {
                    // ソロあり: 分離バッファまたはLayerの長さで終了
                    double clickPos = ((transStartOffsetMs / 1000.0f) * fileSampleRate) + activeVoice.clickReadIndex;
                    double sustainPos = ((tonalStartOffsetMs / 1000.0f) * fileSampleRate) + activeVoice.sustainReadIndex;
                    double layerPos = ((layerStartOffsetMs / 1000.0f) * fileSampleRate) + activeVoice.layerReadIndex;
                    double checkIdx = (soloMode == 1) ? clickPos : ((soloMode == 2) ? sustainPos : layerPos);
                    int bufLen = (soloMode == 1) ? transBufferThread.getNumSamples() : ((soloMode == 2) ? tonalBufferThread.getNumSamples() : customLayerBuffer.getNumSamples());
                    if (checkIdx >= bufLen || activeVoice.releaseGain <= 0.001f || activeMuteGain <= 0.001f)
                    {
                        activeVoice.reset(); activeIsMuting = false; activeMuteGain = 1.0f;
                    }
                }
            }

            mixedTransL += vTransL * activeMuteGain; mixedTransR += vTransR * activeMuteGain;
            mixedTonalL += vTonalL * activeMuteGain; mixedTonalR += vTonalR * activeMuteGain;
            mixedLayerL += vLayerL * activeMuteGain; mixedLayerR += vLayerR * activeMuteGain;
        }

        for (int i = 0; i < maxReleasingVoices; ++i)
        {
            if (releasingVoices[i].isActive)
            {
                float vTransL = 0.0f, vTransR = 0.0f; float vTonalL = 0.0f, vTonalR = 0.0f;
                float vLayerL = 0.0f, vLayerR = 0.0f;
                generateVoiceSample(releasingVoices[i], vTransL, vTransR, vTonalL, vTonalR, vLayerL, vLayerR, clickHold, clickCurve, transScale, tonalScale, currentSampleRate);

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
                    bool tonalDone = (exactSustainIdx >= tonalEndSamples);

                    double exactLayerIdx = ((layerStartOffsetMs / 1000.0f) * fileSampleRate) + releasingVoices[i].layerReadIndex;
                    float layerEndSamples = (layerEndOffsetMs > 0.0f) ? ((layerEndOffsetMs / 1000.0f) * static_cast<float>(fileSampleRate)) : static_cast<float>(customLayerBuffer.getNumSamples());
                    bool layerDone = (customLayerBuffer.getNumSamples() == 0) || (exactLayerIdx >= layerEndSamples);

                    if ((tonalDone && layerDone) || releasingVoices[i].releaseGain <= 0.001f || releasingMuteGain[i] <= 0.001f)
                    {
                        releasingVoices[i].reset(); releasingIsMuting[i] = false; releasingMuteGain[i] = 1.0f;
                    }
                }
                else
                {
                    int soloMode = currentSoloMode.load(std::memory_order_acquire);
                    if (soloMode == 0)
                    {
                        if (releasingVoices[i].clickReadIndex >= rawInputBuffer.getNumSamples())
                        {
                            releasingVoices[i].reset(); releasingIsMuting[i] = false; releasingMuteGain[i] = 1.0f;
                        }
                    }
                    else
                    {
                        double clickPos = ((transStartOffsetMs / 1000.0f) * fileSampleRate) + releasingVoices[i].clickReadIndex;
                        double sustainPos = ((tonalStartOffsetMs / 1000.0f) * fileSampleRate) + releasingVoices[i].sustainReadIndex;
                        double checkIdx = (soloMode == 1) ? clickPos : sustainPos;
                        int bufLen = (soloMode == 1) ? transBufferThread.getNumSamples() : tonalBufferThread.getNumSamples();
                        if (checkIdx >= bufLen || releasingVoices[i].releaseGain <= 0.001f || releasingMuteGain[i] <= 0.001f)
                        {
                            releasingVoices[i].reset(); releasingIsMuting[i] = false; releasingMuteGain[i] = 1.0f;
                        }
                    }
                }

                mixedTransL += vTransL * releasingMuteGain[i]; mixedTransR += vTransR * releasingMuteGain[i];
                mixedTonalL += vTonalL * releasingMuteGain[i]; mixedTonalR += vTonalR * releasingMuteGain[i];
                mixedLayerL += vLayerL * releasingMuteGain[i]; mixedLayerR += vLayerR * releasingMuteGain[i];
            }
        }

        transL[sample] = mixedTransL; if (transR != nullptr) transR[sample] = mixedTransR;
        tonalL[sample] = mixedTonalL; if (tonalR != nullptr) tonalR[sample] = mixedTonalR;
        layerL[sample] = mixedLayerL; if (layerR != nullptr) layerR[sample] = mixedLayerR;
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
    layerChain.process(layerBlockBuffer);

    // ⑥ SmoothedValue によるジッパーノイズ防止
    {
        const float transTargetGain = (currentSoloMode == 2 || currentSoloMode == 3) ? 0.0f : std::pow(10.0f, apvts.getRawParameterValue("transMixGain")->load() / 20.0f);
        const float tonalTargetGain = (currentSoloMode == 1 || currentSoloMode == 3) ? 0.0f : std::pow(10.0f, apvts.getRawParameterValue("tonalMixGain")->load() / 20.0f);
        const float layerTargetGain = (currentSoloMode == 1 || currentSoloMode == 2) ? 0.0f : std::pow(10.0f, apvts.getRawParameterValue("layerGain")->load() / 20.0f);

        smoothedTransGain.setTargetValue(transTargetGain);
        smoothedTonalGain.setTargetValue(tonalTargetGain);
        smoothedLayerGain.setTargetValue(layerTargetGain);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const float tg = smoothedTransGain.getNextValue();
            const float ng = smoothedTonalGain.getNextValue();
            const float lg = smoothedLayerGain.getNextValue();

            outL[sample] = (transL[sample] * tg) + (tonalL[sample] * ng) + (layerL[sample] * lg);
            if (outR != nullptr && transR != nullptr && tonalR != nullptr && layerR != nullptr)
            {
                outR[sample] = (transR[sample] * tg) + (tonalR[sample] * ng) + (layerR[sample] * lg);
            }
        }
    }

    fullMixChain.process(buffer);

    for (int l = 0; l < 4; ++l)
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
                if (l == 3) srcBuf = &layerBlockBuffer;

                for (int ch = 0; ch < 2; ++ch)
                {
                    int srcCh = std::min(ch, srcBuf->getNumChannels() - 1);
                    ExportRecordingCore::lanes[l].buffer.copyFrom(ch, wPos, *srcBuf, srcCh, 0, toWrite);
                }
                ExportRecordingCore::lanes[l].writePos += toWrite;
            }

            bool isSilent = true;
            const juce::AudioBuffer<float>* checkBuf = (l == 0) ? &buffer : ((l == 1) ? &transientBlockBuffer : ((l == 2) ? &tonalBlockBuffer : &layerBlockBuffer));
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
    float& outTransL, float& outTransR, float& outTonalL, float& outTonalR, float& outLayerL, float& outLayerR,
    float clickHold, float clickCurve, float transScale, float tonalScale, double hostSampleRate) noexcept
{
    outTransL = 0.0f; outTransR = 0.0f; outTonalL = 0.0f; outTonalR = 0.0f; outLayerL = 0.0f; outLayerR = 0.0f;
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

    // 4点 Hermite 3次補間読み取りラムダ（エイリアシング完全排除・超高忠実度）
    auto readInterpolated = [](const juce::AudioBuffer<float>& buf, int ch, double idx) noexcept -> float
        {
            const int maxSmp = buf.getNumSamples();
            if (maxSmp <= 0 || idx < 0.0) return 0.0f;
            if (idx >= static_cast<double>(maxSmp - 1)) return 0.0f;
            const int i0 = static_cast<int>(idx);
            const float frac = static_cast<float>(idx - static_cast<double>(i0));

            const int im1 = std::max(0, i0 - 1);
            const int i1  = std::min(i0 + 1, maxSmp - 1);
            const int i2  = std::min(i0 + 2, maxSmp - 1);

            const float* data = buf.getReadPointer(ch);
            const float ym1 = data[im1], y0 = data[i0], y1 = data[i1], y2 = data[i2];
            const float c0 = y0;
            const float c1 = 0.5f * (y1 - ym1);
            const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
            const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
            return ((c3 * frac + c2) * frac + c1) * frac + c0;
        };

    if (isBefore)
    {
        int soloMode = currentSoloMode.load(std::memory_order_acquire);

        // Before (Soloなし = 完全原音再生)
        if (soloMode == 0)
        {
            double rawIdx = voice.clickReadIndex;
            if (rawInputBuffer.getNumSamples() > 0 && rawIdx >= 0.0 && rawIdx < static_cast<double>(rawInputBuffer.getNumSamples() - 1))
            {
                float rawL = readInterpolated(rawInputBuffer, 0, rawIdx);
                float rawR = rawInputBuffer.getNumChannels() > 1
                    ? readInterpolated(rawInputBuffer, 1, rawIdx) : rawL;
                outTransL = rawL * 0.5f; outTransR = rawR * 0.5f;
                outTonalL = rawL * 0.5f; outTonalR = rawR * 0.5f;
            }
        }
        else
        {
            // Solo+Before = オリジナル分離波形再生 (Customは無視)
            if (soloMode == 1 && transBufferThread.getNumSamples() > 0 && exactClickIdx >= 0.0 && exactClickIdx < static_cast<double>(transBufferThread.getNumSamples() - 1))
            {
                outTransL = readInterpolated(transBufferThread, 0, exactClickIdx);
                outTransR = transBufferThread.getNumChannels() > 1
                    ? readInterpolated(transBufferThread, 1, exactClickIdx) : outTransL;
            }
            if (soloMode == 2 && tonalBufferThread.getNumSamples() > 0 && exactSustainIdx >= 0.0 && exactSustainIdx < static_cast<double>(tonalBufferThread.getNumSamples() - 1))
            {
                outTonalL = readInterpolated(tonalBufferThread, 0, exactSustainIdx);
                outTonalR = tonalBufferThread.getNumChannels() > 1
                    ? readInterpolated(tonalBufferThread, 1, exactSustainIdx) : outTonalL;
            }
        }

        voice.clickReadIndex += voice.pitchRatio;
        voice.sustainReadIndex += voice.pitchRatio;
        return;
    }

    float tInMs = 0.0f, tOutMs = 0.0f, tInTension = 0.0f, tOutTension = 0.0f;
    getFadeForUI(1, tInMs, tOutMs, tInTension, tOutTension);
    float oInMs = 0.0f, oOutMs = 0.0f, oInTension = 0.0f, oOutTension = 0.0f;
    getFadeForUI(2, oInMs, oOutMs, oInTension, oOutTension);
    float lInMs = 0.0f, lOutMs = 0.0f, lInTension = 0.0f, lOutTension = 0.0f;
    getFadeForUI(3, lInMs, lOutMs, lInTension, lOutTension);

    float tInSmp = (tInMs / 1000.0f) * static_cast<float>(fileSampleRate);
    float tOutSmp = (tOutMs / 1000.0f) * static_cast<float>(fileSampleRate);
    float oInSmp = (oInMs / 1000.0f) * static_cast<float>(fileSampleRate);
    float oOutSmp = (oOutMs / 1000.0f) * static_cast<float>(fileSampleRate);
    float lInSmp = (lInMs / 1000.0f) * static_cast<float>(fileSampleRate);
    float lOutSmp = (lOutMs / 1000.0f) * static_cast<float>(fileSampleRate);

    bool hasCustomLayer = (customLayerBuffer.getNumSamples() > 0);
    float layerStartSamples = (layerStartOffsetMs / 1000.0f) * static_cast<float>(fileSampleRate);
    float layerEndSamples = (layerEndOffsetMs > 0.0f) ? ((layerEndOffsetMs / 1000.0f) * static_cast<float>(fileSampleRate)) : (hasCustomLayer ? static_cast<float>(customLayerBuffer.getNumSamples()) : 0.0f);
    double exactLayerIdx = layerStartSamples + voice.layerReadIndex;

    auto getFadeGain = [&](double exactIdx, float startSmp, float endSmp, float inSmp, float outSmp, float inTen, float outTen) -> float {
        if (exactIdx < startSmp || exactIdx >= endSmp) return 0.0f;
        float gain = 1.0f;
        float passed = static_cast<float>(exactIdx - startSmp);
        float remain = static_cast<float>(endSmp - exactIdx);
        if (inSmp > 0.0f && passed < inSmp)
            gain *= calculateFadeGain(passed / inSmp, inTen);
        else if (passed < 64.0f)
            gain *= juce::jlimit(0.0f, 1.0f, passed / 64.0f);

        if (outSmp > 0.0f && remain < outSmp)
            gain *= calculateFadeGain(remain / outSmp, outTen);
        else if (remain < 64.0f)
            gain *= juce::jlimit(0.0f, 1.0f, remain / 64.0f);

        return gain;
    };

    float transGain = getFadeGain(exactClickIdx, transStartSamples, transEndSamples, tInSmp, tOutSmp, tInTension, tOutTension);
    float tonalGain = getFadeGain(exactSustainIdx, tonalStartSamples, tonalEndSamples, oInSmp, oOutSmp, oInTension, oOutTension);
    float layerGain = getFadeGain(exactLayerIdx, layerStartSamples, layerEndSamples, lInSmp, lOutSmp, lInTension, lOutTension);

    float layerPitchVal = apvts.getRawParameterValue("layerPitch")->load();
    float layerScale = std::pow(2.0f, layerPitchVal / 12.0f);

    // Layer 音声の生成
    if (hasCustomLayer && exactLayerIdx >= 0.0 && exactLayerIdx < static_cast<double>(customLayerBuffer.getNumSamples() - 1))
    {
        if (currentSoloMode != 1 && currentSoloMode != 2 && layerGain > 0.0f)
        {
            float l = readInterpolated(customLayerBuffer, 0, exactLayerIdx);
            float r = customLayerBuffer.getNumChannels() > 1 ? readInterpolated(customLayerBuffer, 1, exactLayerIdx) : l;
            outLayerL = l * layerGain * voice.triggerVelocity * voice.releaseGain;
            outLayerR = r * layerGain * voice.triggerVelocity * voice.releaseGain;
        }
    }
    voice.layerReadIndex += voice.pitchRatio * layerScale;

    // Fast Path (Pitch Shift == 1.0, Custom Sample なし)
    if (!hasCustomTrans && !hasCustomTonal && std::abs(transScale - 1.0f) < 0.01f && std::abs(tonalScale - 1.0f) < 0.01f)
    {
        if (currentSoloMode != 2 && currentSoloMode != 3 && transGain > 0.0f && transBufferThread.getNumSamples() > 0 && exactClickIdx < static_cast<double>(transBufferThread.getNumSamples() - 1))
        {
            float l = readInterpolated(transBufferThread, 0, exactClickIdx);
            float r = transBufferThread.getNumChannels() > 1 ? readInterpolated(transBufferThread, 1, exactClickIdx) : l;
            outTransL = l * transGain * voice.triggerVelocity * voice.releaseGain;
            outTransR = r * transGain * voice.triggerVelocity * voice.releaseGain;
        }
        if (currentSoloMode != 1 && currentSoloMode != 3 && tonalGain > 0.0f && tonalBufferThread.getNumSamples() > 0 && exactSustainIdx >= 0.0 && exactSustainIdx < static_cast<double>(tonalBufferThread.getNumSamples() - 1))
        {
            float l = readInterpolated(tonalBufferThread, 0, exactSustainIdx);
            float r = tonalBufferThread.getNumChannels() > 1 ? readInterpolated(tonalBufferThread, 1, exactSustainIdx) : l;
            outTonalL = l * tonalGain * voice.triggerVelocity * voice.releaseGain;
            outTonalR = r * tonalGain * voice.triggerVelocity * voice.releaseGain;
        }
        
        if (fullMixEndOffsetMs > 0.0f)
        {
            double curMs = (voice.clickReadIndex / fileSampleRate) * 1000.0;
            if (curMs < fullMixStartOffsetMs || curMs >= fullMixEndOffsetMs)
            {
                outTransL = 0.0f; outTransR = 0.0f;
                outTonalL = 0.0f; outTonalR = 0.0f;
                outLayerL = 0.0f; outLayerR = 0.0f;
            }
            else
            {
                double remMs = fullMixEndOffsetMs - curMs;
                if (remMs < 1.5)
                {
                    float declick = static_cast<float>(remMs / 1.5);
                    outTransL *= declick; outTransR *= declick;
                    outTonalL *= declick; outTonalR *= declick;
                    outLayerL *= declick; outLayerR *= declick;
                }
            }
        }
        voice.clickReadIndex += voice.pitchRatio;
        voice.sustainReadIndex += voice.pitchRatio;
        return;
    }

    const auto& click = (!hasCustomTrans) ? transBufferThread : customTransBuffer;
    const auto& sustain = (!hasCustomTonal) ? tonalBufferThread : customTonalBuffer;

    bool transGateOpen = (transGain > 0.0f) && (voice.clickReadIndex < transHoldSamples);
    bool tonalGateOpen = (tonalGain > 0.0f);

    float shiftedClick = 0.0f;
    float shiftedSustain = 0.0f;

    if (transGateOpen && currentSoloMode != 2 && currentSoloMode != 3)
    {
        if (hasCustomTrans)
            shiftedClick = customTransientReplacer.processSample(voice.clickReadIndex, voice.pitchRatio, transScale, clickHold, clickCurve, hostSampleRate, currentSoloMode);
        else if (voice.transShifter && cIdx >= 0 && cIdx < click.getNumSamples())
            shiftedClick = voice.transShifter->processSample(click, cIdx, transScale);
    }
    voice.clickReadIndex += voice.pitchRatio;

    if (tonalGateOpen && currentSoloMode != 1 && currentSoloMode != 3 && exactSustainIdx >= 0.0)
    {
        if (hasCustomTonal)
            shiftedSustain = customTonalReplacer.processSample(voice.sustainReadIndex, voice.pitchRatio, tonalScale, clickHold, clickCurve, hostSampleRate, currentSoloMode);
        else if (voice.tonalShifter && sIdx >= 0 && sIdx < sustain.getNumSamples())
            shiftedSustain = voice.tonalShifter->processSample(sustain, sIdx, tonalScale);
    }
    voice.sustainReadIndex += voice.pitchRatio;

    float sustainClickFade = 1.0f;
    float sustainTonalFade = 1.0f;
    float currentMs = (voice.sustainReadIndex / fileSampleRate) * 1000.0f;

    if (currentMs < clickCurve && clickCurve > 0.0f)
    {
        float progress = currentMs / clickCurve;
        float angle = progress * juce::MathConstants<float>::halfPi;
        sustainClickFade = std::cos(angle);
        sustainTonalFade = std::sin(angle);
    }

    float finalClick = shiftedClick * voice.triggerVelocity * voice.releaseGain * sustainClickFade * transGain;
    float finalSustain = shiftedSustain * voice.triggerVelocity * voice.releaseGain * sustainTonalFade * tonalGain;

    if (fullMixEndOffsetMs > 0.0f)
    {
        double curMs = (voice.clickReadIndex / fileSampleRate) * 1000.0;
        if (curMs < fullMixStartOffsetMs || curMs >= fullMixEndOffsetMs)
        {
            finalClick = 0.0f;
            finalSustain = 0.0f;
            outLayerL = 0.0f;
            outLayerR = 0.0f;
        }
        else
        {
            double remMs = fullMixEndOffsetMs - curMs;
            if (remMs < 1.5)
            {
                float declick = static_cast<float>(remMs / 1.5);
                finalClick *= declick;
                finalSustain *= declick;
                outLayerL *= declick;
                outLayerR *= declick;
            }
        }
    }

    outTransL = finalClick; outTransR = finalClick;
    outTonalL = finalSustain; outTonalR = finalSustain;
}

void AnatomyAudioProcessor::setOffsetsFromUI(int laneIndex, float startMs, float endMs) noexcept
    {
        const juce::ScopedLock sl(lock);

        if (laneIndex == 0)
        {
            fullMixStartOffsetMs = startMs;
            fullMixEndOffsetMs = endMs;
        }
        else if (laneIndex == 1)
        {
            transStartOffsetMs = startMs;
            transEndOffsetMs = endMs;
            customTransientReplacer.setStartOffsetMs(startMs);
            customTransientReplacer.setEndOffsetMs(endMs);
        }
        else if (laneIndex == 2)
        {
            tonalStartOffsetMs = startMs;
            tonalEndOffsetMs = endMs;
            customTonalReplacer.setStartOffsetMs(startMs);
            customTonalReplacer.setEndOffsetMs(endMs);
        }
        else if (laneIndex == 3)
        {
            layerStartOffsetMs = startMs;
            layerEndOffsetMs = endMs;
            customLayerReplacer.setStartOffsetMs(startMs);
            customLayerReplacer.setEndOffsetMs(endMs);
        }
        offlineMixRenderer.triggerRender();
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

    void AnatomyAudioProcessor::parameterChanged(const juce::String& paramID, float)
    {
        if (paramID == "clickLength" || paramID == "clickCurve")
        {
            needsReanalysis.store(true, std::memory_order_release);
        }
        offlineMixRenderer.triggerRender();
    }

    void AnatomyAudioProcessor::setFadeFromUI(int laneIndex, float inMs, float outMs, float inTension, float outTension) noexcept
    {
        if (laneIndex == 1)
        {
            customTransientReplacer.setFadeInMs(inMs);
            customTransientReplacer.setFadeOutMs(outMs);
            customTransientReplacer.setFadeInTension(inTension);
            customTransientReplacer.setFadeOutTension(outTension);
        }
        else if (laneIndex == 2)
        {
            customTonalReplacer.setFadeInMs(inMs);
            customTonalReplacer.setFadeOutMs(outMs);
            customTonalReplacer.setFadeInTension(inTension);
            customTonalReplacer.setFadeOutTension(outTension);
        }
        else if (laneIndex == 3)
        {
            customLayerReplacer.setFadeInMs(inMs);
            customLayerReplacer.setFadeOutMs(outMs);
            customLayerReplacer.setFadeInTension(inTension);
            customLayerReplacer.setFadeOutTension(outTension);
        }
        offlineMixRenderer.triggerRender();
    }

    void AnatomyAudioProcessor::getFadeForUI(int laneIndex, float& inMs, float& outMs, float& inTension, float& outTension) const noexcept
    {
        if (laneIndex == 1)
        {
            inMs = customTransientReplacer.getFadeInMs();
            outMs = customTransientReplacer.getFadeOutMs();
            inTension = customTransientReplacer.getFadeInTension();
            outTension = customTransientReplacer.getFadeOutTension();
        }
        else if (laneIndex == 2)
        {
            inMs = customTonalReplacer.getFadeInMs();
            outMs = customTonalReplacer.getFadeOutMs();
            inTension = customTonalReplacer.getFadeInTension();
            outTension = customTonalReplacer.getFadeOutTension();
        }
        else if (laneIndex == 3)
        {
            inMs = customLayerReplacer.getFadeInMs();
            outMs = customLayerReplacer.getFadeOutMs();
            inTension = customLayerReplacer.getFadeInTension();
            outTension = customLayerReplacer.getFadeOutTension();
        }
        else
        {
            inMs = 0.0f; outMs = 0.0f; inTension = 0.0f; outTension = 0.0f;
        }
    }

    void AnatomyAudioProcessor::setLaneSolo(int laneIndex, bool isSolo)
    {
        int current = getSoloMode();
        if (isSolo)
        {
            setSoloMode(laneIndex);
        }
        else
        {
            if (current == laneIndex) setSoloMode(0);
        }
    }

    bool AnatomyAudioProcessor::isLaneSolo(int laneIndex) const noexcept
    {
        return getSoloMode() == laneIndex;
    }

    void AnatomyAudioProcessor::storeCustomSampleFromUI(int laneIndex, const juce::AudioBuffer<float>& newBuffer, double sr) noexcept
    {
        const juce::ScopedLock sl(lock);
        float durationMs = (sr > 0.0) ? (static_cast<float>(newBuffer.getNumSamples()) / static_cast<float>(sr)) * 1000.0f : 0.0f;

        if (laneIndex == 1)
        {
            customTransBuffer.makeCopyOf(newBuffer);
            customTransientReplacer.loadSample(newBuffer, sr);
            transStartOffsetMs = 0.0f;
            transEndOffsetMs = durationMs;
            customTransientReplacer.setStartOffsetMs(0.0f);
            customTransientReplacer.setEndOffsetMs(durationMs);
        }
        else if (laneIndex == 2)
        {
            customTonalBuffer.makeCopyOf(newBuffer);
            customTonalReplacer.loadSample(newBuffer, sr);
            tonalStartOffsetMs = 0.0f;
            tonalEndOffsetMs = durationMs;
            customTonalReplacer.setStartOffsetMs(0.0f);
            customTonalReplacer.setEndOffsetMs(durationMs);
        }
        else if (laneIndex == 3)
        {
            customLayerBuffer.makeCopyOf(newBuffer);
            customLayerReplacer.loadSample(newBuffer, sr);
            layerStartOffsetMs = 0.0f;
            layerEndOffsetMs = durationMs;
            customLayerReplacer.setStartOffsetMs(0.0f);
            customLayerReplacer.setEndOffsetMs(durationMs);
        }
        updateActiveSampleData();
        offlineMixRenderer.triggerRender();
    }

    void AnatomyAudioProcessor::clearCustomSampleFromUI(int laneIndex) noexcept
    {
        const juce::ScopedLock sl(lock);
        if (laneIndex == 1)
        {
            customTransBuffer.setSize(0, 0);
            customTransientReplacer.clearSample();
            float origDurationMs = (fileSampleRate > 0.0) ? (static_cast<float>(transBufferThread.getNumSamples()) / static_cast<float>(fileSampleRate)) * 1000.0f : 0.0f;
            transStartOffsetMs = 0.0f;
            transEndOffsetMs = origDurationMs;
            customTransientReplacer.setStartOffsetMs(0.0f);
            customTransientReplacer.setEndOffsetMs(origDurationMs);
        }
        else if (laneIndex == 2)
        {
            customTonalBuffer.setSize(0, 0);
            customTonalReplacer.clearSample();
            float origDurationMs = (fileSampleRate > 0.0) ? (static_cast<float>(tonalBufferThread.getNumSamples()) / static_cast<float>(fileSampleRate)) * 1000.0f : 0.0f;
            tonalStartOffsetMs = 0.0f;
            tonalEndOffsetMs = origDurationMs;
            customTonalReplacer.setStartOffsetMs(0.0f);
            customTonalReplacer.setEndOffsetMs(origDurationMs);
        }
        else if (laneIndex == 3)
        {
            customLayerBuffer.setSize(0, 0);
            customLayerReplacer.clearSample();
            layerStartOffsetMs = 0.0f;
            layerEndOffsetMs = 0.0f;
            customLayerReplacer.setStartOffsetMs(0.0f);
            customLayerReplacer.setEndOffsetMs(0.0f);
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

            fullMixStartOffsetMs = 0.0f;
            fullMixEndOffsetMs = 0.0f;
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

            fullMixStartOffsetMs = 0.0f;
            fullMixEndOffsetMs = static_cast<float>(durationMs);
            transStartOffsetMs = 0.0f;
            transEndOffsetMs = static_cast<float>(durationMs);
            tonalStartOffsetMs = 0.0f;
            tonalEndOffsetMs = static_cast<float>(durationMs);
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
        const juce::AudioBuffer<float>& layerSrc = customLayerBuffer;

        const int transSamples = transSrc.getNumSamples();
        const int tonalSamples = tonalSrc.getNumSamples();
        const int layerSamples = layerSrc.getNumSamples();
        const int maxSamples = std::max({ transSamples, tonalSamples, layerSamples });

        if (maxSamples == 0) return;

        juce::AudioBuffer<float> activeClick(1, maxSamples);
        juce::AudioBuffer<float> activeSustain(1, maxSamples);
        juce::AudioBuffer<float> activeLayer(1, maxSamples);
        activeClick.clear(); activeSustain.clear(); activeLayer.clear();

        if (currentSoloMode == 0)
        {
            if (transSamples > 0) activeClick.copyFrom(0, 0, transSrc, 0, 0, transSamples);
            if (tonalSamples > 0) activeSustain.copyFrom(0, 0, tonalSrc, 0, 0, tonalSamples);
            if (layerSamples > 0) activeLayer.copyFrom(0, 0, layerSrc, 0, 0, layerSamples);
        }
        else if (currentSoloMode == 1)
        {
            if (transSamples > 0) activeClick.copyFrom(0, 0, transSrc, 0, 0, transSamples);
        }
        else if (currentSoloMode == 2)
        {
            if (tonalSamples > 0) activeSustain.copyFrom(0, 0, tonalSrc, 0, 0, tonalSamples);
        }
        else if (currentSoloMode == 3)
        {
            if (layerSamples > 0) activeLayer.copyFrom(0, 0, layerSrc, 0, 0, layerSamples);
        }

        SharedSampleData* newData = new SharedSampleData(std::move(activeClick), std::move(activeSustain), std::move(activeLayer), fileSampleRate);
        SharedSampleData* oldData = masterSampleData.exchange(newData, std::memory_order_acq_rel);
        if (oldData != nullptr) garbageBin.push_back(oldData);
    }

void AnatomyAudioProcessor::cleanUpGarbageBin()
{
    auto it = garbageBin.begin();
    while (it != garbageBin.end())
    {
        SharedSampleData* oldData = *it;
        bool isStillReferencedByVoice = false;
        if (activeVoice.isActive && activeVoice.sampleData == oldData) isStillReferencedByVoice = true;
        for (int i = 0; i < maxReleasingVoices; ++i)
        {
            if (releasingVoices[i].isActive && releasingVoices[i].sampleData == oldData) isStillReferencedByVoice = true;
        }

        if (!isStillReferencedByVoice)
        {
            if (oldData != nullptr) delete oldData;
            it = garbageBin.erase(it);
        }
        else ++it;
    }
    for (auto* oldFxSnapshot : fxGarbageBin)
        if (oldFxSnapshot != nullptr) delete oldFxSnapshot;
    fxGarbageBin.clear();
}

void AnatomyAudioProcessor::flushPendingExports()
{
    for (int l = 0; l < 4; ++l)
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

void AnatomyAudioProcessor::applyEffectsOffline(juce::AudioBuffer<float>& buffer, TargetRoute route, double sr)
{
    const std::vector<int>& order = getEffectOrder(route);
    if (order.empty()) return;

    int laneIdx = (route == TargetRoute::Transient) ? 0 : ((route == TargetRoute::Tonal) ? 1 : ((route == TargetRoute::FullMix) ? 2 : 3));
    const auto& c = cachedLanes[laneIdx];
    const int numSamples = buffer.getNumSamples();

    for (int idx : order)
    {
        if (idx == 0) // SAT
        {
            ADAA_Saturation sat;
            sat.prepare(sr, numSamples);
            if (c.satDrive) sat.setDrive(c.satDrive->load());
            if (c.satMix)   sat.setMix(c.satMix->load());
            if (c.satType)  sat.setType(static_cast<int>(c.satType->load()));
            if (c.satTrim)  sat.setOutputTrimDb(c.satTrim->load());
            if (c.satPre)   sat.setPreCutoffHz(c.satPre->load());
            sat.process(buffer);
        }
        else if (idx == 1) // BITCRUSH
        {
            BitCrusher bc;
            bc.prepare(sr, numSamples);
            if (c.bcBits)   bc.setBits(c.bcBits->load());
            if (c.bcDown)   bc.setDownsample(c.bcDown->load());
            if (c.bcMix)    bc.setMix(c.bcMix->load());
            if (c.bcJitter) bc.setJitter(c.bcJitter->load());
            bc.process(buffer);
        }
        else if (idx == 2) // NOISE
        {
            NoiseGenerator ns;
            ns.prepare(sr, numSamples);
            if (c.nsDecay)  ns.setDecay(c.nsDecay->load());
            if (c.nsMix)    ns.setMix(c.nsMix->load());
            if (c.nsType)   ns.setNoiseType(static_cast<int>(c.nsType->load()));
            if (c.nsGain)   ns.setGainDb(c.nsGain->load());
            if (c.nsAttack) ns.setAttack(c.nsAttack->load());
            if (c.nsBpFreq) ns.setBpCenterHz(c.nsBpFreq->load());
            ns.trigger();
            ns.process(buffer);
        }
        else if (idx == 3) // OTT
        {
            OTT_Multiband ott;
            ott.prepare(sr, numSamples);
            if (c.ottDepth) ott.setMix(c.ottDepth->load());
            if (c.ottTime)  ott.setTimeMultiplier(c.ottTime->load());
            if (c.ottLowMidXOver) ott.setLowMidXOver(c.ottLowMidXOver->load());
            if (c.ottMidHighXOver) ott.setMidHighXOver(c.ottMidHighXOver->load());
            if (c.ottGateFloor) ott.setGateFloorDb(c.ottGateFloor->load());
            for (int b = 0; b < 3; ++b)
            {
                if (c.ottBandUp[b])   ott.setBandUpward(b, c.ottBandUp[b]->load());
                if (c.ottBandDown[b]) ott.setBandDownward(b, c.ottBandDown[b]->load());
                if (c.ottBandGain[b]) ott.setBandGainDb(b, c.ottBandGain[b]->load());
            }
            ott.process(buffer);
        }
        else if (idx == 4) // GLUE
        {
            GlueCompressor glue;
            glue.prepare(sr, numSamples);
            if (c.glueDepth) glue.setMix(c.glueDepth->load());
            if (c.glueThr)   glue.setThresholdDb(c.glueThr->load());
            if (c.glueRatio) glue.setRatio(c.glueRatio->load());
            if (c.glueAtk)   glue.setAttackMs(c.glueAtk->load());
            if (c.glueRel)   glue.setReleaseMs(c.glueRel->load());
            if (c.glueMkp)   glue.setMakeupDb(c.glueMkp->load());
            glue.process(buffer);
        }
        else if (idx == 5) // LIMITER
        {
            Limiter lim;
            lim.prepare(sr, numSamples);
            if (c.limGain) lim.setInputGain(c.limGain->load());
            if (c.limCeil) lim.setCeiling(c.limCeil->load());
            if (c.limMix)  lim.setMix(c.limMix->load());
            if (c.limMode) lim.setMode(static_cast<int>(c.limMode->load()));
            lim.process(buffer);
        }
        else if (idx == 6) // TRANSIENT SHAPER
        {
            TransientShaper ts;
            ts.prepare(sr, numSamples);
            if (c.tsAttack)  ts.setAttack(c.tsAttack->load());
            if (c.tsSustain) ts.setSustain(c.tsSustain->load());
            if (c.tsMix)     ts.setMix(c.tsMix->load());
            ts.process(buffer);
        }
    }
}

juce::File AnatomyAudioProcessor::createTemporaryWavForExport(int laneIndex)
{
    // laneIndex: 0 = FullMix, 1 = Transient, 2 = Tonal
    double sr = (fileSampleRate > 0.0) ? fileSampleRate : 44100.0;

    juce::AudioBuffer<float> localTrans, localTonal, localLayer;
    {
        const juce::ScopedLock sl(lock);
        localTrans.makeCopyOf(customTransBuffer.getNumSamples() > 0 ? customTransBuffer : transBufferThread);
        localTonal.makeCopyOf(customTonalBuffer.getNumSamples() > 0 ? customTonalBuffer : tonalBufferThread);
        localLayer.makeCopyOf(customLayerBuffer);
    }

    int transSamples = localTrans.getNumSamples();
    int tonalSamples = localTonal.getNumSamples();
    int layerSamples = localLayer.getNumSamples();
    int rawMaxSamples = std::max({transSamples, tonalSamples, layerSamples});
    if (rawMaxSamples == 0) return {};

    // エフェクトの余韻（Decay等）を含めるためにテイルを追加
    int tailSamples = static_cast<int>(1.5 * sr);
    int maxSamples = rawMaxSamples + tailSamples;

    // 1. レンダリング用のバッファと VoiceState の準備
    juce::AudioBuffer<float> renderedTrans(2, maxSamples);
    juce::AudioBuffer<float> renderedTonal(2, maxSamples);
    juce::AudioBuffer<float> renderedLayer(2, maxSamples);
    renderedTrans.clear();
    renderedTonal.clear();
    renderedLayer.clear();

    VoiceState exportVoice;
    exportVoice.sampleData = masterSampleData.load(std::memory_order_relaxed); // atomic load
    exportVoice.clickReadIndex = 0.0;
    exportVoice.sustainReadIndex = 0.0;
    exportVoice.layerReadIndex = 0.0;
    
    // ピッチレシオ（generateVoiceSample は内部でこれを使う）
    exportVoice.pitchRatio = 1.0; 
    exportVoice.triggerVelocity = 1.0f;
    exportVoice.releaseGain = 1.0f;
    
    exportVoice.reallocateShifters(sr);

    float clickHold = apvts.getRawParameterValue("clickLength")->load();
    float clickCurve = apvts.getRawParameterValue("clickCurve")->load();
    
    float transPitch = apvts.getRawParameterValue("transPitch")->load();
    float tonalPitch = apvts.getRawParameterValue("tonalPitch")->load();
    float transScale = (std::abs(transPitch) >= 0.01f) ? std::pow(2.0f, transPitch / 12.0f) : 1.0f;
    float tonalScale = (std::abs(tonalPitch) >= 0.01f) ? std::pow(2.0f, tonalPitch / 12.0f) : 1.0f;
    
    // Tonal Offset の適用
    float tonalDelayMs = apvts.getRawParameterValue("tonalDelay")->load();
    exportVoice.sustainReadIndex = -(tonalDelayMs / 1000.0) * sr;

    // Layer Offset の適用
    float layerOffsetMs = apvts.getRawParameterValue("layerOffset")->load();
    exportVoice.layerReadIndex = -(layerOffsetMs / 1000.0) * sr;

    // Mix Gain の適用は generateVoiceSample 内には無いので後でかける
    float transMixGain = std::pow(10.0f, apvts.getRawParameterValue("transMixGain")->load() / 20.0f);
    float tonalMixGain = std::pow(10.0f, apvts.getRawParameterValue("tonalMixGain")->load() / 20.0f);
    float layerGain    = std::pow(10.0f, apvts.getRawParameterValue("layerGain")->load() / 20.0f);

    // 一時的にSoloモードを解除して、全パートが鳴る状態(0)でレンダリングする
    int savedSolo = currentSoloMode.load();
    currentSoloMode.store(0);

    for (int s = 0; s < maxSamples; ++s)
    {
        float outTransL = 0.0f, outTransR = 0.0f, outTonalL = 0.0f, outTonalR = 0.0f, outLayerL = 0.0f, outLayerR = 0.0f;
        generateVoiceSample(exportVoice, outTransL, outTransR, outTonalL, outTonalR, outLayerL, outLayerR, clickHold, clickCurve, transScale, tonalScale, sr);
        
        renderedTrans.setSample(0, s, outTransL * transMixGain);
        renderedTrans.setSample(1, s, outTransR * transMixGain);
        renderedTonal.setSample(0, s, outTonalL * tonalMixGain);
        renderedTonal.setSample(1, s, outTonalR * tonalMixGain);
        renderedLayer.setSample(0, s, outLayerL * layerGain);
        renderedLayer.setSample(1, s, outLayerR * layerGain);
    }
    
    // Soloモード復元
    currentSoloMode.store(savedSolo);

    // 2. 専用 FX 適用！
    applyEffectsOffline(renderedTrans, TargetRoute::Transient, sr);
    applyEffectsOffline(renderedTonal, TargetRoute::Tonal, sr);
    applyEffectsOffline(renderedLayer, TargetRoute::Layer, sr);

    // 3. 出力バッファ決定
    juce::AudioBuffer<float> exportBuf;

    if (laneIndex == 1) // Transient EXPORT
    {
        exportBuf.makeCopyOf(renderedTrans);
    }
    else if (laneIndex == 2) // Tonal EXPORT
    {
        exportBuf.makeCopyOf(renderedTonal);
    }
    else if (laneIndex == 3) // Layer EXPORT
    {
        exportBuf.makeCopyOf(renderedLayer);
    }
    else // FullMix EXPORT (laneIndex == 0)
    {
        int soloMode = getSoloMode();
        juce::AudioBuffer<float> fullMixBuf(2, maxSamples);
        fullMixBuf.clear();

        for (int s = 0; s < maxSamples; ++s)
        {
            float tL = (soloMode == 2 || soloMode == 3) ? 0.0f : renderedTrans.getSample(0, s);
            float tR = (soloMode == 2 || soloMode == 3) ? 0.0f : renderedTrans.getSample(1, s);
            float oL = (soloMode == 1 || soloMode == 3) ? 0.0f : renderedTonal.getSample(0, s);
            float oR = (soloMode == 1 || soloMode == 3) ? 0.0f : renderedTonal.getSample(1, s);
            float lL = (soloMode == 1 || soloMode == 2) ? 0.0f : renderedLayer.getSample(0, s);
            float lR = (soloMode == 1 || soloMode == 2) ? 0.0f : renderedLayer.getSample(1, s);

            fullMixBuf.setSample(0, s, tL + oL + lL);
            fullMixBuf.setSample(1, s, tR + oR + lR);
        }

        // FullMix 専用 FX 適用！
        applyEffectsOffline(fullMixBuf, TargetRoute::FullMix, sr);

        // FullMix の Start/End オフセットでトリミング
        int fStartSmp = static_cast<int>((fullMixStartOffsetMs / 1000.0) * sr);
        int fEndSmp = (fullMixEndOffsetMs > 0.0f) ? (static_cast<int>((fullMixEndOffsetMs / 1000.0) * sr) + static_cast<int>(1.5 * sr)) : maxSamples;

        fStartSmp = juce::jlimit(0, maxSamples, fStartSmp);
        fEndSmp = juce::jlimit(fStartSmp, maxSamples, fEndSmp);
        int trimLength = fEndSmp - fStartSmp;

        if (trimLength > 0 && trimLength < maxSamples)
        {
            exportBuf.setSize(2, trimLength);
            for (int ch = 0; ch < 2; ++ch)
                exportBuf.copyFrom(ch, 0, fullMixBuf, ch, fStartSmp, trimLength);
        }
        else
        {
            exportBuf.makeCopyOf(fullMixBuf);
        }
    }

    if (exportBuf.getNumSamples() == 0 || exportBuf.getNumChannels() == 0)
        return {};

    juce::File tempDir = juce::File::getSpecialLocation(juce::File::SpecialLocationType::tempDirectory);
    juce::String laneName = (laneIndex == 0 ? "FullMix" : (laneIndex == 1 ? "Transient" : "Tonal"));
    juce::File exportFile = tempDir.getChildFile("ANATOMY_" + laneName + "_" + juce::String(juce::Random::getSystemRandom().nextInt64()) + ".wav");

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(
        new juce::FileOutputStream(exportFile),
        sr, exportBuf.getNumChannels(), 24, {}, 0));

    if (writer != nullptr)
    {
        writer->writeFromAudioSampleBuffer(exportBuf, 0, exportBuf.getNumSamples());
        writer.reset();
        return exportFile;
    }
    return {};
}

void OfflineMixRenderer::executeRender()
{
    if (threadShouldExit()) return;

    juce::AudioBuffer<float> localTrans, localTonal, localLayer;
    double sr = 44100.0;

    {
        const juce::ScopedLock sl(processor.lock);
        localTrans.makeCopyOf(processor.customTransBuffer.getNumSamples() > 0 ? processor.customTransBuffer : processor.transBufferThread);
        localTonal.makeCopyOf(processor.customTonalBuffer.getNumSamples() > 0 ? processor.customTonalBuffer : processor.tonalBufferThread);
        localLayer.makeCopyOf(processor.customLayerBuffer);
        sr = processor.fileSampleRate;
    }

    const int transSamples = localTrans.getNumSamples();
    const int tonalSamples = localTonal.getNumSamples();
    const int layerSamples = localLayer.getNumSamples();
    const int maxSamples = std::max({transSamples, tonalSamples, layerSamples});
    if (maxSamples == 0) return;

    if (localTrans.getNumChannels() <= 0 && localTonal.getNumChannels() <= 0 && localLayer.getNumChannels() <= 0) return;

    juce::AudioBuffer<float> workTrans(2, maxSamples);
    juce::AudioBuffer<float> workTonal(2, maxSamples);
    juce::AudioBuffer<float> workLayer(2, maxSamples);
    workTrans.clear(); workTonal.clear(); workLayer.clear();

    for (int ch = 0; ch < 2; ++ch)
    {
        if (transSamples > 0 && ch < localTrans.getNumChannels()) workTrans.copyFrom(ch, 0, localTrans, ch, 0, transSamples);
        if (tonalSamples > 0 && ch < localTonal.getNumChannels()) workTonal.copyFrom(ch, 0, localTonal, ch, 0, tonalSamples);
        if (layerSamples > 0 && ch < localLayer.getNumChannels()) workLayer.copyFrom(ch, 0, localLayer, ch, 0, layerSamples);
    }

    float transPitch = processor.apvts.getRawParameterValue("transPitch")->load();
    float tonalPitch = processor.apvts.getRawParameterValue("tonalPitch")->load();
    float layerPitch = processor.apvts.getRawParameterValue("layerPitch")->load();

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
                if (srcIdx < maxSamples - 1)
                {
                    int i0 = static_cast<int>(srcIdx);
                    float frac = static_cast<float>(srcIdx - static_cast<double>(i0));
                    int im1 = std::max(0, i0 - 1);
                    int i1  = std::min(i0 + 1, maxSamples - 1);
                    int i2  = std::min(i0 + 2, maxSamples - 1);

                    const float ym1 = src[im1], y0 = src[i0], y1 = src[i1], y2 = src[i2];
                    const float c0 = y0;
                    const float c1 = 0.5f * (y1 - ym1);
                    const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
                    const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
                    dest[s] = ((c3 * frac + c2) * frac + c1) * frac + c0;
                }
            }
        }
    };
    applyPitch(workTrans, transPitch);
    applyPitch(workTonal, tonalPitch);
    applyPitch(workLayer, layerPitch);

    float transGain = std::pow(10.0f, processor.apvts.getRawParameterValue("transMixGain")->load() / 20.0f);
    float tonalGain = std::pow(10.0f, processor.apvts.getRawParameterValue("tonalMixGain")->load() / 20.0f);
    float layerGain = std::pow(10.0f, processor.apvts.getRawParameterValue("layerGain")->load() / 20.0f);
    
    float layerOffsetMs = processor.apvts.getRawParameterValue("layerOffset")->load();
    int layerOffsetSamples = static_cast<int>((layerOffsetMs / 1000.0f) * sr);

    juce::AudioBuffer<float> outputMix(2, maxSamples);
    juce::AudioBuffer<float> outTransRendered(2, maxSamples);
    juce::AudioBuffer<float> outTonalRendered(2, maxSamples);
    juce::AudioBuffer<float> outLayerRendered(2, maxSamples);
    outTransRendered.clear();
    outTonalRendered.clear();
    outLayerRendered.clear();
    std::vector<float> ratios(maxSamples * 2, 0.0f);

    float transStart = processor.transStartOffsetMs;
    float transEnd = processor.transEndOffsetMs;
    float tonalStart = processor.tonalStartOffsetMs;
    float tonalEnd = processor.tonalEndOffsetMs;
    float clickHold = processor.apvts.getRawParameterValue("clickLength")->load();

    int tStartSmp = static_cast<int>((transStart / 1000.0) * sr);
    int tEndSmp = static_cast<int>((transEnd / 1000.0) * sr);
    float sustainFade = processor.apvts.getRawParameterValue("clickCurve")->load();
    int tHoldSmp = static_cast<int>(((clickHold + sustainFade) / 1000.0) * sr);
    int oStartSmp = static_cast<int>((tonalStart / 1000.0) * sr);
    int oEndSmp = static_cast<int>((tonalEnd / 1000.0) * sr);

    float tonalDelayMs = processor.apvts.getRawParameterValue("tonalDelay")->load();
    int tonalOffsetSmp = static_cast<int>(-(tonalDelayMs / 1000.0) * sr);

    // フェード設定
    float tInMs, tOutMs, tInTension, tOutTension;
    processor.getFadeForUI(1, tInMs, tOutMs, tInTension, tOutTension);
    int tInSmp = static_cast<int>((tInMs / 1000.0) * sr);
    int tOutSmp = static_cast<int>((tOutMs / 1000.0) * sr);

    float oInMs, oOutMs, oInTension, oOutTension;
    processor.getFadeForUI(2, oInMs, oOutMs, oInTension, oOutTension);
    int oInSmp = static_cast<int>((oInMs / 1000.0) * sr);
    int oOutSmp = static_cast<int>((oOutMs / 1000.0) * sr);

    float lInMs, lOutMs, lInTension, lOutTension;
    processor.getFadeForUI(3, lInMs, lOutMs, lInTension, lOutTension);
    int lInSmp = static_cast<int>((lInMs / 1000.0) * sr);
    int lOutSmp = static_cast<int>((lOutMs / 1000.0) * sr);
    float lStart = processor.layerStartOffsetMs;
    float lEnd = processor.layerEndOffsetMs;
    int lStartSmp = static_cast<int>((lStart / 1000.0) * sr);
    int lEndSmp = (lEnd > 0.0f) ? static_cast<int>((lEnd / 1000.0) * sr) : layerSamples;

    int soloMode = processor.getSoloMode();
    float fStart = processor.fullMixStartOffsetMs;
    float fEnd = processor.fullMixEndOffsetMs;
    int fStartSmp = static_cast<int>((fStart / 1000.0) * sr);
    int fEndSmp = (fEnd > 0.0f) ? static_cast<int>((fEnd / 1000.0) * sr) : maxSamples;

    for (int s = 0; s < maxSamples; ++s)
    {
        float tL = 0.0f, tR = 0.0f;
        float oL = 0.0f, oR = 0.0f;
        float lL = 0.0f, lR = 0.0f;

        int exactClick = tStartSmp + s;
        int exactSustain = oStartSmp + s + tonalOffsetSmp;
        int exactLayer = lStartSmp + s - layerOffsetSamples;

        if (s < tHoldSmp && exactClick < tEndSmp && exactClick < transSamples)
        {
            float fGain = 1.0f;
            if (tInSmp > 1 && s < tInSmp)
                fGain *= calculateFadeGain(static_cast<float>(s) / static_cast<float>(tInSmp), tInTension);
            int remT = tEndSmp - exactClick;
            if (tOutSmp > 1 && remT < tOutSmp)
                fGain *= calculateFadeGain(static_cast<float>(remT) / static_cast<float>(tOutSmp), tOutTension);

            tL = workTrans.getSample(0, exactClick) * transGain * fGain;
            tR = workTrans.getSample(1, exactClick) * transGain * fGain;
        }

        if (exactSustain >= 0 && exactSustain < oEndSmp && exactSustain < tonalSamples)
        {
            float fGain = 1.0f;
            if (oInSmp > 1 && s < oInSmp)
                fGain *= calculateFadeGain(static_cast<float>(s) / static_cast<float>(oInSmp), oInTension);
            int remO = oEndSmp - exactSustain;
            if (oOutSmp > 1 && remO < oOutSmp)
                fGain *= calculateFadeGain(static_cast<float>(remO) / static_cast<float>(oOutSmp), oOutTension);

            oL = workTonal.getSample(0, exactSustain) * tonalGain * fGain;
            oR = workTonal.getSample(1, exactSustain) * tonalGain * fGain;
        }

        if (exactLayer >= 0 && exactLayer < lEndSmp && exactLayer < layerSamples)
        {
            float fGain = 1.0f;
            int relL = exactLayer - lStartSmp;
            if (lInSmp > 1 && relL < lInSmp)
                fGain *= calculateFadeGain(static_cast<float>(relL) / static_cast<float>(lInSmp), lInTension);
            int remL = lEndSmp - exactLayer;
            if (lOutSmp > 1 && remL < lOutSmp)
                fGain *= calculateFadeGain(static_cast<float>(remL) / static_cast<float>(lOutSmp), lOutTension);

            lL = workLayer.getSample(0, exactLayer) * layerGain * fGain;
            lR = workLayer.getSample(1, exactLayer) * layerGain * fGain;
        }

        outTransRendered.setSample(0, s, tL);
        outTransRendered.setSample(1, s, tR);
        outTonalRendered.setSample(0, s, oL);
        outTonalRendered.setSample(1, s, oR);
        outLayerRendered.setSample(0, s, lL);
        outLayerRendered.setSample(1, s, lR);
    }

    // 各パートに専用 FX をオフライン適用
    if (threadShouldExit()) return;
    processor.applyEffectsOffline(outTransRendered, TargetRoute::Transient, sr);
    if (threadShouldExit()) return;
    processor.applyEffectsOffline(outTonalRendered, TargetRoute::Tonal, sr);
    if (threadShouldExit()) return;
    processor.applyEffectsOffline(outLayerRendered, TargetRoute::Layer, sr);

    for (int s = 0; s < maxSamples; ++s)
    {
        float tL = (soloMode == 2 || soloMode == 3) ? 0.0f : outTransRendered.getSample(0, s);
        float tR = (soloMode == 2 || soloMode == 3) ? 0.0f : outTransRendered.getSample(1, s);
        float oL = (soloMode == 1 || soloMode == 3) ? 0.0f : outTonalRendered.getSample(0, s);
        float oR = (soloMode == 1 || soloMode == 3) ? 0.0f : outTonalRendered.getSample(1, s);
        float lL = (soloMode == 1 || soloMode == 2) ? 0.0f : outLayerRendered.getSample(0, s);
        float lR = (soloMode == 1 || soloMode == 2) ? 0.0f : outLayerRendered.getSample(1, s);

        float mixL = tL + oL + lL;
        float mixR = tR + oR + lR;

        if (s < fStartSmp || s >= fEndSmp)
        {
            mixL = 0.0f;
            mixR = 0.0f;
        }

        outputMix.setSample(0, s, mixL);
        outputMix.setSample(1, s, mixR);

        float tEnergy = (tL * tL) + (tR * tR);
        float oEnergy = (oL * oL) + (oR * oR);
        float lEnergy = (lL * lL) + (lR * lR);
        float sum = tEnergy + oEnergy + lEnergy;

        if (sum > 1.0e-6f)
        {
            ratios[s * 2]     = tEnergy / sum;
            ratios[s * 2 + 1] = lEnergy / sum;
        }
        else
        {
            ratios[s * 2]     = 0.5f;
            ratios[s * 2 + 1] = 0.0f;
        }
    }

    processor.applyEffectsOffline(outputMix, TargetRoute::FullMix, sr);

    {
        const juce::ScopedLock sl(renderLock);
        renderedFullMix.makeCopyOf(outputMix);
        renderedTransient.makeCopyOf(outTransRendered);
        renderedTonal.makeCopyOf(outTonalRendered);
        renderedLayer.makeCopyOf(outLayerRendered);
        componentRatios = std::move(ratios);
    }
    hasNewRender.store(true, std::memory_order_release);
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
    bool hasCustomLayer = (customLayerBuffer.getNumSamples() > 0);
    out.writeBool(hasCustomTrans);
    if (hasCustomTrans) {
        writeBufferToStream(out, customTransBuffer);
        out.writeString(customSampleNames[1]);
    }
    out.writeBool(hasCustomTonal);
    if (hasCustomTonal) {
        writeBufferToStream(out, customTonalBuffer);
        out.writeString(customSampleNames[2]);
    }
    out.writeBool(hasCustomLayer);
    if (hasCustomLayer) {
        writeBufferToStream(out, customLayerBuffer);
        out.writeString(customSampleNames[3]);
    }

    // 6. START/END オフセット
    out.writeFloat(transStartOffsetMs);
    out.writeFloat(transEndOffsetMs);
    out.writeFloat(tonalStartOffsetMs);
    out.writeFloat(tonalEndOffsetMs);
    out.writeFloat(layerStartOffsetMs);
    out.writeFloat(layerEndOffsetMs);

    // 7. フェード情報
    out.writeFloat(customTransientReplacer.getFadeInMs());
    out.writeFloat(customTransientReplacer.getFadeOutMs());
    out.writeFloat(customTransientReplacer.getFadeInTension());
    out.writeFloat(customTransientReplacer.getFadeOutTension());

    out.writeFloat(customTonalReplacer.getFadeInMs());
    out.writeFloat(customTonalReplacer.getFadeOutMs());
    out.writeFloat(customTonalReplacer.getFadeInTension());
    out.writeFloat(customTonalReplacer.getFadeOutTension());

    out.writeFloat(customLayerReplacer.getFadeInMs());
    out.writeFloat(customLayerReplacer.getFadeOutMs());
    out.writeFloat(customLayerReplacer.getFadeInTension());
    out.writeFloat(customLayerReplacer.getFadeOutTension());

    // 8. Solo モード
    out.writeInt(currentSoloMode.load(std::memory_order_acquire));

    // 9. エフェクト処理順（ChipBar復元用）
    auto writeOrder = [&](const std::vector<int>& order)
    {
        out.writeInt(static_cast<int>(order.size()));
        for (int idx : order) out.writeInt(idx);
    };
    writeOrder(transEffectOrder);
    writeOrder(tonalEffectOrder);
    writeOrder(layerEffectOrder);
    writeOrder(fullMixEffectOrder);
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
    juce::AudioBuffer<float> restoredCustomTrans, restoredCustomTonal, restoredCustomLayer;
    if (!in.isExhausted())
    {
        bool hasCustomTrans = in.readBool();
        if (hasCustomTrans) {
            restoredCustomTrans = readBufferFromStream(in);
            customSampleNames[1] = in.readString();
        }
    }
    if (!in.isExhausted())
    {
        bool hasCustomTonal = in.readBool();
        if (hasCustomTonal) {
            restoredCustomTonal = readBufferFromStream(in);
            customSampleNames[2] = in.readString();
        }
    }
    if (!in.isExhausted())
    {
        bool hasCustomLayer = in.readBool();
        if (hasCustomLayer) {
            restoredCustomLayer = readBufferFromStream(in);
            customSampleNames[3] = in.readString();
        }
    }

    // 6. START/END オフセット復元
    if (!in.isExhausted())
    {
        transStartOffsetMs = in.readFloat();
        transEndOffsetMs = in.readFloat();
        tonalStartOffsetMs = in.readFloat();
        tonalEndOffsetMs = in.readFloat();
    }
    if (!in.isExhausted())
    {
        layerStartOffsetMs = in.readFloat();
        layerEndOffsetMs = in.readFloat();
    }

    // 7. フェード復元
    if (!in.isExhausted())
    {
        float tIn = in.readFloat(), tOut = in.readFloat(), tInT = in.readFloat(), tOutT = in.readFloat();
        customTransientReplacer.setFadeInMs(tIn);
        customTransientReplacer.setFadeOutMs(tOut);
        customTransientReplacer.setFadeInTension(tInT);
        customTransientReplacer.setFadeOutTension(tOutT);
    }
    if (!in.isExhausted())
    {
        float oIn = in.readFloat(), oOut = in.readFloat(), oInT = in.readFloat(), oOutT = in.readFloat();
        customTonalReplacer.setFadeInMs(oIn);
        customTonalReplacer.setFadeOutMs(oOut);
        customTonalReplacer.setFadeInTension(oInT);
        customTonalReplacer.setFadeOutTension(oOutT);
    }
    if (!in.isExhausted())
    {
        float lIn = in.readFloat(), lOut = in.readFloat(), lInT = in.readFloat(), lOutT = in.readFloat();
        customLayerReplacer.setFadeInMs(lIn);
        customLayerReplacer.setFadeOutMs(lOut);
        customLayerReplacer.setFadeInTension(lInT);
        customLayerReplacer.setFadeOutTension(lOutT);
    }

    // 8. Solo モード復元
    if (!in.isExhausted())
    {
        currentSoloMode.store(in.readInt(), std::memory_order_release);
    }

    // 9. エフェクト処理順の復元
    auto readOrder = [&]() -> std::vector<int>
    {
        std::vector<int> order;
        if (in.isExhausted()) return order;
        int count = in.readInt();
        if (count < 0 || count > 6) return order;
        order.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count && !in.isExhausted(); ++i)
            order.push_back(in.readInt());
        return order;
    };
    if (!in.isExhausted())
    {
        transEffectOrder   = readOrder();
        tonalEffectOrder   = readOrder();
        layerEffectOrder   = readOrder();
        fullMixEffectOrder = readOrder();

        auto restoreChain = [this](TargetRoute route, const std::vector<int>& order)
        {
            for (int idx : order)
            {
                if (idx < 0 || idx >= 6) continue;
                AudioEffect* fx = nullptr;
                if (route == TargetRoute::Transient)     fx = transientPool[idx].get();
                else if (route == TargetRoute::Tonal)    fx = tonalPool[idx].get();
                else if (route == TargetRoute::Layer)    fx = layerPool[idx].get();
                else if (route == TargetRoute::FullMix)  fx = fullMixPool[idx].get();
                if (fx) fx->setActive(true);
            }
            updateRouteOrder(route, order);
        };
        restoreChain(TargetRoute::Transient, transEffectOrder);
        restoreChain(TargetRoute::Tonal,     tonalEffectOrder);
        restoreChain(TargetRoute::Layer,     layerEffectOrder);
        restoreChain(TargetRoute::FullMix,   fullMixEffectOrder);
    }

    // 10. バッファを設定し、分離・合成を再実行
    {
        const juce::ScopedLock sl(lock);
        if (restoredInput.getNumSamples() > 0)
        {
            inputBufferThread.makeCopyOf(restoredInput);
            rawInputBuffer.makeCopyOf(restoredInput);
        }
        if (restoredCustomTrans.getNumSamples() > 0)
        {
            customTransBuffer.makeCopyOf(restoredCustomTrans);
            customTransientReplacer.loadSample(restoredCustomTrans, fileSampleRate);
            customTransientReplacer.setStartOffsetMs(transStartOffsetMs);
            customTransientReplacer.setEndOffsetMs(transEndOffsetMs);
        }
        if (restoredCustomTonal.getNumSamples() > 0)
        {
            customTonalBuffer.makeCopyOf(restoredCustomTonal);
            customTonalReplacer.loadSample(restoredCustomTonal, fileSampleRate);
            customTonalReplacer.setStartOffsetMs(tonalStartOffsetMs);
            customTonalReplacer.setEndOffsetMs(tonalEndOffsetMs);
        }
        if (restoredCustomLayer.getNumSamples() > 0)
        {
            customLayerBuffer.makeCopyOf(restoredCustomLayer);
            customLayerReplacer.loadSample(restoredCustomLayer, fileSampleRate);
            customLayerReplacer.setStartOffsetMs(layerStartOffsetMs);
            customLayerReplacer.setEndOffsetMs(layerEndOffsetMs);
        }
    }

    if (restoredInput.getNumSamples() > 0)
    {
        // バックグラウンドスレッドで分離処理を再実行
        if (isThreadRunning())
        {
            signalThreadShouldExit();
            waitForThreadToExit(2000);
        }
        needsReanalysis.store(false, std::memory_order_release);
        startThread(juce::Thread::Priority::normal);
    }
    else
    {
        updateActiveSampleData();
        offlineMixRenderer.triggerRender();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AnatomyAudioProcessor(); }