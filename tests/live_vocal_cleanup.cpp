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
        // No lookahead: a fresh microphone onset passes on its first sample
        // even with Clean fully enabled (onset-protection window).
        skell::LiveVocalCleanup firstSample;
        firstSample.prepare(sampleRate);
        firstSample.setControls(1.f, 0.f, 0.35f);
        const auto impulse = firstSample.process(0.5f, -0.25f);
        assert(impulse[0] == 0.5f && impulse[1] == -0.25f);

        c.setControls(1.f, 0.f, 0.75f);
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
            if (i > sampleRate / 50) assert(std::abs(y[0] + y[1]) < 1.e-5f);
            if (std::abs(v) > 0.1f && reopens < 0 && std::abs(y[0]) > 0.95f * std::abs(v))
                reopens = i;
            if (i > sampleRate / 2) { voiceIn += v*v; voiceOut += y[0]*y[0]; }
        }
        assert(reopens >= 0 && reopens < sampleRate / 1000);
        const double vowelLoss = 20 * std::log10(std::sqrt(voiceIn / voiceOut));
        assert(std::abs(vowelLoss) < 0.1);

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

        // Isolated-hiss fixture followed by sustained vowel + hiss: Clean
        // must still reduce at least some high-band noise while Gate is OPEN,
        // rather than acting as a second all-or-nothing broadband gate.
        skell::LiveVocalCleanup multiband;
        multiband.prepare(sampleRate);
        multiband.setControls(1.f, 0.f, 0.35f);
        double vocalInPhase = 0., vocalQuadrature = 0.;
        double hissInPhase = 0., hissQuadrature = 0.;
        for (int i = 0; i < sampleRate * 4; ++i) {
            const double time = static_cast<double>(i) / sampleRate;
            const double vocal = i < sampleRate * 2 ? 0. : 0.22 * std::sin(2. * pi * 220. * time);
            const double hiss = 0.0025 * std::sin(2. * pi * 8300. * time);
            const auto output = multiband.process(static_cast<float>(vocal + hiss),
                                                  static_cast<float>(vocal + hiss));
            assert(std::isfinite(output[0]) && output[0] == output[1]);
            if (i >= sampleRate * 3) {
                vocalInPhase += output[0] * std::sin(2. * pi * 220. * time);
                vocalQuadrature += output[0] * std::cos(2. * pi * 220. * time);
                hissInPhase += output[0] * std::sin(2. * pi * 8300. * time);
                hissQuadrature += output[0] * std::cos(2. * pi * 8300. * time);
            }
        }
        const double vowelGain = 2. * std::hypot(vocalInPhase, vocalQuadrature)
                                 / (sampleRate * 0.22);
        const double hissGain = 2. * std::hypot(hissInPhase, hissQuadrature)
                                / (sampleRate * 0.0025);
        const double hissSuppression = -20. * std::log10(hissGain);
        assert(std::abs(20. * std::log10(vowelGain)) < 0.2);
        assert(hissSuppression > 2.5 && hissSuppression < 35.);
        std::printf("LIVE %d Hz concurrent low-vowel / high-hiss: %.2f dB hiss reduction, %.3f dB vowel change\n",
                    sampleRate, hissSuppression, 20. * std::log10(vowelGain));

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
