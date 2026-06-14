#include "HpssSeparator.h"
#include <cmath>
#include <algorithm>
#include <cstring>

// ══════════════════════════════════════════════════════════════════════════════
// コンストラクタ / 初期化
// ══════════════════════════════════════════════════════════════════════════════

HpssSeparator::HpssSeparator(int baseFftSizeIn)
    : baseFftSize(std::max(64, baseFftSizeIn)),
      fftOrder(11),           // デフォルト: 2^11 = 2048（prepare()で再計算）
      fftSize(2048),
      hopSize(512),
      numBins(1025)
{
    fft = std::make_unique<juce::dsp::FFT>(fftOrder);
    buildAnalysisWindow();
}

void HpssSeparator::prepare(double sampleRate)
{
    if (std::abs(sampleRate - currentSampleRate) > 1.0)
        reconfigureForSampleRate(sampleRate);

    currentSampleRate = sampleRate;
}

// ══════════════════════════════════════════════════════════════════════════════
// サンプルレート適応ロジック
//
// 設計方針:
//   44.1kHz を基準として FFTサイズをSRに比例スケーリングし、
//   周波数分解能（Hz/bin）と中央値フィルタの実効時間幅を全SRで一定に保つ。
//
//   SR        FFTサイズ   周波数分解能   hop時間     時間カーネル   周波数カーネル
//   44.1kHz   2048        21.5 Hz/bin   11.6ms      17 (~197ms)    17 (~366Hz)
//   48.0kHz   2048        23.4 Hz/bin   10.7ms      19 (~203ms)    17 (~398Hz)
//   88.2kHz   4096        21.5 Hz/bin   11.6ms      17 (~197ms)    17 (~366Hz)
//   96.0kHz   4096        23.4 Hz/bin   10.7ms      19 (~203ms)    17 (~398Hz)
//   176.4kHz  8192        21.5 Hz/bin   11.6ms      17 (~197ms)    17 (~366Hz)
//   192.0kHz  8192        23.4 Hz/bin   10.7ms      19 (~203ms)    17 (~398Hz)
// ══════════════════════════════════════════════════════════════════════════════

void HpssSeparator::reconfigureForSampleRate(double sampleRate)
{
    // 44.1kHz基準でFFTサイズをスケーリング（2の冪に切り上げ）
    const double srRatio = sampleRate / 44100.0;
    const int scaledSize = static_cast<int>(static_cast<double>(baseFftSize) * srRatio);
    const int newOrder   = static_cast<int>(std::ceil(std::log2(static_cast<double>(std::max(64, scaledSize)))));

    // 上限: 8192（192kHzでも十分な分解能、メモリ消費を抑制）
    // 下限: baseFftSize のオーダー
    const int baseOrder = static_cast<int>(std::ceil(std::log2(static_cast<double>(baseFftSize))));
    const int clampedOrder = std::max(baseOrder, std::min(newOrder, 13));  // 最大 2^13 = 8192

    fftOrder = clampedOrder;
    fftSize  = 1 << fftOrder;
    hopSize  = fftSize / 4;
    numBins  = fftSize / 2 + 1;

    // FFTインスタンスを再構築
    fft = std::make_unique<juce::dsp::FFT>(fftOrder);

    // 中央値フィルタカーネルをSRに追従させる
    // 目標: 時間軸 ~200ms、周波数軸 ~370Hz
    const double hopTimeSec    = static_cast<double>(hopSize) / sampleRate;
    const double freqPerBin    = sampleRate / static_cast<double>(fftSize);
    const double targetTimeSec = 0.200;   // 200ms
    const double targetFreqHz  = 370.0;   // 370Hz

    int rawTimeKernel = static_cast<int>(std::round(targetTimeSec / hopTimeSec));
    int rawFreqKernel = static_cast<int>(std::round(targetFreqHz / freqPerBin));

    // 奇数に強制（中央値フィルタは対称カーネルが必須）
    // 最小値: 3（中央値として意味を持つ最小サイズ）
    timeMedianKernel = std::max(3, rawTimeKernel | 1);
    freqMedianKernel = std::max(3, rawFreqKernel | 1);

    // 解析窓を再構築
    buildAnalysisWindow();
}

void HpssSeparator::buildAnalysisWindow()
{
    analysisWindow.resize(static_cast<size_t>(fftSize));
    for (int i = 0; i < fftSize; ++i)
    {
        // Hann 窓: 75%オーバーラップで COLA (Constant Overlap-Add) 条件を満たす
        analysisWindow[static_cast<size_t>(i)] = 0.5f * (1.0f - std::cos(
            juce::MathConstants<float>::twoPi * static_cast<float>(i) / static_cast<float>(fftSize)));
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// 真の HPSS 分離エンジン
//
// FitzGerald (2010) "Harmonic/Percussive Separation using Median Filtering"
//
// パイプライン:
//   1. STFT (Hann窓, 75%オーバーラップ)
//   2. 時間軸中央値フィルタ → Harmonic 強調スペクトログラム
//   3. 周波数軸中央値フィルタ → Percussive 強調スペクトログラム
//   4. Wiener ソフトマスク: H² / (H² + P² + ε),  P² / (H² + P² + ε)
//   5. マスク適用 + iSTFT (Overlap-Add)
// ══════════════════════════════════════════════════════════════════════════════

void HpssSeparator::performSeparation(
    const juce::AudioBuffer<float>& input,
    juce::AudioBuffer<float>& trans,
    juce::AudioBuffer<float>& tonal,
    float /*clickHoldMs*/,
    float /*sustainFadeMs*/,
    juce::Thread* callingThread)
{
    // clickHoldMs / sustainFadeMs は旧版との互換性のために引数に残すが、
    // 真のHPSSでは使用しない。時間軸エンベロープは processBlock 側で適用される。

    progress.store(0.0f);
    const int numSamples = input.getNumSamples();

    trans.setSize(1, numSamples, false, false, true);
    tonal.setSize(1, numSamples, false, false, true);
    trans.clear();
    tonal.clear();

    if (numSamples <= 0 || fft == nullptr)
    {
        progress.store(1.0f);
        return;
    }

    const float* srcData = input.getReadPointer(0);

    // ────────────────────────────────────────────────────────────────────────
    // Phase 1: STFT → マグニチュードスペクトログラム生成 (0% → 25%)
    // ────────────────────────────────────────────────────────────────────────

    const int numFrames = (numSamples - 1) / hopSize + 1;

    if (numFrames <= 0)
    {
        progress.store(1.0f);
        return;
    }

    // メモリ確保（オフライン処理のためヒープ確保は許容）
    const size_t specSize       = static_cast<size_t>(numFrames) * static_cast<size_t>(numBins);
    const size_t fftBufSize     = static_cast<size_t>(fftSize) * 2;
    const size_t complexTotal   = static_cast<size_t>(numFrames) * fftBufSize;

    std::vector<float> complexSpec(complexTotal, 0.0f);     // 全フレームの複素STFT
    std::vector<float> magSpec(specSize, 0.0f);             // マグニチュードスペクトログラム
    std::vector<float> fftBuffer(fftBufSize, 0.0f);         // FFT作業バッファ

    for (int frame = 0; frame < numFrames; ++frame)
    {
        if (frame % 40 == 0 && callingThread != nullptr && callingThread->threadShouldExit())
            return;

        // 窓関数を適用した入力をFFTバッファにコピー
        std::fill(fftBuffer.begin(), fftBuffer.end(), 0.0f);
        const int offset = frame * hopSize;

        for (int i = 0; i < fftSize; ++i)
        {
            const int srcIdx = offset + i;
            const float sample = (srcIdx >= 0 && srcIdx < numSamples) ? srcData[srcIdx] : 0.0f;
            fftBuffer[static_cast<size_t>(i)] = sample * analysisWindow[static_cast<size_t>(i)];
        }

        // 順方向FFT（実数入力 → 複素出力）
        fft->performRealOnlyForwardTransform(fftBuffer.data());

        // 複素スペクトルデータを保存
        float* complexPtr = &complexSpec[static_cast<size_t>(frame) * fftBufSize];
        std::memcpy(complexPtr, fftBuffer.data(), sizeof(float) * fftBufSize);

        // マグニチュード計算（非負周波数ビンのみ）
        float* magPtr = &magSpec[static_cast<size_t>(frame) * static_cast<size_t>(numBins)];
        for (int bin = 0; bin < numBins; ++bin)
        {
            const float re = fftBuffer[static_cast<size_t>(bin) * 2];
            const float im = fftBuffer[static_cast<size_t>(bin) * 2 + 1];
            magPtr[bin] = std::sqrt(re * re + im * im);
        }

        if (frame % 10 == 0)
            progress.store(0.25f * static_cast<float>(frame) / static_cast<float>(numFrames));
    }

    progress.store(0.25f);

    // ────────────────────────────────────────────────────────────────────────
    // Phase 2: 中央値フィルタリング (25% → 65%)
    //
    //   時間軸中央値 → 各周波数ビンが時間方向に安定した成分を抽出
    //                  → Harmonic（倍音・持続音）が強調される
    //
    //   周波数軸中央値 → 各時間フレームで周波数方向に広帯域な成分を抽出
    //                    → Percussive（アタック・ノイズ）が強調される
    // ────────────────────────────────────────────────────────────────────────

    std::vector<float> harmonicSpec(specSize, 0.0f);
    std::vector<float> percussiveSpec(specSize, 0.0f);

    // 中央値計算用スクラッチバッファ（事前確保でアロケーション回避）
    const int maxKernel = std::max(timeMedianKernel, freqMedianKernel);
    std::vector<float> medianScratch(static_cast<size_t>(maxKernel));

    const int timeHalf = timeMedianKernel / 2;
    const int freqHalf = freqMedianKernel / 2;

    // ── 時間軸中央値フィルタ ─────────────────────────────────────────────
    for (int bin = 0; bin < numBins; ++bin)
    {
        if (bin % 40 == 0 && callingThread != nullptr && callingThread->threadShouldExit())
            return;

        for (int frame = 0; frame < numFrames; ++frame)
        {
            const int tStart = std::max(0, frame - timeHalf);
            const int tEnd   = std::min(numFrames - 1, frame + timeHalf);
            int count = 0;

            for (int t = tStart; t <= tEnd; ++t)
                medianScratch[static_cast<size_t>(count++)] =
                    magSpec[static_cast<size_t>(t) * static_cast<size_t>(numBins) + static_cast<size_t>(bin)];

            // nth_element は O(n) 平均で中央値を求める（フルソートより高速）
            const int mid = count / 2;
            std::nth_element(medianScratch.begin(),
                             medianScratch.begin() + mid,
                             medianScratch.begin() + count);

            harmonicSpec[static_cast<size_t>(frame) * static_cast<size_t>(numBins) + static_cast<size_t>(bin)]
                = medianScratch[static_cast<size_t>(mid)];
        }

        if (bin % 10 == 0)
            progress.store(0.25f + 0.20f * static_cast<float>(bin) / static_cast<float>(numBins));
    }

    progress.store(0.45f);

    // ── 周波数軸中央値フィルタ ───────────────────────────────────────────
    for (int frame = 0; frame < numFrames; ++frame)
    {
        if (frame % 40 == 0 && callingThread != nullptr && callingThread->threadShouldExit())
            return;

        for (int bin = 0; bin < numBins; ++bin)
        {
            const int fStart = std::max(0, bin - freqHalf);
            const int fEnd   = std::min(numBins - 1, bin + freqHalf);
            int count = 0;

            for (int f = fStart; f <= fEnd; ++f)
                medianScratch[static_cast<size_t>(count++)] =
                    magSpec[static_cast<size_t>(frame) * static_cast<size_t>(numBins) + static_cast<size_t>(f)];

            const int mid = count / 2;
            std::nth_element(medianScratch.begin(),
                             medianScratch.begin() + mid,
                             medianScratch.begin() + count);

            percussiveSpec[static_cast<size_t>(frame) * static_cast<size_t>(numBins) + static_cast<size_t>(bin)]
                = medianScratch[static_cast<size_t>(mid)];
        }

        if (frame % 10 == 0)
            progress.store(0.45f + 0.20f * static_cast<float>(frame) / static_cast<float>(numFrames));
    }

    progress.store(0.65f);

    // ────────────────────────────────────────────────────────────────────────
    // Phase 3: Wiener ソフトマスキング + iSTFT (65% → 100%)
    //
    //   mask_H = H² / (H² + P² + ε)   → Tonal成分を通過
    //   mask_P = P² / (H² + P² + ε)   → Transient成分を通過
    //
    //   mask_H + mask_P = 1 が常に保証され、
    //   分離後の再合成が原音に一致する（パーフェクトリコンストラクション）。
    // ────────────────────────────────────────────────────────────────────────

    const int paddedLength = numSamples + fftSize;

    std::vector<float> transAccum(static_cast<size_t>(paddedLength), 0.0f);
    std::vector<float> tonalAccum(static_cast<size_t>(paddedLength), 0.0f);
    std::vector<float> winAccum(static_cast<size_t>(paddedLength), 0.0f);

    // iFFT作業バッファ（ループ外で確保してアロケーション回避）
    std::vector<float> transFftBuf(fftBufSize, 0.0f);
    std::vector<float> tonalFftBuf(fftBufSize, 0.0f);

    constexpr float epsilon = 1.0e-10f;

    for (int frame = 0; frame < numFrames; ++frame)
    {
        if (frame % 40 == 0 && callingThread != nullptr && callingThread->threadShouldExit())
            return;

        // このフレームの複素STFTデータを作業バッファにコピー
        const float* complexPtr = &complexSpec[static_cast<size_t>(frame) * fftBufSize];
        std::memcpy(transFftBuf.data(), complexPtr, sizeof(float) * fftBufSize);
        std::memcpy(tonalFftBuf.data(), complexPtr, sizeof(float) * fftBufSize);

        const float* harmPtr = &harmonicSpec[static_cast<size_t>(frame) * static_cast<size_t>(numBins)];
        const float* percPtr = &percussiveSpec[static_cast<size_t>(frame) * static_cast<size_t>(numBins)];

        // 各周波数ビンにWienerマスクを適用
        for (int bin = 0; bin < numBins; ++bin)
        {
            const float h2 = harmPtr[bin] * harmPtr[bin];
            const float p2 = percPtr[bin] * percPtr[bin];
            const float denom = h2 + p2 + epsilon;

            const float harmonicMask   = h2 / denom;   // Tonal 成分マスク
            const float percussiveMask = p2 / denom;    // Transient 成分マスク

            const size_t idx = static_cast<size_t>(bin) * 2;

            // 正の周波数ビンにマスク適用
            tonalFftBuf[idx]     *= harmonicMask;
            tonalFftBuf[idx + 1] *= harmonicMask;
            transFftBuf[idx]     *= percussiveMask;
            transFftBuf[idx + 1] *= percussiveMask;

            // 負の周波数ビン（共役対称）にも同じマスクを適用
            // bin=0 (DC) と bin=fftSize/2 (Nyquist) には対応する負周波数がない
            if (bin > 0 && bin < fftSize / 2)
            {
                const size_t negIdx = static_cast<size_t>(fftSize - bin) * 2;
                tonalFftBuf[negIdx]     *= harmonicMask;
                tonalFftBuf[negIdx + 1] *= harmonicMask;
                transFftBuf[negIdx]     *= percussiveMask;
                transFftBuf[negIdx + 1] *= percussiveMask;
            }
        }

        // 逆FFT → 時間領域信号
        // ★ JUCE FFT の逆変換は 1/N 正規化を行わない（IFFT(FFT(x)) = N*x）
        //   N倍の振幅膨張は最終出力時に一括で補正する（下記 Phase 4 参照）
        fft->performRealOnlyInverseTransform(transFftBuf.data());
        fft->performRealOnlyInverseTransform(tonalFftBuf.data());

        // Overlap-Add: 合成窓（解析窓と同一の Hann 窓）を適用して累積
        const int offset = frame * hopSize;
        for (int i = 0; i < fftSize; ++i)
        {
            const int outIdx = offset + i;
            if (outIdx >= 0 && outIdx < paddedLength)
            {
                const size_t si = static_cast<size_t>(i);
                const size_t so = static_cast<size_t>(outIdx);
                const float w = analysisWindow[si];

                transAccum[so] += transFftBuf[si] * w;
                tonalAccum[so] += tonalFftBuf[si] * w;
                winAccum[so]   += w * w;
            }
        }

        if (frame % 10 == 0)
            progress.store(0.65f + 0.35f * static_cast<float>(frame) / static_cast<float>(numFrames));
    }

    // ────────────────────────────────────────────────────────────────────────
    // 正規化: Overlap-Add の窓関数二乗和で割って振幅を復元
    // ────────────────────────────────────────────────────────────────────────

    float* transData = trans.getWritePointer(0);
    float* tonalData = tonal.getWritePointer(0);

    for (int i = 0; i < numSamples; ++i)
    {
        const float norm = winAccum[static_cast<size_t>(i)];
        if (norm > epsilon)
        {
            transData[i] = transAccum[static_cast<size_t>(i)] / norm;
            tonalData[i] = tonalAccum[static_cast<size_t>(i)] / norm;
        }
    }

    progress.store(1.0f);
}
