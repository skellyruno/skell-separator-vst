#pragma once
#include "Theme.h"

class SoftButton final : public juce::Button
{
public:
    explicit SoftButton(juce::String label);
    void setCaption(juce::String newCaption) { if (caption != newCaption) { caption = std::move(newCaption); repaint(); } }
    void setAccentEnabled(bool v) { if (accentEnabled != v) { accentEnabled = v; repaint(); } }
    void setOutlineEnabled(bool v) { if (outlined != v) { outlined = v; repaint(); } }
    void paintButton(juce::Graphics&, bool, bool) override;
private:
    juce::String caption;
    bool accentEnabled = false, outlined = false;
};
