#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "../PluginProcessor.h"
#include "../DSP/Effects/AudioEffect.h"
#include <vector>
#include <memory>
#include <algorithm>

/**
 * EffectRackPanel  (Order Strip Edition)
 *
 * ■ 上部 (3×2 グリッド) : エフェクトのON/OFF
 *   左クリック: OFF→チェーン追加, ON→Dock表示切り替え (OFFにはならない)
 *
 * ■ 下部 (ChipBar) : アクティブエフェクトを処理順で縦表示
 *   左クリック      : Dock に表示 (選択)
 *   縦ドラッグ      : 処理順の並び替え
 *   右クリック      : 削除 / Move up / Move down
 */
class EffectRackPanel final : public juce::Component,
    public juce::ChangeBroadcaster
{
public:
    // =========================================================================
    // FxBtn: ON/OFF トグルボタン
    // =========================================================================
    class FxBtn final : public juce::TextButton
    {
    public:
        FxBtn() : juce::TextButton("") {}  // MSVC ネストクラス用に明示
    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxBtn)
    };

    // =========================================================================
    // ChipBar: 処理順チップリスト + D&D 並び替え
    // =========================================================================
    class ChipBar final : public juce::Component,
        public juce::DragAndDropTarget
    {
    public:
        ChipBar(TargetRoute r, juce::Colour c)
            : route(r), accentColor(c)
        {}

        /** 表示を更新 (EffectRackPanel から呼ぶ) */
        void update(const std::vector<int>& indices, int selFxIdx)
        {
            activeIndices = indices;
            selectedFxIdx = selFxIdx;
            repaint();
        }

        // EffectRackPanel が購読するコールバック
        std::function<void(int fxIdx)> onChipClicked;   // クリック→Dock選択
        std::function<void()>          onOrderChanged;  // 並び替え/削除後

        std::vector<int> activeIndices;  // ChipBar が管理する処理順

        // ---- paint ----------------------------------------------------------
        void paint(juce::Graphics& g) override
        {
            g.setColour(juce::Colour(0xff161616));
            g.fillRect(getLocalBounds());

            if (activeIndices.empty())
            {
                g.setColour(juce::Colours::white.withAlpha(0.08f));
                g.setFont(juce::Font(8.0f));
                g.drawText("─  no effects  ─",
                           getLocalBounds().reduced(0, 4),
                           juce::Justification::centred);
                return;
            }

            const int chipH = 21, gap = 3;
            int y = 5;

            for (int i = 0; i < (int)activeIndices.size(); ++i)
            {
                if (i == dragIndicatorPos)
                    drawInsertLine(g, y - 2);

                int fxIdx = activeIndices[i];
                juce::String name = (fxIdx >= 0 && fxIdx < 6)
                                    ? kFxNames[fxIdx] : "?";

                juce::Rectangle<int> chip(6, y, getWidth() - 12, chipH);
                bool isSel  = (fxIdx == selectedFxIdx);
                bool isHov  = (i == hoveredPos && !isDragging);

                float bgA = isSel ? 0.42f : (isHov ? 0.22f : 0.13f);
                g.setColour(accentColor.withAlpha(bgA));
                g.fillRoundedRectangle(chip.toFloat(), 3.0f);

                if (isSel)
                {
                    g.setColour(juce::Colours::white.withAlpha(0.65f));
                    g.drawRoundedRectangle(chip.toFloat(), 3.0f, 1.0f);
                }

                // ≡ ドラッグハンドル
                int hx = chip.getX() + 5, cy = chip.getCentreY();
                g.setColour(juce::Colours::white.withAlpha(isSel ? 0.55f : 0.25f));
                for (int dl = -3; dl <= 3; dl += 3)
                    g.fillRect(hx, cy + dl, 7, 1);

                // エフェクト名
                g.setFont(juce::Font(8.5f, juce::Font::bold));
                g.setColour(isSel ? juce::Colours::white
                                  : juce::Colours::white.withAlpha(0.55f));
                g.drawText(name,
                           chip.withLeft(chip.getX() + 18)
                               .withRight(chip.getRight() - 18),
                           juce::Justification::centredLeft);

                // 順番番号 (右端)
                g.setFont(juce::Font(7.5f));
                g.setColour(accentColor.withAlpha(isSel ? 0.9f : 0.4f));
                g.drawText(juce::String(i + 1),
                           chip.withLeft(chip.getRight() - 16),
                           juce::Justification::centred);

                y += chipH + gap;
            }

            // 末尾インジケーター
            if (dragIndicatorPos == (int)activeIndices.size())
                drawInsertLine(g, y - 2);
        }

        // ---- mouse ----------------------------------------------------------
        void mouseDown(const juce::MouseEvent& e) override
        {
            isDragging  = false;
            dragChipPos = -1;

            int pos = chipAtY((int)e.position.y);
            if (pos < 0) return;

            dragChipPos = pos;

            if (e.mods.isRightButtonDown())
            {
                juce::PopupMenu menu;
                menu.addItem(1, "Remove from chain");
                menu.addSeparator();
                if (pos > 0)
                    menu.addItem(2, "Move up");
                if (pos < (int)activeIndices.size() - 1)
                    menu.addItem(3, "Move down");

                menu.showMenuAsync(juce::PopupMenu::Options{},
                    [this, pos](int result)
                    {
                        if (result == 0) return;

                        if (result == 1)          // 削除
                        {
                            if (pos < (int)activeIndices.size())
                            {
                                activeIndices.erase(activeIndices.begin() + pos);
                            }
                        }
                        else if (result == 2)     // Move up
                        {
                            if (pos > 0 && pos < (int)activeIndices.size())
                                std::swap(activeIndices[pos], activeIndices[pos - 1]);
                        }
                        else if (result == 3)     // Move down
                        {
                            if (pos + 1 < (int)activeIndices.size())
                                std::swap(activeIndices[pos], activeIndices[pos + 1]);
                        }

                        if (onOrderChanged) onOrderChanged();
                        repaint();
                    });
                return;
            }

            // 左クリック: 選択
            if (onChipClicked) onChipClicked(activeIndices[pos]);
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            if (!isDragging && dragChipPos >= 0
                && !e.mods.isRightButtonDown()
                && e.getDistanceFromDragStart() > 6)
            {
                isDragging = true;
                if (auto* dc = juce::DragAndDropContainer::findParentDragContainerFor(this))
                {
                    int laneInt = (route == TargetRoute::Transient) ? 0 :
                                  (route == TargetRoute::Tonal)     ? 1 : 2;
                    juce::String data = "CHIP:" + juce::String(laneInt)
                                        + ":" + juce::String(dragChipPos);

                    int fxIdx = activeIndices[dragChipPos];
                    juce::String nm = (fxIdx >= 0 && fxIdx < 6) ? kFxNames[fxIdx] : "?";

                    int imgW = juce::jmax(60, getWidth() - 12);
                    juce::Image img(juce::Image::ARGB, imgW, 21, true);
                    juce::Graphics ig(img);
                    ig.setColour(accentColor.withAlpha(0.72f));
                    ig.fillRoundedRectangle(img.getBounds().toFloat(), 3.0f);
                    ig.setColour(juce::Colours::white);
                    ig.setFont(juce::Font(8.5f, juce::Font::bold));
                    ig.drawText("≡  " + nm, img.getBounds(), juce::Justification::centred);

                    dc->startDragging(data, this, img, false);
                }
            }
        }

        void mouseUp(const juce::MouseEvent&) override
        {
            isDragging  = false;
            dragChipPos = -1;
        }

        void mouseMove(const juce::MouseEvent& e) override
        {
            int h = chipAtY((int)e.position.y);
            if (h != hoveredPos) { hoveredPos = h; repaint(); }
        }

        void mouseExit(const juce::MouseEvent&) override
        {
            hoveredPos = -1; repaint();
        }

        // ---- DragAndDropTarget ----------------------------------------------
        bool isInterestedInDragSource(const SourceDetails& d) override
        {
            auto str = d.description.toString();
            if (!str.startsWith("CHIP:")) return false;
            auto parts = juce::StringArray::fromTokens(str, ":", "");
            if (parts.size() != 3) return false;
            int src = parts[1].getIntValue();
            int my  = (route == TargetRoute::Transient) ? 0 :
                      (route == TargetRoute::Tonal)     ? 1 : 2;
            return src == my;    // 同レーンのみ
        }

        void itemDragEnter(const SourceDetails& d) override
        {
            dragIndicatorPos = insertPosAtY(d.localPosition.y);
            repaint();
        }

        void itemDragMove(const SourceDetails& d) override
        {
            int p = insertPosAtY(d.localPosition.y);
            if (p != dragIndicatorPos) { dragIndicatorPos = p; repaint(); }
        }

        void itemDragExit(const SourceDetails&) override
        {
            dragIndicatorPos = -1; repaint();
        }

        void itemDropped(const SourceDetails& d) override
        {
            dragIndicatorPos = -1;
            auto parts = juce::StringArray::fromTokens(d.description.toString(), ":", "");
            if (parts.size() != 3) { repaint(); return; }

            int fromPos = parts[2].getIntValue();
            int toPos   = insertPosAtY(d.localPosition.y);

            if (fromPos < 0 || fromPos >= (int)activeIndices.size())
            { repaint(); return; }

            if (toPos != fromPos && toPos != fromPos + 1)
            {
                int val = activeIndices[fromPos];
                activeIndices.erase(activeIndices.begin() + fromPos);
                int ins = juce::jlimit(0, (int)activeIndices.size(),
                                       toPos > fromPos ? toPos - 1 : toPos);
                activeIndices.insert(activeIndices.begin() + ins, val);
            }

            if (onOrderChanged) onOrderChanged();
            repaint();
        }

    private:
        void drawInsertLine(juce::Graphics& g, int y) const
        {
            g.setColour(accentColor.withAlpha(0.85f));
            g.fillRect(10, y, getWidth() - 20, 2);
        }

        int chipAtY(int y) const noexcept
        {
            const int chipH = 21, gap = 3;
            int top = 5;
            for (int i = 0; i < (int)activeIndices.size(); ++i)
            {
                if (y >= top && y < top + chipH) return i;
                top += chipH + gap;
            }
            return -1;
        }

        int insertPosAtY(int y) const noexcept
        {
            const int chipH = 21, gap = 3;
            int top = 5;
            for (int i = 0; i < (int)activeIndices.size(); ++i)
            {
                if (y < top + chipH / 2) return i;
                top += chipH + gap;
            }
            return (int)activeIndices.size();
        }

        static constexpr const char* kFxNames[6] = {
            "SATU", "CRUSH", "NOISE", "OTT", "GLUE", "LIMIT"
        };

        TargetRoute  route;
        juce::Colour accentColor;

        int  selectedFxIdx    = -1;
        int  hoveredPos       = -1;
        int  dragChipPos      = -1;
        int  dragIndicatorPos = -1;
        bool isDragging       = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChipBar)
    };

    // =========================================================================
    EffectRackPanel(AnatomyAudioProcessor& p) : processor(p)
    {
        const juce::StringArray fxNames { "Satu","Crush","Noise","OTT","Glue","Limit" };

        auto setupLane = [&](std::vector<std::unique_ptr<FxBtn>>& btns,
                              juce::Colour activeColor, TargetRoute route)
        {
            btns.clear();
            for (int i = 0; i < 6; ++i)
            {
                auto b = std::make_unique<FxBtn>();
                b->setButtonText(fxNames[i]);
                b->setClickingTogglesState(false);
                b->setColour(juce::TextButton::buttonOnColourId,  activeColor.withAlpha(0.85f));
                b->setColour(juce::TextButton::textColourOnId,    juce::Colours::black);
                b->setColour(juce::TextButton::buttonColourId,    juce::Colour(0xff2a2a2a));
                b->setColour(juce::TextButton::textColourOffId,   juce::Colours::white.withAlpha(0.38f));
                b->onClick = [this, i, route] { handleButtonClick(route, i); };
                btns.push_back(std::move(b));
                addAndMakeVisible(*btns.back());
            }
        };

        setupLane(transButtons,   juce::Colours::cyan,    TargetRoute::Transient);
        setupLane(tonalButtons,   juce::Colours::magenta, TargetRoute::Tonal);
        setupLane(fullMixButtons, juce::Colours::white,   TargetRoute::FullMix);

        // ChipBar 生成
        transChipBar   = std::make_unique<ChipBar>(TargetRoute::Transient, juce::Colours::cyan);
        tonalChipBar   = std::make_unique<ChipBar>(TargetRoute::Tonal,     juce::Colours::magenta);
        fullMixChipBar = std::make_unique<ChipBar>(TargetRoute::FullMix,   juce::Colours::white);

        // ChipBar コールバック設定
        auto wireChipBar = [this](ChipBar& bar, TargetRoute route, std::vector<int>& indices)
        {
            // クリック → Dock 選択
            bar.onChipClicked = [this, route](int fxIdx)
            {
                currentSelectedFX = getPoolInstance(route, fxIdx);
                refreshAllChipBars();
                sendChangeMessage();
            };

            // 並び替え/削除後 → EffectRackPanel の indices を ChipBar に合わせる
            bar.onOrderChanged = [this, &bar, route, &indices]()
            {
                // ① ChipBar が activeIndices を既に更新済み → EffectRackPanel に反映
                indices = bar.activeIndices;

                // ② チェーンから外れたエフェクトを非アクティブに
                for (int i = 0; i < 6; ++i)
                {
                    bool inChain = std::find(indices.begin(), indices.end(), i) != indices.end();
                    if (auto* fx = getPoolInstance(route, i))
                        if (!inChain) fx->setActive(false);
                }

                // ③ 選択中 FX がまだチェーンにあるか確認
                if (currentSelectedFX)
                {
                    bool found = false;
                    for (auto r : { TargetRoute::Transient, TargetRoute::Tonal, TargetRoute::FullMix })
                        for (int idx : getIndices(r))
                            if (getPoolInstance(r, idx) == currentSelectedFX)
                                found = true;
                    if (!found) currentSelectedFX = nullptr;
                }

                processor.updateRouteOrder(route, indices);
                syncBtnStates(transButtons,   transIndices);
                syncBtnStates(tonalButtons,   tonalIndices);
                syncBtnStates(fullMixButtons, fullMixIndices);
                refreshAllChipBars();
                sendChangeMessage();
            };
        };

        wireChipBar(*transChipBar,   TargetRoute::Transient, transIndices);
        wireChipBar(*tonalChipBar,   TargetRoute::Tonal,     tonalIndices);
        wireChipBar(*fullMixChipBar, TargetRoute::FullMix,   fullMixIndices);

        addAndMakeVisible(*transChipBar);
        addAndMakeVisible(*tonalChipBar);
        addAndMakeVisible(*fullMixChipBar);

        // エディタ再開時：プロセッサに保持されたエフェクト順序を復元
        transIndices   = processor.getEffectOrder(TargetRoute::Transient);
        tonalIndices   = processor.getEffectOrder(TargetRoute::Tonal);
        fullMixIndices = processor.getEffectOrder(TargetRoute::FullMix);
        syncBtnStates(transButtons,   transIndices);
        syncBtnStates(tonalButtons,   tonalIndices);
        syncBtnStates(fullMixButtons, fullMixIndices);
        refreshAllChipBars();
    }

    ~EffectRackPanel() override = default;

    /** タイマーから呼ぶ: ボタン状態を同期 */
    void updateCardSlidersFromParameters() noexcept
    {
        syncBtnStates(transButtons,   transIndices);
        syncBtnStates(tonalButtons,   tonalIndices);
        syncBtnStates(fullMixButtons, fullMixIndices);
        refreshAllChipBars();
    }

    AudioEffect* getSelectedEffect() const noexcept { return currentSelectedFX; }

    // =========================================================================
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1a1a1a));

        const int sh = getHeight() / 3;
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.drawHorizontalLine(sh,     0.0f, (float)getWidth());
        g.drawHorizontalLine(sh * 2, 0.0f, (float)getWidth());

        g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.setColour(juce::Colours::cyan.withAlpha(0.7f));
        g.drawText("TRANSIENT", 6, 3,       getWidth()-8, 11, juce::Justification::left);
        g.setColour(juce::Colours::magenta.withAlpha(0.7f));
        g.drawText("TONAL",     6, sh+3,    getWidth()-8, 11, juce::Justification::left);
        g.setColour(juce::Colours::white.withAlpha(0.5f));
        g.drawText("FULL MIX",  6, sh*2+3,  getWidth()-8, 11, juce::Justification::left);
    }

    void resized() override
    {
        const int sh      = getHeight() / 3;
        const int padX    = 5;
        const int padTop  = 15;
        const int gap     = 3;
        const int btnH    = 18;
        const int btnW    = (getWidth() - padX*2 - gap*2) / 3;
        const int btnsEnd = padTop + 2 * (btnH + gap);   // ≈ 15+18+3+18=54 px

        auto posSection = [&](std::vector<std::unique_ptr<FxBtn>>& btns,
                               ChipBar& bar, int baseY)
        {
            for (int i = 0; i < 6; ++i)
            {
                int col = i % 3, row = i / 3;
                btns[i]->setBounds(padX + col * (btnW + gap),
                                   baseY + padTop + row * (btnH + gap),
                                   btnW, btnH);
            }
            int barY = baseY + btnsEnd + 4;
            int barH = sh - btnsEnd - 7;
            bar.setBounds(padX, barY, getWidth() - padX*2, barH);
        };

        posSection(transButtons,   *transChipBar,   0);
        posSection(tonalButtons,   *tonalChipBar,   sh);
        posSection(fullMixButtons, *fullMixChipBar, sh*2);
    }

private:
    // =========================================================================
    void handleButtonClick(TargetRoute route, int typeIdx)
    {
        auto& indices = getIndices(route);
        auto* fxPtr   = getPoolInstance(route, typeIdx);
        auto  it      = std::find(indices.begin(), indices.end(), typeIdx);

        if (it == indices.end())
        {
            // OFF → チェーン追加
            indices.push_back(typeIdx);
            if (fxPtr) fxPtr->setActive(true);
        }
        // 常に Dock 表示を更新 (ON のままキープ)
        currentSelectedFX = fxPtr;

        processor.updateRouteOrder(route, indices);
        syncBtnStates(transButtons,   transIndices);
        syncBtnStates(tonalButtons,   tonalIndices);
        syncBtnStates(fullMixButtons, fullMixIndices);
        refreshAllChipBars();
        sendChangeMessage();
    }

    void refreshAllChipBars()
    {
        int selFxIdx = -1;
        if (currentSelectedFX)
        {
            for (auto r : { TargetRoute::Transient, TargetRoute::Tonal, TargetRoute::FullMix })
                for (int i = 0; i < 6; ++i)
                    if (getPoolInstance(r, i) == currentSelectedFX)
                        selFxIdx = i;
        }
        transChipBar->update(transIndices,    selFxIdx);
        tonalChipBar->update(tonalIndices,    selFxIdx);
        fullMixChipBar->update(fullMixIndices, selFxIdx);
    }

    void syncBtnStates(std::vector<std::unique_ptr<FxBtn>>& btns,
                       const std::vector<int>& indices) noexcept
    {
        for (int i = 0; i < 6; ++i)
        {
            bool on = std::find(indices.begin(), indices.end(), i) != indices.end();
            btns[i]->setToggleState(on, juce::dontSendNotification);
        }
    }

    std::vector<int>& getIndices(TargetRoute r) noexcept
    {
        return (r == TargetRoute::Transient) ? transIndices :
               (r == TargetRoute::Tonal)     ? tonalIndices : fullMixIndices;
    }

    AudioEffect* getPoolInstance(TargetRoute r, int idx) noexcept
    {
        return (r == TargetRoute::Transient) ? processor.getTransientPoolInstance(idx) :
               (r == TargetRoute::Tonal)     ? processor.getTonalPoolInstance(idx) :
                                               processor.getFullMixPoolInstance(idx);
    }

    // =========================================================================
    AnatomyAudioProcessor& processor;
    AudioEffect* currentSelectedFX = nullptr;

    std::vector<std::unique_ptr<FxBtn>> transButtons;
    std::vector<std::unique_ptr<FxBtn>> tonalButtons;
    std::vector<std::unique_ptr<FxBtn>> fullMixButtons;

    std::vector<int> transIndices;
    std::vector<int> tonalIndices;
    std::vector<int> fullMixIndices;

    std::unique_ptr<ChipBar> transChipBar;
    std::unique_ptr<ChipBar> tonalChipBar;
    std::unique_ptr<ChipBar> fullMixChipBar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectRackPanel)
};
