#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "ColorPalette.h"
#include <vector>
#include <functional>

class AnatomyAudioProcessor;

/**
 * WaveformComponent (Granular Style Modern Edition)
 * パステル・ドット/ライン波形描画、2色エネルギー比率色分け、
 * START/ENDバーのマウス直接トリミング、ズーム、およびDAWエクスポートを統合した高機能波形コンポーネント。
 */
class WaveformComponent final : public juce::Component
{
public:
    WaveformComponent();
    ~WaveformComponent() override = default;

    // この波形コンポーネントが担当するレーンを設定 (0 = FullMix, 1 = Transient, 2 = Tonal)
    void setLaneProperties(AnatomyAudioProcessor& p, int laneIdx) noexcept;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    void setBuffer(const juce::AudioBuffer<float>& buffer);
    void setOffsets(float startMs, float endMs, double sr) noexcept;
    void setRatioData(const std::vector<float>& ratios) noexcept;

    // クリックされたときに親ビュー（レーン）を選択するためのコールバック
    std::function<void()> onFocusClicked;

private:
    float getMsFromX(float x) const noexcept;
    float getXFromMs(float ms) const noexcept;
    void synchronizeToTargetSliders(float startMs, float endMs);

    AnatomyAudioProcessor* processor = nullptr;
    int laneIndex = 0;

    juce::CriticalSection renderLock;
    juce::AudioBuffer<float> internalBuffer;
    std::vector<float> componentRatios;

    float startOffsetMs = 0.0f;
    float endOffsetMs = 0.0f;
    double sampleRate = 44100.0;

    bool isDraggingStart = false;
    bool isDraggingEnd = false;
    bool isDraggingExport = false;

    juce::Rectangle<int> exportButtonArea;

    // ズーム機能（FullMix レーン等で利用）
    float zoomLevel = 1.0f;
    static constexpr float zoomMin = 1.0f;
    static constexpr float zoomMax = 32.0f;
    juce::Rectangle<int> zoomInArea;
    juce::Rectangle<int> zoomOutArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformComponent)
};