// ==========================================
// File: WaveformComponent.cpp
// 高精度波形描画・オートフィット・上部マーカー▶◀・Fade中央●表示
// ==========================================
#include "WaveformComponent.h"
#include "../PluginProcessor.h"
#include "../DSP/TransientReplacer.h" // calculateFadeGain
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
    bool isDifferentLength = (internalBuffer.getNumSamples() != buffer.getNumSamples());
    internalBuffer.makeCopyOf(buffer);
    if (isDifferentLength)
    {
        zoomLevel = 1.0f;
        viewOffsetMs = 0.0f;
    }
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

void WaveformComponent::setFade(float inMs, float outMs, float inTension, float outTension) noexcept
{
    const juce::ScopedLock sl(renderLock);
    fadeInMs = inMs;
    fadeOutMs = outMs;
    fadeInTension = inTension;
    fadeOutTension = outTension;
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
    return static_cast<float>(viewOffsetMs + (static_cast<double>(x) / static_cast<double>(getWidth())) * visibleMs);
}

float WaveformComponent::getXFromMs(float ms) const noexcept
{
    const int numSamples = internalBuffer.getNumSamples();
    if (numSamples == 0 || sampleRate <= 0.0 || getWidth() <= 0) return 0.0f;

    double totalMs = (static_cast<double>(numSamples) / sampleRate) * 1000.0;
    double visibleMs = totalMs / static_cast<double>(zoomLevel);
    if (visibleMs <= 0.0) return 0.0f;

    return static_cast<float>(((static_cast<double>(ms) - viewOffsetMs) / visibleMs) * static_cast<double>(getWidth()));
}

int WaveformComponent::findNearestZeroCrossing(const float* data, int numSamples, int targetSample, int searchRadius) const noexcept
{
    if (data == nullptr || numSamples <= 1) return targetSample;

    targetSample = juce::jlimit(0, numSamples - 1, targetSample);
    int startRange = std::max(0, targetSample - searchRadius);
    int endRange = std::min(numSamples - 2, targetSample + searchRadius);

    int bestZeroCrossing = -1;
    int bestDist = 1000000;

    for (int s = startRange; s <= endRange; ++s)
    {
        if ((data[s] <= 0.0f && data[s + 1] >= 0.0f) || (data[s] >= 0.0f && data[s + 1] <= 0.0f))
        {
            int dist = std::abs(s - targetSample);
            if (dist < bestDist)
            {
                bestDist = dist;
                bestZeroCrossing = (std::abs(data[s]) < std::abs(data[s + 1])) ? s : (s + 1);
            }
        }
    }

    if (bestZeroCrossing >= 0) return bestZeroCrossing;

    float minAbs = 1.0e9f;
    int minIdx = targetSample;
    for (int s = startRange; s <= endRange + 1 && s < numSamples; ++s)
    {
        float a = std::abs(data[s]);
        if (a < minAbs)
        {
            minAbs = a;
            minIdx = s;
        }
    }
    return minIdx;
}

void WaveformComponent::paint(juce::Graphics& g)
{
    const juce::ScopedLock sl(renderLock);

    auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth();
    const float h = bounds.getHeight();
    const float mid = h * 0.5f;

    // 1. 背景描画
    g.setColour(AnatomyColors::panel.darker(0.35f));
    g.fillRoundedRectangle(bounds, 6.0f);

    // 2. 微細グリッド
    g.setColour(AnatomyColors::grid.withAlpha(0.6f));
    g.drawHorizontalLine(static_cast<int>(mid), 0.0f, w);

    for (float gx = 40.0f; gx < w; gx += 50.0f)
        g.drawVerticalLine(static_cast<int>(gx), 0.0f, h);

    const int numSamples = internalBuffer.getNumSamples();
    if (numSamples == 0 || w <= 0.0f || h <= 0.0f || sampleRate <= 0.0)
    {
        g.setColour(AnatomyColors::textDim.withAlpha(0.4f));
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText("Drag & Drop WAV Audio File Here", getLocalBounds(), juce::Justification::centred, false);

        g.setColour(AnatomyColors::panelLine);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);
        return;
    }

    const float* data = internalBuffer.getReadPointer(0);
    double totalMs = (static_cast<double>(numSamples) / sampleRate) * 1000.0;
    double visibleMs = totalMs / static_cast<double>(zoomLevel);

    int startSampleIdx = static_cast<int>((viewOffsetMs / 1000.0) * sampleRate);
    int endSampleIdx = static_cast<int>(((viewOffsetMs + visibleMs) / 1000.0) * sampleRate);
    startSampleIdx = juce::jlimit(0, numSamples - 1, startSampleIdx);
    endSampleIdx = juce::jlimit(1, numSamples, endSampleIdx);

    int visibleSampleCount = std::max(1, endSampleIdx - startSampleIdx);
    float samplesPerPixel = static_cast<float>(visibleSampleCount) / w;

    bool isBeforeMode = (processor != nullptr && processor->beforeAfterBypasser.julesIsBeforeBypassed());

    // 3. 高精度波形描画（エリア一杯にフィット）
    if (samplesPerPixel >= 1.0f)
    {
        // ── Min/Max ピークレンダリング ──
        if (laneIndex == 0 && !isBeforeMode && componentRatios.size() >= static_cast<size_t>(numSamples))
        {
            // FullMix 2色比率グラデーション描画
            for (int xPix = 0; xPix < static_cast<int>(w); ++xPix)
            {
                int s0 = startSampleIdx + static_cast<int>(static_cast<float>(xPix) * samplesPerPixel);
                int s1 = startSampleIdx + static_cast<int>(static_cast<float>(xPix + 1) * samplesPerPixel);
                s0 = juce::jlimit(0, numSamples - 1, s0);
                s1 = juce::jlimit(s0 + 1, numSamples, s1);

                float minV = 0.0f, maxV = 0.0f;
                float rTrans = componentRatios[s0];
                for (int s = s0; s < s1; ++s)
                {
                    float v = data[s];
                    if (v < minV) minV = v;
                    if (v > maxV) maxV = v;
                }

                float yTop = mid - maxV * (mid - 2.0f);
                float yBtm = mid - minV * (mid - 2.0f);
                float peak = std::max(std::abs(maxV), std::abs(minV));
                float yTrans = peak * (mid - 2.0f) * rTrans;

                // Transient (Mint)
                g.setColour(AnatomyColors::accentTransient.withAlpha(0.95f));
                g.drawVerticalLine(xPix, mid - yTrans, mid + yTrans);

                // Tonal (Pink)
                g.setColour(AnatomyColors::accentTonal.withAlpha(0.75f));
                g.drawVerticalLine(xPix, yTop, mid - yTrans);
                g.drawVerticalLine(xPix, mid + yTrans, yBtm);
            }
        }
        else
        {
            // 単色パステル Min/Max 描画
            juce::Colour waveColour = (laneIndex == 1) ? AnatomyColors::accentTransient :
                                      (laneIndex == 2) ? AnatomyColors::accentTonal :
                                                         AnatomyColors::accentFull;

            for (int xPix = 0; xPix < static_cast<int>(w); ++xPix)
            {
                int s0 = startSampleIdx + static_cast<int>(static_cast<float>(xPix) * samplesPerPixel);
                int s1 = startSampleIdx + static_cast<int>(static_cast<float>(xPix + 1) * samplesPerPixel);
                s0 = juce::jlimit(0, numSamples - 1, s0);
                s1 = juce::jlimit(s0 + 1, numSamples, s1);

                float minV = 0.0f, maxV = 0.0f;
                for (int s = s0; s < s1; ++s)
                {
                    float v = data[s];
                    if (v < minV) minV = v;
                    if (v > maxV) maxV = v;
                }

                float yTop = mid - maxV * (mid - 2.0f);
                float yBtm = mid - minV * (mid - 2.0f);
                if (std::abs(yBtm - yTop) < 1.0f) yBtm = yTop + 1.0f;

                g.setColour(waveColour.withAlpha(0.35f));
                g.drawVerticalLine(xPix, yTop - 1.0f, yBtm + 1.0f);

                g.setColour(waveColour.withAlpha(0.95f));
                g.drawVerticalLine(xPix, yTop, yBtm);
            }
        }
    }
    else
    {
        // ── 極限ズーム時: サンプル点補間 ＆ 個別ドット（●）描画 ──
        juce::Colour waveColour = (laneIndex == 1) ? AnatomyColors::accentTransient :
                                  (laneIndex == 2) ? AnatomyColors::accentTonal :
                                                     AnatomyColors::accentFull;

        juce::Path p;
        bool pathStarted = false;

        for (int s = startSampleIdx; s < endSampleIdx; ++s)
        {
            double ms = (static_cast<double>(s) / sampleRate) * 1000.0;
            float sx = getXFromMs(static_cast<float>(ms));
            float sy = mid - data[s] * (mid - 2.0f);

            if (!pathStarted)
            {
                p.startNewSubPath(sx, sy);
                pathStarted = true;
            }
            else
            {
                p.lineTo(sx, sy);
            }
        }

        g.setColour(waveColour.withAlpha(0.3f));
        g.strokePath(p, juce::PathStrokeType(2.5f));
        g.setColour(waveColour.withAlpha(0.95f));
        g.strokePath(p, juce::PathStrokeType(1.4f));

        for (int s = startSampleIdx; s < endSampleIdx; ++s)
        {
            double ms = (static_cast<double>(s) / sampleRate) * 1000.0;
            float sx = getXFromMs(static_cast<float>(ms));
            float sy = mid - data[s] * (mid - 2.0f);

            if (sx >= -5.0f && sx <= w + 5.0f)
            {
                g.setColour(waveColour);
                g.fillEllipse(sx - 2.5f, sy - 2.5f, 5.0f, 5.0f);
                g.setColour(juce::Colours::white.withAlpha(0.8f));
                g.fillEllipse(sx - 1.0f, sy - 1.0f, 2.0f, 2.0f);
            }
        }
    }

    // 4. Start / End トリミングマスク ＆ フェードイン・フェードアウト描画（Transient / Tonal）
    if (laneIndex != 0 && endOffsetMs > 0.0f)
    {
        float startX = getXFromMs(startOffsetMs);
        float endX = getXFromMs(endOffsetMs);

        // 範囲外ダークマスク
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        if (startX > 0.0f) g.fillRect(0.0f, 0.0f, std::min(w, startX), h);
        if (endX < w)      g.fillRect(std::max(0.0f, endX), 0.0f, w - std::max(0.0f, endX), h);

        // ── FadeIn 描画 ──
        if (fadeInMs > 0.1f)
        {
            float fInEndX = getXFromMs(startOffsetMs + fadeInMs);
            float fInStartX = std::max(0.0f, startX);
            float fInW = fInEndX - fInStartX;

            if (fInW > 1.0f)
            {
                // フェードイン減衰シェーディング
                juce::Path fadeArea;
                fadeArea.startNewSubPath(fInStartX, 0.0f);
                for (float fx = fInStartX; fx <= fInEndX; fx += 2.0f)
                {
                    float prog = (fx - startX) / (fInEndX - startX);
                    float gain = calculateFadeGain(prog, fadeInTension);
                    float curH = h * (1.0f - gain);
                    fadeArea.lineTo(fx, curH);
                }
                fadeArea.lineTo(fInEndX, 0.0f);
                fadeArea.closeSubPath();

                g.setColour(AnatomyColors::peach.withAlpha(0.18f));
                g.fillPath(fadeArea);

                // フェードイン カーブライン
                juce::Path fadeLine;
                fadeLine.startNewSubPath(fInStartX, h);
                for (float fx = fInStartX; fx <= fInEndX; fx += 2.0f)
                {
                    float prog = (fx - startX) / (fInEndX - startX);
                    float gain = calculateFadeGain(prog, fadeInTension);
                    fadeLine.lineTo(fx, h - gain * (h - 4.0f));
                }
                g.setColour(AnatomyColors::peach.withAlpha(0.9f));
                g.strokePath(fadeLine, juce::PathStrokeType(1.6f));

                // FadeIn テンションハンドル (カーブ上の ●)
                float midX = (fInStartX + fInEndX) * 0.5f;
                float midGain = calculateFadeGain(0.5f, fadeInTension);
                float midY = h - midGain * (h - 4.0f);
                g.setColour(AnatomyColors::peach);
                g.fillEllipse(midX - 3.5f, midY - 3.5f, 7.0f, 7.0f);
                g.setColour(juce::Colours::white);
                g.fillEllipse(midX - 1.5f, midY - 1.5f, 3.0f, 3.0f);

                // FadeIn ハンドル (真ん中 y=mid の ● 表示)
                g.setColour(AnatomyColors::peach);
                g.fillEllipse(fInEndX - 4.5f, mid - 4.5f, 9.0f, 9.0f);
                g.setColour(juce::Colours::white);
                g.fillEllipse(fInEndX - 2.0f, mid - 2.0f, 4.0f, 4.0f);
            }
        }
        else
        {
            // フェード長が0のときでも操作できるように微小な FadeIn ポイントを表示
            float fInX = getXFromMs(startOffsetMs);
            if (fInX >= 0.0f && fInX <= w)
            {
                g.setColour(AnatomyColors::peach.withAlpha(0.7f));
                g.fillEllipse(fInX + 2.0f, mid - 3.5f, 7.0f, 7.0f);
            }
        }

        // ── FadeOut 描画 ──
        if (fadeOutMs > 0.1f)
        {
            float fOutStartX = getXFromMs(endOffsetMs - fadeOutMs);
            float fOutEndX = std::min(w, endX);
            float fOutW = fOutEndX - fOutStartX;

            if (fOutW > 1.0f)
            {
                // フェードアウト減衰シェーディング
                juce::Path fadeArea;
                fadeArea.startNewSubPath(fOutStartX, 0.0f);
                for (float fx = fOutStartX; fx <= fOutEndX; fx += 2.0f)
                {
                    float prog = (endX - fx) / (endX - (endX - fadeOutMs));
                    float gain = calculateFadeGain(prog, fadeOutTension);
                    float curH = h * (1.0f - gain);
                    fadeArea.lineTo(fx, curH);
                }
                fadeArea.lineTo(fOutEndX, 0.0f);
                fadeArea.closeSubPath();

                g.setColour(AnatomyColors::rose.withAlpha(0.18f));
                g.fillPath(fadeArea);

                // フェードアウト カーブライン
                juce::Path fadeLine;
                fadeLine.startNewSubPath(fOutStartX, 4.0f);
                for (float fx = fOutStartX; fx <= fOutEndX; fx += 2.0f)
                {
                    float prog = (endX - fx) / (endX - (endX - fadeOutMs));
                    float gain = calculateFadeGain(prog, fadeOutTension);
                    fadeLine.lineTo(fx, h - gain * (h - 4.0f));
                }
                g.setColour(AnatomyColors::rose.withAlpha(0.9f));
                g.strokePath(fadeLine, juce::PathStrokeType(1.6f));

                // FadeOut テンションハンドル (カーブ上の ●)
                float midX = (fOutStartX + fOutEndX) * 0.5f;
                float midGain = calculateFadeGain(0.5f, fadeOutTension);
                float midY = h - midGain * (h - 4.0f);
                g.setColour(AnatomyColors::rose);
                g.fillEllipse(midX - 3.5f, midY - 3.5f, 7.0f, 7.0f);
                g.setColour(juce::Colours::white);
                g.fillEllipse(midX - 1.5f, midY - 1.5f, 3.0f, 3.0f);

                // FadeOut ハンドル (真ん中 y=mid の ● 表示)
                g.setColour(AnatomyColors::rose);
                g.fillEllipse(fOutStartX - 4.5f, mid - 4.5f, 9.0f, 9.0f);
                g.setColour(juce::Colours::white);
                g.fillEllipse(fOutStartX - 2.0f, mid - 2.0f, 4.0f, 4.0f);
            }
        }
        else
        {
            // フェード長が0のときでも操作できるように微小な FadeOut ポイントを表示
            float fOutX = getXFromMs(endOffsetMs);
            if (fOutX >= 0.0f && fOutX <= w)
            {
                g.setColour(AnatomyColors::rose.withAlpha(0.7f));
                g.fillEllipse(fOutX - 9.0f, mid - 3.5f, 7.0f, 7.0f);
            }
        }

        // ── START マーカー (上部 ▶ 三角形 ＋ 縦線) ──
        if (startX >= -5.0f && startX <= w + 5.0f)
        {
            g.setColour(AnatomyColors::peach);
            g.drawVerticalLine(static_cast<int>(startX), 12.0f, h);

            // 上部 ▶ 三角形
            juce::Path tri;
            tri.startNewSubPath(startX, 0.0f);
            tri.lineTo(startX + 10.0f, 6.0f);
            tri.lineTo(startX, 12.0f);
            tri.closeSubPath();
            g.fillPath(tri);
            g.setColour(juce::Colours::white);
            g.strokePath(tri, juce::PathStrokeType(1.0f));

            if (isSnappedToZeroCrossing && currentDragMode == DragMode::StartMarker)
            {
                g.setColour(AnatomyColors::mint);
                g.drawEllipse(startX - 5.0f, 14.0f, 10.0f, 10.0f, 1.5f);
            }
        }

        // ── END マーカー (上部 ◀ 三角形 ＋ 縦線) ──
        if (endX >= -5.0f && endX <= w + 5.0f)
        {
            g.setColour(AnatomyColors::rose);
            g.drawVerticalLine(static_cast<int>(endX), 12.0f, h);

            // 上部 ◀ 三角形
            juce::Path tri;
            tri.startNewSubPath(endX, 0.0f);
            tri.lineTo(endX - 10.0f, 6.0f);
            tri.lineTo(endX, 12.0f);
            tri.closeSubPath();
            g.fillPath(tri);
            g.setColour(juce::Colours::white);
            g.strokePath(tri, juce::PathStrokeType(1.0f));

            if (isSnappedToZeroCrossing && currentDragMode == DragMode::EndMarker)
            {
                g.setColour(AnatomyColors::mint);
                g.drawEllipse(endX - 5.0f, 14.0f, 10.0f, 10.0f, 1.5f);
            }
        }
    }

    // 5. 拡大時のみ右下にズーム倍率を控えめに表示
    if (zoomLevel > 1.05f)
    {
        juce::String zoomText = "x" + juce::String(zoomLevel, (zoomLevel >= 10.0f ? 0 : 1));
        g.setColour(AnatomyColors::textDim.withAlpha(0.6f));
        g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
        g.drawText(zoomText, getWidth() - 50, getHeight() - 18, 44, 14, juce::Justification::centredRight, false);
    }

    // 6. 外枠境界線
    g.setColour(AnatomyColors::panelLine);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);
}

void WaveformComponent::resized()
{
}

void WaveformComponent::mouseDown(const juce::MouseEvent& e)
{
    if (onFocusClicked)
        onFocusClicked();

    if (internalBuffer.getNumSamples() == 0) return;

    // 右クリック: ズーム ＆ スクロール一発リセット
    if (e.mods.isRightButtonDown())
    {
        zoomLevel = 1.0f;
        viewOffsetMs = 0.0f;
        repaint();
        return;
    }

    // 中ボタンドラッグ判定（スクロール）
    if (e.mods.isMiddleButtonDown() || e.mods.isAltDown())
    {
        currentDragMode = DragMode::ScrollWaveform;
        dragStartPos = e.position;
        dragStartViewOffsetMs = viewOffsetMs;
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }

    if (laneIndex != 0 && endOffsetMs > 0.0f)
    {
        float mx = static_cast<float>(e.x);
        float my = static_cast<float>(e.y);
        float midY = getHeight() * 0.5f;

        float startX = getXFromMs(startOffsetMs);
        float endX = getXFromMs(endOffsetMs);
        float fInX = getXFromMs(startOffsetMs + fadeInMs);
        float fOutX = getXFromMs(endOffsetMs - fadeOutMs);

        // 1. 上部 ▶ / ◀ 三角形マーカー判定 (y <= 16)
        if (my <= 18.0f)
        {
            if (mx >= startX - 4.0f && mx <= startX + 14.0f)
            {
                currentDragMode = DragMode::StartMarker;
                return;
            }
            if (mx >= endX - 14.0f && mx <= endX + 4.0f)
            {
                currentDragMode = DragMode::EndMarker;
                return;
            }
        }

        // 2. FadeIn テンションハンドル判定 (カーブ中央 ●)
        if (fadeInMs > 0.1f)
        {
            float fMidX = (startX + fInX) * 0.5f;
            float fMidY = getHeight() - calculateFadeGain(0.5f, fadeInTension) * (getHeight() - 4.0f);
            if (std::abs(mx - fMidX) <= 8.0f && std::abs(my - fMidY) <= 8.0f)
            {
                currentDragMode = DragMode::FadeInTension;
                dragStartPos = e.position;
                return;
            }
        }

        // 3. FadeOut テンションハンドル判定 (カーブ中央 ●)
        if (fadeOutMs > 0.1f)
        {
            float fMidX = (fOutX + endX) * 0.5f;
            float fMidY = getHeight() - calculateFadeGain(0.5f, fadeOutTension) * (getHeight() - 4.0f);
            if (std::abs(mx - fMidX) <= 8.0f && std::abs(my - fMidY) <= 8.0f)
            {
                currentDragMode = DragMode::FadeOutTension;
                dragStartPos = e.position;
                return;
            }
        }

        // 4. FadeIn ハンドル判定 (真ん中 y=mid 付近の ●)
        if (std::abs(mx - fInX) <= 8.0f && std::abs(my - midY) <= 14.0f)
        {
            currentDragMode = DragMode::FadeInHandle;
            dragStartMs = fadeInMs;
            return;
        }

        // 5. FadeOut ハンドル判定 (真ん中 y=mid 付近の ●)
        if (std::abs(mx - fOutX) <= 8.0f && std::abs(my - midY) <= 14.0f)
        {
            currentDragMode = DragMode::FadeOutHandle;
            dragStartMs = fadeOutMs;
            return;
        }

        // 6. 縦線マーカー判定
        if (std::abs(mx - startX) <= 6.0f)
        {
            currentDragMode = DragMode::StartMarker;
            return;
        }
        if (std::abs(mx - endX) <= 6.0f)
        {
            currentDragMode = DragMode::EndMarker;
            return;
        }
    }

    // 波形背景ドラッグでスクロール
    if (zoomLevel > 1.01f)
    {
        currentDragMode = DragMode::ScrollWaveform;
        dragStartPos = e.position;
        dragStartViewOffsetMs = viewOffsetMs;
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    }
}

void WaveformComponent::mouseDrag(const juce::MouseEvent& e)
{
    const int numSamples = internalBuffer.getNumSamples();
    if (numSamples == 0 || sampleRate <= 0.0) return;

    double totalMs = (static_cast<double>(numSamples) / sampleRate) * 1000.0;
    const float* data = internalBuffer.getReadPointer(0);

    float mouseMs = getMsFromX(static_cast<float>(e.x));
    bool shouldSnap = snapEnabled && !e.mods.isShiftDown();

    switch (currentDragMode)
    {
    case DragMode::StartMarker:
    {
        float targetMs = juce::jlimit(0.0f, endOffsetMs - 0.5f, mouseMs);
        if (shouldSnap)
        {
            int targetSmp = static_cast<int>((targetMs / 1000.0f) * sampleRate);
            int snappedSmp = findNearestZeroCrossing(data, numSamples, targetSmp);
            targetMs = (static_cast<float>(snappedSmp) / static_cast<float>(sampleRate)) * 1000.0f;
            isSnappedToZeroCrossing = true;
        }
        else
        {
            isSnappedToZeroCrossing = false;
        }
        startOffsetMs = targetMs;
        synchronizeToTargetSliders(startOffsetMs, endOffsetMs);
        repaint();
        break;
    }
    case DragMode::EndMarker:
    {
        float targetMs = juce::jlimit(startOffsetMs + 0.5f, static_cast<float>(totalMs), mouseMs);
        if (shouldSnap)
        {
            int targetSmp = static_cast<int>((targetMs / 1000.0f) * sampleRate);
            int snappedSmp = findNearestZeroCrossing(data, numSamples, targetSmp);
            targetMs = (static_cast<float>(snappedSmp) / static_cast<float>(sampleRate)) * 1000.0f;
            isSnappedToZeroCrossing = true;
        }
        else
        {
            isSnappedToZeroCrossing = false;
        }
        endOffsetMs = targetMs;
        synchronizeToTargetSliders(startOffsetMs, endOffsetMs);
        repaint();
        break;
    }
    case DragMode::FadeInHandle:
    {
        float newFadeIn = juce::jlimit(0.0f, (endOffsetMs - startOffsetMs) * 0.95f, mouseMs - startOffsetMs);
        fadeInMs = newFadeIn;
        updateFadeToProcessor();
        repaint();
        break;
    }
    case DragMode::FadeOutHandle:
    {
        float newFadeOut = juce::jlimit(0.0f, (endOffsetMs - startOffsetMs) * 0.95f, endOffsetMs - mouseMs);
        fadeOutMs = newFadeOut;
        updateFadeToProcessor();
        repaint();
        break;
    }
    case DragMode::FadeInTension:
    {
        float dy = dragStartPos.y - static_cast<float>(e.y);
        fadeInTension = juce::jlimit(-1.0f, 1.0f, dy / 40.0f);
        updateFadeToProcessor();
        repaint();
        break;
    }
    case DragMode::FadeOutTension:
    {
        float dy = dragStartPos.y - static_cast<float>(e.y);
        fadeOutTension = juce::jlimit(-1.0f, 1.0f, dy / 40.0f);
        updateFadeToProcessor();
        repaint();
        break;
    }
    case DragMode::ScrollWaveform:
    {
        double visibleMs = totalMs / static_cast<double>(zoomLevel);
        double msPerPixel = visibleMs / static_cast<double>(getWidth());
        float dx = dragStartPos.x - static_cast<float>(e.x);
        viewOffsetMs = static_cast<float>(juce::jlimit(0.0, totalMs - visibleMs, dragStartViewOffsetMs + dx * msPerPixel));
        repaint();
        break;
    }
    default:
        break;
    }
}

void WaveformComponent::mouseUp(const juce::MouseEvent&)
{
    currentDragMode = DragMode::None;
    isSnappedToZeroCrossing = false;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
}

void WaveformComponent::mouseMove(const juce::MouseEvent& e)
{
    if (laneIndex == 0 || endOffsetMs <= 0.0f)
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    float mx = static_cast<float>(e.x);
    float my = static_cast<float>(e.y);
    float midY = getHeight() * 0.5f;
    float startX = getXFromMs(startOffsetMs);
    float endX = getXFromMs(endOffsetMs);
    float fInX = getXFromMs(startOffsetMs + fadeInMs);
    float fOutX = getXFromMs(endOffsetMs - fadeOutMs);

    if (my <= 18.0f && ((mx >= startX - 4.0f && mx <= startX + 14.0f) || (mx >= endX - 14.0f && mx <= endX + 4.0f)))
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }
    else if (std::abs(my - midY) <= 14.0f && (std::abs(mx - fInX) <= 8.0f || std::abs(mx - fOutX) <= 8.0f))
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    else if (std::abs(mx - startX) <= 6.0f || std::abs(mx - endX) <= 6.0f)
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }
    else
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

void WaveformComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int numSamples = internalBuffer.getNumSamples();
    if (numSamples == 0 || sampleRate <= 0.0) return;

    double totalMs = (static_cast<double>(numSamples) / sampleRate) * 1000.0;

    // Shift + ホイール: 水平スクロール
    if (e.mods.isShiftDown())
    {
        double visibleMs = totalMs / static_cast<double>(zoomLevel);
        viewOffsetMs = static_cast<float>(juce::jlimit(0.0, totalMs - visibleMs, viewOffsetMs - wheel.deltaY * visibleMs * 0.15));
        repaint();
        return;
    }

    // Ctrl + ホイール または 通常ホイール: マウス位置を中心としたスムーズズーム
    float delta = (std::abs(wheel.deltaY) > 0.0001f) ? wheel.deltaY : wheel.deltaX;
    if (std::abs(delta) < 0.0001f) return;

    float mouseMs = getMsFromX(static_cast<float>(e.x));
    float factor = (delta > 0.0f) ? 1.25f : 0.8f;
    if (std::abs(delta) > 0.05f)
        factor = std::pow(1.25f, delta * 4.0f);

    float newZoom = juce::jlimit(zoomMin, zoomMax, zoomLevel * factor);
    if (std::abs(newZoom - zoomLevel) < 0.001f) return;

    zoomLevel = newZoom;

    if (zoomLevel <= 1.01f)
    {
        viewOffsetMs = 0.0f;
    }
    else
    {
        double visibleMs = totalMs / static_cast<double>(zoomLevel);
        float mouseRatio = static_cast<float>(e.x) / static_cast<float>(getWidth());
        viewOffsetMs = static_cast<float>(juce::jlimit(0.0, totalMs - visibleMs, mouseMs - mouseRatio * visibleMs));
    }
    repaint();
}

void WaveformComponent::synchronizeToTargetSliders(float startMs, float endMs)
{
    if (processor == nullptr) return;
    processor->setOffsetsFromUI((laneIndex == 1), startMs, endMs);
}

void WaveformComponent::updateFadeToProcessor()
{
    if (processor == nullptr || laneIndex == 0) return;
    processor->setFadeFromUI((laneIndex == 1), fadeInMs, fadeOutMs, fadeInTension, fadeOutTension);
    if (onFadeChanged)
        onFadeChanged(fadeInMs, fadeOutMs, fadeInTension, fadeOutTension);
}