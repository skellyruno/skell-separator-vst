#pragma once
// Skell LIVE: zero-lookahead four-band IIR noise reducer + independent
// broadband adaptive gate/expander. No FFT, model, allocations, or locks.
// Clean is NOT a vocal source separator and does not perform dereverberation.
#include <algorithm>
#include <array>
#include <cmath>

namespace skell {
class LiveVocalCleanup final {
public:
    void prepare(double sampleRate) noexcept {
        const double rate = (std::isfinite(sampleRate) && sampleRate >= 8000.0)
                          ? sampleRate : 48000.0;
        sampleRateHz = static_cast<float>(rate);
        attack = onePole(rate, 0.00045);
        detectorRelease = onePole(rate, 0.025);
        floorRise = onePole(rate, 1.6);
        floorFall = onePole(rate, 0.18);
        bandAttack = onePole(rate, 0.0018);
        bandRelease = onePole(rate, 0.055);
        bandOpen = onePole(rate, 0.00035);
        bandClose = onePole(rate, 0.045);
        gateOpen = onePole(rate, 0.0003);
        gateClose = onePole(rate, 0.19);
        cleanBlendPole = onePole(rate, 0.004);
        for (int k = 0; k < 3; ++k) {
            const double hz = k == 0 ? 270.0 : k == 1 ? 1550.0 : 4800.0;
            const auto cutoff = std::min(hz, rate * 0.40);
            for (auto& channel : highpass)
                channel[static_cast<size_t>(k)].prepare(rate, cutoff);
        }
        reset();
        setControls(0.f, 0.f, 0.35f);
    }

    void reset() noexcept {
        detector = 0.f;
        noiseFloor = 0.0012f;
        gateGain = 1.f;
        cleanBlend = 0.f;
        hold = 0;
        transientGuard = 0;
        lastEffectiveGain = 1.f;
        for (auto& channel : highpass)
            for (auto& stage : channel) stage.reset();
        bandEnvelope.fill(0.f);
        bandFloor.fill(0.00025f);
        bandGain.fill(1.f);
    }

    // Cheap control-rate math only; called once per host audio block.
    void setControls(float clean, float gate, float tails) noexcept {
        cleanAmount = validUnit(clean);
        gateAmount = validUnit(gate);
        tails = validUnit(tails);
        gateThresholdMultiplier = 1.8f + gateAmount * 7.f;
        gateMinGain = std::pow(10.f, -(12.f + gateAmount * 42.f) / 20.f);
        // Tails affects the broadband gate, never the adaptive noise profile.
        gateReleaseFactor = std::pow(gateClose, 2.5f - 1.85f * tails);
        holdSamples = static_cast<int>(sampleRateHz * (0.018f + tails * 0.17f));
    }

    std::array<float, 2> process(float left, float right) noexcept {
        left = std::isfinite(left) ? left : 0.f;
        right = std::isfinite(right) ? right : 0.f;
        const float level = std::max(std::abs(left), std::abs(right));
        // Open a short dry onset window before the per-band envelopes settle.
        // Once the vocal detector is active, band-specific suppression resumes
        // even while the singer continues. No lookahead is required.
        if (cleanAmount > 0.f && detector < 0.012f &&
            level > std::max(0.018f, noiseFloor * 14.f))
            transientGuard = static_cast<int>(sampleRateHz * 0.006f);
        const float onsetProtection = std::clamp(
            static_cast<float>(transientGuard) / (sampleRateHz * 0.003f), 0.f, 1.f);
        if (transientGuard > 0) --transientGuard;
        detector = smooth(detector, level, level > detector ? attack : detectorRelease);
        if (detector < 1.e-18f) detector = 0.f;

        // The shared L/R detector protects stereo balance and opens fast for
        // transients. Never infer a voice solely from one noisy frequency band.
        if (detector <= std::max(noiseFloor * 2.7f, 0.0035f * std::max(cleanAmount, gateAmount))) {
            const float candidate = std::max(detector, 0.00003f);
            noiseFloor = smooth(noiseFloor, candidate,
                                candidate > noiseFloor ? floorRise : floorFall);
        }

        // Three cascaded complementary second-order high-pass crossovers.
        // Top band isolates hiss without opening on low-pitched vowels.
        // The residual of each split guarantees exact reconstruction when
        // all gains are unity; no lookahead or analysis window is introduced.
        std::array<std::array<float, 4>, 2> bands {};
        const std::array<float, 2> inputs {left, right};
        for (size_t ch = 0; ch < 2; ++ch) {
            float remainder = inputs[ch];
            for (int k = 2; k >= 0; --k) {
                const float upper = highpass[ch][static_cast<size_t>(k)].process(remainder);
                bands[ch][static_cast<size_t>(k + 1)] = upper;
                remainder -= upper;
            }
            bands[ch][0] = remainder;
        }

        cleanBlend = smooth(cleanBlend, cleanAmount, cleanBlendPole);
        if (cleanBlend < 1.e-12f) cleanBlend = 0.f;
        const bool mayLearnNoise = detector < std::max(noiseFloor * 7.f, 0.014f);
        float cleaned[2] {0.f, 0.f};
        float activeEnergy = 0.f, attenuatedEnergy = 0.f;
        for (size_t k = 0; k < 4; ++k) {
            const float instant = std::max(std::abs(bands[0][k]), std::abs(bands[1][k]));
            bandEnvelope[k] = smooth(bandEnvelope[k], instant,
                                     instant > bandEnvelope[k] ? bandAttack : bandRelease);
            if (bandEnvelope[k] < 1.e-18f) bandEnvelope[k] = 0.f;
            // Learn noise only during quieter regions, with slow upward
            // adaptation. Active singing cannot teach a loud vocal as noise.
            if (mayLearnNoise || bandEnvelope[k] < bandFloor[k]) {
                const float candidate = std::max(bandEnvelope[k], 0.00001f);
                const float pole = candidate > bandFloor[k] ? floorRise : floorFall;
                bandFloor[k] = smooth(bandFloor[k], candidate, pole);
            }
            const float threshold = std::max(0.000025f, bandFloor[k] * 4.5f);
            // Protect sudden consonants using the instantaneous detector too.
            const float sense = std::max(bandEnvelope[k], instant * 0.55f);
            float activity = std::clamp(sense / threshold, 0.f, 1.f);
            activity = activity * activity * (3.f - 2.f * activity);
            const float target = 0.06f + 0.94f * activity;
            bandGain[k] = smooth(bandGain[k], target,
                                  target > bandGain[k] ? bandOpen : bandClose);
            for (size_t ch = 0; ch < 2; ++ch)
                cleaned[ch] += bands[ch][k] * bandGain[k];
            activeEnergy += instant;
            attenuatedEnergy += instant * bandGain[k];
        }

        const float gateThreshold = std::clamp(
            std::max(noiseFloor * gateThresholdMultiplier, 0.003f * gateAmount),
            0.00013f, 0.05f);
        if (gateAmount > 0.f) {
            const bool voice = detector >= gateThreshold || level >= gateThreshold * 1.4f;
            if (voice) hold = holdSamples;
            else if (hold > 0) --hold;
            const float ratio = std::clamp(detector / gateThreshold, 0.f, 1.f);
            const float curve = ratio * ratio * ratio * ratio;
            const float target = (voice || hold > 0) ? 1.f
                : 1.f - gateAmount * (1.f - gateMinGain) * (1.f - curve);
            gateGain = smooth(gateGain, target,
                              target > gateGain ? gateOpen : gateReleaseFactor);
        } else {
            hold = 0;
            gateGain = smooth(gateGain, 1.f, gateOpen);
        }
        const float estimatedBandGain = activeEnergy > 1.e-12f
            ? attenuatedEnergy / activeEnergy : 1.f;
        // Fade the onset guard back into the multiband estimate to avoid a
        // hard gain discontinuity a few milliseconds into a consonant.
        const float effectiveBlend = cleanBlend * (1.f - onsetProtection);
        lastEffectiveGain = std::clamp(gateGain *
            (1.f - effectiveBlend + effectiveBlend * estimatedBandGain), 0.0001f, 1.f);
        // Suppress residual IIR tail energy when both input samples are
        // mathematically zero; a zero microphone signal must remain zero.
        if (left == 0.f && right == 0.f) return {{0.f, 0.f}};
        return {{(left + effectiveBlend * (cleaned[0] - left)) * gateGain,
                 (right + effectiveBlend * (cleaned[1] - right)) * gateGain}};
    }

    float gainReductionDb() const noexcept {
        // Approximate energy-weighted GR, not a true-peak measurement.
        return -20.f * std::log10(lastEffectiveGain);
    }

private:
    static float onePole(double rate, double seconds) noexcept {
        return static_cast<float>(std::exp(-1.0 / (rate * seconds)));
    }
    static float smooth(float before, float target, float pole) noexcept {
        return target + (before - target) * pole;
    }
    static float validUnit(float value) noexcept {
        return std::isfinite(value) ? std::clamp(value, 0.f, 1.f) : 0.f;
    }
    struct HighPass {
        void prepare(double sr, double hz) noexcept {
            constexpr double pi = 3.14159265358979323846;
            const double w = 2.0 * pi * hz / sr;
            const double cs = std::cos(w), sn = std::sin(w);
            const double alpha = sn / 1.4142135623730951;
            const double a0 = 1.0 + alpha;
            b0 = static_cast<float>((1.0 + cs) * 0.5 / a0);
            b1 = static_cast<float>(-(1.0 + cs) / a0);
            b2 = b0;
            a1 = static_cast<float>(-2.0 * cs / a0);
            a2 = static_cast<float>((1.0 - alpha) / a0);
            reset();
        }
        void reset() noexcept { z1 = z2 = 0.f; }
        float process(float x) noexcept {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            if (std::abs(z1) < 1.e-18f) z1 = 0.f;
            if (std::abs(z2) < 1.e-18f) z2 = 0.f;
            return y;
        }
        float b0 = 0.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;
        float z1 = 0.f, z2 = 0.f;
    };
    std::array<std::array<HighPass, 3>, 2> highpass {};
    std::array<float, 4> bandEnvelope {}, bandFloor {}, bandGain {};
    float attack = 0.f, detectorRelease = 0.f;
    float floorRise = 0.f, floorFall = 0.f;
    float bandAttack = 0.f, bandRelease = 0.f, bandOpen = 0.f, bandClose = 0.f;
    float gateOpen = 0.f, gateClose = 0.f, cleanBlendPole = 0.f;
    float detector = 0.f, noiseFloor = 0.0012f, gateGain = 1.f, cleanBlend = 0.f;
    float gateReleaseFactor = 0.f, lastEffectiveGain = 1.f;
    float cleanAmount = 0.f, gateAmount = 0.f;
    float gateThresholdMultiplier = 1.8f, gateMinGain = 1.f;
    float sampleRateHz = 48000.f;
    int holdSamples = 0, hold = 0, transientGuard = 0;
};
}
