#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include "DSP/HpssSeparator.h"
#include "DSP/ThreadSafeSamplerSound.h"
#include "DSP/ThreadSafeSamplerVoice.h"

class AnatomyAudioProcessor : public juce::AudioProcessor, public juce::Thread
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

    // バックグラウンド非同期スライススレッドの制御
    void startSeparation(const juce::AudioBuffer<float>& inputAudio);
    void run() override;

    bool isCurrentlyProcessing() const { return isThreadRunning(); }
    float getHpssProgress() const { return separator.getProgress(); }

    int getSoloMode() const { return currentSoloMode; }
    void setSoloMode(int mode);

    // UIバッファ読み出し用セーフティインターフェース
    const juce::AudioBuffer<float>& getOriginalBuffer() const { return originalBufferUI; }
    const juce::AudioBuffer<float>& getTransientBuffer() const { return transBufferUI; }
    const juce::AudioBuffer<float>& getTonalBuffer() const { return tonalBufferUI; }

private:
    void updateSynthSound();

    HpssSeparator separator{ 2048 };
    juce::Synthesiser synth;
    ThreadSafeSamplerSound* samplerSound = nullptr;

    juce::CriticalSection lock;

    // スレッド間通信専用バッファ
    juce::AudioBuffer<float> inputBufferThread;
    juce::AudioBuffer<float> originalBufferThread;
    juce::AudioBuffer<float> transBufferThread;
    juce::AudioBuffer<float> tonalBufferThread;

    // UI描画安全確保用スタティックバッファ
    juce::AudioBuffer<float> originalBufferUI;
    juce::AudioBuffer<float> transBufferUI;
    juce::AudioBuffer<float> tonalBufferUI;

    int currentSoloMode = 0; // 0: Full Mix, 1: Transient Solo, 2: Sustain Solo

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnatomyAudioProcessor)
};