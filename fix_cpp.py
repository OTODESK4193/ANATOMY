import re

with open('Source/PluginProcessor.cpp', 'r', encoding='utf-8') as f:
    text = f.read()

# 1. Constructor instantiatePool(layerPool)
if 'instantiatePool(layerPool, TargetRoute::Layer);' not in text:
    text = text.replace('instantiatePool(fullMixPool, TargetRoute::FullMix);', 'instantiatePool(fullMixPool, TargetRoute::FullMix);\n    instantiatePool(layerPool, TargetRoute::Layer);')

# 2. updateParamCache
if 'layerEffectOrder.clear();' not in text:
    p = '''    tonalEffectOrder.clear();
    fullMixEffectOrder.clear();
    layerEffectOrder.clear();'''
    text = re.sub(r'    tonalEffectOrder\.clear\(\);\s*fullMixEffectOrder\.clear\(\);', p, text)

# 3. getEffectOrder inside updateParamCache
if 'case TargetRoute::Layer:' not in text:
    p2 = '''                case TargetRoute::Tonal:     tonalEffectOrder.push_back(i); break;
                case TargetRoute::FullMix:   fullMixEffectOrder.push_back(i); break;
                case TargetRoute::Layer:     layerEffectOrder.push_back(i); break;'''
    text = re.sub(r'                case TargetRoute::Tonal:     tonalEffectOrder\.push_back\(i\); break;\s*case TargetRoute::FullMix:   fullMixEffectOrder\.push_back\(i\); break;', p2, text)

# 4. LaneParamCache ts setup
if 'cache.ts = dynamic_cast<TransientShaper*>(effect);' not in text:
    p3 = '''        } else if (auto* ts = dynamic_cast<TransientShaper*>(effect)) {
            cache.ts = ts;
            cache.satDrive = nullptr;
        }'''
    text = re.sub(r'        } else if \(auto\* lim = dynamic_cast<Limiter\*>\(effect\)\) \{\s*cache\.lim = lim;\s*\}', '        } else if (auto* lim = dynamic_cast<Limiter*>(effect)) {\n            cache.lim = lim;\n        } else if (auto* ts = dynamic_cast<TransientShaper*>(effect)) {\n            cache.ts = ts;\n        }', text)

# 5. processBlock voice generation call
text = re.sub(r'            generateVoiceSample\(v, lT, rT, lO, rO, lP, rP, transPitch, tonalPitch, sr\);', '            float lL=0.0f, lR=0.0f;\n            generateVoiceSample(v, lT, rT, lO, rO, lL, lR, lP, rP, sr);', text)

# 6. processBlock mix layer
if 'float layerGain = std::pow(10.0f,' not in text:
    b = '''                const float tonalGain = std::pow(10.0f, apvts.getRawParameterValue("tonalMixGain")->load() / 20.0f);
                const float layerGain = std::pow(10.0f, apvts.getRawParameterValue("layerGain")->load() / 20.0f);'''
    text = re.sub(r'                const float tonalGain = std::pow\(10\.0f, apvts\.getRawParameterValue\("tonalMixGain"\)->load\(\) / 20\.0f\);', b, text)

    b2 = '''                const float oL = lO * tonalGain;
                const float oR = rO * tonalGain;
                const float lL_m = lL * layerGain;
                const float lR_m = lR * layerGain;'''
    text = re.sub(r'                const float oL = lO \* tonalGain;\s*const float oR = rO \* tonalGain;', b2, text)
    
    text = re.sub(r'                buffer\.addSample\(0, s, tL \+ oL\);\s*buffer\.addSample\(1, s, tR \+ oR\);', '                buffer.addSample(0, s, tL + oL + lL_m);\n                buffer.addSample(1, s, tR + oR + lR_m);', text)

# 7. generateVoiceSample signature and impl
if 'float& lL, float& lR' not in text:
    text = re.sub(r'void AnatomyAudioProcessor::generateVoiceSample\(VoiceState& v, float& lT, float& rT, float& lO, float& rO, float lP, float rP, float transPitch, float tonalPitch, double sr\) noexcept\n\{', 'void AnatomyAudioProcessor::generateVoiceSample(VoiceState& v, float& lT, float& rT, float& lO, float& rO, float& lL, float& lR, float lP, float rP, double sr) noexcept\n{', text)
    # Inside generateVoiceSample, add layer reading
    b3 = '''    lO *= v.tonalFadeOutEnv;
    rO *= v.tonalFadeOutEnv;

    lL = 0.0f; lR = 0.0f;
    float layerOffsetMs = apvts.getRawParameterValue("layerOffset")->load();
    int layerOffsetSamples = static_cast<int>((layerOffsetMs / 1000.0f) * sr);
    if (v.samplesPlayed >= layerOffsetSamples)
    {
        int layerReadIndex = v.samplesPlayed - layerOffsetSamples;
        customLayerReplacer.readSample(layerReadIndex, lL, lR);
        float layerFadeInEnv = 1.0f;
        float layerFadeOutEnv = 1.0f;
        
        float inMs = 1.0f, outMs = 10.0f, inTension = 0.0f, outTension = 0.0f;
        getFadeForUI(3, inMs, outMs, inTension, outTension);
        
        float inSamples = (inMs / 1000.0f) * sr;
        if (inSamples > 1.0f && layerReadIndex < inSamples) {
            float phase = layerReadIndex / inSamples;
            layerFadeInEnv = juce::jmap(std::pow(phase, std::exp(inTension)), 0.0f, 1.0f);
        }
        
        float outSamples = (outMs / 1000.0f) * sr;
        float fadeOutStartIdx = layerEndOffsetMs > 0.0f ? ((layerEndOffsetMs / 1000.0f) * sr - outSamples) : (customLayerReplacer.getBuffer().getNumSamples() - outSamples);
        if (outSamples > 1.0f && layerReadIndex >= fadeOutStartIdx) {
            float phase = (layerReadIndex - fadeOutStartIdx) / outSamples;
            phase = juce::jlimit(0.0f, 1.0f, phase);
            layerFadeOutEnv = 1.0f - juce::jmap(std::pow(phase, std::exp(outTension)), 0.0f, 1.0f);
        }
        lL *= (layerFadeInEnv * layerFadeOutEnv);
        lR *= (layerFadeInEnv * layerFadeOutEnv);
    }
'''
    text = re.sub(r'    lO \*= v\.tonalFadeOutEnv;\s*rO \*= v\.tonalFadeOutEnv;', b3, text)

# 8. setOffsetsFromUI, getFadeForUI, setFadeFromUI
text = re.sub(r'void AnatomyAudioProcessor::setOffsetsFromUI\(bool isTransient, float startMs, float endMs\) noexcept\n\{', 'void AnatomyAudioProcessor::setOffsetsFromUI(int laneIndex, float startMs, float endMs) noexcept\n{', text)
text = re.sub(r'    if \(isTransient\)\s*\{\s*transStartOffsetMs = startMs;\s*transEndOffsetMs = endMs;\s*\}\s*else\s*\{\s*tonalStartOffsetMs = startMs;\s*tonalEndOffsetMs = endMs;\s*\}', '    if (laneIndex == 1) { transStartOffsetMs = startMs; transEndOffsetMs = endMs; }\n    else if (laneIndex == 2) { tonalStartOffsetMs = startMs; tonalEndOffsetMs = endMs; }\n    else if (laneIndex == 3) { layerStartOffsetMs = startMs; layerEndOffsetMs = endMs; }', text)

text = re.sub(r'void AnatomyAudioProcessor::getFadeForUI\(bool isTransient, float& inMs, float& outMs, float& inTension, float& outTension\) const noexcept\n\{', 'void AnatomyAudioProcessor::getFadeForUI(int laneIndex, float& inMs, float& outMs, float& inTension, float& outTension) const noexcept\n{', text)
text = re.sub(r'    juce::String prefix = isTransient \? "trans" : "tonal";', '    juce::String prefix;\n    if (laneIndex == 1) prefix = "trans";\n    else if (laneIndex == 2) prefix = "tonal";\n    else if (laneIndex == 3) prefix = "layer";\n    else prefix = "full";', text)

text = re.sub(r'void AnatomyAudioProcessor::setFadeFromUI\(bool isTransient, float inMs, float outMs, float inTension, float outTension\) noexcept\n\{', 'void AnatomyAudioProcessor::setFadeFromUI(int laneIndex, float inMs, float outMs, float inTension, float outTension) noexcept\n{', text)

# 9. setLaneSolo, isLaneSolo
text = re.sub(r'void AnatomyAudioProcessor::setLaneSolo\(bool isTransient, bool isSolo\)\n\{', 'void AnatomyAudioProcessor::setLaneSolo(int laneIndex, bool isSolo)\n{', text)
text = re.sub(r'    if \(isSolo\)\s*\{\s*setSoloMode\(isTransient \? 1 : 2\);\s*\}\s*else\s*\{\s*if \(getSoloMode\(\) == \(isTransient \? 1 : 2\)\)\s*setSoloMode\(0\);\s*\}', '    if (isSolo) {\n        setSoloMode(laneIndex);\n    } else {\n        if (getSoloMode() == laneIndex) setSoloMode(0);\n    }', text)

text = re.sub(r'bool AnatomyAudioProcessor::isLaneSolo\(bool isTransient\) const noexcept\n\{', 'bool AnatomyAudioProcessor::isLaneSolo(int laneIndex) const noexcept\n{', text)
text = re.sub(r'    return currentSoloMode == \(isTransient \? 1 : 2\);', '    return currentSoloMode == laneIndex;', text)

# 10. storeCustomSampleFromUI
text = re.sub(r'void AnatomyAudioProcessor::storeCustomSampleFromUI\(bool isTransient, const juce::AudioBuffer<float>& newBuffer, double sr\) noexcept\n\{', 'void AnatomyAudioProcessor::storeCustomSampleFromUI(int laneIndex, const juce::AudioBuffer<float>& newBuffer, double sr) noexcept\n{', text)
text = re.sub(r'    if \(isTransient\)\s*\{\s*customTransReplacer\.setSample\(newBuffer, sr\);\s*customTransBuffer\.makeCopyOf\(newBuffer\);\s*\}\s*else\s*\{\s*customTonalReplacer\.setSample\(newBuffer, sr\);\s*customTonalBuffer\.makeCopyOf\(newBuffer\);\s*\}', '    if (laneIndex == 1) { customTransReplacer.setSample(newBuffer, sr); customTransBuffer.makeCopyOf(newBuffer); }\n    else if (laneIndex == 2) { customTonalReplacer.setSample(newBuffer, sr); customTonalBuffer.makeCopyOf(newBuffer); }\n    else if (laneIndex == 3) { customLayerReplacer.setSample(newBuffer, sr); customLayerBuffer.makeCopyOf(newBuffer); }', text)

text = re.sub(r'void AnatomyAudioProcessor::clearCustomSampleFromUI\(bool isTransient\) noexcept\n\{', 'void AnatomyAudioProcessor::clearCustomSampleFromUI(int laneIndex) noexcept\n{', text)
text = re.sub(r'    if \(isTransient\)\s*\{\s*customTransReplacer\.clear\(\);\s*customTransBuffer\.setSize\(0, 0\);\s*\}\s*else\s*\{\s*customTonalReplacer\.clear\(\);\s*customTonalBuffer\.setSize\(0, 0\);\s*\}', '    if (laneIndex == 1) { customTransReplacer.clear(); customTransBuffer.setSize(0, 0); }\n    else if (laneIndex == 2) { customTonalReplacer.clear(); customTonalBuffer.setSize(0, 0); }\n    else if (laneIndex == 3) { customLayerReplacer.clear(); customLayerBuffer.setSize(0, 0); }', text)


with open('Source/PluginProcessor.cpp', 'w', encoding='utf-8') as f:
    f.write(text)
print('Done cpp')
