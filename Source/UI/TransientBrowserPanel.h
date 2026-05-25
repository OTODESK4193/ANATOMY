#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../PluginProcessor.h"
#include "WaveformComponent.h"

class TransientBrowserPanel : public juce::Component, public juce::FileBrowserListener
{
public:
    TransientBrowserPanel(AnatomyAudioProcessor& p, WaveformComponent& w)
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
            processor.customTransientReplacer.clearSample();
            browseButton.setButtonText("Browse");

            startOffsetSlider.setRange(0.0, 500.0);
            endOffsetSlider.setRange(0.0, 500.0);
            startOffsetSlider.setValue(0.0);
            endOffsetSlider.setValue(500.0);

            waveformDisplay.setOffsets(0.0f, 0.0f, 44100.0);

            juce::AudioBuffer<float> tempTrans, tempTonal;
            processor.getCallbackBuffersSecure(tempTrans, tempTonal);
            waveformDisplay.setBuffer(tempTrans);
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
            l.setFont(juce::Font(9.0f, juce::Font::bold));
            l.setJustificationType(juce::Justification::centred);
            l.setColour(juce::Label::textColourId, juce::Colours::cyan.withAlpha(0.7f));
            addAndMakeVisible(l);
            };

        configureKnob(startOffsetSlider, startOffsetLabel, "START", 500.0);
        configureKnob(endOffsetSlider, endOffsetLabel, "END", 500.0);
        endOffsetSlider.setValue(500.0);

        auto onSliderChange = [this] {
            float sVal = static_cast<float>(startOffsetSlider.getValue());
            float eVal = static_cast<float>(endOffsetSlider.getValue());
            processor.customTransientReplacer.setStartOffsetMs(sVal);
            processor.customTransientReplacer.setEndOffsetMs(eVal);
            waveformDisplay.setOffsets(sVal, eVal, processor.customTransientReplacer.getSourceSampleRate());
            };

        startOffsetSlider.onValueChange = onSliderChange;
        endOffsetSlider.onValueChange = onSliderChange;
    }

    ~TransientBrowserPanel() override { closeBrowser(); }

    void resized() override
    {
        auto area = getLocalBounds();
        auto topRow = area.removeFromTop(20);
        browseButton.setBounds(topRow.removeFromLeft(topRow.getWidth() / 2).reduced(1));
        clearButton.setBounds(topRow.reduced(1));

        area.removeFromTop(2);

        // STARTノブとENDノブを横並びで配置
        auto knobArea = area;
        auto leftKnob = knobArea.removeFromLeft(knobArea.getWidth() / 2);

        startOffsetLabel.setBounds(leftKnob.removeFromTop(12));
        startOffsetSlider.setBounds(leftKnob);

        endOffsetLabel.setBounds(knobArea.removeFromTop(12));
        endOffsetSlider.setBounds(knobArea);
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

            processor.customTransientReplacer.loadSample(buffer, reader->sampleRate);

            // 【自動追従】Wavの長さに合わせてノブの上限値を自動拡張
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
    class BrowserDialog : public juce::DialogWindow
    {
    public:
        BrowserDialog(const juce::String& name, juce::Colour bg, TransientBrowserPanel& panel)
            : DialogWindow(name, bg, true, true), owner(panel) {
            setUsingNativeTitleBar(true);
        }
        void closeButtonPressed() override { owner.closeBrowser(); }
    private:
        TransientBrowserPanel& owner;
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

        browserWindow = std::make_unique<BrowserDialog>("Transient Sample Browser", juce::Colours::darkgrey.darker().darker(), *this);
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

    juce::Slider startOffsetSlider;
    juce::Slider endOffsetSlider;
    juce::Label startOffsetLabel;
    juce::Label endOffsetLabel;

    std::unique_ptr<BrowserDialog> browserWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransientBrowserPanel)
};