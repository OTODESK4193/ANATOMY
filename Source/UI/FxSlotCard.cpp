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
    typeBox.addItem("Transient Shaper", 8);
    typeBox.setSelectedId(1, juce::dontSendNotification);

    typeBox.onChange = [this]
    {
        setEffectType(getEffectType());
        if (onSelect != nullptr) onSelect(slot);
        if (onTypeChanged != nullptr) onTypeChanged();
    };
    addAndMakeVisible(typeBox);

    // AMOUNT (DRY/WET) ノブ
    amountKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    amountKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    amountKnob.setColour(juce::Slider::rotarySliderFillColourId, AnatomyColors::accentFull);
    amountKnob.setPopupDisplayEnabled(true, true, this);
    amountKnob.setEnabled(false);
    addAndMakeVisible(amountKnob);

    amountLabel.setText("AMOUNT", juce::dontSendNotification);
    amountLabel.setJustificationType(juce::Justification::centred);
    amountLabel.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
    amountLabel.setColour(juce::Label::textColourId, AnatomyColors::textDim);
    addAndMakeVisible(amountLabel);

    // OTTx2 Stage 2 コントロール
    s2AmountKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s2AmountKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    s2AmountKnob.setColour(juce::Slider::rotarySliderFillColourId, AnatomyColors::babyBlue);
    s2AmountKnob.setPopupDisplayEnabled(true, true, this);
    s2AmountKnob.setVisible(false);
    addChildComponent(s2AmountKnob);

    s2AmountLabel.setText("S2 DEPTH", juce::dontSendNotification);
    s2AmountLabel.setJustificationType(juce::Justification::centred);
    s2AmountLabel.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
    s2AmountLabel.setColour(juce::Label::textColourId, AnatomyColors::babyBlue.withAlpha(0.85f));
    s2AmountLabel.setVisible(false);
    addChildComponent(s2AmountLabel);

    s2ToggleBtn.setColour(juce::ToggleButton::textColourId, AnatomyColors::babyBlue);
    s2ToggleBtn.setColour(juce::ToggleButton::tickColourId, AnatomyColors::babyBlue);
    s2ToggleBtn.setVisible(false);
    addChildComponent(s2ToggleBtn);
}

juce::String FxSlotCard::getPrefix() const
{
    return (currentRoute == TargetRoute::Transient) ? "trans" :
           (currentRoute == TargetRoute::Tonal)     ? "tonal" :
           (currentRoute == TargetRoute::Layer)     ? "layer" : "full";
}

void FxSlotCard::setTargetRoute(TargetRoute r)
{
    currentRoute = r;
    updateFromRoute();
}

void FxSlotCard::updateFromRoute()
{
    juce::String pre = getPrefix();
    const auto& order = proc.getEffectOrder(currentRoute);

    if (slot < (int)order.size())
    {
        int fxType = order[(size_t)slot];
        typeBox.setSelectedId(fxType + 2, juce::dontSendNotification);
        setEffectType(fxType);
    }
    else
    {
        typeBox.setSelectedId(1, juce::dontSendNotification); // 1 = None
        setEffectType(-1);
    }
}

void FxSlotCard::setEffectType(int fxType)
{
    amountAttachment.reset(); // 既存のアタッチメントを解除
    s2AmountAttachment.reset();
    s2ToggleAttachment.reset();

    typeBox.setSelectedId(fxType >= 0 ? fxType + 2 : 1, juce::dontSendNotification);

    if (fxType == 3) // OTT (OTTx2 デュアルステージ)
    {
        juce::String pre = getPrefix();
        amountLabel.setText("S1 DEPTH", juce::dontSendNotification);
        amountLabel.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
        amountLabel.setColour(juce::Label::textColourId, AnatomyColors::peach.withAlpha(0.9f));
        amountKnob.setColour(juce::Slider::rotarySliderFillColourId, AnatomyColors::peach);
        amountKnob.setEnabled(true);
        amountKnob.setAlpha(1.0f);

        if (proc.apvts.getParameter(pre + "OttDepth") != nullptr)
        {
            amountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                proc.apvts, pre + "OttDepth", amountKnob);
        }

        s2AmountKnob.setVisible(true);
        s2AmountKnob.setEnabled(true);
        s2AmountKnob.setAlpha(1.0f);
        s2AmountLabel.setVisible(true);
        s2ToggleBtn.setVisible(true);

        if (proc.apvts.getParameter(pre + "Ott2Depth") != nullptr)
        {
            s2AmountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                proc.apvts, pre + "Ott2Depth", s2AmountKnob);
        }
        if (proc.apvts.getParameter(pre + "Ott2On") != nullptr)
        {
            s2ToggleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                proc.apvts, pre + "Ott2On", s2ToggleBtn);
        }
    }
    else if (fxType >= 0 && fxType < 7)
    {
        s2AmountKnob.setVisible(false);
        s2AmountLabel.setVisible(false);
        s2ToggleBtn.setVisible(false);

        amountLabel.setText("AMOUNT", juce::dontSendNotification);
        amountLabel.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
        amountLabel.setColour(juce::Label::textColourId, AnatomyColors::textDim);
        amountKnob.setColour(juce::Slider::rotarySliderFillColourId, AnatomyColors::accentFull);

        juce::String pre = getPrefix();
        juce::String mixParamId;
        switch (fxType)
        {
            case 0: mixParamId = pre + "SatMix"; break;
            case 1: mixParamId = pre + "BcMix"; break;
            case 2: mixParamId = pre + "NsMix"; break;
            case 4: mixParamId = pre + "GlueDepth"; break;
            case 5: mixParamId = pre + "LimMix"; break;
            case 6: mixParamId = pre + "TsMix"; break;
        }

        amountKnob.setEnabled(true);
        amountKnob.setAlpha(1.0f);
        if (proc.apvts.getParameter(mixParamId) != nullptr)
        {
            amountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                proc.apvts, mixParamId, amountKnob);
        }
    }
    else
    {
        s2AmountKnob.setVisible(false);
        s2AmountLabel.setVisible(false);
        s2ToggleBtn.setVisible(false);

        amountLabel.setText("AMOUNT", juce::dontSendNotification);
        amountLabel.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
        amountLabel.setColour(juce::Label::textColourId, AnatomyColors::textDim);
        amountKnob.setEnabled(false);
        amountKnob.setValue(0.0, juce::dontSendNotification);
        amountKnob.setAlpha(0.35f);
    }

    resized();
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

    int fxType = getEffectType();
    if (fxType == 3) // OTT
    {
        const int knobSize = 42;
        const int s1X = 14;
        const int s2X = getWidth() - 14 - knobSize;
        const int knobY = 54;

        amountKnob.setBounds(s1X, knobY, knobSize, knobSize);
        amountLabel.setBounds(s1X - 6, knobY + knobSize + 2, knobSize + 12, 12);

        s2AmountKnob.setBounds(s2X, knobY, knobSize, knobSize);
        s2AmountLabel.setBounds(s2X - 6, knobY + knobSize + 2, knobSize + 12, 12);

        s2ToggleBtn.setBounds(getWidth() / 2 - 16, knobY + 10, 32, 20);
    }
    else
    {
        amountKnob.setBounds((getWidth() - 48) / 2, 54, 48, 48);
        amountLabel.setBounds(0, 102, getWidth(), 12);
    }
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
