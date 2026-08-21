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

        // ノブ設定 (4基)
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

        setupKnob(knobFadeIn,  lblFadeIn,  "FADE-IN",   AnatomyColors::accentTonal);
        setupKnob(knobRelease, lblRelease, "RELEASE",   AnatomyColors::accentTonal);
        setupKnob(knobPitch,   lblPitch,   "PITCH (st)",AnatomyColors::accentTonal);
        setupKnob(knobGain,    lblGain,    "GAIN (dB)",  AnatomyColors::accentTonal);

        attachFadeIn = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.apvts, "clickCurve", knobFadeIn);
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
        g.drawText("TONAL  (BODY / HARMONICS)", 12, 6, getWidth() - 170, 20, juce::Justification::centredLeft);

        // ヘッダー下仕切り線
        g.setColour(AnatomyColors::panelLine);
        g.fillRect(10, 30, getWidth() - 20, 1);
    }

    void resized() override
    {
        // ヘッダーボタン: 右上
        int btnW = 54, btnH = 20, gap = 4;
        int bx = getWidth() - (btnW * 3 + gap * 2) - 10;
        browseBtn.setBounds(bx, 6, btnW, btnH);
        resetBtn.setBounds(bx + btnW + gap, 6, btnW, btnH);
        exportBtn.setBounds(bx + (btnW + gap) * 2, 6, btnW, btnH);

        // 波形エリア
        waveform.setBounds(10, 36, getWidth() - 20, getHeight() - 120);

        // 下部ノブエリア (4基)
        int knobY = getHeight() - 80;
        int totalW = getWidth() - 20;
        int kw = totalW / 4;

        auto placeKnob = [&](ValueKnob& k, juce::Label& l, int index) {
            int x = 10 + index * kw;
            l.setBounds(x, knobY, kw, 14);
            k.setBounds(x + (kw - 56) / 2, knobY + 16, 56, 56);
        };

        placeKnob(knobFadeIn,  lblFadeIn,  0);
        placeKnob(knobRelease, lblRelease, 1);
        placeKnob(knobPitch,   lblPitch,   2);
        placeKnob(knobGain,    lblGain,    3);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (onSelectLane) onSelectLane();
    }

    void setSelected(bool sel) { if (isSelected != sel) { isSelected = sel; repaint(); } }

    std::function<void()> onSelectLane;
    std::function<void()> onSampleChanged;

    // --- D&D Target ---
    bool isInterestedInFileDrag(const juce::StringArray& files) override
    {
        if (files.size() == 0) return false;
        juce::File f(files[0]);
        auto ext = f.getFileExtension().toLowerCase();
        return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".mp3" || ext == ".flac";
    }

    void filesDropped(const juce::StringArray& files, int, int) override
    {
        if (files.size() > 0)
            loadFile(juce::File(files[0]));
    }

    void fileClicked(const juce::File& file, const juce::MouseEvent&) override
    {
        if (!file.isDirectory())
            loadFile(file);
    }
    void fileDoubleClicked(const juce::File&) override {}
    void browserRootChanged(const juce::File&) override {}
    void selectionChanged() override {}

private:
    void triggerExport()
    {
        juce::File tempWav = processor.createTemporaryWavForExport(2);
        if (tempWav.existsAsFile())
        {
            juce::StringArray files;
            files.add(tempWav.getFullPathName());
            juce::DragAndDropContainer::performExternalDragDropOfFiles(files, false, this);
        }
    }

    void loadFile(const juce::File& file)
    {
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
        if (reader != nullptr)
        {
            juce::AudioBuffer<float> buffer(1, static_cast<int>(reader->lengthInSamples));
            reader->read(&buffer, 0, static_cast<int>(reader->lengthInSamples), 0, true, false);

            processor.customTonalReplacer.loadSample(buffer, reader->sampleRate);
            processor.storeCustomSampleFromUI(false, buffer, reader->sampleRate);

            double durationMs = (static_cast<double>(reader->lengthInSamples) / reader->sampleRate) * 1000.0;
            waveform.setBuffer(buffer);
            waveform.setOffsets(0.0f, static_cast<float>(durationMs), reader->sampleRate);

            browseBtn.setButtonText(file.getFileNameWithoutExtension().substring(0, 6));
            if (onSampleChanged) onSampleChanged();
        }
    }

    void closeBrowser() { browserWindow.reset(); }
    void openBrowser()
    {
        if (browserWindow != nullptr) { browserWindow->toFront(true); return; }
        auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        auto browser = std::make_unique<juce::FileBrowserComponent>(
            chooserFlags, juce::File::getSpecialLocation(juce::File::userHomeDirectory), nullptr, nullptr
        );
        browser->addListener(this);
        browser->setSize(420, 500);

        class Dialog final : public juce::DialogWindow {
        public:
            Dialog(const juce::String& name, juce::Colour bg, TonalLaneView& o)
                : DialogWindow(name, bg, true, true), owner(o) { setUsingNativeTitleBar(true); }
            void closeButtonPressed() override { owner.closeBrowser(); }
        private:
            TonalLaneView& owner;
        };

        browserWindow = std::make_unique<Dialog>("Tonal Sample Browser", AnatomyColors::panel, *this);
        browserWindow->setContentOwned(browser.release(), true);
        browserWindow->centreAroundComponent(this, 420, 500);
        browserWindow->setResizable(true, true);
        browserWindow->setVisible(true);
    }

    AnatomyAudioProcessor& processor;
    WaveformComponent waveform;
    juce::AudioFormatManager formatManager;

    juce::TextButton browseBtn;
    juce::TextButton resetBtn;
    juce::TextButton exportBtn;

    ValueKnob knobFadeIn;
    ValueKnob knobRelease;
    ValueKnob knobPitch;
    ValueKnob knobGain;
    juce::Label lblFadeIn;
    juce::Label lblRelease;
    juce::Label lblPitch;
    juce::Label lblGain;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachFadeIn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachRelease;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachPitch;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachGain;

    std::unique_ptr<juce::DialogWindow> browserWindow;
    bool isSelected = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TonalLaneView)
};
