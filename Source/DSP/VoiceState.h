#pragma once
#include "SharedSampleData.h"
#include "GranularPitchShifter.h"
#include <memory>

struct VoiceState
{
    const SharedSampleData* sampleData = nullptr;
    double clickReadIndex = 0.0;
    double sustainReadIndex = 0.0;
    double pitchRatio = 1.0;
    float triggerVelocity = 0.0f;
    bool isActive = false;
    bool isReleasing = false;
    float releaseGain = 1.0f;
    int currentMidiNote = -1;

    std::unique_ptr<GranularPitchShifter> transShifter;
    std::unique_ptr<GranularPitchShifter> tonalShifter;

    VoiceState()
    {
        transShifter = std::make_unique<GranularPitchShifter>();
        tonalShifter = std::make_unique<GranularPitchShifter>();
        transShifter->init(44100.0, 10.0f, 4);
        tonalShifter->init(44100.0, 40.0f, 4);
    }

    ~VoiceState() = default;

    void reallocateShifters(double sampleRate)
    {
        if (transShifter) transShifter->init(sampleRate, 10.0f, 4);
        if (tonalShifter) tonalShifter->init(sampleRate, 40.0f, 4);
        resetProcessing();
    }

    void resetProcessing() noexcept
    {
        if (transShifter) transShifter->reset();
        if (tonalShifter) tonalShifter->reset();
    }

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
        resetProcessing();
    }

    VoiceState(const VoiceState&) = delete;
    VoiceState& operator=(const VoiceState&) = delete;
    VoiceState(VoiceState&&) = default;
    VoiceState& operator=(VoiceState&&) = default;
};