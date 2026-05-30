#pragma once

#include "AudioEffect.h"
#include <juce_core/juce_core.h>
#include <vector>
#include <memory>
#include <atomic>

class EffectChainSnapshot final : public juce::ReferenceCountedObject
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<EffectChainSnapshot>;

    EffectChainSnapshot() = default;
    ~EffectChainSnapshot() override = default;

    std::vector<AudioEffect*> activeEffects;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectChainSnapshot)
};

class EffectChain final
{
public:
    EffectChain()
    {
        auto initialSnapshot = new EffectChainSnapshot();
        currentSnapshot.store(initialSnapshot, std::memory_order_release);
    }

    ~EffectChain()
    {
        if (auto* snapshot = currentSnapshot.exchange(nullptr, std::memory_order_acq_rel))
        {
            delete snapshot;
        }
    }

    void prepare(double sampleRate, int maxBlockSize)
    {
        if (auto* snapshot = currentSnapshot.load(std::memory_order_acquire))
        {
            for (auto* fx : snapshot->activeEffects)
            {
                if (fx != nullptr)
                    fx->prepare(sampleRate, maxBlockSize);
            }
        }
    }

    void reset() noexcept
    {
        if (auto* snapshot = currentSnapshot.load(std::memory_order_acquire))
        {
            for (auto* fx : snapshot->activeEffects)
            {
                if (fx != nullptr)
                    fx->reset();
            }
        }
    }

    void process(juce::AudioBuffer<float>& buffer) noexcept
    {
        auto* snapshot = currentSnapshot.load(std::memory_order_acquire);
        if (snapshot == nullptr) return;

        for (auto* fx : snapshot->activeEffects)
        {
            if (fx != nullptr && fx->isActive())
            {
                fx->process(buffer);
            }
        }
    }

    void updateChain(const std::vector<AudioEffect*>& newEffects,
        std::vector<EffectChainSnapshot*>& garbageBin)
    {
        auto newSnapshot = new EffectChainSnapshot();
        newSnapshot->activeEffects = newEffects;

        auto* oldSnapshot = currentSnapshot.exchange(newSnapshot, std::memory_order_acq_rel);
        if (oldSnapshot != nullptr)
        {
            garbageBin.push_back(oldSnapshot);
        }
    }

private:
    std::atomic<EffectChainSnapshot*> currentSnapshot{ nullptr };

    JUCE_DECLARE_NON_COPYABLE(EffectChain)
};