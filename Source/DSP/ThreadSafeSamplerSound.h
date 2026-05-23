#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>
#include "SharedSampleData.h"

class ThreadSafeSamplerSound : public juce::SynthesiserSound
{
public:
    ThreadSafeSamplerSound() : currentData(nullptr) {}
    ~ThreadSafeSamplerSound() override = default;

    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }

    void updateSampleData(std::shared_ptr<SharedSampleData> newData)
    {
        currentData.store(newData, std::memory_order_release);
    }

    std::shared_ptr<SharedSampleData> getSampleData() const noexcept
    {
        return currentData.load(std::memory_order_acquire);
    }

private:
    std::atomic<std::shared_ptr<SharedSampleData>> currentData;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThreadSafeSamplerSound)
};