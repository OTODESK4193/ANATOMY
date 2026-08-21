// ==========================================
// File: TonalLaneView.h
// ANATOMY 3段目 右側: Tonal レーンビュー (幅50%)
// ==========================================
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../PluginProcessor.h"
#include "WaveformComponent.h"
#include "ValueKnob.h"
#include "GlowToggle.h"
#include "ColorPalette.h"
#include <functional>
#include <memory>

class TonalLaneView final : public juce::Component,
                            public juce::FileBrowserListener,
                            public juce::FileDragAndDropTarget
{
public:
    TonalLaneView(AnatomyAudioProcessor& p) : processor(p)
    {
        formatManager.registerBasicFormats();

        // 波形設定
        waveform.setLaneProperties(p, 2);
        waveform.onFocusClicked = [this] { if (onSelectLane) onSelectLane(); };
        addAndMakeVisible(waveform);

        // ボタン類
        auto styleBtn = [](juce::TextButton& b, juce::Colour c) {
            b.setColour(juce::TextButton::buttonColourId, AnatomyColors::knobTrack);
            b.setColour(juce::TextButton::textColourOffId, c);
        };

        styleBtn(browseBtn, AnatomyColors::accentTonal);
        styleBtn(resetBtn, AnatomyColors::textDim);
        styleBtn(exportBtn, AnatomyColors::accentTonal);

        browseBtn.setButtonText("BROWSE");
        browseBtn.onClick = [this] { openBrowser(); };
        addAndMakeVisible(browseBtn);

        resetBtn.setButtonText("RESET");
        resetBtn.onClick = [this] {
            processor.clearCustomSampleFromUI(false);
            browseBtn.setButtonText("BROWSE");
            float durationMs = processor.tonalEndOffsetMs;
            waveform.setOffsets(0.0f, durationMs, processor.getFileSampleRate());
            juce::AudioBuffer<float> tempTrans, tempTonal;
            processor.getCallbackBuffersSecure(tempTrans, tempTonal);
            waveform.setBuffer(tempTonal);
            if (onSampleChanged) onSampleChanged();
        };
        addAndMakeVisible(resetBtn);

        exportBtn.setButtonText("EXPORT");
        exportBtn.onClick = [this] { triggerExport(); };
        addAndMakeVisible(exportBtn);

        // SOLO ボタン
        soloToggle.onClick = [this] {
            processor.setLaneSolo(false, soloToggle.getToggleState());
            if (onSoloChanged) onSoloChanged();
        };
        addAndMakeVisible(soloToggle);

        // ノブ設定 (3基: RELEASE, PITCH, GAIN - FadeInはView操作に一本化)
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

        setupKnob(knobRelease, lblRelease, "RELEASE",    AnatomyColors::accentTonal);
        setupKnob(knobPitch,   lblPitch,   "PITCH (st)", AnatomyColors::accentTonal);
        setupKnob(knobGain,    lblGain,    "GAIN (dB)",   AnatomyColors::accentTonal);

        attachRelease = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.apvts, "sustainRelease", knobRelease);
        attachPitch = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.apvts, "tonalPitch", knobPitch);
        attachGain = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.apvts, "tonalMixGain", knobGain);
    }

    ~TonalLaneView() override { closeBrowser(); }

    void setWaveBuffer(const juce::AudioBuffer<float>& buf) { waveform.setBuffer(buf); }
    void setWaveOffsets(float s, float e, double sr) { waveform.setOffsets(s, e, sr); }
    void setWaveFade(float inMs, float outMs, float inTension, float outTension) { waveform.setFade(inMs, outMs, inTension, outTension); }

    void updateSoloState()
    {
        soloToggle.setToggleState(processor.isLaneSolo(false), juce::dontSendNotification);
    }

    void setSelected(bool selected)
    {
        if (isSelected != selected) { isSelected = selected; repaint(); }
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        // パネル背景
        g.setColour(AnatomyColors::panel);
        g.fillRoundedRectangle(bounds, 8.0f);

        // アクセント枠線
        g.setColour(isSelected ? AnatomyColors::accentTonal.withAlpha(0.85f) : AnatomyColors::panelLine);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, isSelected ? 1.5f : 1.0f);

        // ヘッダー部
        g.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
        g.setColour(AnatomyColors::accentTonal);
        g.drawText("TONAL  (BODY / HARMONICS)", 12, 6, getWidth() - 220, 20, juce::Justification::centredLeft);

        // ヘッダー下仕切り線
        g.setColour(AnatomyColors::panelLine);
        g.fillRect(10, 30, getWidth() - 20, 1);
    }

    void resized() override
    {
        // ヘッダーボタン: 右上 (BROWSE, RESET, EXPORT, SOLO)
        int btnW = 50, btnH = 20, gap = 4;
        int bx = getWidth() - (btnW * 3 + 46 + gap * 3) - 10;
        browseBtn.setBounds(bx, 6, btnW, btnH);
        resetBtn.setBounds(bx + btnW + gap, 6, btnW, btnH);
        exportBtn.setBounds(bx + (btnW + gap) * 2, 6, btnW, btnH);
        soloToggle.setBounds(bx + (btnW + gap) * 3, 6, 46, btnH);

        // 波形エリア
        waveform.setBounds(10, 36, getWidth() - 20, getHeight() - 120);

        // 下部ノブエリア (3基均等配置: RELEASE, PITCH, GAIN)
        int knobY = getHeight() - 80;
        int totalW = getWidth() - 20;
        int kw = totalW / 3;

        auto placeKnob = [&](ValueKnob& k, juce::Label& l, int index) {
            int x = 10 + index * kw;
            l.setBounds(x, knobY, kw, 14);
            k.setBounds(x + (kw - 56) / 2, knobY + 16, 56, 56);
        };

        placeKnob(knobRelease, lblRelease, 0);
        placeKnob(knobPitch,   lblPitch,   1);
        placeKnob(knobGain,    lblGain,    2);

        if (browserComponent != nullptr)
            browserComponent->setBounds(getLocalBounds().reduced(10));
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

    // --- FileBrowserListener ---
    void selectionChanged() override {}
    void fileClicked(const juce::File& file, const juce::MouseEvent&) override
    {
        if (!file.isDirectory()) { loadFile(file); closeBrowser(); }
    }
    void fileDoubleClicked(const juce::File& file) override
    {
        if (!file.isDirectory()) { loadFile(file); closeBrowser(); }
    }
    void browserRootChanged(const juce::File&) override {}

    std::function<void()> onSelectLane;
    std::function<void()> onSampleChanged;
    std::function<void()> onSoloChanged;

private:
    void openBrowser()
    {
        if (browserComponent != nullptr) { closeBrowser(); return; }
        browserTree = std::make_unique<juce::FileBrowserComponent>(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            juce::File::getSpecialLocation(juce::File::userMusicDirectory),
            nullptr, nullptr);
        browserTree->addListener(this);
        browserComponent = std::make_unique<juce::Component>();
        browserComponent->addAndMakeVisible(*browserTree);
        browserTree->setBounds(0, 0, getWidth() - 20, getHeight() - 20);
        addAndMakeVisible(*browserComponent);
        browserComponent->setBounds(getLocalBounds().reduced(10));
        browseBtn.setButtonText("CLOSE");
    }

    void closeBrowser()
    {
        if (browserComponent != nullptr)
        {
            removeChildComponent(browserComponent.get());
            browserComponent.reset();
            browserTree.reset();
            browseBtn.setButtonText(processor.isCustomSampleLoaded(false) ? "CUSTOM" : "BROWSE");
        }
    }

    void loadFile(const juce::File& file)
    {
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
        if (reader != nullptr)
        {
            juce::AudioBuffer<float> tempBuf((int)reader->numChannels, (int)reader->lengthInSamples);
            reader->read(&tempBuf, 0, (int)reader->lengthInSamples, 0, true, true);
            processor.storeCustomSampleFromUI(false, tempBuf, reader->sampleRate);
            browseBtn.setButtonText(file.getFileNameWithoutExtension().substring(0, 8));
            waveform.setOffsets(0.0f, (float)(tempBuf.getNumSamples() / reader->sampleRate) * 1000.0f, reader->sampleRate);
            waveform.setBuffer(tempBuf);
            if (onSampleChanged) onSampleChanged();
        }
    }

    void triggerExport()
    {
        juce::File wav = processor.createTemporaryWavForExport(2); // 2 = Tonal
        if (wav.existsAsFile())
        {
            juce::StringArray files;
            files.add(wav.getFullPathName());
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
                container->performExternalDragDropOfFiles(files, false, this);
        }
    }

    AnatomyAudioProcessor& processor;
    juce::AudioFormatManager formatManager;

    WaveformComponent waveform;

    juce::TextButton browseBtn;
    juce::TextButton resetBtn;
    juce::TextButton exportBtn;
    GlowToggle soloToggle{ "SOLO", AnatomyColors::accentTonal };

    ValueKnob knobRelease;
    ValueKnob knobPitch;
    ValueKnob knobGain;

    juce::Label lblRelease;
    juce::Label lblPitch;
    juce::Label lblGain;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachRelease;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachPitch;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachGain;

    std::unique_ptr<juce::Component> browserComponent;
    std::unique_ptr<juce::FileBrowserComponent> browserTree;

    bool isSelected = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TonalLaneView)
};
