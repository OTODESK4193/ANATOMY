#include "WaveformComponent.h"
#include "../PluginProcessor.h"
#include <cmath>
#include <algorithm>

WaveformComponent::WaveformComponent()
{
    componentRatios.clear();
}

void WaveformComponent::setLaneProperties(AnatomyAudioProcessor& p, int laneIdx) noexcept
{
    const juce::ScopedLock sl(renderLock);
    processor = &p;
    laneIndex = laneIdx;
}

void WaveformComponent::setBuffer(const juce::AudioBuffer<float>& buffer)
{
    const juce::ScopedLock sl(renderLock);
    internalBuffer.makeCopyOf(buffer);
    repaint();
}

void WaveformComponent::setOffsets(float startMs, float endMs, double sr) noexcept
{
    const juce::ScopedLock sl(renderLock);
    startOffsetMs = startMs;
    endOffsetMs = endMs;
    sampleRate = sr;
    repaint();
}

void WaveformComponent::setRatioData(const std::vector<float>& ratios) noexcept
{
    const juce::ScopedLock sl(renderLock);
    componentRatios = ratios;
    repaint();
}

float WaveformComponent::getMsFromX(float x) const noexcept
{
    const int numSamples = internalBuffer.getNumSamples();
    if (numSamples == 0 || sampleRate <= 0.0 || getWidth() <= 0) return 0.0f;

    double totalMs = (static_cast<double>(numSamples) / sampleRate) * 1000.0;
    double visibleMs = totalMs / static_cast<double>(zoomLevel);
    return static_cast<float>((x / static_cast<float>(getWidth())) * visibleMs);
}

float WaveformComponent::getXFromMs(float ms) const noexcept
{
    const int numSamples = internalBuffer.getNumSamples();
    if (numSamples == 0 || sampleRate <= 0.0 || getWidth() <= 0) return 0.0f;

    double totalMs = (static_cast<double>(numSamples) / sampleRate) * 1000.0;
    double visibleMs = totalMs / static_cast<double>(zoomLevel);
    return static_cast<float>((ms / visibleMs) * static_cast<double>(getWidth()));
}

void WaveformComponent::paint(juce::Graphics& g)
{
    const juce::ScopedLock sl(renderLock);

    auto bounds = getLocalBounds().toFloat();

    // 1. 背景描画（Granularスタイルのダークパネル背景）
    g.setColour(AnatomyColors::panel.darker(0.3f));
    g.fillRoundedRectangle(bounds, 6.0f);

    // 2. 微細なグリッド線（中央水平線 ＋ 縦グリッド）
    const float w = bounds.getWidth();
    const float h = bounds.getHeight();
    const float mid = h * 0.5f;

    g.setColour(AnatomyColors::grid);
    g.drawHorizontalLine((int)mid, 0.0f, w);

    for (float gx = 40.0f; gx < w; gx += 50.0f)
        g.drawVerticalLine((int)gx, 0.0f, h);

    const int numSamples = internalBuffer.getNumSamples();

    if (numSamples == 0 || w <= 0.0f || h <= 0.0f)
    {
        g.setColour(AnatomyColors::textDim.withAlpha(0.4f));
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText("Drag & Drop WAV Audio File Here", getLocalBounds(), juce::Justification::centred, false);

        g.setColour(AnatomyColors::panelLine);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);
        return;
    }

    const float* data = internalBuffer.getReadPointer(0);

    // ズーム対応: 表示するサンプル範囲を計算
    int visibleSamples = static_cast<int>(static_cast<float>(numSamples) / zoomLevel);
    visibleSamples = juce::jlimit(1, numSamples, visibleSamples);
    int step = std::max(1, visibleSamples / static_cast<int>(w));

    bool isBeforeMode = (processor != nullptr && processor->beforeAfterBypasser.julesIsBeforeBypassed());

    // 3. 波形描画
    if (laneIndex == 0 && !isBeforeMode && componentRatios.size() >= static_cast<size_t>(numSamples))
    {
        // 2色エネルギー比率グラデーション描画 (FullMix)
        for (int xPix = 0; xPix < static_cast<int>(w); ++xPix)
        {
            int srcIdx = static_cast<int>((static_cast<float>(xPix) / w) * static_cast<float>(visibleSamples));
            srcIdx = juce::jlimit(0, numSamples - 1, srcIdx);

            float peak = 0.0f;
            for (int k = 0; k < step && (srcIdx + k) < numSamples; ++k)
                peak = std::max(peak, std::abs(data[srcIdx + k]));

            float yMax = peak * (mid - 2.0f);
            float rTrans = componentRatios[srcIdx];

            float yTrans = yMax * rTrans;

            // Transient成分 (Mint/Cyan)
            g.setColour(AnatomyColors::accentTransient.withAlpha(0.9f));
            g.drawVerticalLine(xPix, mid - yTrans, mid + yTrans);

            // Tonal成分 (Pink/Magenta)
            g.setColour(AnatomyColors::accentTonal.withAlpha(0.75f));
            g.drawVerticalLine(xPix, mid - yMax, mid - yTrans);
            g.drawVerticalLine(xPix, mid + yTrans, mid + yMax);
        }
    }
    else
    {
        // 単色パステル波形
        juce::Path p;
        p.startNewSubPath(0.0f, mid);

        for (int i = 0; i < visibleSamples; i += step)
        {
            float x = (static_cast<float>(i) / static_cast<float>(visibleSamples)) * w;
            float y = data[i] * (mid - 2.0f);
            p.lineTo(x, mid - y);
        }

        juce::Colour waveColour = AnatomyColors::accentFull;
        if (laneIndex == 1)      waveColour = AnatomyColors::accentTransient;
        else if (laneIndex == 2) waveColour = AnatomyColors::accentTonal;

        // グロー線
        g.setColour(waveColour.withAlpha(0.2f));
        g.strokePath(p, juce::PathStrokeType(2.5f));

        // メイン線
        g.setColour(waveColour.withAlpha(0.95f));
        g.strokePath(p, juce::PathStrokeType(1.2f));
    }

    // 4. Start / End トリミングマスク＆マーカー描画（Transient / Tonal レーン）
    if (laneIndex != 0 && endOffsetMs > 0.0f)
    {
        float startX = getXFromMs(startOffsetMs);
        float endX = getXFromMs(endOffsetMs);

        startX = juce::jlimit(0.0f, w, startX);
        endX = juce::jlimit(0.0f, w, endX);

        // 範囲外のダークマスク
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillRect(0.0f, 0.0f, startX, h);
        g.fillRect(endX, 0.0f, w - endX, h);

        // START マーカー (Peach/Mint)
        g.setColour(AnatomyColors::peach);
        g.drawVerticalLine(static_cast<int>(startX), 0.0f, h);
        g.fillEllipse(startX - 3.5f, 2.0f, 7.0f, 7.0f);

        // END マーカー (Rose/Pink)
        g.setColour(AnatomyColors::rose);
        g.drawVerticalLine(static_cast<int>(endX), 0.0f, h);
        g.fillEllipse(endX - 3.5f, h - 9.0f, 7.0f, 7.0f);
    }

    // 5. ズームボタン（FullMixまたは波形右下）
    if (numSamples > 0 && laneIndex == 0)
    {
        auto drawZoomBtn = [&](const juce::Rectangle<int>& area, const juce::String& label, bool enabled) {
            g.setColour(enabled ? AnatomyColors::knobTrack : AnatomyColors::panel);
            g.fillRoundedRectangle(area.toFloat(), 3.0f);
            g.setColour(enabled ? AnatomyColors::text : AnatomyColors::textDim.withAlpha(0.4f));
            g.drawRoundedRectangle(area.toFloat(), 3.0f, 1.0f);
            g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
            g.drawText(label, area, juce::Justification::centred, false);
        };

        drawZoomBtn(zoomOutArea, "-", zoomLevel > zoomMin);
        drawZoomBtn(zoomInArea, "+", zoomLevel < zoomMax);

        if (zoomLevel > 1.01f)
        {
            juce::String zoomText = "x" + juce::String(zoomLevel, 0);
            auto textArea = juce::Rectangle<int>(zoomOutArea.getX() - 32, zoomOutArea.getY(), 30, zoomOutArea.getHeight());
            g.setColour(AnatomyColors::textDim);
            g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
            g.drawText(zoomText, textArea, juce::Justification::centredRight, false);
        }
    }

    // 6. 外枠境界線
    g.setColour(AnatomyColors::panelLine);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);
}

void WaveformComponent::resized()
{
    int btnSize = 18;
    int margin = 4;
    int btnY = getHeight() - btnSize - margin;
    zoomInArea  = juce::Rectangle<int>(getWidth() - btnSize - margin, btnY, btnSize, btnSize);
    zoomOutArea = juce::Rectangle<int>(getWidth() - btnSize * 2 - margin - 3, btnY, btnSize, btnSize);
}

void WaveformComponent::mouseDown(const juce::MouseEvent& e)
{
    if (onFocusClicked)
        onFocusClicked();

    if (internalBuffer.getNumSamples() == 0) return;

    // ズームボタン判定
    if (laneIndex == 0)
    {
        if (zoomInArea.contains(e.getPosition()) && zoomLevel < zoomMax)
        {
            zoomLevel *= 2.0f;
            zoomLevel = juce::jmin(zoomLevel, zoomMax);
            repaint();
            return;
        }
        if (zoomOutArea.contains(e.getPosition()) && zoomLevel > zoomMin)
        {
            zoomLevel /= 2.0f;
            zoomLevel = juce::jmax(zoomLevel, zoomMin);
            repaint();
            return;
        }
    }

    if (laneIndex != 0)
    {
        float mouseX = static_cast<float>(e.x);
        float startX = getXFromMs(startOffsetMs);
        float endX = getXFromMs(endOffsetMs);

        if (std::abs(mouseX - startX) <= 8.0f)      isDraggingStart = true;
        else if (std::abs(mouseX - endX) <= 8.0f) isDraggingEnd = true;
    }
}

void WaveformComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (internalBuffer.getNumSamples() == 0 || laneIndex == 0) return;

    float mouseX = static_cast<float>(e.x);
    float mouseMs = getMsFromX(mouseX);

    if (isDraggingStart)
    {
        startOffsetMs = juce::jlimit(0.0f, endOffsetMs - 1.0f, mouseMs);
        synchronizeToTargetSliders(startOffsetMs, endOffsetMs);
    }
    else if (isDraggingEnd)
    {
        const int numSamples = internalBuffer.getNumSamples();
        double totalMs = (static_cast<double>(numSamples) / sampleRate) * 1000.0;

        endOffsetMs = juce::jlimit(startOffsetMs + 1.0f, static_cast<float>(totalMs), mouseMs);
        synchronizeToTargetSliders(startOffsetMs, endOffsetMs);
    }
}

void WaveformComponent::mouseUp(const juce::MouseEvent&)
{
    isDraggingStart = false;
    isDraggingEnd = false;
    isDraggingExport = false;
}

void WaveformComponent::synchronizeToTargetSliders(float startMs, float endMs)
{
    if (processor == nullptr) return;

    processor->setOffsetsFromUI((laneIndex == 1), startMs, endMs);
    repaint();
}