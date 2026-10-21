#include "RoundIconButton.h"

RoundIconButton::RoundIconButton(Kind k) : Button("round"), kind(k)
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setClickingTogglesState(k == Kind::solo || k == Kind::mute);
}

void RoundIconButton::paintButton(juce::Graphics& g, bool hover, bool down)
{
    g.addTransform(juce::AffineTransform::scale(getWidth() / 38.0f, getHeight() / 38.0f));
    const bool on = getToggleState();
    const bool hot = on && (kind == Kind::solo);
    const bool dim = on && (kind == Kind::mute);
    if (hot && glowing)
    {
        for (int i = 7; i >= 1; --i)
        {
            g.setColour(Theme::accent.withAlpha(0.018f));
            g.fillEllipse(3.5f - i * 0.3f, 3.5f - i * 0.3f, 31.0f + i * 0.6f, 31.0f + i * 0.6f);
        }
    }
    g.setColour(juce::Colours::black.withAlpha(0.43f));
    g.fillEllipse(4.0f, 6.5f, 31.0f, 31.0f);
    g.setColour(hot ? Theme::accent.darker(down ? 0.17f : 0.0f)
                    : juce::Colour(0xff252528).brighter(on ? 0.26f : (hover ? 0.09f : 0.0f)));
    g.fillEllipse(3.2f, 3.3f, 31.0f, 31.0f);
    g.setColour(dim ? Theme::accent.withAlpha(0.69f) : juce::Colours::white.withAlpha(0.055f));
    g.drawEllipse(4.2f, 4.3f, 29.0f, 29.0f, 1.1f);
    g.setColour(hot ? juce::Colours::white : (dim ? Theme::accent : Theme::text));
    g.setFont(Theme::mono(kind == Kind::solo || kind == Kind::mute ? 16.5f : 21.0f));
    const juce::String label = kind == Kind::solo ? "S" : kind == Kind::mute ? "M"
                  : kind == Kind::previous ? "‹" : kind == Kind::next ? "›" : "···";
    g.drawText(label, 2, 1, 34, 33, juce::Justification::centred, false);
}
