#include "StemColumn.h"

StemColumn::StemColumn(SkellSeparatorAudioProcessor& p, int index,
                       juce::String title, const juce::String& stemId, float def)
    : processor(p), stem(index), heading(title), separateHeading(std::move(title)),
      liveHeading(index == 0 ? "CLEAN" : (index == 1 ? "GATE" : "TAILS")),
      gain(def), meter(p.spectra, index)
{
    addAndMakeVisible(gain);
    addAndMakeVisible(liveGain);
    liveGain.configurePercent(index == 2 ? 35.0f : 0.0f);
    const char* liveIds[] { "trackClean", "trackGate", "trackTails" };
    liveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        p.apvts, liveIds[index], liveGain);
    liveGain.setTooltip(index == 0 ? "Clean: gentle adaptive room-noise reduction; 0% is off"
                        : index == 1 ? "Gate: stronger suppression below the adaptive threshold; 0% is off"
                                     : "Tails: longer holds and slower closure at higher settings");
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
    setLiveMode(p.isTrackingMode());
}

void StemColumn::setLiveMode(bool live)
{
    if (liveModeInitialized && liveMode == live) return;
    liveModeInitialized = true;
    liveMode = live;
    heading = live ? liveHeading : separateHeading;
    gain.setVisible(!live);
    liveGain.setVisible(live);
    solo.setVisible(!live);
    mute.setVisible(!live);
    meter.setLiveMode(live);
    repaint();
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
    const auto detail = liveMode ? (panelBypassed ? "LIVE / BYPASS"
                            : stem == 0 ? "ADAPTIVE CLEAN / 0ms"
                            : stem == 1 ? "NOISE GATE / 0ms"
                                        : "HOLD + RELEASE / 0ms")
                        : panelBypassed ? "LATENCY-ALIGNED BYPASS"
                        : !panelAudible ? "NOT IN OUTPUT MIX"
                        : stem == 0 ? "AMBIENCE / RESIDUAL"
                        : stem == 1 ? "VOICE / ESTIMATE" : "REVERB / ESTIMATE";
    // Subtitles should remain legible on the real 785px-wide editor, including
    // muted/soloed/bypassed states, without competing with the main readout.
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
    place(liveGain, 25, 69, 260, 331);
    // Keep button outlines inside the panel at minimum editor size.
    place(solo, 259, 42, 38, 38);
    place(mute, 259, 87, 38, 38);
    place(meter, 71, 410, 168, 40);
}

void StemColumn::refresh(bool active, bool bypassed)
{
    if (liveMode) {
        if (!panelAudible || panelSolo || panelActive != active || panelBypassed != bypassed) {
            panelAudible = true;
            panelSolo = false;
            panelActive = active;
            panelBypassed = bypassed;
            repaint();
        }
        const float mic = processor.inputActivity.load(std::memory_order_relaxed);
        const float reduction = juce::jlimit(0.f, 1.f,
            processor.trackReductionDb.load(std::memory_order_relaxed) / 48.f);
        const float tail = static_cast<float>(liveGain.getValue()) / 100.f;
        const float signal = stem == 0 ? mic : stem == 1 ? reduction : tail;
        liveGain.setSignalLevel(bypassed ? 0.f : signal);
        liveGain.setActive(active && !bypassed);
        liveGain.setMuted(bypassed);
        meter.refreshLive(signal, bypassed);
        return;
    }
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
