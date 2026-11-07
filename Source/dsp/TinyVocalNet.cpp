#include "TinyVocalNet.h"
#include "../../models/TinyVocalWeights.h"
#include <algorithm>
#include <cmath>

namespace skell {
namespace {
constexpr float pi = 3.14159265358979323846f;
static float logistic(float x) noexcept {
    x = std::clamp(x, -15.0f, 15.0f);
    return 1.0f / (1.0f + std::exp(-x));
}
}

TinyVocalNet::TinyVocalNet(bool unity) noexcept : allPass(unity) {
    static_assert(neuralFrameSize * 2 == windowSize, "WOLA must use 50% overlap");
    for (int i = 0; i < windowSize; ++i)
        window[static_cast<size_t>(i)] = std::sin(pi * (i + .5f) / windowSize);
    for (int k = 0; k < fftSize/2; ++k) {
        const float ang = -2.0f * pi * k / fftSize;
        twiddle[static_cast<size_t>(k)] = {std::cos(ang), std::sin(ang)};
    }
    for (int i = 0; i < fftSize; ++i) {
        unsigned v = static_cast<unsigned>(i), reversed = 0;
        for (int j = 0; j < 10; ++j) { reversed = (reversed << 1) | (v & 1u); v >>= 1; }
        bitReverse[static_cast<size_t>(i)] = static_cast<int>(reversed);
    }
    for (int i = 0; i <= fftSize/2; ++i) {
        const float hz = static_cast<float>(i) * 48000.0f / fftSize;
        const float pos = std::log(std::max(hz, 55.0f) / 55.0f) / std::log(22000.0f / 55.0f);
        const int band = std::clamp(static_cast<int>(std::floor(pos * (bands - 1))), 0, bands - 1);
        binBand[static_cast<size_t>(i)] = band;
        ++bandCounts[static_cast<size_t>(band)];
    }
    reset();
}

void TinyVocalNet::reset() noexcept {
    for (auto& c : previous) c.fill(0.0f);
    for (auto& c : overlap) c.fill(0.0f);
    smoothedMask.fill(0.5f);
    hasPrevious = false;
}

void TinyVocalNet::fft(std::array<std::complex<float>, fftSize>& data, bool inverse) const noexcept {
    for (int i = 0; i < fftSize; ++i)
        if (bitReverse[static_cast<size_t>(i)] > i)
            std::swap(data[static_cast<size_t>(i)], data[static_cast<size_t>(bitReverse[static_cast<size_t>(i)])]);
    for (int len = 2; len <= fftSize; len <<= 1) {
        const int half = len >> 1, stride = fftSize / len;
        for (int i = 0; i < fftSize; i += len) {
            for (int j = 0; j < half; ++j) {
                auto w = twiddle[static_cast<size_t>(j * stride)];
                if (inverse) w = std::conj(w);
                const auto u = data[static_cast<size_t>(i + j)];
                const auto v = data[static_cast<size_t>(i + j + half)] * w;
                data[static_cast<size_t>(i + j)] = u + v;
                data[static_cast<size_t>(i + j + half)] = u - v;
            }
        }
    }
    if (inverse) for (auto& v : data) v *= 1.0f / fftSize;
}

void TinyVocalNet::process(const NeuralFrame& input, NeuralFrame& output) noexcept {
    // The worker guarantees exactly 480 samples per frame and handles resets
    // for missing input timestamps. No operation here touches the audio callback.
    energy.fill(0.0f);
    for (int ch = 0; ch < 2; ++ch) {
        auto& spec = spectral[static_cast<size_t>(ch)];
        for (int i = 0; i < neuralFrameSize; ++i) {
            spec[static_cast<size_t>(i)] = {previous[static_cast<size_t>(ch)][static_cast<size_t>(i)] * window[static_cast<size_t>(i)], 0.0f};
            spec[static_cast<size_t>(i + neuralFrameSize)] = {input.audio[static_cast<size_t>(ch)][static_cast<size_t>(i)] * window[static_cast<size_t>(i + neuralFrameSize)], 0.0f};
        }
        for (int i = windowSize; i < fftSize; ++i) spec[static_cast<size_t>(i)] = {};
        fft(spec, false);
        for (int k = 0; k <= fftSize/2; ++k)
            energy[static_cast<size_t>(binBand[static_cast<size_t>(k)])] += std::abs(spec[static_cast<size_t>(k)]) * 0.5f;
    }
    float mean = 0.0f;
    for (int b = 0; b < bands; ++b) {
        // Keep training and inference feature normalization identical.
        const float v = bandCounts[static_cast<size_t>(b)] > 0
             ? energy[static_cast<size_t>(b)] / bandCounts[static_cast<size_t>(b)] : 0.0f;
        features[static_cast<size_t>(b)] = std::log1p(v);
        mean += features[static_cast<size_t>(b)];
    }
    mean /= bands;
    for (auto& f : features) f = std::clamp(f - mean, -6.0f, 6.0f);
    if (allPass) mask.fill(1.0f);
    else {
        for (int h = 0; h < hidden; ++h) {
            float sum = tiny_weights::l1_bias[static_cast<size_t>(h)];
            for (int b = 0; b < bands; ++b)
                sum += tiny_weights::l1_weight[static_cast<size_t>(h * bands + b)] * features[static_cast<size_t>(b)];
            layer[static_cast<size_t>(h)] = std::max(0.0f, sum);
        }
        for (int b = 0; b < bands; ++b) {
            float sum = tiny_weights::l2_bias[static_cast<size_t>(b)];
            for (int h = 0; h < hidden; ++h)
                sum += tiny_weights::l2_weight[static_cast<size_t>(b * hidden + h)] * layer[static_cast<size_t>(h)];
            mask[static_cast<size_t>(b)] = logistic(sum);
        }
    }
    // Smoothing avoids wildly varying masks on successive frames. An identity
    // mask bypasses smoothing for the exact-sum regression check.
    for (int b = 0; b < bands; ++b) {
        const float value = allPass ? 1.0f : std::clamp(mask[static_cast<size_t>(b)], 0.01f, 0.99f);
        smoothedMask[static_cast<size_t>(b)] = allPass ? 1.0f :
            (0.35f * value + 0.65f * smoothedMask[static_cast<size_t>(b)]);
    }
    for (int ch = 0; ch < 2; ++ch) {
        auto& spec = spectral[static_cast<size_t>(ch)];
        for (int k = 0; k <= fftSize/2; ++k) {
            const float gain = smoothedMask[static_cast<size_t>(binBand[static_cast<size_t>(k)])];
            spec[static_cast<size_t>(k)] *= gain;
            if (k > 0 && k < fftSize/2) spec[static_cast<size_t>(fftSize - k)] *= gain;
        }
        fft(spec, true);
        for (int i = 0; i < neuralFrameSize; ++i) {
            const size_t idx = static_cast<size_t>(i), channel = static_cast<size_t>(ch);
            output.audio[channel][idx] = overlap[channel][idx] + spec[idx].real() * window[idx];
            overlap[channel][idx] = spec[static_cast<size_t>(i + neuralFrameSize)].real() * window[static_cast<size_t>(i + neuralFrameSize)];
            previous[channel][idx] = input.audio[channel][idx];
        }
    }
    // The first produced buffer corresponds to a negative input frame and
    // the transport deliberately discards it. Output n>=1 is frame n-1.
    hasPrevious = true;
    output.valid = hasPrevious;
}
std::unique_ptr<NeuralFrameBackend> createTinyVocalBackend(bool testUnity) {
    return std::make_unique<TinyVocalNet>(testUnity);
}
}
