#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/HpssSeparator.h"
#include "DSP/AnatomyVoice.h"

class AnatomyAudioProcessor : public juce::AudioProcessor, private juce::Thread
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

    // 非同期分離処理のエントリポイント
    void startSeparation(const juce::AudioBuffer<float>& inputAudio);

    // スレッド状態と進捗をEditorへ公開するゲッター
    float getHpssProgress() const { return separator.getProgress(); }
    bool isCurrentlyProcessing() const { return isThreadRunning(); }

    // 各波形コンポーネントバインド用の永続ゲッター（原音・Transient・Tonalの3面個別化）
    const juce::AudioBuffer<float>& getOriginalBuffer() const { return originalBufferThread; }
    const juce::AudioBuffer<float>& getTransientBuffer() const { return transBufferThread; }
    const juce::AudioBuffer<float>& getTonalBuffer() const { return tonalBufferThread; }

    // 新設：Solo状態の制御メソッドとゲッター (0: Original, 1: Transient, 2: Tonal)
    void setSoloMode(int mode);
    int getSoloMode() const { return currentSoloMode; }

    // 内部状態に合わせてシンセの音声ソースを安全に組み替えるメソッド
    void updateSynthSound();

private:
    void run() override;

    HpssSeparator separator{ 2048 };
    juce::Synthesiser synth;

    // 現在のSolo選択状態
    int currentSoloMode = 0;

    // スレッド間で安全にコピー・保持するためのバッファ群
    juce::AudioBuffer<float> inputBufferThread;
    juce::AudioBuffer<float> originalBufferThread;
    juce::AudioBuffer<float> transBufferThread;
    juce::AudioBuffer<float> tonalBufferThread;

    juce::CriticalSection lock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnatomyAudioProcessor)
};