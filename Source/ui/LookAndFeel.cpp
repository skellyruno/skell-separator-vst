#include "LookAndFeel.h"

SeparatorLookAndFeel::SeparatorLookAndFeel()
{
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff202024));
    setColour(juce::PopupMenu::textColourId, Theme::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, Theme::accent.withAlpha(0.23f));
    setColour(juce::TextEditor::textColourId, Theme::text);
    setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff202024));
    setColour(juce::TextEditor::highlightColourId, Theme::accent.withAlpha(0.28f));
    setColour(juce::Label::textColourId, Theme::text);
    setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxTextColourId, Theme::text);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

void SeparatorLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int w, int h)
{
    g.fillAll(juce::Colour(0xff202024));
    g.setColour(Theme::ticksOff.withAlpha(0.45f));
    g.drawRect(0, 0, w, h, 1);
}

void SeparatorLookAndFeel::drawPopupMenuItem(juce::Graphics& g,
    const juce::Rectangle<int>& bounds, bool separator, bool active,
    bool highlighted, bool ticked, bool, const juce::String& name,
    const juce::String& shortcut, const juce::Drawable*, const juce::Colour*)
{
    if (separator)
    {
        g.setColour(Theme::ticksOff);
        g.drawLine(static_cast<float>(bounds.getX() + 12), static_cast<float>(bounds.getCentreY()),
                   static_cast<float>(bounds.getRight() - 12), static_cast<float>(bounds.getCentreY()));
        return;
    }
    if (highlighted)
    {
        g.setColour(Theme::accent.withAlpha(0.23f));
        g.fillRoundedRectangle(bounds.reduced(3).toFloat(), 4.0f);
    }
    g.setColour(active ? Theme::text : Theme::textDim.withAlpha(0.5f));
    g.setFont(Theme::mono(13.0f));
    g.drawText((ticked ? "  * " : "    ") + name, bounds.reduced(6), juce::Justification::centredLeft);
    g.drawText(shortcut, bounds.reduced(10), juce::Justification::centredRight);
}

// The Slider-owned Label is the readout: no rectangular outlined text box.
// JUCE retains its normal double-click/right-click editable text behaviour.
void SeparatorLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label)
{
    if (label.isBeingEdited()) return; // Do not paint over the numeric text editor.
    g.setColour(label.findColour(juce::Label::textColourId));
    g.setFont(getLabelFont(label));
    g.drawFittedText(label.getText(), label.getLocalBounds().reduced(2),
                     juce::Justification::centred, 1, 0.9f);
}
