// ==========================================
// File: PluginEditor.h
// ANATOMY V1.1.0 (Granular Style Modern Edition)
// ==========================================
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginProcessor.h"
#include "UI/ColorPalette.h"
#include "UI/ArcDial.h"
#include "UI/ValueKnob.h"
#include "UI/GlowToggle.h"
#include "UI/DragExportButton.h"
#include "UI/FullMixLaneView.h"
#include "UI/TransientLaneView.h"
#include "UI/TonalLaneView.h"
#include "UI/LayerLaneView.h"
#include "UI/FxRackView.h"
#include <memory>

class AnatomyAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          public juce::FileDragAndDropTarget,
                                          public juce::DragAndDropContainer,
                                          private juce::Timer
{
public:
    explicit AnatomyAudioProcessorEditor(AnatomyAudioProcessor&);
    ~AnatomyAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    void updateSoloButtonStates();
    void resetAllParameters();
    void confirmThen(const juce::String& title, const juce::String& message, std::function<void()> action);

    AnatomyAudioProcessor& audioProcessor;
    juce::AudioFormatManager formatManager;
    ArcDialLookAndFeel arcLookAndFeel;

    // --- 1段目: ヘッダー ---
    juce::TextButton loadButton       { "LOAD" };
    juce::TextButton resetButton      { "RESET" };
    juce::ComboBox themeCombo;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::String hudFile, hudLength, hudSr, hudStatus;
    int lastThemeIndex = 0;
    bool wasProcessing = false;

    // --- 2段目: FullMix & Layer (50% スプリット) ---
    FullMixLaneView fullMixLane;
    LayerLaneView layerLane;

    // --- 3段目: TransientView & TonalView (50% スプリット) ---
    TransientLaneView transientLane;
    TonalLaneView tonalLane;

    // --- 4段目: Card FX Rack ---
    FxRackView fxRackView;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnatomyAudioProcessorEditor)
};