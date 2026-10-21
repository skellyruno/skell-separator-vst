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
    void setCircular(bool circular) { gain.setCircular(circular); }
private:
    SkellSeparatorAudioProcessor& processor;
    int stem;
    juce::String heading;
    TickKnob gain;
    RoundIconButton solo { RoundIconButton::Kind::solo };
    RoundIconButton mute { RoundIconButton::Kind::mute };
    StemMeter meter;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloAttachment, muteAttachment;
};
