// ==========================================
// File: ArcDial.h
// ANATOMY パステル・アークダイアル LookAndFeel
// ==========================================
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class ArcDialLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ArcDialLookAndFeel();
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& slider) override;
};
