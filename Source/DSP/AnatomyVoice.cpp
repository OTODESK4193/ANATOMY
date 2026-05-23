#include "AnatomyVoice.h"

AnatomyVoice::AnatomyVoice() {}
AnatomyVoice::~AnatomyVoice() {}

bool AnatomyVoice::canPlaySound(juce::SynthesiserSound* sound)
{
    return dynamic_cast<AnatomySound*> (sound) != nullptr;
}

void AnatomyVoice::startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound* sound, int /*currentPitchWheelPosition*/)
{
    noteVelocity = velocity;
    currentMidiNote = midiNoteNumber;
    sourceSamplePosition = 0.0;
    isPlaying = true;
}

void AnatomyVoice::stopNote(float /*velocity*/, bool allowTailOff)
{
    isPlaying = false;
    currentMidiNote = -1;
    clearCurrentNote();
}

void AnatomyVoice::pitchWheelMoved(int /*newPitchWheelValue*/) {}
void AnatomyVoice::controllerMoved(int /*controllerNumber*/, int /*newControllerValue*/) {}

void AnatomyVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    if (!isPlaying) return;

    auto* currentSound = dynamic_cast<AnatomySound*> (getCurrentlyPlayingSound().get());
    if (currentSound == nullptr) return;

    const auto& transBuffer = currentSound->getTransientBuffer();
    const auto& tonalBuffer = currentSound->getTonalBuffer();

    const int maxSamples = transBuffer.getNumSamples();
    if (maxSamples == 0) return;

    // 【修正：案A】鍵盤の位置に関わらずピッチ倍率は常に1.0（等速）
    double pitchRatio = 1.0;

    // ホストのサンプリングレート補正のみを維持
    if (auto sampleRate = getSampleRate(); sampleRate > 0)
    {
        pitchRatio *= (44100.0 / sampleRate);
    }

    for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch)
    {
        const int sourceCh = (ch < transBuffer.getNumChannels()) ? ch : 0;
        const float* transSrc = transBuffer.getReadPointer(sourceCh);
        const float* tonalSrc = tonalBuffer.getReadPointer(sourceCh);
        float* outData = outputBuffer.getWritePointer(ch);

        double localPos = sourceSamplePosition;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            int idx = static_cast<int>(localPos);
            if (idx >= maxSamples - 1)
            {
                isPlaying = false;
                break;
            }

            float fraction = static_cast<float>(localPos - idx);

            float tSample0 = transSrc[idx];
            float tSample1 = transSrc[idx + 1];
            float interpolatedTrans = tSample0 + fraction * (tSample1 - tSample0);

            float hSample0 = tonalSrc[idx];
            float hSample1 = tonalSrc[idx + 1];
            float interpolatedTonal = hSample0 + fraction * (hSample1 - hSample0);

            float mixedSample = (interpolatedTrans + interpolatedTonal) * noteVelocity;

            outData[startSample + sample] += mixedSample;

            localPos += pitchRatio;
        }

        if (ch == outputBuffer.getNumChannels() - 1)
        {
            sourceSamplePosition = localPos;
        }
    }

    if (!isPlaying)
    {
        clearCurrentNote();
    }
}