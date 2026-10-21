#pragma once
#include "Theme.h"

class TickKnob final : public juce::Slider
{
public:
    explicit TickKnob(float defaultValue);
    void paint(juce::Graphics&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void setSignalLevel(float level) { if (std::abs(signal - level) > 0.002f) { signal = level; repaint(); } }
    void setMuted(bool m) { if (muted != m) { muted = m; repaint(); } }
    void setActive(bool a) { if (active != a) { active = a; repaint(); } }
    void setCircular(bool value);
private:
    void renderFace();
    juce::Image face;
    float signal = 0.0f;
    float defaultDb;
    bool muted = false, active = false;
};
