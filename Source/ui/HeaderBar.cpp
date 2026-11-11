#include "HeaderBar.h"

EngineBadge::EngineBadge() : juce::Button("Processing engine")
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setTooltip("Switch DSP / experimental neural. Neural requires a 48 kHz host sample rate.");
}

void EngineBadge::updateState(bool selected, bool available, bool bypassed, bool offline)
{
    if (selected != neuralSelected || available != modelAvailable || bypassed != isBypassed || offline != offlineMode)
    {
        neuralSelected = selected;
        modelAvailable = available;
        isBypassed = bypassed;
        offlineMode = offline;
        setTooltip(neuralSelected ? (modelAvailable ? (offlineMode
            ? "Neural experimental | deterministic offline render | fixed 2880 samples"
            : "Neural experimental | asynchronous live inference | fixed 2880 samples")
            : "Neural model unavailable: 48 kHz host rate required")
            : "Classical separation | click to switch to experimental neural");
        repaint();
    }
}

void EngineBadge::paintButton(juce::Graphics& g, bool hover, bool down)
{
    g.addTransform(juce::AffineTransform::scale(getWidth() / 218.0f, getHeight() / 26.0f));
    const bool lit = neuralSelected && modelAvailable && !isBypassed;
    const auto surface = juce::Rectangle<float>(1, 1, 216, 24);
    g.setColour(juce::Colour(0xff0f1013).withAlpha(0.95f));
    g.fillRoundedRectangle(surface.translated(0, 1), 5.0f);
    g.setColour(juce::Colour(0xff222329).brighter(hover ? 0.12f : 0.0f)
                .darker(down ? 0.10f : 0.0f));
    g.fillRoundedRectangle(surface, 5.0f);
    g.setColour(lit ? Theme::accent.withAlpha(0.63f)
                    : juce::Colours::white.withAlpha(hover ? 0.19f : 0.10f));
    g.drawRoundedRectangle(surface.reduced(0.5f), 4.7f, 1.0f);
    g.setColour(lit ? Theme::accent : juce::Colour(0xff494b54));
    g.fillRoundedRectangle(5.0f, 4.0f, 31.0f, 18.0f, 3.4f);
    g.setColour(lit ? juce::Colours::white : Theme::text);
    g.setFont(Theme::mono(10.5f));
    g.drawText(neuralSelected ? "AI" : "DSP", 5, 4, 31, 18,
               juce::Justification::centred, false);
    g.setColour(modelAvailable ? Theme::text : Theme::textDim);
    g.setFont(Theme::mono(11.2f));
    const char* status = neuralSelected ? (modelAvailable ? (offlineMode ? "NEURAL / OFFLINE" : "NEURAL / EXPERIMENTAL") : "NEURAL / UNAVAILABLE")
                                        : (modelAvailable ? "CLASSICAL / AI READY" : "CLASSICAL / DSP ONLY");
    g.drawText(status, 41, 2, 174, 22, juce::Justification::centredLeft, false);
}

HeaderBar::HeaderBar(SkellSeparatorAudioProcessor& p) : processor(p)
{
    mode.setTooltip("Switch between mono sum and stereo processing");
    bypass.setTooltip("Use the unprocessed, latency-aligned input");
    addAndMakeVisible(mode);
    addAndMakeVisible(engine);
    addAndMakeVisible(bypass);
    addAndMakeVisible(previous);
    addAndMakeVisible(next);
    addAndMakeVisible(menu);
    bypass.setClickingTogglesState(true);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        p.apvts, "bypass", bypass);

    mode.onClick = [this]
    {
        if (auto* param = processor.apvts.getParameter("channelMode"))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost(param->getValue() > 0.5f ? 0.0f : 1.0f);
            param->endChangeGesture();
        }
    };
    engine.onClick = [this]
    {
        const bool selected = processor.apvts.getRawParameterValue("engineMode")->load() > 0.5f;
        // Allow exiting Neural after reopening at an unsupported sample rate.
        if (!selected && !processor.hasNeuralBackend()) return;
        if (auto* param = processor.apvts.getParameter("engineMode"))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost(param->convertTo0to1(selected ? 0.0f : 1.0f));
            param->endChangeGesture();
        }
    };
    previous.onClick = [this] { processor.usePreset(processor.getPresetIndex() - 1); };
    next.onClick = [this] { processor.usePreset(processor.getPresetIndex() + 1); };
    menu.onClick = [this] { if (onMenu) onMenu(); };
}

void HeaderBar::paint(juce::Graphics& g)
{
    g.addTransform(juce::AffineTransform::scale(getWidth() / Theme::designW,
                                                 getHeight() / 107.0f));
    g.setColour(Theme::text);
    g.setFont(Theme::display(53.0f));
    g.drawText("SKELL", 39, 28, 165, 64, juce::Justification::centredLeft, false);
    g.setColour(Theme::text.withAlpha(0.86f));
    g.setFont(Theme::mono(20.0f));
    g.drawText("STEM SEPARATOR", 208, 38, 220, 31, juce::Justification::centredLeft, false);
    g.setColour(Theme::textDim.withAlpha(0.55f));
    g.drawLine(432.0f, 42.0f, 432.0f, 92.0f, 1.2f);
    // Small sample-peak warning for boosted stem gains. It is not a limiter
    // or a true-peak measurement: no unexpected audio gain changes occur.
    const auto indicator = juce::Rectangle<float>(920.0f, 48.0f, 47.0f, 20.0f);
    if (overloadVisible)
    {
        g.setColour(Theme::accent.withAlpha(0.20f));
        g.fillRoundedRectangle(indicator, 5.0f);
        g.setColour(Theme::accent.withAlpha(0.87f));
        g.drawRoundedRectangle(indicator, 5.0f, 0.8f);
    }
    g.setColour(overloadVisible ? Theme::text : Theme::textDim.withAlpha(0.55f));
    g.setFont(Theme::mono(11.0f));
    g.drawText(overloadVisible ? "OVER" : "PEAK", indicator, juce::Justification::centred, false);
}

void HeaderBar::resized()
{
    const auto sx = getWidth() / Theme::designW;
    const auto sy = getHeight() / 107.0f;
    auto place = [sx, sy](juce::Component& c, float x, float y, float w, float h)
    {
        c.setBounds(juce::roundToInt(x * sx), juce::roundToInt(y * sy),
                    juce::roundToInt(w * sx), juce::roundToInt(h * sy));
    };
    place(mode, 497, 36, 120, 42);
    place(engine, 207, 71, 218, 25);
    place(bypass, 650, 36, 120, 42);
    place(previous, 815, 39, 38, 38);
    place(next, 874, 39, 38, 38);
    place(menu, 973, 39, 46, 38);
}

void HeaderBar::refresh(bool active)
{
    const auto over = processor.overZeroBlocks.load(std::memory_order_relaxed);
    if (over > lastOverZeroBlocks) overloadHold = 45;
    else if (overloadHold > 0) --overloadHold;
    lastOverZeroBlocks = over;
    const bool visible = overloadHold > 0;
    if (overloadVisible != visible) { overloadVisible = visible; repaint(); }
    const bool modelActive = processor.isNeuralRunning();
    if (modelActive != lastModelActive) { lastModelActive = modelActive; repaint(); }
    const auto isStereo = processor.apvts.getRawParameterValue("channelMode")->load() > 0.5f;
    const auto isBypassed = processor.apvts.getRawParameterValue("bypass")->load() > 0.5f
                         || processor.hostBypassActive.load(std::memory_order_relaxed);
    const bool selected = processor.apvts.getRawParameterValue("engineMode")->load() > 0.5f;
    // Disable only entering unavailable Neural, never leaving it.
    engine.setEnabled(processor.hasNeuralBackend() || selected);
    engine.updateState(selected, processor.hasNeuralBackend(), isBypassed,
                       processor.isOfflineNeural());
    mode.setCaption(isStereo ? "Stereo" : "Mono");
    mode.setAccentEnabled(isStereo && active && !isBypassed);
    bypass.setOutlineEnabled(isBypassed);
}
