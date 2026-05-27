#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginProcessor.h"
#include "UI/WaveformComponent.h"
#include "UI/TransientBrowserPanel.h"
#include "UI/TonalBrowserPanel.h"
#include "UI/EffectRackPanel.h"

/**
 * AnatomyAudioProcessorEditor
 * UI各モジュールの配置（Bounds）の決定と、ChangeListenerによる非同期通知の受信に徹する
 * カプセル化されたメインエディタクラス。
 */
class AnatomyAudioProcessorEditor final : public juce::AudioProcessorEditor,
    public juce::FileDragAndDropTarget,
    public juce::Timer,
    public juce::ChangeListener
{
public:
    AnatomyAudioProcessorEditor(AnatomyAudioProcessor&);
    ~AnatomyAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    void timerCallback() override;

    /**
     * 💥基本設計方針：ChangeListenerによる非同期通知の処理実体。
     * ラック側でのD&D並び替えやルート変更イベントを検知し、依存度ゼロでUIをリフレッシュします。
     */
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

private:
    void updateButtonToggleStates();

    AnatomyAudioProcessor& audioProcessor;

    juce::AudioFormatManager formatManager;
    bool wasProcessing = false;

    WaveformComponent waveDndFile;
    WaveformComponent waveTransient;
    WaveformComponent waveTonal;

    juce::TextButton btnOriginal{ "Full Mix" };
    juce::TextButton btnTransient{ "Transient Solo" };
    juce::TextButton btnTonal{ "Sustain Solo" };

    juce::Slider sliderClickLength;
    juce::Slider sliderClickCurve;
    juce::Slider sliderTransPitch;
    juce::Slider sliderTonalPitch;
    juce::Slider sliderSustainRelease;

    juce::Label lblClickLength;
    juce::Label lblClickCurve;
    juce::Label lblTransPitch;
    juce::Label lblTonalPitch;
    juce::Label lblSustainRelease;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachClickLength;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachClickCurve;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachTransPitch;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachTonalPitch;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachSustainRelease;

    TransientBrowserPanel transientBrowserPanel{ audioProcessor, waveTransient };
    TonalBrowserPanel tonalBrowserPanel{ audioProcessor, waveTonal };

    // 💥マルチエフェクトD&DコンテナラックUIの完全カプセル化インスタンス
    EffectRackPanel effectRackPanel{ audioProcessor };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnatomyAudioProcessorEditor)
};