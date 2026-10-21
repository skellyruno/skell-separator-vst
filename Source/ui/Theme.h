#pragma once
#include <JuceHeader.h>
#include <cmath>

namespace Theme
{
    constexpr float designW = 1047.0f;
    constexpr float designH = 597.0f;
    constexpr float columnY = 109.0f;
    constexpr float columnW = 310.0f;
    constexpr float columnH = 475.0f;
    constexpr float centres[3] { 200.0f, 518.0f, 836.0f };
    inline const juce::Colour background { 0xff141416 };
    inline const juce::Colour text { 0xffc8c8cc };
    inline const juce::Colour textDim { 0xff8b8b91 };
    inline const juce::Colour accent { 0xffff4a14 };
    inline const juce::Colour ticks { 0xffececee };
    inline const juce::Colour ticksOff { 0xff55565c };
    inline const juce::Colour navy { 0xff2a3050 };
    juce::Font mono(float pixels);
    juce::Font display(float pixels);
    inline juce::Point<float> radial(juce::Point<float> c, float r, float a)
    {
        return { c.x + r * std::sin(a), c.y - r * std::cos(a) };
    }
    inline juce::Path arc(juce::Point<float> c, float r, float a0, float a1)
    {
        juce::Path p;
        p.addCentredArc(c.x, c.y, r, r, 0, a0, a1, true);
        return p;
    }
}
