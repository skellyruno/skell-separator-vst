#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

SkellSeparatorAudioProcessor::SkellSeparatorAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "SkellSeparatorState", makeLayout()),
      separator(spectra)
{
    const char* ids[3] { "ambience", "voice", "reverb" };
    for (int s = 0; s < 3; ++s)
    {
        const juce::String id(ids[s]);
        gainParams[static_cast<size_t>(s)] = apvts.getRawParameterValue(id + "Gain");
        soloParams[static_cast<size_t>(s)] = apvts.getRawParameterValue(id + "Solo");
        muteParams[static_cast<size_t>(s)] = apvts.getRawParameterValue(id + "Mute");
    }
    modeParam = apvts.getRawParameterValue("channelMode");
    bypassParam = apvts.getRawParameterValue("bypass");
    qualityParam = apvts.getRawParameterValue("quality");
}

juce::AudioProcessorValueTreeState::ParameterLayout SkellSeparatorAudioProcessor::makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& name : { "ambience", "voice", "reverb" })
    {
        const juce::String id(name);
        juce::String title = id == "reverb" ? "Voice Reverb" : id.substring(0, 1).toUpperCase() + id.substring(1);
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { id + "Gain", 1 }, title + " Gain",
            juce::NormalisableRange<float>(-40.0f, 40.0f, 0.1f), 0.0f));
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { id + "Solo", 1 }, title + " Solo", false));
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { id + "Mute", 1 }, title + " Mute", false));
    }
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "channelMode", 1 }, "Channel Mode",
        juce::StringArray { "Mono", "Stereo" }, 1));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "bypass", 1 }, "Bypass", false));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "quality", 1 }, "Quality",
        juce::StringArray { "Gentle", "Balanced", "Strong" }, 1));
    return layout;
}

void SkellSeparatorAudioProcessor::prepareToPlay(double sampleRate, int)
{
    spectra.reset();
    inputActivity.store(0.0f, std::memory_order_relaxed);
    hostBypassActive.store(false, std::memory_order_relaxed);
    separator.prepare(sampleRate);
    alignment.reset();
    neuralSampleIndex = 0;
    neuralBlend.reset();
    // Model loading and thread startup happen at prepareToPlay, NOT on audio.
    // No model at non-48 kHz: RNNoise has a strict 480/48 kHz contract.
    neural.start(std::abs(sampleRate - 48000.0) < 0.5
                 ? skell::createRNNoiseBackend() : nullptr);
    mixer.prepare(sampleRate);
    // Host state may be restored before prepare. Honor the saved settings on
    // the very first processed sample (especially a bypassed session).
    std::array<float, 3> savedGains {};
    std::array<bool, 3> savedSolos {}, savedMutes {};
    for (size_t i = 0; i < 3; ++i)
    {
        savedGains[i] = gainParams[i]->load(std::memory_order_relaxed);
        savedSolos[i] = soloParams[i]->load(std::memory_order_relaxed) > 0.5f;
        savedMutes[i] = muteParams[i]->load(std::memory_order_relaxed) > 0.5f;
    }
    mixer.setTargets(savedGains, savedSolos, savedMutes,
                     bypassParam->load(std::memory_order_relaxed) > 0.5f);
    mixer.snapToTargets();
    monoBlend.reset(sampleRate, 0.012);
    monoBlend.setCurrentAndTargetValue(modeParam->load() < 0.5f ? 1.0f : 0.0f);
    // Fallback uses fixed 1024 samples for ALL quality settings. Never tell
    // the host quality changes latency when this engine does not.
    setLatencySamples(LatencyAlignedStems::skellLatency());
}

void SkellSeparatorAudioProcessor::releaseResources()
{
    neural.stop();
}

bool SkellSeparatorAudioProcessor::isBusesLayoutSupported(const BusesLayout& layout) const
{
    auto in = layout.getMainInputChannelSet();
    auto out = layout.getMainOutputChannelSet();
    return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo()) && in == out;
}

void SkellSeparatorAudioProcessor::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&)
{
    processAudio(b, false);
}

void SkellSeparatorAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& b,
                                                         juce::MidiBuffer&)
{
    // Some hosts invoke the processor-level bypass callback instead of the
    // plugin's own APVTS Bypass control. Process the same delay/OLA timeline;
    // JUCE's default bypass implementation can skip compensation entirely.
    processAudio(b, true);
}

void SkellSeparatorAudioProcessor::processAudio(juce::AudioBuffer<float>& b, bool hostBypassed)
{
    juce::ScopedNoDenormals noDenormals;
    hostBypassActive.store(hostBypassed, std::memory_order_relaxed);
    const int n = b.getNumSamples();
    const int channels = juce::jmin(b.getNumChannels(), 2);
    if (channels < 1) return;

    std::array<float, 3> gains {};
    std::array<bool, 3> solos {}, mutes {};
    for (int s = 0; s < 3; ++s)
    {
        const size_t ix = static_cast<size_t>(s);
        gains[ix] = gainParams[ix]->load(std::memory_order_relaxed);
        solos[ix] = soloParams[ix]->load(std::memory_order_relaxed) > 0.5f;
        mutes[ix] = muteParams[ix]->load(std::memory_order_relaxed) > 0.5f;
    }
    mixer.setTargets(gains, solos, mutes, hostBypassed || bypassParam->load() > 0.5f);
    const bool monoMode = modeParam->load() < 0.5f || channels == 1;
    monoBlend.setTargetValue(monoMode ? 1.0f : 0.0f);
    const int quality = juce::jlimit(0, 2, static_cast<int>(qualityParam->load()));
    auto* l = b.getWritePointer(0);
    auto* r = channels == 2 ? b.getWritePointer(1) : nullptr;
    float peak = 0.0f;
    Separator::Stems stemSamples {};
    std::array<float, 2> rawDelayed {};
    for (int i = 0; i < n; ++i)
    {
        // Hosts occasionally deliver nonfinite samples during plugin/transport
        // transitions; do not allow NaN/Inf to contaminate long-lived FFT state.
        const float rawLeft = l[i];
        const float rawRight = r != nullptr ? r[i] : rawLeft;
        const float left = std::isfinite(rawLeft) ? juce::jlimit(-8.0f, 8.0f, rawLeft) : 0.0f;
        const float right = std::isfinite(rawRight) ? juce::jlimit(-8.0f, 8.0f, rawRight) : 0.0f;
        peak = juce::jmax(peak, juce::jmax(std::abs(left), std::abs(right)));
        const float blend = monoBlend.getNextValue();
        separator.processSample(left, right, blend, quality, stemSamples, rawDelayed);
        alignment.process(stemSamples, rawDelayed);
        if (neural.active())
        {
            const float mid = 0.5f * (left + right);
            neural.submitSample(left + blend * (mid - left),
                                right + blend * (mid - right), neuralSampleIndex);
            std::array<float, 2> neuralVoice {};
            const bool available = neural.readSample(
                neuralSampleIndex - LatencyAlignedStems::skellLatency(), neuralVoice);
            // Missing model data is NOT a zero-valued voice frame. Fade the
            // last applied neural correction toward classical stems without
            // introducing a voice dropout or a transient ambience spike.
            neuralBlend.apply(stemSamples, available, neuralVoice);
        }
        ++neuralSampleIndex;
        const auto out = mixer.mix(stemSamples, rawDelayed);
        l[i] = out[0];
        if (r != nullptr) r[i] = out[1];
    }
    for (int ch = 2; ch < b.getNumChannels(); ++ch) b.clear(ch, 0, n);
    audioBlockCounter.fetch_add(1, std::memory_order_relaxed);
    inputActivity.store(0.80f * inputActivity.load(std::memory_order_relaxed)
                        + 0.20f * juce::jlimit(0.0f, 1.0f, peak * 2.0f),
                        std::memory_order_relaxed);
}

void SkellSeparatorAudioProcessor::resetAllParameters()
{
    for (auto* p : getParameters())
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->getDefaultValue());
        p->endChangeGesture();
    }
    presetIndex.store(0);
}

void SkellSeparatorAudioProcessor::usePreset(int index)
{
    constexpr float presets[4][3] {
        { 0.0f, 0.0f, 0.0f },
        { -40.0f, 0.0f, -40.0f },
        { 0.0f, -40.0f, -40.0f },
        { -20.0f, 0.0f, -5.0f }
    };
    // Host state can contain arbitrary integers; never index the preset table
    // without normalizing the signed value (negative modulo is negative in C++).
    index = ((index % 4) + 4) % 4;
    const char* ids[3] { "ambienceGain", "voiceGain", "reverbGain" };
    for (int s = 0; s < 3; ++s)
    {
        if (auto* p = apvts.getParameter(ids[s]))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(presets[index][s]));
            p->endChangeGesture();
        }
    }
    for (auto* name : { "ambienceSolo", "voiceSolo", "reverbSolo", "ambienceMute", "voiceMute", "reverbMute" })
        if (auto* p = apvts.getParameter(name)) p->setValueNotifyingHost(0.0f);
    presetIndex.store(index);
}

void SkellSeparatorAudioProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty("scale", uiScale.load(), nullptr);
    state.setProperty("preset", presetIndex.load(), nullptr);
    state.setProperty("circularKnobs", circularKnobs.load(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void SkellSeparatorAudioProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(apvts.state.getType()))
        {
            const auto tree = juce::ValueTree::fromXml(*xml);
            setUiScale(static_cast<int>(tree.getProperty("scale", 75)));
            presetIndex.store(juce::jlimit(0, 3, static_cast<int>(tree.getProperty("preset", 0))));
            circularKnobs.store(static_cast<bool>(tree.getProperty("circularKnobs", false)));
            apvts.replaceState(tree);
        }
}

juce::AudioProcessorEditor* SkellSeparatorAudioProcessor::createEditor()
{
    return new SkellSeparatorAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SkellSeparatorAudioProcessor();
}
