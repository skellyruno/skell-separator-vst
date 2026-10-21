#pragma once
#include "Theme.h"
#include "SoftButton.h"
#include "RoundIconButton.h"
#include "../PluginProcessor.h"
#include <functional>
#include <memory>

class HeaderBar final : public juce::Component
{
public:
    explicit HeaderBar(SkellSeparatorAudioProcessor&);
    std::function<void()> onMenu;
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh(bool active);
private:
    SkellSeparatorAudioProcessor& processor;
    SoftButton mode { "Stereo" }, bypass { "Bypass" };
    RoundIconButton previous { RoundIconButton::Kind::previous };
    RoundIconButton next { RoundIconButton::Kind::next };
    RoundIconButton menu { RoundIconButton::Kind::menu };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    bool lastModelActive = false;
};
