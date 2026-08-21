// ==========================================
// File: ValueKnob.h
// 右クリックで数値を直接入力できるロータリースライダー
// ==========================================
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ColorPalette.h"

class ValueKnob : public juce::Slider
{
public:
    ValueKnob() = default;

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown())
        {
            showTextEntry();
            return;
        }
        juce::Slider::mouseDown(e);
    }

private:
    void showTextEntry()
    {
        auto editor = std::make_unique<juce::TextEditor>();
        editor->setSize(96, 26);
        editor->setJustification(juce::Justification::centred);
        editor->setColour(juce::TextEditor::backgroundColourId, AnatomyColors::panel);
        editor->setColour(juce::TextEditor::textColourId, AnatomyColors::text);
        editor->setColour(juce::TextEditor::outlineColourId, AnatomyColors::panelLine);
        editor->setColour(juce::TextEditor::focusedOutlineColourId, AnatomyColors::mint.withAlpha(0.7f));
        editor->setInputRestrictions(12, "0123456789.-");
        editor->setText(juce::String(getValue(), 2), juce::dontSendNotification);
        editor->setSelectAllWhenFocused(true);
        editor->setWantsKeyboardFocus(true);

        auto* edPtr = editor.get();
        auto& box = juce::CallOutBox::launchAsynchronously(std::move(editor),
                                                           getScreenBounds(), nullptr);

        edPtr->onReturnKey = [this, edPtr, &box]
        {
            const double v = edPtr->getText().getDoubleValue();
            setValue(v, juce::sendNotificationSync);
            box.dismiss();
        };
        edPtr->onEscapeKey = [&box] { box.dismiss(); };
        edPtr->onFocusLost = [&box] { box.dismiss(); };

        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<juce::TextEditor>(edPtr)]
        {
            if (safe != nullptr) safe->grabKeyboardFocus();
        });
    }
};
