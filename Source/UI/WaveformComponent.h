// ==========================================
// File: WaveformComponent.h
// 高精度波形描画・極限ズーム・ゼロクロススナップ・インタラクティブFade対応版
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

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMove(const juce::MouseEvent& e) override;

    std::function<void()> onFocusClicked;
    std::function<void(float, float, float, float)> onFadeChanged; // inMs, outMs, inTension, outTension

private:
    float getMsFromX(float x) const noexcept;
    float getXFromMs(float ms) const noexcept;
    int findNearestZeroCrossing(const float* data, int numSamples, int targetSample, int searchRadius = 384) const noexcept;
    void synchronizeToTargetSliders(float startMs, float endMs);
    void updateFadeToProcessor();

    juce::CriticalSection renderLock;
    juce::AudioBuffer<float> internalBuffer;
    std::vector<float> componentRatios;

    AnatomyAudioProcessor* processor = nullptr;
    int laneIndex = 0; // 0: FullMix, 1: Transient, 2: Tonal

    float startOffsetMs = 0.0f;
    float endOffsetMs = 0.0f;
    double sampleRate = 44100.0;

    // フェードパラメータ
    float fadeInMs = 0.0f;
    float fadeOutMs = 0.0f;
    float fadeInTension = 0.0f;  // -1.0 .. +1.0
    float fadeOutTension = 0.0f; // -1.0 .. +1.0

    // 極限ズーム ＆ スクロール
    float zoomLevel = 1.0f;
    float viewOffsetMs = 0.0f; // 画面左端のミリ秒
    static constexpr float zoomMin = 1.0f;
    static constexpr float zoomMax = 1024.0f;

    // ドラッグ状態
    enum class DragMode { None, StartMarker, EndMarker, FadeInHandle, FadeOutHandle, FadeInTension, FadeOutTension, ScrollWaveform };
    DragMode currentDragMode = DragMode::None;
    float dragStartMs = 0.0f;
    float dragStartViewOffsetMs = 0.0f;
    juce::Point<float> dragStartPos;

    bool isSnappedToZeroCrossing = false;

    // ズームボタン領域
    juce::Rectangle<int> zoomInArea;
    juce::Rectangle<int> zoomOutArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformComponent)
};