#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

class SharedSampleData : public juce::ReferenceCountedObject
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<SharedSampleData>;

    SharedSampleData(juce::AudioBuffer<float>&& clickBufferToUse,
        juce::AudioBuffer<float>&& sustainBufferToUse,
        juce::AudioBuffer<float>&& layerBufferToUse,
        double sampleRate)
        : clickBuffer(std::move(clickBufferToUse)),
        sustainBuffer(std::move(sustainBufferToUse)),
        layerBuffer(std::move(layerBufferToUse)),
        originalSampleRate(sampleRate)
    {
    }

    ~SharedSampleData() override = default;

    const juce::AudioBuffer<float>& getClickBuffer() const noexcept { return clickBuffer; }
    const juce::AudioBuffer<float>& getSustainBuffer() const noexcept { return sustainBuffer; }
    const juce::AudioBuffer<float>& getLayerBuffer() const noexcept { return layerBuffer; }
    double getSampleRate() const noexcept { return originalSampleRate; }

private:
    juce::AudioBuffer<float> clickBuffer;
    juce::AudioBuffer<float> sustainBuffer;
    juce::AudioBuffer<float> layerBuffer;
    double originalSampleRate;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SharedSampleData)
};