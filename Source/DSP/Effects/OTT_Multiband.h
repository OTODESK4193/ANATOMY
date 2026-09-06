#pragma once

#include "AudioEffect.h"
#include "FastMath.h"
#include "DynamicsNode.h"
#include "Crossover.h"
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <algorithm>
#include <cmath>

/**
 * OTT_Multiband (MULTI-OTO Equivalent DSP - OTTx2 Dual Stage Cascade)
 * 
 * MULTI-OTO の Linkwitz-Riley 4次 (LR4) + オールパス補正 Crossover、
 * および AVX2 SIMD RMS検波 DynamicsNode を完全移植。
 * 
 * 独立した 2 段直列カスケード（Stage 1 & Stage 2）構成を備え、
 * ドライ信号とのコムフィルタを排除する ALIGN PHASE モードと、
 * ドラム音源の無音部ノイズフロア過剰持ち上げを抑制するスマートゲート（GateFloor）を統合。
 */
class OTT_Multiband final : public AudioEffect
{
public:
    OTT_Multiband()
    {
        updateCrossovers();
    }

    ~OTT_Multiband() override = default;

    void prepare(double sampleRate, int maxBlockSize) override
    {
        currentSampleRate = sampleRate;
        internalMaxBlock = std::max(64, maxBlockSize);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate       = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32>(internalMaxBlock);
        spec.numChannels      = 2;

        crossover1.prepare(spec);
        dryCrossover1.prepare(spec);
        crossover2.prepare(spec);
        dryCrossover2.prepare(spec);

        updateCrossovers();

        node1.prepare(sampleRate, internalMaxBlock);
        node2.prepare(sampleRate, internalMaxBlock);

        node1.setGateFloorDb(gateFloorDb);
        node2.setGateFloorDb(gateFloorDb);

        dryBuffer.setSize(2, internalMaxBlock, false, false, true);
        dryBuffer.clear();

        simdBuffer.resize(static_cast<size_t>(internalMaxBlock));

        updateNodeCoeffs();
    }

    void reset() noexcept override
    {
        crossover1.reset();
        dryCrossover1.reset();
        crossover2.reset();
        dryCrossover2.reset();

        node1.reset();
        node2.reset();

        if (dryBuffer.getNumSamples() > 0)
            dryBuffer.clear();
    }

    void process(juce::AudioBuffer<float>& buffer) noexcept override
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples  = buffer.getNumSamples();

        if (numChannels < 2 || numSamples <= 0 || currentSampleRate <= 0.0)
            return;

        // 係数更新（スムージング目標値適用）
        updateNodeCoeffs();

        float* left  = buffer.getWritePointer(0);
        float* right = buffer.getWritePointer(1);

        // 内部バッファ長を超えるブロック（オフラインレンダリング等）でも安全にチャンク分割処理
        int offset = 0;
        while (offset < numSamples)
        {
            const int n = std::min(internalMaxBlock, numSamples - offset);
            processChunk(left + offset, right + offset, n);
            offset += n;
        }
    }

    juce::String getName() const override { return "OTT Multiband"; }
    TargetRoute getTargetRoute() const noexcept override { return route; }
    void setTargetRoute(TargetRoute r) noexcept override { route = r; }

    bool isActive() const noexcept override { return activeState; }
    void setActive(bool shouldBeActive) noexcept override { activeState = shouldBeActive; }

    // --- Global Controls ---
    void setMix(float newMix) noexcept override { currentMix = juce::jlimit(0.0f, 1.0f, newMix); }
    float getMix() const noexcept override { return currentMix; }

    void setGateFloorDb(float db) noexcept
    {
        gateFloorDb = juce::jlimit(-70.0f, -20.0f, db);
        node1.setGateFloorDb(gateFloorDb);
        node2.setGateFloorDb(gateFloorDb);
    }
    float getGateFloorDb() const noexcept { return gateFloorDb; }

    void setPhaseMode(int mode) noexcept { phaseMode = juce::jlimit(0, 1, mode); }
    int getPhaseMode() const noexcept { return phaseMode; }

    void setXoverLink(bool link) noexcept
    {
        xoverLink = link;
        if (xoverLink)
        {
            s2LowMidFreq  = s1LowMidFreq;
            s2MidHighFreq = s1MidHighFreq;
            updateCrossovers();
        }
    }
    bool isXoverLink() const noexcept { return xoverLink; }

    // --- Stage 1 Controls ---
    void setTimeMultiplier(float t) noexcept { s1Time = juce::jlimit(0.1f, 10.0f, t); }
    float getTimeMultiplier() const noexcept { return s1Time; }

    void setLowMidXOver(float freq) noexcept
    {
        s1LowMidFreq = juce::jlimit(40.0f, 1000.0f, freq);
        if (xoverLink) s2LowMidFreq = s1LowMidFreq;
        updateCrossovers();
    }
    float getLowMidXOver() const noexcept { return s1LowMidFreq; }

    void setMidHighXOver(float freq) noexcept
    {
        s1MidHighFreq = juce::jlimit(1000.0f, 15000.0f, freq);
        if (xoverLink) s2MidHighFreq = s1MidHighFreq;
        updateCrossovers();
    }
    float getMidHighXOver() const noexcept { return s1MidHighFreq; }

    void setBandUpward(int bandIdx, float pct) noexcept
    {
        if (bandIdx >= 0 && bandIdx < 3)
            s1Upward[bandIdx] = juce::jlimit(0.0f, 100.0f, pct);
    }
    void setBandDownward(int bandIdx, float pct) noexcept
    {
        if (bandIdx >= 0 && bandIdx < 3)
            s1Downward[bandIdx] = juce::jlimit(0.0f, 100.0f, pct);
    }
    void setBandGainDb(int bandIdx, float db) noexcept
    {
        if (bandIdx >= 0 && bandIdx < 3)
            s1GainDb[bandIdx] = juce::jlimit(-24.0f, 24.0f, db);
    }

    // --- Stage 2 Controls ---
    void setStage2On(bool on) noexcept { s2On = on; }
    bool isStage2On() const noexcept { return s2On; }

    void setStage2Mix(float mix) noexcept { s2Mix = juce::jlimit(0.0f, 1.0f, mix); }
    float getStage2Mix() const noexcept { return s2Mix; }

    void setStage2TimeMultiplier(float t) noexcept { s2Time = juce::jlimit(0.1f, 10.0f, t); }
    float getStage2TimeMultiplier() const noexcept { return s2Time; }

    void setStage2LowMidXOver(float freq) noexcept
    {
        s2LowMidFreq = juce::jlimit(40.0f, 1000.0f, freq);
        updateCrossovers();
    }
    float getStage2LowMidXOver() const noexcept { return s2LowMidFreq; }

    void setStage2MidHighXOver(float freq) noexcept
    {
        s2MidHighFreq = juce::jlimit(1000.0f, 15000.0f, freq);
        updateCrossovers();
    }
    float getStage2MidHighXOver() const noexcept { return s2MidHighFreq; }

    void setStage2BandUpward(int bandIdx, float pct) noexcept
    {
        if (bandIdx >= 0 && bandIdx < 3)
            s2Upward[bandIdx] = juce::jlimit(0.0f, 100.0f, pct);
    }
    void setStage2BandDownward(int bandIdx, float pct) noexcept
    {
        if (bandIdx >= 0 && bandIdx < 3)
            s2Downward[bandIdx] = juce::jlimit(0.0f, 100.0f, pct);
    }
    void setStage2BandGainDb(int bandIdx, float db) noexcept
    {
        if (bandIdx >= 0 && bandIdx < 3)
            s2GainDb[bandIdx] = juce::jlimit(-24.0f, 24.0f, db);
    }

    float getIndexedParameter(int index) const noexcept override
    {
        if      (index == 0) return getMix();
        else if (index == 1) return getTimeMultiplier();
        else if (index == 2) return getGateFloorDb();
        else if (index == 3) return getStage2Mix();
        return 0.0f;
    }

    void setIndexedParameter(int index, float value) noexcept override
    {
        if      (index == 0) setMix(value);
        else if (index == 1) setTimeMultiplier(value);
        else if (index == 2) setGateFloorDb(value);
        else if (index == 3) setStage2Mix(value);
    }

private:
    void updateCrossovers() noexcept
    {
        crossover1.setFrequencies(s1LowMidFreq, s1MidHighFreq);
        dryCrossover1.setFrequencies(s1LowMidFreq, s1MidHighFreq);

        crossover2.setFrequencies(s2LowMidFreq, s2MidHighFreq);
        dryCrossover2.setFrequencies(s2LowMidFreq, s2MidHighFreq);
    }

    void updateNodeCoeffs() noexcept
    {
        if (currentSampleRate <= 0.0) return;

        // MULTI-OTO 標準: attacks={20, 20, 20}, releases={100, 100, 100}
        const float atk[3] = { 20.0f, 20.0f, 20.0f };
        const float rel[3] = { 100.0f, 100.0f, 100.0f };
        const float depths[3] = { 100.0f, 100.0f, 100.0f };

        // Stage 1
        const auto c1 = DynamicsNode::computeCoeffs(currentSampleRate,
                                                    s1GainDb, depths, s1Upward, s1Downward,
                                                    s1Time * 100.0f, atk, rel, 100.0f);
        node1.applyCoeffs(c1);

        // Stage 2
        const auto c2 = DynamicsNode::computeCoeffs(currentSampleRate,
                                                    s2GainDb, depths, s2Upward, s2Downward,
                                                    s2Time * 100.0f, atk, rel, s2Mix * 100.0f);
        node2.applyCoeffs(c2);
    }

    void processChunk(float* left, float* right, int numSamples) noexcept
    {
        // 1. Dry バッファのバックアップ
        float* dL = dryBuffer.getWritePointer(0);
        float* dR = dryBuffer.getWritePointer(1);
        for (int i = 0; i < numSamples; ++i)
        {
            dL[i] = left[i];
            dR[i] = right[i];
        }

        // 2. ALIGN PHASE モード: Dry 側にも Wet と同一のオールパス位相回転を通過させる
        // これにより Dry/Wet ミックス時のコムフィルタ・中域位相キャンセルを 100% 根絶
        if (phaseMode == 1)
        {
            for (int i = 0; i < numSamples; ++i)
                dryCrossover1.processDry(dL[i], dR[i], dL[i], dR[i]);

            if (s2On && s2Mix > 0.001f)
            {
                for (int i = 0; i < numSamples; ++i)
                    dryCrossover2.processDry(dL[i], dR[i], dL[i], dR[i]);
            }
        }

        // 3. Stage 1 (Pre/Main)
        for (int i = 0; i < numSamples; ++i)
        {
            float lL, lR, mL, mR, hL, hR;
            crossover1.process(left[i], right[i], lL, lR, mL, mR, hL, hR);
            alignas(32) float raw[8] = { lL, lR, mL, mR, hL, hR, 0.0f, 0.0f };
            simdBuffer[static_cast<size_t>(i)] = juce::dsp::SIMDRegister<float>::fromRawArray(raw);
        }

        node1.process(simdBuffer.data(), numSamples);

        for (int i = 0; i < numSamples; ++i)
        {
            alignas(32) float raw[8];
            simdBuffer[static_cast<size_t>(i)].copyToRawArray(raw);
            left[i]  = raw[0] + raw[2] + raw[4];
            right[i] = raw[1] + raw[3] + raw[5];
        }

        // 4. Stage 2 (Post/Wall) - OTTx2 カスケード
        if (s2On && s2Mix > 0.001f)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                float lL, lR, mL, mR, hL, hR;
                crossover2.process(left[i], right[i], lL, lR, mL, mR, hL, hR);
                alignas(32) float raw[8] = { lL, lR, mL, mR, hL, hR, 0.0f, 0.0f };
                simdBuffer[static_cast<size_t>(i)] = juce::dsp::SIMDRegister<float>::fromRawArray(raw);
            }

            node2.process(simdBuffer.data(), numSamples);

            for (int i = 0; i < numSamples; ++i)
            {
                alignas(32) float raw[8];
                simdBuffer[static_cast<size_t>(i)].copyToRawArray(raw);
                left[i]  = raw[0] + raw[2] + raw[4];
                right[i] = raw[1] + raw[3] + raw[5];
            }
        }

        // 5. 最終 Dry / Wet ミックス
        const float depth = currentMix;
        for (int i = 0; i < numSamples; ++i)
        {
            left[i]  = dL[i] + (left[i]  - dL[i]) * depth;
            right[i] = dR[i] + (right[i] - dR[i]) * depth;

            // 安全サニタイズ（NaN / Inf 除去）
            if (std::isnan(left[i])  || std::isinf(left[i]))  left[i]  = 0.0f;
            if (std::isnan(right[i]) || std::isinf(right[i])) right[i] = 0.0f;
            left[i]  = juce::jlimit(-DynamicsNode::kSafeCeiling, DynamicsNode::kSafeCeiling, left[i]);
            right[i] = juce::jlimit(-DynamicsNode::kSafeCeiling, DynamicsNode::kSafeCeiling, right[i]);
        }
    }

    double currentSampleRate = 44100.0;
    int internalMaxBlock     = 2048;

    // Stage 1 エンジン
    Crossover crossover1;
    Crossover dryCrossover1;
    DynamicsNode node1;

    // Stage 2 エンジン (OTT x 2)
    Crossover crossover2;
    Crossover dryCrossover2;
    DynamicsNode node2;

    // 内部作業バッファ
    juce::AudioBuffer<float> dryBuffer;
    alignas(32) std::vector<juce::dsp::SIMDRegister<float>> simdBuffer;

    // --- Global パラメータ ---
    float currentMix  = 0.35f;
    float gateFloorDb = -45.0f;
    int   phaseMode   = 1;     // 0: Color, 1: Align Phase (デフォルト Align)
    bool  xoverLink   = true;  // S1/S2 クロスオーバー連動

    // --- Stage 1 パラメータ ---
    float s1Time         = 1.35f;
    float s1LowMidFreq   = 140.0f;
    float s1MidHighFreq  = 3800.0f;
    float s1Upward[3]    = { 60.0f, 40.0f, 15.0f };
    float s1Downward[3]  = { 75.0f, 70.0f, 60.0f };
    float s1GainDb[3]    = { 0.0f,  0.0f,  0.0f  };

    // --- Stage 2 パラメータ ---
    bool  s2On           = true;
    float s2Mix          = 0.50f;
    float s2Time         = 1.35f;
    float s2LowMidFreq   = 140.0f;
    float s2MidHighFreq  = 3800.0f;
    float s2Upward[3]    = { 60.0f, 40.0f, 15.0f };
    float s2Downward[3]  = { 75.0f, 70.0f, 60.0f };
    float s2GainDb[3]    = { 0.0f,  0.0f,  0.0f  };

    TargetRoute route = TargetRoute::FullMix;
    bool activeState  = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OTT_Multiband)
};
