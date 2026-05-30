#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../PluginProcessor.h"
#include "../DSP/Effects/AudioEffect.h"
#include "../DSP/Effects/ADAA_Saturation.h"
#include "../DSP/Effects/BitCrusher.h"
#include "../DSP/Effects/NoiseGenerator.h"
#include "../DSP/Effects/OTT_Multiband.h"
#include "../DSP/Effects/Limiter.h"
#include <vector>
#include <memory>

/**
 * ParameterDockPanel (Phase 2 Ultimate Edition)
 * OTT選択時に丸型ノブから「横型スライダーマトリクス」へ動的形状変貌を遂げ、
 * 狭小な縦幅制限の中で12個の全深層ダイナミクスパラメータを完璧に描画するプロフェッショナルドック。
 */
class ParameterDockPanel final : public juce::Component
{
public:
    ParameterDockPanel(AnatomyAudioProcessor& p) : processor(p)
    {
        lblInfo.setText("Select an active effect card from the rack to tweak parameters", juce::dontSendNotification);
        lblInfo.setFont(juce::Font(12.0f, juce::Font::italic));
        lblInfo.setJustificationType(juce::Justification::centred);
        lblInfo.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.4f));
        addAndMakeVisible(lblInfo);

        // Noise Generator専用 4連相互点灯ボタン
        juce::StringArray typeNames{ "WHITE", "PINK", "BROWN", "BLUE" };
        for (int i = 0; i < 4; ++i)
        {
            auto btn = std::make_unique<juce::TextButton>(typeNames[i]);
            btn->setClickingTogglesState(true);
            btn->setRadioGroupId(100);
            btn->setColour(juce::TextButton::buttonOnColourId, juce::Colours::cyan);
            btn->setColour(juce::TextButton::textColourOnId, juce::Colours::black);
            btn->setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey.darker());
            btn->setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.5f));

            btn->onClick = [this, i] {
                if (currentFx != nullptr)
                {
                    if (auto* pParam = processor.apvts.getParameter(getResolvedID("NsType")))
                        pParam->setValueNotifyingHost(pParam->convertTo0to1(static_cast<float>(i)));
                }
                };

            noiseTypeButtons.push_back(std::move(btn));
            addAndMakeVisible(*noiseTypeButtons.back());
        }

        // 自作OTT専用：9連独立Dynamicsスライダー＆ラベルの生成マウント
        juce::StringArray bandPrefixes{ "Low", "Mid", "High" };
        for (int b = 0; b < 3; ++b)
        {
            juce::String bName = bandPrefixes[b];

            // 💥 狭小縦幅対応：初期設定として横型フェーダー形式で鉄壁マウント
            setupHorizontalSlider(ottUpSliders[b], ottUpLabels[b], bName.toUpperCase() + " UP", 0.0, 1.0, 1.0);
            setupHorizontalSlider(ottDownSliders[b], ottDownLabels[b], bName.toUpperCase() + " DOWN", 0.0, 1.0, 1.0);
            setupHorizontalSlider(ottGainSliders[b], ottGainLabels[b], bName.toUpperCase() + " GAIN", -24.0, 24.0, 0.0);

            ottUpSliders[b].onValueChange = [this, b, bName] {
                if (currentFx != nullptr) {
                    if (auto* pParam = processor.apvts.getParameter(getResolvedID("Ott" + bName + "Up")))
                        pParam->setValueNotifyingHost(pParam->convertTo0to1(static_cast<float>(ottUpSliders[b].getValue())));
                }
                };
            ottDownSliders[b].onValueChange = [this, b, bName] {
                if (currentFx != nullptr) {
                    if (auto* pParam = processor.apvts.getParameter(getResolvedID("Ott" + bName + "Down")))
                        pParam->setValueNotifyingHost(pParam->convertTo0to1(static_cast<float>(ottDownSliders[b].getValue())));
                }
                };
            ottGainSliders[b].onValueChange = [this, b, bName] {
                if (currentFx != nullptr) {
                    if (auto* pParam = processor.apvts.getParameter(getResolvedID("Ott" + bName + "Gain")))
                        pParam->setValueNotifyingHost(pParam->convertTo0to1(static_cast<float>(ottGainSliders[b].getValue())));
                }
                };
        }
    }

    ~ParameterDockPanel() override = default;

    void setTargetEffect(AudioEffect* newFx)
    {
        currentFx = newFx;

        slider1.setVisible(false); lbl1.setVisible(false);
        slider2.setVisible(false); lbl2.setVisible(false);
        slider3.setVisible(false); lbl3.setVisible(false);
        slider4.setVisible(false); lbl4.setVisible(false);
        sliderMix.setVisible(false); lblMix.setVisible(false);
        lblInfo.setVisible(false);
        for (auto& btn : noiseTypeButtons) btn->setVisible(false);

        for (int b = 0; b < 3; ++b)
        {
            ottUpSliders[b].setVisible(false);   ottUpLabels[b].setVisible(false);
            ottDownSliders[b].setVisible(false); ottDownLabels[b].setVisible(false);
            ottGainSliders[b].setVisible(false); ottGainLabels[b].setVisible(false);
        }

        if (currentFx == nullptr || !currentFx->isActive())
        {
            currentFx = nullptr;
            lblInfo.setVisible(true);
            return;
        }

        // DRY/WETは十分なスペースがあるため、常に美しい丸型（Rotary）ノブで統一保持
        setupKnob(sliderMix, lblMix, "DRY / WET", 0.0, 1.0, static_cast<double>(currentFx->getMix()));
        sliderMix.onValueChange = [this] {
            if (auto* pParam = processor.apvts.getParameter(getResolvedID("Mix")))
                pParam->setValueNotifyingHost(pParam->convertTo0to1(static_cast<float>(sliderMix.getValue())));
            };

        if (auto* sat = dynamic_cast<ADAA_Saturation*>(currentFx))
        {
            setupKnob(slider1, lbl1, "DRIVE", 1.0, 16.0, 2.0);
            setupKnob(slider2, lbl2, "ASYMMETRY", 0.0, 1.0, 0.0);

            slider1.onValueChange = [this] {
                if (auto* pParam = processor.apvts.getParameter(getResolvedID("SatDrive")))
                    pParam->setValueNotifyingHost(pParam->convertTo0to1(static_cast<float>(slider1.getValue())));
                };
            slider2.onValueChange = [this] {
                if (auto* pParam = processor.apvts.getParameter(getResolvedID("SatAsym")))
                    pParam->setValueNotifyingHost(pParam->convertTo0to1(static_cast<float>(slider2.getValue())));
                };
            sliderMix.onValueChange = [this] {
                if (auto* pParam = processor.apvts.getParameter(getResolvedID("SatMix")))
                    pParam->setValueNotifyingHost(pParam->convertTo0to1(static_cast<float>(sliderMix.getValue())));
                };
        }
        else if (auto* crusher = dynamic_cast<BitCrusher*>(currentFx))
        {
            setupKnob(slider1, lbl1, "BITS", 2.0, 24.0, 8.0);
            setupKnob(slider2, lbl2, "DOWNSAMPLE", 1.0, 32.0, 4.0);
            setupKnob(slider3, lbl3, "JITTER", 0.0, 1.0, 0.0);

            slider1.onValueChange = [this] {
                if (auto* pParam = processor.apvts.getParameter(getResolvedID("BcBits")))
                    pParam->setValueNotifyingHost(pParam->convertTo0to1(static_cast<float>(slider1.getValue())));
                };
            slider2.onValueChange = [this] {
                if (auto* pParam = processor.apvts.getParameter(getResolvedID("BcDown")))
                    pParam->setValueNotifyingHost(pParam->convertTo0to1(static_cast<float>(slider2.getValue())));
                };
            slider3.onValueChange = [this] {
                if (auto* pParam = processor.apvts.getParameter(getResolvedID("BcJitter")))
                    pParam->setValueNotifyingHost(pParam->convertTo0to1(static_cast<float>(slider3.getValue())));
                };
            sliderMix.onValueChange = [this] {
                if (auto* pParam = processor.apvts.getParameter(getResolvedID("BcMix")))
                    pParam->setValueNotifyingHost(pParam->convertTo0to1(static_cast<float>(sliderMix.getValue())));
                };
        }
        else if (auto* noise = dynamic_cast<NoiseGenerator*>(currentFx))
        {
            setupKnob(slider1, lbl1, "ENV DECAY (ms)", 1.0, 1000.0, 100.0);
            setupKnob(slider2, lbl2, "GAIN (dB)", -60.0, 0.0, 0.0);

            slider1.onValueChange = [this] {
                if (auto* pParam = processor.apvts.getParameter(getResolvedID("NsDecay")))
                    pParam->setValueNotifyingHost(pParam->convertTo0to1(static_cast<float>(slider1.getValue())));
                };
            slider2.onValueChange = [this] {
                if (auto* pParam = processor.apvts.getParameter(getResolvedID("NsGain")))
                    pParam->setValueNotifyingHost(pParam->convertTo0to1(static_cast<float>(slider2.getValue())));
                };

            for (auto& btn : noiseTypeButtons) btn->setVisible(true);

            sliderMix.onValueChange = [this] {
                if (auto* pParam = processor.apvts.getParameter(getResolvedID("NsMix")))
                    pParam->setValueNotifyingHost(pParam->convertTo0to1(static_cast<float>(sliderMix.getValue())));
                };
        }
        else if (auto* limiter = dynamic_cast<Limiter*>(currentFx))
        {
            setupKnob(slider1, lbl1, "CEILING (dB)", -24.0, 0.0, -0.1);
            slider1.onValueChange = [this] {
                if (auto* pParam = processor.apvts.getParameter(getResolvedID("LimCeil")))
                    pParam->setValueNotifyingHost(pParam->convertTo0to1(static_cast<float>(slider1.getValue())));
                };
            sliderMix.onValueChange = [this] {
                if (auto* pParam = processor.apvts.getParameter(getResolvedID("LimMix")))
                    pParam->setValueNotifyingHost(pParam->convertTo0to1(static_cast<float>(sliderMix.getValue())));
                };
        }
        else if (dynamic_cast<OTT_Multiband*>(currentFx))
        {
            // 💥【超重要】OTT選択時のみ、左側の3つの親スライダーも狭い縦幅（28px）に耐えるよう「横型フェーダー」へトポロジー変貌を命令
            setupHorizontalSlider(slider1, lbl1, "TIME MULTI", 0.1, 10.0, 1.0);
            setupHorizontalSlider(slider2, lbl2, "LOW/MID X-OVER", 40.0, 1000.0, 200.0);
            setupHorizontalSlider(slider3, lbl3, "MID/HIGH X-OVER", 1000.0, 15000.0, 2500.0);

            slider1.onValueChange = [this] {
                if (auto* pParam = processor.apvts.getParameter(getResolvedID("OttTime")))
                    pParam->setValueNotifyingHost(pParam->convertTo0to1(static_cast<float>(slider1.getValue())));
                };
            slider2.onValueChange = [this] {
                if (auto* pParam = processor.apvts.getParameter(getResolvedID("OttLowMidXOver")))
                    pParam->setValueNotifyingHost(pParam->convertTo0to1(static_cast<float>(slider2.getValue())));
                };
            slider3.onValueChange = [this] {
                if (auto* pParam = processor.apvts.getParameter(getResolvedID("OttMidHighXOver")))
                    pParam->setValueNotifyingHost(pParam->convertTo0to1(static_cast<float>(slider3.getValue())));
                };
            sliderMix.onValueChange = [this] {
                if (auto* pParam = processor.apvts.getParameter(getResolvedID("OttDepth")))
                    pParam->setValueNotifyingHost(pParam->convertTo0to1(static_cast<float>(sliderMix.getValue())));
                };

            for (int b = 0; b < 3; ++b)
            {
                ottUpSliders[b].setVisible(true);   ottUpLabels[b].setVisible(true);
                ottDownSliders[b].setVisible(true); ottDownLabels[b].setVisible(true);
                ottGainSliders[b].setVisible(true); ottGainLabels[b].setVisible(true);
            }
        }

        synchronizeSlidersFromParameters();
        resized();
        repaint();
    }

    void synchronizeSlidersFromParameters() noexcept
    {
        if (currentFx == nullptr) return;

        if (dynamic_cast<ADAA_Saturation*>(currentFx))
        {
            slider1.setValue(processor.apvts.getRawParameterValue(getResolvedID("SatDrive"))->load(), juce::dontSendNotification);
            sliderMix.setValue(processor.apvts.getRawParameterValue(getResolvedID("SatMix"))->load(), juce::dontSendNotification);
            slider2.setValue(processor.apvts.getRawParameterValue(getResolvedID("SatAsym"))->load(), juce::dontSendNotification);
        }
        else if (dynamic_cast<BitCrusher*>(currentFx))
        {
            slider1.setValue(processor.apvts.getRawParameterValue(getResolvedID("BcBits"))->load(), juce::dontSendNotification);
            slider2.setValue(processor.apvts.getRawParameterValue(getResolvedID("BcDown"))->load(), juce::dontSendNotification);
            sliderMix.setValue(processor.apvts.getRawParameterValue(getResolvedID("BcMix"))->load(), juce::dontSendNotification);
            slider3.setValue(processor.apvts.getRawParameterValue(getResolvedID("BcJitter"))->load(), juce::dontSendNotification);
        }
        else if (dynamic_cast<NoiseGenerator*>(currentFx))
        {
            slider1.setValue(processor.apvts.getRawParameterValue(getResolvedID("NsDecay"))->load(), juce::dontSendNotification);
            sliderMix.setValue(processor.apvts.getRawParameterValue(getResolvedID("NsMix"))->load(), juce::dontSendNotification);
            slider2.setValue(processor.apvts.getRawParameterValue(getResolvedID("NsGain"))->load(), juce::dontSendNotification);

            auto* pParam = processor.apvts.getRawParameterValue(getResolvedID("NsType"));
            if (pParam != nullptr && noiseTypeButtons.size() >= 4)
            {
                int typeIdx = static_cast<int>(pParam->load());
                for (int i = 0; i < 4; ++i)
                {
                    noiseTypeButtons[i]->setToggleState(i == typeIdx, juce::dontSendNotification);
                }
            }
        }
        else if (dynamic_cast<Limiter*>(currentFx))
        {
            slider1.setValue(processor.apvts.getRawParameterValue(getResolvedID("LimCeil"))->load(), juce::dontSendNotification);
            sliderMix.setValue(processor.apvts.getRawParameterValue(getResolvedID("LimMix"))->load(), juce::dontSendNotification);
        }
        else if (dynamic_cast<OTT_Multiband*>(currentFx))
        {
            sliderMix.setValue(processor.apvts.getRawParameterValue(getResolvedID("OttDepth"))->load(), juce::dontSendNotification);
            slider1.setValue(processor.apvts.getRawParameterValue(getResolvedID("OttTime"))->load(), juce::dontSendNotification);
            slider2.setValue(processor.apvts.getRawParameterValue(getResolvedID("OttLowMidXOver"))->load(), juce::dontSendNotification);
            slider3.setValue(processor.apvts.getRawParameterValue(getResolvedID("OttMidHighXOver"))->load(), juce::dontSendNotification);

            juce::StringArray bandNames{ "Low", "Mid", "High" };
            for (int b = 0; b < 3; ++b)
            {
                ottUpSliders[b].setValue(processor.apvts.getRawParameterValue(getResolvedID("Ott" + bandNames[b] + "Up"))->load(), juce::dontSendNotification);
                ottDownSliders[b].setValue(processor.apvts.getRawParameterValue(getResolvedID("Ott" + bandNames[b] + "Down"))->load(), juce::dontSendNotification);
                ottGainSliders[b].setValue(processor.apvts.getRawParameterValue(getResolvedID("Ott" + bandNames[b] + "Gain"))->load(), juce::dontSendNotification);
            }
        }
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::darkgrey.darker().darker().darker());
        g.setColour(juce::Colours::cyan.withAlpha(0.3f));
        g.drawRect(getLocalBounds(), 1);

        if (currentFx != nullptr)
        {
            g.setFont(juce::Font(10.0f, juce::Font::bold));
            g.setColour(juce::Colours::cyan);
            g.drawText(currentFx->getName().toUpperCase() + " PRO-DYNAMIC MATRIX", 10, 8, getWidth(), 12, juce::Justification::left);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        if (lblInfo.isVisible())
        {
            lblInfo.setBounds(area);
            return;
        }

        area.removeFromTop(25);
        area.reduce(10, 5);

        auto kw = area.getWidth() / 5;

        // DRY/WETノブは右端固定
        auto mixArea = area.removeFromRight(kw);
        lblMix.setBounds(mixArea.removeFromTop(15));
        sliderMix.setBounds(mixArea);

        bool isNoiseActive = (currentFx != nullptr && dynamic_cast<NoiseGenerator*>(currentFx) != nullptr);
        bool isOttActive = (currentFx != nullptr && dynamic_cast<OTT_Multiband*>(currentFx) != nullptr);

        if (isOttActive)
        {
            // 残りの左側4/5幅スペースをベースコア（左列スタック）とDynamicsタワー（3列）へ分割配置
            auto leftCoreArea = area.removeFromLeft(kw);
            auto rh = leftCoreArea.getHeight() / 3;

            // 💥【超高密度マウント】上にテキストラベル、下に横型フェーダー本体を綺麗に2分割スタック
            auto r0 = leftCoreArea.removeFromTop(rh);
            lbl1.setBounds(r0.removeFromTop(11)); slider1.setBounds(r0.reduced(2, 0));
            auto r1 = leftCoreArea.removeFromTop(rh);
            lbl2.setBounds(r1.removeFromTop(11)); slider2.setBounds(r1.reduced(2, 0));
            auto r2 = leftCoreArea;
            lbl3.setBounds(r2.removeFromTop(11)); slider3.setBounds(r2.reduced(2, 0));

            // 残ったスペースを3等分してLOW / MID / HIGHタワーを綺麗にタイト配置
            auto towerWidth = area.getWidth() / 3;
            for (int b = 0; b < 3; ++b)
            {
                auto bandArea = area.removeFromLeft(towerWidth).reduced(6, 0);
                auto th = bandArea.getHeight() / 3;

                auto t0 = bandArea.removeFromTop(th);
                ottUpLabels[b].setBounds(t0.removeFromTop(11)); ottUpSliders[b].setBounds(t0.reduced(2, 0));
                auto t1 = bandArea.removeFromTop(th);
                ottDownLabels[b].setBounds(t1.removeFromTop(11)); ottDownSliders[b].setBounds(t1.reduced(2, 0));
                auto t2 = bandArea;
                ottGainLabels[b].setBounds(t2.removeFromTop(11)); ottGainSliders[b].setBounds(t2.reduced(2, 0));
            }
        }
        else if (isNoiseActive && noiseTypeButtons.size() >= 4 && noiseTypeButtons[0]->isVisible())
        {
            if (slider1.isVisible()) { auto s = area.removeFromLeft(kw); lbl1.setBounds(s.removeFromTop(15)); slider1.setBounds(s); }
            if (slider2.isVisible()) { auto s = area.removeFromLeft(kw); lbl2.setBounds(s.removeFromTop(15)); slider2.setBounds(s); }

            auto btnArea = area.removeFromLeft(kw * 2).reduced(5, 5);
            auto hw = btnArea.getWidth() / 2; auto hh = btnArea.getHeight() / 2;
            noiseTypeButtons[0]->setBounds(btnArea.getX(), btnArea.getY(), hw - 2, hh - 2);
            noiseTypeButtons[1]->setBounds(btnArea.getX() + hw, btnArea.getY(), hw - 2, hh - 2);
            noiseTypeButtons[2]->setBounds(btnArea.getX(), btnArea.getY() + hh, hw - 2, hh - 2);
            noiseTypeButtons[3]->setBounds(btnArea.getX() + hw, btnArea.getY() + hh, hw - 2, hh - 2);
        }
        else
        {
            if (slider1.isVisible()) { auto s = area.removeFromLeft(kw); lbl1.setBounds(s.removeFromTop(15)); slider1.setBounds(s); }
            if (slider2.isVisible()) { auto s = area.removeFromLeft(kw); lbl2.setBounds(s.removeFromTop(15)); slider2.setBounds(s); }
            if (slider3.isVisible()) { auto s = area.removeFromLeft(kw); lbl3.setBounds(s.removeFromTop(15)); slider3.setBounds(s); }
            if (slider4.isVisible()) { auto s = area.removeFromLeft(kw); lbl4.setBounds(s.removeFromTop(15)); slider4.setBounds(s); }
        }
    }

private:
    juce::String getResolvedID(const juce::String& baseName) const noexcept
    {
        if (currentFx == nullptr) return {};
        auto r = currentFx->getTargetRoute();
        juce::String prefix = (r == TargetRoute::Transient) ? "trans" : ((r == TargetRoute::Tonal) ? "tonal" : "full");
        return prefix + baseName;
    }

    void setupKnob(juce::Slider& s, juce::Label& l, const juce::String& name, double minV, double maxV, double defV)
    {
        addAndMakeVisible(s); addAndMakeVisible(l);
        s.setVisible(true);  l.setVisible(true);

        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 14);
        s.setRange(minV, maxV, 0.01);
        s.setValue(defV, juce::dontSendNotification);
        s.setColour(juce::Slider::rotarySliderFillColourId, juce::Colours::cyan);
        s.setColour(juce::Slider::thumbColourId, juce::Colours::white);

        l.setText(name, juce::dontSendNotification);
        l.setFont(juce::Font(9.0f, juce::Font::bold));
        l.setJustificationType(juce::Justification::centred);
        l.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
    }

    // 💥【新設】超省スペース環境でも100%美しく描画される横型フェーダー専用の初期化関数
    void setupHorizontalSlider(juce::Slider& s, juce::Label& l, const juce::String& name, double minV, double maxV, double defV)
    {
        addAndMakeVisible(s); addAndMakeVisible(l);
        s.setVisible(true);  l.setVisible(true);

        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 11); // 右側に数値をコンパクト配置
        s.setRange(minV, maxV, 0.01);
        s.setValue(defV, juce::dontSendNotification);
        s.setColour(juce::Slider::trackColourId, juce::Colours::cyan.withAlpha(0.3f));
        s.setColour(juce::Slider::thumbColourId, juce::Colours::cyan);

        l.setText(name, juce::dontSendNotification);
        l.setFont(juce::Font(8.5f, juce::Font::bold));
        l.setJustificationType(juce::Justification::centredLeft);
        l.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.5f));
    }

    AnatomyAudioProcessor& processor;
    AudioEffect* currentFx = nullptr;

    juce::Label lblInfo;
    juce::Slider slider1, slider2, slider3, slider4, sliderMix;
    juce::Label lbl1, lbl2, lbl3, lbl4, lblMix;

    std::vector<std::unique_ptr<juce::TextButton>> noiseTypeButtons;

    // 自作OTT専用プロマトリクスパーツコンテナ [0=Low, 1=Mid, 2=High]
    juce::Slider ottUpSliders[3];   juce::Label ottUpLabels[3];
    juce::Slider ottDownSliders[3]; juce::Label ottDownLabels[3];
    juce::Slider ottGainSliders[3]; juce::Label ottGainLabels[3];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParameterDockPanel)
};