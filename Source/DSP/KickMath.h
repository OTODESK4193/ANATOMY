#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cmath>

// ============================================================================
//  KickMath  -  状態を持たない純粋な数学関数の集約
//
//  「入力を与えると必ず同じ結果を返す」関数だけを集めています（内部状態なし）。
//  どこからでも安全に再利用でき、単体テストもしやすいのが利点です。
//
//  ※ ここに入れて良いもの : Sine 近似 / PolyBLEP / サチュレーションの原始関数 など
//  ※ ここに入れてはダメなもの: 乱数・フィルタ・位相など「前の値を覚える」処理
// ============================================================================
namespace ngk
{
    // -- 高精度サイン波（テイラー級数による近似。位相 0..2π を想定）--
    inline double ultraPureSine(double phase) noexcept
    {
        double x = phase - juce::MathConstants<double>::pi;
        if (x < -juce::MathConstants<double>::pi)      x += juce::MathConstants<double>::twoPi;
        else if (x > juce::MathConstants<double>::pi)  x -= juce::MathConstants<double>::twoPi;
        const double x2 = x * x;
        return x * (1.0 - x2 * (0.1666666667 - x2 * (0.0083333333 - x2 * (0.0001984127 - x2 * (0.0000027557 - x2 * 0.0000000209)))));
    }

    // -- PolyBLEP（矩形波/ノコギリ波の段差を滑らかにしエイリアスを抑える）--
    inline double polyBlep(double t, double dt) noexcept
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
        return 0.0;
    }

    // -- PolyBLAMP（傾きの折れ＝三角波の角を滑らかにしエイリアスを抑える）--
    inline double polyBlamp(double t, double dt) noexcept
    {
        if (t < dt) { double x = t / dt - 1.0; return -1.0 / 3.0 * x * x * x; }
        if (t > 1.0 - dt) { double x = (t - 1.0) / dt + 1.0; return 1.0 / 3.0 * x * x * x; }
        return 0.0;
    }

    // -- Attack/Body 共通ピッチ: 周波数(Hz) ⇔ 正規化値(0..1) の対数マッピング --
    //    D#0(MIDI15≒19.45Hz) .. A#9(MIDI130≒14948Hz) を 0..1 に対応。
    //    全レイヤーで MIDI 基準(A4=440)に揃うので C3 等の音名がレイヤー間で一致する。
    //    範囲 = (130-15)/12 = 9.583333 オクターブ、下限 = 440*2^(-54/12)。
    static constexpr float kPitchLoHz   = 19.44544f;  // D#0
    static constexpr float kPitchOctRng = 9.583333f;  // (130-15)/12

    inline float bodyHzToNorm(float hz) noexcept
    {
        return juce::jlimit(0.0f, 1.0f, std::log2(juce::jmax(1.0f, hz) / kPitchLoHz) / kPitchOctRng);
    }
    inline float bodyNormToHz(float v) noexcept
    {
        return kPitchLoHz * std::pow(2.0f, juce::jlimit(0.0f, 1.0f, v) * kPitchOctRng);
    }

    // Attack も Body と同一マッピング（音名を完全一致させるため）
    inline float atkHzToNorm(float hz) noexcept { return bodyHzToNorm(hz); }
    inline float atkNormToHz(float v)  noexcept { return bodyNormToHz(v); }

    // -- ADAA（アンチエイリアス・サチュレーション）の原始関数 --
    inline float calcADAAFunc(float x, int type) noexcept
    {
        switch (type)
        {
        case 0: // Soft Tanh
            if (std::abs(x) > 10.0f) return std::abs(x) - 0.693147f;
            return std::log(std::cosh(x));

        case 1: // Hard Clip
            if (x < -1.0f) return -x - 0.5f;
            if (x > 1.0f)  return  x - 0.5f;
            return 0.5f * x * x;

        case 6: // BJT (Atan based)
        {
            const float k = 2.2f;
            const float scale = 0.58f;
            float term1 = x * std::atan(k * x);
            float term2 = (0.5f / k) * std::log(1.0f + k * k * x * x);
            return scale * (term1 - term2);
        }

        case 7: // Wavefold
            return -1.0f / juce::MathConstants<float>::pi * std::cos(x * juce::MathConstants<float>::pi);

        case 10: // Cubic
            return (0.5f * x * x) - (x * x * x * x * 0.08333333f);

        default: return 0.0f;
        }
    }
} // namespace ngk
