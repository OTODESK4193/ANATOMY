// ==========================================
// File: WaveformComponent.cpp
// 高精度波形描画・PicoSampler式スムーズドラッグ・ゼロクロススナップ・選択枠線
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
    int numS = buffer.getNumSamples();
    int numCh = buffer.getNumChannels();

    if (numS == 0 || numCh == 0)
    {
        if (internalBuffer.getNumSamples() != 0 || internalBuffer.getNumChannels() != 0)
            internalBuffer.setSize(0, 0);
        needsWaveformCacheUpdate = true;
        repaint();
        return;
    }

    bool isDifferentLength = (internalBuffer.getNumSamples() != numS || internalBuffer.getNumChannels() != numCh);
    internalBuffer.makeCopyOf(buffer);
    if (isDifferentLength || !hasInitializedZoom)
    {
        autoFitToContent();
        hasInitializedZoom = true;
    }
    needsWaveformCacheUpdate = true;
    repaint();
}

void WaveformComponent::autoFitToContent()
{
    needsWaveformCacheUpdate = true;
    const int numSamples = internalBuffer.getNumSamples();
    const int numChannels = internalBuffer.getNumChannels();
    if (numSamples == 0 || numChannels == 0 || sampleRate <= 0.0)
    {
        zoomLevel = 1.0f;
        viewOffsetMs = 0.0f;
        return;
    }

    double totalMs = (static_cast<double>(numSamples) / sampleRate) * 1000.0;
    const float* data = internalBuffer.getReadPointer(0);

    // 最大ピークを検出
    float maxPeak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        maxPeak = std::max(maxPeak, std::abs(data[i]));

    if (maxPeak < 0.001f)
    {
        zoomLevel = 1.0f;
        viewOffsetMs = 0.0f;
        return;
    }

    // 末尾から有効信号（-42dB）を検出
    float threshold = maxPeak * 0.008f;
    int lastActiveSample = numSamples - 1;
    for (int i = numSamples - 1; i >= 0; --i)
    {
        if (std::abs(data[i]) >= threshold)
        {
            lastActiveSample = i;
            break;
        }
    }

    double activeMs = (static_cast<double>(lastActiveSample) / sampleRate) * 1000.0;
    activeMs = std::max(10.0, activeMs);

    if (laneIndex == 1) // Transient レーン
    {
        // 過渡音は通常 20ms〜80ms。有効期間が画面の 60〜75% に収まるようにズーム
        double targetViewMs = std::max(40.0, activeMs * 1.35);
        if (targetViewMs < totalMs)
            zoomLevel = juce::jlimit(zoomMin, zoomMax, static_cast<float>(totalMs / targetViewMs));
        else
            zoomLevel = 1.0f;
    }
    else // Tonal & FullMix レーン
    {
        // 有効期間が画面の 85% に収まるようにズーム（末尾の長い無音をカット）
        double targetViewMs = activeMs * 1.15;
        if (targetViewMs < totalMs * 0.9)
            zoomLevel = juce::jlimit(zoomMin, zoomMax, static_cast<float>(totalMs / targetViewMs));
        else
            zoomLevel = 1.0f;
    }

    viewOffsetMs = 0.0f;
}

void WaveformComponent::setOffsets(float startMs, float endMs, double sr) noexcept
{
    const juce::ScopedLock sl(renderLock);
    // ドラッグ中はタイマーによる古い値の上書き（チラつき・点滅・ガタつき）を完全遮断！
    if (currentDragMode == DragMode::StartMarker || currentDragMode == DragMode::EndMarker)
        return;

    float resolvedEndMs = endMs;
    if (resolvedEndMs <= 0.0f && internalBuffer.getNumSamples() > 0 && sr > 0.0)
        resolvedEndMs = static_cast<float>((static_cast<double>(internalBuffer.getNumSamples()) / sr) * 1000.0);

    if (std::abs(startOffsetMs - startMs) < 0.001f && std::abs(endOffsetMs - resolvedEndMs) < 0.001f && sampleRate == sr)
        return;

    startOffsetMs = startMs;
    endOffsetMs = resolvedEndMs;
    sampleRate = sr;
    repaint();
}

void WaveformComponent::setFade(float inMs, float outMs, float inTension, float outTension) noexcept
{
    const juce::ScopedLock sl(renderLock);
    if (currentDragMode == DragMode::FadeInHandle || currentDragMode == DragMode::FadeOutHandle ||
        currentDragMode == DragMode::FadeInTension || currentDragMode == DragMode::FadeOutTension)
        return;

    if (fadeInMs == inMs && fadeOutMs == outMs && fadeInTension == inTension && fadeOutTension == outTension)
        return;

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
    needsWaveformCacheUpdate = true;
    repaint();
}

float WaveformComponent::getMsFromX(float x) const noexcept
{
    const int numSamples = internalBuffer.getNumSamples();
    const int numChannels = internalBuffer.getNumChannels();
    if (numSamples == 0 || numChannels == 0 || sampleRate <= 0.0 || getWidth() <= 0) return 0.0f;

    double totalMs = (static_cast<double>(numSamples) / sampleRate) * 1000.0;
    double visibleMs = totalMs / static_cast<double>(zoomLevel);
    return static_cast<float>(viewOffsetMs + (static_cast<double>(x) / static_cast<double>(getWidth())) * visibleMs);
}

float WaveformComponent::getXFromMs(float ms) const noexcept
{
    const int numSamples = internalBuffer.getNumSamples();
    const int numChannels = internalBuffer.getNumChannels();
    if (numSamples == 0 || numChannels == 0 || sampleRate <= 0.0 || getWidth() <= 0) return 0.0f;

    double totalMs = (static_cast<double>(numSamples) / sampleRate) * 1000.0;
    double visibleMs = totalMs / static_cast<double>(zoomLevel);
    if (visibleMs <= 0.0) return 0.0f;

    return static_cast<float>(((static_cast<double>(ms) - viewOffsetMs) / visibleMs) * static_cast<double>(getWidth()));
}

float WaveformComponent::findZeroCrossingMs(float targetMs, float magnetPixels) const noexcept
{
    const juce::ScopedLock sl(renderLock);
    const int numSamples = internalBuffer.getNumSamples();
    const int numChannels = internalBuffer.getNumChannels();
    if (numSamples <= 1 || numChannels == 0 || sampleRate <= 0.0) return targetMs;

    const float* data = internalBuffer.getReadPointer(0);
    int targetSample = static_cast<int>((targetMs / 1000.0f) * sampleRate);
    targetSample = juce::jlimit(0, numSamples - 1, targetSample);

    // 画面ピクセル距離基準（magnetPixels px）の探索サンプル半径
    double totalMs = (static_cast<double>(numSamples) / sampleRate) * 1000.0;
    double visibleMs = totalMs / static_cast<double>(zoomLevel);
    double msPerPixel = visibleMs / static_cast<double>(std::max(1, getWidth()));
    int magnetSamples = static_cast<int>((magnetPixels * msPerPixel / 1000.0) * sampleRate);
    magnetSamples = juce::jlimit(4, 256, magnetSamples);

    int startRange = std::max(0, targetSample - magnetSamples);
    int endRange = std::min(numSamples - 2, targetSample + magnetSamples);

    // 無音または極小振幅時は全サンプルがゼロクロス判定に合致するため探索をバイパス
    float maxAbsInWindow = 0.0f;
    for (int s = startRange; s <= endRange; ++s)
    {
        float a = std::abs(data[s]);
        if (a > maxAbsInWindow) maxAbsInWindow = a;
    }
    if (maxAbsInWindow < 0.0005f) return targetMs;

    int bestZeroCrossingS = -1;
    float bestFrac = 0.0f;
    int bestDist = 1000000;

    for (int s = startRange; s <= endRange; ++s)
    {
        float v0 = data[s];
        float v1 = data[s + 1];
        if ((v0 <= 0.0f && v1 >= 0.0f) || (v0 >= 0.0f && v1 <= 0.0f))
        {
            int dist = std::abs(s - targetSample);
            if (dist < bestDist)
            {
                bestDist = dist;
                bestZeroCrossingS = s;
                float diff = v1 - v0;
                bestFrac = (std::abs(diff) > 1.0e-7f) ? juce::jlimit(0.0f, 1.0f, -v0 / diff) : 0.0f;
            }
        }
    }

    if (bestZeroCrossingS >= 0)
    {
        double exactSample = static_cast<double>(bestZeroCrossingS) + static_cast<double>(bestFrac);
        return static_cast<float>((exactSample / sampleRate) * 1000.0);
    }

    return targetMs;
}

void WaveformComponent::renderWaveformCache()
{
    const juce::ScopedLock sl(renderLock);

    const int w = getWidth();
    const int h = getHeight();
    if (w <= 0 || h <= 0) return;

    if (cachedWaveformImage.isNull() || cachedWaveformImage.getWidth() != w || cachedWaveformImage.getHeight() != h)
    {
        cachedWaveformImage = juce::Image(juce::Image::ARGB, w, h, true);
    }
    else
    {
        cachedWaveformImage.clear(juce::Rectangle<int>(0, 0, w, h), juce::Colours::transparentBlack);
    }

    const int numSamples = internalBuffer.getNumSamples();
    const int numChannels = internalBuffer.getNumChannels();
    if (numSamples == 0 || numChannels == 0 || sampleRate <= 0.0)
    {
        needsWaveformCacheUpdate = false;
        return;
    }

    juce::Graphics g(cachedWaveformImage);
    const float mid = static_cast<float>(h) * 0.5f;
    const float* data = internalBuffer.getReadPointer(0);
    double totalMs = (static_cast<double>(numSamples) / sampleRate) * 1000.0;
    double visibleMs = totalMs / static_cast<double>(zoomLevel);

    int startSampleIdx = static_cast<int>((viewOffsetMs / 1000.0) * sampleRate);
    int endSampleIdx = static_cast<int>(((viewOffsetMs + visibleMs) / 1000.0) * sampleRate);
    startSampleIdx = juce::jlimit(0, numSamples - 1, startSampleIdx);
    endSampleIdx = juce::jlimit(1, numSamples, endSampleIdx);

    int visibleSampleCount = std::max(1, endSampleIdx - startSampleIdx);
    float samplesPerPixel = static_cast<float>(visibleSampleCount) / static_cast<float>(w);

    bool isBeforeMode = (processor != nullptr && processor->beforeAfterBypasser.julesIsBeforeBypassed());

    // 高精度波形描画
    if (samplesPerPixel >= 1.0f)
    {
        if (laneIndex == 0 && !isBeforeMode && !componentRatios.empty())
        {
            // FullMix 3色（Transient: Mint / Layer: Peach / Tonal: Pink）比率グラデーション加算描画
            bool hasDualRatios = (componentRatios.size() >= static_cast<size_t>(numSamples * 2));

            for (int xPix = 0; xPix < w; ++xPix)
            {
                int s0 = startSampleIdx + static_cast<int>(static_cast<float>(xPix) * samplesPerPixel);
                int s1 = startSampleIdx + static_cast<int>(static_cast<float>(xPix + 1) * samplesPerPixel);
                s0 = juce::jlimit(0, numSamples - 1, s0);
                s1 = juce::jlimit(s0 + 1, numSamples, s1);

                float minV = 0.0f, maxV = 0.0f;
                float rTrans = hasDualRatios ? componentRatios[s0 * 2] : componentRatios[s0];
                float rLayer = hasDualRatios ? componentRatios[s0 * 2 + 1] : 0.0f;

                for (int s = s0; s < s1; ++s)
                {
                    float v = data[s];
                    if (v < minV) minV = v;
                    if (v > maxV) maxV = v;
                }

                float yTop = mid - maxV * (mid - 2.0f);
                float yBtm = mid - minV * (mid - 2.0f);
                if (std::abs(yBtm - yTop) < 1.0f) { yTop = mid - 0.5f; yBtm = mid + 0.5f; }

                float peak = std::max(std::abs(maxV), std::abs(minV));
                float totalH = peak * (mid - 2.0f);
                float yTrans = std::min(totalH, totalH * rTrans);
                float yLayer = std::min(totalH - yTrans, totalH * rLayer);

                // 1. Transient (Mint / Accent - 中心)
                if (yTrans > 0.4f)
                {
                    g.setColour(AnatomyColors::accentTransient.withAlpha(0.95f));
                    g.drawVerticalLine(xPix, mid - yTrans, mid + yTrans);
                }

                // 2. Layer (Peach - 中間層)
                if (yLayer > 0.4f)
                {
                    g.setColour(AnatomyColors::peach.withAlpha(0.92f));
                    g.drawVerticalLine(xPix, mid - (yTrans + yLayer), mid - yTrans);
                    g.drawVerticalLine(xPix, mid + yTrans, mid + (yTrans + yLayer));
                }

                // 3. Tonal (Pink / Accent - 外側)
                float yInner = yTrans + yLayer;
                g.setColour(AnatomyColors::accentTonal.withAlpha(0.85f));
                if (yTop < mid - yInner) g.drawVerticalLine(xPix, yTop, mid - yInner);
                if (yBtm > mid + yInner) g.drawVerticalLine(xPix, mid + yInner, yBtm);
            }
        }
        else
        {
            // 単色パステル Min/Max 描画
            juce::Colour waveColour = (laneIndex == 1) ? AnatomyColors::accentTransient :
                                      (laneIndex == 2) ? AnatomyColors::accentTonal :
                                      (laneIndex == 3) ? AnatomyColors::peach :
                                                         AnatomyColors::accentFull;

            for (int xPix = 0; xPix < w; ++xPix)
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
        // 極限ズーム時: サンプル点補間 ＆ ドット（●）描画
        juce::Colour waveColour = (laneIndex == 1) ? AnatomyColors::accentTransient :
                                  (laneIndex == 2) ? AnatomyColors::accentTonal :
                                  (laneIndex == 3) ? AnatomyColors::peach :
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

            if (sx >= -5.0f && sx <= static_cast<float>(w) + 5.0f)
            {
                g.setColour(waveColour);
                g.fillEllipse(sx - 2.5f, sy - 2.5f, 5.0f, 5.0f);
                g.setColour(juce::Colours::white.withAlpha(0.8f));
                g.fillEllipse(sx - 1.0f, sy - 1.0f, 2.0f, 2.0f);
            }
        }
    }

    needsWaveformCacheUpdate = false;
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
    const int numChannels = internalBuffer.getNumChannels();
    if (numSamples == 0 || numChannels == 0 || w <= 0.0f || h <= 0.0f || sampleRate <= 0.0)
    {
        g.setColour(AnatomyColors::textDim.withAlpha(0.4f));
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText("Drag & Drop WAV Audio File Here", getLocalBounds(), juce::Justification::centred, false);

        g.setColour(isSelected ? (laneIndex == 0 ? AnatomyColors::accentFull : (laneIndex == 1 ? AnatomyColors::accentTransient : (laneIndex == 2 ? AnatomyColors::accentTonal : AnatomyColors::peach))) : AnatomyColors::panelLine);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, isSelected ? 1.5f : 1.0f);
        return;
    }

    double totalMs = (sampleRate > 0.0) ? (static_cast<double>(numSamples) / sampleRate) * 1000.0 : 0.0;
    double visibleMs = (zoomLevel > 0.0f) ? totalMs / static_cast<double>(zoomLevel) : totalMs;

    // 3. 高精度波形描画（オフスクリーンキャッシュからの超高速転送）
    if (needsWaveformCacheUpdate || cachedWaveformImage.isNull() ||
        cachedWaveformImage.getWidth() != static_cast<int>(w) ||
        cachedWaveformImage.getHeight() != static_cast<int>(h))
    {
        renderWaveformCache();
    }

    if (!cachedWaveformImage.isNull())
    {
        g.drawImageAt(cachedWaveformImage, 0, 0);
    }

    // 4. Start / End トリミングマスク ＆ フェードイン・フェードアウト描画（全レーン共通）
    if (endOffsetMs > 0.0f)
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
            float denomIn = fInEndX - startX;

            if (fInW > 1.0f && denomIn > 0.5f)
            {
                juce::Path fadeArea;
                fadeArea.startNewSubPath(fInStartX, 0.0f);
                for (float fx = fInStartX; fx <= fInEndX; fx += 2.0f)
                {
                    float prog = juce::jlimit(0.0f, 1.0f, (fx - startX) / denomIn);
                    float gain = calculateFadeGain(prog, fadeInTension);
                    float curH = h * (1.0f - gain);
                    fadeArea.lineTo(fx, curH);
                }
                fadeArea.lineTo(fInEndX, 0.0f);
                fadeArea.closeSubPath();

                g.setColour(AnatomyColors::peach.withAlpha(0.18f));
                g.fillPath(fadeArea);

                juce::Path fadeLine;
                fadeLine.startNewSubPath(fInStartX, h);
                for (float fx = fInStartX; fx <= fInEndX; fx += 2.0f)
                {
                    float prog = juce::jlimit(0.0f, 1.0f, (fx - startX) / denomIn);
                    float gain = calculateFadeGain(prog, fadeInTension);
                    fadeLine.lineTo(fx, h - gain * (h - 4.0f));
                }
                g.setColour(AnatomyColors::peach.withAlpha(0.9f));
                g.strokePath(fadeLine, juce::PathStrokeType(1.6f));

                // FadeIn テンションハンドル (カーブ中央 ●)
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
            float fInX = getXFromMs(startOffsetMs);
            if (fInX >= 0.0f && fInX <= w)
            {
                g.setColour(AnatomyColors::peach.withAlpha(0.85f));
                g.fillEllipse(fInX + 2.0f, mid - 4.0f, 8.0f, 8.0f);
            }
        }

        // ── FadeOut 描画 ──
        if (fadeOutMs > 0.1f)
        {
            float fOutStartX = getXFromMs(endOffsetMs - fadeOutMs);
            float fOutEndX = std::min(w, endX);
            float fOutW = fOutEndX - fOutStartX;
            float denomOut = endX - fOutStartX;

            if (fOutW > 1.0f && denomOut > 0.5f)
            {
                juce::Path fadeArea;
                fadeArea.startNewSubPath(fOutStartX, 0.0f);
                for (float fx = fOutStartX; fx <= fOutEndX; fx += 2.0f)
                {
                    float prog = juce::jlimit(0.0f, 1.0f, (endX - fx) / denomOut);
                    float gain = calculateFadeGain(prog, fadeOutTension);
                    float curH = h * (1.0f - gain);
                    fadeArea.lineTo(fx, curH);
                }
                fadeArea.lineTo(fOutEndX, 0.0f);
                fadeArea.closeSubPath();

                g.setColour(AnatomyColors::rose.withAlpha(0.18f));
                g.fillPath(fadeArea);

                juce::Path fadeLine;
                fadeLine.startNewSubPath(fOutStartX, 4.0f);
                for (float fx = fOutStartX; fx <= fOutEndX; fx += 2.0f)
                {
                    float prog = juce::jlimit(0.0f, 1.0f, (endX - fx) / denomOut);
                    float gain = calculateFadeGain(prog, fadeOutTension);
                    fadeLine.lineTo(fx, h - gain * (h - 4.0f));
                }
                g.setColour(AnatomyColors::rose.withAlpha(0.9f));
                g.strokePath(fadeLine, juce::PathStrokeType(1.6f));

                // FadeOut テンションハンドル (カーブ中央 ●)
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
            float fOutX = std::min(w - 6.0f, getXFromMs(endOffsetMs));
            if (fOutX >= 0.0f && fOutX <= w)
            {
                g.setColour(AnatomyColors::rose.withAlpha(0.85f));
                g.fillEllipse(fOutX - 10.0f, mid - 4.0f, 8.0f, 8.0f);
            }
        }

        // ── START マーカー (上部 ▶ 三角形 ＋ 縦線) ──
        if (startX >= -5.0f && startX <= w + 5.0f)
        {
            g.setColour(juce::Colours::white.withAlpha(0.6f));
            g.drawVerticalLine(static_cast<int>(startX), 12.0f, h);

            juce::Path tri;
            tri.startNewSubPath(startX, 0.0f);
            tri.lineTo(startX + 10.0f, 6.0f);
            tri.lineTo(startX, 12.0f);
            tri.closeSubPath();
            
            // 影/縁取り効果として少しずらして黒で描画
            g.setColour(juce::Colours::black.withAlpha(0.5f));
            g.strokePath(tri, juce::PathStrokeType(2.5f));
            
            g.setColour(juce::Colours::white);
            g.fillPath(tri);
            
            if (isSnappedToZeroCrossing && currentDragMode == DragMode::StartMarker)
            {
                g.setColour(AnatomyColors::mint);
                g.drawEllipse(startX - 5.0f, 14.0f, 10.0f, 10.0f, 1.5f);
            }
        }

        // ── END マーカー (上部 ◀ 三角形 ＋ 縦線) ──
        float drawEndX = std::min(w - 2.0f, endX);
        if (drawEndX >= -5.0f && drawEndX <= w + 5.0f)
        {
            g.setColour(juce::Colours::white.withAlpha(0.6f));
            g.drawVerticalLine(static_cast<int>(drawEndX), 12.0f, h);

            juce::Path tri;
            tri.startNewSubPath(drawEndX, 0.0f);
            tri.lineTo(drawEndX - 10.0f, 6.0f);
            tri.lineTo(drawEndX, 12.0f);
            tri.closeSubPath();

            // 影/縁取り効果として少しずらして黒で描画
            g.setColour(juce::Colours::black.withAlpha(0.5f));
            g.strokePath(tri, juce::PathStrokeType(2.5f));

            g.setColour(juce::Colours::white);
            g.fillPath(tri);

            if (isSnappedToZeroCrossing && currentDragMode == DragMode::EndMarker)
            {
                g.setColour(AnatomyColors::mint);
                g.drawEllipse(drawEndX - 5.0f, 14.0f, 10.0f, 10.0f, 1.5f);
            }
        }
    }

    // 5. 拡大時の最下部スクロールバー描画 (zoomLevel > 1.05f)
    if (zoomLevel > 1.05f)
    {
        float sbH = 5.0f;
        float sbY = h - sbH - 1.0f;

        // トラック（背景溝）
        g.setColour(AnatomyColors::knobTrack.withAlpha(0.5f));
        g.fillRoundedRectangle(2.0f, sbY, w - 4.0f, sbH, 2.5f);

        // Thumb（つまみ）
        float thumbW = static_cast<float>((visibleMs / totalMs) * (w - 4.0f));
        thumbW = std::max(16.0f, thumbW);
        float thumbX = 2.0f + static_cast<float>((viewOffsetMs / totalMs) * (w - 4.0f));
        thumbX = juce::jlimit(2.0f, w - 2.0f - thumbW, thumbX);

        juce::Colour thumbColour = (laneIndex == 1) ? AnatomyColors::accentTransient :
                                  (laneIndex == 2) ? AnatomyColors::accentTonal :
                                  (laneIndex == 3) ? AnatomyColors::peach :
                                                     AnatomyColors::accentFull;
        g.setColour(thumbColour.withAlpha(currentDragMode == DragMode::ScrollBarThumb ? 0.9f : 0.65f));
        g.fillRoundedRectangle(thumbX, sbY, thumbW, sbH, 2.5f);

        // ズーム倍率表示
        juce::String zoomText = "x" + juce::String(zoomLevel, (zoomLevel >= 10.0f ? 0 : 1));
        g.setColour(AnatomyColors::textDim.withAlpha(0.6f));
        g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
        g.drawText(zoomText, getWidth() - 50, getHeight() - 22, 44, 14, juce::Justification::centredRight, false);
    }

    // 6. 外枠境界線 (選択時は各アクセント色で光る)
    juce::Colour borderCol = isSelected ? (laneIndex == 0 ? AnatomyColors::accentFull :
                                           (laneIndex == 1 ? AnatomyColors::accentTransient :
                                           (laneIndex == 2 ? AnatomyColors::accentTonal :
                                                             AnatomyColors::peach))) : AnatomyColors::panelLine;
    g.setColour(borderCol);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, isSelected ? 1.5f : 1.0f);
}

void WaveformComponent::resized()
{
    needsWaveformCacheUpdate = true;
}

void WaveformComponent::mouseDown(const juce::MouseEvent& e)
{
    if (onFocusClicked)
        onFocusClicked();

    if (internalBuffer.getNumSamples() == 0) return;

    if (endOffsetMs <= 0.0f && sampleRate > 0.0 && internalBuffer.getNumSamples() > 0)
    {
        endOffsetMs = static_cast<float>((static_cast<double>(internalBuffer.getNumSamples()) / sampleRate) * 1000.0);
    }

    // 右クリック: ズーム ＆ スクロール一発リセット
    if (e.mods.isRightButtonDown())
    {
        zoomLevel = 1.0f;
        viewOffsetMs = 0.0f;
        needsWaveformCacheUpdate = true;
        repaint();
        return;
    }

    // 最下部スクロールバーのクリック判定
    if (zoomLevel > 1.05f && e.y >= getHeight() - 8)
    {
        const int numSamples = internalBuffer.getNumSamples();
        double totalMs = (static_cast<double>(numSamples) / sampleRate) * 1000.0;
        double visibleMs = totalMs / static_cast<double>(zoomLevel);
        float thumbW = static_cast<float>((visibleMs / totalMs) * (getWidth() - 4.0f));
        thumbW = std::max(16.0f, thumbW);
        float thumbX = 2.0f + static_cast<float>((viewOffsetMs / totalMs) * (getWidth() - 4.0f));

        if (e.x >= thumbX && e.x <= thumbX + thumbW)
        {
            currentDragMode = DragMode::ScrollBarThumb;
            dragStartPos = e.position;
            scrollThumbDragStartOffset = viewOffsetMs;
        }
        else
        {
            float ratio = (static_cast<float>(e.x) - thumbW * 0.5f) / static_cast<float>(getWidth());
            viewOffsetMs = static_cast<float>(juce::jlimit(0.0, totalMs - visibleMs, ratio * totalMs));
            currentDragMode = DragMode::ScrollBarThumb;
            dragStartPos = e.position;
            scrollThumbDragStartOffset = viewOffsetMs;
            needsWaveformCacheUpdate = true;
            repaint();
        }
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

    if (endOffsetMs > 0.0f)
    {
        float mx = static_cast<float>(e.x);
        float my = static_cast<float>(e.y);
        float midY = getHeight() * 0.5f;

        float startX = getXFromMs(startOffsetMs);
        float endX = getXFromMs(endOffsetMs);
        float drawEndX = std::min(static_cast<float>(getWidth() - 2), endX);
        float fInX = getXFromMs(startOffsetMs + fadeInMs);
        float fOutX = (fadeOutMs > 0.1f) ? getXFromMs(endOffsetMs - fadeOutMs) : std::min(static_cast<float>(getWidth() - 6), getXFromMs(endOffsetMs));

        // スマート・アンカリング付き Endマーカー開始ラムダ
        auto startEndMarkerDrag = [&]()
        {
            currentDragMode = DragMode::EndMarker;
            float actualStartMs = endOffsetMs;
            if (endX >= static_cast<float>(getWidth() - 3))
            {
                // 画面外または右端ピン留め時は、見えている画面右端の時間からドラッグを開始
                actualStartMs = getMsFromX(drawEndX);
            }
            dragStartParamMs = actualStartMs;
            dragStartMouseXf = e.position.x;
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        };

        auto startStartMarkerDrag = [&]()
        {
            currentDragMode = DragMode::StartMarker;
            dragStartParamMs = startOffsetMs;
            dragStartMouseXf = e.position.x;
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        };

        // 1. 上部 ▶ / ◀ 三角形マーカー判定 (y <= 18) (最近傍判定で密集時も確実に選択)
        if (my <= 18.0f)
        {
            bool inStartTri = (mx >= startX - 6.0f && mx <= startX + 16.0f);
            bool inEndTri   = (mx >= drawEndX - 16.0f && mx <= drawEndX + 8.0f);
            if (inStartTri && inEndTri)
            {
                if (std::abs(mx - startX) <= std::abs(mx - drawEndX))
                {
                    startStartMarkerDrag();
                    return;
                }
                else
                {
                    startEndMarkerDrag();
                    return;
                }
            }
            else if (inStartTri)
            {
                startStartMarkerDrag();
                return;
            }
            else if (inEndTri)
            {
                startEndMarkerDrag();
                return;
            }
        }

        // 2. 縦線マーカー判定（右端付近では EndMarker を最優先判定）
        float distStart = std::abs(mx - startX);
        float distEnd   = std::abs(mx - drawEndX);
        bool nearStart  = (distStart <= 8.0f);
        bool nearEnd    = (distEnd <= 10.0f || (mx >= static_cast<float>(getWidth() - 16) && endX >= static_cast<float>(getWidth() - 4)));

        if (nearEnd && !nearStart)
        {
            startEndMarkerDrag();
            return;
        }
        else if (nearStart && !nearEnd)
        {
            startStartMarkerDrag();
            return;
        }
        else if (nearStart && nearEnd)
        {
            if (distStart <= distEnd)
                startStartMarkerDrag();
            else
                startEndMarkerDrag();
            return;
        }

        // 3. FadeIn テンションハンドル判定 (カーブ中央 ●)
        if (fadeInMs > 0.1f)
        {
            float fMidX = (startX + fInX) * 0.5f;
            float fMidY = getHeight() - calculateFadeGain(0.5f, fadeInTension) * (getHeight() - 4.0f);
            if (std::abs(mx - fMidX) <= 10.0f && std::abs(my - fMidY) <= 10.0f)
            {
                currentDragMode = DragMode::FadeInTension;
                dragStartPos = e.position;
                dragStartTension = fadeInTension;
                return;
            }
        }

        // 4. FadeOut テンションハンドル判定 (カーブ中央 ●)
        if (fadeOutMs > 0.1f)
        {
            float fMidX = (fOutX + drawEndX) * 0.5f;
            float fMidY = getHeight() - calculateFadeGain(0.5f, fadeOutTension) * (getHeight() - 4.0f);
            if (std::abs(mx - fMidX) <= 10.0f && std::abs(my - fMidY) <= 10.0f)
            {
                currentDragMode = DragMode::FadeOutTension;
                dragStartPos = e.position;
                dragStartTension = fadeOutTension;
                return;
            }
        }

        // 5. FadeIn ハンドル判定 (真ん中 y=mid 付近の ●)
        if (fadeInMs > 0.1f && std::abs(mx - fInX) <= 10.0f && std::abs(my - midY) <= 16.0f)
        {
            currentDragMode = DragMode::FadeInHandle;
            dragStartParamMs = fadeInMs;
            dragStartMouseXf = e.position.x;
            return;
        }

        // 6. FadeOut ハンドル判定 (真ん中 y=mid 付近の ●: フェード有効時のみ判定)
        if (fadeOutMs > 0.1f && std::abs(mx - fOutX) <= 10.0f && std::abs(my - midY) <= 16.0f)
        {
            currentDragMode = DragMode::FadeOutHandle;
            dragStartParamMs = fadeOutMs;
            dragStartMouseXf = e.position.x;
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
    double visibleMs = totalMs / static_cast<double>(zoomLevel);
    double msPerPixel = visibleMs / static_cast<double>(std::max(1, getWidth()));

    bool shouldSnap = snapEnabled && !e.mods.isShiftDown();

    switch (currentDragMode)
    {
    case DragMode::StartMarker:
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        // PicoSampler 方式: サブピクセル変位を加算
        double deltaMs = static_cast<double>(e.position.x - dragStartMouseXf) * msPerPixel;
        double minMarginMs = (1.0 / sampleRate) * 1000.0;
        double maxAllowedMs = std::max(0.0, static_cast<double>(endOffsetMs) - minMarginMs);
        double targetMs = juce::jlimit(0.0, maxAllowedMs, dragStartParamMs + deltaMs);

        if (shouldSnap)
        {
            targetMs = static_cast<double>(findZeroCrossingMs(static_cast<float>(targetMs), 14.0f));
            isSnappedToZeroCrossing = true;
        }
        else
        {
            isSnappedToZeroCrossing = false;
        }

        // スナップ後の逆転防止クランプ
        targetMs = juce::jlimit(0.0, maxAllowedMs, targetMs);

        startOffsetMs = static_cast<float>(targetMs);
        synchronizeToTargetSliders(startOffsetMs, endOffsetMs, false); // ドラッグ中は高速反映
        repaint();
        break;
    }
    case DragMode::EndMarker:
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        double deltaMs = static_cast<double>(e.position.x - dragStartMouseXf) * msPerPixel;
        double minMarginMs = (1.0 / sampleRate) * 1000.0;
        double minAllowedMs = std::min(totalMs, static_cast<double>(startOffsetMs) + minMarginMs);
        double targetMs = juce::jlimit(minAllowedMs, totalMs, dragStartParamMs + deltaMs);

        if (shouldSnap)
        {
            targetMs = static_cast<double>(findZeroCrossingMs(static_cast<float>(targetMs), 14.0f));
            isSnappedToZeroCrossing = true;
        }
        else
        {
            isSnappedToZeroCrossing = false;
        }

        // スナップ後の逆転防止クランプ
        targetMs = juce::jlimit(minAllowedMs, totalMs, targetMs);

        endOffsetMs = static_cast<float>(targetMs);
        synchronizeToTargetSliders(startOffsetMs, endOffsetMs, false); // ドラッグ中は高速反映
        repaint();
        break;
    }
    case DragMode::FadeInHandle:
    {
        double deltaMs = static_cast<double>(e.position.x - dragStartMouseXf) * msPerPixel;
        float maxFade = std::max(0.0f, (endOffsetMs - startOffsetMs) * 0.95f);
        fadeInMs = juce::jlimit(0.0f, maxFade, static_cast<float>(dragStartParamMs + deltaMs));
        updateFadeToProcessor(false);
        repaint();
        break;
    }
    case DragMode::FadeOutHandle:
    {
        double deltaMs = static_cast<double>(dragStartMouseXf - e.position.x) * msPerPixel;
        float maxFade = std::max(0.0f, (endOffsetMs - startOffsetMs) * 0.95f);
        fadeOutMs = juce::jlimit(0.0f, maxFade, static_cast<float>(dragStartParamMs + deltaMs));
        updateFadeToProcessor(false);
        repaint();
        break;
    }
    case DragMode::FadeInTension:
    {
        // 上ドラッグ（dy > 0）で急峻（プラス / 上に凸）、下ドラッグ（dy < 0）でなだらか（マイナス / 下に凹）
        float dy = dragStartPos.y - static_cast<float>(e.y);
        fadeInTension = juce::jlimit(-1.0f, 1.0f, dragStartTension + dy / 40.0f);
        updateFadeToProcessor(false);
        repaint();
        break;
    }
    case DragMode::FadeOutTension:
    {
        // 上ドラッグ（dy > 0）で急峻（プラス / 上に凸）、下ドラッグ（dy < 0）でなだらか（マイナス / 下に凹）
        float dy = dragStartPos.y - static_cast<float>(e.y);
        fadeOutTension = juce::jlimit(-1.0f, 1.0f, dragStartTension + dy / 40.0f);
        updateFadeToProcessor(false);
        repaint();
        break;
    }
    case DragMode::ScrollWaveform:
    {
        float dx = dragStartPos.x - static_cast<float>(e.x);
        viewOffsetMs = static_cast<float>(juce::jlimit(0.0, std::max(0.0, totalMs - visibleMs), static_cast<double>(dragStartViewOffsetMs) + static_cast<double>(dx) * msPerPixel));
        needsWaveformCacheUpdate = true;
        repaint();
        break;
    }
    case DragMode::ScrollBarThumb:
    {
        float dx = static_cast<float>(e.x) - dragStartPos.x;
        double msPerPix = totalMs / static_cast<double>(getWidth());
        viewOffsetMs = static_cast<float>(juce::jlimit(0.0, std::max(0.0, totalMs - visibleMs), static_cast<double>(scrollThumbDragStartOffset) + static_cast<double>(dx) * msPerPix));
        needsWaveformCacheUpdate = true;
        repaint();
        break;
    }
    default:
        break;
    }
}

void WaveformComponent::mouseUp(const juce::MouseEvent&)
{
    if (currentDragMode == DragMode::StartMarker || currentDragMode == DragMode::EndMarker)
    {
        // 値が実際に変化した時のみ確定レンダリングをキック（単なるクリックでの無駄なレンダリングと競合を完全防止）
        bool hasChanged = (currentDragMode == DragMode::StartMarker)
            ? (std::abs(startOffsetMs - static_cast<float>(dragStartParamMs)) > 0.001f)
            : (std::abs(endOffsetMs - static_cast<float>(dragStartParamMs)) > 0.001f);
        synchronizeToTargetSliders(startOffsetMs, endOffsetMs, hasChanged);
    }
    else if (currentDragMode == DragMode::FadeInHandle || currentDragMode == DragMode::FadeOutHandle ||
             currentDragMode == DragMode::FadeInTension || currentDragMode == DragMode::FadeOutTension)
    {
        bool hasChanged = false;
        if (currentDragMode == DragMode::FadeInHandle)
            hasChanged = (std::abs(fadeInMs - static_cast<float>(dragStartParamMs)) > 0.001f);
        else if (currentDragMode == DragMode::FadeOutHandle)
            hasChanged = (std::abs(fadeOutMs - static_cast<float>(dragStartParamMs)) > 0.001f);
        else if (currentDragMode == DragMode::FadeInTension)
            hasChanged = (std::abs(fadeInTension - dragStartTension) > 0.001f);
        else if (currentDragMode == DragMode::FadeOutTension)
            hasChanged = (std::abs(fadeOutTension - dragStartTension) > 0.001f);

        updateFadeToProcessor(hasChanged);
    }

    currentDragMode = DragMode::None;
    isSnappedToZeroCrossing = false;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
}

void WaveformComponent::mouseMove(const juce::MouseEvent& e)
{
    if (endOffsetMs <= 0.0f && sampleRate > 0.0 && internalBuffer.getNumSamples() > 0)
        endOffsetMs = static_cast<float>((static_cast<double>(internalBuffer.getNumSamples()) / sampleRate) * 1000.0);

    if (endOffsetMs <= 0.0f)
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    float mx = static_cast<float>(e.x);
    float my = static_cast<float>(e.y);
    float midY = getHeight() * 0.5f;
    float startX = getXFromMs(startOffsetMs);
    float endX = getXFromMs(endOffsetMs);
    float drawEndX = std::min(static_cast<float>(getWidth() - 2), endX);
    float fInX = getXFromMs(startOffsetMs + fadeInMs);
    float fOutX = (fadeOutMs > 0.1f) ? getXFromMs(endOffsetMs - fadeOutMs) : std::min(static_cast<float>(getWidth() - 6), getXFromMs(endOffsetMs));

    float distStart = std::abs(mx - startX);
    float distEnd   = std::abs(mx - drawEndX);
    bool nearStart  = (distStart <= 8.0f);
    bool nearEnd    = (distEnd <= 10.0f || (mx >= static_cast<float>(getWidth() - 16) && endX >= static_cast<float>(getWidth() - 4)));

    if (my <= 18.0f && ((mx >= startX - 6.0f && mx <= startX + 16.0f) || (mx >= drawEndX - 16.0f && mx <= drawEndX + 8.0f)))
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }
    else if (nearStart || nearEnd)
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }
    else if (std::abs(my - midY) <= 16.0f && ((fadeInMs > 0.1f && std::abs(mx - fInX) <= 10.0f) || (fadeOutMs > 0.1f && std::abs(mx - fOutX) <= 10.0f)))
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    else if (zoomLevel > 1.05f && my >= getHeight() - 8)
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
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
        viewOffsetMs = static_cast<float>(juce::jlimit(0.0, totalMs - visibleMs, static_cast<double>(viewOffsetMs) - static_cast<double>(wheel.deltaY) * visibleMs * 0.15));
        needsWaveformCacheUpdate = true;
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
        viewOffsetMs = static_cast<float>(juce::jlimit(0.0, totalMs - visibleMs, static_cast<double>(mouseMs) - static_cast<double>(mouseRatio) * visibleMs));
    }
    needsWaveformCacheUpdate = true;
    repaint();
}

void WaveformComponent::synchronizeToTargetSliders(float startMs, float endMs, bool notifyProcessor)
{
    if (processor == nullptr) return;
    processor->setOffsetsFromUI(laneIndex, startMs, endMs, notifyProcessor);
}

void WaveformComponent::updateFadeToProcessor(bool triggerOfflineRender)
{
    if (processor == nullptr || laneIndex == 0) return;
    processor->setFadeFromUI(laneIndex, fadeInMs, fadeOutMs, fadeInTension, fadeOutTension, triggerOfflineRender);
    if (onFadeChanged)
        onFadeChanged(fadeInMs, fadeOutMs, fadeInTension, fadeOutTension);
}