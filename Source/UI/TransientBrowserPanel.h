#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../PluginProcessor.h"
#include "WaveformComponent.h"

/**
 * TransientBrowserPanel (ノブ完全廃止・フラット集約設計版)
 * 重複していた不要なGAINノブやタイマー干渉スライダーを完全消滅。
 * マウスドラッグ可動レンジのみを100%解放した高効率型アタック置換UI。
 */
class TransientBrowserPanel final : public juce::Component, public juce::FileBrowserListener
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
            processor.clearCustomSampleFromUI(true);
            browseButton.setButtonText("Browse");

            float durationMs = processor.transEndOffsetMs;
            waveformDisplay.setOffsets(0.0f, durationMs, processor.getFileSampleRate());

            juce::AudioBuffer<float> tempTrans, tempTonal;
            processor.getCallbackBuffersSecure(tempTrans, tempTonal);
            waveformDisplay.setBuffer(tempTrans);
            };
        addAndMakeVisible(clearButton);
    }

    ~TransientBrowserPanel() override { closeBrowser(); }

    void paint(juce::Graphics& /*g*/) override
    {
        float currentMax = processor.transEndOffsetMs;
        if (currentMax > 0.0f && waveformDisplay.getHeight() > 0)
        {
            waveformDisplay.setOffsets(processor.transStartOffsetMs, processor.transEndOffsetMs, processor.getFileSampleRate());
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(2, 4);
        browseButton.setBounds(area.removeFromLeft(area.getWidth() / 2).reduced(1));
        clearButton.setBounds(area.reduced(1));
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
            processor.storeCustomSampleFromUI(true, buffer, reader->sampleRate);

            double durationMs = (static_cast<double>(reader->lengthInSamples) / reader->sampleRate) * 1000.0;
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

    std::unique_ptr<BrowserDialog> browserWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransientBrowserPanel)
};