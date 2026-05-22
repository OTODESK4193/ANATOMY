#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginProcessor.h"
#include "UI/WaveformComponent.h"

class AnatomyAudioProcessorEditor : public juce::AudioProcessorEditor,
    public juce::FileDragAndDropTarget
{
public:
    AnatomyAudioProcessorEditor(AnatomyAudioProcessor&);
    ~AnatomyAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // ドラッグ＆ドロップ対応
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    AnatomyAudioProcessor& audioProcessor;
    juce::AudioFormatManager formatManager;

    // 波形表示用コンポーネント
    WaveformComponent waveA, waveB, waveTransient, waveTonal;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnatomyAudioProcessorEditor)
};