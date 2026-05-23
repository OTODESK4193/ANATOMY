#include "PluginEditor.h"

AnatomyAudioProcessorEditor::AnatomyAudioProcessorEditor(AnatomyAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    formatManager.registerBasicFormats();

    // 各波形ディスプレイの登録
    addAndMakeVisible(waveDndFile);
    addAndMakeVisible(waveProcessorOriginal);
    addAndMakeVisible(waveTransient);
    addAndMakeVisible(waveTonal);

    // Soloボタンのラジオグループ化（トグル式）設定
    btnOriginal.setRadioGroupId(1);
    btnTransient.setRadioGroupId(1);
    btnTonal.setRadioGroupId(1);

    btnOriginal.setClickingTogglesState(true);
    btnTransient.setClickingTogglesState(true);
    btnTonal.setClickingTogglesState(true);

    addAndMakeVisible(btnOriginal);
    addAndMakeVisible(btnTransient);
    addAndMakeVisible(btnTonal);

    // 初期状態はOriginalをON
    btnOriginal.setToggleState(true, juce::dontSendNotification);

    // 各ボタンのクリックイベント（ラムダ式によるProcessorへのSolo状態通知）
    btnOriginal.onClick = [this] { audioProcessor.setSoloMode(0); };
    btnTransient.onClick = [this] { audioProcessor.setSoloMode(1); };
    btnTonal.onClick = [this] { audioProcessor.setSoloMode(2); };

    setSize(800, 650);
    startTimer(40);
}

AnatomyAudioProcessorEditor::~AnatomyAudioProcessorEditor()
{
    stopTimer();
}

bool AnatomyAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    return true;
}

void AnatomyAudioProcessorEditor::filesDropped(const juce::StringArray& files, int x, int y)
{
    if (audioProcessor.isCurrentlyProcessing()) return;

    juce::File file(files[0]);
    auto* reader = formatManager.createReaderFor(file);

    if (reader != nullptr)
    {
        juce::AudioBuffer<float> buffer((int)reader->numChannels, (int)reader->lengthInSamples);
        reader->read(&buffer, 0, (int)reader->lengthInSamples, 0, true, true);

        // 1段目のディスプレイに生の元ファイルを描画
        waveDndFile.setBuffer(buffer);

        audioProcessor.startSeparation(buffer);
        wasProcessing = true;

        delete reader;
    }
}

void AnatomyAudioProcessorEditor::timerCallback()
{
    bool isProcessing = audioProcessor.isCurrentlyProcessing();

    if (isProcessing || wasProcessing)
    {
        repaint();
    }

    // バックグラウンドスレッドが計算を完了した瞬間
    if (wasProcessing && !isProcessing)
    {
        // 各バッファを安全に個別バインド
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

    g.drawText("1. Drag & Drop Raw File (Source)", 0, 45, getWidth(), 15, juce::Justification::left);
    g.drawText("2. Processor Reconstructed Original Buffer", 0, 45 + h, getWidth(), 15, juce::Justification::left);
    g.drawText("3. Extracted Transient Component (Attack / Spikes)", 0, 45 + h * 2, getWidth(), 15, juce::Justification::left);
    g.drawText("4. Extracted Tonal Component (Sustain / Harmonics)", 0, 45 + h * 3, getWidth(), 15, juce::Justification::left);

    // 非同期HPSSエンジン処理中のプログレス表示オーバーレイ
    if (audioProcessor.isCurrentlyProcessing())
    {
        g.setColour(juce::Colours::black.withAlpha(0.7f));
        g.fillRect(getLocalBounds());

        float progress = audioProcessor.getHpssProgress();
        int percent = static_cast<int>(std::round(progress * 100.0f));

        g.setColour(juce::Colours::cyan);
        g.setFont(28.0f);

        g.drawText("Processing: " + juce::String(percent) + "% ...",
            getLocalBounds(),
            juce::Justification::centred,
            true);
    }
}

void AnatomyAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    // 上部にSolo制御用のコントロールパネル（45px）を配置
    auto buttonArea = area.removeFromTop(45).reduced(5);
    auto btnWidth = buttonArea.getWidth() / 3;

    btnOriginal.setBounds(buttonArea.removeFromLeft(btnWidth).reduced(2));
    btnTransient.setBounds(buttonArea.removeFromLeft(btnWidth).reduced(2));
    btnTonal.setBounds(buttonArea.reduced(2));

    // 残りのエリアを均等に4等分して各波形ディスプレイを配置
    auto h = area.getHeight() / 4;

    waveDndFile.setBounds(area.removeFromTop(h));
    waveProcessorOriginal.setBounds(area.removeFromTop(h));
    waveTransient.setBounds(area.removeFromTop(h));
    waveTonal.setBounds(area);
}