#pragma once
#include "Theme.h"

class RoundIconButton final : public juce::Button
{
public:
    enum class Kind { solo, mute, previous, next, menu };
    explicit RoundIconButton(Kind);
    void paintButton(juce::Graphics&, bool, bool) override;
    void setActiveGlow(bool v) { if (glowing != v) { glowing = v; repaint(); } }
private:
    Kind kind;
    bool glowing = false;
};
