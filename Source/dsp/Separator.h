#pragma once
#include <JuceHeader.h>
#include <array>
#include <cstdint>
#include "SpectralAnalyzer.h"
#include "MaskEstimator.h"

// CPU-only classical separator; NOT a trained AI/neural model.
// A future licensed model backend can replace the mask estimator only.
// All temporal arrays are allocated at construction/prepare, never in processSample.
class Separator final
{
public:
    static constexpr int fftSize = 1024;
    static constexpr int hopSize = 256;
    static constexpr int latency = fftSize;
    static constexpr int channels = 2;
    static constexpr int ringSize = 4096;
    static constexpr int spectralBins = fftSize / 2 + 1;
    using Stems = std::array<std::array<float, channels>, 3>;

    explicit Separator(SpectralAnalyzer& analyzer);
    void prepare(double sampleRate);
    void reset() noexcept;
    void processSample(float left, float right, float monoBlend, int quality,
                       Stems& output, std::array<float, channels>& originalDelayed) noexcept;

private:
    void processFrame(int quality) noexcept;
    void runInverse(int channel, int stem, int64_t frameStart) noexcept;
    SpectralAnalyzer& analyzer;
    juce::dsp::FFT fft { 10 };
    std::array<float, fftSize> window {};
    std::array<std::array<float, fftSize>, channels> inputHistory {};
    std::array<std::array<float, ringSize>, channels> dryOla {}, reverbOla {}, delayedInput {}, rawDryDelay {};
    // Use JUCE's actual complex FFT API, not float* interleaved arrays.
    // The regression shim has this same signature to catch compile errors.
    using Complex = juce::dsp::Complex<float>;
    using FftBuffer = std::array<Complex, fftSize>;
    std::array<FftBuffer, channels> forward {}, spectrum {};
    FftBuffer maskSpectrum {}, ifftBuffer {};
    MaskEstimator estimator;
    MaskEstimator::Spectrum analysisMagnitude {}, sideMagnitude {}, dryMask {}, wetMask {};
    std::array<int, spectralBins> displayBands {};
    std::array<float, SpectralAnalyzer::bands> displayBandCounts {};
    int64_t currentSample = 0;
    double sr = 44100.0;
};
