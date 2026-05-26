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

    // メッセージスレッド側での遅延ゴミ箱の完全解放
    cleanUpGarbageBin();

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

    // 核心制約3：ブロックの原点において最新コンテナへの不変(Immutable)参照ポインタを取得
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

                // 核心制約4：連打（再トリガー）時は1.5msランプの高速固定ミュート状態へスワップ退避
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
                customTonalReplacer.reset(); // 【接着剤】トリガーの絶対瞬間におけるグラニュラー位相強制アライメント
            }
        }
        else if (msg.isNoteOff())
        {
            if (activeVoice.isActive && activeVoice.currentMidiNote == msg.getNoteNumber())
            {
                // 通常のNote Off時は、ユーザー設定のリリースタイム(ms)による豊かな減衰を開始
                activeVoice.isReleasing = true;
            }
        }
    }

    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    // 核心制約2：processBlock最先頭でのパラメータ一括アトミックロード（サンプルループ内負荷ゼロ化）
    float clickHold = apvts.getRawParameterValue("clickLength")->load();
    float clickCurve = apvts.getRawParameterValue("clickCurve")->load();
    float transPitchVal = apvts.getRawParameterValue("transPitch")->load();
    float tonalPitchVal = apvts.getRawParameterValue("tonalPitch")->load();
    float relMs = apvts.getRawParameterValue("sustainRelease")->load();

    float transScale = std::pow(2.0f, transPitchVal / 12.0f);
    float tonalScale = std::pow(2.0f, tonalPitchVal / 12.0f);

    float rampSamples = static_cast<float>((relMs / 1000.0f) * currentSampleRate);
    float dynamicReleaseFactor = std::exp(std::log(0.001f) / std::max(1.0f, rampSamples));

    // 核心制約4：1.5ms固定ミュート用の減衰ステップ係数
    float muteRampSamples = static_cast<float>(0.0015 * currentSampleRate);
    float muteFactor = std::exp(std::log(0.001f) / std::max(1.0f, muteRampSamples));

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float mixedL = 0.0f;
        float mixedR = 0.0f;

        if (activeVoice.isActive)
        {
            float vL = 0.0f, vR = 0.0f;
            generateVoiceSample(activeVoice, vL, vR, clickHold, clickCurve, transScale, tonalScale);

            mixedL += vL * activeMuteGain;
            mixedR += vR * activeMuteGain;

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
                float vL = 0.0f, vR = 0.0f;
                generateVoiceSample(releasingVoices[i], vL, vR, clickHold, clickCurve, transScale, tonalScale);

                mixedL += vL * releasingMuteGain[i];
                mixedR += vR * releasingMuteGain[i];

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

        outL[sample] += mixedL;
        if (outR != nullptr) outR[sample] += mixedR;
    }
}

void AnatomyAudioProcessor::generateVoiceSample(VoiceState& voice, float& outL, float& outR,
    float clickHold, float clickCurve,
    float transScale, float tonalScale) noexcept
{
    outL = 0.0f;
    outR = 0.0f;

    if (voice.sampleData == nullptr) return;

    const auto& click = voice.sampleData->getClickBuffer();
    const auto& sustain = voice.sampleData->getSustainBuffer();
    const int clickLen = click.getNumSamples();
    const int sustainLen = sustain.getNumSamples();

    float shiftedClick = 0.0f;
    float shiftedSustain = 0.0f;

    int cIdx = static_cast<int>(voice.clickReadIndex);
    int sIdx = static_cast<int>(voice.sustainReadIndex);

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

    float finalSample = (shiftedClick + shiftedSustain) * voice.triggerVelocity * voice.releaseGain;
    finalSample *= 0.63f; // ステレオ音量相殺

    outL = finalSample;
    outR = finalSample;

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
        bool clickFinished = (static_cast<int>(voice.clickReadIndex) >= clickLen);
        bool sustainFinished = (static_cast<int>(voice.sustainReadIndex) >= sustainLen);

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
    // 核心制約3：次回のファイルドロップ（新規解析開始）の瞬間に、メッセージスレッド側で過去の不要バッファを安全に回収して全破棄
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
    // HPSSバックグラウンドスレッドが解析完了した合図をメッセージスレッド側で安全に受信
    if (isAnalysisFinished.exchange(false, std::memory_order_acq_rel))
    {
        updateActiveSampleData();
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

    // 解析完了フラグを立てて、メッセージスレッド側での安全な直列データ交換を予約
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
    // 核心制約3：パラメータ変更・モード切替・再解析時にメッセージスレッド側でゴミ箱の古いバッファを安全に全破棄
    cleanUpGarbageBin();

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

    // ロックフリー生ポインタ交換を実行
    SharedSampleData* oldData = masterSampleData.exchange(newData, std::memory_order_acq_rel);
    if (oldData != nullptr)
    {
        // 瞬時deleteを完全バイパスし、安全なメッセージスレッド遅延回収コンテナへ退避
        garbageBin.push_back(oldData);
    }
}

void AnatomyAudioProcessor::cleanUpGarbageBin()
{
    // メッセージスレッド（非オーディオスレッド）側で呼び出し
    for (auto* oldData : garbageBin)
    {
        if (oldData != nullptr)
            delete oldData;
    }
    garbageBin.clear();
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