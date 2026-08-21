// ==========================================
// File: TransientLaneView.h
// ANATOMY 3段目 左側: Transient レーンビュー (幅50%)
// ==========================================
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../PluginProcessor.h"
#include "WaveformComponent.h"
#include "ValueKnob.h"
#include "GlowToggle.h"
#include "DragExportButton.h"
#include "ColorPalette.h"
#include <functional>
#include <memory>

class TransientLaneView final : public juce::Component,
                                public juce::FileDragAndDropTarget
{
public:
    TransientLaneView(AnatomyAudioProcessor& p) : processor(p)
    {
        formatManager.registerBasicFormats();

        // 波形設定
        waveform.setLaneProperties(p, 1);
        waveform.onFocusClicked = [this] { if (onSelectLane) onSelectLane(); };
        addAndMakeVisible(waveform);

        // ボタン類
        auto styleBtn = [](juce::TextButton& b, juce::Colour c) {
            b.setColour(juce::TextButton::buttonColourId, AnatomyColors::knobTrack);
            b.setColour(juce::TextButton::textColourOffId, c);
        };

        styleBtn(browseBtn, AnatomyColors::accentTransient);
        styleBtn(resetBtn, AnatomyColors::textDim);

        browseBtn.setButtonText("BROWSE");
        browseBtn.onClick = [this] { openBrowser(); };
        addAndMakeVisible(browseBtn);

        resetBtn.setButtonText("RESET");
        resetBtn.onClick = [this] {
            processor.clearCustomSampleFromUI(1);
            browseBtn.setButtonText("BROWSE");
            processor.setFadeFromUI(1, 0.0f, 0.0f, 0.0f, 0.0f);
            float durationMs = processor.transEndOffsetMs;
            waveform.setOffsets(0.0f, durationMs, processor.getFileSampleRate());
            waveform.setFade(0.0f, 0.0f, 0.0f, 0.0f);
            if (onSampleChanged) onSampleChanged();
        };
        addAndMakeVisible(resetBtn);

        // D&D対応 EXPORT ボタン
        exportBtn.setFileGenerator([this] { return processor.createTemporaryWavForExport(1); });
        addAndMakeVisible(exportBtn);

        // SOLO ボタン
        soloToggle.onClick = [this] {
            processor.setLaneSolo(1, soloToggle.getToggleState());
            if (onSoloChanged) onSoloChanged();
        };
        addAndMakeVisible(soloToggle);

        // SNAP ボタン (ゼロクロス吸着 ON/OFF)
        snapToggle.setToggleState(true, juce::dontSendNotification);
        snapToggle.onClick = [this] {
            waveform.setSnapEnabled(snapToggle.getToggleState());
        };
        addAndMakeVisible(snapToggle);

        // ノブ設定 (3基: CLICK HOLD, PITCH, GAIN)
        auto setupKnob = [this](ValueKnob& k, juce::Label& l, const juce::String& text, juce::Colour c) {
            k.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            k.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 58, 14);
            k.setColour(juce::Slider::rotarySliderFillColourId, c);
            k.setColour(juce::Slider::textBoxTextColourId, AnatomyColors::text);
            k.setPopupDisplayEnabled(true, true, this);
            addAndMakeVisible(k);

            l.setText(text, juce::dontSendNotification);
            l.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
            l.setJustificationType(juce::Justification::centred);
            l.setColour(juce::Label::textColourId, c.withAlpha(0.9f));
            addAndMakeVisible(l);
        };

        setupKnob(knobClickHold, lblClickHold, "CLICK HOLD", AnatomyColors::accentTransient);
        setupKnob(knobPitch,     lblPitch,     "PITCH (st)",  AnatomyColors::accentTransient);
        setupKnob(knobGain,      lblGain,      "GAIN (dB)",   AnatomyColors::accentTransient);

        attachClickHold = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.apvts, "clickLength", knobClickHold);
        attachPitch = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.apvts, "transPitch", knobPitch);
        attachGain = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.apvts, "transMixGain", knobGain);
    }

    ~TransientLaneView() override = default;

    void setWaveBuffer(const juce::AudioBuffer<float>& buf) { waveform.setBuffer(buf); }
    void setWaveOffsets(float s, float e, double sr) { waveform.setOffsets(s, e, sr); }
    void setWaveFade(float inMs, float outMs, float inTension, float outTension) { waveform.setFade(inMs, outMs, inTension, outTension); }

    void updateSoloState()
    {
        soloToggle.setToggleState(processor.isLaneSolo(1), juce::dontSendNotification);
    }

    void resetCustomSampleState()
    {
        browseBtn.setButtonText("BROWSE");
    }

    void setSelected(bool selected)
    {
        if (isSelected != selected)
        {
            isSelected = selected;
            waveform.setSelected(selected);
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        // パネル背景
        g.setColour(AnatomyColors::panel);
        g.fillRoundedRectangle(bounds, 8.0f);

        // アクセント枠線
        g.setColour(isSelected ? AnatomyColors::accentTransient.withAlpha(0.85f) : AnatomyColors::panelLine);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, isSelected ? 1.5f : 1.0f);

        // ヘッダー部
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.setColour(AnatomyColors::accentTransient);
        g.drawText("TRANSIENT (CLICK / ATTACK)", 12, 6, getWidth() - 250, 20, juce::Justification::centredLeft);

        // ヘッダー下仕切り線
        g.setColour(AnatomyColors::panelLine);
        g.fillRect(10, 30, getWidth() - 20, 1);
    }

    void resized() override
    {
        // ヘッダーボタン: 右上 (BROWSE 52, RESET 44, EXPORT 50, SOLO 56, SNAP 56)
        int gap = 4;
        int bx = getWidth() - 8;

        bx -= 56; snapToggle.setBounds(bx, 5, 56, 21);
        bx -= (gap + 56); soloToggle.setBounds(bx, 5, 56, 21);
        bx -= (gap + 50); exportBtn.setBounds(bx, 5, 50, 21);
        bx -= (gap + 44); resetBtn.setBounds(bx, 5, 44, 21);
        bx -= (gap + 52); browseBtn.setBounds(bx, 5, 52, 21);

        // 波形エリア
        waveform.setBounds(10, 36, getWidth() - 20, getHeight() - 120);

        // 下部ノブエリア (3基均等配置)
        int knobY = getHeight() - 80;
        int totalW = getWidth() - 20;
        int kw = totalW / 3;

        auto placeKnob = [&](ValueKnob& k, juce::Label& l, int index) {
            int x = 10 + index * kw;
            l.setBounds(x, knobY, kw, 14);
            k.setBounds(x + (kw - 56) / 2, knobY + 16, 56, 56);
        };

        placeKnob(knobClickHold, lblClickHold, 0);
        placeKnob(knobPitch,     lblPitch,     1);
        placeKnob(knobGain,      lblGain,      2);
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
        return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac";
    }

    void filesDropped(const juce::StringArray& files, int, int) override
    {
        if (files.size() >= 1) loadFile(juce::File(files[0]));
    }

    std::function<void()> onSelectLane;
    std::function<void()> onSampleChanged;
    std::function<void()> onSoloChanged;

private:
    void openBrowser()
    {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Select Audio Sample for Transient",
            juce::File::getSpecialLocation(juce::File::userMusicDirectory),
            "*.wav;*.aif;*.aiff;*.flac;*.mp3");

        fileChooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file.existsAsFile())
                {
                    loadFile(file);
                }
            });
    }

    void loadFile(const juce::File& file)
    {
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
        if (reader != nullptr)
        {
            juce::AudioBuffer<float> tempBuf((int)reader->numChannels, (int)reader->lengthInSamples);
            reader->read(&tempBuf, 0, (int)reader->lengthInSamples, 0, true, true);
            processor.storeCustomSampleFromUI(1, tempBuf, reader->sampleRate);
            browseBtn.setButtonText(file.getFileNameWithoutExtension().substring(0, 8));
            waveform.setOffsets(0.0f, (float)(tempBuf.getNumSamples() / reader->sampleRate) * 1000.0f, reader->sampleRate);
            waveform.setBuffer(tempBuf);
            if (onSampleChanged) onSampleChanged();
        }
    }

    AnatomyAudioProcessor& processor;
    juce::AudioFormatManager formatManager;

    WaveformComponent waveform;

    juce::TextButton browseBtn;
    juce::TextButton resetBtn;
    DragExportButton exportBtn{ "EXPORT", AnatomyColors::accentTransient };
    GlowToggle soloToggle{ "SOLO", AnatomyColors::accentTransient };
    GlowToggle snapToggle{ "SNAP", AnatomyColors::mint };

    ValueKnob knobClickHold;
    ValueKnob knobPitch;
    ValueKnob knobGain;

    juce::Label lblClickHold;
    juce::Label lblPitch;
    juce::Label lblGain;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachClickHold;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachPitch;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachGain;

    std::unique_ptr<juce::FileChooser> fileChooser;

    bool isSelected = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransientLaneView)
};
