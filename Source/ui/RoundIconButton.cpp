#include "RoundIconButton.h"

RoundIconButton::RoundIconButton(Kind k) : Button("round"), kind(k)
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setClickingTogglesState(k == Kind::solo || k == Kind::mute);
    const char* description = k == Kind::solo ? "Solo stem" : k == Kind::mute ? "Mute stem"
                              : k == Kind::previous ? "Previous preset"
                              : k == Kind::next ? "Next preset" : "Open settings menu";
    setTooltip(description);
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
    g.setColour(juce::Colours::black.withAlpha(0.38f));
    g.fillEllipse(4.0f, 6.0f, 31.0f, 31.0f);
    g.setColour(hot ? Theme::accent.darker(down ? 0.17f : 0.0f)
                    : juce::Colour(0xff28282c).brighter(on ? 0.26f : (hover ? 0.12f : 0.0f)));
    g.fillEllipse(3.2f, 3.3f, 31.0f, 31.0f);
    g.setColour(dim ? Theme::accent.withAlpha(0.72f) : juce::Colours::white.withAlpha(0.09f));
    g.drawEllipse(4.2f, 4.3f, 29.0f, 29.0f, 1.1f);
    g.setColour(hot ? juce::Colours::white : (dim ? Theme::accent : Theme::text));
    if (kind == Kind::solo || kind == Kind::mute)
    {
        g.setFont(Theme::mono(16.5f));
        g.drawText(kind == Kind::solo ? "S" : "M", 2, 1, 34, 33,
                   juce::Justification::centred, false);
    }
    else if (kind == Kind::menu)
    {
        // Three filled circles, not font-dependent Unicode ellipsis glyphs.
        for (float x : { 12.0f, 19.0f, 26.0f })
            g.fillEllipse(x - 1.6f, 17.4f, 3.2f, 3.2f);
    }
    else
    {
        // Vectors avoid the mojibake / missing-font arrows in Windows hosts.
        const bool next = kind == Kind::next;
        const float direction = next ? 1.0f : -1.0f;
        juce::Path chevron;
        chevron.startNewSubPath(19.0f - direction * 3.2f, 12.7f);
        chevron.lineTo(19.0f + direction * 3.2f, 19.0f);
        chevron.lineTo(19.0f - direction * 3.2f, 25.3f);
        g.strokePath(chevron, juce::PathStrokeType(2.15f,
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}
