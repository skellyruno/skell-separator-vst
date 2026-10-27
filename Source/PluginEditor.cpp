#include "PluginEditor.h"

SkellSeparatorAudioProcessorEditor::SkellSeparatorAudioProcessorEditor(SkellSeparatorAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p), header(p),
      ambience(p, 0, "Ambience", "ambience", 0.0f),
      voice(p, 1, "Voice", "voice", 0.0f),
      reverb(p, 2, "Voice Reverb", "reverb", 0.0f)
{
    setLookAndFeel(&style);
    addAndMakeVisible(header);
    addAndMakeVisible(ambience);
    addAndMakeVisible(voice);
    addAndMakeVisible(reverb);
    header.onMenu = [this] { showMenu(); };
    constrainer.setSizeLimits(785, 448, 1571, 896);
    constrainer.setFixedAspectRatio(Theme::designW / Theme::designH);
    setConstrainer(&constrainer);
    setResizable(true, true);
    setSize(juce::roundToInt(Theme::designW * p.getUiScale() / 100.0f),
            juce::roundToInt(Theme::designH * p.getUiScale() / 100.0f));
    startTimerHz(45);
}

SkellSeparatorAudioProcessorEditor::~SkellSeparatorAudioProcessorEditor()
{
    stopTimer();
    header.onMenu = {};
    setLookAndFeel(nullptr);
    setConstrainer(nullptr);
}

void SkellSeparatorAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.addTransform(juce::AffineTransform::scale(getWidth() / Theme::designW,
                                                 getHeight() / Theme::designH));
    g.fillAll(Theme::background);
    juce::ColourGradient top(juce::Colour(0xff222023), 520, -30,
                             juce::Colour(0xff141416), 520, 597, false);
    top.addColour(0.45, juce::Colour(0xff1a191b));
    g.setGradientFill(top);
    g.fillRect(0, 0, 1047, 597);
    if (visualActivity > 0.005f)
    {
        juce::ColourGradient glow(Theme::accent.withAlpha(0.058f * visualActivity),
                                   528, 237, Theme::accent.withAlpha(0.0f),
                                   528, 520, true);
        g.setGradientFill(glow);
        g.fillRect(0, 0, 1047, 597);
    }
    g.setColour(juce::Colours::white.withAlpha(0.045f));
    g.drawLine(0.0f, 0.5f, 1047.0f, 0.5f, 1.0f);
    // Fine separator under the header, deliberately soft at 75% size.
    g.setColour(juce::Colours::white.withAlpha(0.050f));
    g.drawLine(48.0f, 105.5f, 1001.0f, 105.5f, 1.0f);
    g.setColour(juce::Colour(0xff0b0b0c));
    g.drawRect(0.0f, 0.0f, 1047.0f, 597.0f, 2.0f);
}

void SkellSeparatorAudioProcessorEditor::resized()
{
    const float sx = getWidth() / Theme::designW;
    const float sy = getHeight() / Theme::designH;
    auto place = [sx, sy](juce::Component& component, float x, float y, float w, float h)
    {
        component.setBounds(juce::roundToInt(x * sx), juce::roundToInt(y * sy),
                            juce::roundToInt(w * sx), juce::roundToInt(h * sy));
    };
    place(header, 0, 0, Theme::designW, 107);
    place(ambience, Theme::centres[0] - Theme::columnW * 0.5f, Theme::columnY,
          Theme::columnW, Theme::columnH);
    place(voice, Theme::centres[1] - Theme::columnW * 0.5f, Theme::columnY,
          Theme::columnW, Theme::columnH);
    place(reverb, Theme::centres[2] - Theme::columnW * 0.5f, Theme::columnY,
          Theme::columnW, Theme::columnH);
}

void SkellSeparatorAudioProcessorEditor::timerCallback()
{
    const bool bypass = processor.apvts.getRawParameterValue("bypass")->load() > 0.5f
                     || processor.hostBypassActive.load(std::memory_order_relaxed);
    const auto block = processor.audioBlockCounter.load(std::memory_order_relaxed);
    if (block != lastBlockSeen) { lastBlockSeen = block; ticksWithoutAudio = 0; }
    else ticksWithoutAudio = juce::jmin(100, ticksWithoutAudio + 1);
    const bool running = ticksWithoutAudio <= 11; // approximately 250 ms at 45 Hz
    const float target = (bypass || !running) ? 0.0f : processor.inputActivity.load(std::memory_order_relaxed);
    const float old = visualActivity;
    visualActivity += (target > visualActivity ? 0.11f : 0.07f) * (target - visualActivity);
    const bool active = visualActivity > 0.028f;
    header.refresh(active);
    ambience.refresh(active, bypass);
    voice.refresh(active, bypass);
    reverb.refresh(active, bypass);
    if (std::abs(old - visualActivity) > 0.002f) repaint();
    const bool circular = processor.getKnobCircular();
    ambience.setCircular(circular);
    voice.setCircular(circular);
    reverb.setCircular(circular);
}

void SkellSeparatorAudioProcessorEditor::chooseQuality(int index)
{
    if (auto* q = processor.apvts.getParameter("quality"))
    {
        q->beginChangeGesture();
        q->setValueNotifyingHost(q->convertTo0to1(index));
        q->endChangeGesture();
    }
}

void SkellSeparatorAudioProcessorEditor::applyScale(int percent)
{
    processor.setUiScale(percent);
    setSize(juce::roundToInt(Theme::designW * percent / 100.0f),
            juce::roundToInt(Theme::designH * percent / 100.0f));
}

void SkellSeparatorAudioProcessorEditor::showMenu()
{
    juce::PopupMenu popup;
    popup.addSectionHeader("SKELL SEPARATOR  /  1.8");
    popup.addItem(1, "Reset All");
    popup.addItem(2, "Copy Settings to Clipboard");
    popup.addItem(3, "Input Mono Sum", true,
        processor.apvts.getRawParameterValue("channelMode")->load() < 0.5f);
    popup.addSeparator();
    juce::PopupMenu quality;
    const int qualityIndex = static_cast<int>(processor.apvts.getRawParameterValue("quality")->load());
    quality.addItem(10, "Gentle (less suppression)", true, qualityIndex == 0);
    quality.addItem(11, "Balanced", true, qualityIndex == 1);
    quality.addItem(12, "Strong (more suppression)", true, qualityIndex == 2);
    popup.addSubMenu("Separation Quality", quality);
    juce::PopupMenu modes;
    modes.addItem(20, "Vertical Drag", true, !processor.getKnobCircular());
    modes.addItem(21, "Circular Drag", true, processor.getKnobCircular());
    popup.addSubMenu("Knob Interaction", modes);
    juce::PopupMenu scale;
    for (int pct : { 75, 100, 125, 150 })
        scale.addItem(100 + pct, juce::String(pct) + "%", true, processor.getUiScale() == pct);
    popup.addSubMenu("Scale", scale);
    popup.addSeparator();
    popup.addItem(50, "About / Model Status");
    juce::Component::SafePointer<SkellSeparatorAudioProcessorEditor> safe(this);
    popup.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&header),
        [safe](int result)
        {
            if (safe == nullptr) return;
            auto& editor = *safe.getComponent();
            auto& p = editor.processor;
            if (result == 1) p.resetAllParameters();
            else if (result == 2)
            {
                juce::String out("Skell Separator settings\n");
                for (const auto& id : { "ambienceGain", "voiceGain", "reverbGain",
                                        "ambienceSolo", "voiceSolo", "reverbSolo",
                                        "ambienceMute", "voiceMute", "reverbMute",
                                        "channelMode", "bypass", "quality" })
                    out << id << " = " << p.apvts.getRawParameterValue(id)->load() << "\n";
                juce::SystemClipboard::copyTextToClipboard(out);
            }
            else if (result == 3)
            {
                if (auto* prm = p.apvts.getParameter("channelMode"))
                {
                    prm->beginChangeGesture();
                    prm->setValueNotifyingHost(prm->getValue() > 0.5f ? 0.0f : 1.0f);
                    prm->endChangeGesture();
                }
            }
            else if (result >= 10 && result <= 12) editor.chooseQuality(result - 10);
            else if (result == 20 || result == 21) p.setKnobCircular(result == 21);
            else if (result == 175 || result == 200 || result == 225 || result == 250)
                editor.applyScale(result - 100);
            else if (result == 50)
            {
                const bool neural = p.isNeuralRunning();
                const juce::String details = neural
                    ? (juce::String("RNNoise / speech enhancement active (48 kHz only).\n"
                       "RNNoise does NOT isolate singing reliably from dense music and is NOT dereverberation.\n"
                       "Voice Reverb remains classical.\n"
                       "Latency: 2880 samples (60 ms @ 48 kHz).\n"
                       "Worker inference mean: ") + juce::String(p.getNeuralWorkerMeanUs(), 1) + " us/frame\n"
                       "Worker inference max: " + juce::String(static_cast<juce::int64>(p.getNeuralWorkerMaxUs())) + " us/frame\n"
                       "Missed frames: " + juce::String(static_cast<juce::int64>(p.getNeuralMissedFrames())) + "\n"
                       "Dropped queued frames: " + juce::String(static_cast<juce::int64>(p.getNeuralQueueDropped())) + "\n"
                       "Rejected corrupt frames: " + juce::String(static_cast<juce::int64>(p.getNeuralRejectedFrames())))
                    : "Classical STFT fallback is active.\n"
                      "No neural model is running; either RNNoise is not linked, or the sample rate is not 48 kHz.\n"
                      "The three stems are estimated, not true clean source separation.\n"
                      "Original build latency: 1024 samples. RNNoise-enabled build: 2880 samples (all rates).";
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                    "Skell Separator — Processing Engine", "Skell Separator 1.8\n\n" + details,
                    "OK", safe.getComponent());
            }
        });
}
