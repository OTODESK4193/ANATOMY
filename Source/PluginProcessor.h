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
#include "DSP/BeforeAfterBypasser.h"
#include "DSP/OfflineMixRenderer.h"

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
        if (customTransBuffer.getNumSamples() > 0) transDest.makeCopyOf(customTransBuffer);
        else                                       transDest.makeCopyOf(transBufferUI);

        if (customTonalBuffer.getNumSamples() > 0) tonalDest.makeCopyOf(customTonalBuffer);
        else                                       tonalDest.makeCopyOf(tonalBufferUI);
    }

    void updateRouteOrder(TargetRoute route, const std::vector<int>& activeEffectIndices);

    AudioEffect* getTransientPoolInstance(int idx) const noexcept { return (idx >= 0 && idx < 5) ? transientPool[idx].get() : nullptr; }
    AudioEffect* getTonalPoolInstance(int idx) const noexcept { return (idx >= 0 && idx < 5) ? tonalPool[idx].get() : nullptr; }
    AudioEffect* getFullMixPoolInstance(int idx) const noexcept { return (idx >= 0 && idx < 5) ? fullMixPool[idx].get() : nullptr; }

    BeforeAfterBypasser beforeAfterBypasser;
    OfflineMixRenderer offlineMixRenderer;

    float transStartOffsetMs = 0.0f;
    float transEndOffsetMs = 0.0f;
    float tonalStartOffsetMs = 0.0f;
    float tonalEndOffsetMs = 0.0f;

    juce::File createTemporaryWavForExport(int laneIndex);
    void setOffsetsFromUI(bool isTransient, float startMs, float endMs) noexcept;

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
        float transScale, float tonalScale) noexcept;

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

    int currentSoloMode = 0;

    std::unique_ptr<AudioEffect> transientPool[5];
    std::unique_ptr<AudioEffect> tonalPool[5];
    std::unique_ptr<AudioEffect> fullMixPool[5];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnatomyAudioProcessor)
};