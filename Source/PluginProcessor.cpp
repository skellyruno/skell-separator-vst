#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "dsp/TinyVocalNet.h"
#include <cmath>
#include <memory>
#include <utility>

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
    engineModeParam = apvts.getRawParameterValue("engineMode");
    neuralStrengthParam = apvts.getRawParameterValue("neuralStrength");
    stemgenResearchParam = apvts.getRawParameterValue("stemgenResearch");
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
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "engineMode", 1 }, "Processing Engine",
        juce::StringArray { "Classical", "Neural Experimental" }, 0));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "stemgenResearch", 1 }, "StemgenRT Research Backend", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "neuralStrength", 1 }, "Neural Strength",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.65f));
    return layout;
}

void SkellSeparatorAudioProcessor::prepareToPlay(double sampleRate, int)
{
    spectra.reset();
    inputActivity.store(0.0f, std::memory_order_relaxed);
    overZeroBlocks.store(0, std::memory_order_relaxed);
    hostBypassActive.store(false, std::memory_order_relaxed);
    separator.prepare(sampleRate);
    alignment.reset();
    neuralSampleIndex = 0;
    neuralBlend.reset();
    strengthSmoothing.reset(sampleRate, 0.020);
    strengthSmoothing.setCurrentAndTargetValue(neuralStrengthParam->load(std::memory_order_relaxed));
    // Model selection/initialization happens ONLY in prepareToPlay, never on
    // the host callback. This model's STFT/training is explicitly 48 kHz.
    // When both are built, original Tiny Vocal takes precedence over RNNoise.
    std::unique_ptr<skell::NeuralFrameBackend> backend;
    int backendMode = 0;
    if (std::abs(sampleRate - 48000.0) < 0.5) {
#if defined(SKELL_WITH_TINY_VOCAL) && SKELL_WITH_TINY_VOCAL
        backend = skell::createTinyVocalBackend();
#elif defined(SKELL_WITH_RNNOISE) && SKELL_WITH_RNNOISE
        backend = skell::createRNNoiseBackend();
#endif
        if (backend) backendMode = 1;
    }
#if defined(SKELL_WITH_STEMGENRT) && SKELL_WITH_STEMGENRT
    if (std::abs(sampleRate - skell::stemgenSampleRate) < 0.5) {
        const auto file = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Skell/Separator/Models/independent.onnx");
        backend = skell::createStemgenRTBackend(file.getFullPathName().toRawUTF8());
        if (backend) backendMode = 2;
    }
#endif
    // Hosts normally set the non-realtime flag before prepareToPlay. In an
    // offline bounce, deterministic inline inference is safe and avoids
    // asynchronous worker starvation. Live playback remains lock-free.
    const bool started = neural.start(std::move(backend), isNonRealtime());
    preparedNeuralMode.store(started ? backendMode : 0, std::memory_order_release);
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
    preparedNeuralMode.store(0, std::memory_order_release);
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
    const bool bypassRequested = hostBypassed || bypassParam->load() > 0.5f;
    mixer.setTargets(gains, solos, mutes, bypassRequested);
    // Read automation once per callback, just like gain/Solo/Mute. When in
    // Classical or Bypass modes, the neural worker must not compute unused
    // masks; it will restart at a clean frame boundary after reactivation.
    const bool neuralEnabled = neural.active();
    const bool wantsResearch = stemgenResearchParam != nullptr &&
        stemgenResearchParam->load(std::memory_order_relaxed) > 0.5f;
    const bool neuralSelected = neuralEnabled && !bypassRequested &&
        engineModeParam->load(std::memory_order_relaxed) > 0.5f &&
        (availableNeuralMode() == (wantsResearch ? 2 : 1));
    strengthSmoothing.setTargetValue(neuralStrengthParam->load(std::memory_order_relaxed));
    const bool monoMode = modeParam->load() < 0.5f || channels == 1;
    monoBlend.setTargetValue(monoMode ? 1.0f : 0.0f);
    const int quality = juce::jlimit(0, 2, static_cast<int>(qualityParam->load()));
    auto* l = b.getWritePointer(0);
    auto* r = channels == 2 ? b.getWritePointer(1) : nullptr;
    float peak = 0.0f;
    bool overZero = false;
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
        const float neuralStrength = strengthSmoothing.getNextValue();
        if (neuralEnabled)
        {
            const float mid = 0.5f * (left + right);
            neural.submitSample(left + blend * (mid - left),
                                right + blend * (mid - right), neuralSampleIndex,
                                neuralSelected);
            std::array<float, 2> neuralVoice {};
            const bool available = neuralSelected && neural.readSample(
                neuralSampleIndex - LatencyAlignedStems::skellLatency(), neuralVoice);
            // A missing or late frame crossfades to the aligned classical
            // stem; selection changes never alter reported host latency.
            // Original neural isolation is intentionally user-blendable: the
            // synthetic-trained research model can over-suppress real vocals.
            // Preserve the exact three-stem complement in NeuralVoiceBlend.
            if (available) {
                for (size_t ch = 0; ch < 2; ++ch)
                    neuralVoice[ch] = (1.0f - neuralStrength) * stemSamples[1][ch]
                                      + neuralStrength * neuralVoice[ch];
            }
            // Conservative correction guard applies ONLY to synthetic-trained
            // TinyVocalNet, not the separately licensed RNNoise backend.
#if defined(SKELL_WITH_TINY_VOCAL) && SKELL_WITH_TINY_VOCAL
            neuralBlend.apply(stemSamples, available && neuralSelected, neuralVoice,
                              availableNeuralMode() == 1);
#else
            neuralBlend.apply(stemSamples, available && neuralSelected, neuralVoice);
#endif
        } else {
            // Smoothly return to classical when a model is unavailable.
            neuralBlend.apply(stemSamples, false, {});
        }
        ++neuralSampleIndex;
        const auto out = mixer.mix(stemSamples, rawDelayed);
        l[i] = out[0];
        if (r != nullptr) r[i] = out[1];
        // DAW floating-point buses can exceed 0 dBFS without numerical
        // clipping. Show the user; do not insert an unrequested limiter.
        overZero |= std::abs(out[0]) > 1.0f || (r != nullptr && std::abs(out[1]) > 1.0f);
    }
    for (int ch = 2; ch < b.getNumChannels(); ++ch) b.clear(ch, 0, n);
    if (overZero) overZeroBlocks.fetch_add(1, std::memory_order_relaxed);
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
        if (auto* p = apvts.getParameter(name))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(0.0f);
            p->endChangeGesture();
        }
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
