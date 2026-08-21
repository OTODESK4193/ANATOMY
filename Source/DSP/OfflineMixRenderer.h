#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "SharedSampleData.h"
#include <atomic>
#include <vector>

class AnatomyAudioProcessor;

/**
 * OfflineMixRenderer (Phase 3-4 Realtime Update Edition)
 * エフェクトやピッチ、トリミングに完全追従し、FullMixだけでなく、
 * 加工済みのTransientおよびTonalバッファも完全並列で脳内レンダリングするバックグラウンドコア。
 */
class OfflineMixRenderer final : public juce::Thread
{
public:
    OfflineMixRenderer(AnatomyAudioProcessor& p)
        : juce::Thread("AnatomyOfflineMixRendererThread"), processor(p)
    {
        triggerFlag.store(false, std::memory_order_release);
    }

    ~OfflineMixRenderer() override
    {
        signalThreadShouldExit();
        stopThread(3000);
    }

    void triggerRender() noexcept
    {
        triggerFlag.store(true, std::memory_order_release);
        notify();
    }

    void run() override
    {
        while (!threadShouldExit())
        {
            wait(-1);

            if (threadShouldExit()) break;

            if (triggerFlag.exchange(false, std::memory_order_acq_rel))
            {
                executeRender();
            }
        }
    }

    // 💥【仕様拡張】3レーンすべての加工済み（FX適用後）バッファを一括して安全に非同期取得
    void getRenderedResults(juce::AudioBuffer<float>& destFull,
        juce::AudioBuffer<float>& destTrans,
        juce::AudioBuffer<float>& destTonal,
        juce::AudioBuffer<float>& destLayer,
        std::vector<float>& destRatios)
    {
        const juce::ScopedLock sl(renderLock);
        destFull.makeCopyOf(renderedFullMix);
        destTrans.makeCopyOf(renderedTransient);
        destTonal.makeCopyOf(renderedTonal);
        destLayer.makeCopyOf(renderedLayer);
        destRatios = componentRatios;
    }

private:
    void executeRender();

    AnatomyAudioProcessor& processor;
    juce::CriticalSection renderLock;
    std::atomic<bool> triggerFlag;

    juce::AudioBuffer<float> renderedFullMix;
    juce::AudioBuffer<float> renderedTransient; 
    juce::AudioBuffer<float> renderedTonal;     
    juce::AudioBuffer<float> renderedLayer;     
    std::vector<float> componentRatios;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OfflineMixRenderer)
};