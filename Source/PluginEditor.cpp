// ==========================================
// File: PluginEditor.cpp
// ANATOMY V1.1.0 (Granular Style Modern Edition)
// ==========================================
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <algorithm>

namespace
{
    constexpr int kHeaderH  = 60;
    constexpr int kFullMixH = 136;
    constexpr int kLanesH   = 236;
    constexpr int kFxH      = 268;
    constexpr int kMargin   = 16;
}

AnatomyAudioProcessorEditor::AnatomyAudioProcessorEditor(AnatomyAudioProcessor& p)
    : AudioProcessorEditor(&p),
      audioProcessor(p),
      transientLane(p),
      tonalLane(p),
      fxRackView(p)
{
    formatManager.registerBasicFormats();
    setLookAndFeel(&arcLookAndFeel);

    // --- 1段目: ヘッダーボタン ---
    auto styleHeaderButton = [](juce::TextButton& b, juce::Colour c) {
        b.setColour(juce::TextButton::buttonColourId, AnatomyColors::knobTrack);
        b.setColour(juce::TextButton::textColourOffId, c);
    };

    styleHeaderButton(loadButton,  AnatomyColors::text);
    styleHeaderButton(resetButton, AnatomyColors::textDim);

    loadButton.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Load an audio sample", juce::File(), "*.wav;*.aif;*.aiff;*.mp3;*.flac");
        fileChooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (file.existsAsFile())
                {
                    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
                    if (reader != nullptr)
                    {
                        juce::AudioBuffer<float> buffer((int)reader->numChannels, (int)reader->lengthInSamples);
                        reader->read(&buffer, 0, (int)reader->lengthInSamples, 0, true, true);
                        audioProcessor.startSeparation(buffer, reader->sampleRate);
                        wasProcessing = true;
                    }
                }
            });
    };

    resetButton.onClick = [this]
    {
        confirmThen("Reset Parameters",
                    juce::String::fromUTF8("すべてのパラメータをデフォルトに戻しますか？"),
                    [this] { resetAllParameters(); });
    };

    beforeToggle.onClick = [this]
    {
        audioProcessor.beforeAfterBypasser.setBeforeStatus(beforeToggle.getToggleState());
        audioProcessor.offlineMixRenderer.triggerRender();
        repaint();
    };

    addAndMakeVisible(loadButton);
    addAndMakeVisible(resetButton);
    addAndMakeVisible(beforeToggle);

    // テーマ選択
    themeCombo.addItemList(AnatomyColors::getThemeNames(), 1);
    themeCombo.setSelectedId(1, juce::dontSendNotification);
    themeCombo.onChange = [this]
    {
        int idx = themeCombo.getSelectedId() - 1;
        AnatomyColors::setTheme(idx);
        lastThemeIndex = idx;
        beforeToggle.setAccentColour(AnatomyColors::peach);
        repaint();
        for (auto* child : getChildren()) child->repaint();
    };
    addAndMakeVisible(themeCombo);

    // --- 2段目: FullMixPreview ---
    waveFullMix.setLaneProperties(audioProcessor, 0);
    waveFullMix.setSelected(true); // 初期選択
    waveFullMix.onFocusClicked = [this] {
        fxRackView.setTargetRoute(TargetRoute::FullMix);
        waveFullMix.setSelected(true);
        transientLane.setSelected(false);
        tonalLane.setSelected(false);
    };
    addAndMakeVisible(waveFullMix);

    knobTonalDelay.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knobTonalDelay.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 58, 14);
    knobTonalDelay.setTextValueSuffix(" ms");
    knobTonalDelay.setColour(juce::Slider::rotarySliderFillColourId, AnatomyColors::accentFull);
    knobTonalDelay.setColour(juce::Slider::textBoxTextColourId, AnatomyColors::text);
    knobTonalDelay.setPopupDisplayEnabled(true, true, this);
    addAndMakeVisible(knobTonalDelay);

    lblTonalDelay.setText("TONAL OFFSET", juce::dontSendNotification);
    lblTonalDelay.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
    lblTonalDelay.setJustificationType(juce::Justification::centred);
    lblTonalDelay.setColour(juce::Label::textColourId, AnatomyColors::accentFull.withAlpha(0.9f));
    addAndMakeVisible(lblTonalDelay);

    btnExportFullMix.setButtonText("EXPORT");
    styleHeaderButton(btnExportFullMix, AnatomyColors::accentFull);
    btnExportFullMix.onClick = [this] { triggerFullMixExport(); };
    addAndMakeVisible(btnExportFullMix);

    attachTonalDelay = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "tonalDelay", knobTonalDelay);

    // --- 3段目: TransientView & TonalView ---
    transientLane.onSelectLane = [this] {
        fxRackView.setTargetRoute(TargetRoute::Transient);
        waveFullMix.setSelected(false);
        transientLane.setSelected(true);
        tonalLane.setSelected(false);
    };
    transientLane.onSampleChanged = [this] {
        audioProcessor.offlineMixRenderer.triggerRender();
    };
    transientLane.onSoloChanged = [this] {
        updateSoloButtonStates();
    };
    addAndMakeVisible(transientLane);

    tonalLane.onSelectLane = [this] {
        fxRackView.setTargetRoute(TargetRoute::Tonal);
        waveFullMix.setSelected(false);
        transientLane.setSelected(false);
        tonalLane.setSelected(true);
    };
    tonalLane.onSampleChanged = [this] {
        audioProcessor.offlineMixRenderer.triggerRender();
    };
    tonalLane.onSoloChanged = [this] {
        updateSoloButtonStates();
    };
    addAndMakeVisible(tonalLane);

    // --- 4段目: Card FX Rack ---
    fxRackView.onRouteTabChanged = [this](TargetRoute route) {
        waveFullMix.setSelected(route == TargetRoute::FullMix);
        transientLane.setSelected(route == TargetRoute::Transient);
        tonalLane.setSelected(route == TargetRoute::Tonal);
    };
    addAndMakeVisible(fxRackView);

    updateSoloButtonStates();
    setSize(1080, 720);
    startTimer(30); // 30Hzで波形とHUDをリフレッシュ
}

AnatomyAudioProcessorEditor::~AnatomyAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void AnatomyAudioProcessorEditor::updateSoloButtonStates()
{
    transientLane.updateSoloState();
    tonalLane.updateSoloState();
}

void AnatomyAudioProcessorEditor::resetAllParameters()
{
    for (auto* param : audioProcessor.getParameters())
        param->setValueNotifyingHost(param->getDefaultValue());
}

void AnatomyAudioProcessorEditor::confirmThen(const juce::String& title, const juce::String& message, std::function<void()> action)
{
    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::QuestionIcon, title, message, "Yes", "No", this,
        juce::ModalCallbackFunction::create([action](int result)
        {
            if (result == 1 && action != nullptr) action();
        }));
}

void AnatomyAudioProcessorEditor::triggerFullMixExport()
{
    juce::File tempWav = audioProcessor.createTemporaryWavForExport(0);
    if (tempWav.existsAsFile())
    {
        juce::StringArray files;
        files.add(tempWav.getFullPathName());
        juce::DragAndDropContainer::performExternalDragDropOfFiles(files, false, this);
    }
}

bool AnatomyAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray&)
{
    return true;
}

void AnatomyAudioProcessorEditor::filesDropped(const juce::StringArray& files, int x, int y)
{
    if (audioProcessor.isCurrentlyProcessing() || files.size() == 0) return;

    juce::File file(files[0]);
    auto ext = file.getFileExtension().toLowerCase();
    if (ext != ".wav" && ext != ".aif" && ext != ".aiff" && ext != ".mp3" && ext != ".flac") return;

    juce::Point<int> dropPoint(x, y);

    if (waveFullMix.getBounds().contains(dropPoint))
    {
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
        if (reader != nullptr)
        {
            juce::AudioBuffer<float> buffer((int)reader->numChannels, (int)reader->lengthInSamples);
            reader->read(&buffer, 0, (int)reader->lengthInSamples, 0, true, true);
            audioProcessor.startSeparation(buffer, reader->sampleRate);
            wasProcessing = true;
        }
    }
}

void AnatomyAudioProcessorEditor::timerCallback()
{
    audioProcessor.handleAsyncReanalysis();
    audioProcessor.flushPendingExports();

    // 波形バッファの取得と更新
    juce::AudioBuffer<float> tempTrans, tempTonal, tempFullMix;
    std::vector<float> mixRatios;
    audioProcessor.offlineMixRenderer.getRenderedResults(tempFullMix, tempTrans, tempTonal, mixRatios);

    if (beforeToggle.getToggleState())
        waveFullMix.setBuffer(audioProcessor.getRawInputBufferForUI());
    else
    {
        waveFullMix.setBuffer(tempFullMix);
        waveFullMix.setRatioData(mixRatios);
    }

    transientLane.setWaveBuffer(tempTrans);
    tonalLane.setWaveBuffer(tempTonal);

    double sr = audioProcessor.getFileSampleRate();
    waveFullMix.setOffsets(0.0f, 0.0f, sr);
    transientLane.setWaveOffsets(audioProcessor.transStartOffsetMs, audioProcessor.transEndOffsetMs, sr);
    tonalLane.setWaveOffsets(audioProcessor.tonalStartOffsetMs, audioProcessor.tonalEndOffsetMs, sr);

    // フェード設定の反映
    float tInMs, tOutMs, tInTension, tOutTension;
    audioProcessor.getFadeForUI(true, tInMs, tOutMs, tInTension, tOutTension);
    transientLane.setWaveFade(tInMs, tOutMs, tInTension, tOutTension);

    float oInMs, oOutMs, oInTension, oOutTension;
    audioProcessor.getFadeForUI(false, oInMs, oOutMs, oInTension, oOutTension);
    tonalLane.setWaveFade(oInMs, oOutMs, oInTension, oOutTension);

    // HUD更新
    auto& rawBuf = audioProcessor.getRawInputBufferForUI();
    if (rawBuf.getNumSamples() > 0)
    {
        double lengthSec = (double)rawBuf.getNumSamples() / sr;
        hudFile = "Sample: Loaded (" + juce::String(lengthSec, 2) + "s)";
        hudSr   = "Rate: " + juce::String((int)sr) + " Hz";
    }
    else
    {
        hudFile = "Sample: (No file loaded)";
        hudSr   = "Rate: 44100 Hz";
    }

    if (audioProcessor.isCurrentlyProcessing())
    {
        int percent = (int)std::round(audioProcessor.getHpssProgress() * 100.0f);
        hudStatus = "SPLICING SOURCE ATOMS... " + juce::String(percent) + "%";
    }
    else
    {
        hudStatus = "Engine: Ready";
    }

    if (wasProcessing && !audioProcessor.isCurrentlyProcessing())
    {
        wasProcessing = false;
        updateSoloButtonStates();
        repaint();
    }

    repaint(0, 0, getWidth(), kHeaderH);
}

void AnatomyAudioProcessorEditor::paint(juce::Graphics& g)
{
    // 全体背景
    g.fillAll(AnatomyColors::bg);

    // --- 1段目: ヘッダー ---
    // グラデーションタイトルロゴ
    g.setFont(juce::Font(juce::FontOptions(22.0f, juce::Font::bold)));
    juce::ColourGradient titleGrad(AnatomyColors::mint, 20.0f, 16.0f,
                                   AnatomyColors::pink, 240.0f, 36.0f, false);
    titleGrad.addColour(0.5, AnatomyColors::lavender);
    g.setGradientFill(titleGrad);
    g.drawText("A N A T O M Y", 20, 8, 220, 26, juce::Justification::centredLeft);

    g.setColour(AnatomyColors::textDim);
    g.setFont(juce::Font(juce::FontOptions(10.5f)));
    g.drawText("OTODESK  |  V1.1.0  HPSS Audio Splice & Synthesis", 22, 34, 300, 14, juce::Justification::centredLeft);

    // HUD (ロゴ右側・2行)
    g.setFont(juce::Font(juce::FontOptions(10.5f)));
    g.setColour(AnatomyColors::text.withAlpha(0.85f));
    g.drawText(hudFile + "   " + hudSr, 320, 12, 300, 14, juce::Justification::centredLeft);

    g.setColour(audioProcessor.isCurrentlyProcessing() ? AnatomyColors::accentTransient : AnatomyColors::textDim);
    g.drawText(hudStatus, 320, 30, 300, 14, juce::Justification::centredLeft);

    // --- 2段目: FullMixPreview ヘッダー ---
    int fY = kHeaderH + 6;
    g.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
    g.setColour(AnatomyColors::accentFull);
    g.drawText("FULL MIX PREVIEW  (TRANSIENT / TONAL RATIO)", kMargin + 10, fY + 4, 350, 16, juce::Justification::centredLeft);

    // 解析中オーバーレイ表示
    if (audioProcessor.isCurrentlyProcessing())
    {
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillRect(0, kHeaderH, getWidth(), getHeight() - kHeaderH);

        float progress = audioProcessor.getHpssProgress();
        int percent = static_cast<int>(std::round(progress * 100.0f));

        g.setColour(AnatomyColors::panel);
        g.fillRoundedRectangle((float)getWidth() / 2 - 160, (float)getHeight() / 2 - 40, 320.0f, 80.0f, 8.0f);
        g.setColour(AnatomyColors::accentTransient);
        g.drawRoundedRectangle((float)getWidth() / 2 - 160, (float)getHeight() / 2 - 40, 320.0f, 80.0f, 8.0f, 1.5f);

        g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        g.drawText("SPLICING SOURCE ATOMS: " + juce::String(percent) + "%",
                   getWidth() / 2 - 150, getHeight() / 2 - 30, 300, 24, juce::Justification::centred);

        // プログレスバー
        g.setColour(AnatomyColors::knobTrack);
        g.fillRoundedRectangle((float)getWidth() / 2 - 120, (float)getHeight() / 2 + 6, 240.0f, 10.0f, 5.0f);
        g.setColour(AnatomyColors::mint);
        g.fillRoundedRectangle((float)getWidth() / 2 - 120, (float)getHeight() / 2 + 6, 240.0f * progress, 10.0f, 5.0f);
    }
}

void AnatomyAudioProcessorEditor::resized()
{
    // --- 1段目: ヘッダーボタン (右側) ---
    int hX = getWidth() - kMargin;

    hX -= 90;
    themeCombo.setBounds(hX, 16, 90, 24);

    hX -= 12;
    hX -= 68;
    resetButton.setBounds(hX, 16, 68, 24);

    hX -= 8;
    hX -= 68;
    loadButton.setBounds(hX, 16, 68, 24);

    hX -= 16;
    hX -= 76;
    beforeToggle.setBounds(hX, 16, 76, 24);

    // --- 2段目: FullMixPreview ---
    int curY = kHeaderH + 4;
    int fullW = getWidth() - kMargin * 2;
    int waveW = fullW - 130;

    waveFullMix.setBounds(kMargin, curY + 20, waveW, kFullMixH - 24);

    // 右側コントロール
    int ctrlX = kMargin + waveW + 10;
    lblTonalDelay.setBounds(ctrlX, curY + 16, 110, 14);
    knobTonalDelay.setBounds(ctrlX + 26, curY + 32, 58, 58);
    btnExportFullMix.setBounds(ctrlX + 10, curY + 98, 90, 22);

    curY += kFullMixH + 6;

    // --- 3段目: TransientView & TonalView (50% スプリット) ---
    int laneGap = 10;
    int laneW = (fullW - laneGap) / 2;

    transientLane.setBounds(kMargin, curY, laneW, kLanesH);
    tonalLane.setBounds(kMargin + laneW + laneGap, curY, laneW, kLanesH);

    curY += kLanesH + 8;

    // --- 4段目: Card FX Rack ---
    fxRackView.setBounds(kMargin, curY, fullW, kFxH);
}