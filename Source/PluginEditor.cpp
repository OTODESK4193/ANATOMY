#include "PluginEditor.h"

AnatomyAudioProcessorEditor::AnatomyAudioProcessorEditor(AnatomyAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    formatManager.registerBasicFormats();

    addAndMakeVisible(waveA);
    addAndMakeVisible(waveB);
    addAndMakeVisible(waveTransient);
    addAndMakeVisible(waveTonal);

    setSize(800, 600);
}

AnatomyAudioProcessorEditor::~AnatomyAudioProcessorEditor() {}

bool AnatomyAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    return true; // どのようなファイルでも受け入れる設定
}

void AnatomyAudioProcessorEditor::filesDropped(const juce::StringArray& files, int x, int y)
{
    juce::File file(files[0]);
    auto* reader = formatManager.createReaderFor(file);

    if (reader != nullptr)
    {
        juce::AudioBuffer<float> buffer((int)reader->numChannels, (int)reader->lengthInSamples);
        reader->read(&buffer, 0, (int)reader->lengthInSamples, 0, true, true);

        // UIの更新
        waveA.setBuffer(buffer);

        // 処理開始
        audioProcessor.startSeparation(buffer);

        delete reader;
    }
}

void AnatomyAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
}

void AnatomyAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    auto h = area.getHeight() / 4;

    waveA.setBounds(area.removeFromTop(h));
    waveB.setBounds(area.removeFromTop(h));
    waveTransient.setBounds(area.removeFromTop(h));
    waveTonal.setBounds(area.removeFromTop(h));
}