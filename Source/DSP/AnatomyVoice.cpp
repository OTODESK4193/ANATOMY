#include "AnatomyVoice.h"

bool AnatomyVoice::canPlaySound(juce::SynthesiserSound* sound)
{
    return dynamic_cast<AnatomySound*> (sound) != nullptr;
}

void AnatomyVoice::startNote(int /*midiNoteNumber*/, float velocity, juce::SynthesiserSound* sound, int /*currentPitchWheelPosition*/)
{
    if (auto* samplerSound = dynamic_cast<AnatomySound*>(sound))
    {
        activeData = samplerSound->getSampleData();
        if (activeData != nullptr)
        {
            triggerVelocity = velocity;
            clickReadIndex = 0.0;
            sustainReadIndex = 0.0;

            // DAWの動作レートとファイル本来のナマのレートを取得
            double hostRate = getSampleRate();
            double fileRate = activeData->getSampleRate();

            // どのキーを押しても音階追従はせず、サンプリングレートの差のみを完全に相殺
            if (hostRate > 0.0 && fileRate > 0.0)
            {
                pitchRatio = fileRate / hostRate;
            }
            else
            {
                pitchRatio = 1.0;
            }

            isActive = true;
        }
        else
        {
            clearCurrentNote();
        }
    }
}

void AnatomyVoice::stopNote(float /*velocity*/, bool allowTailOff)
{
    if (!allowTailOff)
    {
        clearCurrentNote();
        activeData = nullptr;
        isActive = false;
    }
}

void AnatomyVoice::pitchWheelMoved(int /*newPitchWheelValue*/) {}
void AnatomyVoice::controllerMoved(int /*controllerNumber*/, int /*newControllerValue*/) {}

void AnatomyVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    if (!isActive || activeData == nullptr) return;

    auto localData = activeData;
    if (localData == nullptr) return;

    const auto& click = localData->getClickBuffer();
    const auto& sustain = localData->getSustainBuffer();

    const int clickSamples = click.getNumSamples();
    const int sustainSamples = sustain.getNumSamples();

    if (clickSamples == 0 && sustainSamples == 0) return;

    float* outL = outputBuffer.getWritePointer(0, startSample);
    float* outR = outputBuffer.getNumChannels() > 1 ? outputBuffer.getWritePointer(1, startSample) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        float clickVal = 0.0f;
        float sustainVal = 0.0f;

        // 線形補間を完全維持した、安全なロックフリー等速オリジナル再生
        double cPos = clickReadIndex;
        int cIdx = static_cast<int>(cPos);
        if (cIdx < clickSamples)
        {
            float frac = static_cast<float>(cPos - cIdx);
            float s0 = click.getReadPointer(0)[cIdx];
            float s1 = (cIdx + 1 < clickSamples) ? click.getReadPointer(0)[cIdx + 1] : 0.0f;
            clickVal = s0 + frac * (s1 - s0);
        }

        double sPos = sustainReadIndex;
        int sIdx = static_cast<int>(sPos);
        if (sIdx < sustainSamples)
        {
            float frac = static_cast<float>(sPos - sIdx);
            float s0 = sustain.getReadPointer(0)[sIdx];
            float s1 = (sIdx + 1 < sustainSamples) ? sustain.getReadPointer(0)[sIdx + 1] : 0.0f;
            sustainVal = s0 + frac * (s1 - s0);
        }

        const float mixedVal = (clickVal + sustainVal) * triggerVelocity;

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