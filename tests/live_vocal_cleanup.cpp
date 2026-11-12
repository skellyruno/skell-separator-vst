#include "../Source/dsp/LiveVocalCleanup.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

// Pure DSP tests. Never require JUCE, model files, an audio interface, or FL.
static double rms(const double sum, int count) { return std::sqrt(sum / count); }
static constexpr double pi = 3.14159265358979323846;
int main() {
    for (int sampleRate : {44100, 48000, 96000}) {
        skell::LiveVocalCleanup c;
        c.prepare(sampleRate);
        c.setControls(0.f, 0.f, 0.35f);
        for (int i = 0; i < sampleRate; ++i) {
            const float x = (i % 119 == 0 ? -1.45f : (i % 241 == 0 ? 2.1f : 0.04f));
            const auto y = c.process(x, -x);
            assert(y[0] == x && y[1] == -x);
        }
        c.setControls(1.f, 0.f, 0.75f);
        // Starting from a silent room must NOT make future quiet noise look
        // like foreground singing. This previously failed as floor -> zero.
        for (int i = 0; i < sampleRate * 3; ++i)
            (void)c.process(0.f, 0.f);
        double afterSilenceIn = 0.0, afterSilenceOut = 0.0;
        for (int i = 0; i < sampleRate * 3; ++i) {
            const float noise = 0.0012f * std::sin(2.0 * pi * 151.0 * i / sampleRate);
            const auto y = c.process(noise, -noise);
            if (i >= sampleRate * 2) {
                afterSilenceIn += noise * noise;
                afterSilenceOut += y[0] * y[0];
            }
        }
        assert(afterSilenceOut < afterSilenceIn * 0.4); // > 4dB attenuation.
        c.reset();
        const auto invalid = c.process(std::numeric_limits<float>::infinity(),
                                       std::numeric_limits<float>::quiet_NaN());
        assert(invalid[0] == 0.f && invalid[1] == 0.f);
        // 2 seconds of steady -58 dBFS-ish room tone; compare *last* 300ms
        // after adaptive envelope and release have stabilized.
        double inNoise = 0., outNoise = 0.;
        for (int i = 0; i < sampleRate * 2; ++i) {
            const float noise = 0.00125f * std::sin(2.0 * pi * 163.0 * i / sampleRate);
            const auto y = c.process(noise, noise);
            if (i >= sampleRate * 17 / 10) {
                inNoise += noise * noise;
                outNoise += y[0] * y[0];
            }
        }
        const auto reduction = 20.0 * std::log10(std::max(1.e-15, rms(inNoise, sampleRate * 3 / 10)) /
                                                 std::max(1.e-15, rms(outNoise, sampleRate * 3 / 10)));
        assert(reduction > 3.0 && reduction < 45.0);
        // A consonant/word onset should reopen within <=1ms; no lookahead.
        // At 0% clean, even sample zero must pass unmodified (checked above).
        int reopens = -1;
        double voiceIn = 0., voiceOut = 0.;
        for (int i = 0; i < sampleRate; ++i) {
            const float v = 0.27f * std::sin(2.0 * pi * 218.0 * i / sampleRate);
            auto y = c.process(v, -v);
            assert(std::isfinite(y[0]) && std::isfinite(y[1]));
            assert(std::abs(y[0] + y[1]) < 1.e-5f);
            if (std::abs(v) > 0.1f && reopens < 0 && std::abs(y[0]) > 0.95f * std::abs(v))
                reopens = i;
            if (i > sampleRate / 2) { voiceIn += v*v; voiceOut += y[0]*y[0]; }
        }
        assert(reopens >= 0 && reopens < sampleRate / 1000);
        const double vowelLoss = 20 * std::log10(std::sqrt(voiceIn / voiceOut));
        assert(std::abs(vowelLoss) < 0.1);
        // Clean->Dry while transport runs must not jump abruptly from a
        // heavily reduced noise level to full scale; settle to exact dry.
        skell::LiveVocalCleanup toggle;
        toggle.prepare(sampleRate);
        toggle.setControls(1.f, 0.5f);
        for (int i=0; i<sampleRate*2; ++i)
            (void)toggle.process(0.001f, -0.001f);
        const auto beforeToggle = toggle.process(0.001f, -0.001f);
        toggle.setControls(0.f, 0.5f);
        const auto firstDry = toggle.process(0.001f, -0.001f);
        assert(firstDry[0] >= beforeToggle[0] && firstDry[0]-beforeToggle[0] < 0.00015f);
        for (int i=0; i<sampleRate/100; ++i)
            (void)toggle.process(0.001f, -0.001f);
        const auto dry = toggle.process(0.001f, -0.001f);
        assert(dry[0] == 0.001f && dry[1] == -0.001f);

        // Gate remains fully independent: Clean = 0 must not disable Gate.
        skell::LiveVocalCleanup gate;
        gate.prepare(sampleRate);
        gate.setControls(0.f, 1.f, 0.35f);
        double gatedIn = 0., gatedOut = 0.;
        for (int i = 0; i < sampleRate * 2; ++i) {
            const float noise = 0.00125f * std::sin(2.0 * pi * 163.0 * i / sampleRate);
            const auto output = gate.process(noise, noise);
            if (i > sampleRate * 17 / 10) {
                gatedIn += static_cast<double>(noise) * noise;
                gatedOut += static_cast<double>(output[0]) * output[0];
            }
        }
        const double gateReduction = 10.0 * std::log10(gatedIn / std::max(gatedOut, 1.e-18));
        assert(gateReduction > 12.0 && gateReduction < 80.0);
        int gateOpen = -1;
        for (int i = 0; i < sampleRate / 50; ++i) {
            const float speech = 0.3f * std::sin(2.0 * pi * 412.0 * i / sampleRate);
            const auto output = gate.process(speech, -speech);
            if (gateOpen < 0 && std::abs(speech) > .08f &&
                std::abs(output[0]) > std::abs(speech) * .95f)
                gateOpen = i;
        }
        assert(gateOpen >= 0 && gateOpen < sampleRate / 1000);
        // Tails must mean longer, more natural decay at higher values.
        // Compare 100 ms after release from a sustained vocal event.
        auto tailAfter = [&](float tails) {
            skell::LiveVocalCleanup x;
            x.prepare(sampleRate);
            x.setControls(0.f, 1.f, tails);
            for (int i = 0; i < sampleRate / 2; ++i)
                x.process(0.3f, 0.3f);
            std::array<float, 2> output {};
            for (int i = 0; i < sampleRate / 5; ++i)
                output = x.process(0.00125f, 0.00125f);
            return output[0];
        };
        const float tight = tailAfter(0.f), natural = tailAfter(1.f);
        assert(natural > tight * 1.5f && natural > 0.0009f);
        std::printf("live gate %d Hz: gate-only attenuation %.2f dB, onset %d samples, tails tight %.7f / long %.7f\n",
                    sampleRate, gateReduction, gateOpen, tight, natural);

        // Adversarial automation and silent input never produce NaNs/glitches.
        c.setControls(std::numeric_limits<float>::quiet_NaN(),
                      std::numeric_limits<float>::infinity(),
                      std::numeric_limits<float>::quiet_NaN());
        for (int i=0; i<512; ++i) {
            const auto y = c.process(0.f, 0.f);
            assert(y[0] == 0.f && y[1] == 0.f);
        }
        std::printf("live cleanup %d Hz: room-noise attenuation %.2f dB; onset %d samples; vowel loss %.4f dB\n",
                    sampleRate, reduction, reopens, vowelLoss);
    }
}
