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
        holdNormal = static_cast<int>(rate * 0.110);
        reset();
        setControls(0.f, 0.35f);
    }
    void reset() noexcept {
        detector = 0.f;
        floor = 0.0012f; // -58 dBFS initial conservative noise reference.
        gain = 1.f;
        hold = 0;
    }
    // Call once per audio block: transcedentals stay OFF the sample loop.
    // Clamped controls keep corrupted automation from contaminating DSP.
    void setControls(float clean, float tails) noexcept {
        clean = std::isfinite(clean) ? std::clamp(clean, 0.f, 1.f) : 0.f;
        tails = std::isfinite(tails) ? std::clamp(tails, 0.f, 1.f) : 0.f;
        cleanAmount = clean;
        attenuationAmount = clean * (2.f - clean);
        thresholdMultiplier = 2.8f + 2.0f * clean;
        minGain = std::pow(10.0f, -(8.0f + clean * 26.0f + tails * 8.0f) / 20.0f);
        releaseFactor = std::pow(gainDown, 1.f + tails * 1.4f);
        holdSamples = static_cast<int>(holdNormal * (1.0f - 0.70f * tails));
    }
    // The DSP path operates at the same sample it reads (zero added latency).
    // Unlike neural denoising, this expander cannot remove noise under words.
    std::array<float, 2> process(float left, float right) noexcept {
        left = std::isfinite(left) ? left : 0.f;
        right = std::isfinite(right) ? right : 0.f;
        const float level = std::max(std::abs(left), std::abs(right));
        if (cleanAmount <= 0.f) {
            // Keep detector warm during bypass, but never change the samples.
            detector = level + (detector - level) * (level > detector ? attack : detectorRelease);
            hold = 0;
            // Smooth Clean->Dry changes while audio is playing. A fresh Dry
            // track starts with gain=1 and remains bit-exact from sample zero.
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
        if (detector <= std::max(floor * 2.7f, 0.0035f * cleanAmount)) {
            const float candidate = std::max(detector, 0.00003f);
            floor = candidate + (floor - candidate) * (candidate > floor ? floorRise : floorFall);
        }
        const float threshold = std::clamp(
            std::max(floor * thresholdMultiplier, 0.003f * cleanAmount),
            0.00013f, 0.035f);
        const bool voice = detector >= threshold || level >= threshold * 1.4f;
        if (voice) hold = holdSamples;
        else if (hold > 0) --hold;
        // Never apply attenuation to clear voice. Tail control shortens the
        // hold window and increases attenuation, not a fake neural dereverb.
        const float lowRatio = std::clamp(detector / std::max(threshold, 1.e-8f), 0.f, 1.f);
        const float shaped = lowRatio * lowRatio;
        const float target = (voice || hold > 0) ? 1.f
            : 1.f - attenuationAmount * (1.f - minGain) * (1.f - shaped);
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
    int holdNormal = 5280, holdSamples = 5280, hold = 0;
    float cleanAmount = 0.f, attenuationAmount = 0.f;
    float thresholdMultiplier = 2.8f, minGain = 1.f, releaseFactor = 0.f;
};
}
