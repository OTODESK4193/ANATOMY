#pragma once
#include <juce_dsp/juce_dsp.h>

// ============================================================================
//  Crossover — Linkwitz-Riley 3バンド分割
//
//  低域には高域クロスオーバーのオールパス (ap2) を通します。
//  これが無いと low + mid + high の和がフラットにならず、その誤差が
//  128 段で累乗されて中域に巨大な癖を作ります (旧・再構成誤差)。
//
//  位相補償は行わないため、LR 特有の位相回転 (ディスパーション) は
//  意図どおり蓄積されます。
// ============================================================================
class Crossover {
public:
    Crossover();
    void prepare(const juce::dsp::ProcessSpec& spec);
    void reset();
    void setFrequencies(float lowFreq, float highFreq);

    void process(float inL, float inR,
        float& lowL, float& lowR,
        float& midL, float& midR,
        float& highL, float& highR);

    // ALIGN PHASE 用。DRY 側に「分割して足し戻すだけ」の処理を通し、
    // WET と同じ位相回転を与えます (振幅は変わりません)。
    void processDry(float inL, float inR, float& dryL, float& dryR);

private:
    juce::dsp::LinkwitzRileyFilter<float> lp1L, hp1L, lp2L, hp2L, ap2L;
    juce::dsp::LinkwitzRileyFilter<float> lp1R, hp1R, lp2R, hp2R, ap2R;
    float currentLowFreq = 88.0f;
    float currentHighFreq = 2500.0f;
};
