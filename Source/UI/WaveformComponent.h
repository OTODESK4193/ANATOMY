#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>

class WaveformComponent : public juce::Component
{
public:
    WaveformComponent() = default;
    ~WaveformComponent() override = default;

    void paint(juce::Graphics& g) override
    {
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
        double totalMs = (static_cast<double>(numSamples) / sampleRate) * 1000.0;
        if (totalMs <= 0.0) return;

        // 画面上のピクセル位置へ換算
        float startX = (startOffsetMs / static_cast<float>(totalMs)) * w;
        float endX = (endOffsetMs / static_cast<float>(totalMs)) * w;

        startX = juce::jlimit(0.0f, w, startX);
        endX = juce::jlimit(0.0f, w, endX);

        // 1. STARTより前をマスク
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillRect(0.0f, 0.0f, startX, h);

        // 2. ENDより後ろをマスク
        g.fillRect(endX, 0.0f, w - endX, h);

        // STARTライン（黄）
        g.setColour(juce::Colours::yellow.withAlpha(0.8f));
        g.drawVerticalLine(static_cast<int>(startX), 0.0f, h);

        // ENDライン（赤）
        g.setColour(juce::Colours::red.withAlpha(0.7f));
        g.drawVerticalLine(static_cast<int>(endX), 0.0f, h);
    }

    void setBuffer(const juce::AudioBuffer<float>& buffer)
    {
        internalBuffer.makeCopyOf(buffer);
        repaint();
    }

    void setOffsets(float startMs, float endMs, double sr) noexcept
    {
        startOffsetMs = startMs;
        endOffsetMs = endMs;
        sampleRate = sr;
        repaint();
    }

private:
    juce::AudioBuffer<float> internalBuffer;
    float startOffsetMs = 0.0f;
    float endOffsetMs = 0.0f;
    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformComponent)
};