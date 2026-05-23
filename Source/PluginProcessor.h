#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <memory>
#include <atomic>
#include "DSP/HpssSeparator.h"
#include "DSP/AnatomySound.h"
#include "DSP/AnatomyVoice.h"

class AnatomyAudioProcessor : public juce::AudioProcessor,
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

    // 仕様修正：ファイル本来のサンプリングレートを第2引数で受け取るように拡張
    void startSeparation(const juce::AudioBuffer<float>& inputAudio, double sourceSampleRate);
    void run() override;

    void handleAsyncReanalysis();

    bool isCurrentlyProcessing() const { return isThreadRunning() || needsReanalysis.load(); }
    float getHpssProgress() const { return separator.getProgress(); }

    int getSoloMode() const { return currentSoloMode; }
    void setSoloMode(int mode);

    // 【重要】GUIスレッドからの不意のアクセス衝突を100%遮断する安全なディープコピー関数
    void getCallbackBuffersSecure(juce::AudioBuffer<float>& transDest, juce::AudioBuffer<float>& tonalDest)
    {
        const juce::ScopedLock sl(lock);
        transDest.makeCopyOf(transBufferUI);
        tonalDest.makeCopyOf(tonalBufferUI);
    }

    juce::AudioProcessorValueTreeState apvts;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateSynthSound();

    HpssSeparator separator{ 2048 };
    juce::Synthesiser synth;
    AnatomySound* samplerSound = nullptr;

    juce::CriticalSection lock;

    std::atomic<bool> needsReanalysis{ false };
    double fileSampleRate = 44100.0; // ファイル固有のサンプリングレート保持用

    juce::AudioBuffer<float> rawInputBuffer;
    juce::AudioBuffer<float> inputBufferThread;
    juce::AudioBuffer<float> transBufferThread;
    juce::AudioBuffer<float> tonalBufferThread;

    juce::AudioBuffer<float> transBufferUI;
    juce::AudioBuffer<float> tonalBufferUI;

    int currentSoloMode = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnatomyAudioProcessor)
};