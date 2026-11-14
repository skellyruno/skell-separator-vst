#pragma once
// Experimental zero-lookahead canceller for NARROW, STATIONARY tonal noise.
// Frequencies must be independently identified and the coefficients trained
// while the singer is silent.  This does NOT suppress broadband noise, moving
// fan tones, or reverberation and is NOT enabled in Skell LIVE.
// No allocations, locks, FFT, or model execution in process().
#include <algorithm>
#include <array>
#include <cmath>

namespace skell {
class TonalNoiseResearch final {
public:
    static constexpr int toneCount = 2;
    void prepare(double sampleRate, const std::array<double, toneCount>& frequencies) noexcept {
        const double rate = (std::isfinite(sampleRate) && sampleRate >= 8000.0) ? sampleRate : 48000.0;
        constexpr double twoPi = 6.283185307179586476925286766559;
        for (int i = 0; i < toneCount; ++i) {
            auto& t = tones[static_cast<std::size_t>(i)];
            const double f = frequencies[static_cast<std::size_t>(i)];
            t.enabled = std::isfinite(f) && f >= 40.0 && f < rate * 0.45;
            const double w = t.enabled ? twoPi * f / rate : 0.0;
            t.stepCos = std::cos(w);
            t.stepSin = std::sin(w);
        }
        reset();
    }
    void reset() noexcept {
        for (auto& t : tones) {
            t.oscCos = 1.0;
            t.oscSin = 0.0;
            t.cosGain.fill(0.f);
            t.sinGain.fill(0.f);
        }
        samples = 0;
    }
    std::array<float, 2> process(float left, float right, bool learnNoiseOnly) noexcept {
        const float in[2] {std::isfinite(left) ? left : 0.f,
                           std::isfinite(right) ? right : 0.f};
        std::array<float, 2> result {};
        for (int ch = 0; ch < 2; ++ch) {
            float estimate = 0.f;
            for (const auto& t : tones) {
                if (t.enabled) estimate += t.cosGain[static_cast<std::size_t>(ch)] * static_cast<float>(t.oscCos)
                    + t.sinGain[static_cast<std::size_t>(ch)] * static_cast<float>(t.oscSin);
            }
            const float residual = in[ch] - estimate;
            result[static_cast<std::size_t>(ch)] = residual;
            if (learnNoiseOnly) {
                // Normalized two-basis LMS. Never update during a vocal: that
                // would teach the canceller to remove some of the singer.
                constexpr float mu = 0.001f;
                for (auto& t : tones) {
                    if (!t.enabled) continue;
                    auto& a = t.cosGain[static_cast<std::size_t>(ch)];
                    auto& b = t.sinGain[static_cast<std::size_t>(ch)];
                    a = std::clamp(a + mu * residual * static_cast<float>(t.oscCos), -1.f, 1.f);
                    b = std::clamp(b + mu * residual * static_cast<float>(t.oscSin), -1.f, 1.f);
                }
            }
        }
        for (auto& t : tones) {
            if (!t.enabled) continue;
            const double c = t.oscCos * t.stepCos - t.oscSin * t.stepSin;
            const double s = t.oscSin * t.stepCos + t.oscCos * t.stepSin;
            t.oscCos = c;
            t.oscSin = s;
        }
        // Keep quadrature amplitude stable over very long sessions.
        if (++samples % 4096 == 0) {
            for (auto& t : tones) {
                if (!t.enabled) continue;
                const double scale = 1.0 / std::sqrt(t.oscCos * t.oscCos + t.oscSin * t.oscSin);
                t.oscCos *= scale;
                t.oscSin *= scale;
            }
        }
        return result;
    }
private:
    struct Tone {
        bool enabled = false;
        double stepCos = 1.0, stepSin = 0.0;
        double oscCos = 1.0, oscSin = 0.0;
        std::array<float, 2> cosGain {}, sinGain {};
    };
    std::array<Tone, toneCount> tones {};
    unsigned samples = 0;
};
} // namespace skell
