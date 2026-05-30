#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "SharedSampleData.h"
#include <atomic>
#include <vector>

class AnatomyAudioProcessor;

/**
 * OfflineMixRenderer
 * ユーザーのエディットに非同期追従し、バックグラウンドで超高速オフライン合成を行うスレッド。
 * 最終波形の「Transient / Tonal のエネルギー比率（2色色分け用）」も同時に数理生成する。
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

    // レンダリングされた最新のFullMixバッファと、色分け比率の配列を安全に取得
    void getRenderedResults(juce::AudioBuffer<float>& destBuffer, std::vector<float>& destRatios)
    {
        const juce::ScopedLock sl(renderLock);
        destBuffer.makeCopyOf(renderedFullMix);
        destRatios = componentRatios;
    }

private:
    void executeRender();

    AnatomyAudioProcessor& processor;
    juce::CriticalSection renderLock;
    std::atomic<bool> triggerFlag;

    juce::AudioBuffer<float> renderedFullMix;
    std::vector<float> componentRatios; // 💥2色色分け用：0.0(純Tonal)〜1.0(純Transient)の比率データ配列

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OfflineMixRenderer)
};