// ==========================================
// File: GlowToggle.h
// LED点灯式トグルボタン（ON時にアクセント色でグロー）
// ==========================================
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ColorPalette.h"

class GlowToggle : public juce::ToggleButton
{
public:
    GlowToggle(const juce::String& text, juce::Colour accentColour)
        : juce::ToggleButton(text), accent(accentColour) {}

    void setAccentColour(juce::Colour c) noexcept { accent = c; repaint(); }

    void paintButton(juce::Graphics& g, bool highlighted, bool /*down*/) override
    {
        const auto r = getLocalBounds().toFloat().reduced(1.0f);
        const bool on = getToggleState();

        // 背景
        g.setColour(on ? accent.withAlpha(0.20f)
                       : (highlighted ? AnatomyColors::knobTrack.brighter(0.15f) : AnatomyColors::knobTrack));
        g.fillRoundedRectangle(r, 4.0f);

        // 枠
        g.setColour(on ? accent.withAlpha(0.9f) : AnatomyColors::panelLine);
        g.drawRoundedRectangle(r, 4.0f, on ? 1.5f : 1.0f);

        // LEDインジケーター
        const float ledX = r.getX() + 10.0f;
        const float ledY = r.getCentreY();
        if (on)
        {
            g.setColour(accent.withAlpha(0.40f)); // グロー
            g.fillEllipse(ledX - 6.0f, ledY - 6.0f, 12.0f, 12.0f);
        }
        g.setColour(on ? accent : AnatomyColors::textDim.withAlpha(0.45f));
        g.fillEllipse(ledX - 3.0f, ledY - 3.0f, 6.0f, 6.0f);

        // テキスト
        g.setColour(on ? AnatomyColors::text : AnatomyColors::textDim);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText(getButtonText(), (int)ledX + 8, 0, getWidth() - (int)ledX - 10, getHeight(),
                   juce::Justification::centredLeft);
    }

private:
    juce::Colour accent;
};
