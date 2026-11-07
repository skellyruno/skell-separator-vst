#pragma once
// Original trained 64-band MLP mask (synthetic-data research prototype).
// Stereo 960-sample sqrt-Hann WOLA; one-frame-delayed 480-sample outputs.
// No third-party inference runtime, no allocations/locks during processing.
#include "StreamingNeural.h"
#include <array>
#include <complex>
#include <cstdint>
#include <memory>

namespace skell {
class TinyVocalNet final : public NeuralFrameBackend {
public:
    // allPassForTest validates STFT/WOLA time alignment with a unity mask.
    explicit TinyVocalNet(bool allPassForTest = false) noexcept;
    bool valid() const noexcept override { return true; }
    void reset() noexcept override;
    void process(const NeuralFrame& input, NeuralFrame& output) noexcept override;
private:
    static constexpr int fftSize = 1024;
    static constexpr int windowSize = 960;
    static constexpr int bands = 64;
    static constexpr int hidden = 48;
    void fft(std::array<std::complex<float>, fftSize>& x, bool inverse) const noexcept;
    const bool allPass;
    std::array<float, windowSize> window {};
    std::array<std::complex<float>, fftSize/2> twiddle {};
    std::array<int, fftSize> bitReverse {};
    std::array<int, fftSize/2+1> binBand {};
    std::array<int, bands> bandCounts {};
    std::array<float, bands> energy {}, features {}, layer {}, mask {}, smoothedMask {};
    std::array<std::array<std::complex<float>, fftSize>, 2> spectral {};
    std::array<std::array<float, neuralFrameSize>, 2> previous {}, overlap {};
    bool hasPrevious = false;
};
std::unique_ptr<NeuralFrameBackend> createTinyVocalBackend(bool allPassForTest = false);
}
