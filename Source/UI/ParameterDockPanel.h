#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../PluginProcessor.h"
#include "../DSP/Effects/AudioEffect.h"
#include "../DSP/Effects/ADAA_Saturation.h"
#include "../DSP/Effects/BitCrusher.h"
#include "../DSP/Effects/NoiseGenerator.h"
#include "../DSP/Effects/OTT_Multiband.h"
#include "../DSP/Effects/GlueCompressor.h"
#include "../DSP/Effects/Limiter.h"
#include <vector>
#include <memory>

/**
 * ParameterDockPanel (All-Knob Edition)
 * 全パラメーターをロータリーノブで表示。
 * OTT は BANDS ボタンで3バンドの詳細パラメーターへ切り替え可能。
 */
class ParameterDockPanel final : public juce::Component
{
public:
    ParameterDockPanel(AnatomyAudioProcessor& p) : processor(p)
    {
        // 情報ラベル（エフェクト未選択時）
        lblInfo.setText("Select an effect to edit parameters", juce::dontSendNotification);
        lblInfo.setFont(juce::Font(11.5f, juce::Font::italic));
        lblInfo.setJustificationType(juce::Justification::centred);
        lblInfo.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.3f));
        addAndMakeVisible(lblInfo);

        // ── Noise Generator: WHITE / PINK / BROWN / BLUE ──────────────────
        juce::StringArray typeNames{ "WHITE", "PINK", "BROWN", "BLUE" };
        for (int i = 0; i < 4; ++i)
        {
            auto btn = std::make_unique<juce::TextButton>(typeNames[i]);
            btn->setClickingTogglesState(true);
            btn->setRadioGroupId(100);
            btn->setColour(juce::TextButton::buttonOnColourId,  juce::Colours::cyan);
            btn->setColour(juce::TextButton::textColourOnId,    juce::Colours::black);
            btn->setColour(juce::TextButton::buttonColourId,    juce::Colours::darkgrey.darker());
            btn->setColour(juce::TextButton::textColourOffId,   juce::Colours::white.withAlpha(0.5f));
            btn->onClick = [this, i]
            {
                if (currentFx != nullptr)
                    if (auto* pParam = processor.apvts.getParameter(getResolvedID("NsType")))
                        pParam->setValueNotifyingHost(pParam->convertTo0to1(static_cast<float>(i)));
            };
            noiseTypeButtons.push_back(std::move(btn));
            addAndMakeVisible(*noiseTypeButtons.back());
        }

        // ── OTT バンド詳細トグルボタン ────────────────────────────────────
        bandDetailBtn.setButtonText("BANDS");
        bandDetailBtn.setClickingTogglesState(true);
        bandDetailBtn.setColour(juce::TextButton::buttonOnColourId,  juce::Colours::cyan.darker(0.3f));
        bandDetailBtn.setColour(juce::TextButton::textColourOnId,    juce::Colours::white);
        bandDetailBtn.setColour(juce::TextButton::buttonColourId,    juce::Colour(0xff3a3a3a));
        bandDetailBtn.setColour(juce::TextButton::textColourOffId,   juce::Colours::white.withAlpha(0.55f));
        bandDetailBtn.onClick = [this]
        {
            showOttBands = bandDetailBtn.getToggleState();
            setTargetEffect(currentFx);   // バンド/メインビューを切り替え
        };
        addChildComponent(bandDetailBtn);

        // ── OTT バンドセレクター: LOW / MID / HIGH ────────────────────────
        juce::StringArray bandLabels{ "LOW", "MID", "HIGH" };
        for (int i = 0; i < 3; ++i)
        {
            ottBandSelectBtns[i].setButtonText(bandLabels[i]);
            ottBandSelectBtns[i].setClickingTogglesState(true);
            ottBandSelectBtns[i].setRadioGroupId(200);
            ottBandSelectBtns[i].setColour(juce::TextButton::buttonOnColourId,  juce::Colours::cyan);
            ottBandSelectBtns[i].setColour(juce::TextButton::textColourOnId,    juce::Colours::black);
            ottBandSelectBtns[i].setColour(juce::TextButton::buttonColourId,    juce::Colour(0xff2e2e2e));
            ottBandSelectBtns[i].setColour(juce::TextButton::textColourOffId,   juce::Colours::white.withAlpha(0.5f));
            ottBandSelectBtns[i].onClick = [this, i]
            {
                selectedOttBand = i;
                updateOttBandKnobs();
                synchronizeSlidersFromParameters();
                resized();
            };
            addChildComponent(ottBandSelectBtns[i]);
        }
        ottBandSelectBtns[0].setToggleState(true, juce::dontSendNotification);

        // OTT 旧バンドスライダー（非表示専用・ロジック保持）
        juce::StringArray bandPrefixes{ "Low", "Mid", "High" };
        for (int b = 0; b < 3; ++b)
        {
            juce::String bName = bandPrefixes[b];
            setupKnob(ottUpSliders[b],   ottUpLabels[b],   bName + " UP",   0.0,   1.0,   1.0);
            setupKnob(ottDownSliders[b], ottDownLabels[b], bName + " DOWN", 0.0,   1.0,   1.0);
            setupKnob(ottGainSliders[b], ottGainLabels[b], bName + " GAIN", -24.0, 24.0, 0.0);

            ottUpSliders[b].onValueChange = [this, b, bName] {
                if (currentFx)
                    if (auto* pp = processor.apvts.getParameter(getResolvedID("Ott" + bName + "Up")))
                        pp->setValueNotifyingHost(pp->convertTo0to1((float)ottUpSliders[b].getValue()));
            };
            ottDownSliders[b].onValueChange = [this, b, bName] {
                if (currentFx)
                    if (auto* pp = processor.apvts.getParameter(getResolvedID("Ott" + bName + "Down")))
                        pp->setValueNotifyingHost(pp->convertTo0to1((float)ottDownSliders[b].getValue()));
            };
            ottGainSliders[b].onValueChange = [this, b, bName] {
                if (currentFx)
                    if (auto* pp = processor.apvts.getParameter(getResolvedID("Ott" + bName + "Gain")))
                        pp->setValueNotifyingHost(pp->convertTo0to1((float)ottGainSliders[b].getValue()));
            };

            // 旧バンドスライダーは常に非表示
            ottUpSliders[b].setVisible(false);   ottUpLabels[b].setVisible(false);
            ottDownSliders[b].setVisible(false); ottDownLabels[b].setVisible(false);
            ottGainSliders[b].setVisible(false); ottGainLabels[b].setVisible(false);
        }
    }

    ~ParameterDockPanel() override = default;

    // ─────────────────────────────────────────────────────────────────────────
    void setTargetEffect(AudioEffect* newFx)
    {
        currentFx = newFx;

        // ── 全コントロールを初期非表示 ────────────────────────────────────
        slider1.setVisible(false); lbl1.setVisible(false);
        slider2.setVisible(false); lbl2.setVisible(false);
        slider3.setVisible(false); lbl3.setVisible(false);
        slider4.setVisible(false); lbl4.setVisible(false);
        slider5.setVisible(false); lbl5.setVisible(false);
        sliderMix.setVisible(false); lblMix.setVisible(false);
        lblInfo.setVisible(false);
        for (auto& btn : noiseTypeButtons) btn->setVisible(false);
        bandDetailBtn.setVisible(false);
        for (auto& b : ottBandSelectBtns) b.setVisible(false);

        // OTT切り替え時にバンドビューをリセット
        if (!dynamic_cast<OTT_Multiband*>(newFx))
        {
            showOttBands = false;
            bandDetailBtn.setToggleState(false, juce::dontSendNotification);
        }

        if (currentFx == nullptr || !currentFx->isActive())
        {
            currentFx = nullptr;
            lblInfo.setVisible(true);
            return;
        }

        // ── DRY/WET ノブ（全エフェクト共通）──────────────────────────────
        setupKnob(sliderMix, lblMix, "DRY/WET", 0.0, 1.0, (double)currentFx->getMix());
        sliderMix.onValueChange = [this] {
            if (auto* pp = processor.apvts.getParameter(getResolvedID("Mix")))
                pp->setValueNotifyingHost(pp->convertTo0to1((float)sliderMix.getValue()));
        };

        // ── Saturation ────────────────────────────────────────────────────
        if (auto* sat = dynamic_cast<ADAA_Saturation*>(currentFx))
        {
            setupKnob(slider1, lbl1, "DRIVE",     1.0,   16.0,   2.0);
            setupKnob(slider2, lbl2, "ASYMMETRY", 0.0,    1.0,   0.0);
            setupKnob(slider3, lbl3, "OUT TRIM",  -12.0, 12.0,   0.0);
            setupKnob(slider4, lbl4, "PRE HPF",   20.0, 2000.0, 20.0);

            slider1.onValueChange = [this] { setParam("SatDrive", slider1); };
            slider2.onValueChange = [this] { setParam("SatAsym",  slider2); };
            slider3.onValueChange = [this] { setParam("SatTrim",  slider3); };
            slider4.onValueChange = [this] { setParam("SatPre",   slider4); };
            sliderMix.onValueChange = [this] { setParam("SatMix", sliderMix); };
        }
        // ── BitCrusher ────────────────────────────────────────────────────
        else if (auto* crusher = dynamic_cast<BitCrusher*>(currentFx))
        {
            setupKnob(slider1, lbl1, "BITS",       2.0, 24.0, 8.0);
            setupKnob(slider2, lbl2, "DOWNSAMPLE", 1.0, 32.0, 4.0);
            setupKnob(slider3, lbl3, "JITTER",     0.0,  1.0, 0.0);

            slider1.onValueChange = [this] { setParam("BcBits",   slider1); };
            slider2.onValueChange = [this] { setParam("BcDown",   slider2); };
            slider3.onValueChange = [this] { setParam("BcJitter", slider3); };
            sliderMix.onValueChange = [this] { setParam("BcMix",  sliderMix); };
        }
        // ── Noise Generator ───────────────────────────────────────────────
        else if (auto* noise = dynamic_cast<NoiseGenerator*>(currentFx))
        {
            setupKnob(slider1, lbl1, "DECAY ms",  1.0,  1000.0, 100.0);
            setupKnob(slider2, lbl2, "GAIN dB",  -60.0,    0.0,   0.0);
            setupKnob(slider3, lbl3, "ATTACK ms",  0.0,   50.0,   0.0);
            setupKnob(slider4, lbl4, "BP FREQ",    0.0, 4000.0,   0.0);

            slider1.onValueChange = [this] { setParam("NsDecay",  slider1); };
            slider2.onValueChange = [this] { setParam("NsGain",   slider2); };
            slider3.onValueChange = [this] { setParam("NsAttack", slider3); };
            slider4.onValueChange = [this] { setParam("NsBpFreq", slider4); };
            sliderMix.onValueChange = [this] { setParam("NsMix",  sliderMix); };

            for (auto& btn : noiseTypeButtons) btn->setVisible(true);
        }
        // ── OTT Multiband ─────────────────────────────────────────────────
        else if (dynamic_cast<OTT_Multiband*>(currentFx))
        {
            bandDetailBtn.setVisible(true);
            bandDetailBtn.setToggleState(showOttBands, juce::dontSendNotification);

            sliderMix.onValueChange = [this] { setParam("OttDepth", sliderMix); };

            if (!showOttBands)
            {
                // ── メインビュー: TIME / XOVER×2 / GATE ─────────────────
                setupKnob(slider1, lbl1, "TIME",     0.1,   10.0,    1.0);
                setupKnob(slider2, lbl2, "LO/MI XO", 40.0, 1000.0, 200.0);
                setupKnob(slider3, lbl3, "MI/HI XO", 1000.0, 15000.0, 2500.0);
                setupKnob(slider4, lbl4, "GATE dB",  -70.0,  -20.0,  -45.0);

                slider1.onValueChange = [this] { setParam("OttTime",        slider1); };
                slider2.onValueChange = [this] { setParam("OttLowMidXOver", slider2); };
                slider3.onValueChange = [this] { setParam("OttMidHighXOver",slider3); };
                slider4.onValueChange = [this] { setParam("OttGateFloor",   slider4); };
            }
            else
            {
                // ── バンドビュー: LOW / MID / HIGH セレクター + 3ノブ ─────
                for (auto& b : ottBandSelectBtns) b.setVisible(true);
                updateOttBandKnobs();
            }
        }
        // ── Glue Compressor ───────────────────────────────────────────────
        else if (auto* glue = dynamic_cast<GlueCompressor*>(currentFx))
        {
            setupKnob(slider1, lbl1, "THR dBFS",  -40.0,   0.0, -18.0);
            setupKnob(slider2, lbl2, "RATIO",        1.0,  20.0,   2.0);
            setupKnob(slider3, lbl3, "ATK ms",        1.0, 100.0,  30.0);
            setupKnob(slider4, lbl4, "REL ms",       10.0, 1000.0, 200.0);
            setupKnob(slider5, lbl5, "MAKEUP dB",   -12.0,  12.0,   0.0);

            slider1.onValueChange = [this] { setParam("GlueThr",   slider1); };
            slider2.onValueChange = [this] { setParam("GlueRatio", slider2); };
            slider3.onValueChange = [this] { setParam("GlueAtk",   slider3); };
            slider4.onValueChange = [this] { setParam("GlueRel",   slider4); };
            slider5.onValueChange = [this] { setParam("GlueMkp",   slider5); };
            sliderMix.onValueChange = [this] { setParam("GlueDepth", sliderMix); };
        }
        // ── Limiter ───────────────────────────────────────────────────────
        else if (auto* limiter = dynamic_cast<Limiter*>(currentFx))
        {
            setupKnob(slider1, lbl1, "CEILING dB", -24.0, 0.0, -0.1);
            slider1.onValueChange = [this] { setParam("LimCeil", slider1); };
            sliderMix.onValueChange = [this] { setParam("LimMix", sliderMix); };
        }

        synchronizeSlidersFromParameters();
        resized();
        repaint();
    }

    // ─────────────────────────────────────────────────────────────────────────
    void synchronizeSlidersFromParameters() noexcept
    {
        if (currentFx == nullptr) return;

        auto load = [this](const juce::String& id) -> float
        {
            auto* p = processor.apvts.getRawParameterValue(getResolvedID(id));
            return p ? p->load() : 0.0f;
        };

        if (dynamic_cast<ADAA_Saturation*>(currentFx))
        {
            slider1.setValue(load("SatDrive"), juce::dontSendNotification);
            slider2.setValue(load("SatAsym"),  juce::dontSendNotification);
            slider3.setValue(load("SatTrim"),  juce::dontSendNotification);
            slider4.setValue(load("SatPre"),   juce::dontSendNotification);
            sliderMix.setValue(load("SatMix"), juce::dontSendNotification);
        }
        else if (dynamic_cast<BitCrusher*>(currentFx))
        {
            slider1.setValue(load("BcBits"),   juce::dontSendNotification);
            slider2.setValue(load("BcDown"),   juce::dontSendNotification);
            slider3.setValue(load("BcJitter"), juce::dontSendNotification);
            sliderMix.setValue(load("BcMix"),  juce::dontSendNotification);
        }
        else if (dynamic_cast<NoiseGenerator*>(currentFx))
        {
            slider1.setValue(load("NsDecay"),  juce::dontSendNotification);
            slider2.setValue(load("NsGain"),   juce::dontSendNotification);
            slider3.setValue(load("NsAttack"), juce::dontSendNotification);
            slider4.setValue(load("NsBpFreq"), juce::dontSendNotification);
            sliderMix.setValue(load("NsMix"),  juce::dontSendNotification);

            auto* pType = processor.apvts.getRawParameterValue(getResolvedID("NsType"));
            if (pType && noiseTypeButtons.size() >= 4)
            {
                int idx = static_cast<int>(pType->load());
                for (int i = 0; i < 4; ++i)
                    noiseTypeButtons[i]->setToggleState(i == idx, juce::dontSendNotification);
            }
        }
        else if (dynamic_cast<OTT_Multiband*>(currentFx))
        {
            sliderMix.setValue(load("OttDepth"), juce::dontSendNotification);

            if (!showOttBands)
            {
                slider1.setValue(load("OttTime"),         juce::dontSendNotification);
                slider2.setValue(load("OttLowMidXOver"),  juce::dontSendNotification);
                slider3.setValue(load("OttMidHighXOver"), juce::dontSendNotification);
                slider4.setValue(load("OttGateFloor"),    juce::dontSendNotification);
            }
            else
            {
                juce::StringArray bns{ "Low", "Mid", "High" };
                auto bn = bns[selectedOttBand];
                slider1.setValue(load("Ott" + bn + "Up"),   juce::dontSendNotification);
                slider2.setValue(load("Ott" + bn + "Down"), juce::dontSendNotification);
                slider3.setValue(load("Ott" + bn + "Gain"), juce::dontSendNotification);
            }
        }
        else if (dynamic_cast<GlueCompressor*>(currentFx))
        {
            slider1.setValue(load("GlueThr"),    juce::dontSendNotification);
            slider2.setValue(load("GlueRatio"),  juce::dontSendNotification);
            slider3.setValue(load("GlueAtk"),    juce::dontSendNotification);
            slider4.setValue(load("GlueRel"),    juce::dontSendNotification);
            slider5.setValue(load("GlueMkp"),    juce::dontSendNotification);
            sliderMix.setValue(load("GlueDepth"),juce::dontSendNotification);
        }
        else if (dynamic_cast<Limiter*>(currentFx))
        {
            slider1.setValue(load("LimCeil"),   juce::dontSendNotification);
            sliderMix.setValue(load("LimMix"),  juce::dontSendNotification);
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1c1c1c));
        g.setColour(juce::Colours::white.withAlpha(0.07f));
        g.drawRect(getLocalBounds(), 1);

        if (currentFx != nullptr)
        {
            g.setFont(juce::Font(9.5f, juce::Font::bold));
            g.setColour(juce::Colours::cyan.withAlpha(0.7f));
            juce::String title = currentFx->getName().toUpperCase();
            if (dynamic_cast<OTT_Multiband*>(currentFx))
                title += showOttBands ? "  —  BANDS" : "  —  MAIN";
            g.drawText(title, 10, 6, getWidth() - 20, 13, juce::Justification::left);
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    void resized() override
    {
        auto area = getLocalBounds();
        if (lblInfo.isVisible()) { lblInfo.setBounds(area); return; }

        area.removeFromTop(22);
        area.reduce(8, 4);

        const bool isNoise = (currentFx && dynamic_cast<NoiseGenerator*>(currentFx));
        const bool isOtt   = (currentFx && dynamic_cast<OTT_Multiband*>(currentFx));
        const bool isGlue  = (currentFx && dynamic_cast<GlueCompressor*>(currentFx));

        // Glue は6ノブ(5+mix)、それ以外は5ノブ(4+mix) でkwを計算
        const int totalCols = isGlue ? 6 : 5;
        const int kw = area.getWidth() / totalCols;

        // DRY/WET ノブは常に右端
        if (sliderMix.isVisible())
        {
            auto mx = area.removeFromRight(kw);
            lblMix.setBounds(mx.removeFromTop(15));
            sliderMix.setBounds(mx);
        }

        // ── Noise ──────────────────────────────────────────────────────────
        if (isNoise && noiseTypeButtons.size() >= 4 && noiseTypeButtons[0]->isVisible())
        {
            const int btnH = 18;
            auto topRow = area.removeFromTop(btnH);
            const int bw = topRow.getWidth() / 4;
            for (int i = 0; i < 4; ++i)
                noiseTypeButtons[i]->setBounds(topRow.getX() + i * bw, topRow.getY(), bw - 2, btnH);

            auto placeKnob = [&](juce::Slider& s, juce::Label& l) {
                if (!s.isVisible()) return;
                auto c = area.removeFromLeft(kw); l.setBounds(c.removeFromTop(15)); s.setBounds(c);
            };
            placeKnob(slider1, lbl1); placeKnob(slider2, lbl2);
            placeKnob(slider3, lbl3); placeKnob(slider4, lbl4);
        }
        // ── OTT メインビュー ───────────────────────────────────────────────
        else if (isOtt && !showOttBands)
        {
            // [TIME][LO/MI][MI/HI][GATE] → 各 kw 幅, 残りに BANDS ボタン
            auto placeKnob = [&](juce::Slider& s, juce::Label& l) {
                if (!s.isVisible()) return;
                auto c = area.removeFromLeft(kw); l.setBounds(c.removeFromTop(15)); s.setBounds(c);
            };
            placeKnob(slider1, lbl1); placeKnob(slider2, lbl2);
            placeKnob(slider3, lbl3); placeKnob(slider4, lbl4);
            if (bandDetailBtn.isVisible())
                bandDetailBtn.setBounds(area.reduced(4, 18));
        }
        // ── OTT バンドビュー ──────────────────────────────────────────────
        else if (isOtt && showOttBands)
        {
            // [LOW/MID/HIGH ボタン縦積み] [UP][DOWN][GAIN] [BANDS▴]
            auto selCol = area.removeFromLeft(kw);
            const int sh = selCol.getHeight() / 3;
            for (int i = 0; i < 3; ++i)
                ottBandSelectBtns[i].setBounds(selCol.removeFromTop(sh).reduced(2, 3));

            auto placeKnob = [&](juce::Slider& s, juce::Label& l) {
                if (!s.isVisible()) return;
                auto c = area.removeFromLeft(kw); l.setBounds(c.removeFromTop(15)); s.setBounds(c);
            };
            placeKnob(slider1, lbl1); placeKnob(slider2, lbl2); placeKnob(slider3, lbl3);
            if (bandDetailBtn.isVisible())
                bandDetailBtn.setBounds(area.reduced(4, 18));
        }
        // ── Glue: 5ノブ横1列 ─────────────────────────────────────────────
        else if (isGlue)
        {
            auto placeKnob = [&](juce::Slider& s, juce::Label& l) {
                if (!s.isVisible()) return;
                auto c = area.removeFromLeft(kw); l.setBounds(c.removeFromTop(15)); s.setBounds(c);
            };
            placeKnob(slider1, lbl1); placeKnob(slider2, lbl2); placeKnob(slider3, lbl3);
            placeKnob(slider4, lbl4); placeKnob(slider5, lbl5);
        }
        // ── その他 (Sat / Crusher / Limiter): ノブ横1列 ─────────────────
        else
        {
            auto placeKnob = [&](juce::Slider& s, juce::Label& l) {
                if (!s.isVisible()) return;
                auto c = area.removeFromLeft(kw); l.setBounds(c.removeFromTop(15)); s.setBounds(c);
            };
            placeKnob(slider1, lbl1); placeKnob(slider2, lbl2);
            placeKnob(slider3, lbl3); placeKnob(slider4, lbl4);
        }
    }

private:
    // ── ヘルパー ──────────────────────────────────────────────────────────────
    juce::String getResolvedID(const juce::String& base) const noexcept
    {
        if (currentFx == nullptr) return {};
        auto r = currentFx->getTargetRoute();
        juce::String pre = (r == TargetRoute::Transient) ? "trans" :
                           (r == TargetRoute::Tonal)     ? "tonal" : "full";
        return pre + base;
    }

    void setParam(const juce::String& id, juce::Slider& s) noexcept
    {
        if (auto* pp = processor.apvts.getParameter(getResolvedID(id)))
            pp->setValueNotifyingHost(pp->convertTo0to1((float)s.getValue()));
    }

    void setupKnob(juce::Slider& s, juce::Label& l,
                   const juce::String& name,
                   double minV, double maxV, double defV)
    {
        addAndMakeVisible(s); addAndMakeVisible(l);
        s.setVisible(true);  l.setVisible(true);

        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 52, 13);
        s.setRange(minV, maxV, 0.01);
        s.setValue(defV, juce::dontSendNotification);
        s.setColour(juce::Slider::rotarySliderFillColourId, juce::Colours::cyan);
        s.setColour(juce::Slider::thumbColourId,            juce::Colours::white);

        l.setText(name, juce::dontSendNotification);
        l.setFont(juce::Font(8.5f, juce::Font::bold));
        l.setJustificationType(juce::Justification::centred);
        l.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.55f));
    }

    // OTT バンドビュー: 選択バンドの knob を slider1/2/3 に割り当て
    void updateOttBandKnobs()
    {
        if (currentFx == nullptr) return;

        juce::StringArray bns{ "Low", "Mid", "High" };
        const juce::String bn = bns[selectedOttBand];

        setupKnob(slider1, lbl1, "UP",      0.0, 1.0,    1.0);
        setupKnob(slider2, lbl2, "DOWN",    0.0, 1.0,    1.0);
        setupKnob(slider3, lbl3, "GAIN dB", -24.0, 24.0, 0.0);

        slider4.setVisible(false); lbl4.setVisible(false);

        slider1.onValueChange = [this, bn] { setParam("Ott" + bn + "Up",   slider1); };
        slider2.onValueChange = [this, bn] { setParam("Ott" + bn + "Down", slider2); };
        slider3.onValueChange = [this, bn] { setParam("Ott" + bn + "Gain", slider3); };

        // 現在値をロード
        auto load = [this](const juce::String& id) -> float {
            auto* p = processor.apvts.getRawParameterValue(getResolvedID(id));
            return p ? p->load() : 0.0f;
        };
        slider1.setValue(load("Ott" + bn + "Up"),   juce::dontSendNotification);
        slider2.setValue(load("Ott" + bn + "Down"),  juce::dontSendNotification);
        slider3.setValue(load("Ott" + bn + "Gain"),  juce::dontSendNotification);
    }

    // ── メンバー ──────────────────────────────────────────────────────────────
    AnatomyAudioProcessor& processor;
    AudioEffect* currentFx = nullptr;

    juce::Label  lblInfo;
    juce::Slider slider1, slider2, slider3, slider4, slider5, sliderMix;
    juce::Label  lbl1, lbl2, lbl3, lbl4, lbl5, lblMix;

    std::vector<std::unique_ptr<juce::TextButton>> noiseTypeButtons;

    // OTT バンド詳細UI
    juce::TextButton bandDetailBtn;
    juce::TextButton ottBandSelectBtns[3];
    bool showOttBands    = false;
    int  selectedOttBand = 0;      // 0=Low, 1=Mid, 2=High

    // 旧バンドスライダー（常時非表示・APVTS接続保持用）
    juce::Slider ottUpSliders[3];   juce::Label ottUpLabels[3];
    juce::Slider ottDownSliders[3]; juce::Label ottDownLabels[3];
    juce::Slider ottGainSliders[3]; juce::Label ottGainLabels[3];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParameterDockPanel)
};
