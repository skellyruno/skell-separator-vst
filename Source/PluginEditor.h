#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ui/LookAndFeel.h"
#include "ui/StemColumn.h"
#include "ui/HeaderBar.h"

class SkellSeparatorAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                 private juce::Timer
{
public:
    explicit SkellSeparatorAudioProcessorEditor(SkellSeparatorAudioProcessor&);
    ~SkellSeparatorAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    void showMenu();
    void chooseQuality(int index);
    void applyScale(int percent);
    SkellSeparatorAudioProcessor& processor;
    SeparatorLookAndFeel style;
    juce::ComponentBoundsConstrainer constrainer;
    HeaderBar header;
    StemColumn ambience, voice, reverb;
    float visualActivity = 0.0f;
    juce::uint32 lastBlockSeen = 0;
    int ticksWithoutAudio = 100;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SkellSeparatorAudioProcessorEditor)
};
