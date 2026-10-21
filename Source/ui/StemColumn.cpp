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
}

void StemColumn::paint(juce::Graphics& g)
{
    g.addTransform(juce::AffineTransform::scale(getWidth() / Theme::columnW,
                                                 getHeight() / Theme::columnH));
    g.setColour(Theme::text);
    g.setFont(Theme::mono(24.0f));
    g.drawText(heading, 8, 3, 292, 46, juce::Justification::centred, false);
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
    place(solo, 266, 42, 38, 38);
    place(mute, 266, 87, 38, 38);
    place(meter, 71, 410, 168, 40);
}

void StemColumn::refresh(bool active, bool bypassed)
{
    const bool anySolo = processor.apvts.getRawParameterValue("ambienceSolo")->load() > 0.5f
                      || processor.apvts.getRawParameterValue("voiceSolo")->load() > 0.5f
                      || processor.apvts.getRawParameterValue("reverbSolo")->load() > 0.5f;
    const bool audible = !mute.getToggleState() && (!anySolo || solo.getToggleState());
    const float amount = static_cast<float>(gain.getValue());
    const float gainMultiplier = amount <= -39.95f ? 0.0f
                                : juce::Decibels::decibelsToGain(amount);
    const float lvl = processor.spectra.getLevel(stem) * gainMultiplier;
    gain.setSignalLevel(audible ? juce::jmin(1.0f, lvl) : 0.0f);
    gain.setActive(active && audible && !bypassed);
    gain.setMuted(!audible || bypassed);
    meter.refresh(bypassed || !active, !audible, gainMultiplier);
}
