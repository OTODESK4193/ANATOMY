#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../PluginProcessor.h"
#include "WaveformComponent.h"

/**
 * GuardedSlider_Tonal (逆流シャットアウト数理ノブ)
 * ユーザーがノブをドラッグ操作している間、外部の古いタイマー上書き数値を
 * 100%完全にシールド（遮断）し、ピクピクバグを根絶する特製クラス。
 */
struct GuardedSlider_Tonal : public juce::Slider
{
    double snapValue(double attemptedValue, juce::Slider::DragMode dragMode) override
    {
        if (isMouseButtonDown() && dragMode == juce::Slider::notDragging)
        {
            return getValue();
        }
        return juce::Slider::snapValue(attemptedValue, dragMode);
    }
};

class TonalBrowserPanel final : public juce::Component, public juce::FileBrowserListener
{
public:
    TonalBrowserPanel(AnatomyAudioProcessor& p, WaveformComponent& w)
        : processor(p), waveformDisplay(w)
    {
        formatManager.registerBasicFormats();

        browseButton.setButtonText("Browse");
        browseButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey.darker());
        browseButton.setColour(juce::TextButton::textColourOffId, juce::Colours::cyan);
        browseButton.onClick = [this] { openBrowserWindow(); };
        addAndMakeVisible(browseButton);

        clearButton.setButtonText("Reset");
        clearButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey.darker());
        clearButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        clearButton.onClick = [this] {
            processor.clearCustomSampleFromUI(false);
            browseButton.setButtonText("Browse");

            float durationMs = processor.tonalEndOffsetMs;
            startOffsetSlider.setRange(0.0, static_cast<double>(durationMs), 0.1);
            endOffsetSlider.setRange(0.0, static_cast<double>(durationMs), 0.1);
            startOffsetSlider.setValue(0.0);
            endOffsetSlider.setValue(static_cast<double>(durationMs));

            waveformDisplay.setOffsets(0.0f, durationMs, processor.getFileSampleRate());

            juce::AudioBuffer<float> tempTrans, tempTonal;
            processor.getCallbackBuffersSecure(tempTrans, tempTonal);
            waveformDisplay.setBuffer(tempTonal);
            };
        addAndMakeVisible(clearButton);

        auto configureKnob = [this](juce::Slider& s, juce::Label& l, const juce::String& txt, double maxVal) {
            s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 45, 12);
            s.setRange(0.0, maxVal, 0.1);
            s.setColour(juce::Slider::rotarySliderFillColourId, juce::Colours::cyan);
            s.setColour(juce::Slider::thumbColourId, juce::Colours::white);
            addAndMakeVisible(s);

            l.setText(txt, juce::dontSendNotification);
            l.setFont(juce::Font(10.0f, juce::Font::bold));
            l.setJustificationType(juce::Justification::centred);
            l.setColour(juce::Label::textColourId, juce::Colours::cyan.withAlpha(0.7f));
            addAndMakeVisible(l);
            };

        configureKnob(startOffsetSlider, startOffsetLabel, "START", 1000.0);
        configureKnob(endOffsetSlider, endOffsetLabel, "END", 1000.0);
        endOffsetSlider.setValue(1000.0);

        auto onSliderChange = [this] {
            float sVal = static_cast<float>(startOffsetSlider.getValue());
            float eVal = static_cast<float>(endOffsetSlider.getValue());

            // 💥【バグ治療】ノブ変更時にプロセッサ本体の「setOffsetsFromUI」へ値を完全直流フック
            processor.setOffsetsFromUI(false, sVal, eVal);
            };

        startOffsetSlider.onValueChange = onSliderChange;
        endOffsetSlider.onValueChange = onSliderChange;

        gainSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        gainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 45, 12);
        gainSlider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colours::cyan);
        gainSlider.setColour(juce::Slider::thumbColourId, juce::Colours::white);
        addAndMakeVisible(gainSlider);

        gainLabel.setText("GAIN", juce::dontSendNotification);
        gainLabel.setFont(juce::Font(10.0f, juce::Font::bold));
        gainLabel.setJustificationType(juce::Justification::centred);
        gainLabel.setColour(juce::Label::textColourId, juce::Colours::cyan.withAlpha(0.7f));
        addAndMakeVisible(gainLabel);

        gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "tonalMixGain", gainSlider);
    }

    ~TonalBrowserPanel() override { closeBrowser(); }

    // 💥【1000ms上限バグ自動治療】画面が描画される瞬間にプロセッサの最新の全長をノブの可動レンジへ動的同期
    void paint(juce::Graphics& g) override
    {
        float currentMax = processor.tonalEndOffsetMs;
        if (currentMax > 0.0f && endOffsetSlider.getMaximum() != static_cast<double>(currentMax))
        {
            startOffsetSlider.setRange(0.0, static_cast<double>(currentMax), 0.1);
            endOffsetSlider.setRange(0.0, static_cast<double>(currentMax), 0.1);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto topRow = area.removeFromTop(20);
        browseButton.setBounds(topRow.removeFromLeft(topRow.getWidth() / 2).reduced(1));
        clearButton.setBounds(topRow.reduced(1));

        area.removeFromTop(2);

        auto kw = area.getWidth() / 3;

        auto leftKnob = area.removeFromLeft(kw);
        startOffsetLabel.setBounds(leftKnob.removeFromTop(12));
        startOffsetSlider.setBounds(leftKnob);

        auto midKnob = area.removeFromLeft(kw);
        endOffsetLabel.setBounds(midKnob.removeFromTop(12));
        endOffsetSlider.setBounds(midKnob);

        auto rightKnob = area;
        gainLabel.setBounds(rightKnob.removeFromTop(12));
        gainSlider.setBounds(rightKnob);
    }

    void closeBrowser() { browserWindow.reset(); }
    void selectionChanged() override {}

    void fileClicked(const juce::File& file, const juce::MouseEvent&) override
    {
        if (file.isDirectory() || file.getFileExtension().toLowerCase() != ".wav") return;

        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
        if (reader != nullptr)
        {
            juce::AudioBuffer<float> buffer(1, static_cast<int>(reader->lengthInSamples));
            reader->read(&buffer, 0, static_cast<int>(reader->lengthInSamples), 0, true, false);

            processor.customTonalReplacer.loadSample(buffer, reader->sampleRate);
            processor.storeCustomSampleFromUI(false, buffer, reader->sampleRate);

            double durationMs = (static_cast<double>(reader->lengthInSamples) / reader->sampleRate) * 1000.0;
            startOffsetSlider.setRange(0.0, durationMs, 0.1);
            endOffsetSlider.setRange(0.0, durationMs, 0.1);
            startOffsetSlider.setValue(0.0);
            endOffsetSlider.setValue(durationMs);

            waveformDisplay.setBuffer(buffer);
            waveformDisplay.setOffsets(0.0f, static_cast<float>(durationMs), reader->sampleRate);

            browseButton.setButtonText(file.getFileNameWithoutExtension().substring(0, 7));
        }
    }

    void fileDoubleClicked(const juce::File&) override {}
    void browserRootChanged(const juce::File&) override {}

private:
    class BrowserDialog final : public juce::DialogWindow
    {
    public:
        BrowserDialog(const juce::String& name, juce::Colour bg, TonalBrowserPanel& panel)
            : DialogWindow(name, bg, true, true), owner(panel) {
            setUsingNativeTitleBar(true);
        }
        void closeButtonPressed() override { owner.closeBrowser(); }
    private:
        TonalBrowserPanel& owner;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BrowserDialog)
    };

    void openBrowserWindow()
    {
        if (browserWindow != nullptr) { browserWindow->toFront(true); return; }

        auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        auto browser = std::make_unique<juce::FileBrowserComponent>(
            chooserFlags, juce::File::getSpecialLocation(juce::File::userHomeDirectory), nullptr, nullptr
        );
        browser->addListener(this);
        browser->setSize(450, 550);

        browserWindow = std::make_unique<BrowserDialog>("Sustain Sample Browser", juce::Colours::darkgrey.darker().darker(), *this);
        browserWindow->setContentOwned(browser.release(), true);
        browserWindow->centreAroundComponent(this, 450, 550);
        browserWindow->setResizable(true, true);
        browserWindow->setVisible(true);
    }

    AnatomyAudioProcessor& processor;
    WaveformComponent& waveformDisplay;
    juce::AudioFormatManager formatManager;

    juce::TextButton browseButton;
    juce::TextButton clearButton;

    GuardedSlider_Tonal startOffsetSlider;
    GuardedSlider_Tonal endOffsetSlider;
    juce::Label startOffsetLabel;
    juce::Label endOffsetLabel;

    juce::Slider gainSlider;
    juce::Label gainLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;

    std::unique_ptr<BrowserDialog> browserWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TonalBrowserPanel)
};