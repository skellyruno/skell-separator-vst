#pragma once
#include "Theme.h"
#include "SoftButton.h"
#include "RoundIconButton.h"
#include "../PluginProcessor.h"
#include <functional>
#include <memory>

// Compact, clickable engine selector: stable DSP or explicit experimental AI.
// Always shows the real selection/availability, not just the incoming level.
class EngineBadge final : public juce::Button
{
public:
    EngineBadge();
    void paintButton(juce::Graphics&, bool hovered, bool pressed) override;
    void updateState(bool selected, bool available, bool bypassed);
private:
    bool neuralSelected = false, modelAvailable = false, isBypassed = false;
};

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
    EngineBadge engine;
    RoundIconButton previous { RoundIconButton::Kind::previous };
    RoundIconButton next { RoundIconButton::Kind::next };
    RoundIconButton menu { RoundIconButton::Kind::menu };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    bool lastModelActive = false;
};
