#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include "dsp/Separator.h"
#include "dsp/StemMixer.h"
#include "dsp/SpectralAnalyzer.h"
#include "dsp/StreamingNeural.h"
#include "dsp/LatencyAlignedStems.h"

class SkellSeparatorAudioProcessor final : public juce::AudioProcessor
{
public:
    SkellSeparatorAudioProcessor();
    ~SkellSeparatorAudioProcessor() override = default;
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Skell Separator"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    const juce::String getProgramName(int) override { return {}; }
    void setCurrentProgram(int) override {}
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    SpectralAnalyzer spectra;
    std::atomic<float> inputActivity { 0.0f };
    std::atomic<bool> hostBypassActive { false };
    std::atomic<juce::uint32> audioBlockCounter { 0 };
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void resetAllParameters();
    bool isNeuralRunning() const noexcept { return neural.active(); }
    std::uint64_t getNeuralMissedFrames() const noexcept { return neural.missedDeadlines(); }
    std::uint64_t getNeuralQueueDropped() const noexcept { return neural.droppedFrames(); }
    double getNeuralWorkerMeanUs() const noexcept { return neural.meanWorkerFrameUs(); }
    std::uint64_t getNeuralWorkerMaxUs() const noexcept { return neural.maxWorkerFrameUs(); }
    void usePreset(int index);
    int getUiScale() const noexcept { return uiScale.load(); }
    void setUiScale(int scale) noexcept { uiScale.store(juce::jlimit(75, 150, scale)); }
    int getPresetIndex() const noexcept { return presetIndex.load(); }
    void setKnobCircular(bool c) noexcept { circularKnobs.store(c); }
    bool getKnobCircular() const noexcept { return circularKnobs.load(); }

private:
    void processAudio(juce::AudioBuffer<float>&, bool hostBypassed);
    Separator separator;
    LatencyAlignedStems alignment;
    skell::StreamingNeural neural;
    std::int64_t neuralSampleIndex = 0;
    float neuralMix = 0.0f;
    StemMixer mixer;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> monoBlend;
    std::array<std::atomic<float>*, 3> gainParams {}, soloParams {}, muteParams {};
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* qualityParam = nullptr;
    std::atomic<int> uiScale { 100 }, presetIndex { 0 };
    std::atomic<bool> circularKnobs { false };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SkellSeparatorAudioProcessor)
};
