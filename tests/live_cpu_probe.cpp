#include "../Source/dsp/LiveVocalCleanup.h"
#include <chrono>
#include <cmath>
#include <cstdio>
int main(){
    skell::LiveVocalCleanup p;
    p.prepare(48000);
    p.setControls(1.f,1.f,.35f);
    volatile float sink = 0;
    constexpr int n=48000*15;
    const auto begin=std::chrono::steady_clock::now();
    for(int i=0;i<n;i++){
        auto v=.15f*std::sin(float(i)*.03f)+.003f*std::sin(float(i)*.78f);
        auto out=p.process(v,-v);
        sink+=out[0];
    }
    const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    const auto per256=seconds*1e6*256/n;
    const auto deadline=1e6*256.0/48000;
    std::printf("LIVE Clean+Gate 48k: %.1f us / 256 samples = %.2f%% of 48k/256 buffer deadline; checksum %f\n",per256,100.0*per256/deadline,sink);
}
