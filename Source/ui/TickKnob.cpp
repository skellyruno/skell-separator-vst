#include "TickKnob.h"
#include <cmath>

TickKnob::TickKnob(float defaultValue) : defaultDb(defaultValue)
{
    setRange(-40.0, 40.0, 0.1);
    setSliderStyle(juce::Slider::RotaryVerticalDrag);
    setRotaryParameters(-juce::MathConstants<float>::pi * 0.75f,
                        juce::MathConstants<float>::pi * 0.75f, true);
    setTextBoxStyle(juce::Slider::TextBoxBelow, false, 146, 29);
    setTextValueSuffix(" dB");
    textFromValueFunction = [](double v) {
        return v <= -39.95 ? juce::String("-inf dB") : juce::String(v, 1) + " dB";
    };
    valueFromTextFunction = [](const juce::String& str) {
        return str.containsIgnoreCase("inf") ? -40.0 : str.getDoubleValue();
    };
    setDoubleClickReturnValue(true, defaultDb);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    renderFace();
}

void TickKnob::setCircular(bool value)
{
    auto style = value ? juce::Slider::Rotary : juce::Slider::RotaryVerticalDrag;
    if (getSliderStyle() != style) setSliderStyle(style);
}

void TickKnob::renderFace()
{
    face = juce::Image(juce::Image::ARGB, 216, 216, true);
    juce::Graphics g(face);
    const juce::Point<float> c { 108.0f, 108.0f };
    constexpr float radius = 101.0f;
    juce::Path clipping;
    clipping.addEllipse(c.x - radius, c.y - radius, radius * 2, radius * 2);
    g.reduceClipRegion(clipping);
    for (int i = 0; i < 240; ++i)
    {
        const float a1 = juce::MathConstants<float>::twoPi * i / 240.0f;
        const float a2 = juce::MathConstants<float>::twoPi * (i + 1.04f) / 240.0f;
        const float beam = 0.5f + 0.5f * std::cos(a1 * 2.0f + 0.65f);
        const float groove = 0.5f + 0.5f * std::cos(a1 * 10.0f + 0.15f);
        const float bright = 0.17f * beam + 0.055f * groove;
        const auto colour = juce::Colour::fromRGB(
            static_cast<juce::uint8>(29 + bright * 48),
            static_cast<juce::uint8>(34 + bright * 58),
            static_cast<juce::uint8>(55 + bright * 75));
        juce::Path wedge;
        wedge.startNewSubPath(c);
        wedge.lineTo(c.x + radius * std::sin(a1), c.y - radius * std::cos(a1));
        wedge.lineTo(c.x + radius * std::sin(a2), c.y - radius * std::cos(a2));
        wedge.closeSubPath();
        g.setColour(colour);
        g.fillPath(wedge);
    }
    juce::ColourGradient shade(juce::Colours::black.withAlpha(0.04f), 56, 40,
                               juce::Colours::black.withAlpha(0.44f), 165, 201, false);
    g.setGradientFill(shade);
    g.fillEllipse(7.0f, 7.0f, 202.0f, 202.0f);
}

void TickKnob::paint(juce::Graphics& g)
{
    g.addTransform(juce::AffineTransform::scale(getWidth() / 260.0f, getHeight() / 331.0f));
    const juce::Point<float> centre { 130.0f, 151.0f };
    const float fraction = static_cast<float>((getValue() + 40.0) / 80.0);
    const float from = -0.75f * juce::MathConstants<float>::pi;
    const float sweep = 1.5f * juce::MathConstants<float>::pi;
    for (int i = 0; i < 55; ++i)
    {
        const bool major = i % 5 == 0 || i == 27;
        const float a = from + sweep * static_cast<float>(i) / 54.0f;
        const auto a0 = Theme::radial(centre, major ? 117.0f : 122.0f, a);
        const auto a1 = Theme::radial(centre, i == 27 ? 132.0f : 129.0f, a);
        g.setColour((i / 54.0f <= fraction ? Theme::ticks : Theme::ticksOff)
                    .withAlpha(i / 54.0f <= fraction ? 0.85f : 0.56f));
        if (i == 0 || i == 54)
            g.fillEllipse(a1.x - 1.7f, a1.y - 1.7f, 3.4f, 3.4f);
        else
            g.drawLine(a0.x, a0.y, a1.x, a1.y, major ? 2.4f : 1.5f);
    }
    for (int j = 10; j > 0; --j)
    {
        g.setColour(juce::Colours::black.withAlpha(0.013f));
        const float r = 103.0f + j * 0.7f;
        g.fillEllipse(centre.x - r + 2.0f, centre.y - r + 5.0f, r * 2, r * 2);
    }
    g.setColour(juce::Colour(0xff12131b));
    g.fillEllipse(centre.x - 105, centre.y - 105, 210, 210);
    g.drawImage(face, juce::Rectangle<float>(centre.x - 104, centre.y - 104, 208, 208));
    // Brushed navy rim with a small upper highlight: readable at 75% scale.
    g.setColour(juce::Colour(0xff515774).withAlpha(0.60f));
    g.drawEllipse(centre.x - 101, centre.y - 101, 202, 202, 1.1f);
    g.setColour(juce::Colour(0xff687095).withAlpha(0.15f));
    g.strokePath(Theme::arc(centre, 100.0f, -0.9f, 0.9f), juce::PathStrokeType(1.2f));
    if (active && !muted && signal > 0.006f)
    {
        const auto glowPath = Theme::arc(centre, 103.0f, -1.16f, 1.12f);
        for (int j = 12; j >= 3; j -= 3)
        {
            g.setColour(Theme::accent.withAlpha(juce::jmin(0.055f, signal * 0.026f)));
            g.strokePath(glowPath, juce::PathStrokeType(static_cast<float>(j),
                         juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour(Theme::accent.withAlpha(juce::jmin(0.95f, signal * 0.9f)));
        g.strokePath(glowPath, juce::PathStrokeType(3.6f));
    }
    const auto pointer = Theme::radial(centre, 87.0f, from + sweep * fraction);
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.fillEllipse(pointer.x - 11, pointer.y - 11, 22, 22);
    g.setColour(muted ? juce::Colour(0xff85868c) : juce::Colour(0xfff4f4f6));
    g.fillEllipse(pointer.x - 5.5f, pointer.y - 5.5f, 11, 11);
}

void TickKnob::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (wheel.deltaY != 0)
        setValue(getValue() + (wheel.deltaY > 0.0f ? 0.5 : -0.5), juce::sendNotificationSync);
}

void TickKnob::mouseDown(const juce::MouseEvent& event)
{
    if (event.mods.isRightButtonDown())
    {
        showTextBox();
        return;
    }
    juce::Slider::mouseDown(event);
}
