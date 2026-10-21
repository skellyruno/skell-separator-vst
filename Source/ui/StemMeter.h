#pragma once
#include "Theme.h"
#include "../dsp/SpectralAnalyzer.h"
#include <array>

class StemMeter final : public juce::Component
{
public:
    StemMeter(SpectralAnalyzer&, int stemIndex);
    void refresh(bool bypassed, bool muted, float gain);
    void paint(juce::Graphics&) override;
private:
    SpectralAnalyzer& analyzer;
    int stem;
    std::array<float, SpectralAnalyzer::bands> smoothed {};
    bool dimmed = false;
};
