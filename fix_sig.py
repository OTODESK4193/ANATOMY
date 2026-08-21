import re

with open('Source/PluginProcessor.cpp', 'r', encoding='utf-8') as f:
    text = f.read()

sig = '''void AnatomyAudioProcessor::generateVoiceSample(VoiceState& voice,
    float& outTransL, float& outTransR, float& outTonalL, float& outTonalR, float& outLayerL, float& outLayerR,
    float clickHold, float clickCurve, float transScale, float tonalScale, double hostSampleRate) noexcept
{
    outTransL = 0.0f; outTransR = 0.0f; outTonalL = 0.0f; outTonalR = 0.0f; outLayerL = 0.0f; outLayerR = 0.0f;'''

text = re.sub(r'void AnatomyAudioProcessor::generateVoiceSample\(VoiceState& voice,\s*float& outTransL, float& outTransR, float& outTonalL, float& outTonalR,\s*float clickHold, float clickCurve, float transScale, float tonalScale, double hostSampleRate\) noexcept\s*\{\s*outTransL = 0\.0f; outTransR = 0\.0f; outTonalL = 0\.0f; outTonalR = 0\.0f;', sig, text)

# Layer processing inside generateVoiceSample:
# I need to add it near the end of generateVoiceSample, before the return.
layer_logic = '''
    // --- Layer Processing ---
    outLayerL = 0.0f; outLayerR = 0.0f;
    float layerOffsetMs = apvts.getRawParameterValue("layerOffset")->load();
    int layerOffsetSamples = static_cast<int>((layerOffsetMs / 1000.0f) * hostSampleRate);
    if (voice.samplesPlayed >= layerOffsetSamples)
    {
        int layerReadIndex = voice.samplesPlayed - layerOffsetSamples;
        customLayerReplacer.readSample(layerReadIndex, outLayerL, outLayerR);
        float layerFadeInEnv = 1.0f;
        float layerFadeOutEnv = 1.0f;
        
        float inMs = 1.0f, outMs = 10.0f, inTension = 0.0f, outTension = 0.0f;
        getFadeForUI(3, inMs, outMs, inTension, outTension);
        
        float inSamples = (inMs / 1000.0f) * hostSampleRate;
        if (inSamples > 1.0f && layerReadIndex < inSamples) {
            float phase = layerReadIndex / inSamples;
            layerFadeInEnv = juce::jmap(std::pow(phase, std::exp(inTension)), 0.0f, 1.0f);
        }
        
        float outSamples = (outMs / 1000.0f) * hostSampleRate;
        float fadeOutStartIdx = layerEndOffsetMs > 0.0f ? ((layerEndOffsetMs / 1000.0f) * hostSampleRate - outSamples) : (customLayerReplacer.getBuffer().getNumSamples() - outSamples);
        if (outSamples > 1.0f && layerReadIndex >= fadeOutStartIdx) {
            float phase = (layerReadIndex - fadeOutStartIdx) / outSamples;
            phase = juce::jlimit(0.0f, 1.0f, phase);
            layerFadeOutEnv = 1.0f - juce::jmap(std::pow(phase, std::exp(outTension)), 0.0f, 1.0f);
        }
        outLayerL *= (layerFadeInEnv * layerFadeOutEnv);
        outLayerR *= (layerFadeInEnv * layerFadeOutEnv);
    }
}
'''
# Replace the end of generateVoiceSample:
text = re.sub(r'    outTonalL \*= voice\.tonalFadeOutEnv;\s*outTonalR \*= voice\.tonalFadeOutEnv;\s*\}', '    outTonalL *= voice.tonalFadeOutEnv;\n    outTonalR *= voice.tonalFadeOutEnv;\n' + layer_logic, text)


# Update processBlock to mix layer correctly:
text = text.replace('buffer.addSample(0, s, mixedTransL + mixedTonalL);', 'buffer.addSample(0, s, mixedTransL + mixedTonalL + (vLayerL * smoothedLayerGain.getNextValue()));')
text = text.replace('buffer.addSample(1, s, mixedTransR + mixedTonalR);', 'buffer.addSample(1, s, mixedTransR + mixedTonalR + (vLayerR * smoothedLayerGain.getCurrentValue()));')

# smoothedLayerGain initialization:
# smoothedLayerGain.setTargetValue(...)
# smoothedLayerGain.applyGain(...)
text = text.replace('smoothedTonalGain.setTargetValue(std::pow(10.0f, apvts.getRawParameterValue("tonalMixGain")->load() / 20.0f));', 'smoothedTonalGain.setTargetValue(std::pow(10.0f, apvts.getRawParameterValue("tonalMixGain")->load() / 20.0f));\n        smoothedLayerGain.setTargetValue(std::pow(10.0f, apvts.getRawParameterValue("layerGain")->load() / 20.0f));')
text = text.replace('smoothedTonalGain.reset(currentSampleRate, 0.01);', 'smoothedTonalGain.reset(currentSampleRate, 0.01);\n    smoothedLayerGain.reset(currentSampleRate, 0.01);')

# Fix processBlock generateVoiceSample calls to include vLayerL and vLayerR
text = text.replace('generateVoiceSample(activeVoice, vTransL, vTransR, vTonalL, vTonalR, clickHold, clickCurve, transScale, tonalScale, currentSampleRate);', 'float vLayerL = 0.0f, vLayerR = 0.0f;\n            generateVoiceSample(activeVoice, vTransL, vTransR, vTonalL, vTonalR, vLayerL, vLayerR, clickHold, clickCurve, transScale, tonalScale, currentSampleRate);')
text = text.replace('generateVoiceSample(releasingVoices[i], vTransL, vTransR, vTonalL, vTonalR, clickHold, clickCurve, transScale, tonalScale, currentSampleRate);', 'float vLayerL = 0.0f, vLayerR = 0.0f;\n                generateVoiceSample(releasingVoices[i], vTransL, vTransR, vTonalL, vTonalR, vLayerL, vLayerR, clickHold, clickCurve, transScale, tonalScale, currentSampleRate);')

with open('Source/PluginProcessor.cpp', 'w', encoding='utf-8') as f:
    f.write(text)
print("Done sig")
