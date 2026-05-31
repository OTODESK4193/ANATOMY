#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginProcessor.h"
#include "UI/WaveformComponent.h"
#include "UI/TransientBrowserPanel.h"
#include "UI/TonalBrowserPanel.h"
#include "UI/EffectRackPanel.h"
#include "UI/ParameterDockPanel.h"

/**
 * 外部DAWへのドラッグ＆ドロップエクスポートをネイティブに成立させる
 * 特製インタラクティブ・エクスポートソースコンポーネント
 */
class ExportButton final : public juce::Component
{
public:
    ExportButton(int lane, AnatomyAudioProcessor& p) : laneIndex(lane), processor(p) {}
    ~ExportButton() override = default;

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        if (isReady)
            g.setColour(juce::Colour::fromRGB(0, 180, 100)); // Drag OK (Green)
        else if (isProcessing)
            g.setColour(juce::Colour::fromRGB(220, 130, 0)); // Processing (Orange)
        else
            g.setColour(juce::Colours::darkgrey.darker());   // Normal (Dark)

        g.fillRoundedRectangle(bounds, 4.0f);

        g.setColour(juce::Colours::white.withAlpha(0.3f));
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(10.5f, juce::Font::bold));

        if (isProcessing)
            g.drawText("Processing...", getLocalBounds(), juce::Justification::centred);
        else if (isReady)
            g.drawText("Drag OK!", getLocalBounds(), juce::Justification::centred);
        else
            g.drawText("EXPORT", getLocalBounds(), juce::Justification::centred);
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        if (!isProcessing && !isReady)
        {
            isProcessing = true;
            repaint();

            // 一時フォルダに実動作レート流路で音声を生成
            exportedFile = processor.createTemporaryWavForExport(laneIndex);

            isProcessing = false;
            isReady = exportedFile.existsAsFile();
            repaint();
        }
    }

    void mouseDrag(const juce::MouseEvent&) override
    {
        if (isReady && exportedFile.existsAsFile())
        {
            if (auto* dragContainer = juce::DragAndDropContainer::findParentDragContainerFor(this))
            {
                // ドラッグ中の視覚的サムネイルを生成
                juce::Image dragImage(juce::Image::ARGB, getWidth(), getHeight(), true);
                juce::Graphics dg(dragImage);
                dg.setColour(juce::Colour::fromRGB(0, 180, 100).withAlpha(0.6f));
                dg.fillRoundedRectangle(dragImage.getBounds().toFloat(), 4.0f);
                dg.setColour(juce::Colours::white);
                dg.setFont(juce::Font(10.0f, juce::Font::bold));
                dg.drawText("Dropping WAV...", dragImage.getBounds(), juce::Justification::centred);

                // 第1引数にファイルの絶対パスを渡し、OSを介して外部DAWのタイムラインへ直流ドロップ
                dragContainer->startDragging(exportedFile.getFullPathName(), this, dragImage, true);

                // ドロップ開始後に状態をクリアし次のエクスポートに備える
                isReady = false;
                repaint();
            }
        }
    }

    void reset() noexcept
    {
        if (isReady || isProcessing)
        {
            isReady = false;
            isProcessing = false;
            exportedFile = juce::File();
            repaint();
        }
    }

private:
    const int laneIndex;
    AnatomyAudioProcessor& processor;
    bool isProcessing = false;
    bool isReady = false;
    juce::File exportedFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExportButton)
};

/**
 * AnatomyAudioProcessorEditor (Phase 2-4 Ultimate Edition)
 * 5段積み超統合レイアウト、左右コアエリア分割、およびBefore/Afterのスマートトグルを統括。
 */
class AnatomyAudioProcessorEditor final : public juce::AudioProcessorEditor,
    public juce::FileDragAndDropTarget,
    public juce::Timer,
    public juce::ChangeListener,
    public juce::DragAndDropContainer
{
public:
    AnatomyAudioProcessorEditor(AnatomyAudioProcessor&);
    ~AnatomyAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

private:
    void updateButtonToggleStates();

    AnatomyAudioProcessor& audioProcessor;
    juce::AudioFormatManager formatManager;
    bool wasProcessing = false;

    // 5段積み専用：3枚の特製ハイパー波形ビジュアルパネル
    WaveformComponent waveFullMix;
    WaveformComponent waveTransient;
    WaveformComponent waveTonal;

    // 1段目操作部
    juce::TextButton btnOriginal{ "Full Mix" };
    juce::TextButton btnTransient{ "Transient Solo" };
    juce::TextButton btnTonal{ "Tonal Solo" };     // SustainからTonalへ名称変更
    juce::TextButton btnBefore{ "BEFORE" };        // スマートON/OFFトグルボタン

    // 2段目左右引き裂き用 Transientコアパラメータ
    juce::Slider sliderClickLength;
    juce::Slider sliderTransPitch;
    juce::Slider sliderTransGain; // 1段目から引っ越しマウント
    juce::Label lblClickLength;
    juce::Label lblTransPitch;
    juce::Label lblTransGain;

    // 2段目左右引き裂き用 Tonalコアパラメータ
    juce::Slider sliderClickCurve;
    juce::Slider sliderTonalPitch; // SustainからTonalへ名称変更
    juce::Slider sliderTonalGain;  // 1段目から引っ越しマウント
    juce::Slider sliderSustainRelease;
    juce::Label lblClickCurve;
    juce::Label lblTonalPitch;
    juce::Label lblTonalGain;
    juce::Label lblSustainRelease;

    // 各レーン専用のネイティブドラッグエクスポートソース
    ExportButton btnExportFull{ 0, audioProcessor };
    ExportButton btnExportTransient{ 1, audioProcessor };
    ExportButton btnExportTonal{ 2, audioProcessor };

    // 鉄壁の初期化アタッチメント
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachClickLength;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachClickCurve;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachTransPitch;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachTonalPitch;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachSustainRelease;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachTransMixGain;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachTonalMixGain;

    // 各レーンブラウザ
    TransientBrowserPanel transientBrowserPanel{ audioProcessor, waveTransient };
    TonalBrowserPanel tonalBrowserPanel{ audioProcessor, waveTonal };

    // 各エフェクトラックと最下段フル幅パラメータドック
    EffectRackPanel effectRackPanel{ audioProcessor };
    ParameterDockPanel parameterDockPanel{ audioProcessor };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnatomyAudioProcessorEditor)
};