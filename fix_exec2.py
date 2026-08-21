import re

with open('Source/PluginProcessor.cpp', 'r', encoding='utf-8') as f:
    text = f.read()

# I will find the OfflineMixRenderer::executeRender() block precisely.
start_idx = text.find('void OfflineMixRenderer::executeRender()')
end_idx = text.find('void AnatomyAudioProcessor::reanalyzeSelectedArea()')
if start_idx == -1 or end_idx == -1:
    print('Could not find block!')
else:
    block = text[start_idx:end_idx]
    
    # 1. replace localTrans, localTonal
    b1 = '''    juce::AudioBuffer<float> localTrans, localTonal, localLayer;
    double sr = 44100.0;

    {
        const juce::ScopedLock sl(processor.lock);
        localTrans.makeCopyOf(processor.customTransBuffer.getNumSamples() > 0 ? processor.customTransBuffer : processor.transBufferThread);
        localTonal.makeCopyOf(processor.customTonalBuffer.getNumSamples() > 0 ? processor.customTonalBuffer : processor.tonalBufferThread);
        localLayer.makeCopyOf(processor.customLayerBuffer);
        sr = processor.fileSampleRate;
    }

    const int transSamples = localTrans.getNumSamples();
    const int tonalSamples = localTonal.getNumSamples();
    const int layerSamples = localLayer.getNumSamples();
    const int maxSamples = std::max({transSamples, tonalSamples, layerSamples});
    if (maxSamples == 0) return;

    if (localTrans.getNumChannels() <= 0 && localTonal.getNumChannels() <= 0 && localLayer.getNumChannels() <= 0) return;

    juce::AudioBuffer<float> workTrans(2, maxSamples);
    juce::AudioBuffer<float> workTonal(2, maxSamples);
    juce::AudioBuffer<float> workLayer(2, maxSamples);
    workTrans.clear(); workTonal.clear(); workLayer.clear();

    for (int ch = 0; ch < 2; ++ch)
    {
        if (transSamples > 0 && ch < localTrans.getNumChannels()) workTrans.copyFrom(ch, 0, localTrans, ch, 0, transSamples);
        if (tonalSamples > 0 && ch < localTonal.getNumChannels()) workTonal.copyFrom(ch, 0, localTonal, ch, 0, tonalSamples);
        if (layerSamples > 0 && ch < localLayer.getNumChannels()) workLayer.copyFrom(ch, 0, localLayer, ch, 0, layerSamples);
    }'''
    block = re.sub(r'    juce::AudioBuffer<float> localTrans.*?    for \(int ch = 0; ch < 2; \+\+ch\).*?    }', b1, block, flags=re.DOTALL)
    
    # 2. replace gain block
    b2 = '''    applyPitch(workTonal, tonalPitch);

    float transGain = std::pow(10.0f, processor.apvts.getRawParameterValue("transMixGain")->load() / 20.0f);
    float tonalGain = std::pow(10.0f, processor.apvts.getRawParameterValue("tonalMixGain")->load() / 20.0f);
    float layerGain = std::pow(10.0f, processor.apvts.getRawParameterValue("layerGain")->load() / 20.0f);
    
    float layerOffsetMs = processor.apvts.getRawParameterValue("layerOffset")->load();
    int layerOffsetSamples = static_cast<int>((layerOffsetMs / 1000.0f) * sr);

    juce::AudioBuffer<float> outputMix(2, maxSamples);
    juce::AudioBuffer<float> outTransRendered(2, maxSamples);
    juce::AudioBuffer<float> outTonalRendered(2, maxSamples);
    juce::AudioBuffer<float> outLayerRendered(2, maxSamples);
    outTransRendered.clear();
    outTonalRendered.clear();
    outLayerRendered.clear();'''
    block = re.sub(r'    applyPitch\(workTonal, tonalPitch\);.*?    outTonalRendered\.clear\(\);', b2, block, flags=re.DOTALL)

    # 3. replace applyEffectsOffline and mixing loop
    b3 = '''    processor.applyEffectsOffline(workTrans, TargetRoute::Transient, sr);
    processor.applyEffectsOffline(workTonal, TargetRoute::Tonal, sr);
    processor.applyEffectsOffline(workLayer, TargetRoute::Layer, sr);

    outputMix.clear();
    std::vector<float> ratios(maxSamples, 0.5f);

    int mode = processor.getSoloMode(); // 0=Mix, 1=TransSolo, 2=TonalSolo, 3=LayerSolo

    for (int s = 0; s < maxSamples; ++s)
    {
        float tL = (mode == 2 || mode == 3) ? 0.0f : workTrans.getSample(0, s) * transGain;
        float tR = (mode == 2 || mode == 3) ? 0.0f : workTrans.getSample(1, s) * transGain;
        float oL = (mode == 1 || mode == 3) ? 0.0f : workTonal.getSample(0, s) * tonalGain;
        float oR = (mode == 1 || mode == 3) ? 0.0f : workTonal.getSample(1, s) * tonalGain;
        
        float lL = 0.0f, lR = 0.0f;
        if (s >= layerOffsetSamples && (s - layerOffsetSamples) < maxSamples)
        {
            lL = (mode == 1 || mode == 2) ? 0.0f : workLayer.getSample(0, s - layerOffsetSamples) * layerGain;
            lR = (mode == 1 || mode == 2) ? 0.0f : workLayer.getSample(1, s - layerOffsetSamples) * layerGain;
        }

        outputMix.setSample(0, s, tL + oL + lL);
        outputMix.setSample(1, s, tR + oR + lR);

        outTransRendered.setSample(0, s, tL);
        outTransRendered.setSample(1, s, tR);
        outTonalRendered.setSample(0, s, oL);
        outTonalRendered.setSample(1, s, oR);
        outLayerRendered.setSample(0, s, lL);
        outLayerRendered.setSample(1, s, lR);

        float eTrans = tL*tL + tR*tR;
        float eTonal = oL*oL + oR*oR;
        float eLayer = lL*lL + lR*lR;
        float sumE = eTrans + eTonal + eLayer;
        ratios[s] = (sumE > 1e-6f) ? eTrans / sumE : 0.5f;
    }

    processor.applyEffectsOffline(outputMix, TargetRoute::FullMix, sr);

    {
        const juce::ScopedLock sl(renderLock);
        renderedFullMix.makeCopyOf(outputMix);
        renderedTransient.makeCopyOf(outTransRendered);
        renderedTonal.makeCopyOf(outTonalRendered);
        renderedLayer.makeCopyOf(outLayerRendered);
        componentRatios = std::move(ratios);
    }'''
    block = re.sub(r'    processor\.applyEffectsOffline\(workTrans.*?componentRatios = std::move\(ratios\);\n    }', b3, block, flags=re.DOTALL)
    
    text = text[:start_idx] + block + text[end_idx:]
    with open('Source/PluginProcessor.cpp', 'w', encoding='utf-8') as f:
        f.write(text)
    print("done!")
