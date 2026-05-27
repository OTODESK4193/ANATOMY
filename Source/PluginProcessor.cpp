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
    activeIsMuting = false;
    activeMuteGain = 1.0f;

    for (int i = 0; i < maxReleasingVoices; ++i)
    {
        releasingVoices[i].reset();
        releasingIsMuting[i] = false;
        releasingMuteGain[i] = 1.0f;
    }

    apvts.addParameterListener("clickLength", this);
    apvts.addParameterListener("clickCurve", this);
}

AnatomyAudioProcessor::~AnatomyAudioProcessor()
{
    apvts.removeParameterListener("clickLength", this);
    apvts.removeParameterListener("clickCurve", this);

    signalThreadShouldExit();
    stopThread(4000);

    // 強制全解放（シャットダウン時は安全チェックをスキップして全消去）
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

    // ホストの動的変更に対応する安全マージン最大確保
    const int safetyBufferSize = std::max(4096, samplesPerBlock * 2);
    transientBlockBuffer.setSize(2, safetyBufferSize, false, false, true);
    tonalBlockBuffer.setSize(2, safetyBufferSize, false, false, true);

    transientChain.prepare(sampleRate, safetyBufferSize);
    tonalChain.prepare(sampleRate, safetyBufferSize);
    fullMixChain.prepare(sampleRate, safetyBufferSize);
}

void AnatomyAudioProcessor::releaseResources() {}

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

    transientBlockBuffer.clear();
    tonalBlockBuffer.clear();

    // 核心制約3：ブロックの原点において最新コンテナへの不変参照ポインタを取得
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

    // --- TRANSIENT (CLICK) PROCESS ---
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

    // --- TONAL (SUSTAIN) PROCESS ---
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
    // メッセージスレッド側での定期クリーンアップ
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

    // バックグラウンドスレッドが動いておらず、かつゴミ箱のクリーンアップ要求があれば定期処理を実行
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

    // アトミックポインタの交換
    SharedSampleData* oldData = masterSampleData.exchange(newData, std::memory_order_acq_rel);
    if (oldData != nullptr)
    {
        // 💥即deleteを完全バイパスし、ゴミ箱へ退避
        garbageBin.push_back(oldData);
    }
}

void AnatomyAudioProcessor::updateTransientChain(std::vector<std::unique_ptr<AudioEffect>>&& newEffects)
{
    transientChain.updateChain(std::move(newEffects), fxGarbageBin);
}

void AnatomyAudioProcessor::updateTonalChain(std::vector<std::unique_ptr<AudioEffect>>&& newEffects)
{
    tonalChain.updateChain(std::move(newEffects), fxGarbageBin);
}

void AnatomyAudioProcessor::updateFullMixChain(std::vector<std::unique_ptr<AudioEffect>>&& newEffects)
{
    fullMixChain.updateChain(std::move(newEffects), fxGarbageBin);
}

void AnatomyAudioProcessor::cleanUpGarbageBin()
{
    // ==============================================================================
    // 💥【核心修正】ボイス参照追跡型ガベージコレクション
    // メッセージスレッド側で実行され、オーディオスレッドの全ボイスの生存状況を直列監視します。
    // ==============================================================================
    auto it = garbageBin.begin();
    while (it != garbageBin.end())
    {
        SharedSampleData* oldData = *it;
        bool isStillReferencedByVoice = false;

        // 1. 現在発音中のメインボイスのチェック
        if (activeVoice.isActive && activeVoice.sampleData == oldData)
        {
            isStillReferencedByVoice = true;
        }

        // 2. クロスフェード退避中のリリーススロット全4ボイスのチェック
        for (int i = 0; i < maxReleasingVoices; ++i)
        {
            if (releasingVoices[i].isActive && releasingVoices[i].sampleData == oldData)
            {
                isStillReferencedByVoice = true;
            }
        }

        // どのボイスからも完全に手が離れている場合のみ安全に物理メモリを解体
        if (!isStillReferencedByVoice)
        {
            if (oldData != nullptr)
            {
                delete oldData;
            }
            it = garbageBin.erase(it); // コンテナから削除してイテレータを進める
        }
        else
        {
            ++it; // まだオーディオスレッドで発音中のため、今回は解体をスキップして次回に持ち越し
        }
    }

    // エフェクト配列のスナップショット回収
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