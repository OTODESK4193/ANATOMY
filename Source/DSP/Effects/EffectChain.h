#pragma once

#include "AudioEffect.h"
#include <juce_core/juce_core.h>
#include <vector>
#include <memory>
#include <atomic>

/**
 * EffectChainSnapshot
 * オーディオスレッドから「不変（Immutable）データ」として参照される
 * ポインタ配列を保持する、自動寿命管理用のコンテナ。
 */
class EffectChainSnapshot final : public juce::ReferenceCountedObject
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<EffectChainSnapshot>;

    EffectChainSnapshot() = default;
    ~EffectChainSnapshot() override = default;

    // オーディオスレッドのループから超高速かつ安全にインライン参照される生ポインタ配列
    std::vector<AudioEffect*> activeEffects;

    // 所有権を保持するマスター配列（メッセージスレッド側でのみ増減・移動を操作）
    std::vector<std::unique_ptr<AudioEffect>> ownedEffects;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectChainSnapshot)
};

/**
 * EffectChain
 * 独立した一つのシグナルチェインを管理するクラス。
 * 「Transient用」「Tonal用」「FullMix用」のそれぞれがこのインスタンスを持ちます。
 */
class EffectChain final
{
public:
    EffectChain()
    {
        // 初期状態の空スナップショットをアトミックに展開
        auto initialSnapshot = new EffectChainSnapshot();
        currentSnapshot.store(initialSnapshot, std::memory_order_release);
    }

    ~EffectChain()
    {
        // 保持されている最新スナップショットを安全に手動解放
        if (auto* snapshot = currentSnapshot.exchange(nullptr, std::memory_order_acq_rel))
        {
            delete snapshot;
        }
    }

    /**
     * 事前準備（メッセージスレッド側から一括駆動）
     */
    void prepare(double sampleRate, int maxBlockSize)
    {
        // 現在稼働中の最新ポインタを取得して準備
        if (auto* snapshot = currentSnapshot.load(std::memory_order_acquire))
        {
            for (auto* fx : snapshot->activeEffects)
            {
                if (fx != nullptr)
                    fx->prepare(sampleRate, maxBlockSize);
            }
        }
    }

    /**
     * チェイン履歴のクリア
     */
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

    /**
     * オーディオスレッド専用：ブロック処理メソッド
     * 核心制約3：ブロックの原点においてアトミックに不変参照を1度だけロードし、インライン直列実行。
     */
    void process(juce::AudioBuffer<float>& buffer) noexcept
    {
        // ブロック原点での不変（Immutable）参照取得。処理中のドラッグによる干渉を100%遮断
        auto* snapshot = currentSnapshot.load(std::memory_order_acquire);
        if (snapshot == nullptr) return;

        for (auto* fx : snapshot->activeEffects)
        {
            if (fx != nullptr)
            {
                fx->process(buffer);
            }
        }
    }

    /**
     * メッセージスレッド専用：エフェクト配列の動的アップデート関数
     * 新しいチェイン状態を構築してアトミックに交換し、古いチェインオブジェクトを遅延ゴミ箱へ送ります。
     * * @param newEffects メッセージスレッド側で並び替えや引越しが完了した新しい実体配列
     * @param garbageBin メッセージスレッドが管理する安全な遅延回収用ポインタコンテナ
     */
    void updateChain(std::vector<std::unique_ptr<AudioEffect>>&& newEffects,
        std::vector<EffectChainSnapshot*>& garbageBin)
    {
        auto newSnapshot = new EffectChainSnapshot();
        newSnapshot->ownedEffects = std::move(newEffects);

        // 高速ループ駆動のために生ポインタ配列を並列構築
        newSnapshot->activeEffects.reserve(newSnapshot->ownedEffects.size());
        for (const auto& fx : newSnapshot->ownedEffects)
        {
            newSnapshot->activeEffects.push_back(fx.get());
        }

        // ロックフリー生ポインタ交換
        auto* oldSnapshot = currentSnapshot.exchange(newSnapshot, std::memory_order_acq_rel);
        if (oldSnapshot != nullptr)
        {
            // オーディオスレッドでの即時deleteを完全バイパスし、ゴミ箱へ退避
            garbageBin.push_back(oldSnapshot);
        }
    }

private:
    std::atomic<EffectChainSnapshot*> currentSnapshot{ nullptr };

    JUCE_DECLARE_NON_COPYABLE(EffectChain)
};