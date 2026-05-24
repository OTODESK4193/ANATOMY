#pragma once
#include "SharedSampleData.h"

struct VoiceState
{
    SharedSampleData::Ptr sampleData{ nullptr };
    double clickReadIndex = 0.0;
    double sustainReadIndex = 0.0;
    double pitchRatio = 1.0;
    float triggerVelocity = 0.0f;
    bool isActive = false;
    bool isReleasing = false;
    float releaseGain = 1.0f;
    int currentMidiNote = -1;

    void reset() noexcept
    {
        sampleData = nullptr;
        clickReadIndex = 0.0;
        sustainReadIndex = 0.0;
        pitchRatio = 1.0;
        triggerVelocity = 0.0f;
        isActive = false;
        isReleasing = false;
        releaseGain = 1.0f;
        currentMidiNote = -1;
    }
};