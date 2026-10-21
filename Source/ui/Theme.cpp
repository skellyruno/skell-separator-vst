#include "Theme.h"

// Use native system fonts, rather than distributing third-party font binaries.
// The names are common on Windows and JUCE applies a safe default if missing.
juce::Font Theme::mono(float pixels)
{
    return juce::Font(juce::FontOptions("Consolas", pixels, juce::Font::plain));
}

juce::Font Theme::display(float pixels)
{
    return juce::Font(juce::FontOptions("Arial", pixels, juce::Font::bold));
}
