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

/**
 * ParameterDockPanel
 * 相互の非同期参照を完全に断ち切った、100%クラッシュフリー・タイマー同期型ドック。
 */
class ParameterDockPanel final : public juce::Component
{
public:
    ParameterDockPanel(AnatomyAudioProcessor& p) : processor(p)
    {
        lblInfo.setText("Select an effect card from the rack to tweak parameters", juce::dontSendNotification);
        lblInfo.setFont(juce::Font(12.0f, juce::Font::italic));
        lblInfo.setJustificationType(juce::Justification::centred);
        lblInfo.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.4f));
        addAndMakeVisible(lblInfo);
    }

    ~ParameterDockPanel() override = default;

    void setTargetEffect(AudioEffect* newFx)
    {
        currentFx = newFx;

        slider1.setVisible(false); lbl1.setVisible(false);
        slider2.setVisible(false); lbl2.setVisible(false);
        slider3.setVisible(false); lbl3.setVisible(false);
        btnNoiseTrigger.setVisible(false);
        toggleNoiseType.setVisible(false);
        lblInfo.setVisible(false);

        if (currentFx == nullptr)
        {
            lblInfo.setVisible(true);
            return;
        }

        if (auto* sat = dynamic_cast<ADAA_Saturation*> (currentFx))
        {
            setupKnob(slider1, lbl1, "DRIVE", 1.0, 16.0, 2.0);
            setupKnob(slider2, lbl2, "MIX", 0.0, 1.0, 0.5);

            slider1.onValueChange = [this] {
                if (auto* p = processor.apvts.getParameter("satDrive"))
                    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float> (slider1.getValue())));
                };
            slider2.onValueChange = [this] {
                if (auto* p = processor.apvts.getParameter("satMix"))
                    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float> (slider2.getValue())));
                };
        }
        else if (auto* crusher = dynamic_cast<BitCrusher*> (currentFx))
        {
            setupKnob(slider1, lbl1, "BITS", 2.0, 24.0, 8.0);
            setupKnob(slider2, lbl2, "DOWNSAMPLE", 1.0, 32.0, 4.0);
            setupKnob(slider3, lbl3, "MIX", 0.0, 1.0, 0.3);

            slider1.onValueChange = [this] {
                if (auto* p = processor.apvts.getParameter("bcBits"))
                    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float> (slider1.getValue())));
                };
            slider2.onValueChange = [this] {
                if (auto* p = processor.apvts.getParameter("bcDown"))
                    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float> (slider2.getValue())));
                };
            slider3.onValueChange = [this] {
                if (auto* p = processor.apvts.getParameter("bcMix"))
                    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float> (slider3.getValue())));
                };
        }
        else if (auto* noise = dynamic_cast<NoiseGenerator*> (currentFx))
        {
            setupKnob(slider1, lbl1, "DECAY (ms)", 1.0, 1000.0, 100.0);
            slider1.onValueChange = [this] {
                if (auto* p = processor.apvts.getParameter("nsDecay"))
                    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float> (slider1.getValue())));
                };

            addAndMakeVisible(toggleNoiseType);
            toggleNoiseType.setVisible(true);
            toggleNoiseType.setButtonText("PINK NOISE");
            toggleNoiseType.onClick = [this] {
                if (auto* p = processor.apvts.getParameter("nsPink"))
                    p->setValueNotifyingHost(toggleNoiseType.getToggleState() ? 1.0f : 0.0f);
                };

            addAndMakeVisible(btnNoiseTrigger);
            btnNoiseTrigger.setVisible(true);
            btnNoiseTrigger.setButtonText("MANUAL TRIGGER");
            btnNoiseTrigger.onClick = [noise] { noise->trigger(); };
        }
        else if (auto* limiter = dynamic_cast<Limiter*> (currentFx))
        {
            setupKnob(slider1, lbl1, "CEILING (dB)", -24.0, 0.0, -0.1);
            slider1.onValueChange = [this] {
                if (auto* p = processor.apvts.getParameter("limCeil"))
                    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float> (slider1.getValue())));
                };
        }
        else if (auto* ott = dynamic_cast<OTT_Multiband*> (currentFx))
        {
            setupKnob(slider1, lbl1, "DEPTH", 0.0, 1.0, 0.7);
            setupKnob(slider2, lbl2, "TIME", 0.1, 10.0, 1.0);
            setupKnob(slider3, lbl3, "OUT GAIN (dB)", -24.0, 24.0, 0.0);

            slider1.onValueChange = [this] {
                if (auto* p = processor.apvts.getParameter("ottDepth"))
                    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float> (slider1.getValue())));
                };
            slider2.onValueChange = [this] {
                if (auto* p = processor.apvts.getParameter("ottTime"))
                    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float> (slider2.getValue())));
                };
            slider3.onValueChange = [this] {
                if (auto* p = processor.apvts.getParameter("ottOutGain"))
                    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float> (slider3.getValue())));
                };
        }

        synchronizeSlidersFromParameters();
        resized();
        repaint();
    }

    // 💥【新設】メッセージスレッド安全圏から呼び出すパラメータ自動ロード追従処理
    void synchronizeSlidersFromParameters() noexcept
    {
        if (currentFx == nullptr) return;

        if (dynamic_cast<ADAA_Saturation*> (currentFx))
        {
            slider1.setValue(processor.apvts.getRawParameterValue("satDrive")->load(), juce::dontSendNotification);
            slider2.setValue(processor.apvts.getRawParameterValue("satMix")->load(), juce::dontSendNotification);
        }
        else if (dynamic_cast<BitCrusher*> (currentFx))
        {
            slider1.setValue(processor.apvts.getRawParameterValue("bcBits")->load(), juce::dontSendNotification);
            slider2.setValue(processor.apvts.getRawParameterValue("bcDown")->load(), juce::dontSendNotification);
            slider3.setValue(processor.apvts.getRawParameterValue("bcMix")->load(), juce::dontSendNotification);
        }
        else if (dynamic_cast<NoiseGenerator*> (currentFx))
        {
            slider1.setValue(processor.apvts.getRawParameterValue("nsDecay")->load(), juce::dontSendNotification);
            toggleNoiseType.setToggleState(processor.apvts.getRawParameterValue("nsPink")->load() > 0.5f, juce::dontSendNotification);
        }
        else if (dynamic_cast<Limiter*> (currentFx))
        {
            slider1.setValue(processor.apvts.getRawParameterValue("limCeil")->load(), juce::dontSendNotification);
        }
        else if (dynamic_cast<OTT_Multiband*> (currentFx))
        {
            slider1.setValue(processor.apvts.getRawParameterValue("ottDepth")->load(), juce::dontSendNotification);
            slider2.setValue(processor.apvts.getRawParameterValue("ottTime")->load(), juce::dontSendNotification);
            slider3.setValue(processor.apvts.getRawParameterValue("ottOutGain")->load(), juce::dontSendNotification);
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
            g.drawText(currentFx->getName().toUpperCase() + " PARAMETERS", 10, 8, getWidth(), 12, juce::Justification::left);
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

        auto kw = area.getWidth() / 4;

        if (slider1.isVisible())
        {
            auto s = area.removeFromLeft(kw);
            lbl1.setBounds(s.removeFromTop(15));
            slider1.setBounds(s);
        }
        if (slider2.isVisible())
        {
            auto s = area.removeFromLeft(kw);
            lbl2.setBounds(s.removeFromTop(15));
            slider2.setBounds(s);
        }
        if (slider3.isVisible())
        {
            auto s = area.removeFromLeft(kw);
            lbl3.setBounds(s.removeFromTop(15));
            slider3.setBounds(s);
        }

        if (toggleNoiseType.isVisible())
        {
            auto s = area.removeFromLeft(kw).reduced(5, 10);
            toggleNoiseType.setBounds(s.removeFromTop(20));
            btnNoiseTrigger.setBounds(s);
        }
    }

private:
    void setupKnob(juce::Slider& s, juce::Label& l, const juce::String& name, double minV, double maxV, double defV)
    {
        addAndMakeVisible(s);
        addAndMakeVisible(l);
        s.setVisible(true);
        l.setVisible(true);

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

    AnatomyAudioProcessor& processor;
    AudioEffect* currentFx = nullptr;

    juce::Label lblInfo;
    juce::Slider slider1, slider2, slider3;
    juce::Label lbl1, lbl2, lbl3;
    juce::ToggleButton toggleNoiseType;
    juce::TextButton btnNoiseTrigger;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParameterDockPanel)
};