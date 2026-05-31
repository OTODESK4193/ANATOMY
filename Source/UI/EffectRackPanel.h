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
 * 縦幅半分（26px）にスリム化され、名称、On/Off、格納（X）ボタンのみで構成されたラックカードUI。
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

        // エフェクト名称ラベル
        lblTitle.setText(fx->getName(), juce::dontSendNotification);
        lblTitle.setFont(juce::Font(11.0f, juce::Font::bold));
        lblTitle.setJustificationType(juce::Justification::centredLeft);
        lblTitle.setColour(juce::Label::textColourId, juce::Colours::cyan);
        addAndMakeVisible(lblTitle);

        // ON/OFF トグルボタン
        btnActive.setButtonText("ON");
        btnActive.setClickingTogglesState(true);
        btnActive.setToggleState(fx->isActive(), juce::dontSendNotification);
        btnActive.setColour(juce::TextButton::buttonOnColourId, juce::Colours::cyan);
        btnActive.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
        btnActive.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a3a3a));
        btnActive.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.4f));
        btnActive.onClick = [this] {
            if (fx != nullptr)
            {
                fx->setActive(btnActive.getToggleState());
                if (onToggle) onToggle();
            }
            };
        addAndMakeVisible(btnActive);

        // 格納（×）ボタン
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

    void paradoxicalTriggerSelection() { if (onCardSelectedCallback && fx != nullptr) onCardSelectedCallback(fx); }

    void mouseDown(const juce::MouseEvent& /*e*/) override
    {
        paradoxicalTriggerSelection();
    }

    void mouseDrag(const juce::MouseEvent& /*e*/) override
    {
        if (auto* dragContainer = juce::DragAndDropContainer::findParentDragContainerFor(this))
        {
            if (!dragContainer->isDragAndDropActive())
            {
                // 💥【修正】内部型へのキャストエラーを、ポインタの生アドレス数値を安全に格納するint64キャストへ復元
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
 * 5連スタイリッシュトグルボタン配置コンテナ。
 */
class EffectRackPanel final : public juce::Component,
    public juce::DragAndDropTarget,
    public juce::ChangeBroadcaster
{
public:
    EffectRackPanel(AnatomyAudioProcessor& p) : processor(p)
    {
        auto setupToggleButtons = [this](std::vector<std::unique_ptr<juce::TextButton>>& btns, const juce::StringArray& names) {
            btns.clear();
            for (int i = 0; i < 5; ++i)
            {
                auto b = std::make_unique<juce::TextButton>(names[i]);
                b->setClickingTogglesState(true);
                b->setColour(juce::TextButton::buttonOnColourId, juce::Colours::cyan);
                b->setColour(juce::TextButton::textColourOnId, juce::Colours::black);
                b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a3a3a));
                b->setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.6f));

                btns.push_back(std::move(b));
                addAndMakeVisible(*btns.back());
            }
            };

        juce::StringArray fxNames{ "Satu", "Crush", "Noise", "OTT", "Limit" };
        setupToggleButtons(transToggleButtons, fxNames);
        setupToggleButtons(tonalToggleButtons, fxNames);
        setupToggleButtons(fullMixToggleButtons, fxNames);

        for (int i = 0; i < 5; ++i)
        {
            transToggleButtons[i]->onClick = [this, i] { handleToggleClick(TargetRoute::Transient, i, transToggleButtons[i]->getToggleState()); };
            tonalToggleButtons[i]->onClick = [this, i] { handleToggleClick(TargetRoute::Tonal, i, tonalToggleButtons[i]->getToggleState()); };
            fullMixToggleButtons[i]->onClick = [this, i] { handleToggleClick(TargetRoute::FullMix, i, fullMixToggleButtons[i]->getToggleState()); };
        }

        rebuildCardComponents();
    }

    ~EffectRackPanel() override = default;

    void updateCardSlidersFromParameters() noexcept
    {
        for (auto* card : activeCards)
        {
            card->updateActiveStates();
        }

        auto updateHeaderToggles = [](std::vector<std::unique_ptr<juce::TextButton>>& btns, const std::vector<int>& activeIndices) {
            if (btns.size() < 5) return;
            for (int i = 0; i < 5; ++i)
            {
                bool isActive = (std::find(activeIndices.begin(), activeIndices.end(), i) != activeIndices.end());
                btns[i]->setToggleState(isActive, juce::dontSendNotification);
            }
            };
        updateHeaderToggles(transToggleButtons, transActiveIndices);
        updateHeaderToggles(tonalToggleButtons, tonalActiveIndices);
        updateHeaderToggles(fullMixToggleButtons, fullMixActiveIndices);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff242424));
        auto sectionHeight = getHeight() / 3;

        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawHorizontalLine(sectionHeight, 0.0f, static_cast<float>(getWidth()));
        g.drawHorizontalLine(sectionHeight * 2, 0.0f, static_cast<float>(getWidth()));

        g.setFont(juce::Font(10.5f, juce::Font::bold));

        g.setColour(juce::Colours::cyan.withAlpha(0.7f));
        g.drawText("TransientSlot", 10, 4, getWidth() - 20, 14, juce::Justification::left);

        g.setColour(juce::Colours::magenta.withAlpha(0.7f));
        g.drawText("TonalSlot", 10, sectionHeight + 4, getWidth() - 20, 14, juce::Justification::left);

        g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.drawText("FullMixSlot", 10, (sectionHeight * 2) + 4, getWidth() - 20, 14, juce::Justification::left);
    }

    void resized() override
    {
        if (transToggleButtons.size() < 5 || tonalToggleButtons.size() < 5 || fullMixToggleButtons.size() < 5) return;

        auto sectionHeight = getHeight() / 3;
        const int cardH = 26;
        const int padding = 2;

        const int btnW = 44;
        const int btnH = 16;

        auto positionHeaderButtons = [this, btnW, btnH](std::vector<std::unique_ptr<juce::TextButton>>& btns, int sectionTopY) {
            int startX = 10;
            for (int i = 0; i < 5; ++i)
            {
                btns[i]->setBounds(startX + (i * btnW), sectionTopY + 18, btnW - 1, btnH);
            }
            };

        positionHeaderButtons(transToggleButtons, 0);
        positionHeaderButtons(tonalToggleButtons, sectionHeight);
        positionHeaderButtons(fullMixToggleButtons, sectionHeight * 2);

        int transCount = 0; int tonalCount = 0; int fullCount = 0;

        for (auto* card : activeCards)
        {
            const auto route = card->getRoute();
            if (route == TargetRoute::Transient)
            {
                card->setBounds(10, 38 + (transCount * (cardH + padding)), getWidth() - 20, cardH);
                transCount++;
            }
            else if (route == TargetRoute::Tonal)
            {
                card->setBounds(10, sectionHeight + 38 + (tonalCount * (cardH + padding)), getWidth() - 20, cardH);
                tonalCount++;
            }
            else if (route == TargetRoute::FullMix)
            {
                card->setBounds(10, (sectionHeight * 2) + 38 + (fullCount * (cardH + padding)), getWidth() - 20, cardH);
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
    void handleToggleClick(TargetRoute route, int typeIdx, bool shouldExist)
    {
        std::vector<int>& indices = (route == TargetRoute::Transient) ? transActiveIndices :
            ((route == TargetRoute::Tonal) ? tonalActiveIndices : fullMixActiveIndices);

        auto it = std::find(indices.begin(), indices.end(), typeIdx);

        if (shouldExist && it == indices.end())
        {
            indices.push_back(typeIdx);
            if (auto* fx = (route == TargetRoute::Transient ? processor.getTransientPoolInstance(typeIdx) :
                (route == TargetRoute::Tonal ? processor.getTonalPoolInstance(typeIdx) : processor.getFullMixPoolInstance(typeIdx))))
            {
                fx->setActive(true);
            }
        }
        else if (!shouldExist && it != indices.end())
        {
            indices.erase(it);
        }

        processor.updateRouteOrder(route, indices);
        rebuildCardComponents();
        sendChangeMessage();
    }

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
            currentSelectedFX = fxPtr;
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

    std::vector<std::unique_ptr<juce::TextButton>> transToggleButtons;
    std::vector<std::unique_ptr<juce::TextButton>> tonalToggleButtons;
    std::vector<std::unique_ptr<juce::TextButton>> fullMixToggleButtons;

    std::vector<int> transActiveIndices;
    std::vector<int> tonalActiveIndices;
    std::vector<int> fullMixActiveIndices;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectRackPanel)
};