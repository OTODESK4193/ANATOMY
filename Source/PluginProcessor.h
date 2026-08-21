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
#include "DSP/Effects/ADAA_Saturation.h"
#include "DSP/Effects/BitCrusher.h"
#include "DSP/Effects/NoiseGenerator.h"
#include "DSP/Effects/OTT_Multiband.h"
#include "DSP/Effects/GlueCompressor.h"
#include "DSP/Effects/Limiter.h"
#include "DSP/Effects/ADAA_Saturation.h"
#include "DSP/Effects/TransientShaper.h"
#include "DSP/BeforeAfterBypasser.h"
#include "DSP/OfflineMixRenderer.h"

namespace ExportRecordingCore
{
    enum class State { Idle, Request, Recording, PendingWrite, Ready };
    struct Lane {
        std::atomic<State> state{ State::Idle };
        juce::AudioBuffer<float> buffer;
        int writePos = 0;
        int sampleCounter = 0;
        int noteOffSample = 0;
        bool isNoteOffTriggered = false;
        juce::File file;
    };
    extern Lane lanes[4];
}

class AnatomyAudioProcessor final : public juce::AudioProcessor,
    public juce::Thread,
    public juce::AudioProcessorValueTreeState::Listener
{
public:
    AnatomyAudioProcessor();
    ~AnatomyAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
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

    int getSoloMode() const { return currentSoloMode.load(std::memory_order_acquire); }
    void setSoloMode(int mode);

    void getCallbackBuffersSecure(juce::AudioBuffer<float>& transDest, juce::AudioBuffer<float>& tonalDest)
    {
        const juce::ScopedLock sl(lock);
        if (customTransBuffer.getNumSamples() > 0) transDest.makeCopyOf(customTransBuffer);
        else                                       transDest.makeCopyOf(transBufferUI);

        if (customTonalBuffer.getNumSamples() > 0) tonalDest.makeCopyOf(customTonalBuffer);
        else                                       tonalDest.makeCopyOf(tonalBufferUI);
    }

    void updateRouteOrder(TargetRoute route, const std::vector<int>& activeEffectIndices);

    AudioEffect* getTransientPoolInstance(int idx) const noexcept { return (idx >= 0 && idx < 7) ? transientPool[idx].get() : nullptr; }
    AudioEffect* getTonalPoolInstance(int idx) const noexcept { return (idx >= 0 && idx < 7) ? tonalPool[idx].get() : nullptr; }
    AudioEffect* getFullMixPoolInstance(int idx) const noexcept { return (idx >= 0 && idx < 7) ? fullMixPool[idx].get() : nullptr; }
    AudioEffect* getLayerPoolInstance(int idx) const noexcept { return (idx >= 0 && idx < 7) ? layerPool[idx].get() : nullptr; }

    const std::vector<int>& getEffectOrder(TargetRoute route) const noexcept
    {
        if (route == TargetRoute::Transient) return transEffectOrder;
        if (route == TargetRoute::Tonal)     return tonalEffectOrder;
        if (route == TargetRoute::Layer)     return layerEffectOrder;
        return fullMixEffectOrder;
    }

    BeforeAfterBypasser beforeAfterBypasser;
    OfflineMixRenderer offlineMixRenderer;

    float fullMixStartOffsetMs = 0.0f;
    float fullMixEndOffsetMs = 0.0f;
    float transStartOffsetMs = 0.0f;
    float transEndOffsetMs = 0.0f;
    float tonalStartOffsetMs = 0.0f;
    float tonalEndOffsetMs = 0.0f;
    float layerStartOffsetMs = 0.0f;
    float layerEndOffsetMs = 0.0f;

    void flushPendingExports();
    juce::File createTemporaryWavForExport(int laneIndex);
    void applyEffectsOffline(juce::AudioBuffer<float>& buffer, TargetRoute route, double sr);
    void setOffsetsFromUI(int laneIndex, float startMs, float endMs) noexcept;
    void setFadeFromUI(int laneIndex, float inMs, float outMs, float inTension, float outTension) noexcept;
    void getFadeForUI(int laneIndex, float& inMs, float& outMs, float& inTension, float& outTension) const noexcept;

    void setLaneSolo(int laneIndex, bool isSolo);
    bool isLaneSolo(int laneIndex) const noexcept;

    void storeCustomSampleFromUI(int laneIndex, const juce::AudioBuffer<float>& newBuffer, double sr) noexcept;
    void clearCustomSampleFromUI(int laneIndex) noexcept;

    int snapToZeroCrossing(const juce::AudioBuffer<float>& buffer, int targetSample) noexcept;

    juce::AudioProcessorValueTreeState apvts;

    TransientReplacer customTransientReplacer;
    TonalReplacer customTonalReplacer;
    TonalReplacer customLayerReplacer; // Reusing TonalReplacer logic for Layer

    EffectChain transientChain;
    EffectChain tonalChain;
    EffectChain fullMixChain;
    EffectChain layerChain;

    juce::AudioBuffer<float>& getRawInputBufferForUI() noexcept { return rawInputBuffer; }
    double getFileSampleRate() const noexcept { return fileSampleRate; }

    bool isCustomSampleLoaded(int laneIndex) const noexcept
    {
        if (laneIndex == 1) return customTransBuffer.getNumSamples() > 0;
        if (laneIndex == 2) return customTonalBuffer.getNumSamples() > 0;
        if (laneIndex == 3) return customLayerBuffer.getNumSamples() > 0;
        return false;
    }

    void setCustomSampleName(int laneIndex, const juce::String& name) noexcept
    {
        if (laneIndex >= 0 && laneIndex < 4)
            customSampleNames[laneIndex] = name;
    }

    juce::String getCustomSampleName(int laneIndex) const noexcept
    {
        return (laneIndex >= 0 && laneIndex < 4) ? customSampleNames[laneIndex] : juce::String();
    }

    juce::String customSampleNames[4] = { {}, {}, {}, {} };

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void generateVoiceSample(VoiceState& voice, float& outTransL, float& outTransR, float& outTonalL, float& outTonalR, float& outLayerL, float& outLayerR, float clickHold, float clickCurve, float transScale, float tonalScale, double hostSampleRate) noexcept;

    void updateActiveSampleData();
    void cleanUpGarbageBin();
    void synchronizePoolParameters() noexcept;

    friend class OfflineMixRenderer;

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
    juce::AudioBuffer<float> layerBlockBuffer;

    juce::AudioBuffer<float> rawInputBuffer;
    juce::AudioBuffer<float> inputBufferThread;
    juce::AudioBuffer<float> transBufferThread;
    juce::AudioBuffer<float> tonalBufferThread;

    juce::AudioBuffer<float> customTransBuffer;
    juce::AudioBuffer<float> customTonalBuffer;
    juce::AudioBuffer<float> customLayerBuffer;

    juce::AudioBuffer<float> transBufferUI;
    juce::AudioBuffer<float> tonalBufferUI;

    std::atomic<int> currentSoloMode{ 0 };

    // ⑥ ジッパーノイズ防止: ミックスゲインのサンプル精度スムージング
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedTransGain { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedTonalGain { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedLayerGain { 1.0f };

    std::unique_ptr<AudioEffect> transientPool[7];
    std::unique_ptr<AudioEffect> tonalPool[7];
    std::unique_ptr<AudioEffect> fullMixPool[7];
    std::unique_ptr<AudioEffect> layerPool[7];

    // エフェクト処理順を保持（エディタ再構築時にChipBarを復元するため）
    std::vector<int> transEffectOrder;
    std::vector<int> tonalEffectOrder;
    std::vector<int> fullMixEffectOrder;
    std::vector<int> layerEffectOrder;

    // ③ パラメータポインタキャッシュ — processBlock毎のハッシュ検索とdynamic_castを排除
    struct LaneParamCache
    {
        // エフェクト型ポインタ（static_cast済み、所有権なし）
        ADAA_Saturation* sat = nullptr;
        BitCrusher*      bc  = nullptr;
        NoiseGenerator*  ns  = nullptr;
        OTT_Multiband*   ott = nullptr;
        GlueCompressor*  glue = nullptr;
        Limiter*         lim = nullptr;
        TransientShaper* ts  = nullptr;

        // APVTS パラメータの std::atomic<float>* キャッシュ
        std::atomic<float>* satDrive = nullptr;
        std::atomic<float>* satMix   = nullptr;
        std::atomic<float>* satType  = nullptr;
        std::atomic<float>* satTrim  = nullptr;
        std::atomic<float>* satPre   = nullptr;

        std::atomic<float>* bcBits   = nullptr;
        std::atomic<float>* bcDown   = nullptr;
        std::atomic<float>* bcMix    = nullptr;
        std::atomic<float>* bcJitter = nullptr;

        std::atomic<float>* nsDecay  = nullptr;
        std::atomic<float>* nsMix    = nullptr;
        std::atomic<float>* nsType   = nullptr;
        std::atomic<float>* nsGain   = nullptr;
        std::atomic<float>* nsAttack = nullptr;
        std::atomic<float>* nsBpFreq = nullptr;

        std::atomic<float>* ottDepth      = nullptr;
        std::atomic<float>* ottTime       = nullptr;
        std::atomic<float>* ottLowMidXOver = nullptr;
        std::atomic<float>* ottMidHighXOver = nullptr;
        std::atomic<float>* ottGateFloor  = nullptr;
        std::atomic<float>* ottBandUp[3]   = {};
        std::atomic<float>* ottBandDown[3] = {};
        std::atomic<float>* ottBandGain[3] = {};

        std::atomic<float>* glueDepth = nullptr;
        std::atomic<float>* glueThr   = nullptr;
        std::atomic<float>* glueRatio = nullptr;
        std::atomic<float>* glueAtk   = nullptr;
        std::atomic<float>* glueRel   = nullptr;
        std::atomic<float>* glueMkp   = nullptr;

        std::atomic<float>* limGain = nullptr;
        std::atomic<float>* limCeil = nullptr;
        std::atomic<float>* limMix  = nullptr;
        std::atomic<float>* limMode = nullptr;
        
        std::atomic<float>* tsAttack  = nullptr;
        std::atomic<float>* tsSustain = nullptr;
        std::atomic<float>* tsMix     = nullptr;
    };

    LaneParamCache cachedLanes[4]; // 0=trans, 1=tonal, 2=full, 3=layer
    void initParamCache();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnatomyAudioProcessor)
};