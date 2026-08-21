import re

with open('Source/PluginProcessor.h', 'r', encoding='utf-8') as f:
    text = f.read()

# Includes
if 'ADAA_Saturation.h' not in text:
    text = text.replace('#include "DSP/Effects/Limiter.h"', '#include "DSP/Effects/Limiter.h"\n#include "DSP/Effects/ADAA_Saturation.h"\n#include "DSP/Effects/TransientShaper.h"')

# layerPool and layerEffectOrder
if 'layerPool[7];' not in text:
    text = text.replace('std::unique_ptr<AudioEffect> fullMixPool[7];', 'std::unique_ptr<AudioEffect> fullMixPool[7];\n    std::unique_ptr<AudioEffect> layerPool[7];')
    text = text.replace('std::vector<int> fullMixEffectOrder;', 'std::vector<int> fullMixEffectOrder;\n    std::vector<int> layerEffectOrder;')

# getLayerPoolInstance
if 'getLayerPoolInstance' not in text:
    text = text.replace('AudioEffect* getFullMixPoolInstance(int idx) const noexcept { return (idx >= 0 && idx < 7) ? fullMixPool[idx].get() : nullptr; }', 'AudioEffect* getFullMixPoolInstance(int idx) const noexcept { return (idx >= 0 && idx < 7) ? fullMixPool[idx].get() : nullptr; }\n    AudioEffect* getLayerPoolInstance(int idx) const noexcept { return (idx >= 0 && idx < 7) ? layerPool[idx].get() : nullptr; }')

# getEffectOrder route
text = text.replace('if (route == TargetRoute::Tonal)     return tonalEffectOrder;\n        return fullMixEffectOrder;', 'if (route == TargetRoute::Tonal)     return tonalEffectOrder;\n        if (route == TargetRoute::Layer)     return layerEffectOrder;\n        return fullMixEffectOrder;')

# layerStartOffsetMs etc.
if 'layerStartOffsetMs' not in text:
    text = text.replace('float tonalEndOffsetMs = 0.0f;', 'float tonalEndOffsetMs = 0.0f;\n    float layerStartOffsetMs = 0.0f;\n    float layerEndOffsetMs = 0.0f;')

# customLayerBuffer
if 'customLayerBuffer' not in text:
    text = text.replace('juce::AudioBuffer<float> customTonalBuffer;', 'juce::AudioBuffer<float> customTonalBuffer;\n    juce::AudioBuffer<float> customLayerBuffer;')
    text = text.replace('TonalReplacer customTonalReplacer;', 'TonalReplacer customTonalReplacer;\n    TonalReplacer customLayerReplacer;')

# laneIndex signatures
text = re.sub(r'void setOffsetsFromUI\(bool isTransient, float startMs, float endMs\) noexcept;', 'void setOffsetsFromUI(int laneIndex, float startMs, float endMs) noexcept;', text)
text = re.sub(r'void setFadeFromUI\(bool isTransient, float inMs, float outMs, float inTension, float outTension\) noexcept;', 'void setFadeFromUI(int laneIndex, float inMs, float outMs, float inTension, float outTension) noexcept;', text)
text = re.sub(r'void getFadeForUI\(bool isTransient, float& inMs, float& outMs, float& inTension, float& outTension\) const noexcept;', 'void getFadeForUI(int laneIndex, float& inMs, float& outMs, float& inTension, float& outTension) const noexcept;', text)
text = re.sub(r'void setLaneSolo\(bool isTransient, bool isSolo\);', 'void setLaneSolo(int laneIndex, bool isSolo);', text)
text = re.sub(r'bool isLaneSolo\(bool isTransient\) const noexcept;', 'bool isLaneSolo(int laneIndex) const noexcept;', text)
text = re.sub(r'void storeCustomSampleFromUI\(bool isTransient, const juce::AudioBuffer<float>& newBuffer, double sr\) noexcept;', 'void storeCustomSampleFromUI(int laneIndex, const juce::AudioBuffer<float>& newBuffer, double sr) noexcept;', text)
text = re.sub(r'void clearCustomSampleFromUI\(bool isTransient\) noexcept;', 'void clearCustomSampleFromUI(int laneIndex) noexcept;', text)

# generateVoiceSample signature
text = re.sub(r'void generateVoiceSample\(VoiceState& v, float& lT, float& rT, float& lO, float& rO, float lP, float rP, float transP, float tonalP, double sr\) noexcept;', 'void generateVoiceSample(VoiceState& v, float& lT, float& rT, float& lO, float& rO, float& lL, float& lR, float lP, float rP, double sr) noexcept;', text)

with open('Source/PluginProcessor.h', 'w', encoding='utf-8') as f:
    f.write(text)
print('Done h')
