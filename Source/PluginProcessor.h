#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <memory>
#include <atomic>
#include <vector>
#include "DSP/HpssSeparator.h"
#include "DSP/SharedSampleData.h"
#include "DSP/VoiceState.h"
#include "DSP/TransientReplacer.h"
#include "DSP/TonalReplacer.h"
#include "DSP/Effects/AudioEffect.h"
#include "DSP/Effects/EffectChain.h"

class AnatomyAudioProcessor final : public juce::AudioProcessor,
    public juce::Thread,
    public juce::AudioProcessorValueTreeState::Listener
{
public:
    AnatomyAudioProcessor();
    ~AnatomyAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    void parameterChanged(const juce::String& parameterID, float newValue) override;

    void startSeparation(const juce::AudioBuffer<float>& inputAudio, double sourceSampleRate);
    void run() override;

    void handleAsyncReanalysis();

    bool isCurrentlyProcessing() const { return isThreadRunning() || needsReanalysis.load(std::memory_order_acquire); }
    float getHpssProgress() const { return separator.getProgress(); }

    int getSoloMode() const { return currentSoloMode; }
    void setSoloMode(int mode);

    void getCallbackBuffersSecure(juce::AudioBuffer<float>& transDest, juce::AudioBuffer<float>& tonalDest)
    {
        const juce::ScopedLock sl(lock);
        transDest.makeCopyOf(transBufferUI);
        tonalDest.makeCopyOf(tonalBufferUI);
    }

    /**
     * 💥【構造案A-2＆カプセル化準拠】
     * UI側（Rack）から、現在そのレーンに配置されているエフェクトの種類インデックス順（配列）のみを非同期受信するインターフェース。
     * インデックス定義: 0=Saturation, 1=BitCrusher, 2=NoiseGenerator, 3=OTT_Multiband, 4=Limiter
     */
    void updateRouteOrder(TargetRoute route, const std::vector<int>& activeEffectIndices);

    juce::AudioProcessorValueTreeState apvts;

    TransientReplacer customTransientReplacer;
    TonalReplacer customTonalReplacer;

    EffectChain transientChain;
    EffectChain tonalChain;
    EffectChain fullMixChain;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void generateVoiceSample(VoiceState& voice,
        float& outTransL, float& outTransR,
        float& outTonalL, float& outTonalR,
        float clickHold, float clickCurve,
        float transScale, float tonalScale) noexcept;

    void updateActiveSampleData();
    void cleanUpGarbageBin();
    void synchronizePoolParameters() noexcept;

    HpssSeparator separator{ 2048 };
    juce::CriticalSection lock;

    std::atomic<bool> needsReanalysis{ false };
    std::atomic<bool> isAnalysisFinished{ false };

    double fileSampleRate = 44100.0;
    double currentSampleRate = 44100.0;
    float releaseFactor = 0.95f;

    std::atomic<SharedSampleData*> masterSampleData{ nullptr };

    VoiceState activeVoice;
    static constexpr int maxReleasingVoices = 4;
    VoiceState releasingVoices[maxReleasingVoices];

    bool activeIsMuting = false;
    float activeMuteGain = 1.0f;
    bool releasingIsMuting[maxReleasingVoices];
    float releasingMuteGain[maxReleasingVoices];

    std::vector<SharedSampleData*> garbageBin;
    std::vector<EffectChainSnapshot*> fxGarbageBin;

    juce::AudioBuffer<float> transientBlockBuffer;
    juce::AudioBuffer<float> tonalBlockBuffer;

    juce::AudioBuffer<float> rawInputBuffer;
    juce::AudioBuffer<float> inputBufferThread;
    juce::AudioBuffer<float> transBufferThread;
    juce::AudioBuffer<float> tonalBufferThread;

    juce::AudioBuffer<float> transBufferUI;
    juce::AudioBuffer<float> tonalBufferUI;

    int currentSoloMode = 0;

    // 💥【A案：完全独立マルチインスタンス・プール】
    // 3レーンに対して全具象エフェクトクラスを独立配置し、内部過去ログ（状態変数）を完全に隔離。
    std::unique_ptr<AudioEffect> transientPool[5];
    std::unique_ptr<AudioEffect> tonalPool[5];
    std::unique_ptr<AudioEffect> fullMixPool[5];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnatomyAudioProcessor)
};