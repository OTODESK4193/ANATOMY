#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginProcessor.h"
#include "UI/WaveformComponent.h"

class AnatomyAudioProcessorEditor : public juce::AudioProcessorEditor,
    public juce::FileDragAndDropTarget,
    public juce::Timer
{
public:
    AnatomyAudioProcessorEditor(AnatomyAudioProcessor&);
    ~AnatomyAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    void timerCallback() override;

private:
    void updateButtonToggleStates();

    AnatomyAudioProcessor& audioProcessor;

    juce::AudioFormatManager formatManager;
    bool wasProcessing = false;

    // 2段目の監視バッファを完全抹消し、原音・過渡・持続の3階層へシェイプアップ
    WaveformComponent waveDndFile;
    WaveformComponent waveTransient;
    WaveformComponent waveTonal;

    juce::TextButton btnOriginal{ "Full Mix" };
    juce::TextButton btnTransient{ "Transient Solo" };
    juce::TextButton btnTonal{ "Sustain Solo" };

    // 新次世代操作ノブ（ロータリーコントロール）
    juce::Slider sliderSensitivity;
    juce::Slider sliderClickLength;
    juce::Slider sliderLookAhead;

    juce::Label lblSensitivity;
    juce::Label lblClickLength;
    juce::Label lblLookAhead;

    // 双方向リアルタイム通信用パラメータアタッチメント
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachSensitivity;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachClickLength;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachLookAhead;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnatomyAudioProcessorEditor)
};