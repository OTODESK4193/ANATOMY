import os
import re

effects = ['BitCrusher.h', 'NoiseGenerator.h', 'OTT_Multiband.h', 'GlueCompressor.h', 'Limiter.h', 'TransientShaper.h']
for e in effects:
    path = f'Source/DSP/Effects/{e}'
    with open(path, 'r', encoding='utf-8') as f:
        text = f.read()
    
    # Fix the literal \\n to actual newline
    text = text.replace(r'\n', '\n')
    text = text.replace('float getIndexedParameter(int index) const noexcept override { return 0.0f; }    void setIndexedParameter', 'float getIndexedParameter(int index) const noexcept override { return 0.0f; }\n    void setIndexedParameter')
    
    with open(path, 'w', encoding='utf-8') as f:
        f.write(text)


with open('Source/PluginProcessor.cpp', 'r', encoding='utf-8') as f:
    text = f.read()

# Fix SharedSampleData at 1189
text = text.replace('auto newSampleData = std::make_shared<SharedSampleData>(std::move(tT), std::move(tO), fileSampleRate);', 'juce::AudioBuffer<float> tL; tL.makeCopyOf(customLayerBuffer);\n                auto newSampleData = std::make_shared<SharedSampleData>(std::move(tT), std::move(tO), std::move(tL), fileSampleRate);')

# Fix generateVoiceSample at 1400
text = text.replace('generateVoiceSample(dummyVoice, vTransL, vTransR, vTonalL, vTonalR, clickHold, clickCurve, transScale, tonalScale, currentSampleRate);', 'float vLayerL = 0.0f, vLayerR = 0.0f;\n                generateVoiceSample(dummyVoice, vTransL, vTransR, vTonalL, vTonalR, vLayerL, vLayerR, clickHold, clickCurve, transScale, tonalScale, currentSampleRate);')
text = text.replace('mixBuf.setSample(0, s, vTransL + vTonalL);', 'mixBuf.setSample(0, s, vTransL + vTonalL + vLayerL);')
text = text.replace('mixBuf.setSample(1, s, vTransR + vTonalR);', 'mixBuf.setSample(1, s, vTransR + vTonalR + vLayerR);')

# Fix remaining satAsym
text = re.sub(r'\s*if\s*\(cache\.satAsym\)\s*cache\.sat->setAsymmetry\(cache\.satAsym->load\(\)\);', '', text)
text = re.sub(r'cache\.satAsym = [^;]+;', '', text)

# Fix lane methods
text = re.sub(r'void AnatomyAudioProcessor::setFadeFromUI\(bool isTransient', 'void AnatomyAudioProcessor::setFadeFromUI(int laneIndex', text)
text = re.sub(r'void AnatomyAudioProcessor::getFadeForUI\(bool isTransient', 'void AnatomyAudioProcessor::getFadeForUI(int laneIndex', text)
text = re.sub(r'void AnatomyAudioProcessor::setLaneSolo\(bool isTransient', 'void AnatomyAudioProcessor::setLaneSolo(int laneIndex', text)
text = re.sub(r'bool AnatomyAudioProcessor::isLaneSolo\(bool isTransient\)', 'bool AnatomyAudioProcessor::isLaneSolo(int laneIndex)', text)
text = re.sub(r'void AnatomyAudioProcessor::storeCustomSampleFromUI\(bool isTransient', 'void AnatomyAudioProcessor::storeCustomSampleFromUI(int laneIndex', text)
text = re.sub(r'void AnatomyAudioProcessor::clearCustomSampleFromUI\(bool isTransient', 'void AnatomyAudioProcessor::clearCustomSampleFromUI(int laneIndex', text)

with open('Source/PluginProcessor.cpp', 'w', encoding='utf-8') as f:
    f.write(text)

with open('Source/PluginProcessor.h', 'r', encoding='utf-8') as f:
    text2 = f.read()
text2 = text2.replace('Lane lanes[3];', 'Lane lanes[4];')
with open('Source/PluginProcessor.h', 'w', encoding='utf-8') as f:
    f.write(text2)
