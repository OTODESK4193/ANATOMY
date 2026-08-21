// ==========================================
// File: FullMixLaneView.h
// ANATOMY 2段目 左側: FullMix レーンビュー (幅50%)
// [EXPORT] [BEFORE] [SNAP] ヘッダー統合版
// ==========================================
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../PluginProcessor.h"
#include "WaveformComponent.h"
#include "GlowToggle.h"
#include "DragExportButton.h"
#include "ColorPalette.h"
#include <functional>

class FullMixLaneView final : public juce::Component,
                              public juce::FileDragAndDropTarget
{
public:
    FullMixLaneView(AnatomyAudioProcessor& p) : processor(p)
    {
        formatManager.registerBasicFormats();

        // 波形設定
        waveform.setLaneProperties(p, 0);
        waveform.onFocusClicked = [this] { if (onSelectLane) onSelectLane(); };
        addAndMakeVisible(waveform);

        // BEFORE トグルボタン
        beforeToggle.setToggleState(false, juce::dontSendNotification);
        beforeToggle.onClick = [this] {
            processor.beforeAfterBypasser.setBeforeStatus(beforeToggle.getToggleState());
            processor.offlineMixRenderer.triggerRender();
            if (onBeforeChanged) onBeforeChanged();
            repaint();
        };
        addAndMakeVisible(beforeToggle);

        // D&D対応 EXPORT ボタン
        exportBtn.setFileGenerator([this] {
            return processor.createTemporaryWavForExport(0);
        });
        addAndMakeVisible(exportBtn);

        // SNAP ボタン (ゼロクロス吸着 ON/OFF)
        snapToggle.setToggleState(true, juce::dontSendNotification);
        snapToggle.onClick = [this] {
            waveform.setSnapEnabled(snapToggle.getToggleState());
        };
        addAndMakeVisible(snapToggle);
    }

    ~FullMixLaneView() override = default;

    void setWaveBuffer(const juce::AudioBuffer<float>& buf) { waveform.setBuffer(buf); }
    void setWaveRatioData(const std::vector<float>& ratios) { waveform.setRatioData(ratios); }
    void setWaveOffsets(float s, float e, double sr) { waveform.setOffsets(s, e, sr); }

    void setSelected(bool selected)
    {
        if (isSelected != selected)
        {
            isSelected = selected;
            waveform.setSelected(selected);
            repaint();
        }
    }

    bool isBeforeActive() const noexcept { return beforeToggle.getToggleState(); }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        // パネル背景
        g.setColour(AnatomyColors::panel);
        g.fillRoundedRectangle(bounds, 8.0f);

        // アクセント枠線
        g.setColour(isSelected ? AnatomyColors::accentFull.withAlpha(0.85f) : AnatomyColors::panelLine);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, isSelected ? 1.5f : 1.0f);

        // ヘッダー部
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.setColour(AnatomyColors::accentFull);
        g.drawText("FULLMIX", 12, 6, 120, 20, juce::Justification::centredLeft);

        // ヘッダー下仕切り線
        g.setColour(AnatomyColors::panelLine);
        g.fillRect(10, 30, getWidth() - 20, 1);
    }

    void resized() override
    {
        // ヘッダーボタン: 右上 [EXPORT 50] [BEFORE 64] [SNAP 56]
        int gap = 4;
        int bx = getWidth() - 8;

        bx -= 56; snapToggle.setBounds(bx, 5, 56, 21);
        bx -= (gap + 64); beforeToggle.setBounds(bx, 5, 64, 21);
        bx -= (gap + 50); exportBtn.setBounds(bx, 5, 50, 21);

        // 波形エリア (全体幅)
        waveform.setBounds(10, 36, getWidth() - 20, getHeight() - 44);
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        if (onSelectLane) onSelectLane();
    }

    // --- FileDragAndDropTarget ---
    bool isInterestedInFileDrag(const juce::StringArray& files) override
    {
        if (files.size() != 1) return false;
        auto ext = juce::File(files[0]).getFileExtension().toLowerCase();
        return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".mp3" || ext == ".flac";
    }

    void filesDropped(const juce::StringArray& files, int, int) override
    {
        if (files.size() >= 1 && onFileDropped)
            onFileDropped(juce::File(files[0]));
    }

    std::function<void()> onSelectLane;
    std::function<void()> onBeforeChanged;
    std::function<void(const juce::File&)> onFileDropped;

    WaveformComponent waveform;

private:
    AnatomyAudioProcessor& processor;
    juce::AudioFormatManager formatManager;

    DragExportButton exportBtn{ "EXPORT", AnatomyColors::accentFull };
    GlowToggle beforeToggle{ "BEFORE", AnatomyColors::accentFull };
    GlowToggle snapToggle{ "SNAP", AnatomyColors::mint };

    bool isSelected = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FullMixLaneView)
};
