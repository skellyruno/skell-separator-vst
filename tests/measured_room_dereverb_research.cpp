#include "../Source/dsp/MeasuredRoomDereverbResearch.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

int main() {
    using Dereverb = skell::MeasuredRoomDereverbResearch;
    Dereverb deverb;
    assert(!deverb.prepare({{{0, .4f}, {2200, .2f}}}));
    assert(!deverb.prepare({{{1300, .8f}, {2400, .25f}}}));
    const auto response = std::array<Dereverb::Tap, 2>{{{1344, .42f}, {2736, .22f}}};
    assert(deverb.prepare(response));
    std::array<float, 48000> dryL {}, dryR {}, wetL {}, wetR {};
    for (int i=0; i<48000; ++i) {
        const double t=double(i)/48000;
        // Both vowels and consonant-like transients while room tails overlap.
        const float signal = static_cast<float>(.27*std::sin(2*3.141592653589793*191*t)
          + .09*std::sin(2*3.141592653589793*381*t) + ((i%2301)<19 ? .14 : 0.));
        dryL[static_cast<std::size_t>(i)]=signal;
        dryR[static_cast<std::size_t>(i)]=-.63f*signal;
    }
    double inputError = 0, outputError = 0, maximumError = 0;
    for (int i=0; i<48000; ++i) {
        const auto idx=static_cast<std::size_t>(i);
        wetL[idx]=dryL[idx];wetR[idx]=dryR[idx];
        for (const auto tap : response) {
            if (i>=tap.delay) {
                wetL[idx]+=tap.gain*dryL[static_cast<std::size_t>(i-tap.delay)];
                wetR[idx]+=tap.gain*dryR[static_cast<std::size_t>(i-tap.delay)];
            }
        }
        const auto recovered=deverb.process(wetL[idx],wetR[idx]);
        inputError+=std::pow(double(wetL[idx]-dryL[idx]),2);
        outputError+=std::pow(double(recovered[0]-dryL[idx]),2);
        maximumError=std::max(maximumError, std::abs(double(recovered[0]-dryL[idx])));
        assert(std::abs(recovered[1]-dryR[idx]) < 2.e-6);
    }
    std::printf("Matched sparse room: input error %.6f, output error %.10f, peak error %.8f\n",
                inputError, outputError, maximumError);
    assert(inputError > 1.0);
    assert(outputError < 1.e-7);
    assert(maximumError < 2.e-6);
    deverb.reset();
    const auto safe = deverb.process(std::numeric_limits<float>::quiet_NaN(),
                                     std::numeric_limits<float>::infinity());
    assert(std::isfinite(safe[0]) && std::isfinite(safe[1]));
    puts("Measured room dereverb research: PASS (known sparse room only)");
}
