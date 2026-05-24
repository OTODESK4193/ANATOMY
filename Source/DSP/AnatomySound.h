#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "SharedSampleData.h"

class AnatomySound : public juce::SynthesiserSound
{
public:
    AnatomySound() : currentData(nullptr) {}
    ~AnatomySound() override = default;

    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }

    // スマートポインタの安全なアトミック差し替え
    void updateSampleData(SharedSampleData::Ptr newData)
    {
        const juce::ScopedLock sl(lock);
        currentData = newData;
    }

    SharedSampleData::Ptr getSampleData() const noexcept
    {
        const juce::ScopedLock sl(lock);
        return currentData;
    }

private:
    juce::CriticalSection lock;
    SharedSampleData::Ptr currentData; // 参照カウントポインタによる自動寿命管理
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnatomySound)
};