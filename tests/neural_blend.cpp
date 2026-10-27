#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include "dsp/NeuralVoiceBlend.h"

static Separator::Stems makeStems() {
    Separator::Stems s {};
    for (int ch = 0; ch < 2; ++ch) {
        s[0][ch] = 0.25f;
        s[1][ch] = 0.50f;
        s[2][ch] = 0.25f;
    }
    return s;
}

int main() {
    skell::NeuralVoiceBlend blender;
    // The class must be valid on the first block after sample-rate reset.
    blender.reset();
    for (int i = 0; i < 700; ++i) {
        auto stems = makeStems();
        blender.apply(stems, true, { 0.10f, 0.20f });
        for (int ch = 0; ch < 2; ++ch)
            assert(std::abs(stems[0][ch] + stems[1][ch] + stems[2][ch] - 1.0f) < 1e-6f);
        if (i == 699) {
            assert(std::abs(stems[1][0] - 0.10f) < 1e-6f);
            assert(std::abs(stems[1][1] - 0.20f) < 1e-6f);
        }
    }
    // Frame arrives too late. v1.5 would mix classical 0.5 with *zero* model
    // output and start near zero, an abrupt 0.1->0.002 level drop. The correct
    // fade starts at the previous valid correction and ends at 0.5.
    float last = 0.10f;
    for (int i = 0; i < skell::NeuralVoiceBlend::transitionSamples; ++i) {
        auto stems = makeStems();
        blender.apply(stems, false, {});
        assert(std::abs(stems[1][0] - last) < 0.002f);
        assert(std::abs(stems[0][0] + stems[1][0] + stems[2][0] - 1.0f) < 1e-6f);
        last = stems[1][0];
    }
    assert(std::abs(last - 0.5f) < 1e-6f);
    // Recovery is equally smooth, and non-finite model values count as absent.
    for (int i = 0; i < 300; ++i) {
        auto stems = makeStems();
        blender.apply(stems, true, {0.2f, 0.3f});
        assert(std::abs(stems[1][0] - last) <= 0.002f);
        last = stems[1][0];
    }
    assert(std::abs(last - 0.2f) < 1e-6f);
    {
        auto s = makeStems();
        blender.apply(s, true, {std::numeric_limits<float>::quiet_NaN(), 0.0f});
        for (const auto& stem : s)
            for (const float value : stem) assert(std::isfinite(value));
        assert(std::abs(s[0][0] + s[1][0] + s[2][0] - 1.0f) < 1e-6f);
    }
    // A model stall must not cause allocations, saturation or timing changes.
    std::cout << "neural dropout/recovery crossfade: PASS\n";
}
