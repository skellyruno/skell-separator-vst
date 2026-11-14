#include "../Source/dsp/LiveVocalCleanup.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

static constexpr double pi = 3.14159265358979323846;
static double gainAt(double sineSum, double cosineSum, double amplitude, int samples) {
    return 2.0 * std::hypot(sineSum, cosineSum) / (amplitude * samples);
}

// Zero-lookahead, frequency-aware cleanup quality guard. Two sources are
// deliberately within the broad 'vocal' range, but not coincident frequencies.
// No causally filtered expander can resolve sources at identical frequencies.
int main() {
    for (int rate : {44100, 48000, 96000}) {
        for (double interferingHz : {470.0, 950.0, 3900.0, 8300.0}) {
            skell::LiveVocalCleanup clean;
            clean.prepare(rate);
            clean.setControls(1.0f, 0.0f, .35f);
            double voiceI=0, voiceQ=0, noiseI=0, noiseQ=0;
            for (int i=0; i<rate*4; ++i) {
                const double t=double(i)/rate;
                const double voice=i>=rate*2 ? 0.22*std::sin(2*pi*220*t) : 0.;
                const double noise=0.0025*std::sin(2*pi*interferingHz*t);
                const auto y=clean.process(float(voice+noise),float(-(voice+noise)));
                assert(std::isfinite(y[0]) && std::isfinite(y[1]));
                assert(std::abs(y[0] + y[1])<1.e-5f);
                if (i>=rate*3) {
                    voiceI+=y[0]*std::sin(2*pi*220*t);
                    voiceQ+=y[0]*std::cos(2*pi*220*t);
                    noiseI+=y[0]*std::sin(2*pi*interferingHz*t);
                    noiseQ+=y[0]*std::cos(2*pi*interferingHz*t);
                }
            }
            const double vg=gainAt(voiceI,voiceQ,.22,rate);
            const double ng=gainAt(noiseI,noiseQ,.0025,rate);
            const double vowelChange=20*std::log10(vg);
            const double noiseReduction=-20*std::log10(ng);
            const double separationGain=noiseReduction+vowelChange;
            std::printf("%d Hz voice 220 / interference %.0f: suppression %.2f dB, voice %.3f dB, isolation gain %.2f dB\n",
                        rate, interferingHz, noiseReduction,vowelChange,separationGain);
            assert(std::abs(vowelChange)<0.25);
            // A narrow tone inside an active vocal band cannot be isolated by a
            // zero-latency band envelope. Reject unexpected amplification;
            // require documented gains only in distinguishable upper bands.
            assert(std::isfinite(separationGain));
            assert(separationGain > -0.4);
            if (interferingHz >= 3900.0) assert(separationGain > 3.0);
            if (interferingHz >= 8300.0) assert(separationGain > 12.0);
        }
    }
}
