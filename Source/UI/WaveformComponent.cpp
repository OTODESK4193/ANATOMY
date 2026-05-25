    #include "WaveformComponent.h"

    WaveformComponent::WaveformComponent() {}

    void WaveformComponent::paint(juce::Graphics& g)
    {
        g.fillAll(juce::Colours::black.withAlpha(0.3f));

        if (internalBuffer.getNumSamples() == 0 || getWidth() <= 0)
            return;

        auto w = (float)getWidth();
        auto h = (float)getHeight();
        auto mid = h * 0.5f;
        auto numSamples = internalBuffer.getNumSamples();
        const float* data = internalBuffer.getReadPointer(0);

        // パフォーマンスのためのダウンサンプリング描画
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
    }

    void WaveformComponent::setBuffer(const juce::AudioBuffer<float>& buffer)
    {
        internalBuffer.makeCopyOf(buffer);
        repaint();
    }