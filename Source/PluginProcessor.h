#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include "dsp/Separator.h"
#include "dsp/StemMixer.h"
#include "dsp/SpectralAnalyzer.h"
#include "dsp/StreamingNeural.h"
#include "dsp/StemgenRTBridge.h"
#include "dsp/TrackingMonitor.h"
#include "dsp/LiveVocalCleanup.h"
#include "dsp/LatencyAlignedStems.h"
#include "dsp/NeuralVoiceBlend.h"

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
    // Counts callback blocks with floating-point output above 0 dBFS.
    // Diagnostic only: never silently clamp user-selected boosted gains.
    std::atomic<std::uint64_t> overZeroBlocks { 0 };
    std::atomic<float> trackReductionDb { 0.0f };
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void resetAllParameters();
    int availableNeuralMode() const noexcept { return preparedNeuralMode.load(std::memory_order_relaxed); }
    bool isNeuralRunning() const noexcept {
        return !isTrackingMode() && neural.active() && engineModeParam != nullptr && availableNeuralMode() != 0 &&
            engineModeParam->load(std::memory_order_relaxed) > 0.5f &&
            (availableNeuralMode() == ((stemgenResearchParam != nullptr &&
               stemgenResearchParam->load(std::memory_order_relaxed) > 0.5f) ? 2 : 1));
    }
    bool hasNeuralBackend() const noexcept { return neural.active(); }
    bool isOfflineNeural() const noexcept { return neural.active() && neural.rendersOffline(); }
    const char* neuralBackendName() const noexcept {
        if (availableNeuralMode() == 2)
            return "StemgenRT independent vocals — externally licensed ONNX research model";
        if (availableNeuralMode() == 1) {
#if defined(SKELL_WITH_TINY_VOCAL) && SKELL_WITH_TINY_VOCAL
            return "Tiny Vocal Mask — synthetic-trained experimental model";
#elif defined(SKELL_WITH_RNNOISE) && SKELL_WITH_RNNOISE
            return "RNNoise — speech denoiser";
#endif
        }
        return "Neural model unavailable at this sample rate or not installed";
    }
    std::uint64_t getNeuralMissedFrames() const noexcept { return neural.missedDeadlines(); }
    std::uint64_t getNeuralQueueDropped() const noexcept { return neural.droppedFrames(); }
    std::uint64_t getNeuralRejectedFrames() const noexcept { return neural.rejectedFrames(); }
    std::uint64_t getNeuralStaleFrames() const noexcept { return neural.staleFrames(); }
    double getNeuralWorkerMeanUs() const noexcept { return neural.meanWorkerFrameUs(); }
    std::uint64_t getNeuralWorkerMaxUs() const noexcept { return neural.maxWorkerFrameUs(); }
    // Called from GUI/non-real-time host state thread; changes reported PDC.
    // Switch while the DAW transport is stopped to avoid timeline jumps.
    void setTrackingMode(bool enabled);
    bool isTrackingMode() const noexcept { return trackingMode.load(std::memory_order_relaxed); }
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
    skell::LiveVocalCleanup liveCleanup;
    bool wasTrackingAudio = false;
    std::atomic<int> preparedNeuralMode { 0 }; // 0 off, 1 tiny/RNNoise, 2 StemgenRT research
    std::int64_t neuralSampleIndex = 0;
    skell::NeuralVoiceBlend neuralBlend;
    StemMixer mixer;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> monoBlend;
    std::array<std::atomic<float>*, 3> gainParams {}, soloParams {}, muteParams {};
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* qualityParam = nullptr;
    std::atomic<float>* engineModeParam = nullptr;
    std::atomic<float>* neuralStrengthParam = nullptr;
    std::atomic<float>* stemgenResearchParam = nullptr;
    std::atomic<float>* trackCleanParam = nullptr;
    std::atomic<float>* trackTailsParam = nullptr;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> strengthSmoothing;
    std::atomic<bool> trackingMode { false };
    std::atomic<int> uiScale { 75 }, presetIndex { 0 };
    std::atomic<bool> circularKnobs { false };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SkellSeparatorAudioProcessor)
};
