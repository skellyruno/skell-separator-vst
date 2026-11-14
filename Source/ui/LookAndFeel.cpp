#include "LookAndFeel.h"

SeparatorLookAndFeel::SeparatorLookAndFeel()
{
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff0c1112));
    setColour(juce::PopupMenu::textColourId, Theme::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, Theme::accent.withAlpha(0.14f));
    setColour(juce::TextEditor::textColourId, Theme::text);
    setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0c1112));
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
    g.fillAll(juce::Colour(0xff0c1112));
    g.setColour(juce::Colour(0xff293c36).withAlpha(0.98f));
    g.drawRect(0, 0, w, h, 1);
}

void SeparatorLookAndFeel::drawPopupMenuItem(juce::Graphics& g,
    const juce::Rectangle<int>& bounds, bool separator, bool active,
    bool highlighted, bool ticked, bool hasSubMenu, const juce::String& name,
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
        g.setColour(Theme::accent.withAlpha(0.14f));
        const auto selected = bounds.reduced(3).toFloat();
        g.fillRoundedRectangle(selected, 5.0f);
        g.setColour(Theme::accent.withAlpha(0.39f));
        g.drawRoundedRectangle(selected.reduced(0.4f), 4.7f, 1.0f);
    }
    g.setColour(active ? Theme::text : Theme::textDim.withAlpha(0.5f));
    g.setFont(Theme::mono(13.0f));
    if (ticked) {
        g.setColour(Theme::accent);
        g.fillRoundedRectangle(juce::Rectangle<float>(bounds.getX() + 12.0f,
            bounds.getCentreY() - 3.0f, 6.0f, 6.0f), 2.0f);
    }
    g.setColour(active ? Theme::text : Theme::textDim.withAlpha(0.5f));
    g.drawText(name, bounds.withTrimmedLeft(26).withTrimmedRight(hasSubMenu ? 30 : 12),
               juce::Justification::centredLeft, true);
    if (hasSubMenu) {
        const auto x = static_cast<float>(bounds.getRight() - 17);
        const auto y = static_cast<float>(bounds.getCentreY());
        g.setColour(active ? Theme::accent.withAlpha(0.86f) : Theme::textDim);
        g.drawLine(x - 3.0f, y - 5.0f, x + 2.0f, y, 1.4f);
        g.drawLine(x + 2.0f, y, x - 3.0f, y + 5.0f, 1.4f);
    }
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

void SeparatorLookAndFeel::drawPopupMenuSectionHeader(juce::Graphics& g,
    const juce::Rectangle<int>& bounds, const juce::String& title)
{
    g.setColour(Theme::accent.withAlpha(0.9f));
    g.fillRoundedRectangle(juce::Rectangle<float>(static_cast<float>(bounds.getX() + 12),
        static_cast<float>(bounds.getCentreY() - 5), 2.0f, 10.0f), 1.0f);
    g.setColour(juce::Colour(0xffeff7ef));
    g.setFont(Theme::mono(11.0f));
    g.drawFittedText(title, bounds.withTrimmedLeft(25).withTrimmedRight(9),
                     juce::Justification::centredLeft, 1);
}
