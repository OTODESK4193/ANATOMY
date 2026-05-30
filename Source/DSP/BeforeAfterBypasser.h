#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

/**
 * BeforeAfterBypasser
 * Before状態（生音バイパス）のON/OFF状態をアトミックに管理し、
 * processBlock内の走行ラインをオーディオコンテキストに負荷をかけずに一瞬でスイッチするクラス。
 */
class BeforeAfterBypasser final
{
public:
    BeforeAfterBypasser() : isBeforeActive(false) {}
    ~BeforeAfterBypasser() = default;

    void setBeforeStatus(bool shouldBypass) noexcept
    {
        isBeforeActive.store(shouldBypass, std::memory_order_release);
    }

    bool julesIsBeforeBypassed() const noexcept
    {
        return isBeforeActive.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> isBeforeActive;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BeforeAfterBypasser)
};