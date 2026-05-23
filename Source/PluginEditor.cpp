#include "PluginProcessor.h"
#include "PluginEditor.h"

AnatomyAudioProcessorEditor::AnatomyAudioProcessorEditor(AnatomyAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    formatManager.registerBasicFormats();

    addAndMakeVisible(waveDndFile);
    addAndMakeVisible(waveProcessorOriginal);
    addAndMakeVisible(waveTransient);
    addAndMakeVisible(waveTonal);

    // ラジオグループ化（相互排他トグル）
    btnOriginal.setRadioGroupId(1);
    btnTransient.setRadioGroupId(1);
    btnTonal.setRadioGroupId(1);

    btnOriginal.setClickingTogglesState(true);
    btnTransient.setClickingTogglesState(true);
    btnTonal.setClickingTogglesState(true);

    // 【点灯バグ完全修正】ON時にシアン（水色）背景・黒文字へ強制反転させるカラーバインディング
    auto configureButtonLook = [](juce::TextButton& b) {
        b.setColour(juce::TextButton::buttonOnColourId, juce::Colours::cyan);
        b.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
        b.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey.darker());
        b.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        };

    configureButtonLook(btnOriginal);
    configureButtonLook(btnTransient);
    configureButtonLook(btnTonal);

    addAndMakeVisible(btnOriginal);
    addAndMakeVisible(btnTransient);
    addAndMakeVisible(btnTonal);

    updateButtonToggleStates();

    btnOriginal.onClick = [this] { audioProcessor.setSoloMode(0); };
    btnTransient.onClick = [this] { audioProcessor.setSoloMode(1); };
    btnTonal.onClick = [this] { audioProcessor.setSoloMode(2); };

    setSize(800, 650);
    startTimer(40); // 40ms（25FPS）の高速描画監視タイマー
}

AnatomyAudioProcessorEditor::~AnatomyAudioProcessorEditor()
{
    stopTimer();
}

bool AnatomyAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray&)
{
    return true;
}

void AnatomyAudioProcessorEditor::filesDropped(const juce::StringArray& files, int, int)
{
    if (audioProcessor.isCurrentlyProcessing()) return;

    juce::File file(files[0]);
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));

    if (reader != nullptr)
    {
        juce::AudioBuffer<float> buffer((int)reader->numChannels, (int)reader->lengthInSamples);
        reader->read(&buffer, 0, (int)reader->lengthInSamples, 0, true, true);

        waveDndFile.setBuffer(buffer);

        audioProcessor.startSeparation(buffer);
        wasProcessing = true;
    }
}

void AnatomyAudioProcessorEditor::timerCallback()
{
    bool isProcessing = audioProcessor.isCurrentlyProcessing();

    if (isProcessing || wasProcessing)
    {
        repaint();
    }

    if (wasProcessing && !isProcessing)
    {
        // 非同期スライス完了の瞬間に、時間軸完全再構成バッファをUIコンポーネントへ流し込み
        waveProcessorOriginal.setBuffer(audioProcessor.getOriginalBuffer());
        waveTransient.setBuffer(audioProcessor.getTransientBuffer());
        waveTonal.setBuffer(audioProcessor.getTonalBuffer());

        wasProcessing = false;
        updateButtonToggleStates();
        repaint();
    }
}

void AnatomyAudioProcessorEditor::updateButtonToggleStates()
{
    int mode = audioProcessor.getSoloMode();
    btnOriginal.setToggleState(mode == 0, juce::dontSendNotification);
    btnTransient.setToggleState(mode == 1, juce::dontSendNotification);
    btnTonal.setToggleState(mode == 2, juce::dontSendNotification);
}

void AnatomyAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);

    g.setColour(juce::Colours::white.withAlpha(0.5f));
    g.setFont(12.0f);

    auto area = getLocalBounds();
    area.removeFromTop(45);
    auto h = area.getHeight() / 4;

    g.drawText("1. Drag & Drop Raw File (Source)", 10, 45, getWidth(), 15, juce::Justification::left);
    g.drawText("2. Processor Reconstructed Original Buffer", 10, 45 + h, getWidth(), 15, juce::Justification::left);
    g.drawText("3. Extracted Transient Component (Click / Attack)", 10, 45 + h * 2, getWidth(), 15, juce::Justification::left);
    g.drawText("4. Extracted Sustain Component (Body / Harmonics)", 10, 45 + h * 3, getWidth(), 15, juce::Justification::left);

    if (audioProcessor.isCurrentlyProcessing())
    {
        g.setColour(juce::Colours::black.withAlpha(0.7f));
        g.fillRect(getLocalBounds());

        float progress = audioProcessor.getHpssProgress();
        int percent = static_cast<int>(std::round(progress * 100.0f));

        g.setColour(juce::Colours::cyan);
        g.setFont(28.0f);

        g.drawText("Splicing & Reconstructing: " + juce::String(percent) + "% ...",
            getLocalBounds(),
            juce::Justification::centred,
            true);
    }
}

void AnatomyAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    auto buttonArea = area.removeFromTop(45).reduced(5);
    auto btnWidth = buttonArea.getWidth() / 3;

    btnOriginal.setBounds(buttonArea.removeFromLeft(btnWidth).reduced(2));
    btnTransient.setBounds(buttonArea.removeFromLeft(btnWidth).reduced(2));
    btnTonal.setBounds(buttonArea.reduced(2));

    auto h = area.getHeight() / 4;

    waveDndFile.setBounds(area.removeFromTop(h));
    waveProcessorOriginal.setBounds(area.removeFromTop(h));
    waveTransient.setBounds(area.removeFromTop(h));
    waveTonal.setBounds(area);
}