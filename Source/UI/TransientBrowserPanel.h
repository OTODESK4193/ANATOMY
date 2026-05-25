#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../PluginProcessor.h"

/**
 * TransientBrowserPanel (Header-Only UI Module - Fixed Compliance)
 * * JUCE 7/8 の FileBrowserListener および DialogWindow の仕様に完全適合させた修正版です。
 */
class TransientBrowserPanel : public juce::Component, public juce::FileBrowserListener
{
public:
    TransientBrowserPanel(AnatomyAudioProcessor& p) : processor(p)
    {
        formatManager.registerBasicFormats();

        // 1. 差し替え用ブラウザ起動ボタン
        browseButton.setButtonText("Browse");
        browseButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey.darker());
        browseButton.setColour(juce::TextButton::textColourOffId, juce::Colours::cyan);
        browseButton.onClick = [this] { openBrowserWindow(); };
        addAndMakeVisible(browseButton);

        // 2. 差し替え解除（リセット）ボタン
        clearButton.setButtonText("Reset");
        clearButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey.darker());
        clearButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        clearButton.onClick = [this] {
            processor.customTransientReplacer.clearSample();
            browseButton.setButtonText("Browse");
            };
        addAndMakeVisible(clearButton);

        // 3. 差し替えサンプルのStart位置調整ノブ
        startOffsetSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        startOffsetSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 55, 14);
        startOffsetSlider.setRange(0.0, 500.0, 0.1);
        startOffsetSlider.setValue(0.0);
        startOffsetSlider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colours::cyan);
        startOffsetSlider.setColour(juce::Slider::thumbColourId, juce::Colours::white);
        startOffsetSlider.onValueChange = [this] {
            processor.customTransientReplacer.setStartOffsetMs(static_cast<float>(startOffsetSlider.getValue()));
            };
        addAndMakeVisible(startOffsetSlider);

        startOffsetLabel.setText("START (ms)", juce::dontSendNotification);
        startOffsetLabel.setFont(juce::Font(10.0f, juce::Font::bold));
        startOffsetLabel.setJustificationType(juce::Justification::centred);
        startOffsetLabel.setColour(juce::Label::textColourId, juce::Colours::cyan.withAlpha(0.8f));
        addAndMakeVisible(startOffsetLabel);
    }

    ~TransientBrowserPanel() override
    {
        closeBrowser();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto topRow = area.removeFromTop(22);
        browseButton.setBounds(topRow.removeFromLeft(topRow.getWidth() / 2).reduced(1));
        clearButton.setBounds(topRow.reduced(1));

        area.removeFromTop(4);
        startOffsetLabel.setBounds(area.removeFromTop(12));
        startOffsetSlider.setBounds(area);
    }

    /**
     * ウィンドウを安全に破棄する内部関数
     */
    void closeBrowser()
    {
        browserWindow.reset();
    }

    // ==============================================================================
    // --- JUCE FileBrowserListener 正式オーバーライド関数群 ---
    // ==============================================================================
    void selectionChanged() override {} // 必須関数の追加

    void fileClicked(const juce::File& file, const juce::MouseEvent&) override
    {
        if (file.isDirectory() || file.getFileExtension().toLowerCase() != ".wav") return;

        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
        if (reader != nullptr)
        {
            juce::AudioBuffer<float> buffer(1, static_cast<int>(reader->lengthInSamples));
            reader->read(&buffer, 0, static_cast<int>(reader->lengthInSamples), 0, true, false);

            processor.customTransientReplacer.loadSample(buffer, reader->sampleRate);
            browseButton.setButtonText(file.getFileNameWithoutExtension().substring(0, 7));
        }
    }

    void fileDoubleClicked(const juce::File&) override {} // 引数をJUCE標準規格に修正
    void browserRootChanged(const juce::File&) override {}

private:
    // --- 閉じるボタンの挙動を正しくハンドリングするためのインナークラス ---
    class BrowserDialog : public juce::DialogWindow
    {
    public:
        BrowserDialog(const juce::String& name, juce::Colour bg, TransientBrowserPanel& panel)
            : DialogWindow(name, bg, true, true), owner(panel)
        {
            setUsingNativeTitleBar(true);
        }

        void closeButtonPressed() override
        {
            owner.closeBrowser(); // 閉じるボタンが押されたら親パネルのポインタをリセットする
        }

    private:
        TransientBrowserPanel& owner;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BrowserDialog)
    };

    void openBrowserWindow()
    {
        if (browserWindow != nullptr)
        {
            browserWindow->toFront(true);
            return;
        }

        auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        auto browser = std::make_unique<juce::FileBrowserComponent>(
            chooserFlags,
            juce::File::getSpecialLocation(juce::File::userHomeDirectory),
            nullptr,
            nullptr
        );

        browser->addListener(this);
        browser->setSize(450, 550);

        // 修正した堅牢なダイアログウィンドウクラスで実体化
        browserWindow = std::make_unique<BrowserDialog>(
            "Transient Sample Browser",
            juce::Colours::darkgrey.darker().darker(),
            *this
        );

        browserWindow->setContentOwned(browser.release(), true);
        browserWindow->centreAroundComponent(this, 450, 550);
        browserWindow->setResizable(true, true);
        browserWindow->setVisible(true);
    }

    AnatomyAudioProcessor& processor;
    juce::AudioFormatManager formatManager;

    juce::TextButton browseButton;
    juce::TextButton clearButton;
    juce::Slider startOffsetSlider;
    juce::Label startOffsetLabel;

    std::unique_ptr<BrowserDialog> browserWindow; // 修正した型で保持

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransientBrowserPanel)
};