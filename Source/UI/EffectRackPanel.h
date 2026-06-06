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
 * EffectRackPanel (Compact Edition)
 * 3レーン × 6エフェクト = 合計18ボタンを 3×2 グリッドで配置。
 * カードUIを廃止し、トグルボタンのクリックで Dock パラメーターを直接表示。
 */
class EffectRackPanel final : public juce::Component,
    public juce::ChangeBroadcaster
{
public:
    EffectRackPanel(AnatomyAudioProcessor& p) : processor(p)
    {
        juce::StringArray fxNames{ "Satu", "Crush", "Noise", "OTT", "Glue", "Limit" };

        auto setupButtons = [this](std::vector<std::unique_ptr<juce::TextButton>>& btns,
                                   const juce::StringArray& names,
                                   juce::Colour activeColor)
        {
            btns.clear();
            for (int i = 0; i < 6; ++i)
            {
                auto b = std::make_unique<juce::TextButton>(names[i]);
                b->setClickingTogglesState(true);
                b->setColour(juce::TextButton::buttonOnColourId,   activeColor);
                b->setColour(juce::TextButton::textColourOnId,     juce::Colours::black);
                b->setColour(juce::TextButton::buttonColourId,     juce::Colour(0xff323232));
                b->setColour(juce::TextButton::textColourOffId,    juce::Colours::white.withAlpha(0.55f));
                btns.push_back(std::move(b));
                addAndMakeVisible(*btns.back());
            }
        };

        setupButtons(transToggleButtons,   fxNames, juce::Colours::cyan);
        setupButtons(tonalToggleButtons,   fxNames, juce::Colours::magenta);
        setupButtons(fullMixToggleButtons, fxNames, juce::Colours::white);

        for (int i = 0; i < 6; ++i)
        {
            transToggleButtons[i]->onClick   = [this, i] { handleToggleClick(TargetRoute::Transient, i, transToggleButtons[i]->getToggleState()); };
            tonalToggleButtons[i]->onClick   = [this, i] { handleToggleClick(TargetRoute::Tonal,     i, tonalToggleButtons[i]->getToggleState()); };
            fullMixToggleButtons[i]->onClick = [this, i] { handleToggleClick(TargetRoute::FullMix,   i, fullMixToggleButtons[i]->getToggleState()); };
        }
    }

    ~EffectRackPanel() override = default;

    // タイマーから呼ばれてトグル状態を同期
    void updateCardSlidersFromParameters() noexcept
    {
        auto syncToggles = [](std::vector<std::unique_ptr<juce::TextButton>>& btns,
                              const std::vector<int>& active)
        {
            for (int i = 0; i < 6; ++i)
            {
                bool on = std::find(active.begin(), active.end(), i) != active.end();
                btns[i]->setToggleState(on, juce::dontSendNotification);
            }
        };
        syncToggles(transToggleButtons,   transActiveIndices);
        syncToggles(tonalToggleButtons,   tonalActiveIndices);
        syncToggles(fullMixToggleButtons, fullMixActiveIndices);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff222222));

        const int sh = getHeight() / 3;

        // セクション区切り線
        g.setColour(juce::Colours::white.withAlpha(0.07f));
        g.drawHorizontalLine(sh,     0.0f, (float)getWidth());
        g.drawHorizontalLine(sh * 2, 0.0f, (float)getWidth());

        g.setFont(juce::Font(9.0f, juce::Font::bold));

        g.setColour(juce::Colours::cyan.withAlpha(0.65f));
        g.drawText("TRANSIENT", 8, 4,       getWidth() - 10, 11, juce::Justification::left);

        g.setColour(juce::Colours::magenta.withAlpha(0.65f));
        g.drawText("TONAL",     8, sh + 4,  getWidth() - 10, 11, juce::Justification::left);

        g.setColour(juce::Colours::white.withAlpha(0.55f));
        g.drawText("FULL MIX",  8, sh * 2 + 4, getWidth() - 10, 11, juce::Justification::left);
    }

    void resized() override
    {
        if (transToggleButtons.size() < 6) return;

        const int sh    = getHeight() / 3;
        const int padX  = 6;
        const int padTop = 17;   // セクションラベル下
        const int gap   = 3;
        const int btnH  = 18;
        const int btnW  = (getWidth() - padX * 2 - gap * 2) / 3;

        auto positionGrid = [&](std::vector<std::unique_ptr<juce::TextButton>>& btns, int baseY)
        {
            for (int i = 0; i < 6; ++i)
            {
                const int col = i % 3;
                const int row = i / 3;
                btns[i]->setBounds(padX + col * (btnW + gap),
                                   baseY + padTop + row * (btnH + gap),
                                   btnW, btnH);
            }
        };

        positionGrid(transToggleButtons,   0);
        positionGrid(tonalToggleButtons,   sh);
        positionGrid(fullMixToggleButtons, sh * 2);
    }

    AudioEffect* getSelectedEffect() const noexcept { return currentSelectedFX; }

private:
    void handleToggleClick(TargetRoute route, int typeIdx, bool shouldExist)
    {
        std::vector<int>& indices =
            (route == TargetRoute::Transient) ? transActiveIndices :
            (route == TargetRoute::Tonal)     ? tonalActiveIndices : fullMixActiveIndices;

        auto it = std::find(indices.begin(), indices.end(), typeIdx);

        AudioEffect* fxPtr =
            (route == TargetRoute::Transient) ? processor.getTransientPoolInstance(typeIdx) :
            (route == TargetRoute::Tonal)     ? processor.getTonalPoolInstance(typeIdx) :
                                                processor.getFullMixPoolInstance(typeIdx);

        if (shouldExist && it == indices.end())
        {
            indices.push_back(typeIdx);
            if (fxPtr) fxPtr->setActive(true);
            currentSelectedFX = fxPtr;         // Dock にパラメーターを表示
        }
        else if (!shouldExist && it != indices.end())
        {
            indices.erase(it);
            if (fxPtr) fxPtr->setActive(false);
            currentSelectedFX = nullptr;        // Dock をクリア
        }

        processor.updateRouteOrder(route, indices);
        repaint();
        sendChangeMessage();
    }

    AnatomyAudioProcessor& processor;
    AudioEffect* currentSelectedFX = nullptr;

    std::vector<std::unique_ptr<juce::TextButton>> transToggleButtons;
    std::vector<std::unique_ptr<juce::TextButton>> tonalToggleButtons;
    std::vector<std::unique_ptr<juce::TextButton>> fullMixToggleButtons;

    std::vector<int> transActiveIndices;
    std::vector<int> tonalActiveIndices;
    std::vector<int> fullMixActiveIndices;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectRackPanel)
};
