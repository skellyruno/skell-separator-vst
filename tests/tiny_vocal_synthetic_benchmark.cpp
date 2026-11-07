// Does not use licensed recordings. Synthetic diagnostic, *not* a real-vocals benchmark.
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <vector>
#include "dsp/TinyVocalNet.h"

static constexpr float pi = 3.14159265358979f;
static float singer(float seconds, int variant) {
    const float f0 = (variant == 0 ? 174.0f : variant == 1 ? 260.0f : 105.0f);
    const float amp = .32f * (0.55f + .45f * std::sin(2*pi*.8f*seconds)*std::sin(2*pi*.8f*seconds));
    const float phase = 2*pi*f0*seconds + .025f*std::sin(2*pi*5.2f*seconds);
    float s = 0;
    for (int h=1;h<12;++h) {
        const float a = (0.13f + std::exp(-.5f*std::pow((f0*h-750.f)/240.f, 2.f))) / std::pow(static_cast<float>(h),.85f);
        s += a * std::sin(h*phase + .14f*h);
    }
    return amp*s;
}
static float music(float seconds, int variant) {
    const float bass = variant==2 ? 65.f : 82.4f;
    const float tone = .13f*std::sin(2*pi*bass*seconds) + .14f*std::sin(2*pi*(bass*3.0f)*seconds);
    const float chord = .18f*std::sin(2*pi*329.6f*seconds+.21f) + .15f*std::sin(2*pi*392.f*seconds+.6f);
    const float cymbal = .018f*std::sin(2*pi*5099.f*seconds)+.018f*std::sin(2*pi*7231.f*seconds);
    return tone+chord+cymbal;
}
static double sidr(const std::vector<float>& target, const std::vector<float>& estimate) {
    assert(target.size()==estimate.size());
    double t2=0, et=0, residual=0;
    for(size_t i=0;i<target.size();++i) {t2+=target[i]*target[i]; et+=target[i]*estimate[i];}
    const double coeff=et/(t2+1e-9);
    for(size_t i=0;i<target.size();++i) {
        const double e=estimate[i]-coeff*target[i]; residual+=e*e;
    }
    return 10.0*std::log10((coeff*coeff*t2+1e-9)/(residual+1e-9));
}
int main() {
    for(int variant=0;variant<3;++variant) {
        auto model=skell::createTinyVocalBackend();
        constexpr int nframes=240;
        std::vector<float> truth, mixture, isolated;
        truth.reserve((nframes-2)*skell::neuralFrameSize);
        mixture.reserve((nframes-2)*skell::neuralFrameSize);
        isolated.reserve((nframes-2)*skell::neuralFrameSize);
        for(int frame=0;frame<nframes;++frame) {
            skell::NeuralFrame in{},out{};
            for(int i=0;i<skell::neuralFrameSize;++i) {
                const float t=(frame*skell::neuralFrameSize+i)/48000.0f;
                in.audio[0][static_cast<size_t>(i)]=singer(t,variant)+music(t,variant);
                in.audio[1][static_cast<size_t>(i)]=in.audio[0][static_cast<size_t>(i)];
            }
            model->process(in,out);
            if(frame >= 2) {
                for(int i=0;i<skell::neuralFrameSize;++i) {
                    const float t=((frame-1)*skell::neuralFrameSize+i)/48000.0f;
                    truth.push_back(singer(t,variant));
                    mixture.push_back(singer(t,variant)+music(t,variant));
                    isolated.push_back(out.audio[0][static_cast<size_t>(i)]);
                }
            }
        }
        std::cout << "procedural mixture " << variant << " inputSI-SDR=" << std::fixed << std::setprecision(2) << sidr(truth,mixture)
                  << "dB neuralSI-SDR=" << sidr(truth,isolated) << "dB\n";
    }
    std::cout << "Synthetic sanity only; no real-singing validation.\n";
}
