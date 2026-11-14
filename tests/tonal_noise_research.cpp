#include "../Source/dsp/TonalNoiseResearch.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

constexpr double pi = 3.14159265358979323846;
int main() {
    for (int rate : {44100, 48000, 96000}) {
        skell::TonalNoiseResearch cancel;
        cancel.prepare(rate, {{470.0, 950.0}});
        double voiceI = 0., voiceQ = 0., toneI[2] {}, toneQ[2] {};
        constexpr double tones[2] {470., 950.};
        for (int i = 0; i < rate * 4; ++i) {
            const double t = static_cast<double>(i) / rate;
            const double vocal = i >= rate * 2 ? .20 * std::sin(2*pi*220*t) : 0.;
            const double hum = .004 * std::sin(2*pi*tones[0]*t + .3)
                             + .003 * std::sin(2*pi*tones[1]*t - .2);
            const float in = static_cast<float>(vocal + hum);
            const auto out = cancel.process(in, -in, i < rate * 2);
            assert(std::isfinite(out[0]) && out[0] == -out[1]);
            if (i >= rate * 3) {
                voiceI += out[0] * std::sin(2*pi*220*t);
                voiceQ += out[0] * std::cos(2*pi*220*t);
                for (int k=0; k<2; ++k) {
                    toneI[k] += out[0] * std::sin(2*pi*tones[k]*t);
                    toneQ[k] += out[0] * std::cos(2*pi*tones[k]*t);
                }
            }
        }
        const double vocalGain = 2*std::hypot(voiceI,voiceQ) / (rate*.20);
        const double reduction0 = -20*std::log10(2*std::hypot(toneI[0],toneQ[0]) / (rate*.004));
        const double reduction1 = -20*std::log10(2*std::hypot(toneI[1],toneQ[1]) / (rate*.003));
        const double db = 20*std::log10(vocalGain);
        std::printf("%d Hz calibrated tones 470/950: %.2f / %.2f dB reduced, voice %.4f dB\n",
                    rate, reduction0, reduction1, db);
        assert(reduction0 > 20.0 && reduction1 > 20.0);
        assert(std::abs(db) < .04);
        // Exact-frequency overlap: a changing vocal envelope can occupy the
        // *same* 470 Hz line as a stationary calibrated interference tone.
        // This only works because the interference phase/frequency stays fixed.
        cancel.reset();
        double squaredError=0., squaredVoice=0.;
        for (int i=0; i<rate*3; ++i) {
            const double t=double(i)/rate;
            const double hum=.004*std::sin(2*pi*470*t+.3)
                            +.003*std::sin(2*pi*950*t-.2);
            const double envelope=(i>=rate*2) ? (.07+.08*(.5+.5*std::sin(2*pi*3*t))) : 0.;
            const double singing=envelope*std::sin(2*pi*470*t+.71);
            const auto y=cancel.process(static_cast<float>(hum+singing),
                                        static_cast<float>(-hum-singing),i<rate*2);
            if (i>=rate*2) {
                squaredError+=std::pow(y[0]-singing,2);
                squaredVoice+=singing*singing;
            }
        }
        const double normalizedError=std::sqrt(squaredError/squaredVoice);
        std::printf("%d Hz same-line 470 Hz tone + 470 Hz enveloped singer: relative error %.6f\n",
                    rate,normalizedError);
        assert(normalizedError<.005);
        // The research canceller never accepts invalid control/audio samples.
        auto bad = cancel.process(std::numeric_limits<float>::quiet_NaN(),
                                  std::numeric_limits<float>::infinity(), false);
        assert(bad[0] == bad[0] && bad[1] == bad[1]);
    }
    puts("Tonal cancellation research: PASS (calibrated stationary tones only)");
}
