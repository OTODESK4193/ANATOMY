#include "Crossover.h"

Crossover::Crossover() {
    using LRType = juce::dsp::LinkwitzRileyFilterType;
    lp1L.setType(LRType::lowpass);  hp1L.setType(LRType::highpass);
    lp1R.setType(LRType::lowpass);  hp1R.setType(LRType::highpass);
    lp2L.setType(LRType::lowpass);  hp2L.setType(LRType::highpass);
    lp2R.setType(LRType::lowpass);  hp2R.setType(LRType::highpass);

    // 低域を高域クロスオーバーと位相整合させるためのオールパス
    ap2L.setType(LRType::allpass);
    ap2R.setType(LRType::allpass);
}

void Crossover::prepare(const juce::dsp::ProcessSpec& spec) {
    lp1L.prepare(spec); hp1L.prepare(spec); lp2L.prepare(spec); hp2L.prepare(spec); ap2L.prepare(spec);
    lp1R.prepare(spec); hp1R.prepare(spec); lp2R.prepare(spec); hp2R.prepare(spec); ap2R.prepare(spec);
    setFrequencies(currentLowFreq, currentHighFreq);
}

void Crossover::reset() {
    lp1L.reset(); hp1L.reset(); lp2L.reset(); hp2L.reset(); ap2L.reset();
    lp1R.reset(); hp1R.reset(); lp2R.reset(); hp2R.reset(); ap2R.reset();
}

void Crossover::setFrequencies(float lowFreq, float highFreq) {
    currentLowFreq = lowFreq; currentHighFreq = highFreq;
    lp1L.setCutoffFrequency(lowFreq); hp1L.setCutoffFrequency(lowFreq);
    lp1R.setCutoffFrequency(lowFreq); hp1R.setCutoffFrequency(lowFreq);
    lp2L.setCutoffFrequency(highFreq); hp2L.setCutoffFrequency(highFreq);
    lp2R.setCutoffFrequency(highFreq); hp2R.setCutoffFrequency(highFreq);
    ap2L.setCutoffFrequency(highFreq); ap2R.setCutoffFrequency(highFreq);
}

void Crossover::process(float inL, float inR, float& lowL, float& lowR, float& midL, float& midR, float& highL, float& highR) {
    // LOW は高域XOのオールパスを追加で通す ➔ low + mid + high が完全な allpass になる
    lowL = ap2L.processSample(0, lp1L.processSample(0, inL));
    lowR = ap2R.processSample(1, lp1R.processSample(1, inR));

    const float remL = hp1L.processSample(0, inL);
    const float remR = hp1R.processSample(1, inR);

    midL  = lp2L.processSample(0, remL); midR  = lp2R.processSample(1, remR);
    highL = hp2L.processSample(0, remL); highR = hp2R.processSample(1, remR);
}

void Crossover::processDry(float inL, float inR, float& dryL, float& dryR) {
    // 分割 ➔ 即座に再合成。振幅はそのまま、位相だけ WET と揃う。
    float lL, lR, mL, mR, hL, hR;
    process(inL, inR, lL, lR, mL, mR, hL, hR);
    dryL = lL + mL + hL;
    dryR = lR + mR + hR;
}
