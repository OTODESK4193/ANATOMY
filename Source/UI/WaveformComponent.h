#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>

/**
 * WaveformComponent
 * JUCE 8のDirect2D非同期描画スレッドとの衝突（データレース）を
 * 内部CriticalSectionによって100%遮断した、鉄壁の波形ビジュアルクラス。
 */
class WaveformComponent final : public juce::Component
{
public:
    WaveformComponent() = default;
    ~WaveformComponent() override = default;

    void paint(juce::Graphics& g) override
    {
        // 💥JUCE 8 Direct2D描画スレッドからの非同期アクセスを完全にロック保護
        const juce::ScopedLock sl(renderLock);

        g.fillAll(juce::Colours::black.withAlpha(0.3f));

        const int numSamples = internalBuffer.getNumSamples();
        if (numSamples == 0 || getWidth() <= 0)
            return;

        auto w = (float)getWidth();
        auto h = (float)getHeight();
        auto mid = h * 0.5f;
        const float* data = internalBuffer.getReadPointer(0);

        int step = std::max(1, numSamples / (int)w);
        juce::Path p;
        p.startNewSubPath(0, mid);

        for (int i = 0; i < numSamples; i += step)
        {
            float x = (float)i / (float)numSamples * w;
            float y = data[i] * mid;
            p.lineTo(x, mid - y);
        }

        g.setColour(juce::Colours::cyan);
        g.strokePath(p, juce::PathStrokeType(1.0f));

        if (sampleRate <= 0.0) return;

        // --- START / END 位置の可視化とダブル遮光マスク描画 ---
        double totalMs = (static_cast<double> (numSamples) / sampleRate) * 1000.0;
        if (totalMs <= 0.0) return;

        float startX = (startOffsetMs / static_cast<float> (totalMs)) * w;
        float endX = (endOffsetMs / static_cast<float> (totalMs)) * w;

        startX = juce::jlimit(0.0f, w, startX);
        endX = juce::jlimit(0.0f, w, endX);

        // 1. STARTより前をマスク
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillRect(0.0f, 0.0f, startX, h);

        // 2. ENDより後ろをマスク
        g.fillRect(endX, 0.0f, w - endX, h);

        // STARTライン（黄）
        g.setColour(juce::Colours::yellow.withAlpha(0.8f));
        g.drawVerticalLine(static_cast<int> (startX), 0.0f, h);

        // ENDライン（赤）
        g.setColour(juce::Colours::red.withAlpha(0.7f));
        g.drawVerticalLine(static_cast<int> (endX), 0.0f, h);
    }

    void setBuffer(const juce::AudioBuffer<float>& buffer)
    {
        // 💥メッセージスレッド側（D&Dやタイマー）でのバッファ再確保を完全ロック保護
        const juce::ScopedLock sl(renderLock);
        internalBuffer.makeCopyOf(buffer);
        repaint();
    }

    void setOffsets(float startMs, float endMs, double sr) noexcept
    {
        const juce::ScopedLock sl(renderLock);
        startOffsetMs = startMs;
        endOffsetMs = endMs;
        sampleRate = sr;
        repaint();
    }

private:
    juce::CriticalSection renderLock; // 描画レースコンディションを根絶する専用ミューテックス
    juce::AudioBuffer<float> internalBuffer;
    float startOffsetMs = 0.0f;
    float endOffsetMs = 0.0f;
    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformComponent)
};