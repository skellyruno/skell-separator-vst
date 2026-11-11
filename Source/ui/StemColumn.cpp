#include "StemColumn.h"

StemColumn::StemColumn(SkellSeparatorAudioProcessor& p, int index,
                       juce::String title, const juce::String& stemId, float def)
    : processor(p), stem(index), heading(std::move(title)), gain(def), meter(p.spectra, index)
{
    addAndMakeVisible(gain);
    addAndMakeVisible(solo);
    addAndMakeVisible(mute);
    addAndMakeVisible(meter);
    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        p.apvts, stemId + "Gain", gain);
    soloAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        p.apvts, stemId + "Solo", solo);
    muteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        p.apvts, stemId + "Mute", mute);
    gain.setTooltip("Drag to adjust " + heading + " level; double-click to reset to 0 dB; right-click to type.");
    solo.setTooltip("Isolate " + heading + " (solo)");
    mute.setTooltip("Silence " + heading + " (mute)");
}

void StemColumn::paint(juce::Graphics& g)
{
    g.addTransform(juce::AffineTransform::scale(getWidth() / Theme::columnW,
                                                 getHeight() / Theme::columnH));
    const auto panel = juce::Rectangle<float>(7.5f, 1.0f, 295.0f, 469.0f);
    juce::ColourGradient surface(juce::Colour(0xff202025), 140.0f, 0.0f,
                                 juce::Colour(0xff16171b), 140.0f, 467.0f, false);
    g.setGradientFill(surface);
    g.fillRoundedRectangle(panel, 11.0f);
    g.setColour(juce::Colours::white.withAlpha(0.067f));
    g.drawRoundedRectangle(panel.reduced(0.5f), 10.5f, 1.0f);
    if (panelSolo && panelAudible && !panelBypassed)
    {
        g.setColour(Theme::accent.withAlpha(0.47f));
        g.drawRoundedRectangle(panel.reduced(1.5f), 10.0f, 1.5f);
    }
    g.setColour(panelActive && panelAudible && !panelBypassed
                ? Theme::text : Theme::text.withAlpha(0.79f));
    g.setFont(Theme::mono(23.0f));
    g.drawText(heading, 10, 3, 280, 45, juce::Justification::centred, false);
    g.setColour(juce::Colours::white.withAlpha(0.047f));
    g.drawLine(24.0f, 54.5f, 254.0f, 54.5f, 1.0f);
    const auto detail = panelBypassed ? "LATENCY-ALIGNED BYPASS"
                        : !panelAudible ? "NOT IN OUTPUT MIX"
                        : stem == 0 ? "AMBIENCE / RESIDUAL"
                        : stem == 1 ? "VOICE / ESTIMATE" : "REVERB / ESTIMATE";
    // Make secondary labels readable at the default 75%-scale editor size.
    g.setColour(Theme::textDim.withAlpha(panelAudible && !panelBypassed ? 0.96f : 0.88f));
    g.setFont(Theme::mono(14.0f));
    g.drawText(detail, 24, 453, 262, 16, juce::Justification::centred, false);
}

void StemColumn::resized()
{
    const float sx = getWidth() / Theme::columnW;
    const float sy = getHeight() / Theme::columnH;
    auto place = [sx, sy](juce::Component& c, float x, float y, float w, float h)
    {
        c.setBounds(juce::roundToInt(x * sx), juce::roundToInt(y * sy),
                    juce::roundToInt(w * sx), juce::roundToInt(h * sy));
    };
    place(gain, 25, 69, 260, 331);
    // Keep button outlines inside the panel at minimum editor size.
    place(solo, 259, 42, 38, 38);
    place(mute, 259, 87, 38, 38);
    place(meter, 71, 410, 168, 40);
}

void StemColumn::refresh(bool active, bool bypassed)
{
    const bool anySolo = processor.apvts.getRawParameterValue("ambienceSolo")->load() > 0.5f
                      || processor.apvts.getRawParameterValue("voiceSolo")->load() > 0.5f
                      || processor.apvts.getRawParameterValue("reverbSolo")->load() > 0.5f;
    const bool audible = !mute.getToggleState() && (!anySolo || solo.getToggleState());
    const bool soloed = solo.getToggleState();
    if (panelAudible != audible || panelActive != active || panelSolo != soloed || panelBypassed != bypassed)
    {
        panelAudible = audible;
        panelActive = active;
        panelSolo = soloed;
        panelBypassed = bypassed;
        repaint();
    }
    const float amount = static_cast<float>(gain.getValue());
    const float gainMultiplier = amount <= -39.95f ? 0.0f
                                : juce::Decibels::decibelsToGain(amount);
    const float lvl = processor.spectra.getLevel(stem) * gainMultiplier;
    gain.setSignalLevel(audible ? juce::jmin(1.0f, lvl) : 0.0f);
    gain.setActive(active && audible && !bypassed);
    gain.setMuted(!audible || bypassed);
    meter.refresh(bypassed || !active, !audible, gainMultiplier);
}
