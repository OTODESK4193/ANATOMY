#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "../PluginProcessor.h"
#include "../DSP/Effects/AudioEffect.h"
#include "../DSP/Effects/ADAA_Saturation.h"
#include "../DSP/Effects/BitCrusher.h"
#include "../DSP/Effects/NoiseGenerator.h"
#include "../DSP/Effects/OTT_Multiband.h"
#include "../DSP/Effects/Limiter.h"
#include <vector>
#include <memory>
#include <algorithm>

class EffectCardComponent final : public juce::Component
{
public:
    EffectCardComponent(AnatomyAudioProcessor& p, AudioEffect* fxInstance, std::function<void(TargetRoute)> onRouteChangedCallback)
        : processor(p), fx(fxInstance), onRouteChanged(onRouteChangedCallback)
    {
        jassert(fx != nullptr);

        lblTitle.setText(fx->getName(), juce::dontSendNotification);
        lblTitle.setFont(juce::Font(11.0f, juce::Font::bold));
        lblTitle.setJustificationType(juce::Justification::centredLeft);
        lblTitle.setColour(juce::Label::textColourId, juce::Colours::cyan);
        addAndMakeVisible(lblTitle);

        auto configureRouteButton = [this](juce::TextButton& b, const juce::String& text, int id) {
            b.setButtonText(text);
            b.setRadioGroupId(99);
            b.setClickingTogglesState(true);
            b.setColour(juce::TextButton::buttonOnColourId, juce::Colours::cyan);
            b.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
            b.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey.darker());
            b.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.6f));
            b.onClick = [this, id] { if (onRouteChanged) onRouteChanged(static_cast<TargetRoute> (id)); };
            addAndMakeVisible(b);
            };

        configureRouteButton(btnRouteTrans, "TRANS", 0);
        configureRouteButton(btnRouteTonal, "TONAL", 1);
        configureRouteButton(btnRouteFull, "FULL", 2);

        updateRouteButtonStates();

        if (auto* sat = dynamic_cast<ADAA_Saturation*> (fx))
        {
            setupKnob(sliderParam1, lblParam1, "DRIVE", 1.0, 16.0, 2.0);
            setupKnob(sliderParam2, lblParam2, "MIX", 0.0, 1.0, 0.5);

            sliderParam1.onValueChange = [this] {
                if (auto* param = processor.apvts.getParameter("satDrive"))
                    param->setValueNotifyingHost(param->convertTo0to1(static_cast<float> (sliderParam1.getValue())));
                };
            sliderParam2.onValueChange = [this] {
                if (auto* param = processor.apvts.getParameter("satMix"))
                    param->setValueNotifyingHost(param->convertTo0to1(static_cast<float> (sliderParam2.getValue())));
                };
        }
        else if (auto* crusher = dynamic_cast<BitCrusher*> (fx))
        {
            setupKnob(sliderParam1, lblParam1, "BITS", 2.0, 24.0, 8.0);
            setupKnob(sliderParam2, lblParam2, "DOWNS", 1.0, 32.0, 4.0);
            setupKnob(sliderParam3, lblParam3, "MIX", 0.0, 1.0, 0.3);

            sliderParam1.onValueChange = [this] {
                if (auto* param = processor.apvts.getParameter("bcBits"))
                    param->setValueNotifyingHost(param->convertTo0to1(static_cast<float> (sliderParam1.getValue())));
                };
            sliderParam2.onValueChange = [this] {
                if (auto* param = processor.apvts.getParameter("bcDown"))
                    param->setValueNotifyingHost(param->convertTo0to1(static_cast<float> (sliderParam2.getValue())));
                };
            sliderParam3.onValueChange = [this] {
                if (auto* param = processor.apvts.getParameter("bcMix"))
                    param->setValueNotifyingHost(param->convertTo0to1(static_cast<float> (sliderParam3.getValue())));
                };
        }
        else if (auto* noise = dynamic_cast<NoiseGenerator*> (fx))
        {
            setupKnob(sliderParam1, lblParam1, "DECAY", 1.0, 1000.0, 100.0);
            addAndMakeVisible(toggleNoiseType);
            toggleNoiseType.setButtonText("PINK");
            toggleNoiseType.setColour(juce::ToggleButton::textColourId, juce::Colours::white);

            sliderParam1.onValueChange = [this] {
                if (auto* param = processor.apvts.getParameter("nsDecay"))
                    param->setValueNotifyingHost(param->convertTo0to1(static_cast<float> (sliderParam1.getValue())));
                };
            toggleNoiseType.onClick = [this] {
                if (auto* param = processor.apvts.getParameter("nsPink"))
                    param->setValueNotifyingHost(toggleNoiseType.getToggleState() ? 1.0f : 0.0f);
                };
        }
        else if (auto* limiter = dynamic_cast<Limiter*> (fx))
        {
            setupKnob(sliderParam1, lblParam1, "CEIL", -24.0, 0.0, -0.1);

            sliderParam1.onValueChange = [this] {
                if (auto* param = processor.apvts.getParameter("limCeil"))
                    param->setValueNotifyingHost(param->convertTo0to1(static_cast<float> (sliderParam1.getValue())));
                };
        }
        else if (auto* ott = dynamic_cast<OTT_Multiband*> (fx))
        {
            setupKnob(sliderParam1, lblParam1, "DEPTH", 0.0, 1.0, 0.7);
            setupKnob(sliderParam2, lblParam2, "TIME", 0.1, 10.0, 1.0);
            setupKnob(sliderParam3, lblParam3, "GAIN", -24.0, 24.0, 0.0);

            sliderParam1.onValueChange = [this] {
                if (auto* param = processor.apvts.getParameter("ottDepth"))
                    param->setValueNotifyingHost(param->convertTo0to1(static_cast<float> (sliderParam1.getValue())));
                };
            sliderParam2.onValueChange = [this] {
                if (auto* param = processor.apvts.getParameter("ottTime"))
                    param->setValueNotifyingHost(param->convertTo0to1(static_cast<float> (sliderParam2.getValue())));
                };
            sliderParam3.onValueChange = [this] {
                if (auto* param = processor.apvts.getParameter("ottOutGain"))
                    param->setValueNotifyingHost(param->convertTo0to1(static_cast<float> (sliderParam3.getValue())));
                };
        }
    }

    ~EffectCardComponent() override = default;

    void updateRouteButtonStates()
    {
        const auto currentRoute = fx->getTargetRoute();
        btnRouteTrans.setToggleState(currentRoute == TargetRoute::Transient, juce::dontSendNotification);
        btnRouteTonal.setToggleState(currentRoute == TargetRoute::Tonal, juce::dontSendNotification);
        btnRouteFull.setToggleState(currentRoute == TargetRoute::FullMix, juce::dontSendNotification);
    }

    void synchronizeSlidersWithParameters() noexcept
    {
        if (auto* sat = dynamic_cast<ADAA_Saturation*> (fx))
        {
            sliderParam1.setValue(processor.apvts.getRawParameterValue("satDrive")->load(), juce::dontSendNotification);
            sliderParam2.setValue(processor.apvts.getRawParameterValue("satMix")->load(), juce::dontSendNotification);
        }
        else if (auto* crusher = dynamic_cast<BitCrusher*> (fx))
        {
            sliderParam1.setValue(processor.apvts.getRawParameterValue("bcBits")->load(), juce::dontSendNotification);
            sliderParam2.setValue(processor.apvts.getRawParameterValue("bcDown")->load(), juce::dontSendNotification);
            sliderParam3.setValue(processor.apvts.getRawParameterValue("bcMix")->load(), juce::dontSendNotification);
        }
        else if (auto* noise = dynamic_cast<NoiseGenerator*> (fx))
        {
            sliderParam1.setValue(processor.apvts.getRawParameterValue("nsDecay")->load(), juce::dontSendNotification);
            toggleNoiseType.setToggleState(processor.apvts.getRawParameterValue("nsPink")->load() > 0.5f, juce::dontSendNotification);
        }
        else if (auto* limiter = dynamic_cast<Limiter*> (fx))
        {
            sliderParam1.setValue(processor.apvts.getRawParameterValue("limCeil")->load(), juce::dontSendNotification);
        }
        else if (auto* ott = dynamic_cast<OTT_Multiband*> (fx))
        {
            sliderParam1.setValue(processor.apvts.getRawParameterValue("ottDepth")->load(), juce::dontSendNotification);
            sliderParam2.setValue(processor.apvts.getRawParameterValue("ottTime")->load(), juce::dontSendNotification);
            sliderParam3.setValue(processor.apvts.getRawParameterValue("ottOutGain")->load(), juce::dontSendNotification);
        }
    }

    AudioEffect* getEffect() const noexcept { return fx; }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::darkgrey.darker().darker());
        g.setColour(juce::Colours::cyan.withAlpha(0.4f));
        g.drawRect(getLocalBounds(), 1);

        g.setColour(juce::Colours::cyan.withAlpha(0.2f));
        g.fillRect(0, 0, 15, getHeight());
        g.setColour(juce::Colours::white.withAlpha(0.3f));
        for (int i = 4; i < getHeight(); i += 6)
        {
            g.fillEllipse(5, i, 2, 2);
            g.fillEllipse(9, i, 2, 2);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromLeft(20);

        auto topArea = area.removeFromTop(24);
        lblTitle.setBounds(topArea.removeFromLeft(120));

        auto routeArea = topArea.removeFromRight(150);
        auto w = routeArea.getWidth() / 3;
        btnRouteTrans.setBounds(routeArea.removeFromLeft(w).reduced(1));
        btnRouteTonal.setBounds(routeArea.removeFromLeft(w).reduced(1));
        btnRouteFull.setBounds(routeArea.reduced(1));

        area.removeFromTop(4);

        auto knobArea = area;
        const int numKnobs = 5;
        auto kw = knobArea.getWidth() / numKnobs;

        auto s1 = knobArea.removeFromLeft(kw);
        lblParam1.setBounds(s1.removeFromTop(12));
        sliderParam1.setBounds(s1);

        auto s2 = knobArea.removeFromLeft(kw);
        lblParam2.setBounds(s2.removeFromTop(12));
        sliderParam2.setBounds(s2);

        auto s3 = knobArea.removeFromLeft(kw);
        lblParam3.setBounds(s3.removeFromTop(12));
        sliderParam3.setBounds(s3);

        if (toggleNoiseType.isVisible())
        {
            toggleNoiseType.setBounds(knobArea.removeFromLeft(kw).reduced(2));
        }
    }

    void mouseDown(const juce::MouseEvent& /*e*/) override
    {
        if (onCardSelectedCallback)
            onCardSelectedCallback(fx);
    }

    void mouseDrag(const juce::MouseEvent& /*e*/) override
    {
        if (auto* dragContainer = juce::DragAndDropContainer::findParentDragContainerFor(this))
        {
            if (!dragContainer->isDragAndDropActive())
            {
                auto ptrValue = reinterpret_cast<juce::int64> (this);
                dragContainer->startDragging(juce::var(ptrValue), this);
            }
        }
    }

    // 💥【修正完了】構文エラー箇所：テンプレート引数の閉じブラケットを正しい位置へアライメント
    std::function<void(AudioEffect*)> onCardSelectedCallback;

private:
    void setupKnob(juce::Slider& s, juce::Label& l, const juce::String& name, double minV, double maxV, double defV)
    {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 40, 12);
        s.setRange(minV, maxV, 0.01);
        s.setValue(defV, juce::dontSendNotification);
        s.setColour(juce::Slider::rotarySliderFillColourId, juce::Colours::cyan);
        s.setColour(juce::Slider::thumbColourId, juce::Colours::white);
        addAndMakeVisible(s);

        l.setText(name, juce::dontSendNotification);
        l.setFont(juce::Font(8.0f, juce::Font::bold));
        l.setJustificationType(juce::Justification::centred);
        l.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.5f));
        addAndMakeVisible(l);
    }

    AnatomyAudioProcessor& processor;
    AudioEffect* fx;
    std::function<void(TargetRoute)> onRouteChanged;

    juce::Label lblTitle;
    juce::TextButton btnRouteTrans;
    juce::TextButton btnRouteTonal;
    juce::TextButton btnRouteFull;

    juce::Slider sliderParam1, sliderParam2, sliderParam3;
    juce::Label lblParam1, lblParam2, lblParam3;
    juce::ToggleButton toggleNoiseType;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectCardComponent)
};

class EffectRackPanel final : public juce::Component,
    public juce::DragAndDropTarget,
    public juce::ChangeBroadcaster
{
public:
    EffectRackPanel(AnatomyAudioProcessor& p) : processor(p)
    {
        masterFXPool.push_back(std::make_unique<ADAA_Saturation>());
        masterFXPool.push_back(std::make_unique<BitCrusher>());
        masterFXPool.push_back(std::make_unique<NoiseGenerator>());
        masterFXPool.push_back(std::make_unique<OTT_Multiband>());
        masterFXPool.push_back(std::make_unique<Limiter>());

        for (auto& fx : masterFXPool)
        {
            fx->prepare(processor.getSampleRate(), processor.getBlockSize());
            fx->setTargetRoute(TargetRoute::FullMix);
        }

        rebuildCardComponents();
        pushChainsToProcessor();
    }

    ~EffectRackPanel() override = default;

    void updateCardSlidersFromParameters() noexcept
    {
        for (auto* card : activeCards)
        {
            card->synchronizeSlidersWithParameters();
        }
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black.withAlpha(0.5f));

        auto area = getLocalBounds();
        auto sectionHeight = getHeight() / 3;

        g.setColour(juce::Colours::white.withAlpha(0.1f));
        g.drawHorizontalLine(sectionHeight, 0.0f, static_cast<float> (getWidth()));
        g.drawHorizontalLine(sectionHeight * 2, 0.0f, static_cast<float> (getWidth()));

        g.setFont(12.0f);
        g.setColour(juce::Colours::white.withAlpha(0.4f));
        g.drawText("TRANSIENT FX SLOT (D&D REORDERABLE)", 15, 5, getWidth(), 15, juce::Justification::left);
        g.drawText("SUSTAIN TONAL FX SLOT (D&D REORDERABLE)", 15, sectionHeight + 5, getWidth(), 15, juce::Justification::left);
        g.drawText("FULL MIX MASTER FX SLOT (D&D REORDERABLE)", 15, (sectionHeight * 2) + 5, getWidth(), 15, juce::Justification::left);
    }

    void resized() override
    {
        auto sectionHeight = getHeight() / 3;
        int transCount = 0; int tonalCount = 0; int fullCount = 0;
        const int cardH = 55;

        for (auto* card : activeCards)
        {
            const auto route = card->getEffect()->getTargetRoute();
            if (route == TargetRoute::Transient)
            {
                card->setBounds(10, 22 + (transCount * (cardH + 4)), getWidth() - 20, cardH);
                transCount++;
            }
            else if (route == TargetRoute::Tonal)
            {
                card->setBounds(10, sectionHeight + 22 + (tonalCount * (cardH + 4)), getWidth() - 20, cardH);
                tonalCount++;
            }
            else if (route == TargetRoute::FullMix)
            {
                card->setBounds(10, (sectionHeight * 2) + 22 + (fullCount * (cardH + 4)), getWidth() - 20, cardH);
                fullCount++;
            }
        }
    }

    bool isInterestedInDragSource(const DragAndDropTarget::SourceDetails& /*dragSourceDetails*/) override { return true; }
    void itemDragEnter(const DragAndDropTarget::SourceDetails&) override { repaint(); }
    void itemDragMove(const DragAndDropTarget::SourceDetails&) override {}
    void itemDragExit(const DragAndDropTarget::SourceDetails&) override { repaint(); }

    void itemDropped(const DragAndDropTarget::SourceDetails& dragSourceDetails) override
    {
        juce::var ptrDescription = dragSourceDetails.description;
        juce::int64 ptrVal = ptrDescription;
        auto* droppedCard = reinterpret_cast<EffectCardComponent*> (ptrVal);
        if (droppedCard == nullptr) return;

        auto dropY = dragSourceDetails.localPosition.getY();
        auto sectionHeight = getHeight() / 3;

        TargetRoute targetRoute = TargetRoute::FullMix;
        if (dropY < sectionHeight)         targetRoute = TargetRoute::Transient;
        else if (dropY < sectionHeight * 2)        targetRoute = TargetRoute::Tonal;

        auto* targetFx = droppedCard->getEffect();
        targetFx->setTargetRoute(targetRoute);

        activeCards.removeObject(droppedCard, false);

        int insertIndex = activeCards.size();
        for (int i = 0; i < activeCards.size(); ++i)
        {
            if (activeCards[i]->getEffect()->getTargetRoute() == targetRoute)
            {
                auto cardCenterY = activeCards[i]->getY() + (activeCards[i]->getHeight() / 2);
                if (dropY < cardCenterY) { insertIndex = i; break; }
            }
        }

        if (insertIndex >= activeCards.size()) activeCards.add(droppedCard);
        else                                   activeCards.insert(insertIndex, droppedCard);

        droppedCard->updateRouteButtonStates();

        pushChainsToProcessor();
        sendChangeMessage();

        repaint();
        resized();
    }

    AudioEffect* getSelectedEffect() const noexcept { return currentSelectedFX; }

private:
    int getEffectTypeIndex(AudioEffect* fx) const noexcept
    {
        if (dynamic_cast<ADAA_Saturation*> (fx))  return 0;
        if (dynamic_cast<BitCrusher*> (fx))      return 1;
        if (dynamic_cast<NoiseGenerator*> (fx))  return 2;
        if (dynamic_cast<OTT_Multiband*> (fx))   return 3;
        if (dynamic_cast<Limiter*> (fx))         return 4;
        return -1;
    }

    void rebuildCardComponents()
    {
        activeCards.clear();
        for (auto& fx : masterFXPool)
        {
            auto* card = new EffectCardComponent(processor, fx.get(), [this, fxPtr = fx.get()](TargetRoute newRoute) {
                fxPtr->setTargetRoute(newRoute);
                pushChainsToProcessor();
                sendChangeMessage();
                repaint();
                resized();
                });

            card->onCardSelectedCallback = [this](AudioEffect* clickedFx) {
                currentSelectedFX = clickedFx;
                sendChangeMessage();
                };

            addAndMakeVisible(card);
            activeCards.add(card);
        }
    }

    void pushChainsToProcessor()
    {
        std::vector<int> transOrder;
        std::vector<int> tonalOrder;
        std::vector<int> fullMixOrder;

        for (auto* card : activeCards)
        {
            auto* fx = card->getEffect();
            auto route = fx->getTargetRoute();
            int typeIdx = getEffectTypeIndex(fx);

            if (typeIdx != -1)
            {
                if (route == TargetRoute::Transient)     transOrder.push_back(typeIdx);
                else if (route == TargetRoute::Tonal)    tonalOrder.push_back(typeIdx);
                else if (route == TargetRoute::FullMix)  fullMixOrder.push_back(typeIdx);
            }
        }

        processor.updateRouteOrder(TargetRoute::Transient, transOrder);
        processor.updateRouteOrder(TargetRoute::Tonal, tonalOrder);
        processor.updateRouteOrder(TargetRoute::FullMix, fullMixOrder);
    }

    AnatomyAudioProcessor& processor;
    std::vector<std::unique_ptr<AudioEffect>> masterFXPool;
    juce::OwnedArray<EffectCardComponent> activeCards;
    AudioEffect* currentSelectedFX = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectRackPanel)
};