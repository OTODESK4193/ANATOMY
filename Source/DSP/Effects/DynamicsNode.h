#pragma once
#include <juce_dsp/juce_dsp.h>
#include <immintrin.h>
#include <cmath>
#include <algorithm>
#include "FastMath.h"

// ============================================================================
//  AVX2 前提チェック
//  process() は simdData を __m256 (float×8) の配列として再解釈します。
//  JUCE が SSE ビルド (float×4 = 16byte) になっていると、ストライドが半分に
//  なりバッファオーバーランと完全な誤処理を起こすため、ここで必ず止めます。
// ============================================================================
static_assert (sizeof (juce::dsp::SIMDRegister<float>) == 32,
               "MULTI-OTO requires an 8-wide (AVX2) juce::dsp::SIMDRegister<float>. "
               "Check that /arch:AVX2 is enabled and JUCE is building its AVX path.");
static_assert (alignof (juce::dsp::SIMDRegister<float>) == 32,
               "SIMDRegister<float> must be 32-byte aligned. std::vector uses aligned "
               "operator new for over-aligned types only from C++17 onwards.");

class DynamicsNode {
public:
    // 全ノードが float の生存範囲内に収まるための安全天井 (+120 dBFS)
    // 音は思い切り暴れさせるが、Inf/NaN に落ちて復帰不能になることだけは防ぐ。
    static constexpr float kSafeCeiling = 1.0e6f;

    // ------------------------------------------------------------------
    //  Coeffs — 1ブロックにつき1回だけ計算し、全ノードへ配る係数セット
    //  (従来は 128 ノード分ループして exp/pow を毎回叩いていた)
    // ------------------------------------------------------------------
    struct alignas(32) Coeffs {
        __m256 atk, rel, up, down, gain, mix;
    };

    static Coeffs computeCoeffs (double sampleRate,
                                 const float* gains, const float* depths,
                                 const float* upWards, const float* downWards,
                                 float macroTime,
                                 const float* attacks, const float* releases,
                                 float mix)
    {
        const float timeScale = macroTime * 0.01f;
        const float sr = static_cast<float> (sampleRate);

        alignas(32) float aArr[8]  = { 1, 1, 1, 1, 1, 1, 1, 1 };
        alignas(32) float rArr[8]  = { 1, 1, 1, 1, 1, 1, 1, 1 };
        alignas(32) float upArr[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        alignas(32) float dnArr[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        alignas(32) float gArr[8]  = { 1, 1, 1, 1, 1, 1, 1, 1 };

        for (int i = 0; i < 3; ++i)
        {
            const float atkMs = std::max (0.1f, attacks[i]  * timeScale);
            const float relMs = std::max (1.0f, releases[i] * timeScale);
            const float aC = 1.0f - std::exp (-1.0f / (atkMs * 0.001f * sr));
            const float rC = 1.0f - std::exp (-1.0f / (relMs * 0.001f * sr));
            aArr[i * 2] = aArr[i * 2 + 1] = aC;
            rArr[i * 2] = rArr[i * 2 + 1] = rC;

            const float dVal = depths[i] * 0.01f;
            upArr[i * 2] = upArr[i * 2 + 1] = dVal * (upWards[i]   * 0.01f);
            dnArr[i * 2] = dnArr[i * 2 + 1] = dVal * (downWards[i] * 0.01f);

            const float gVal = std::pow (10.0f, gains[i] / 20.0f);
            gArr[i * 2] = gArr[i * 2 + 1] = gVal;
        }

        Coeffs c;
        c.atk  = _mm256_load_ps (aArr);
        c.rel  = _mm256_load_ps (rArr);
        c.up   = _mm256_load_ps (upArr);
        c.down = _mm256_load_ps (dnArr);
        c.gain = _mm256_load_ps (gArr);
        c.mix  = _mm256_set1_ps (mix * 0.01f);
        return c;
    }

    // ------------------------------------------------------------------
    DynamicsNode() { reset(); }

    void prepare (double sr, int samplesPerBlock)
    {
        juce::ignoreUnused (samplesPerBlock);
        currentSampleRate = sr;

        // 20ms のパラメータスムージング。
        // 128 段直列だと 0.1dB のステップでも合計 12.8dB の段差になり、
        // 巨大なインパルス → float オーバーフロー → 無音バグの引き金になる。
        smoothCoef = 1.0f - std::exp (-1.0f / (0.020f * static_cast<float> (sr)));

        reset();
    }

    void reset()
    {
        v_envSq = _mm256_setzero_ps();
        v_atk   = _mm256_set1_ps (1.0f);
        v_rel   = _mm256_set1_ps (1.0f);

        v_up_depth   = v_up_target   = _mm256_setzero_ps();
        v_down_depth = v_down_target = _mm256_setzero_ps();
        v_gain       = v_gain_target = _mm256_set1_ps (1.0f);
        v_mix        = v_mix_target  = _mm256_set1_ps (1.0f);

        needsSnap = true;
    }

    /** 目標値をセット。reset 直後だけはスムージングせず即座に反映する。 */
    void applyCoeffs (const Coeffs& c)
    {
        v_atk = c.atk;
        v_rel = c.rel;

        v_up_target   = c.up;
        v_down_target = c.down;
        v_gain_target = c.gain;
        v_mix_target  = c.mix;

        if (needsSnap)
        {
            v_up_depth   = v_up_target;
            v_down_depth = v_down_target;
            v_gain       = v_gain_target;
            v_mix        = v_mix_target;
            needsSnap    = false;
        }
    }

    void process (juce::dsp::SIMDRegister<float>* simdData, int numSamples)
    {
        const __m256 db_scaler     = _mm256_set1_ps (6.0205f);
        const __m256 linear_scaler = _mm256_set1_ps (0.16609f);

        const __m256 down_thresh  = _mm256_set1_ps (-15.0f);
        const __m256 up_thresh    = _mm256_set1_ps (-40.0f);
        const __m256 down_ratio_m = _mm256_set1_ps (-(1.0f - 0.25f)); // 1:4 Ratio
        const __m256 up_ratio_m   = _mm256_set1_ps (0.5f);            // 1:2 Ratio
        const __m256 max_up_gain  = _mm256_set1_ps (36.0f);           // Upward Range 制限

        const __m256 vSmooth = _mm256_set1_ps (smoothCoef);
        const __m256 kCeil   = _mm256_set1_ps (kSafeCeiling);
        const __m256 kFloor  = _mm256_set1_ps (-kSafeCeiling);
        const __m256 kEnvMax = _mm256_set1_ps (kSafeCeiling * kSafeCeiling);
        const __m256 kZero   = _mm256_setzero_ps();
        const __m256 kTiny   = _mm256_set1_ps (1e-15f);

        __m256* buffer = reinterpret_cast<__m256*> (simdData);

        for (int i = 0; i < numSamples; ++i)
        {
            // --- パラメータスムージング (ノブ操作時のステップを潰す) ---
            v_gain       = _mm256_add_ps (v_gain,       _mm256_mul_ps (vSmooth, _mm256_sub_ps (v_gain_target,   v_gain)));
            v_mix        = _mm256_add_ps (v_mix,        _mm256_mul_ps (vSmooth, _mm256_sub_ps (v_mix_target,    v_mix)));
            v_up_depth   = _mm256_add_ps (v_up_depth,   _mm256_mul_ps (vSmooth, _mm256_sub_ps (v_up_target,     v_up_depth)));
            v_down_depth = _mm256_add_ps (v_down_depth, _mm256_mul_ps (vSmooth, _mm256_sub_ps (v_down_target,   v_down_depth)));

            // --- 入力サニタイズ ---
            // _CMP_ORD_Q は NaN でない要素にだけビットを立てる。AND で NaN を 0 にする。
            __m256 input = buffer[i];
            input = _mm256_and_ps (input, _mm256_cmp_ps (input, input, _CMP_ORD_Q));
            input = _mm256_max_ps (_mm256_min_ps (input, kCeil), kFloor);

            // --- RMS エンベロープ ---
            __m256 inSq     = _mm256_mul_ps (input, input);
            __m256 isAttack = _mm256_cmp_ps (inSq, v_envSq, _CMP_GT_OQ);
            __m256 coeff    = _mm256_blendv_ps (v_rel, v_atk, isAttack);

            v_envSq = _mm256_add_ps (v_envSq, _mm256_mul_ps (coeff, _mm256_sub_ps (inSq, v_envSq)));

            // envSq が Inf になると次のフレームで Inf - Inf = NaN になり、
            // 以降このノードは永久に NaN を吐き続ける (旧・無音バグの本体)。
            v_envSq = _mm256_and_ps (v_envSq, _mm256_cmp_ps (v_envSq, v_envSq, _CMP_ORD_Q));
            v_envSq = _mm256_max_ps (_mm256_min_ps (v_envSq, kEnvMax), kZero);

            __m256 envRms = _mm256_sqrt_ps (_mm256_max_ps (v_envSq, kTiny));
            __m256 envDb  = _mm256_mul_ps (FastMath::fast_log2_256 (envRms), db_scaler);

            // --- Downward (大音量圧縮) ---
            __m256 overThresh = _mm256_max_ps (kZero, _mm256_sub_ps (envDb, down_thresh));
            __m256 downGainDb = _mm256_mul_ps (_mm256_mul_ps (overThresh, down_ratio_m), v_down_depth);

            // --- Upward (弱音引き上げ) ---
            __m256 underThresh = _mm256_max_ps (kZero, _mm256_sub_ps (up_thresh, envDb));
            __m256 cappedUp    = _mm256_min_ps (_mm256_mul_ps (underThresh, up_ratio_m), max_up_gain);
            __m256 upGainDb    = _mm256_mul_ps (cappedUp, v_up_depth);

            // --- Smart Low-Level Gate (ANATOMYドラム特化: gateFloorDb 以下のフロアノイズ増幅抑制) ---
            __m256 gateThresh  = _mm256_set1_ps (gateFloorDb);
            __m256 gateBottom  = _mm256_sub_ps (gateThresh, _mm256_set1_ps (9.0f));
            __m256 gateDiff    = _mm256_max_ps (kZero, _mm256_sub_ps (envDb, gateBottom));
            __m256 gateFactor  = _mm256_min_ps (_mm256_set1_ps (1.0f), _mm256_mul_ps (gateDiff, _mm256_set1_ps (1.0f / 9.0f)));
            upGainDb           = _mm256_mul_ps (upGainDb, gateFactor);

            __m256 totalGainDb = _mm256_add_ps (downGainDb, upGainDb);
            __m256 linearGain  = _mm256_mul_ps (FastMath::fast_exp2_256 (_mm256_mul_ps (totalGainDb, linear_scaler)), v_gain);

            __m256 wet = _mm256_mul_ps (input, linearGain);
            __m256 out = _mm256_add_ps (input, _mm256_mul_ps (_mm256_sub_ps (wet, input), v_mix));

            // --- 出力サニタイズ (段間で暴走を閉じ込める) ---
            out = _mm256_and_ps (out, _mm256_cmp_ps (out, out, _CMP_ORD_Q));
            buffer[i] = _mm256_max_ps (_mm256_min_ps (out, kCeil), kFloor);
        }
    }

    /** 直近フレームのエンベロープ (dB) と適用ゲイン変化 (dB) を返す */
    void getLastMeter (float outEnvDb[3], float outGainDb[3]) const
    {
        alignas(32) float envSqRaw[8], upArr[8], dnArr[8], gArr[8];
        _mm256_store_ps (envSqRaw,  v_envSq);
        _mm256_store_ps (upArr,     v_up_depth);
        _mm256_store_ps (dnArr,     v_down_depth);
        _mm256_store_ps (gArr,      v_gain);

        for (int b = 0; b < 3; ++b)
        {
            const float rms = std::sqrt (std::max (envSqRaw[b * 2], 1e-15f));
            outEnvDb[b] = (rms > 1e-7f) ? (FastMath::fast_log2 (rms) * 6.0205f) : -60.0f;

            const float envDb    = outEnvDb[b];
            const float downOver = std::max (0.0f, envDb - (-15.0f));
            const float upUnder  = std::max (0.0f, (-40.0f) - envDb);

            const float downGain = downOver * (-(1.0f - 0.25f)) * dnArr[b * 2];
            const float upGain   = std::min (upUnder * 0.5f, 36.0f) * upArr[b * 2];
            const float totalDb  = downGain + upGain;

            const float bandGainDb = (gArr[b * 2] > 0.001f) ? (20.0f * std::log10 (gArr[b * 2])) : 0.0f;
            outGainDb[b] = totalDb + bandGainDb;
        }
    }

    void setGateFloorDb (float db) noexcept { gateFloorDb = juce::jlimit (-70.0f, -20.0f, db); }
    float getGateFloorDb() const noexcept    { return gateFloorDb; }

private:
    double currentSampleRate = 44100.0;
    float  smoothCoef        = 0.001f;
    float  gateFloorDb       = -45.0f;
    bool   needsSnap         = true;

    __m256 v_envSq, v_atk, v_rel;
    __m256 v_up_depth,   v_up_target;
    __m256 v_down_depth, v_down_target;
    __m256 v_gain,       v_gain_target;
    __m256 v_mix,        v_mix_target;
};
