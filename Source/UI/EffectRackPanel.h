#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "../PluginProcessor.h"
#include "../DSP/Effects/AudioEffect.h"
#include <vector>
#include <memory>
#include <algorithm>

/**
 * EffectRackPanel  (UX Fix Edition)
 *
 * ■ クリック動作の修正:
 *   OFF → 左クリック → チェーン追加 + Dock 表示選択
 *   ON  → 左クリック → 選択のみ (ONのままキープ)
 *   ON  → 右クリック → ポップアップ (チェーンから削除 / 順序変更)
 *
 * ■ D&D 復元:
 *   ONのボタンをドラッグ → 別レーンへ移動 (レーン間の移動)
 *   DropTarget: EffectRackPanel 自身
 *
 * ■ 選択インジケーター:
 *   現在 Dock 表示中のボタンに白い枠線を描画
 */
class EffectRackPanel final : public juce::Component,
    public juce::DragAndDropTarget,
    public juce::ChangeBroadcaster
{
public:
    // =========================================================================
    // 右クリック + D&D ドラッグに対応したカスタムボタン
    // =========================================================================
    class FxBtn final : public juce::TextButton
    {
    public:
        FxBtn() : juce::TextButton("") {}   // MSVC: ネストクラスは明示的に宣言が必要

        std::function<void()> onRightClicked;
        TargetRoute lane  = TargetRoute::FullMix;
        int         fxIdx = -1;

        void mouseDown(const juce::MouseEvent& e) override
        {
            dragStarted = false;
            if (e.mods.isRightButtonDown())
            {
                if (onRightClicked) onRightClicked();
                return;
            }
            juce::TextButton::mouseDown(e);
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            // ON 状態のボタンのみ D&D ソースとして機能
            if (!dragStarted && getToggleState() && e.getDistanceFromDragStart() > 8)
            {
                dragStarted = true;
                if (auto* dc = juce::DragAndDropContainer::findParentDragContainerFor(this))
                {
                    int laneInt = (lane == TargetRoute::Transient) ? 0 :
                                  (lane == TargetRoute::Tonal)     ? 1 : 2;
                    juce::String data = "FX:" + juce::String(laneInt) + ":" + juce::String(fxIdx);

                    // ドラッグイメージ (ボタン名をゴースト表示)
                    juce::Image img(juce::Image::ARGB, getWidth(), getHeight(), true);
                    juce::Graphics ig(img);
                    ig.setColour(juce::Colours::white.withAlpha(0.35f));
                    ig.fillRoundedRectangle(img.getBounds().toFloat(), 3.0f);
                    ig.setColour(juce::Colours::black);
                    ig.setFont(juce::Font(8.5f, juce::Font::bold));
                    ig.drawText(getButtonText(), img.getBounds(), juce::Justification::centred);

                    dc->startDragging(data, this, img, false);
                }
            }
        }

        void mouseUp(const juce::MouseEvent& e) override
        {
            dragStarted = false;
            juce::TextButton::mouseUp(e);
        }

    private:
        bool dragStarted = false;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxBtn)
    };

    // =========================================================================
    EffectRackPanel(AnatomyAudioProcessor& p) : processor(p)
    {
        juce::StringArray fxNames{ "Satu", "Crush", "Noise", "OTT", "Glue", "Limit" };

        auto setupLane = [&](std::vector<std::unique_ptr<FxBtn>>& btns,
                              juce::Colour activeColor,
                              TargetRoute  route)
        {
            btns.clear();
            for (int i = 0; i < 6; ++i)
            {
                auto b = std::make_unique<FxBtn>();
                b->setButtonText(fxNames[i]);
                b->setClickingTogglesState(false);  // 自動トグル OFF — 手動管理
                b->lane  = route;
                b->fxIdx = i;

                b->setColour(juce::TextButton::buttonOnColourId,  activeColor.withAlpha(0.85f));
                b->setColour(juce::TextButton::textColourOnId,    juce::Colours::black);
                b->setColour(juce::TextButton::buttonColourId,    juce::Colour(0xff2a2a2a));
                b->setColour(juce::TextButton::textColourOffId,   juce::Colours::white.withAlpha(0.38f));

                // 左クリック: 選択 (OFF→追加+選択、ON→選択のみ)
                b->onClick = [this, i, route] { handleButtonClick(route, i); };

                // 右クリック: 削除 / 順序変更メニュー
                b->onRightClicked = [this, i, route] { handleRightClick(route, i); };

                btns.push_back(std::move(b));
                addAndMakeVisible(*btns.back());
            }
        };

        setupLane(transButtons,   juce::Colours::cyan,    TargetRoute::Transient);
        setupLane(tonalButtons,   juce::Colours::magenta, TargetRoute::Tonal);
        setupLane(fullMixButtons, juce::Colours::white,   TargetRoute::FullMix);
    }

    ~EffectRackPanel() override = default;

    /** タイマーから呼ばれる: チェーン状態とボタン表示を同期 */
    void updateCardSlidersFromParameters() noexcept
    {
        syncStates(transButtons,   transIndices);
        syncStates(tonalButtons,   tonalIndices);
        syncStates(fullMixButtons, fullMixIndices);
    }

    AudioEffect* getSelectedEffect() const noexcept { return currentSelectedFX; }

    // =========================================================================
    // DragAndDropTarget — レーン間の移動
    // =========================================================================
    bool isInterestedInDragSource(const SourceDetails& details) override
    {
        return details.description.toString().startsWith("FX:");
    }

    void itemDragEnter(const SourceDetails&) override { repaint(); }
    void itemDragExit(const SourceDetails&)  override { repaint(); }

    void itemDropped(const SourceDetails& details) override
    {
        auto parts = juce::StringArray::fromTokens(details.description.toString(), ":", "");
        if (parts.size() != 3 || parts[0] != "FX") return;

        int fromLaneInt = parts[1].getIntValue();
        int typeIdx     = parts[2].getIntValue();
        int toLaneInt   = getLaneForY(details.localPosition.y);

        auto fromRoute = intToRoute(fromLaneInt);
        auto toRoute   = intToRoute(toLaneInt);

        auto& fromIdxs = getIndices(fromRoute);
        auto& toIdxs   = getIndices(toRoute);

        auto it = std::find(fromIdxs.begin(), fromIdxs.end(), typeIdx);
        if (it == fromIdxs.end()) return;

        if (fromRoute == toRoute)
        {
            // 同レーン内: ドロップY位置から挿入位置を計算して並び替え
            auto& btns = getButtons(toRoute);
            int baseY  = getSectionBaseY(toLaneInt);
            int insertPos = (int)fromIdxs.size();
            int fromPos   = (int)(it - fromIdxs.begin());

            for (int i = 0; i < 6 && i < (int)fromIdxs.size(); ++i)
            {
                // activeIndices 内の各要素に対応するボタン位置と比較
                int btnIdx = fromIdxs[i];
                if (btnIdx < 6 && btns[btnIdx]->getY() - baseY > details.localPosition.y - baseY)
                {
                    insertPos = i;
                    break;
                }
            }

            if (fromPos != insertPos && insertPos <= (int)fromIdxs.size())
            {
                fromIdxs.erase(it);
                int adj = (insertPos > fromPos) ? insertPos - 1 : insertPos;
                adj = juce::jlimit(0, (int)fromIdxs.size(), adj);
                fromIdxs.insert(fromIdxs.begin() + adj, typeIdx);
            }
            processor.updateRouteOrder(fromRoute, fromIdxs);
        }
        else
        {
            // 別レーンへ移動
            if (auto* fxFrom = getPoolInstance(fromRoute, typeIdx)) fxFrom->setActive(false);
            fromIdxs.erase(it);

            if (std::find(toIdxs.begin(), toIdxs.end(), typeIdx) == toIdxs.end())
            {
                if (auto* fxTo = getPoolInstance(toRoute, typeIdx))
                {
                    fxTo->setActive(true);
                    toIdxs.push_back(typeIdx);
                    currentSelectedFX = fxTo;
                    selectedRoute     = toRoute;
                    selectedIdx       = typeIdx;
                }
            }

            processor.updateRouteOrder(fromRoute, fromIdxs);
            processor.updateRouteOrder(toRoute,   toIdxs);
        }

        syncStates(transButtons,   transIndices);
        syncStates(tonalButtons,   tonalIndices);
        syncStates(fullMixButtons, fullMixIndices);
        repaint();
        sendChangeMessage();
    }

    // =========================================================================
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1e1e1e));

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

        // 現在選択中のボタンに白枠インジケーター
        if (selectedIdx >= 0 && selectedIdx < 6)
        {
            auto& btns = getButtons(selectedRoute);
            if (btns[selectedIdx]->getToggleState())
            {
                g.setColour(juce::Colours::white.withAlpha(0.85f));
                g.drawRoundedRectangle(
                    btns[selectedIdx]->getBounds().toFloat().expanded(1.5f), 3.0f, 1.5f);
            }
        }
    }

    void resized() override
    {
        const int sh    = getHeight() / 3;
        const int padX  = 5;
        const int padTop = 16;
        const int gap   = 3;
        const int btnH  = 18;
        const int btnW  = (getWidth() - padX*2 - gap*2) / 3;

        auto posGrid = [&](std::vector<std::unique_ptr<FxBtn>>& btns, int baseY)
        {
            for (int i = 0; i < 6; ++i)
            {
                const int col = i % 3, row = i / 3;
                btns[i]->setBounds(padX + col*(btnW+gap),
                                   baseY + padTop + row*(btnH+gap),
                                   btnW, btnH);
            }
        };
        posGrid(transButtons,   0);
        posGrid(tonalButtons,   sh);
        posGrid(fullMixButtons, sh*2);
    }

private:
    // =========================================================================
    // クリック処理: OFF→追加+選択、ON→選択のみ
    // =========================================================================
    void handleButtonClick(TargetRoute route, int typeIdx)
    {
        auto& indices = getIndices(route);
        auto* fxPtr   = getPoolInstance(route, typeIdx);
        auto  it      = std::find(indices.begin(), indices.end(), typeIdx);

        if (it == indices.end())
        {
            // OFF → チェーンに追加
            indices.push_back(typeIdx);
            if (fxPtr) fxPtr->setActive(true);
        }
        // ON でも OFF でも: 常にこのエフェクトを Dock 表示として選択
        currentSelectedFX = fxPtr;
        selectedRoute     = route;
        selectedIdx       = typeIdx;

        processor.updateRouteOrder(route, indices);
        syncStates(transButtons,   transIndices);
        syncStates(tonalButtons,   tonalIndices);
        syncStates(fullMixButtons, fullMixIndices);
        repaint();
        sendChangeMessage();
    }

    // =========================================================================
    // 右クリックメニュー: 削除 / 順序変更
    // =========================================================================
    void handleRightClick(TargetRoute route, int typeIdx)
    {
        auto& indices = getIndices(route);
        auto  it      = std::find(indices.begin(), indices.end(), typeIdx);
        if (it == indices.end()) return;  // 未アクティブ → 何もしない

        int pos = (int)(it - indices.begin());

        juce::PopupMenu menu;
        menu.addItem(1, "Remove from chain");
        menu.addSeparator();
        if (pos > 0)                             menu.addItem(2, "Move earlier in chain");
        if (pos < (int)indices.size() - 1)       menu.addItem(3, "Move later in chain");

        menu.showMenuAsync(juce::PopupMenu::Options{}, [this, route, typeIdx](int result)
        {
            if (result == 0) return;

            auto& idxs = getIndices(route);
            auto  it2  = std::find(idxs.begin(), idxs.end(), typeIdx);
            if (it2 == idxs.end()) return;

            auto* fxPtr = getPoolInstance(route, typeIdx);

            if (result == 1)  // Remove
            {
                idxs.erase(it2);
                if (fxPtr) fxPtr->setActive(false);
                if (currentSelectedFX == fxPtr)
                {
                    currentSelectedFX = nullptr;
                    selectedIdx       = -1;
                }
            }
            else if (result == 2)  // Move earlier
            {
                if (it2 != idxs.begin()) std::iter_swap(it2, it2 - 1);
            }
            else if (result == 3)  // Move later
            {
                auto next = std::next(it2);
                if (next != idxs.end()) std::iter_swap(it2, next);
            }

            processor.updateRouteOrder(route, idxs);
            syncStates(transButtons,   transIndices);
            syncStates(tonalButtons,   tonalIndices);
            syncStates(fullMixButtons, fullMixIndices);
            repaint();
            sendChangeMessage();
        });
    }

    // =========================================================================
    // ヘルパー
    // =========================================================================
    void syncStates(std::vector<std::unique_ptr<FxBtn>>& btns,
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

    std::vector<std::unique_ptr<FxBtn>>& getButtons(TargetRoute r) noexcept
    {
        return (r == TargetRoute::Transient) ? transButtons :
               (r == TargetRoute::Tonal)     ? tonalButtons : fullMixButtons;
    }

    AudioEffect* getPoolInstance(TargetRoute r, int idx) noexcept
    {
        return (r == TargetRoute::Transient) ? processor.getTransientPoolInstance(idx) :
               (r == TargetRoute::Tonal)     ? processor.getTonalPoolInstance(idx) :
                                               processor.getFullMixPoolInstance(idx);
    }

    static TargetRoute intToRoute(int i) noexcept
    {
        return (i == 0) ? TargetRoute::Transient :
               (i == 1) ? TargetRoute::Tonal : TargetRoute::FullMix;
    }

    int getLaneForY(int y) const noexcept
    {
        const int sh = getHeight() / 3;
        return (y < sh) ? 0 : (y < sh*2) ? 1 : 2;
    }

    int getSectionBaseY(int lane) const noexcept
    {
        return lane * (getHeight() / 3);
    }

    // =========================================================================
    AnatomyAudioProcessor& processor;

    AudioEffect* currentSelectedFX = nullptr;
    TargetRoute  selectedRoute     = TargetRoute::FullMix;
    int          selectedIdx       = -1;

    std::vector<std::unique_ptr<FxBtn>> transButtons;
    std::vector<std::unique_ptr<FxBtn>> tonalButtons;
    std::vector<std::unique_ptr<FxBtn>> fullMixButtons;

    std::vector<int> transIndices;
    std::vector<int> tonalIndices;
    std::vector<int> fullMixIndices;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectRackPanel)
};
