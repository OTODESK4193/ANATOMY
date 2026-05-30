#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "../DSP/Effects/AudioEffect.h"
#include <functional>

/**
 * EffectCardComponent
 * 縦幅半分にスリム化され、名称、On/Offトグル、格納（×）ボタンのみを配置した
 * メモリ安全なエフェクトカードUI。パラメータ表示を完全に排除。
 */
class EffectCardComponent final : public juce::Component
{
public:
    EffectCardComponent(AudioEffect* fxInstance,
        std::function<void()> onToggleCallback,
        std::function<void()> onRemoveCallback)
        : fx(fxInstance), onToggle(onToggleCallback), onRemove(onRemoveCallback)
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

    void paint(juce::Graphics& g) override
    {
        // スリム化された背景描画 (縦幅半分の26pxに最適化)
        g.fillAll(juce::Colours::darkgrey.darker().darker());
        g.setColour(juce::Colours::cyan.withAlpha(0.3f));
        g.drawRect(getLocalBounds(), 1);

        // ドラッグ用グリッドハンドルテクスチャ
        g.setColour(juce::Colours::white.withAlpha(0.2f));
        g.fillRect(0, 0, 12, getHeight());
        g.setColour(juce::Colours::white.withAlpha(0.3f));
        for (int i = 4; i < getHeight() - 2; i += 4)
        {
            g.fillEllipse(4, i, 2, 2);
            g.fillEllipse(7, i, 2, 2);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromLeft(16);

        btnRemove.setBounds(area.removeFromRight(20).reduced(2));
        area.removeFromRight(4);
        btnActive.setBounds(area.removeFromRight(40).reduced(2));

        area.removeFromRight(10);
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
    std::function<void()> onToggle;
    std::function<void()> onRemove;

    juce::Label lblTitle;
    juce::TextButton btnActive;
    juce::TextButton btnRemove;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectCardComponent)
};