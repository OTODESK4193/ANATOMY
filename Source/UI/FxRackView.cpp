// ==========================================
// File: FxRackView.cpp
// ==========================================
#include "FxRackView.h"

namespace
{
    constexpr int kHeaderH = 26;
    constexpr int kCardY = 32;
    constexpr int kCardH = 118;
    constexpr int kDetailY = 156;
}

FxRackView::FxRackView(AnatomyAudioProcessor& p) : proc(p)
{
    // 全レーンのスロットを初期化 (-1 = None)
    for (int r = 0; r < 4; ++r)
        for (int s = 0; s < kNumSlots; ++s)
            laneSlotTypes[(size_t)r][(size_t)s] = -1;

    // プロセッサに既存の初期エフェクト順序があればスロットに展開
    for (int r = 0; r < 4; ++r)
    {
        TargetRoute rt = (r == 0) ? TargetRoute::Transient :
                         (r == 1) ? TargetRoute::Tonal :
                         (r == 2) ? TargetRoute::FullMix : TargetRoute::Layer;
        const auto& order = proc.getEffectOrder(rt);
        for (size_t i = 0; i < order.size() && i < (size_t)kNumSlots; ++i)
            laneSlotTypes[(size_t)r][i] = order[i];
    }

    // タブボタン設定
    auto styleTab = [this](juce::TextButton& b, juce::Colour c) {
        b.setClickingTogglesState(false);
        b.setColour(juce::TextButton::buttonColourId, AnatomyColors::knobTrack);
        b.setColour(juce::TextButton::textColourOffId, AnatomyColors::textDim);
        addAndMakeVisible(b);
    };

    styleTab(btnTabTransient, AnatomyColors::accentTransient);
    styleTab(btnTabTonal,     AnatomyColors::accentTonal);
    styleTab(btnTabFullMix,   AnatomyColors::accentFull);
    styleTab(btnTabLayer,     AnatomyColors::peach);

    btnTabTransient.onClick = [this] { setTargetRoute(TargetRoute::Transient); if (onRouteTabChanged) onRouteTabChanged(TargetRoute::Transient); };
    btnTabTonal.onClick     = [this] { setTargetRoute(TargetRoute::Tonal);     if (onRouteTabChanged) onRouteTabChanged(TargetRoute::Tonal); };
    btnTabFullMix.onClick   = [this] { setTargetRoute(TargetRoute::FullMix);   if (onRouteTabChanged) onRouteTabChanged(TargetRoute::FullMix); };
    btnTabLayer.onClick     = [this] { setTargetRoute(TargetRoute::Layer);     if (onRouteTabChanged) onRouteTabChanged(TargetRoute::Layer); };

    // 6スロットカード生成
    for (int i = 0; i < kNumSlots; ++i)
    {
        cards[(size_t)i] = std::make_unique<FxSlotCard>(proc, i,
            [this](int a, int b) { swapSlots(a, b); },
            [this](int s) { selectSlot(s); },
            [this, i] { handleSlotTypeChanged(i); });
        addAndMakeVisible(*cards[(size_t)i]);
    }

    // OTT BANDS ボタン初期化
    ottBandsBtn.setClickingTogglesState(true);
    ottBandsBtn.setColour(juce::TextButton::buttonColourId, AnatomyColors::knobTrack);
    ottBandsBtn.setColour(juce::TextButton::buttonOnColourId, AnatomyColors::accentFull.withAlpha(0.7f));
    ottBandsBtn.setColour(juce::TextButton::textColourOffId, AnatomyColors::textDim);
    ottBandsBtn.setColour(juce::TextButton::textColourOnId, AnatomyColors::text);
    ottBandsBtn.onClick = [this] {
        juce::MessageManager::callAsync([this] {
            showOttBands = ottBandsBtn.getToggleState();
            rebuildDetails();
        });
    };

    // OTT LOW/MID/HIGH ボタン
    juce::StringArray bNames{ "LOW", "MID", "HIGH" };
    for (int i = 0; i < 3; ++i)
    {
        ottBandSelectBtns[i].setButtonText(bNames[i]);
        ottBandSelectBtns[i].setClickingTogglesState(true);
        ottBandSelectBtns[i].setRadioGroupId(300);
        ottBandSelectBtns[i].setColour(juce::TextButton::buttonColourId, AnatomyColors::knobTrack);
        ottBandSelectBtns[i].setColour(juce::TextButton::buttonOnColourId, AnatomyColors::accentFull);
        ottBandSelectBtns[i].setColour(juce::TextButton::textColourOffId, AnatomyColors::textDim);
        ottBandSelectBtns[i].setColour(juce::TextButton::textColourOnId, juce::Colours::black);
        ottBandSelectBtns[i].onClick = [this, i] {
            juce::MessageManager::callAsync([this, i] {
                selectedOttBand = i;
                rebuildDetails();
            });
        };
    }
    ottBandSelectBtns[0].setToggleState(true, juce::dontSendNotification);

    setTargetRoute(TargetRoute::Transient);
}

juce::String FxRackView::getPrefix() const
{
    return (activeRoute == TargetRoute::Transient) ? "trans" :
           (activeRoute == TargetRoute::Tonal)     ? "tonal" : 
           (activeRoute == TargetRoute::Layer)     ? "layer" : "full";
}

void FxRackView::setTargetRoute(TargetRoute route)
{
    activeRoute = route;

    btnTabTransient.setColour(juce::TextButton::textColourOffId,
        (route == TargetRoute::Transient) ? AnatomyColors::accentTransient : AnatomyColors::textDim);
    btnTabTonal.setColour(juce::TextButton::textColourOffId,
        (route == TargetRoute::Tonal) ? AnatomyColors::accentTonal : AnatomyColors::textDim);
    btnTabFullMix.setColour(juce::TextButton::textColourOffId,
        (route == TargetRoute::FullMix) ? AnatomyColors::accentFull : AnatomyColors::textDim);
    btnTabLayer.setColour(juce::TextButton::textColourOffId,
        (route == TargetRoute::Layer) ? AnatomyColors::peach : AnatomyColors::textDim);

    updateAllCardStates();
    selectSlot(0);
    repaint();
}

int FxRackView::getSlotEffectType(int slot) const
{
    if (slot >= 0 && slot < kNumSlots)
    {
        int routeIdx = (activeRoute == TargetRoute::Transient) ? 0 :
                       (activeRoute == TargetRoute::Tonal)     ? 1 :
                       (activeRoute == TargetRoute::Layer)     ? 3 : 2;
        return laneSlotTypes[(size_t)routeIdx][(size_t)slot];
    }
    return -1;
}

void FxRackView::selectSlot(int slot)
{
    selectedSlot = juce::jlimit(0, kNumSlots - 1, slot);
    for (int i = 0; i < kNumSlots; ++i)
        cards[(size_t)i]->setSelected(i == selectedSlot);
    rebuildDetails();
    repaint();
}

void FxRackView::swapSlots(int a, int b)
{
    if (a == b || a < 0 || b < 0 || a >= kNumSlots || b >= kNumSlots)
        return;

    int routeIdx = (activeRoute == TargetRoute::Transient) ? 0 :
                   (activeRoute == TargetRoute::Tonal)     ? 1 :
                   (activeRoute == TargetRoute::Layer)     ? 3 : 2;

    std::swap(laneSlotTypes[(size_t)routeIdx][(size_t)a], laneSlotTypes[(size_t)routeIdx][(size_t)b]);
    updateAllCardStates();

    // アクティブ順序を更新
    std::vector<int> newOrder;
    for (int i = 0; i < kNumSlots; ++i)
    {
        int t = laneSlotTypes[(size_t)routeIdx][(size_t)i];
        if (t >= 0 && t < 7)
        {
            if (std::find(newOrder.begin(), newOrder.end(), t) == newOrder.end())
                newOrder.push_back(t);
        }
    }
    proc.updateRouteOrder(activeRoute, newOrder);
    selectSlot(b);
}

void FxRackView::handleSlotTypeChanged(int slot)
{
    int routeIdx = (activeRoute == TargetRoute::Transient) ? 0 :
                   (activeRoute == TargetRoute::Tonal)     ? 1 :
                   (activeRoute == TargetRoute::Layer)     ? 3 : 2;

    int newType = cards[(size_t)slot]->getEffectType(); // -1..6
    laneSlotTypes[(size_t)routeIdx][(size_t)slot] = newType;

    // スロット1〜6を順に走査し、アクティブなエフェクト順序を生成
    std::vector<int> newOrder;
    for (int i = 0; i < kNumSlots; ++i)
    {
        int t = laneSlotTypes[(size_t)routeIdx][(size_t)i];
        if (t >= 0 && t < 7)
        {
            if (std::find(newOrder.begin(), newOrder.end(), t) == newOrder.end())
                newOrder.push_back(t);
        }
    }

    // エフェクトのactive状態更新
    for (int i = 0; i < 6; ++i)
    {
        bool inChain = std::find(newOrder.begin(), newOrder.end(), i) != newOrder.end();
        AudioEffect* fx = nullptr;
        if (activeRoute == TargetRoute::Transient) fx = proc.getTransientPoolInstance(i);
        else if (activeRoute == TargetRoute::Tonal) fx = proc.getTonalPoolInstance(i);
        else fx = proc.getFullMixPoolInstance(i);
        if (fx != nullptr) fx->setActive(inChain);
    }

    proc.updateRouteOrder(activeRoute, newOrder);
    rebuildDetails();
}

void FxRackView::updateAllCardStates()
{
    int routeIdx = (activeRoute == TargetRoute::Transient) ? 0 :
                   (activeRoute == TargetRoute::Tonal)     ? 1 : 2;

    for (int i = 0; i < kNumSlots; ++i)
    {
        cards[(size_t)i]->setTargetRoute(activeRoute);
        cards[(size_t)i]->setEffectType(laneSlotTypes[(size_t)routeIdx][(size_t)i]);
    }
}

void FxRackView::synchronizeDetailsFromParameters()
{
    // タイマーからの呼び出し用
}

void FxRackView::rebuildDetails()
{
    // 既存コントロールの破棄
    detailAttachments.clear();
    detailKnobs.clear();
    detailKnobLabels.clear();
    satTypeButtons.clear();
    noiseTypeButtons.clear();
    removeChildComponent(&ottBandsBtn);
    for (auto& b : ottBandSelectBtns) removeChildComponent(&b);

    int fxType = getSlotEffectType(selectedSlot);
    if (fxType < 0 || fxType >= 7)
    {
        repaint();
        return;
    }

    juce::String pre = getPrefix();
    juce::Colour accent = (activeRoute == TargetRoute::Transient) ? AnatomyColors::accentTransient :
                          (activeRoute == TargetRoute::Tonal)     ? AnatomyColors::accentTonal :
                          (activeRoute == TargetRoute::Layer)     ? AnatomyColors::peach :
                                                                     AnatomyColors::accentFull;

    struct KnobDef { juce::String id; juce::String label; };
    std::vector<KnobDef> knobDefs;

    switch (fxType)
    {
    case 0: // Saturation (NextGenKick2準拠: TYPE 4種 + Drive, Trim, PreHPF, Dry/Wet)
        {
            juce::StringArray typeNames{ "TUBE", "TAPE", "DIODE", "FUZZ" };
            for (int i = 0; i < 4; ++i)
            {
                auto btn = std::make_unique<juce::TextButton>(typeNames[i]);
                btn->setClickingTogglesState(true);
                btn->setRadioGroupId(500);
                btn->setColour(juce::TextButton::buttonColourId, AnatomyColors::knobTrack);
                btn->setColour(juce::TextButton::buttonOnColourId, accent);
                btn->setColour(juce::TextButton::textColourOffId, AnatomyColors::textDim);
                btn->setColour(juce::TextButton::textColourOnId, juce::Colours::black);
                btn->onClick = [this, i, pre] {
                    if (auto* p = proc.apvts.getParameter(pre + "SatType"))
                        p->setValueNotifyingHost(p->convertTo0to1((float)i));
                };
                addAndMakeVisible(*btn);
                satTypeButtons.push_back(std::move(btn));
            }

            if (auto* pVal = proc.apvts.getRawParameterValue(pre + "SatType"))
            {
                int curType = (int)pVal->load();
                if (curType >= 0 && curType < 4)
                    satTypeButtons[(size_t)curType]->setToggleState(true, juce::dontSendNotification);
            }

            knobDefs = {
                { pre + "SatDrive", "DRIVE" },
                { pre + "SatTrim",  "OUT TRIM" },
                { pre + "SatPre",   "PRE HPF" },
                { pre + "SatMix",   "DRY/WET" }
            };
        }
        break;
    case 1: // BitCrusher
        knobDefs = {
            { pre + "BcBits",   "BITS" },
            { pre + "BcDown",   "DOWNSAMPLE" },
            { pre + "BcJitter", "JITTER" },
            { pre + "BcMix",    "DRY/WET" }
        };
        break;
    case 2: // Noise
        {
            juce::StringArray typeNames{ "WHITE", "PINK", "BROWN", "BLUE" };
            for (int i = 0; i < 4; ++i)
            {
                auto btn = std::make_unique<juce::TextButton>(typeNames[i]);
                btn->setClickingTogglesState(true);
                btn->setRadioGroupId(400);
                btn->setColour(juce::TextButton::buttonColourId, AnatomyColors::knobTrack);
                btn->setColour(juce::TextButton::buttonOnColourId, accent);
                btn->setColour(juce::TextButton::textColourOffId, AnatomyColors::textDim);
                btn->setColour(juce::TextButton::textColourOnId, juce::Colours::black);
                btn->onClick = [this, i, pre] {
                    if (auto* p = proc.apvts.getParameter(pre + "NsType"))
                        p->setValueNotifyingHost(p->convertTo0to1((float)i));
                };
                addAndMakeVisible(*btn);
                noiseTypeButtons.push_back(std::move(btn));
            }

            if (auto* pVal = proc.apvts.getRawParameterValue(pre + "NsType"))
            {
                int curType = (int)pVal->load();
                if (curType >= 0 && curType < 4)
                    noiseTypeButtons[(size_t)curType]->setToggleState(true, juce::dontSendNotification);
            }

            knobDefs = {
                { pre + "NsDecay",  "DECAY ms" },
                { pre + "NsGain",   "GAIN dB" },
                { pre + "NsAttack", "ATTACK ms" },
                { pre + "NsBpFreq", "BP FREQ" },
                { pre + "NsMix",    "DRY/WET" }
            };
        }
        break;
    case 3: // OTT
        addAndMakeVisible(ottBandsBtn);
        ottBandsBtn.setToggleState(showOttBands, juce::dontSendNotification);

        if (!showOttBands)
        {
            knobDefs = {
                { pre + "OttTime",          "TIME" },
                { pre + "OttLowMidXOver",   "LO/MI XO" },
                { pre + "OttMidHighXOver",  "MI/HI XO" },
                { pre + "OttGateFloor",     "GATE dB" },
                { pre + "OttDepth",         "DRY/WET" }
            };
        }
        else
        {
            for (auto& b : ottBandSelectBtns) addAndMakeVisible(b);
            juce::StringArray bns{ "Low", "Mid", "High" };
            auto bn = bns[selectedOttBand];

            knobDefs = {
                { pre + "Ott" + bn + "Up",   "UP COMP" },
                { pre + "Ott" + bn + "Down", "DOWN COMP" },
                { pre + "Ott" + bn + "Gain", "GAIN dB" },
                { pre + "OttDepth",          "DRY/WET" }
            };
        }
        break;
    case 4: // Glue
        knobDefs = {
            { pre + "GlueThr",   "THR dBFS" },
            { pre + "GlueRatio", "RATIO" },
            { pre + "GlueAtk",   "ATTACK ms" },
            { pre + "GlueRel",   "RELEASE ms" },
            { pre + "GlueMkp",   "MAKEUP dB" },
            { pre + "GlueDepth", "DRY/WET" }
        };
        break;
    case 5: // Limiter
        knobDefs = {
            { pre + "LimCeil", "CEILING dB" },
            { pre + "LimMix",  "DRY/WET" }
        };
        break;
    case 6: // Transient Shaper (NextGenKick2完全移植)
        knobDefs = {
            { pre + "TsAttack",  "ATTACK" },
            { pre + "TsSustain", "SUSTAIN" },
            { pre + "TsMix",     "DRY/WET" }
        };
        break;
    }

    for (const auto& def : knobDefs)
    {
        auto knob = std::make_unique<ValueKnob>();
        knob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 13);
        knob->setColour(juce::Slider::rotarySliderFillColourId, accent);
        knob->setColour(juce::Slider::textBoxTextColourId, AnatomyColors::text);
        knob->setPopupDisplayEnabled(true, true, this);
        addAndMakeVisible(*knob);

        detailAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            proc.apvts, def.id, *knob));
        detailKnobs.push_back(std::move(knob));

        auto label = std::make_unique<juce::Label>();
        label->setText(def.label, juce::dontSendNotification);
        label->setJustificationType(juce::Justification::centred);
        label->setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
        label->setColour(juce::Label::textColourId, accent.withAlpha(0.9f));
        addAndMakeVisible(*label);
        detailKnobLabels.push_back(std::move(label));
    }

    layoutDetails();
    repaint();
}

void FxRackView::layoutDetails()
{
    int fxType = getSlotEffectType(selectedSlot);
    if (fxType < 0 || fxType >= 7) return;

    int x = 20;
    int y = kDetailY + 22;

    // Saturation Type ボタン
    if (fxType == 0 && satTypeButtons.size() >= 4)
    {
        for (size_t i = 0; i < satTypeButtons.size(); ++i)
        {
            satTypeButtons[i]->setBounds(x, y + 10 + (int)i * 20, 68, 18);
        }
        x += 78;
    }

    // Noise Type ボタン
    if (fxType == 2 && noiseTypeButtons.size() >= 4)
    {
        for (size_t i = 0; i < noiseTypeButtons.size(); ++i)
        {
            noiseTypeButtons[i]->setBounds(x, y + 10 + (int)i * 20, 72, 18);
        }
        x += 86;
    }

    // ノブ配置
    for (size_t i = 0; i < detailKnobs.size(); ++i)
    {
        detailKnobLabels[i]->setBounds(x, y, 68, 14);
        detailKnobs[i]->setBounds(x + 5, y + 15, 58, 58);
        x += 76;
    }

    // OTT BANDS / Band Selectors
    if (fxType == 3)
    {
        int btnY = y + 35;
        int btnX = x + 10;
        
        ottBandsBtn.setBounds(btnX, btnY, 60, 20);
        btnX += 70;

        if (showOttBands)
        {
            for (int i = 0; i < 3; ++i)
            {
                ottBandSelectBtns[i].setBounds(btnX, btnY, 46, 20);
                btnX += 50;
            }
        }
    }
}

void FxRackView::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // 背景
    g.setColour(AnatomyColors::panel);
    g.fillRoundedRectangle(bounds, 8.0f);

    juce::Colour accent = (activeRoute == TargetRoute::Transient) ? AnatomyColors::accentTransient :
                          (activeRoute == TargetRoute::Tonal)     ? AnatomyColors::accentTonal :
                          (activeRoute == TargetRoute::Layer)     ? AnatomyColors::peach :
                                                                     AnatomyColors::accentFull;

    // 外枠
    g.setColour(AnatomyColors::panelLine);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);

    // 上部ヘッダー部アンダーライン
    g.setColour(AnatomyColors::panelLine);
    g.fillRect(10, kHeaderH + 4, getWidth() - 20, 1);

    // ガイドテキスト
    g.setFont(juce::Font(juce::FontOptions(10.5f)));
    g.setColour(AnatomyColors::textDim.withAlpha(0.65f));
    g.drawText("(Signal flows Slot 1 -> 6, drag headers to reorder)", 420, 6, 400, 20, juce::Justification::centredLeft);

    // スロット間の矢印 (>)
    g.setColour(AnatomyColors::textDim.withAlpha(0.45f));
    g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    for (int i = 0; i < kNumSlots - 1; ++i)
    {
        int cardW = (getWidth() - 40 - (kNumSlots - 1) * 12) / kNumSlots;
        int arrX = 20 + (i + 1) * cardW + i * 12;
        g.drawText(">", arrX, kCardY + kCardH / 2 - 10, 12, 20, juce::Justification::centred);
    }

    // 詳細エリア ヘッダー
    g.setColour(accent);
    g.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));

    const juce::StringArray typeNames{ "SATURATION", "BITCRUSHER", "NOISE GENERATOR", "OTT MULTIBAND", "GLUE COMPRESSOR", "LIMITER", "TRANSIENT SHAPER" };
    int t = getSlotEffectType(selectedSlot);
    juce::String fxName = (t >= 0 && t < 7) ? typeNames[t] : "NONE";

    g.drawText("DETAILS  -  SLOT " + juce::String(selectedSlot + 1) + "  [" + fxName + "]",
               20, kDetailY, 600, 16, juce::Justification::centredLeft);

    g.setColour(accent.withAlpha(0.35f));
    g.fillRect(20, kDetailY + 18, getWidth() - 40, 1);

    if (t < 0 || t >= 7)
    {
        g.setColour(AnatomyColors::textDim.withAlpha(0.55f));
        g.setFont(juce::Font(juce::FontOptions(11.5f)));
        g.drawText("Select an FX type on this slot to edit its parameters.",
                   20, kDetailY + 36, 500, 20, juce::Justification::centredLeft);
    }
}

void FxRackView::resized()
{
    // タブボタン: 左上 (4タブ配置: Transient, Tonal, Layer, FullMix)
    int tabW = 96, tabH = 22, gap = 6;
    btnTabTransient.setBounds(20, 6, tabW, tabH);
    btnTabTonal.setBounds(20 + tabW + gap, 6, tabW, tabH);
    btnTabLayer.setBounds(20 + (tabW + gap) * 2, 6, tabW, tabH);
    btnTabFullMix.setBounds(20 + (tabW + gap) * 3, 6, tabW, tabH);

    // 6スロットカード
    int availW = getWidth() - 40;
    int cardGap = 12;
    int cardW = (availW - (kNumSlots - 1) * cardGap) / kNumSlots;

    for (int i = 0; i < kNumSlots; ++i)
        cards[(size_t)i]->setBounds(20 + i * (cardW + cardGap), kCardY, cardW, kCardH);

    layoutDetails();
}

void FxRackView::resetAllSlotsToDefault()
{
    for (int r = 0; r < 4; ++r)
    {
        for (int s = 0; s < kNumSlots; ++s)
            laneSlotTypes[(size_t)r][(size_t)s] = -1; // 全スロットを NONE に設定
        TargetRoute rt = (r == 0) ? TargetRoute::Transient :
                         (r == 1) ? TargetRoute::Tonal :
                         (r == 2) ? TargetRoute::FullMix : TargetRoute::Layer;
        proc.updateRouteOrder(rt, {}); // プロセッサのエフェクトチェインも空に更新
    }
    selectedSlot = 0;
    updateAllCardStates();
    rebuildDetails();
    repaint();
}
