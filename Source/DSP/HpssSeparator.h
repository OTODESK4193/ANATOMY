#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_core/juce_core.h>
#include <atomic>
#include <vector>
#include <memory>

/**
 * HpssSeparator (Real HPSS Edition — SR-Adaptive)
 *
 * FitzGerald (2010) に基づく真の Harmonic-Percussive Source Separation。
 * STFT スペクトログラム上で中央値フィルタリングを行い、
 * Wiener ソフトマスキングで Harmonic（Tonal）と Percussive（Transient）を分離する。
 *
 * サンプルレート適応設計:
 *   - FFTサイズを SR に比例してスケーリング（44.1kHz=2048 基準）
 *     → 全SRで周波数分解能 ≒ 21.5 Hz/bin を維持
 *   - 中央値フィルタカーネルも SR に追従
 *     → 全SRで時間幅 ≒ 200ms / 周波数幅 ≒ 370Hz を維持
 *
 * 対応範囲: 44.1kHz 〜 192kHz（それ以外も安全にフォールバック）
 *
 * 公開インターフェースは旧版と100%互換。PluginProcessor側の変更は不要。
 */
class HpssSeparator
{
public:
    HpssSeparator(int baseFftSize);
    void prepare(double sampleRate);

    void performSeparation(const juce::AudioBuffer<float>& input,
        juce::AudioBuffer<float>& trans,
        juce::AudioBuffer<float>& tonal,
        float clickHoldMs,
        float sustainFadeMs,
        juce::Thread* callingThread);

    float getProgress() const { return progress.load(); }
    void resetProgress() { progress.store(0.0f); }

private:
    // ── 基準値（44.1kHz 時の設計値） ──────────────────────────────────────
    int baseFftSize;                        // コンストラクタ引数（44.1kHz基準）

    // ── 現在のSRに適応した実動値 ──────────────────────────────────────────
    int fftOrder;
    int fftSize;
    int hopSize;
    int numBins;                            // fftSize / 2 + 1
    std::unique_ptr<juce::dsp::FFT> fft;    // SRに応じて再構築

    int timeMedianKernel = 17;              // SR適応：~200ms相当
    int freqMedianKernel = 17;              // SR適応：~370Hz相当

    // ── 解析窓 ────────────────────────────────────────────────────────────
    std::vector<float> analysisWindow;
    void buildAnalysisWindow();

    // ── SR適応ロジック ────────────────────────────────────────────────────
    void reconfigureForSampleRate(double sampleRate);

    double currentSampleRate = 44100.0;
    std::atomic<float> progress{ 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HpssSeparator)
};
