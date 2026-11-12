#pragma once
#include "Theme.h"
#include "TickKnob.h"
#include "StemMeter.h"
#include "RoundIconButton.h"
#include "../PluginProcessor.h"
#include <memory>

class StemColumn final : public juce::Component
{
public:
    StemColumn(SkellSeparatorAudioProcessor&, int stemIndex,
               juce::String title, const juce::String& stemId, float defaultDb);
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh(bool active, bool bypassed);
    void setCircular(bool circular) { gain.setCircular(circular); liveGain.setCircular(circular); }
    void setLiveMode(bool live);
    bool isLiveMode() const noexcept { return liveMode; }
private:
    SkellSeparatorAudioProcessor& processor;
    int stem;
    bool panelAudible = true, panelActive = false, panelSolo = false, panelBypassed = false;
    juce::String heading, separateHeading, liveHeading;
    bool liveMode = false, liveModeInitialized = false;
    TickKnob gain;
    TickKnob liveGain { 0.0f };
    RoundIconButton solo { RoundIconButton::Kind::solo };
    RoundIconButton mute { RoundIconButton::Kind::mute };
    StemMeter meter;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment, liveAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloAttachment, muteAttachment;
};
