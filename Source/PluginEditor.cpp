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
      layerLane(p),
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

    // FullMix D&D対応 EXPORT ボタン (BEFORE の左隣)
    btnExportFullMix.setFileGenerator([this] {
        return audioProcessor.createTemporaryWavForExport(0);
    });

    addAndMakeVisible(btnExportFullMix);
    addAndMakeVisible(beforeToggle);
    addAndMakeVisible(loadButton);
    addAndMakeVisible(resetButton);

    // テーマ選択
    themeCombo.addItemList(AnatomyColors::getThemeNames(), 1);
    themeCombo.setSelectedId(1, juce::dontSendNotification);
    themeCombo.onChange = [this]
    {
        int idx = themeCombo.getSelectedId() - 1;
        AnatomyColors::setTheme(idx);
        lastThemeIndex = idx;
        beforeToggle.setAccentColour(AnatomyColors::peach);
        btnExportFullMix.setAccentColour(AnatomyColors::accentFull);
        repaint();
        for (auto* child : getChildren()) child->repaint();
    };
    addAndMakeVisible(themeCombo);

    // --- 2段目: FullMixPreview (左半分表示に変更) ---
    waveFullMix.setLaneProperties(audioProcessor, 0);
    waveFullMix.setSelected(true); // 初期選択
    waveFullMix.onFocusClicked = [this] {
        fxRackView.setTargetRoute(TargetRoute::FullMix);
        waveFullMix.setSelected(true);
        transientLane.setSelected(false);
        tonalLane.setSelected(false);
        layerLane.setSelected(false);
    };
    addAndMakeVisible(waveFullMix);

    // LayerLaneView (右半分表示)
    layerLane.onSelectLane = [this] {
        fxRackView.setTargetRoute(TargetRoute::Layer);
        waveFullMix.setSelected(false);
        transientLane.setSelected(false);
        tonalLane.setSelected(false);
        layerLane.setSelected(true);
    };
    layerLane.onSampleChanged = [this] {
        audioProcessor.offlineMixRenderer.triggerRender();
    };
    layerLane.onSoloChanged = [this] {
        updateSoloButtonStates();
    };
    addAndMakeVisible(layerLane);

    // --- 3段目: TransientView & TonalView ---
    transientLane.onSelectLane = [this] {
        fxRackView.setTargetRoute(TargetRoute::Transient);
        waveFullMix.setSelected(false);
        transientLane.setSelected(true);
        tonalLane.setSelected(false);
        layerLane.setSelected(false);
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
        layerLane.setSelected(false);
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
    // 1. カスタムサンプルをクリアして最初のHPSS分離音源に戻す
    audioProcessor.clearCustomSampleFromUI(1);
    audioProcessor.clearCustomSampleFromUI(2);
    audioProcessor.clearCustomSampleFromUI(3);

    // 2. APVTS パラメータをデフォルト値に戻す
    for (auto* param : audioProcessor.getParameters())
        param->setValueNotifyingHost(param->getDefaultValue());

    // 3. FX ラックのスロット配置・ChipBar をデフォルト順にリセット
    fxRackView.resetAllSlotsToDefault();

    // 4. Solo モードを解除
    audioProcessor.setSoloMode(0);
    updateSoloButtonStates();

    // 5. UIのBROWSEボタン表示をリセット
    transientLane.resetCustomSampleState();
    tonalLane.resetCustomSampleState();

    // 6. Start / End オフセットとフェードを初期化
    double sr = audioProcessor.getFileSampleRate();
    float durMs = (sr > 0.0) ? (static_cast<float>(audioProcessor.getRawInputBufferForUI().getNumSamples()) / static_cast<float>(sr)) * 1000.0f : 0.0f;
    audioProcessor.setOffsetsFromUI(0, 0.0f, durMs);
    audioProcessor.setOffsetsFromUI(1, 0.0f, durMs);
    audioProcessor.setOffsetsFromUI(2, 0.0f, durMs);
    audioProcessor.setOffsetsFromUI(3, 0.0f, durMs);
    audioProcessor.setFadeFromUI(1, 0.0f, 0.0f, 0.0f, 0.0f);
    audioProcessor.setFadeFromUI(2, 0.0f, 0.0f, 0.0f, 0.0f);
    audioProcessor.setFadeFromUI(3, 0.0f, 0.0f, 0.0f, 0.0f);

    audioProcessor.offlineMixRenderer.triggerRender();
    repaint();
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

    // 波形バッファの取得と更新（新規レンダ完了時のみバッファと波形を再生成）
    juce::AudioBuffer<float> tempTrans, tempTonal, tempFullMix, tempLayer;
    std::vector<float> mixRatios;
    if (audioProcessor.offlineMixRenderer.getRenderedResults(tempFullMix, tempTrans, tempTonal, tempLayer, mixRatios))
    {
        if (beforeToggle.getToggleState())
            waveFullMix.setBuffer(audioProcessor.getRawInputBufferForUI());
        else
        {
            waveFullMix.setBuffer(tempFullMix);
            waveFullMix.setRatioData(mixRatios);
        }

        transientLane.setWaveBuffer(tempTrans);
        tonalLane.setWaveBuffer(tempTonal);
        layerLane.setWaveBuffer(tempLayer);
    }

    double sr = audioProcessor.getFileSampleRate();
    waveFullMix.setOffsets(audioProcessor.fullMixStartOffsetMs, audioProcessor.fullMixEndOffsetMs, sr);
    transientLane.setWaveOffsets(audioProcessor.transStartOffsetMs, audioProcessor.transEndOffsetMs, sr);
    tonalLane.setWaveOffsets(audioProcessor.tonalStartOffsetMs, audioProcessor.tonalEndOffsetMs, sr);
    layerLane.setWaveOffsets(audioProcessor.layerStartOffsetMs, audioProcessor.layerEndOffsetMs, sr);

    // フェード設定の反映
    float tInMs, tOutMs, tInTension, tOutTension;
    audioProcessor.getFadeForUI(1, tInMs, tOutMs, tInTension, tOutTension);
    transientLane.setWaveFade(tInMs, tOutMs, tInTension, tOutTension);

    float oInMs, oOutMs, oInTension, oOutTension;
    audioProcessor.getFadeForUI(2, oInMs, oOutMs, oInTension, oOutTension);
    tonalLane.setWaveFade(oInMs, oOutMs, oInTension, oOutTension);
    
    float lInMs, lOutMs, lInTension, lOutTension;
    audioProcessor.getFadeForUI(3, lInMs, lOutMs, lInTension, lOutTension);
    layerLane.setWaveFade(lInMs, lOutMs, lInTension, lOutTension);

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
    // [EXPORT] -> [BEFORE] -> [LOAD] -> [RESET] -> [THEME]
    int hX = getWidth() - kMargin;

    hX -= 90;
    themeCombo.setBounds(hX, 16, 90, 24);

    hX -= 10;
    hX -= 64;
    resetButton.setBounds(hX, 16, 64, 24);

    hX -= 8;
    hX -= 64;
    loadButton.setBounds(hX, 16, 64, 24);

    hX -= 12;
    hX -= 72;
    beforeToggle.setBounds(hX, 16, 72, 24);

    hX -= 8;
    hX -= 68;
    btnExportFullMix.setBounds(hX, 16, 68, 24);

    // --- 2段目: FullMixPreview & LayerView (50% スプリット) ---
    int curY = kHeaderH + 4;
    int fullW = getWidth() - kMargin * 2;
    int halfW = (fullW - 10) / 2;

    waveFullMix.setBounds(kMargin, curY + 20, halfW, kFullMixH - 24);
    layerLane.setBounds(kMargin + halfW + 10, curY, halfW, kFullMixH + 4); // 少し高さを確保

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