#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

// HPSSで分離されたオーディオデータをVoiceに安全に渡すためのSoundクラス
class AnatomySound : public juce::SynthesiserSound
{
public:
    AnatomySound(const juce::AudioBuffer<float>& transientData, const juce::AudioBuffer<float>& tonalData)
        : transientBuffer(transientData), tonalBuffer(tonalData) {
    }

    bool appliesToNote(int /*midiNoteNumber*/) override { return true; }
    bool appliesToChannel(int /*midiChannel*/) override { return true; }

    const juce::AudioBuffer<float>& getTransientBuffer() const { return transientBuffer; }
    const juce::AudioBuffer<float>& getTonalBuffer() const { return tonalBuffer; }

private:
    juce::AudioBuffer<float> transientBuffer;
    juce::AudioBuffer<float> tonalBuffer;
};

// MIDIトリガーに応じてTransient/Tonalバッファをピッチ追従再生するVoiceクラス
class AnatomyVoice : public juce::SynthesiserVoice
{
public:
    AnatomyVoice();
    ~AnatomyVoice() override;

    bool canPlaySound(juce::SynthesiserSound* sound) override;
    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound* sound, int currentPitchWheelPosition) override;
    void stopNote(float velocity, bool allowTailOff) override;
    void pitchWheelMoved(int newPitchWheelValue) override;
    void controllerMoved(int controllerNumber, int newControllerValue) override;
    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override;

private:
    double sourceSamplePosition = 0.0;
    float noteVelocity = 0.0f;
    bool isPlaying = false;
    int currentMidiNote = -1;
    float pitchCorrectionDelta = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnatomyVoice)
};