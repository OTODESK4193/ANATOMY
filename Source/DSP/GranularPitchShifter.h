#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>
#include <algorithm>

/**
 * GranularPitchShifter (Snap-Engine Architecture)
 * * 1. Transient: 窓関数を完全に排除した「ワンショット・リサンプリング」で鋭さを100%維持
 * 2. Tonal: ポインタのワープを根絶した「中心基準型・連続位相走行シフター」で滑らかさを維持
 * 3. 接着剤: ノートONの瞬間に遅延を「完全ゼロ」に強制同期して一体感を偽装
 */
class GranularPitchShifter
{
    /**
     * Hermite 4点補間（3次）
     * 線形補間に比べ高周波のエイリアシングを大幅に低減。
     * 4点 (y[-1], y[0], y[1], y[2]) から t ∈ [0,1) の位置を補間。
     */
    static float hermiteInterp(float ym1, float y0, float y1, float y2, float t) noexcept
    {
        const float c0 = y0;
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }

    /**
     * ソースバッファからHermite補間で読み出し。
     * 境界はクランプで安全に処理。
     */
    static float readHermite(const float* src, int maxSamples, float srcPos) noexcept
    {
        if (srcPos < 0.0f) srcPos = 0.0f;
        if (srcPos >= static_cast<float>(maxSamples - 1)) srcPos = static_cast<float>(maxSamples - 1) - 0.0001f;

        int idx0 = static_cast<int>(srcPos);
        float frac = srcPos - static_cast<float>(idx0);

        int idxM1 = std::max(0, idx0 - 1);
        int idx1  = std::min(idx0 + 1, maxSamples - 1);
        int idx2  = std::min(idx0 + 2, maxSamples - 1);

        return hermiteInterp(src[idxM1], src[idx0], src[idx1], src[idx2], frac);
    }

public:
    GranularPitchShifter()
    {
        init(44100.0, 40.0f, 4);
    }

    ~GranularPitchShifter() = default;

    /**
     * アルゴリズムの初期化
     * @param grainSizeMs 20ms未満であれば自動的にTransient（ワンショット）モードとして動作します
     */
    void init(double sampleRate, float grainSizeMs, int /*numOverlaps*/)
    {
        this->currentSampleRate = sampleRate;

        // 20ms未満の短い設定（VoiceStateでの10ms）なら自動でTransientワンショットモードに設定
        this->isTransientMode = (grainSizeMs < 20.0f);

        // Tonal用の最大遅延幅（窓サイズ）の設定
        this->maxDelaySamples = static_cast<float>((grainSizeMs / 1000.0f) * sampleRate);
        if (this->maxDelaySamples < 64.0f) this->maxDelaySamples = 64.0f;

        reset();
    }

    /**
     * ノートON（トリガー）の瞬間に呼ばれる強制同期関数【接着剤】
     */
    void reset() noexcept
    {
        // 仮想再生ポインタの初期位相を「0.5」に強制アライメント
        // これにより、ノートONの瞬間は「遅延が完全にゼロ」の状態でTransientと100%同期してスタートします
        tapAPhase = 0.5f;
    }

    /**
     * タイムドメイン・ピッチシフト処理の実体
     */
    float processSample(const juce::AudioBuffer<float>& sourceBuffer, int currentTimelineIdx, float scaleFactor) noexcept
    {
        const int maxSamples = sourceBuffer.getNumSamples();
        if (maxSamples <= 0 || currentTimelineIdx < 0 || currentTimelineIdx >= maxSamples)
            return 0.0f;

        // ピッチ変更なし（1.0倍）の時は、一切の演算をバイパスして100%完全な同値原音を保証
        if (std::abs(scaleFactor - 1.0f) < 0.001f)
        {
            return sourceBuffer.getReadPointer(0)[currentTimelineIdx];
        }

        const float* src = sourceBuffer.getReadPointer(0);

        // ==============================================================================
        // 【MODE 1】Transient: 窓関数を通さない「ワンショット・リサンプリング」
        // ==============================================================================
        if (isTransientMode)
        {
            float srcPos = static_cast<float>(currentTimelineIdx) * scaleFactor;
            if (srcPos < 0.0f) srcPos = 0.0f;
            if (srcPos >= static_cast<float>(maxSamples - 1)) return 0.0f;

            return readHermite(src, maxSamples, srcPos);
        }

        // ==============================================================================
        // 【MODE 2】Tonal: ポインタをワープさせない「連続走行グラニュラー」
        // ==============================================================================
        // ピッチ比に基づいて、遅延の進捗（ノコギリ波の傾き）を完全に連続走行させる
        float phaseIncrement = (1.0f - scaleFactor) / maxDelaySamples;
        tapAPhase += phaseIncrement;

        // 位相を 0.0 〜 1.0 の間に滑らかにローテーション（ワープの根絶）
        while (tapAPhase >= 1.0f) tapAPhase -= 1.0f;
        while (tapAPhase < 0.0f)  tapAPhase += 1.0f;

        // もう一つの仮想ポインタ（Tap B）は常に180度反転した位置を追従
        float tapBPhase = tapAPhase + 0.5f;
        if (tapBPhase >= 1.0f) tapBPhase -= 1.0f;

        // 中心基準型ディ延数：未来と過去を対象に分配（レンジ： -maxDelay/2 〜 +maxDelay/2）
        float delayA = (tapAPhase - 0.5f) * maxDelaySamples;
        float delayB = (tapBPhase - 0.5f) * maxDelaySamples;

        // 窓関数（Hann Window）によるクロスフェード係数の算出
        auto getHannWeight = [](float phase) noexcept -> float
            {
                return 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * phase));
            };

        float weightA = getHannWeight(tapAPhase);
        float weightB = getHannWeight(tapBPhase);

        // 2つの波形位置からHermite 4点補間で高品質に読み出し
        float sampleA = readHermite(src, maxSamples, static_cast<float>(currentTimelineIdx) - delayA);
        float sampleB = readHermite(src, maxSamples, static_cast<float>(currentTimelineIdx) - delayB);

        // 重ね合わせ（Overlap-Add）して滑らかに出力
        return (sampleA * weightA) + (sampleB * weightB);
    }

private:
    double currentSampleRate = 44100.0;
    float maxDelaySamples = 1024.0f;
    float tapAPhase = 0.5f;
    bool isTransientMode = false;
};