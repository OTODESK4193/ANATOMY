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

namespace ExportRecordingCore
{
    enum class State { Idle, Request, Recording, PendingWrite, Ready };
    struct Lane {
        std::atomic<State> state{ State::Idle };
        juce::AudioBuffer<float> buffer;
        int writePos = 0;
        int sampleCounter = 0;
        int noteOffSample = 0;
        bool isNoteOffTriggered = false;
        juce::File file;
    };
    extern Lane lanes[3];
}

/**
 * 提示された数理ロジックに基づくグラデーション・アークダイヤル・ルックアンドフィール
 */
class ArcDialLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ArcDialLookAndFeel()
    {
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff1a1a1a));
        setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
        // 💥【修正】juce::Colours::transparent を存在定義されている transparentBlack へ変更
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }
    ~ArcDialLookAndFeel() override = default;

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
        float sliderPos, const float rotaryStartAngle,
        const float rotaryEndAngle, juce::Slider& slider) override
    {
        auto radius = (float)juce::jmin(width / 2, height / 2) - 4.0f;
        auto centreX = (float)x + (float)width * 0.5f;
        auto centreY = (float)y + (float)height * 0.5f;
        auto rx = centreX - radius;
        auto ry = centreY - radius;
        auto rw = radius * 2.0f;
        auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        auto arcThickness = 6.0f;

        // 1. 背景のトラック（Ableton風のダークグレー）
        g.setColour(juce::Colour(0xff1a1a1a));
        g.drawEllipse(rx, ry, rw, rw, arcThickness);

        // 2. 塗りつぶされるアーク（セクション色を基にしたグラデーション）
        juce::Path p;
        p.addArc(rx, ry, rw, rw, rotaryStartAngle, angle, true);

        auto baseColour = slider.findColour(juce::Slider::rotarySliderFillColourId);
        auto lightColour = baseColour.brighter(1.2f);
        auto darkColour = baseColour.darker(0.8f);

        juce::ColourGradient gradient(
            darkColour, rx, centreY,
            lightColour, rx + rw, centreY,
            false
        );

        g.setGradientFill(gradient);
        g.strokePath(p, juce::PathStrokeType(arcThickness, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));

        // 3. ポインター（直線のインジケーター）
        juce::Path p2;
        auto pointerLength = radius * 0.4f;
        p2.addRectangle(-1.5f, -radius, 3.0f, pointerLength);
        p2.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));
        g.setColour(juce::Colours::white);
        g.fillPath(p2);
    }
};

/**
 * 外部DAWへの直接ドラッグ＆ドロップエクスポートを成立させる
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
        auto s = ExportRecordingCore::lanes[laneIndex].state.load();

        if (s == ExportRecordingCore::State::Ready)
            g.setColour(juce::Colour::fromRGB(0, 180, 100)); // Drag OK (Green)
        else if (s == ExportRecordingCore::State::Request || s == ExportRecordingCore::State::Recording
                 || s == ExportRecordingCore::State::PendingWrite)
            g.setColour(juce::Colour::fromRGB(220, 130, 0)); // Recording/Writing (Orange)
        else
            g.setColour(juce::Colour(0xff3a3a3a));           // Ableton風フラットグレー

        g.fillRoundedRectangle(bounds, 2.0f);
        g.setColour(juce::Colour(0xff1a1a1a));
        g.drawRoundedRectangle(bounds, 2.0f, 1.0f);

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(10.0f, juce::Font::bold));

        if (s == ExportRecordingCore::State::Request || s == ExportRecordingCore::State::Recording
            || s == ExportRecordingCore::State::PendingWrite)
            g.drawText("RECORDING...", getLocalBounds(), juce::Justification::centred);
        else if (s == ExportRecordingCore::State::Ready)
            g.drawText("DRAG OK!", getLocalBounds(), juce::Justification::centred);
        else
            g.drawText("EXPORT", getLocalBounds(), juce::Justification::centred);
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        ExportRecordingCore::lanes[laneIndex].state.store(ExportRecordingCore::State::Request);
        repaint();
    }

    void mouseDrag(const juce::MouseEvent&) override
    {
        auto s = ExportRecordingCore::lanes[laneIndex].state.load();
        if (s == ExportRecordingCore::State::Ready)
        {
            juce::File file = processor.createTemporaryWavForExport(laneIndex);
            if (file.existsAsFile())
            {
                if (auto* dragContainer = juce::DragAndDropContainer::findParentDragContainerFor(this))
                {
                    juce::Image dragImage(juce::Image::ARGB, getWidth(), getHeight(), true);
                    juce::Graphics dg(dragImage);
                    dg.setColour(juce::Colour::fromRGB(0, 180, 100).withAlpha(0.6f));
                    dg.fillRoundedRectangle(dragImage.getBounds().toFloat(), 2.0f);
                    dg.setColour(juce::Colours::white);
                    dg.setFont(juce::Font(9.0f, juce::Font::bold));
                    dg.drawText("DROP WAV", dragImage.getBounds(), juce::Justification::centred);

                    dragContainer->startDragging(file.getFullPathName(), this, dragImage, true);

                    ExportRecordingCore::lanes[laneIndex].state.store(ExportRecordingCore::State::Idle);
                    repaint();
                }
            }
        }
    }

    void reset() noexcept
    {
        ExportRecordingCore::lanes[laneIndex].state.store(ExportRecordingCore::State::Idle);
        repaint();
    }

private:
    const int laneIndex;
    AnatomyAudioProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExportButton)
};

/**
 * AnatomyAudioProcessorEditor (Phase 2-4 Ultimate Edition)
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

    // 特製ルックアンドフィール常駐インスタンス
    ArcDialLookAndFeel arcLookAndFeel;

    // 5段積み専用：3枚の特製ハイパー波形ビジュアルパネル
    WaveformComponent waveFullMix;
    WaveformComponent waveTransient;
    WaveformComponent waveTonal;

    // 1段目操作部
    juce::TextButton btnOriginal{ "Full Mix" };
    juce::TextButton btnTransient{ "Transient Solo" };
    juce::TextButton btnTonal{ "Tonal Solo" };
    juce::TextButton btnBefore{ "BEFORE" };

    // 2段目左右引き裂き用 Transientコアパラメータ
    juce::Slider sliderClickLength;
    juce::Slider sliderTransPitch;
    juce::Slider sliderTransGain;
    juce::Label lblClickLength;
    juce::Label lblTransPitch;
    juce::Label lblTransGain;

    // 2段目左右引き裂き用 Tonalコアパラメータ
    juce::Slider sliderClickCurve;
    juce::Slider sliderTonalPitch;
    juce::Slider sliderTonalGain;
    juce::Slider sliderSustainRelease;
    juce::Label lblClickCurve;
    juce::Label lblTonalPitch;
    juce::Label lblTonalGain;
    juce::Label lblSustainRelease;

    // 各レーン専用のリアルタイムエクスポートトリガーボタン
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