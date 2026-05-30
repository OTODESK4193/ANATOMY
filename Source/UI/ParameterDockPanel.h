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
 * 💥【15面展開独立IDに完全追従】クリックされたカードの所属レーン（Route）を自動判別し、
 * APVTS内の該当プレフィックスIDへノブを瞬時に動的再バインドする高精度パラメータドック。
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
        toggleNoiseType.setVisible(false);
        lblInfo.setVisible(false);

        if (currentFx == nullptr || !currentFx->isActive())
        {
            currentFx = nullptr;
            lblInfo.setVisible(true);
            return;
        }

        // 💥所属レーンに応じた独立APVTSパラメータIDを動的に解決
        setupKnob(sliderMix, lblMix, "DRY / WET", 0.0, 1.0, static_cast<double>(currentFx->getMix()));
        sliderMix.onValueChange = [this] {
            if (auto* p = processor.apvts.getParameter(getResolvedID("Mix")))
                p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(sliderMix.getValue())));
            };

        if (auto* sat = dynamic_cast<ADAA_Saturation*>(currentFx))
        {
            setupKnob(slider1, lbl1, "DRIVE", 1.0, 16.0, 2.0);
            setupKnob(slider2, lbl2, "ASYMMETRY", 0.0, 1.0, 0.0);

            slider1.onValueChange = [this] {
                if (auto* p = processor.apvts.getParameter(getResolvedID("SatDrive")))
                    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(slider1.getValue())));
                };
            slider2.onValueChange = [this] {
                if (auto* p = processor.apvts.getParameter(getResolvedID("SatAsym")))
                    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(slider2.getValue())));
                };
            sliderMix.onValueChange = [this] {
                if (auto* p = processor.apvts.getParameter(getResolvedID("SatMix")))
                    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(sliderMix.getValue())));
                };
        }
        else if (auto* crusher = dynamic_cast<BitCrusher*>(currentFx))
        {
            setupKnob(slider1, lbl1, "BITS", 2.0, 24.0, 8.0);
            setupKnob(slider2, lbl2, "DOWNSAMPLE", 1.0, 32.0, 4.0);
            setupKnob(slider3, lbl3, "JITTER", 0.0, 1.0, 0.0);

            slider1.onValueChange = [this] {
                if (auto* p = processor.apvts.getParameter(getResolvedID("BcBits")))
                    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(slider1.getValue())));
                };
            slider2.onValueChange = [this] {
                if (auto* p = processor.apvts.getParameter(getResolvedID("BcDown")))
                    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(slider2.getValue())));
                };
            slider3.onValueChange = [this] {
                if (auto* p = processor.apvts.getParameter(getResolvedID("BcJitter")))
                    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(slider3.getValue())));
                };
            sliderMix.onValueChange = [this] {
                if (auto* p = processor.apvts.getParameter(getResolvedID("BcMix")))
                    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(sliderMix.getValue())));
                };
        }
        else if (auto* noise = dynamic_cast<NoiseGenerator*>(currentFx))
        {
            setupKnob(slider1, lbl1, "ENV DECAY (ms)", 1.0, 1000.0, 100.0);
            setupKnob(slider2, lbl2, "GAIN (dB)", -60.0, 0.0, 0.0); // 💥Noise専用独立音量ノブの完全敷設

            slider1.onValueChange = [this] {
                if (auto* p = processor.apvts.getParameter(getResolvedID("NsDecay")))
                    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(slider1.getValue())));
                };
            slider2.onValueChange = [this] {
                if (auto* p = processor.apvts.getParameter(getResolvedID("NsGain")))
                    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(slider2.getValue())));
                };

            addAndMakeVisible(toggleNoiseType);
            toggleNoiseType.setVisible(true);
            toggleNoiseType.setButtonText("PINK NOISE");
            toggleNoiseType.onClick = [this] {
                if (auto* p = processor.apvts.getParameter(getResolvedID("NsPink")))
                    p->setValueNotifyingHost(toggleNoiseType.getToggleState() ? 1.0f : 0.0f);
                };

            sliderMix.onValueChange = [this] {
                if (auto* p = processor.apvts.getParameter(getResolvedID("NsMix")))
                    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(sliderMix.getValue())));
                };
        }
        else if (auto* limiter = dynamic_cast<Limiter*>(currentFx))
        {
            setupKnob(slider1, lbl1, "CEILING (dB)", -24.0, 0.0, -0.1);
            slider1.onValueChange = [this] {
                if (auto* p = processor.apvts.getParameter(getResolvedID("LimCeil")))
                    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(slider1.getValue())));
                };
            sliderMix.onValueChange = [this] {
                if (auto* p = processor.apvts.getParameter(getResolvedID("LimMix")))
                    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(sliderMix.getValue())));
                };
        }
        else if (auto* ott = dynamic_cast<OTT_Multiband*>(currentFx))
        {
            setupKnob(slider1, lbl1, "TIME MULTI", 0.1, 10.0, 1.0);
            setupKnob(slider2, lbl2, "OUT GAIN (dB)", -24.0, 24.0, 0.0);
            setupKnob(slider3, lbl3, "X-OVER FREQ", 100.0, 1000.0, 200.0);

            slider1.onValueChange = [this] {
                if (auto* p = processor.apvts.getParameter(getResolvedID("OttTime")))
                    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(slider1.getValue())));
                };
            slider2.onValueChange = [this] {
                if (auto* p = processor.apvts.getParameter(getResolvedID("OttOutGain")))
                    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(slider2.getValue())));
                };
            slider3.onValueChange = [this] {
                if (auto* p = processor.apvts.getParameter(getResolvedID("OttXOver")))
                    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(slider3.getValue())));
                };
            sliderMix.onValueChange = [this] {
                if (auto* p = processor.apvts.getParameter(getResolvedID("OttDepth")))
                    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(sliderMix.getValue())));
                };
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
            toggleNoiseType.setToggleState(processor.apvts.getRawParameterValue(getResolvedID("NsPink"))->load() > 0.5f, juce::dontSendNotification);
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
            slider2.setValue(processor.apvts.getRawParameterValue(getResolvedID("OttOutGain"))->load(), juce::dontSendNotification);
            slider3.setValue(processor.apvts.getRawParameterValue(getResolvedID("OttXOver"))->load(), juce::dontSendNotification);
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

        auto kw = area.getWidth() / 5;

        auto mixArea = area.removeFromRight(kw);
        lblMix.setBounds(mixArea.removeFromTop(15));
        sliderMix.setBounds(mixArea);

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
        if (slider4.isVisible())
        {
            auto s = area.removeFromLeft(kw);
            lbl4.setBounds(s.removeFromTop(15));
            slider4.setBounds(s);
        }

        if (toggleNoiseType.isVisible())
        {
            auto s = area.removeFromLeft(kw).reduced(5, 12);
            toggleNoiseType.setBounds(s);
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
    juce::Slider slider1, slider2, slider3, slider4, sliderMix;
    juce::Label lbl1, lbl2, lbl3, lbl4, lblMix;
    juce::ToggleButton toggleNoiseType;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParameterDockPanel)
};