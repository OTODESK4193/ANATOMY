#include "WaveformComponent.h"
#include "../PluginProcessor.h"
#include "../PluginEditor.h" 
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
    return static_cast<float>((x / static_cast<float>(getWidth())) * totalMs);
}

float WaveformComponent::getXFromMs(float ms) const noexcept
{
    const int numSamples = internalBuffer.getNumSamples();
    if (numSamples == 0 || sampleRate <= 0.0 || getWidth() <= 0) return 0.0f;

    double totalMs = (static_cast<double>(numSamples) / sampleRate) * 1000.0;
    return static_cast<float>((ms / totalMs) * static_cast<double>(getWidth()));
}

void WaveformComponent::paint(juce::Graphics& g)
{
    const juce::ScopedLock sl(renderLock);

    g.fillAll(juce::Colours::black.withAlpha(0.4f));

    const int numSamples = internalBuffer.getNumSamples();
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());
    const float mid = h * 0.5f;

    if (numSamples == 0 || w <= 0.0f || h <= 0.0f)
    {
        g.setColour(juce::Colours::white.withAlpha(0.2f));
        g.setFont(12.0f);
        g.drawText("Drag & Drop Audio File Here", getLocalBounds(), juce::Justification::centred, false);
        return;
    }

    const float* data = internalBuffer.getReadPointer(0);
    int step = std::max(1, numSamples / static_cast<int>(w));

    bool isBeforeMode = (processor != nullptr && processor->beforeAfterBypasser.julesIsBeforeBypassed());

    if (laneIndex == 0 && !isBeforeMode && componentRatios.size() >= static_cast<size_t>(numSamples))
    {
        for (int xPix = 0; xPix < static_cast<int>(w); ++xPix)
        {
            int srcIdx = static_cast<int>((static_cast<float>(xPix) / w) * static_cast<float>(numSamples));
            srcIdx = juce::jlimit(0, numSamples - 1, srcIdx);

            float peak = 0.0f;
            for (int k = 0; k < step && (srcIdx + k) < numSamples; ++k)
            {
                peak = std::max(peak, std::abs(data[srcIdx + k]));
            }

            float yMax = peak * mid;
            float rTrans = componentRatios[srcIdx];

            float yTrans = yMax * rTrans;
            g.setColour(juce::Colours::cyan.withAlpha(0.85f));
            g.drawVerticalLine(xPix, mid - yTrans, mid + yTrans);

            g.setColour(juce::Colours::magenta.withAlpha(0.65f));
            g.drawVerticalLine(xPix, mid - yMax, mid - yTrans);
            g.drawVerticalLine(xPix, mid + yTrans, mid + yMax);
        }
    }
    else
    {
        juce::Path p;
        p.startNewSubPath(0.0f, mid);

        for (int i = 0; i < numSamples; i += step)
        {
            float x = (static_cast<float>(i) / static_cast<float>(numSamples)) * w;
            float y = data[i] * mid;
            p.lineTo(x, mid - y);
        }

        if (laneIndex == 0)      g.setColour(juce::Colours::white.withAlpha(0.8f));
        else if (laneIndex == 1) g.setColour(juce::Colours::cyan);
        else                     g.setColour(juce::Colours::magenta);

        g.strokePath(p, juce::PathStrokeType(1.0f));
    }

    float startX = getXFromMs(startOffsetMs);
    float endX = getXFromMs(endOffsetMs);

    startX = juce::jlimit(0.0f, w, startX);
    endX = juce::jlimit(0.0f, w, endX);

    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.fillRect(0.0f, 0.0f, startX, h);
    g.fillRect(endX, 0.0f, w - endX, h);

    g.setColour(juce::Colours::yellow);
    g.drawVerticalLine(static_cast<int>(startX), 0.0f, h);
    g.fillEllipse(startX - 3.0f, 2.0f, 6.0f, 6.0f);

    g.setColour(juce::Colours::red);
    g.drawVerticalLine(static_cast<int>(endX), 0.0f, h);
    g.fillEllipse(endX - 3.0f, h - 8.0f, 6.0f, 6.0f);

    g.setColour(juce::Colours::cyan.withAlpha(0.2f));
    g.fillRect(exportButtonArea);
    g.setColour(juce::Colours::cyan);
    g.drawRect(exportButtonArea, 1);
    g.setFont(juce::Font(9.0f, juce::Font::bold));
    g.drawText("DRAG EXPORT", exportButtonArea, juce::Justification::centred, false);
}

void WaveformComponent::resized()
{
    exportButtonArea = juce::Rectangle<int>(getWidth() - 82, 3, 78, 14);
}

void WaveformComponent::mouseDown(const juce::MouseEvent& e)
{
    if (internalBuffer.getNumSamples() == 0) return;

    if (exportButtonArea.contains(e.getPosition()))
    {
        isDraggingExport = true;
        return;
    }

    float mouseX = static_cast<float>(e.x);
    float startX = getXFromMs(startOffsetMs);
    float endX = getXFromMs(endOffsetMs);

    if (std::abs(mouseX - startX) <= 8.0f)       isDraggingStart = true;
    else if (std::abs(mouseX - endX) <= 8.0f)  isDraggingEnd = true;
}

void WaveformComponent::mouseDrag(const juce::MouseEvent& e)
{
    // 💥【核心修正：DAW直流エクスポート】startDraggingを完全撤廃し、OSネイティブD&Dを完全駆動！
    if (isDraggingExport && processor != nullptr)
    {
        isDraggingExport = false;
        juce::File tempWav = processor->createTemporaryWavForExport(laneIndex);
        if (tempWav.existsAsFile())
        {
            juce::StringArray files;
            files.add(tempWav.getFullPathName());

            // DAW（外部プロセス）が100%オーディオファイルをコピー認識するJUCE公式API
            juce::DragAndDropContainer::performExternalDragDropOfFiles(files, false, this);
        }
        return;
    }

    if (internalBuffer.getNumSamples() == 0) return;

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

    if (auto* editor = juce::Component::findParentComponentOfClass<AnatomyAudioProcessorEditor>())
    {
        editor->changeListenerCallback(nullptr);
    }
    repaint();
}