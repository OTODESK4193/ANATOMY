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

    WaveformComponent waveDndFile;
    WaveformComponent waveTransient;
    WaveformComponent waveTonal;

    juce::TextButton btnOriginal{ "Full Mix" };
    juce::TextButton btnTransient{ "Transient Solo" };
    juce::TextButton btnTonal{ "Sustain Solo" };

    juce::Slider sliderClickLength;
    juce::Slider sliderClickCurve;
    juce::Slider sliderTransPitch; // ’Ç‰Á
    juce::Slider sliderTonalPitch; // ’Ç‰Á

    juce::Label lblClickLength;
    juce::Label lblClickCurve;
    juce::Label lblTransPitch;     // ’Ç‰Á
    juce::Label lblTonalPitch;     // ’Ç‰Á

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachClickLength;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachClickCurve;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachTransPitch; // ’Ç‰Á
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachTonalPitch; // ’Ç‰Á

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnatomyAudioProcessorEditor)
};