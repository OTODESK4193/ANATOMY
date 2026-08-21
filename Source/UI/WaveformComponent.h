// ==========================================
// File: WaveformComponent.h
// 高精度波形描画・PicoSampler式スムーズドラッグ・ゼロクロススナップ・選択枠線
// ==========================================
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "ColorPalette.h"
#include <vector>
#include <functional>

class AnatomyAudioProcessor;

class WaveformComponent final : public juce::Component,
                                public juce::DragAndDropContainer
{
public:
    WaveformComponent();
    ~WaveformComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setBuffer(const juce::AudioBuffer<float>& buffer);
    void setOffsets(float startMs, float endMs, double sampleRate) noexcept;
    void setFade(float inMs, float outMs, float inTension, float outTension) noexcept;
    void setRatioData(const std::vector<float>& ratios) noexcept;
    void setLaneProperties(AnatomyAudioProcessor& processor, int laneIndex) noexcept;
    void setSnapEnabled(bool enabled) noexcept { snapEnabled = enabled; repaint(); }
    void setSelected(bool selected) noexcept { isSelected = selected; repaint(); }

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMove(const juce::MouseEvent& e) override;

    void autoFitToContent();

    std::function<void()> onFocusClicked;
    std::function<void(float, float, float, float)> onFadeChanged; // inMs, outMs, inTension, outTension

private:
    float getMsFromX(float x) const noexcept;
    float getXFromMs(float ms) const noexcept;
    float findZeroCrossingMs(float targetMs, float magnetPixels = 14.0f) const noexcept;
    void synchronizeToTargetSliders(float startMs, float endMs, bool notifyProcessor = true);
    void updateFadeToProcessor();

    juce::CriticalSection renderLock;
    juce::AudioBuffer<float> internalBuffer;
    std::vector<float> componentRatios;

    AnatomyAudioProcessor* processor = nullptr;
    int laneIndex = 0; // 0: FullMix, 1: Transient, 2: Tonal
    bool isSelected = false;

    float startOffsetMs = 0.0f;
    float endOffsetMs = 0.0f;
    double sampleRate = 44100.0;

    // フェードパラメータ
    float fadeInMs = 0.0f;
    float fadeOutMs = 0.0f;
    float fadeInTension = 0.0f;  // -1.0 .. +1.0 (上: 急峻, 下: なだらか)
    float fadeOutTension = 0.0f; // -1.0 .. +1.0

    // スナップ設定
    bool snapEnabled = true;

    // 極限ズーム ＆ スクロール
    float zoomLevel = 1.0f;
    float viewOffsetMs = 0.0f; // 画面左端のミリ秒
    static constexpr float zoomMin = 1.0f;
    static constexpr float zoomMax = 1024.0f;

    // PicoSampler 方式 高精度ドラッグ状態
    enum class DragMode { None, StartMarker, EndMarker, FadeInHandle, FadeOutHandle, FadeInTension, FadeOutTension, ScrollWaveform, ScrollBarThumb };
    DragMode currentDragMode = DragMode::None;

    double dragStartParamMs = 0.0;
    float dragStartTension = 0.0f;
    float dragStartMouseXf = 0.0f;
    float dragStartViewOffsetMs = 0.0f;
    juce::Point<float> dragStartPos;
    float scrollThumbDragStartOffset = 0.0f;

    bool isSnappedToZeroCrossing = false;
    bool hasInitializedZoom = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformComponent)
};