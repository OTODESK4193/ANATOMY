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
    juce::TextButton btnBefore{ "BEFORE" };        // 💥案A：スマートON/OFFトグルボタン

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