// ==========================================
// File: FxSlotCard.h
// ANATOMY 4段目 FXスロットカード (Granularスタイル)
// ==========================================
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../PluginProcessor.h"
#include "ValueKnob.h"
#include "ColorPalette.h"
#include <functional>
#include <memory>

class FxSlotCard : public juce::Component,
                   public juce::DragAndDropTarget
{
public:
    FxSlotCard(AnatomyAudioProcessor& processor, int slotIndex,
               std::function<void(int, int)> onSwapCallback,
               std::function<void(int)> onSelectCallback,
               std::function<void()> onTypeChangedCallback);
    ~FxSlotCard() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setSelected(bool shouldBeSelected)
    {
        if (selected != shouldBeSelected) { selected = shouldBeSelected; repaint(); }
    }

    void setTargetRoute(TargetRoute r);
    void updateFromRoute();

    int getEffectType() const { return typeBox.getSelectedId() - 2; }
    void setEffectType(int fxType);

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;

    // --- DragAndDropTarget ---
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails&) override { dragOver = true; repaint(); }
    void itemDragExit(const SourceDetails&) override { dragOver = false; repaint(); }
    void itemDropped(const SourceDetails& details) override;

private:
    AnatomyAudioProcessor& proc;
    const int slot; // 0-5
    TargetRoute currentRoute = TargetRoute::Transient;

    std::function<void(int, int)> onSwap;
    std::function<void(int)> onSelect;
    std::function<void()> onTypeChanged;

    juce::ComboBox typeBox;
    ValueKnob amountKnob;
    juce::Label amountLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttachment;

    bool dragOver = false;
    bool selected = false;

    juce::String getPrefix() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxSlotCard)
};
