#include "SoftButton.h"

SoftButton::SoftButton(juce::String label) : Button(label), caption(std::move(label))
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void SoftButton::paintButton(juce::Graphics& g, bool hover, bool pressed)
{
    g.addTransform(juce::AffineTransform::scale(getWidth() / 120.0f, getHeight() / 42.0f));
    const auto r = juce::Rectangle<float>(5.0f, 5.0f, 110.0f, 31.0f);
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillRoundedRectangle(r.translated(1.5f, 3.5f), 16.0f);
    if (accentEnabled)
    {
        for (int i = 8; i > 0; --i)
        {
            g.setColour(Theme::accent.withAlpha(0.008f + (8 - i) * 0.002f));
            g.fillRoundedRectangle(r.expanded(static_cast<float>(i)), 20.0f);
        }
        g.setColour(Theme::accent.brighter(hover ? 0.10f : 0.0f).darker(pressed ? 0.15f : 0.0f));
    }
    else g.setColour(juce::Colour(0xff252427).brighter(hover ? 0.08f : 0.0f));
    g.fillRoundedRectangle(r, 16.0f);
    g.setColour(outlined ? Theme::accent.withAlpha(0.66f) : juce::Colours::white.withAlpha(0.075f));
    g.drawRoundedRectangle(r.reduced(0.5f), 16.0f, 1.1f);
    g.setFont(Theme::mono(16.0f));
    g.setColour(accentEnabled ? juce::Colours::white : Theme::text);
    g.drawText(caption, r, juce::Justification::centred, false);
}
