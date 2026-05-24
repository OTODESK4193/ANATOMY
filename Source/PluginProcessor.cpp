#include "PluginProcessor.h"
#include "PluginEditor.h"

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
        oldData->decReferenceCount();
}

juce::AudioProcessorValueTreeState::ParameterLayout AnatomyAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("clickLength", 1), "Click Hold (ms)", 0.0f, 50.0f, 2.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("clickCurve", 1), "Sustain Fade-In (ms)", 1.0f, 100.0f, 15.0f));
    return { params.begin(), params.end() };
}

void AnatomyAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;
    separator.prepare(sampleRate);

    // 1.5msで-60dB (0.001) まで綺麗に軟着陸させる指数関数減衰係数の算出
    float rampSamples = static_cast<float>(std::max(1.0, 0.0015 * sampleRate));
    releaseFactor = std::exp(std::log(0.001f) / rampSamples);
}

void AnatomyAudioProcessor::releaseResources() {}

void AnatomyAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    // 【クラッシュ根絶：Immutableバッファロード】ブロック先頭で最新コンテナをローカルへ1回だけ安全ロード
    SharedSampleData::Ptr currentDataSnapshot = nullptr;
    SharedSampleData* rawPtr = masterSampleData.load(std::memory_order_acquire);
    if (rawPtr != nullptr)
    {
        currentDataSnapshot = rawPtr;
    }

    // --- 完全同期決定論的 MIDI パース処理 ---
    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();
        const int samplePos = metadata.samplePosition;

        if (msg.isNoteOn())
        {
            // 連打対策の核心：既存のactiveボイスを即座に空きリリーススロットへスワップ退避
            if (activeVoice.isActive)
            {
                int slotToUse = 0;
                for (int i = 0; i < maxReleasingVoices; ++i)
                {
                    if (!releasingVoices[i].isActive) { slotToUse = i; break; }
                }
                releasingVoices[slotToUse] = activeVoice;
                releasingVoices[slotToUse].isReleasing = true;
            }

            // 新規ノートの確定初期化
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
            }
        }
        else if (msg.isNoteOff())
        {
            if (activeVoice.isActive && activeVoice.currentMidiNote == msg.getNoteNumber())
            {
                activeVoice.isReleasing = true; // Simpler Gateモード挙動：離された瞬間に即超高速指数リリース開始
            }
        }
    }

    // --- アロケーションフリー・サンプルレンダリングループ ---
    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float mixedL = 0.0f;
        float mixedR = 0.0f;

        // 1. メインアクティブボイスのレンダリング
        if (activeVoice.isActive && activeVoice.sampleData != nullptr)
        {
            float vL = 0.0f, vR = 0.0f;
            generateVoiceSample(activeVoice, vL, vR);
            mixedL += vL;
            mixedR += vR;

            if (activeVoice.isReleasing)
            {
                activeVoice.releaseGain *= releaseFactor; // 指数関数フェード
                if (activeVoice.releaseGain <= 0.001f) activeVoice.reset();
            }
        }

        // 2. 連打によって退避された過去の残響のミキシング ＋ 超高速消音
        for (int i = 0; i < maxReleasingVoices; ++i)
        {
            if (releasingVoices[i].isActive && releasingVoices[i].sampleData != nullptr)
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
    const auto& click = voice.sampleData->getClickBuffer();
    const auto& sustain = voice.sampleData->getSustainBuffer();
    const int clickLen = click.getNumSamples();
    const int sustainLen = sustain.getNumSamples();

    float cVal = 0.0f;
    float sVal = 0.0f;

    // 高精度線形補間
    double cPos = voice.clickReadIndex;
    int cIdx = static_cast<int>(cPos);
    if (cIdx < clickLen)
    {
        float frac = static_cast<float>(cPos - cIdx);
        float s0 = click.getReadPointer(0)[cIdx];
        float s1 = (cIdx + 1 < clickLen) ? click.getReadPointer(0)[cIdx + 1] : 0.0f;
        cVal = s0 + frac * (s1 - s0);
        voice.clickReadIndex += voice.pitchRatio;
    }

    double sPos = voice.sustainReadIndex;
    int sIdx = static_cast<int>(sPos);
    if (sIdx < sustainLen)
    {
        float frac = static_cast<float>(sPos - sIdx);
        float s0 = sustain.getReadPointer(0)[sIdx];
        float s1 = (sIdx + 1 < sustainLen) ? sustain.getReadPointer(0)[sIdx + 1] : 0.0f;
        sVal = s0 + frac * (s1 - s0);
        voice.sustainReadIndex += voice.pitchRatio;
    }

    float finalSample = (cVal + sVal) * voice.triggerVelocity * voice.releaseGain;
    outL = finalSample;
    outR = finalSample;

    if (static_cast<int>(voice.clickReadIndex) >= clickLen && static_cast<int>(voice.sustainReadIndex) >= sustainLen)
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

    // 【リアルタイム安全長寿命管理】
    SharedSampleData* newData = new SharedSampleData(std::move(activeClick), std::move(activeSustain), fileSampleRate);
    newData->incReferenceCount();

    SharedSampleData* oldData = masterSampleData.exchange(newData, std::memory_order_acq_rel);
    if (oldData != nullptr)
    {
        oldData->decReferenceCount(); // 旧データを安全破棄
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