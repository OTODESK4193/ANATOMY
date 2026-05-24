#include "PluginProcessor.h"
#include "PluginEditor.h"

AnatomyAudioProcessorEditor::AnatomyAudioProcessorEditor(AnatomyAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    formatManager.registerBasicFormats();

    addAndMakeVisible(waveDndFile);
    addAndMakeVisible(waveTransient);
    addAndMakeVisible(waveTonal);

    btnOriginal.setRadioGroupId(1);
    btnTransient.setRadioGroupId(1);
    btnTonal.setRadioGroupId(1);

    btnOriginal.setClickingTogglesState(true);
    btnTransient.setClickingTogglesState(true);
    btnTonal.setClickingTogglesState(true);

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

    auto configureSlider = [this](juce::Slider& s, juce::Label& l, const juce::String& name) {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 16);
        s.setColour(juce::Slider::rotarySliderFillColourId, juce::Colours::cyan);
        s.setColour(juce::Slider::thumbColourId, juce::Colours::white);
        addAndMakeVisible(s);

        l.setText(name, juce::dontSendNotification);
        l.setFont(juce::Font(10.0f, juce::Font::bold));
        l.setJustificationType(juce::Justification::centred);
        l.setColour(juce::Label::textColourId, juce::Colours::cyan.withAlpha(0.9f));
        addAndMakeVisible(l);
        };

    configureSlider(sliderClickLength, lblClickLength, "CLICK HOLD (ms)");
    configureSlider(sliderClickCurve, lblClickCurve, "SUSTAIN FADE-IN (ms)");

    attachClickLength = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts, "clickLength", sliderClickLength);
    attachClickCurve = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts, "clickCurve", sliderClickCurve);

    setSize(800, 680);
    startTimer(40);
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
        audioProcessor.startSeparation(buffer, reader->sampleRate);
        wasProcessing = true;
    }
}

void AnatomyAudioProcessorEditor::timerCallback()
{
    audioProcessor.handleAsyncReanalysis();

    bool isProcessing = audioProcessor.isCurrentlyProcessing();

    if (isProcessing || wasProcessing)
    {
        juce::AudioBuffer<float> tempTrans, tempTonal;
        audioProcessor.getCallbackBuffersSecure(tempTrans, tempTonal);

        waveTransient.setBuffer(tempTrans);
        waveTonal.setBuffer(tempTonal);
        repaint();
    }

    if (wasProcessing && !isProcessing)
    {
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
    area.removeFromTop(125);
    auto h = area.getHeight() / 3;

    g.drawText("1. Drag & Drop Raw File (Original Source)", 15, 125, getWidth(), 15, juce::Justification::left);
    g.drawText("2. Extracted Transient Component (Click / Attack)", 15, 125 + h, getWidth(), 15, juce::Justification::left);
    g.drawText("3. Extracted Sustain Component (Body / Harmonics)", 15, 125 + h * 2, getWidth(), 15, juce::Justification::left);

    if (audioProcessor.isCurrentlyProcessing() &&
        !sliderClickLength.isMouseOverOrDragging() &&
        !sliderClickCurve.isMouseOverOrDragging())
    {
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.fillRect(0, 125, getWidth(), getHeight() - 125);

        float progress = audioProcessor.getHpssProgress();
        int percent = static_cast<int>(std::round(progress * 100.0f));

        g.setColour(juce::Colours::cyan);
        g.setFont(18.0f);
        g.drawText("Splicing: " + juce::String(percent) + "%",
            0, 125, getWidth(), getHeight() - 125,
            juce::Justification::centred, true);
    }
}

void AnatomyAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    auto buttonArea = area.removeFromTop(35).reduced(5);
    auto btnWidth = buttonArea.getWidth() / 3;
    btnOriginal.setBounds(buttonArea.removeFromLeft(btnWidth).reduced(2));
    btnTransient.setBounds(buttonArea.removeFromLeft(btnWidth).reduced(2));
    btnTonal.setBounds(buttonArea.reduced(2));

    auto controlArea = area.removeFromTop(90).reduced(5);
    auto ctrlWidth = controlArea.getWidth() / 2;

    auto s0 = controlArea.removeFromLeft(ctrlWidth);
    lblClickLength.setBounds(s0.removeFromTop(15));
    sliderClickLength.setBounds(s0);

    auto s1 = controlArea;
    lblClickCurve.setBounds(s1.removeFromTop(15));
    sliderClickCurve.setBounds(s1);

    auto h = area.getHeight() / 3;
    waveDndFile.setBounds(area.removeFromTop(h).reduced(10, 12));
    waveTransient.setBounds(area.removeFromTop(h).reduced(10, 12));
    waveTonal.setBounds(area.reduced(10, 12));
}