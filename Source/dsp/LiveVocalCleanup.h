#pragma once
// Skell Live vocal tracking: adaptive zero-lookahead downward expander.
// NOT neural noise suppression and NOT true dereverberation. Suppresses
// steady room noise and late tails between phrases without gate chatter.
// Real-time safe: all state preallocated; no locks, heap, FFT, or sleeps.
#include <algorithm>
#include <array>
#include <cmath>

namespace skell {
class LiveVocalCleanup final {
public:
    void prepare(double sampleRate) noexcept {
        const auto rate = (std::isfinite(sampleRate) && sampleRate >= 8000.0)
                        ? sampleRate : 48000.0;
        // Shared stereo detector keeps the imaging fixed while the input varies.
        attack = onePole(rate, 0.00045);
        detectorRelease = onePole(rate, 0.025);
        floorRise = onePole(rate, 0.9);  // Slow profile update; no pumping during vowels.
        floorFall = onePole(rate, 0.13);
        gainUp = onePole(rate, 0.0003);
        gainDown = onePole(rate, 0.19);
        sampleRate = static_cast<float>(rate);
        reset();
        setControls(0.f, 0.f, 0.35f);
    }
    void reset() noexcept {
        detector = 0.f;
        floor = 0.0012f; // -58 dBFS initial conservative noise reference.
        gain = 1.f;
        hold = 0;
    }
    // Three independent APVTS controls: Clean is the gentle expander,
    // Gate adds stronger below-threshold attenuation, Tails changes hold/release.
    // All expensive control math is calculated once per host block.
    void setControls(float clean, float gate, float tails) noexcept {
        clean = std::isfinite(clean) ? std::clamp(clean, 0.f, 1.f) : 0.f;
        gate = std::isfinite(gate) ? std::clamp(gate, 0.f, 1.f) : 0.f;
        tails = std::isfinite(tails) ? std::clamp(tails, 0.f, 1.f) : 0.f;
        cleanAmount = clean;
        gateAmount = gate;
        cleanDepth = clean * (2.f - clean);
        cleanThresholdMultiplier = 2.8f + 2.0f * clean;
        cleanMinGain = std::pow(10.f, -(8.f + clean * 26.f) / 20.f);
        gateThresholdMultiplier = 1.8f + gate * 7.f;
        gateMinGain = std::pow(10.f, -(12.f + gate * 42.f) / 20.f);
        // Higher Tails = longer hold and slower release; lower = tighter.
        releaseFactor = std::pow(gainDown, 2.5f - 1.85f * tails);
        holdSamples = static_cast<int>(sampleRate * (0.018f + tails * 0.17f));
    }
    // The DSP path operates at the same sample it reads (zero added latency).
    // Unlike neural denoising, this expander cannot remove noise under words.
    std::array<float, 2> process(float left, float right) noexcept {
        left = std::isfinite(left) ? left : 0.f;
        right = std::isfinite(right) ? right : 0.f;
        const float level = std::max(std::abs(left), std::abs(right));
        if (cleanAmount <= 0.f && gateAmount <= 0.f) {
            // Keep detector warm during bypass, but never change the samples.
            detector = level + (detector - level) * (level > detector ? attack : detectorRelease);
            hold = 0;
            // Smooth a Clean/Gate -> Dry automation change to prevent clicks,
            // while a fresh zero-strength LIVE path remains sample-exact.
            if (gain < 0.9999f) {
                gain = 1.f + (gain - 1.f) * gainUp;
                return {left * gain, right * gain};
            }
            gain = 1.f;
            return {left, right};
        }
        detector = level + (detector - level) * (level > detector ? attack : detectorRelease);
        // An upward noise-floor estimate is only allowed well below speech
        // onset. Prolonged vocals never train the profile upward.
        if (detector <= std::max(floor * 2.7f, 0.0035f * std::max(cleanAmount, gateAmount))) {
            const float candidate = std::max(detector, 0.00003f);
            floor = candidate + (floor - candidate) * (candidate > floor ? floorRise : floorFall);
        }
        const float cleanThreshold = std::clamp(std::max(floor * cleanThresholdMultiplier,
                                                     0.003f * cleanAmount), 0.00013f, 0.035f);
        const float gateThreshold = std::clamp(std::max(floor * gateThresholdMultiplier,
                                                    0.003f * gateAmount), 0.00013f, 0.05f);
        const float openThreshold = std::max(cleanAmount > 0.f ? cleanThreshold : 0.f,
                                             gateAmount > 0.f ? gateThreshold : 0.f);
        const bool voice = detector >= openThreshold || level >= openThreshold * 1.4f;
        if (voice) hold = holdSamples;
        else if (hold > 0) --hold;
        // A strong onset always takes priority over gate/expander attenuation.
        const float cleanRatio = std::clamp(detector / std::max(cleanThreshold, 1.e-8f), 0.f, 1.f);
        const float gateRatio = std::clamp(detector / std::max(gateThreshold, 1.e-8f), 0.f, 1.f);
        const float gentle = 1.f - cleanDepth * (1.f - cleanMinGain) *
                                     (1.f - cleanRatio * cleanRatio);
        const float gateCurve = gateRatio * gateRatio * gateRatio * gateRatio;
        const float strong = 1.f - gateAmount * (1.f - gateMinGain) * (1.f - gateCurve);
        const float target = (voice || hold > 0) ? 1.f : gentle * strong;
        const auto factor = target > gain ? gainUp : releaseFactor;
        gain = std::clamp(target + (gain - target) * factor, 0.f, 1.f);
        return {left * gain, right * gain};
    }
    float gainReductionDb() const noexcept {
        return -20.f * std::log10(std::max(gain, 0.0001f));
    }
private:
    static float onePole(double rate, double seconds) noexcept {
        return static_cast<float>(std::exp(-1.0 / (rate * seconds)));
    }
    float attack = 0.f, detectorRelease = 0.f, floorRise = 0.f, floorFall = 0.f;
    float gainUp = 0.f, gainDown = 0.f;
    float detector = 0.f, floor = 0.0012f, gain = 1.f;
    int holdSamples = 5280, hold = 0;
    float sampleRate = 48000.f;
    float cleanAmount = 0.f, gateAmount = 0.f, cleanDepth = 0.f;
    float cleanThresholdMultiplier = 2.8f, cleanMinGain = 1.f;
    float gateThresholdMultiplier = 1.8f, gateMinGain = 1.f;
    float releaseFactor = 0.f;
};
}
