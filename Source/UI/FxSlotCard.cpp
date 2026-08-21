// ==========================================
// File: FxSlotCard.cpp
// ==========================================
#include "FxSlotCard.h"

FxSlotCard::FxSlotCard(AnatomyAudioProcessor& processor, int slotIndex,
                       std::function<void(int, int)> onSwapCallback,
                       std::function<void(int)> onSelectCallback,
                       std::function<void()> onTypeChangedCallback)
    : proc(processor), slot(slotIndex),
      onSwap(std::move(onSwapCallback)),
      onSelect(std::move(onSelectCallback)),
      onTypeChanged(std::move(onTypeChangedCallback))
{
    // タイプ選択コンボボックス
    typeBox.addItem("None", 1);
    typeBox.addItem("Saturation", 2);
    typeBox.addItem("BitCrusher", 3);
    typeBox.addItem("Noise", 4);
    typeBox.addItem("OTT", 5);
    typeBox.addItem("Glue Comp", 6);
    typeBox.addItem("Limiter", 7);
    typeBox.setSelectedId(1, juce::dontSendNotification);

    typeBox.onChange = [this]
    {
        if (onSelect != nullptr) onSelect(slot);
        if (onTypeChanged != nullptr) onTypeChanged();
    };
    addAndMakeVisible(typeBox);

    // AMOUNT (DRY/WET) ノブ
    amountKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    amountKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    amountKnob.setColour(juce::Slider::rotarySliderFillColourId, AnatomyColors::accentFull);
    amountKnob.setRange(0.0, 1.0, 0.01);
    amountKnob.setValue(1.0, juce::dontSendNotification);
    amountKnob.setPopupDisplayEnabled(true, true, this);
    addAndMakeVisible(amountKnob);

    amountKnob.onValueChange = [this]
    {
        int fxType = getEffectType(); // 0..5
        if (fxType >= 0 && fxType < 6)
        {
            juce::String pre = getPrefix();
            juce::String mixParamId;
            switch (fxType)
            {
                case 0: mixParamId = pre + "SatMix"; break;
                case 1: mixParamId = pre + "BcMix"; break;
                case 2: mixParamId = pre + "NsMix"; break;
                case 3: mixParamId = pre + "OttDepth"; break;
                case 4: mixParamId = pre + "GlueDepth"; break;
                case 5: mixParamId = pre + "LimMix"; break;
            }
            if (auto* p = proc.apvts.getParameter(mixParamId))
                p->setValueNotifyingHost(p->convertTo0to1((float)amountKnob.getValue()));
        }
    };

    amountLabel.setText("AMOUNT", juce::dontSendNotification);
    amountLabel.setJustificationType(juce::Justification::centred);
    amountLabel.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
    amountLabel.setColour(juce::Label::textColourId, AnatomyColors::textDim);
    addAndMakeVisible(amountLabel);
}

juce::String FxSlotCard::getPrefix() const
{
    return (currentRoute == TargetRoute::Transient) ? "trans" :
           (currentRoute == TargetRoute::Tonal)     ? "tonal" : "full";
}

void FxSlotCard::setTargetRoute(TargetRoute r)
{
    currentRoute = r;
    updateFromRoute();
}

void FxSlotCard::updateFromRoute()
{
    const auto& order = proc.getEffectOrder(currentRoute);
    if (slot < (int)order.size())
    {
        int fxType = order[(size_t)slot];
        typeBox.setSelectedId(fxType + 2, juce::dontSendNotification); // +2 maps 0(Sat)->2

        // Amount ノブに現在の Mix パラメータ値を反映
        juce::String pre = getPrefix();
        juce::String mixParamId;
        switch (fxType)
        {
            case 0: mixParamId = pre + "SatMix"; break;
            case 1: mixParamId = pre + "BcMix"; break;
            case 2: mixParamId = pre + "NsMix"; break;
            case 3: mixParamId = pre + "OttDepth"; break;
            case 4: mixParamId = pre + "GlueDepth"; break;
            case 5: mixParamId = pre + "LimMix"; break;
        }
        if (auto* val = proc.apvts.getRawParameterValue(mixParamId))
            amountKnob.setValue(val->load(), juce::dontSendNotification);

        amountKnob.setEnabled(true);
    }
    else
    {
        typeBox.setSelectedId(1, juce::dontSendNotification); // 1 = None
        amountKnob.setEnabled(false);
    }
    repaint();
}

void FxSlotCard::setEffectType(int fxType)
{
    typeBox.setSelectedId(fxType >= 0 ? fxType + 2 : 1, juce::dontSendNotification);
    if (fxType >= 0 && fxType < 6)
    {
        juce::String pre = getPrefix();
        juce::String mixParamId;
        switch (fxType)
        {
            case 0: mixParamId = pre + "SatMix"; break;
            case 1: mixParamId = pre + "BcMix"; break;
            case 2: mixParamId = pre + "NsMix"; break;
            case 3: mixParamId = pre + "OttDepth"; break;
            case 4: mixParamId = pre + "GlueDepth"; break;
            case 5: mixParamId = pre + "LimMix"; break;
        }
        if (auto* val = proc.apvts.getRawParameterValue(mixParamId))
            amountKnob.setValue(val->load(), juce::dontSendNotification);
        amountKnob.setEnabled(true);
    }
    else
    {
        amountKnob.setEnabled(false);
    }
    repaint();
}

void FxSlotCard::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // 背景
    g.setColour(selected ? AnatomyColors::panel.brighter(0.08f) : AnatomyColors::panel);
    g.fillRoundedRectangle(bounds, 8.0f);

    // 枠線
    juce::Colour border = AnatomyColors::panelLine;
    float borderW = 1.0f;
    if (dragOver)
    {
        border = AnatomyColors::mint.withAlpha(0.9f);
        borderW = 2.0f;
    }
    else if (selected)
    {
        juce::Colour accent = (currentRoute == TargetRoute::Transient) ? AnatomyColors::accentTransient :
                              (currentRoute == TargetRoute::Tonal)     ? AnatomyColors::accentTonal :
                                                                         AnatomyColors::accentFull;
        border = accent.withAlpha(0.85f);
        borderW = 1.6f;
    }
    g.setColour(border);
    g.drawRoundedRectangle(bounds.reduced(0.75f), 8.0f, borderW);

    // ヘッダーバー (ドラッグハンドル領域)
    g.setColour(AnatomyColors::knobTrack.withAlpha(0.6f));
    g.fillRoundedRectangle(bounds.withHeight(22.0f), 8.0f);

    juce::Colour titleColour = selected ? ((currentRoute == TargetRoute::Transient) ? AnatomyColors::accentTransient :
                                          (currentRoute == TargetRoute::Tonal)     ? AnatomyColors::accentTonal :
                                                                                     AnatomyColors::accentFull)
                                        : AnatomyColors::textDim;
    g.setColour(titleColour);
    g.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
    g.drawText("SLOT " + juce::String(slot + 1), 8, 2, 70, 18, juce::Justification::centredLeft);

    // ドラッグハンドルアイコン (≡)
    g.setColour(AnatomyColors::textDim);
    const float hx = bounds.getWidth() - 22.0f;
    for (int i = 0; i < 3; ++i)
        g.fillRoundedRectangle(hx, 6.0f + (float)i * 3.5f, 12.0f, 1.5f, 0.75f);
}

void FxSlotCard::resized()
{
    typeBox.setBounds(8, 28, getWidth() - 16, 22);
    amountKnob.setBounds((getWidth() - 48) / 2, 54, 48, 48);
    amountLabel.setBounds(0, 102, getWidth(), 12);
}

void FxSlotCard::mouseDown(const juce::MouseEvent&)
{
    if (onSelect != nullptr) onSelect(slot);
}

void FxSlotCard::mouseDrag(const juce::MouseEvent& e)
{
    if (e.mouseDownPosition.getY() <= 24.0f)
    {
        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
        {
            if (!container->isDragAndDropActive())
            {
                juce::Image dragImage(juce::Image::ARGB, getWidth(), 24, true);
                juce::Graphics dg(dragImage);
                dg.setColour(AnatomyColors::accentFull.withAlpha(0.75f));
                dg.fillRoundedRectangle(dragImage.getBounds().toFloat(), 4.0f);
                dg.setColour(juce::Colours::black);
                dg.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
                dg.drawText("SLOT " + juce::String(slot + 1) + " : " + typeBox.getText(),
                            dragImage.getBounds(), juce::Justification::centred);

                container->startDragging(juce::var(slot), this, dragImage, true);
            }
        }
    }
}

bool FxSlotCard::isInterestedInDragSource(const SourceDetails& details)
{
    return details.description.isInt() && (int)details.description != slot;
}

void FxSlotCard::itemDropped(const SourceDetails& details)
{
    dragOver = false;
    repaint();
    if (onSwap != nullptr && details.description.isInt())
        onSwap((int)details.description, slot);
}
