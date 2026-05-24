#include "AnatomyVoice.h"
#include <algorithm>

bool AnatomyVoice::canPlaySound(juce::SynthesiserSound* sound)
{
    return dynamic_cast<AnatomySound*> (sound) != nullptr;
}

void AnatomyVoice::startNote(int /*midiNoteNumber*/, float velocity, juce::SynthesiserSound* sound, int /*currentPitchWheelPosition*/)
{
    if (auto* samplerSound = dynamic_cast<AnatomySound*>(sound))
    {
        // ノイズ根絶：次の波形に入る前に、現在鳴っている波形を強制的に0.5msでミュートさせる
        // Poly仕様を使わず、単音のままスムーズに切り替える最速の手法
        releaseGain = 0.0f;

        activeData = samplerSound->getSampleData();
        if (activeData != nullptr)
        {
            triggerVelocity = velocity;
            clickReadIndex = 0.0;
            sustainReadIndex = 0.0;

            double hostRate = getSampleRate();
            double fileRate = activeData->getSampleRate();
            pitchRatio = (hostRate > 0.0 && fileRate > 0.0) ? (fileRate / hostRate) : 1.0;

            isActive = true;
        }
    }
}

void AnatomyVoice::stopNote(float /*velocity*/, bool allowTailOff)
{
    // 即座にミュートして終了
    clearCurrentNote();
    activeData = nullptr;
    isActive = false;
}

void AnatomyVoice::pitchWheelMoved(int) {}
void AnatomyVoice::controllerMoved(int, int) {}

void AnatomyVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    if (!isActive || activeData == nullptr) return;

    const auto& click = activeData->getClickBuffer();
    const auto& sustain = activeData->getSustainBuffer();

    const int clickSamples = click.getNumSamples();
    const int sustainSamples = sustain.getNumSamples();

    float* outL = outputBuffer.getWritePointer(0, startSample);
    float* outR = outputBuffer.getNumChannels() > 1 ? outputBuffer.getWritePointer(1, startSample) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        // 0.5msの超高速ランプアップ（ノイズ根絶）
        if (releaseGain < 1.0f) releaseGain += 0.02f;

        double cPos = clickReadIndex;
        int cIdx = static_cast<int>(cPos);
        float clickVal = (cIdx < clickSamples) ? click.getReadPointer(0)[cIdx] : 0.0f;

        double sPos = sustainReadIndex;
        int sIdx = static_cast<int>(sPos);
        float sustainVal = (sIdx < sustainSamples) ? sustain.getReadPointer(0)[sIdx] : 0.0f;

        float mixedVal = (clickVal + sustainVal) * triggerVelocity * releaseGain;

        if (outL != nullptr) outL[startSample + i] += mixedVal;
        if (outR != nullptr) outR[startSample + i] += mixedVal;

        clickReadIndex += pitchRatio;
        sustainReadIndex += pitchRatio;

        if (static_cast<int>(clickReadIndex) >= clickSamples && static_cast<int>(sustainReadIndex) >= sustainSamples)
        {
            clearCurrentNote();
            activeData = nullptr;
            isActive = false;
            break;
        }
    }
}