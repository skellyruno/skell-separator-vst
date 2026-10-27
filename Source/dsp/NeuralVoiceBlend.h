#pragma once
// The audio-thread-only handoff between an asynchronous speech model and the
// latency-aligned classical stems. Never interpret a missing model frame as
// silence: that creates a 5 ms hole in the Voice stem and an ambience burst.
// All transition state is fixed-size and no allocation/blocking is performed.
#include "Separator.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace skell {
class NeuralVoiceBlend final {
public:
    static constexpr int transitionSamples = 240; // 5 ms at RNNoise's 48 kHz
    using Stereo = std::array<float, 2>;

    void reset() noexcept {
        previousAvailable = false;
        transition = transitionSamples;
        transitionFrom = {};
        lastCorrection = {};
    }

    // `candidate` is aligned to the SAME original input sample as the
    // classical stems. When absent, fade out the final known correction,
    // *not* a zero neural waveform. When a new frame is available, fade in
    // its true, time-varying samples without filtering the steady-state model.
    void apply(Separator::Stems& stems, bool available, const Stereo& candidate) noexcept {
        for (float v : candidate)
            if (!std::isfinite(v) || std::abs(v) > 8.0f)
                available = false;

        if (available != previousAvailable) {
            transitionFrom = lastCorrection;
            transition = 0;
            previousAvailable = available;
        }
        transition = std::min(transitionSamples, transition + 1);
        const float amount = static_cast<float>(transition) / transitionSamples;
        for (int ch = 0; ch < 2; ++ch) {
            const auto c = static_cast<std::size_t>(ch);
            const float original = stems[0][c] + stems[1][c] + stems[2][c];
            const float dry = stems[1][c];
            const float neuralCorrection = available ? candidate[c] - dry : 0.0f;
            const float correction = transitionFrom[c] * (1.0f - amount)
                                   + neuralCorrection * (available ? amount : 0.0f);
            const float voice = dry + correction;
            stems[1][c] = voice;
            stems[0][c] = original - voice - stems[2][c];
            lastCorrection[c] = correction;
        }
    }

private:
    bool previousAvailable = false;
    int transition = transitionSamples;
    Stereo transitionFrom {}, lastCorrection {};
};
}
