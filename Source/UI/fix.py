import re

with open("LayerLaneView.h", "r", encoding="utf-8") as f:
    text = f.read()

# basic string replacement
text = text.replace("TransientLaneView", "LayerLaneView")
text = text.replace("transient", "layer")
text = text.replace("Transient", "Layer")
text = text.replace("accentLayer", "peach")
text = text.replace("TRANSIENT", "LAYER")

text = text.replace("waveform.setLaneProperties(p, 1);", "waveform.setLaneProperties(p, 3);")
text = text.replace("processor.clearCustomSampleFromUI(true);", "processor.clearCustomSampleFromUI(3);")
text = text.replace("processor.setFadeFromUI(true, 0.0f, 0.0f, 0.0f, 0.0f);", "processor.setFadeFromUI(3, 0.0f, 0.0f, 0.0f, 0.0f);")
text = text.replace("processor.transEndOffsetMs", "processor.layerEndOffsetMs")
text = text.replace("processor.createTemporaryWavForExport(1)", "processor.createTemporaryWavForExport(3)")
text = text.replace("processor.setLaneSolo(true,", "processor.setLaneSolo(3,")

# fix knob definitions
knob_code = """
        setupKnob(knobOffset, lblOffset, "OFFSET (ms)", AnatomyColors::peach);
        setupKnob(knobGain,   lblGain,   "GAIN (dB)",   AnatomyColors::peach);

        attachOffset = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.apvts, "layerOffset", knobOffset);
        attachGain = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.apvts, "layerGain", knobGain);
"""

# remove old attach
text = re.sub(r'setupKnob\(knobClickHold.*?attachGain = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>\(\s*processor\.apvts,\s*"layerGain",\s*knobGain\);', knob_code, text, flags=re.DOTALL)

# remove 3rd knob
text = text.replace("knobClickHold", "knobOffset")
text = text.replace("lblClickHold", "lblOffset")
text = text.replace("attachClickHold", "attachOffset")

text = text.replace("knobPitch", "knobGain")
text = text.replace("lblPitch", "lblGain")
text = text.replace("attachPitch", "attachGain")

text = text.replace("ValueKnob knobGain;\n    ValueKnob knobGain;", "ValueKnob knobGain;")
text = text.replace("juce::Label lblGain;\n    juce::Label lblGain;", "juce::Label lblGain;")
text = text.replace("std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachGain;\n    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachGain;", "std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachGain;")


with open("LayerLaneView.h", "w", encoding="utf-8") as f:
    f.write(text)
