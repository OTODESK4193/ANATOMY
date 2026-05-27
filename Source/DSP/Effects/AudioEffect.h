#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

/**
 * TargetRoute
 * エフェクトの適用先を決定するルーティング定義
 */
enum class TargetRoute
{
    Transient,
    Tonal,
    FullMix
};

/**
 * AudioEffect
 * ANATOMY v2 全マルチエフェクトの抽象基底クラス。
 * オーディオスレッド内での動的メモリ確保を永久に排除するためのライフサイクルを定義します。
 */
class AudioEffect
{
public:
    virtual ~AudioEffect() = default;

    /**
     * サンプルレートやブロックサイズ変更時の事前バッファ確保
     */
    virtual void prepare(double sampleRate, int maxBlockSize) = 0;

    /**
     * エフェクト内部状態（ディレイ履歴、フィルターレジスタ等）の完全初期化
     */
    virtual void reset() noexcept = 0;

    /**
     * 純粋なDSP数理駆動ブロック処理（ゼロアロケーション、完全ゼロレイテンシー）
     */
    virtual void process(juce::AudioBuffer<float>& buffer) noexcept = 0;

    /**
     * エフェクトの識別名を取得
     */
    virtual juce::String getName() const = 0;

    /**
     * 現在設定されている宛先ルートの取得
     */
    virtual TargetRoute getTargetRoute() const noexcept = 0;

    /**
     * 宛先ルートの動的書き換え（メッセージスレッド側でのみ呼び出し）
     */
    virtual void setTargetRoute(TargetRoute newRoute) noexcept = 0;
};