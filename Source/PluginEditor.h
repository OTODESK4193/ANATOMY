#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginProcessor.h"
#include "UI/WaveformComponent.h"

class AnatomyAudioProcessorEditor : public juce::AudioProcessorEditor,
    public juce::FileDragAndDropTarget,
    private juce::Timer
{
public:
    AnatomyAudioProcessorEditor(AnatomyAudioProcessor&);
    ~AnatomyAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    void updateButtonToggleStates();

    AnatomyAudioProcessor& audioProcessor;
    juce::AudioFormatManager formatManager;

    // 4面の波形ディスプレイを明確に個別定義
    WaveformComponent waveDndFile;
    WaveformComponent waveProcessorOriginal;
    WaveformComponent waveTransient;
    WaveformComponent waveTonal;

    // 新設：検証用Solo切り替えラジオボタン
    juce::TextButton btnOriginal{ "ORIGINAL (Full Mix)" };
    juce::TextButton btnTransient{ "TRANSIENT SOLO" };
    juce::TextButton btnTonal{ "TONAL SOLO" };

    bool wasProcessing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnatomyAudioProcessorEditor)
};