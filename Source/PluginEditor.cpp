#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <algorithm>

AnatomyAudioProcessorEditor::AnatomyAudioProcessorEditor(AnatomyAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    formatManager.registerBasicFormats();

    waveFullMix.setLaneProperties(p, 0);
    waveTransient.setLaneProperties(p, 1);
    waveTonal.setLaneProperties(p, 2);

    addAndMakeVisible(waveFullMix);
    addAndMakeVisible(waveTransient);
    addAndMakeVisible(waveTonal);
    addAndMakeVisible(transientBrowserPanel);
    addAndMakeVisible(tonalBrowserPanel);

    effectRackPanel.addChangeListener(this);
    addAndMakeVisible(effectRackPanel);
    addAndMakeVisible(parameterDockPanel);

    btnOriginal.setRadioGroupId(1);
    btnTransient.setRadioGroupId(1);
    btnTonal.setRadioGroupId(1);

    btnOriginal.setClickingTogglesState(true);
    btnTransient.setClickingTogglesState(true);
    btnTonal.setClickingTogglesState(true);

    auto configureButtonLook = [](juce::TextButton& b, juce::Colour activeColor) {
        b.setColour(juce::TextButton::buttonOnColourId, activeColor);
        b.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
        b.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey.darker());
        b.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        };

    configureButtonLook(btnOriginal, juce::Colours::white.withAlpha(0.8f));
    configureButtonLook(btnTransient, juce::Colours::cyan);
    configureButtonLook(btnTonal, juce::Colours::magenta);

    btnBefore.setClickingTogglesState(true);
    configureButtonLook(btnBefore, juce::Colours::yellow);
    addAndMakeVisible(btnBefore);

    addAndMakeVisible(btnOriginal);
    addAndMakeVisible(btnTransient);
    addAndMakeVisible(btnTonal);

    updateButtonToggleStates();

    btnOriginal.onClick = [this] { audioProcessor.setSoloMode(0); };
    btnTransient.onClick = [this] { audioProcessor.setSoloMode(1); };
    btnTonal.onClick = [this] { audioProcessor.setSoloMode(2); };

    btnBefore.onClick = [this] {
        audioProcessor.beforeAfterBypasser.setBeforeStatus(btnBefore.getToggleState());
        audioProcessor.offlineMixRenderer.triggerRender();
        repaint();
        };

    auto configureSlider = [this](juce::Slider& s, juce::Label& l, const juce::String& name, juce::Colour color) {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 14);
        s.setColour(juce::Slider::rotarySliderFillColourId, color);
        s.setColour(juce::Slider::thumbColourId, juce::Colours::white);
        addAndMakeVisible(s);

        l.setText(name, juce::dontSendNotification);
        l.setFont(juce::Font(9.5f, juce::Font::bold));
        l.setJustificationType(juce::Justification::centred);
        l.setColour(juce::Label::textColourId, color.withAlpha(0.9f));
        addAndMakeVisible(l);
        };

    configureSlider(sliderClickLength, lblClickLength, "CLICK HOLD (ms)", juce::Colours::cyan);
    configureSlider(sliderTransPitch, lblTransPitch, "TRANSIENT PITCH (st)", juce::Colours::cyan);
    configureSlider(sliderTransGain, lblTransGain, "TRANSIENT GAIN (dB)", juce::Colours::cyan);

    configureSlider(sliderClickCurve, lblClickCurve, "SUSTAIN FADE-IN (ms)", juce::Colours::magenta);
    configureSlider(sliderSustainRelease, lblSustainRelease, "SUSTAIN RELEASE (ms)", juce::Colours::magenta);
    configureSlider(sliderTonalPitch, lblTonalPitch, "TONAL PITCH (st)", juce::Colours::magenta);
    configureSlider(sliderTonalGain, lblTonalGain, "TONAL GAIN (dB)", juce::Colours::magenta);

    attachClickLength = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "clickLength", sliderClickLength);
    attachClickCurve = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "clickCurve", sliderClickCurve);
    attachTransPitch = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "transPitch", sliderTransPitch);
    attachTonalPitch = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "tonalPitch", sliderTonalPitch);
    attachSustainRelease = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "sustainRelease", sliderSustainRelease);
    attachTransMixGain = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "transMixGain", sliderTransGain);
    attachTonalMixGain = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "tonalMixGain", sliderTonalGain);

    // 💥【修正④・確定】DeleteボタンをJUCEコンポーネント構造からテキストベースで自動検出し、
    // プロセッサの独立クリア関数（clearCustomSampleFromUI）と安全に直流フック結合
    for (auto* child : transientBrowserPanel.getChildren())
    {
        if (auto* b = dynamic_cast<juce::TextButton*>(child))
        {
            juce::String txt = b->getButtonText().toLowerCase();
            if (txt.contains("delete") || txt.contains("clear") || txt == "x" || txt.contains("remove") || txt.contains("reset"))
            {
                b->onClick = [this] {
                    audioProcessor.clearCustomSampleFromUI(true);
                    for (auto* c : transientBrowserPanel.getChildren()) {
                        if (auto* btn = dynamic_cast<juce::TextButton*>(c)) {
                            juce::String cTxt = btn->getButtonText().toLowerCase();
                            // 隣のDelete/Resetボタン自体のテキストを破壊から完全シールドガード
                            if (!cTxt.contains("delete") && !cTxt.contains("clear") && cTxt != "x" && !cTxt.contains("reset"))
                                btn->setButtonText("Browse");
                        }
                    }
                    repaint();
                    };
            }
        }
    }

    for (auto* child : tonalBrowserPanel.getChildren())
    {
        if (auto* b = dynamic_cast<juce::TextButton*>(child))
        {
            juce::String txt = b->getButtonText().toLowerCase();
            if (txt.contains("delete") || txt.contains("clear") || txt == "x" || txt.contains("remove") || txt.contains("reset"))
            {
                b->onClick = [this] {
                    audioProcessor.clearCustomSampleFromUI(false);
                    for (auto* c : tonalBrowserPanel.getChildren()) {
                        if (auto* btn = dynamic_cast<juce::TextButton*>(c)) {
                            juce::String cTxt = btn->getButtonText().toLowerCase();
                            if (!cTxt.contains("delete") && !cTxt.contains("clear") && cTxt != "x" && !cTxt.contains("reset"))
                                btn->setButtonText("Browse");
                        }
                    }
                    repaint();
                    };
            }
        }
    }

    setSize(1150, 720);
    startTimer(40);
}

AnatomyAudioProcessorEditor::~AnatomyAudioProcessorEditor()
{
    effectRackPanel.removeChangeListener(this);
    stopTimer();
}

bool AnatomyAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray&)
{
    return true;
}

void AnatomyAudioProcessorEditor::filesDropped(const juce::StringArray& files, int x, int y)
{
    if (audioProcessor.isCurrentlyProcessing()) return;

    juce::File file(files[0]);
    if (file.getFileExtension().toLowerCase() != ".wav") return;

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
    else if (waveTransient.getBounds().contains(dropPoint))
    {
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
        if (reader != nullptr)
        {
            juce::AudioBuffer<float> buffer(1, static_cast<int>(reader->lengthInSamples));
            reader->read(&buffer, 0, static_cast<int>(reader->lengthInSamples), 0, true, false);

            audioProcessor.customTransientReplacer.loadSample(buffer, reader->sampleRate);

            double durationMs = (static_cast<double>(reader->lengthInSamples) / reader->sampleRate) * 1000.0;
            audioProcessor.setOffsetsFromUI(true, 0.0f, static_cast<float>(durationMs));

            // プロセッサ側の独立カスタム常駐メモリへ安全コピー
            audioProcessor.storeCustomSampleFromUI(true, buffer, reader->sampleRate);

            // 💥【名称バグ修正】Deleteボタンを巻き込まず、Browseボタンのみをピンポイント狙撃置換
            for (auto* child : transientBrowserPanel.getChildren()) {
                if (auto* b = dynamic_cast<juce::TextButton*>(child)) {
                    juce::String bTxt = b->getButtonText().toLowerCase();
                    if (bTxt == "browse" || bTxt == "load" || bTxt == "" || bTxt.contains("cf_"))
                        b->setButtonText(file.getFileNameWithoutExtension().substring(0, 7));
                }
            }
        }
    }
    else if (waveTonal.getBounds().contains(dropPoint))
    {
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
        if (reader != nullptr)
        {
            juce::AudioBuffer<float> buffer(1, static_cast<int>(reader->lengthInSamples));
            reader->read(&buffer, 0, static_cast<int>(reader->lengthInSamples), 0, true, false);

            audioProcessor.customTonalReplacer.loadSample(buffer, reader->sampleRate);

            double durationMs = (static_cast<double>(reader->lengthInSamples) / reader->sampleRate) * 1000.0;
            audioProcessor.setOffsetsFromUI(false, 0.0f, static_cast<float>(durationMs));

            audioProcessor.storeCustomSampleFromUI(false, buffer, reader->sampleRate);

            for (auto* child : tonalBrowserPanel.getChildren()) {
                if (auto* b = dynamic_cast<juce::TextButton*>(child)) {
                    juce::String bTxt = b->getButtonText().toLowerCase();
                    if (bTxt == "browse" || bTxt == "load" || bTxt == "" || bTxt.contains("cf_"))
                        b->setButtonText(file.getFileNameWithoutExtension().substring(0, 7));
                }
            }
        }
    }
}

void AnatomyAudioProcessorEditor::timerCallback()
{
    audioProcessor.handleAsyncReanalysis();

    effectRackPanel.updateCardSlidersFromParameters();
    parameterDockPanel.synchronizeSlidersFromParameters();

    auto synchronizeBrowserKnobs = [](juce::Component& panel, float startVal, float endVal) {
        int sliderCount = 0;
        for (auto* child : panel.getChildren())
        {
            if (auto* s = dynamic_cast<juce::Slider*>(child))
            {
                if (sliderCount == 0)      s->setValue(startVal, juce::dontSendNotification);
                else if (sliderCount == 1) s->setValue(endVal, juce::dontSendNotification);
                sliderCount++;
            }
        }
        };

    synchronizeBrowserKnobs(transientBrowserPanel, audioProcessor.transStartOffsetMs, audioProcessor.transEndOffsetMs);
    synchronizeBrowserKnobs(tonalBrowserPanel, audioProcessor.tonalStartOffsetMs, audioProcessor.tonalEndOffsetMs);

    juce::AudioBuffer<float> tempTrans, tempTonal, tempFullMix;
    std::vector<float> mixRatios;

    // オフラインレンダラーから常時最新のFX適用後・全長バッファをキャッチ
    audioProcessor.offlineMixRenderer.getRenderedResults(tempFullMix, tempTrans, tempTonal, mixRatios);

    if (btnBefore.getToggleState())
    {
        waveFullMix.setBuffer(audioProcessor.getRawInputBufferForUI());
    }
    else
    {
        waveFullMix.setBuffer(tempFullMix);
        waveFullMix.setRatioData(mixRatios);
    }

    // 💥【修正④・完全解決】
    // 下段の波形コンポーネントへ加工完了後の最新バッファを直流伝送。
    // Resetボタン押下時は内部で空（サイズ0）になるため、自動的に元の生ドラム波形がクッキリ100%美しく再描画復元される
    waveTransient.setBuffer(tempTrans);
    waveTonal.setBuffer(tempTonal);

    double sr = audioProcessor.getFileSampleRate();
    waveFullMix.setOffsets(0.0f, 0.0f, sr);
    waveTransient.setOffsets(audioProcessor.transStartOffsetMs, audioProcessor.transEndOffsetMs, sr);
    waveTonal.setOffsets(audioProcessor.tonalStartOffsetMs, audioProcessor.tonalEndOffsetMs, sr);

    repaint();

    if (wasProcessing && !audioProcessor.isCurrentlyProcessing())
    {
        wasProcessing = false;
        updateButtonToggleStates();
        repaint();
    }
}

void AnatomyAudioProcessorEditor::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &effectRackPanel)
    {
        parameterDockPanel.setTargetEffect(effectRackPanel.getSelectedEffect());
        repaint();
    }
    else if (source == nullptr)
    {
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
    g.fillAll(juce::Colours::black.withAlpha(0.95f));

    g.setFont(juce::Font(10.5f, juce::Font::bold));

    auto area = getLocalBounds();
    area.removeFromRight(350);
    area.removeFromTop(155);
    area.removeFromBottom(130);

    g.setColour(juce::Colours::white.withAlpha(0.4f));
    g.drawText("1. CURRENT FULL MIX PRE-VIEW (2-COLOR RATIO DISPLAY)", 15, 155, area.getWidth(), 12, juce::Justification::left);

    g.setColour(juce::Colours::cyan.withAlpha(0.5f));
    g.drawText("2. TRANSIENT COMPONENT BROWSER (CLICK / ATTACK)", 15, 155 + 130, area.getWidth(), 12, juce::Justification::left);

    g.setColour(juce::Colours::magenta.withAlpha(0.5f));
    g.drawText("3. TONAL COMPONENT BROWSER (BODY / HARMONICS)", 15, 155 + 130 * 2, area.getWidth(), 12, juce::Justification::left);

    if (audioProcessor.isCurrentlyProcessing() &&
        !sliderClickLength.isMouseOverOrDragging() &&
        !sliderClickCurve.isMouseOverOrDragging())
    {
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillRect(0, 155, area.getWidth(), 130 * 3);

        float progress = audioProcessor.getHpssProgress();
        int percent = static_cast<int> (std::round(progress * 100.0f));

        g.setColour(juce::Colours::cyan);
        g.setFont(16.0f);
        g.drawText("SPLICING SOURCE ATOM: " + juce::String(percent) + "%",
            0, 155, area.getWidth(), 130 * 3,
            juce::Justification::centred, true);
    }
}

void AnatomyAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    auto rackArea = area.removeFromRight(350);
    effectRackPanel.setBounds(rackArea.reduced(2));

    auto dockArea = area.removeFromBottom(130).reduced(5, 2);
    parameterDockPanel.setBounds(dockArea);

    auto buttonArea = area.removeFromTop(35).reduced(10, 3);
    auto btnWidth = buttonArea.getWidth() / 4;
    btnOriginal.setBounds(buttonArea.removeFromLeft(btnWidth).reduced(3, 0));
    btnTransient.setBounds(buttonArea.removeFromLeft(btnWidth).reduced(3, 0));
    btnTonal.setBounds(buttonArea.removeFromLeft(btnWidth).reduced(3, 0));
    btnBefore.setBounds(buttonArea.reduced(3, 0));

    auto controlArea = area.removeFromTop(120).reduced(5, 2);

    auto transCtrlArea = controlArea.removeFromLeft(controlArea.getWidth() / 2).reduced(5, 0);
    auto tcWidth = transCtrlArea.getWidth() / 3;
    auto tc0 = transCtrlArea.removeFromLeft(tcWidth); lblClickLength.setBounds(tc0.removeFromTop(14)); sliderClickLength.setBounds(tc0);
    auto tc1 = transCtrlArea.removeFromLeft(tcWidth); lblTransPitch.setBounds(tc1.removeFromTop(14)); sliderTransPitch.setBounds(tc1);
    auto tc2 = transCtrlArea;                         lblTransGain.setBounds(tc2.removeFromTop(14));  sliderTransGain.setBounds(tc2);

    auto tonalCtrlArea = controlArea.reduced(5, 0);
    auto tnHWidth = tonalCtrlArea.getWidth() / 4;
    auto tn0 = tonalCtrlArea.removeFromLeft(tnHWidth); lblClickCurve.setBounds(tn0.removeFromTop(14));     sliderClickCurve.setBounds(tn0);
    auto tn1 = tonalCtrlArea.removeFromLeft(tnHWidth); lblSustainRelease.setBounds(tn1.removeFromTop(14)); sliderSustainRelease.setBounds(tn1);
    auto tn2 = tonalCtrlArea.removeFromLeft(tnHWidth); lblTonalPitch.setBounds(tn2.removeFromTop(14));     sliderTonalPitch.setBounds(tn2);
    auto tn3 = tonalCtrlArea;                          lblTonalGain.setBounds(tn3.removeFromTop(14));      sliderTonalGain.setBounds(tn3);

    auto fMixArea = area.removeFromTop(130).reduced(10, 14);
    waveFullMix.setBounds(fMixArea);

    auto transArea = area.removeFromTop(130).reduced(10, 14);
    auto transBrowserArea = transArea.removeFromRight(135);
    waveTransient.setBounds(transArea);
    transientBrowserPanel.setBounds(transBrowserArea.removeFromTop(75));

    auto tonalArea = area.removeFromTop(130).reduced(10, 14);
    auto tonalBrowserArea = tonalArea.removeFromRight(135);
    waveTonal.setBounds(tonalArea);
    tonalBrowserPanel.setBounds(tonalBrowserArea.removeFromTop(75));
}