// ==========================================
// File: DragExportButton.h
// DAW/デスクトップへ安全にD&D可能なエクスポートボタン (SafePointer & OLE 安全対応版)
// ==========================================
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ColorPalette.h"
#include <functional>

class DragExportButton final : public juce::TextButton
{
public:
    DragExportButton(const juce::String& text = "EXPORT", juce::Colour accent = juce::Colours::white)
        : juce::TextButton(text), accentColour(accent)
    {
        setColour(juce::TextButton::buttonColourId, AnatomyColors::knobTrack);
        setColour(juce::TextButton::textColourOffId, accentColour);
        setTooltip("Drag & Drop to DAW track or folder");
    }

    void setFileGenerator(std::function<juce::File()> generator)
    {
        getFile = std::move(generator);
    }

    void setAccentColour(juce::Colour c)
    {
        accentColour = c;
        setColour(juce::TextButton::textColourOffId, accentColour);
        repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        juce::TextButton::mouseDown(e);
        dragStarted = false;
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        juce::TextButton::mouseDrag(e);

        if (!dragStarted && e.getDistanceFromDragStart() > 4)
        {
            dragStarted = true;
            if (getFile)
            {
                juce::File wav = getFile();
                if (wav.existsAsFile())
                {
                    juce::StringArray files;
                    files.add(wav.getFullPathName());

                    // OLE ドロップ完了後に安全に復帰するための SafePointer
                    juce::Component::SafePointer<DragExportButton> safeThis(this);

                    juce::DragAndDropContainer::performExternalDragDropOfFiles(
                        files, false, nullptr,
                        [safeThis]()
                        {
                            if (safeThis != nullptr)
                                safeThis->dragStarted = false;
                        });
                }
                else
                {
                    dragStarted = false;
                }
            }
        }
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        juce::TextButton::mouseUp(e);
        dragStarted = false;
    }

private:
    std::function<juce::File()> getFile;
    juce::Colour accentColour;
    bool dragStarted = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DragExportButton)
};
