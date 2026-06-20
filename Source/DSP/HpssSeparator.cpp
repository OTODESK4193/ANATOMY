#include "HpssSeparator.h"
#include <cmath>
#include <algorithm>

// ══════════════════════════════════════════════════════════════════════════════
// cos² クロスフェード分離エンジン
//
// 設計原理:
//   holdMs 区間 → wClick = 1.0 (トランジェント 100%)
//   fadeMs 区間 → wClick = cos²(θ) でスムーズにフェードアウト
//                 (1 - wClick) = sin²(θ) でトーナルがフェードイン
//   それ以降   → wClick = 0.0 (トーナル 100%)
//
// cos²(θ) + sin²(θ) = 1 により:
//   trans + tonal = input * wClick + input * (1 - wClick) = input
//   → パーフェクトリコンストラクションが数学的に保証される。
//
// 処理はタイムドメインのみ（FFT不使用）で、
// サンプル数に対して O(n) の線形時間で完了する。
// ══════════════════════════════════════════════════════════════════════════════

void HpssSeparator::performSeparation(
    const juce::AudioBuffer<float>& input,
    juce::AudioBuffer<float>& trans,
    juce::AudioBuffer<float>& tonal,
    float clickHoldMs,
    float sustainFadeMs,
    juce::Thread* callingThread)
{
    progress.store(0.0f);
    const int numSamples = input.getNumSamples();
    const int numChannels = input.getNumChannels();

    // ステレオ対応: 入力チャンネル数を保持して分離バッファを生成
    trans.setSize(numChannels, numSamples, false, false, true);
    tonal.setSize(numChannels, numSamples, false, false, true);
    trans.clear();
    tonal.clear();

    if (numSamples <= 0)
    {
        progress.store(1.0f);
        return;
    }

    // ── hold / fade 区間をサンプル数に変換 ──────────────────────────────
    const double sr = (currentSampleRate > 0.0) ? currentSampleRate : 44100.0;
    const int holdSamples = static_cast<int>((clickHoldMs / 1000.0) * sr);
    const int fadeSamples = std::max(1, static_cast<int>((sustainFadeMs / 1000.0) * sr));
    const int fadeEnd     = holdSamples + fadeSamples;

    // ── サンプルごとの分離処理（全チャンネル独立） ──────────────────────
    for (int i = 0; i < numSamples; ++i)
    {
        // スレッド中断チェック（4096サンプルごと = 軽量）
        if ((i & 0xFFF) == 0 && callingThread != nullptr && callingThread->threadShouldExit())
            return;

        if (i < holdSamples)
        {
            // Hold 区間: トランジェント 100%
            for (int ch = 0; ch < numChannels; ++ch)
                trans.setSample(ch, i, input.getSample(ch, i));
        }
        else if (i < fadeEnd)
        {
            // Fade 区間: cos² クロスフェード
            const float progress01 = static_cast<float>(i - holdSamples) / static_cast<float>(fadeSamples);
            const float angle = progress01 * juce::MathConstants<float>::halfPi;
            const float wClick = std::cos(angle);
            const float wClickSq = wClick * wClick;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float sample = input.getSample(ch, i);
                trans.setSample(ch, i, sample * wClickSq);
                tonal.setSample(ch, i, sample * (1.0f - wClickSq));
            }
        }
        else
        {
            // Post-fade 区間: トーナル 100%
            for (int ch = 0; ch < numChannels; ++ch)
                tonal.setSample(ch, i, input.getSample(ch, i));
        }

        // プログレス更新（8192サンプルごと = UI負荷を最小化）
        if ((i & 0x1FFF) == 0)
            progress.store(static_cast<float>(i) / static_cast<float>(numSamples));
    }

    progress.store(1.0f);
}
