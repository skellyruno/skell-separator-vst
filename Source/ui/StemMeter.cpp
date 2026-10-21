#include "StemMeter.h"
#include <cmath>

StemMeter::StemMeter(SpectralAnalyzer& a, int s) : analyzer(a), stem(s)
{
    setInterceptsMouseClicks(false, false);
}

void StemMeter::refresh(bool bypassed, bool muted, float gain)
{
    dimmed = bypassed || muted;
    for (int i = 0; i < SpectralAnalyzer::bands; ++i)
    {
        const float raw = dimmed ? 0.0f : analyzer.getBand(stem, i) * gain;
        const float target = juce::jlimit(0.0f, 1.0f,
                           std::log1p(raw * 0.13f) / std::log1p(2.0f));
        const size_t idx = static_cast<size_t>(i);
        smoothed[idx] += (target > smoothed[idx] ? 0.82f : 0.12f)
                       * (target - smoothed[idx]);
    }
    repaint();
}

void StemMeter::paint(juce::Graphics& g)
{
    g.addTransform(juce::AffineTransform::scale(getWidth() / 168.0f, getHeight() / 40.0f));
    g.setColour(Theme::textDim.withAlpha(0.38f));
    g.drawLine(2, 34.0f, 166.0f, 34.0f, 1.7f);
    juce::Path silhouette;
    silhouette.startNewSubPath(2.0f, 34.0f);
    bool any = false;
    for (int i = 0; i < SpectralAnalyzer::bands; ++i)
    {
        const float x = 2.0f + 164.0f * i / (SpectralAnalyzer::bands - 1);
        const float h = 30.0f * smoothed[static_cast<size_t>(i)];
        any |= h > 0.4f;
        silhouette.lineTo(x, 34.0f - h);
    }
    silhouette.lineTo(166.0f, 34.0f);
    silhouette.closeSubPath();
    if (any)
    {
        g.setColour((stem == 1 ? juce::Colour(0xfff0f0f2) : juce::Colour(0xff8a8a90))
                    .withAlpha(dimmed ? 0.3f : 0.95f));
        g.fillPath(silhouette);
    }
}
