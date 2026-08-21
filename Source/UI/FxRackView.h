// ==========================================
// File: FxRackView.h
// ANATOMY 4段目 FXラックパネル (Granular同等カード形式)
// ==========================================
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../PluginProcessor.h"
#include "FxSlotCard.h"
#include "ValueKnob.h"
#include "ColorPalette.h"
#include <array>
#include <vector>
#include <memory>

class FxRackView final : public juce::Component,
                         public juce::DragAndDropContainer
{
public:
    explicit FxRackView(AnatomyAudioProcessor& processor);
    ~FxRackView() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setTargetRoute(TargetRoute route);
    TargetRoute getTargetRoute() const { return activeRoute; }

    void updateAllCardStates();
    void synchronizeDetailsFromParameters();
    void resetAllSlotsToDefault();

    std::function<void(TargetRoute)> onRouteTabChanged;

private:
    void selectSlot(int slot);
    void swapSlots(int a, int b);
    void handleSlotTypeChanged(int slot);
    void rebuildDetails();
    void layoutDetails();

    juce::String getPrefix() const;
    int getSlotEffectType(int slot) const;

    AnatomyAudioProcessor& proc;
    TargetRoute activeRoute = TargetRoute::Transient;

    // レーンごとの6スロットのFX種別保持 (-1 = None, 0..5 = 各種FX)
    static constexpr int kNumSlots = 6;
    std::array<std::array<int, kNumSlots>, 3> laneSlotTypes;

    // タブボタン
    juce::TextButton btnTabTransient{ "TRANSIENT FX" };
    juce::TextButton btnTabTonal{ "TONAL FX" };
    juce::TextButton btnTabFullMix{ "FULL MIX FX" };

    // 6スロットカード
    std::array<std::unique_ptr<FxSlotCard>, kNumSlots> cards;
    int selectedSlot = 0;

    // 詳細パラメータコントロール群
    std::vector<std::unique_ptr<ValueKnob>> detailKnobs;
    std::vector<std::unique_ptr<juce::Label>> detailKnobLabels;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> detailAttachments;

    // 特殊コントロール (Noise用ラジオボタン、OTT用BANDSボタン等)
    std::vector<std::unique_ptr<juce::TextButton>> noiseTypeButtons;
    juce::TextButton ottBandsBtn{ "BANDS" };
    juce::TextButton ottBandSelectBtns[3];
    bool showOttBands = false;
    int selectedOttBand = 0; // 0=Low, 1=Mid, 2=High

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxRackView)
};
