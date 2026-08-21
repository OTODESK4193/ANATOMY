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
    extern Lane lanes[3];
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

    AudioEffect* getTransientPoolInstance(int idx) const noexcept { return (idx >= 0 && idx < 6) ? transientPool[idx].get() : nullptr; }
    AudioEffect* getTonalPoolInstance(int idx) const noexcept { return (idx >= 0 && idx < 6) ? tonalPool[idx].get() : nullptr; }
    AudioEffect* getFullMixPoolInstance(int idx) const noexcept { return (idx >= 0 && idx < 6) ? fullMixPool[idx].get() : nullptr; }

    const std::vector<int>& getEffectOrder(TargetRoute route) const noexcept
    {
        if (route == TargetRoute::Transient) return transEffectOrder;
        if (route == TargetRoute::Tonal)     return tonalEffectOrder;
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

    void flushPendingExports();
    juce::File createTemporaryWavForExport(int laneIndex);
    void setOffsetsFromUI(int laneIndex, float startMs, float endMs) noexcept;
    void setFadeFromUI(bool isTransient, float inMs, float outMs, float inTension, float outTension) noexcept;
    void getFadeForUI(bool isTransient, float& inMs, float& outMs, float& inTension, float& outTension) const noexcept;

    void setLaneSolo(bool isTransient, bool isSolo);
    bool isLaneSolo(bool isTransient) const noexcept;

    void storeCustomSampleFromUI(bool isTransient, const juce::AudioBuffer<float>& newBuffer, double sr) noexcept;
    void clearCustomSampleFromUI(bool isTransient) noexcept;

    int snapToZeroCrossing(const juce::AudioBuffer<float>& buffer, int targetSample) noexcept;

    juce::AudioProcessorValueTreeState apvts;

    TransientReplacer customTransientReplacer;
    TonalReplacer customTonalReplacer;

    EffectChain transientChain;
    EffectChain tonalChain;
    EffectChain fullMixChain;

    juce::AudioBuffer<float>& getRawInputBufferForUI() noexcept { return rawInputBuffer; }
    double getFileSampleRate() const noexcept { return fileSampleRate; }

    bool isCustomSampleLoaded(bool isTransient) const noexcept
    {
        return isTransient ? (customTransBuffer.getNumSamples() > 0) : (customTonalBuffer.getNumSamples() > 0);
    }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void generateVoiceSample(VoiceState& voice,
        float& outTransL, float& outTransR,
        float& outTonalL, float& outTonalR,
        float clickHold, float clickCurve,
        float transScale, float tonalScale, double hostSampleRate) noexcept;

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

    juce::AudioBuffer<float> rawInputBuffer;
    juce::AudioBuffer<float> inputBufferThread;
    juce::AudioBuffer<float> transBufferThread;
    juce::AudioBuffer<float> tonalBufferThread;

    juce::AudioBuffer<float> customTransBuffer;
    juce::AudioBuffer<float> customTonalBuffer;

    juce::AudioBuffer<float> transBufferUI;
    juce::AudioBuffer<float> tonalBufferUI;

    std::atomic<int> currentSoloMode{ 0 };

    // ⑥ ジッパーノイズ防止: ミックスゲインのサンプル精度スムージング
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedTransGain { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedTonalGain { 1.0f };

    std::unique_ptr<AudioEffect> transientPool[6];
    std::unique_ptr<AudioEffect> tonalPool[6];
    std::unique_ptr<AudioEffect> fullMixPool[6];

    // エフェクト処理順を保持（エディタ再構築時にChipBarを復元するため）
    std::vector<int> transEffectOrder;
    std::vector<int> tonalEffectOrder;
    std::vector<int> fullMixEffectOrder;

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

        // APVTS パラメータの std::atomic<float>* キャッシュ
        std::atomic<float>* satDrive = nullptr;
        std::atomic<float>* satMix   = nullptr;
        std::atomic<float>* satAsym  = nullptr;
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

        std::atomic<float>* limCeil = nullptr;
        std::atomic<float>* limMix  = nullptr;
    };

    LaneParamCache cachedLanes[3]; // 0=trans, 1=tonal, 2=full
    void initParamCache();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnatomyAudioProcessor)
};