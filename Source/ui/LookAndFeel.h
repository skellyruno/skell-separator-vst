#pragma once
#include "Theme.h"

class SeparatorLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    SeparatorLookAndFeel();
    void drawLabel(juce::Graphics&, juce::Label&) override;
    void drawPopupMenuBackground(juce::Graphics&, int, int) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>&,
                           bool, bool, bool, bool, bool, const juce::String&,
                           const juce::String&, const juce::Drawable*, const juce::Colour*) override;
    juce::Font getLabelFont(juce::Label& label) override
    {
        return Theme::mono(juce::jmax(12.0f, label.getHeight() * 0.72f));
    }
};
