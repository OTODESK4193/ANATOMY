#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "KickMath.h"
#include <cmath>

// ============================================================================
//  Saturation  -  ADAA(アンチエイリアス)サチュレーション
//
//  サチュレーション(歪み)1サンプル分の処理をまとめたモジュールです。
//  「1サンプル前の値(履歴)」を覚えておく必要があるため、状態は SaturationState
//  に保持し、関数へ参照渡しします（チャンネル毎に1つ用意します）。
//
//  ※ アルゴリズムは元の PluginProcessor から移設。音は完全に同一です。
// ============================================================================
namespace ngk
{
    // 1チャンネル分のサチュレーション履歴
    struct SaturationState
    {
        float tapeHysteresis = 0.0f; // Tape モデルのヒステリシス
        float lastX = 0.0f;          // 1サンプル前の入力
        float lastF = 0.0f;          // 1サンプル前の原始関数値
        bool  active = false;

        void reset()
        {
            tapeHysteresis = 0.0f;
            lastX = 0.0f;
            lastF = 0.0f;
            active = false;
        }
    };

    // 1サンプルを歪ませて返す
    //  type  : 歪みの種類(0..10)
    //  drive : 歪みの深さ(1.0で無歪み)
    inline float processSaturationSampleADAA(float x, int type, float drive, SaturationState& state) noexcept
    {
        if (drive <= 1.001f) {
            state.active = false;
            state.lastX = x;
            return x;
        }

        // --- Tape (履歴を使う特殊処理) ---
        if (type == 3) {
            float g = x * drive;
            float y = 0.92f * std::tanh(g + 0.08f * state.tapeHysteresis);
            state.tapeHysteresis = y;
            return y;
        }

        // --- ADAA を使わない静的カーブ群 ---
        if (type == 2 || type == 4 || type == 5 || type == 8 || type == 9 || type == 10) {
            float g = x * drive;
            switch (type) {
            case 2: { // Triode: 非対称ソフト（連続・滑らか / 偶数次倍音）。旧版は v=0 で段差→ノイズだった
                const float b = 0.25f;
                return std::tanh(g + b) - std::tanh(b);
            }
            case 4: return g / (1.0f + 0.45f * std::abs(g));
            case 5: return (std::abs(g) < 1.0f) ? g - (g * g * g) / 3.0f
                                                : (g > 0 ? (2.0f / 3.0f) : -(2.0f / 3.0f)); // |g|=1 で厳密連続
            case 8: { float step = 1.0f / (1.0f + (25.0f - drive)); return std::round(g / step) * step; }
            case 9: { // Exciter: 有界（高次倍音を付加しつつ暴れない）。旧版は g 素通しで過大だった
                const float s = std::tanh(g);
                return s + 0.3f * (std::tanh(3.0f * g) - s);
            }
            case 10: { // Cubic soft clip: 有界・単調（高ドライブでも発散しない）。旧版は g³ が発散していた
                const float c = juce::jlimit(-1.0f, 1.0f, g);
                return 1.5f * c - 0.5f * c * c * c;
            }
            default: return g;
            }
        }

        // --- ADAA (1次アンチエイリアス) を使う群 (0,1,6,7,10) ---
        float g = x * drive;

        if (!state.active) {
            state.active = true;
            state.lastX = g;
            state.lastF = calcADAAFunc(g, type);

            switch (type) {
            case 0: return std::tanh(g);
            case 1: return juce::jlimit(-1.0f, 1.0f, g);
            case 6: return std::atan(g * 2.2f) * 0.58f;
            case 7: return std::sin(g * juce::MathConstants<float>::pi);
            case 10: return g - (g * g * g) / 3.1f;
            }
        }

        float Fx = calcADAAFunc(g, type);
        float output = 0.0f;
        float delta = g - state.lastX;

        if (std::abs(delta) < 1.0e-5f) {
            switch (type) {
            case 0: output = std::tanh(g); break;
            case 1: output = juce::jlimit(-1.0f, 1.0f, g); break;
            case 6: output = std::atan(g * 2.2f) * 0.58f; break;
            case 7: output = std::sin(g * juce::MathConstants<float>::pi); break;
            case 10: output = g - (g * g * g) / 3.1f; break;
            }
        }
        else {
            output = (Fx - state.lastF) / delta;
        }

        state.lastX = g;
        state.lastF = Fx;
        return output;
    }
} // namespace ngk
