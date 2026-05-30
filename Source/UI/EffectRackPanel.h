#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "../PluginProcessor.h"
#include "../DSP/Effects/AudioEffect.h"
#include <vector>
#include <memory>
#include <algorithm>
#include <functional>

/**
 * EffectCardComponent
 * 縦幅半分（26px）にスリム化され、名称、On/Off、格納（X）ボタンのみで構成された
 * 依存関係のない純粋なラックカードUI。
 */
class EffectCardComponent final : public juce::Component
{
public:
    EffectCardComponent(AudioEffect* fxInstance,
        int typeIdx,
        TargetRoute r,
        std::function<void()> onToggleCallback,
        std::function<void()> onRemoveCallback)
        : fx(fxInstance), effectTypeIndex(typeIdx), currentRoute(r),
        onToggle(onToggleCallback), onRemove(onRemoveCallback)
    {
        jassert(fx != nullptr);

        lblTitle.setText(fx->getName(), juce::dontSendNotification);
        lblTitle.setFont(juce::Font(11.0f, juce::Font::bold));
        lblTitle.setJustificationType(juce::Justification::centredLeft);
        lblTitle.setColour(juce::Label::textColourId, juce::Colours::cyan);
        addAndMakeVisible(lblTitle);

        btnActive.setButtonText("ON");
        btnActive.setClickingTogglesState(true);
        btnActive.setToggleState(fx->isActive(), juce::dontSendNotification);
        btnActive.setColour(juce::TextButton::buttonOnColourId, juce::Colours::cyan);
        btnActive.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
        btnActive.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey.darker());
        btnActive.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.4f));
        btnActive.onClick = [this] {
            if (fx != nullptr)
            {
                fx->setActive(btnActive.getToggleState());
                if (onToggle) onToggle();
            }
            };
        addAndMakeVisible(btnActive);

        btnRemove.setButtonText("X");
        btnRemove.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        btnRemove.setColour(juce::TextButton::textColourOffId, juce::Colours::red.withAlpha(0.6f));
        btnRemove.setColour(juce::TextButton::textColourOnId, juce::Colours::red);
        btnRemove.onClick = [this] {
            if (onRemove) onRemove();
            };
        addAndMakeVisible(btnRemove);
    }

    ~EffectCardComponent() override = default;

    void updateActiveStates() noexcept
    {
        if (fx != nullptr)
        {
            btnActive.setToggleState(fx->isActive(), juce::dontSendNotification);
        }
    }

    AudioEffect* getEffect() const noexcept { return fx; }
    int getTypeIndex() const noexcept { return effectTypeIndex; }
    TargetRoute getRoute() const noexcept { return currentRoute; }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::darkgrey.darker().darker());
        g.setColour(juce::Colours::cyan.withAlpha(0.3f));
        g.drawRect(getLocalBounds(), 1);

        g.setColour(juce::Colours::white.withAlpha(0.15f));
        g.fillRect(0, 0, 12, getHeight());
        g.setColour(juce::Colours::white.withAlpha(0.3f));
        for (int i = 4; i < getHeight() - 2; i += 4)
        {
            g.fillEllipse(3, i, 2, 2);
            g.fillEllipse(6, i, 2, 2);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromLeft(16);

        btnRemove.setBounds(area.removeFromRight(18).reduced(1));
        area.removeFromRight(4);
        btnActive.setBounds(area.removeFromRight(36).reduced(1));

        area.removeFromRight(8);
        lblTitle.setBounds(area);
    }

    void mouseDown(const juce::MouseEvent& /*e*/) override
    {
        if (onCardSelectedCallback && fx != nullptr)
            onCardSelectedCallback(fx);
    }

    void mouseDrag(const juce::MouseEvent& /*e*/) override
    {
        if (auto* dragContainer = juce::DragAndDropContainer::findParentDragContainerFor(this))
        {
            if (!dragContainer->isDragAndDropActive())
            {
                auto ptrValue = reinterpret_cast<juce::int64>(this);
                dragContainer->startDragging(juce::var(ptrValue), this);
            }
        }
    }

    std::function<void(AudioEffect*)> onCardSelectedCallback;

private:
    AudioEffect* fx;
    int effectTypeIndex;
    TargetRoute currentRoute;
    std::function<void()> onToggle;
    std::function<void()> onRemove;

    juce::Label lblTitle;
    juce::TextButton btnActive;
    juce::TextButton btnRemove;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectCardComponent)
};

/**
 * EffectRackPanel
 * 各エリアの出現・格納インデックス配列、および追加コンボボックスを統括し、
 * 重複配置および縦幅半分ソートロジックを完全駆動させるメインコンテナクラス。
 */
class EffectRackPanel final : public juce::Component,
    public juce::DragAndDropTarget,
    public juce::ChangeBroadcaster
{
public:
    EffectRackPanel(AnatomyAudioProcessor& p) : processor(p)
    {
        auto setupCombo = [](juce::ComboBox& c) {
            c.addItem("+ Saturation", 1);
            c.addItem("+ Bitcrusher", 2);
            c.addItem("+ Noise Gen", 3);
            c.addItem("+ OTT", 4);
            c.addItem("+ Limiter", 5);
            c.setText("[ + Add FX ]", juce::dontSendNotification);
            c.setColour(juce::ComboBox::backgroundColourId, juce::Colours::darkgrey.darker());
            c.setColour(juce::ComboBox::outlineColourId, juce::Colours::cyan.withAlpha(0.3f));
            c.setColour(juce::ComboBox::textColourId, juce::Colours::cyan);
            };

        addAndMakeVisible(transAddCombo);
        setupCombo(transAddCombo);
        transAddCombo.onChange = [this] {
            int id = transAddCombo.getSelectedId();
            if (id > 0) {
                int idx = id - 1;
                if (std::find(transActiveIndices.begin(), transActiveIndices.end(), idx) == transActiveIndices.end()) {
                    transActiveIndices.push_back(idx);
                    processor.updateRouteOrder(TargetRoute::Transient, transActiveIndices);
                    rebuildCardComponents();
                    sendChangeMessage();
                }
                transAddCombo.setText("[ + Add FX ]", juce::dontSendNotification);
            }
            };

        addAndMakeVisible(tonalAddCombo);
        setupCombo(tonalAddCombo);
        tonalAddCombo.onChange = [this] {
            int id = tonalAddCombo.getSelectedId();
            if (id > 0) {
                int idx = id - 1;
                if (std::find(tonalActiveIndices.begin(), tonalActiveIndices.end(), idx) == tonalActiveIndices.end()) {
                    tonalActiveIndices.push_back(idx);
                    processor.updateRouteOrder(TargetRoute::Tonal, tonalActiveIndices);
                    rebuildCardComponents();
                    sendChangeMessage();
                }
                tonalAddCombo.setText("[ + Add FX ]", juce::dontSendNotification);
            }
            };

        addAndMakeVisible(fullMixAddCombo);
        setupCombo(fullMixAddCombo);
        fullMixAddCombo.onChange = [this] {
            int id = fullMixAddCombo.getSelectedId();
            if (id > 0) {
                int idx = id - 1;
                if (std::find(fullMixActiveIndices.begin(), fullMixActiveIndices.end(), idx) == fullMixActiveIndices.end()) {
                    fullMixActiveIndices.push_back(idx);
                    processor.updateRouteOrder(TargetRoute::FullMix, fullMixActiveIndices);
                    rebuildCardComponents();
                    sendChangeMessage();
                }
                fullMixAddCombo.setText("[ + Add FX ]", juce::dontSendNotification);
            }
            };

        rebuildCardComponents();
    }

    ~EffectRackPanel() override = default;

    void updateCardSlidersFromParameters() noexcept
    {
        for (auto* card : activeCards)
        {
            card->updateActiveStates();
        }
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black.withAlpha(0.5f));

        auto sectionHeight = getHeight() / 3;

        g.setColour(juce::Colours::white.withAlpha(0.1f));
        g.drawHorizontalLine(sectionHeight, 0.0f, static_cast<float>(getWidth()));
        g.drawHorizontalLine(sectionHeight * 2, 0.0f, static_cast<float>(getWidth()));

        g.setFont(11.0f);
        g.setColour(juce::Colours::white.withAlpha(0.4f));
        g.drawText("TRANSIENT FX SLOT (D&D REORDERABLE)", 10, 5, getWidth(), 15, juce::Justification::left);
        g.drawText("SUSTAIN TONAL FX SLOT (D&D REORDERABLE)", 10, sectionHeight + 5, getWidth(), 15, juce::Justification::left);
        g.drawText("FULL MIX MASTER FX SLOT (D&D REORDERABLE)", 10, (sectionHeight * 2) + 5, getWidth(), 15, juce::Justification::left);
    }

    void resized() override
    {
        auto sectionHeight = getHeight() / 3;
        const int cardH = 26;
        const int padding = 2;

        transAddCombo.setBounds(getWidth() - 95, 3, 85, 16);
        tonalAddCombo.setBounds(getWidth() - 95, sectionHeight + 3, 85, 16);
        fullMixAddCombo.setBounds(getWidth() - 95, (sectionHeight * 2) + 3, 85, 16);

        int transCount = 0; int tonalCount = 0; int fullCount = 0;

        for (auto* card : activeCards)
        {
            const auto route = card->getRoute();
            if (route == TargetRoute::Transient)
            {
                card->setBounds(10, 22 + (transCount * (cardH + padding)), getWidth() - 20, cardH);
                transCount++;
            }
            else if (route == TargetRoute::Tonal)
            {
                card->setBounds(10, sectionHeight + 22 + (tonalCount * (cardH + padding)), getWidth() - 20, cardH);
                tonalCount++;
            }
            else if (route == TargetRoute::FullMix)
            {
                card->setBounds(10, (sectionHeight * 2) + 22 + (fullCount * (cardH + padding)), getWidth() - 20, cardH);
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
        auto* droppedCard = reinterpret_cast<EffectCardComponent*>(ptrVal);
        if (droppedCard == nullptr) return;

        auto dropY = dragSourceDetails.localPosition.getY();
        auto sectionHeight = getHeight() / 3;

        TargetRoute targetRoute = TargetRoute::FullMix;
        std::vector<int>* targetIndices = &fullMixActiveIndices;

        if (dropY < sectionHeight)
        {
            targetRoute = TargetRoute::Transient;
            targetIndices = &transActiveIndices;
        }
        else if (dropY < sectionHeight * 2)
        {
            targetRoute = TargetRoute::Tonal;
            targetIndices = &tonalActiveIndices;
        }

        int typeIdx = droppedCard->getTypeIndex();
        TargetRoute oldRoute = droppedCard->getRoute();

        if (oldRoute == TargetRoute::Transient)
            transActiveIndices.erase(std::remove(transActiveIndices.begin(), transActiveIndices.end(), typeIdx), transActiveIndices.end());
        else if (oldRoute == TargetRoute::Tonal)
            tonalActiveIndices.erase(std::remove(tonalActiveIndices.begin(), tonalActiveIndices.end(), typeIdx), tonalActiveIndices.end());
        else if (oldRoute == TargetRoute::FullMix)
            fullMixActiveIndices.erase(std::remove(fullMixActiveIndices.begin(), fullMixActiveIndices.end(), typeIdx), fullMixActiveIndices.end());

        int insertIndex = 0;
        for (auto* card : activeCards)
        {
            if (card->getRoute() == targetRoute)
            {
                auto cardCenterY = card->getY() + (card->getHeight() / 2);
                if (dropY > cardCenterY)
                {
                    insertIndex++;
                }
            }
        }

        if (insertIndex > static_cast<int>(targetIndices->size()))
            insertIndex = static_cast<int>(targetIndices->size());

        targetIndices->insert(targetIndices->begin() + insertIndex, typeIdx);

        processor.updateRouteOrder(TargetRoute::Transient, transActiveIndices);
        processor.updateRouteOrder(TargetRoute::Tonal, tonalActiveIndices);
        processor.updateRouteOrder(TargetRoute::FullMix, fullMixActiveIndices);

        rebuildCardComponents();
        sendChangeMessage();
    }

    AudioEffect* getSelectedEffect() const noexcept { return currentSelectedFX; }

private:
    void rebuildCardComponents()
    {
        activeCards.clear();
        currentSelectedFX = nullptr;

        auto createCardInLane = [this](int typeIdx, TargetRoute route) {
            AudioEffect* fxPtr = nullptr;
            if (route == TargetRoute::Transient)     fxPtr = processor.getTransientPoolInstance(typeIdx);
            else if (route == TargetRoute::Tonal)    fxPtr = processor.getTonalPoolInstance(typeIdx);
            else if (route == TargetRoute::FullMix)  fxPtr = processor.getFullMixPoolInstance(typeIdx);

            if (fxPtr == nullptr) return;

            auto* card = new EffectCardComponent(fxPtr, typeIdx, route,
                [this] { sendChangeMessage(); },
                [this, typeIdx, route] {
                    if (route == TargetRoute::Transient)
                        transActiveIndices.erase(std::remove(transActiveIndices.begin(), transActiveIndices.end(), typeIdx), transActiveIndices.end());
                    else if (route == TargetRoute::Tonal)
                        tonalActiveIndices.erase(std::remove(tonalActiveIndices.begin(), tonalActiveIndices.end(), typeIdx), tonalActiveIndices.end());
                    else if (route == TargetRoute::FullMix)
                        fullMixActiveIndices.erase(std::remove(fullMixActiveIndices.begin(), fullMixActiveIndices.end(), typeIdx), fullMixActiveIndices.end());

                    processor.updateRouteOrder(TargetRoute::Transient, transActiveIndices);
                    processor.updateRouteOrder(TargetRoute::Tonal, tonalActiveIndices);
                    processor.updateRouteOrder(TargetRoute::FullMix, fullMixActiveIndices);

                    rebuildCardComponents();
                    sendChangeMessage();
                });

            card->onCardSelectedCallback = [this](AudioEffect* clickedFx) {
                currentSelectedFX = clickedFx;
                sendChangeMessage();
                };

            addAndMakeVisible(card);
            activeCards.add(card);
            };

        for (int idx : transActiveIndices)   createCardInLane(idx, TargetRoute::Transient);
        for (int idx : tonalActiveIndices)   createCardInLane(idx, TargetRoute::Tonal);
        for (int idx : fullMixActiveIndices) createCardInLane(idx, TargetRoute::FullMix);

        resized();
        repaint();
    }

    AnatomyAudioProcessor& processor;
    juce::OwnedArray<EffectCardComponent> activeCards;
    AudioEffect* currentSelectedFX = nullptr;

    juce::ComboBox transAddCombo;
    juce::ComboBox tonalAddCombo;
    juce::ComboBox fullMixAddCombo;

    std::vector<int> transActiveIndices;
    std::vector<int> tonalActiveIndices;
    std::vector<int> fullMixActiveIndices;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectRackPanel)
};