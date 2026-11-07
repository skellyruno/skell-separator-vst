#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>
#include "dsp/TinyVocalNet.h"

static constexpr int hop = skell::neuralFrameSize;
static_assert(hop == 480);

int main() {
    // Bit-exact timing to within expected floating FFT roundoff; interchannel
    // cancellation and changes in frame amplitude must not shift either stem.
    auto allpass = skell::createTinyVocalBackend(true);
    std::array<float, hop> previousL {}, previousR {};
    float worst = 0.0f;
    for (int f = 0; f < 90; ++f) {
        skell::NeuralFrame in {}, out {};
        in.startSample = f * hop;
        for (int i = 0; i < hop; ++i) {
            const float t = static_cast<float>(f * hop + i);
            const float pulse = i == 0 && f % 9 == 2 ? 0.85f : 0.0f;
            in.audio[0][static_cast<size_t>(i)] = .4f * std::sin(t * .019f) + pulse;
            in.audio[1][static_cast<size_t>(i)] = -.31f * std::sin(t * .021f) + pulse * .7f;
        }
        allpass->process(in, out);
        if (f > 0) {
            for (int i = 0; i < hop; ++i) {
                worst = std::max(worst, std::abs(out.audio[0][static_cast<size_t>(i)] - previousL[static_cast<size_t>(i)]));
                worst = std::max(worst, std::abs(out.audio[1][static_cast<size_t>(i)] - previousR[static_cast<size_t>(i)]));
            }
        }
        previousL = in.audio[0]; previousR = in.audio[1];
    }
    std::cout << "tiny neural allpass WOLA worst error=" << worst << '\n';
    assert(worst < 8e-5f);

    auto model = skell::createTinyVocalBackend(false);
    assert(model && model->valid());
    double energyIn = 0.0, energyOut = 0.0;
    constexpr int frames = 120;
    std::vector<double> timings;
    timings.reserve(frames);
    for (int f = 0; f < frames; ++f) {
        skell::NeuralFrame in {}, out {};
        in.startSample = f * hop;
        for (int i = 0; i < hop; ++i) {
            const float t = (f * hop + i) / 48000.0f;
            // Artificial vocal harmonics plus low-frequency accompaniment.
            const float voice = .25f * std::sin(2*3.14159265359f*220*t)
                              + .18f * std::sin(2*3.14159265359f*440*t);
            const float music = .2f * std::sin(2*3.14159265359f*73.0f*t);
            in.audio[0][static_cast<size_t>(i)] = voice + music;
            in.audio[1][static_cast<size_t>(i)] = -(voice + .5f*music);
        }
        const auto start = std::chrono::steady_clock::now();
        model->process(in, out);
        const auto stop = std::chrono::steady_clock::now();
        timings.push_back(std::chrono::duration<double, std::micro>(stop-start).count());
        for (int i = 0; i < hop; ++i) {
            const auto x = out.audio[0][static_cast<size_t>(i)];
            assert(std::isfinite(x) && std::isfinite(out.audio[1][static_cast<size_t>(i)]));
            assert(std::fabs(x) < 8.0f);
            if (f > 1) {
                energyIn += in.audio[0][static_cast<size_t>(i)] * in.audio[0][static_cast<size_t>(i)];
                energyOut += x*x;
            }
        }
    }
    std::sort(timings.begin(), timings.end());
    double sum = 0;
    for (const auto t : timings) sum += t;
    std::cout << "trained model: mean=" << sum/timings.size()
              << "us, p99=" << timings[static_cast<size_t>(frames*.99)]
              << "us, max=" << timings.back() << "us/frame"
              << " output/input energy=" << energyOut/energyIn << '\n';
    assert(energyOut > 0.01 * energyIn);
    assert(energyOut < 2.0 * energyIn);
    // Training should not have converged to identity; this is a neural mask.
    assert(energyOut < .98 * energyIn);
    std::cout << "trained model and aligned stereo WOLA: PASS\n";
}
