#include "HeaderBar.h"

HeaderBar::HeaderBar(SkellSeparatorAudioProcessor& p) : processor(p)
{
    mode.setTooltip("Switch between mono sum and stereo processing");
    bypass.setTooltip("Use the unprocessed, latency-aligned input");
    addAndMakeVisible(mode);
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
    g.setColour(Theme::textDim);
    g.setFont(Theme::mono(11.0f));
    g.drawRoundedRectangle(209, 73, 25, 17, 2.6f, 1.2f);
    const bool neural = processor.isNeuralRunning();
    g.drawText(neural ? "NN" : "DSP", 209, 72, 26, 18, juce::Justification::centred, false);
    g.drawText(neural ? "RNNOISE / SPEECH DENOISE" : "ON-DEVICE / CLASSICAL",
               243, 72, 185, 19, juce::Justification::centredLeft, false);
    g.setColour(Theme::textDim.withAlpha(0.55f));
    g.drawLine(432.0f, 42.0f, 432.0f, 92.0f, 1.2f);
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
    place(bypass, 650, 36, 120, 42);
    place(previous, 815, 39, 38, 38);
    place(next, 874, 39, 38, 38);
    place(menu, 973, 39, 46, 38);
}

void HeaderBar::refresh(bool active)
{
    const bool modelActive = processor.isNeuralRunning();
    if (modelActive != lastModelActive) { lastModelActive = modelActive; repaint(); }
    const auto isStereo = processor.apvts.getRawParameterValue("channelMode")->load() > 0.5f;
    const auto isBypassed = processor.apvts.getRawParameterValue("bypass")->load() > 0.5f
                         || processor.hostBypassActive.load(std::memory_order_relaxed);
    mode.setCaption(isStereo ? "Stereo" : "Mono");
    mode.setAccentEnabled(isStereo && active && !isBypassed);
    bypass.setOutlineEnabled(isBypassed);
}
