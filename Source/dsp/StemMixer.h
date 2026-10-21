#pragma once
#include <JuceHeader.h>
#include <array>
#include "Separator.h"

class StemMixer final
{
public:
    void prepare(double sampleRate) noexcept;
    // Only call while the host is stopped/preparing, not during playback.
    // Restores saved-state gain/solo/bypass without an unintended startup ramp.
    void snapToTargets() noexcept;
    void setTargets(const std::array<float, 3>& db, const std::array<bool, 3>& solo,
                    const std::array<bool, 3>& mute, bool bypass) noexcept;
    std::array<float, 2> mix(const Separator::Stems& stems,
                              const std::array<float, 2>& delayed) noexcept;
private:
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>, 3> gains;
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>, 3> gates;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> bypassCrossfade;
};
