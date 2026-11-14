#include "../Source/dsp/FastNeuralHopResearch.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <limits>

struct PreviousHop final : skell::StemgenHopModel {
    skell::StemgenStereoHop previous {};
    bool primed = false;
    bool valid() const noexcept override {return true;}
    void reset() noexcept override {previous = {}; primed=false;}
    bool infer(const skell::StemgenStereoHop& current,
               skell::StemgenStereoHop& output) noexcept override {
        output=previous;
        previous=current;
        bool ready=primed;
        primed=true;
        return ready;
    }
};
int main() {
    static_assert(skell::FastNeuralHopResearch::outputLatencySamples == 384);
    for (bool offline : {true, false}) {
        skell::FastNeuralHopResearch engine;
        assert(engine.start(std::make_unique<PreviousHop>(), offline));
        constexpr int total = 128*150;
        int verified=0;
        for (int i=0; i<total; ++i) {
            float x=0.24f*std::sin(i*0.032f);
            float y=0.13f*std::cos(i*0.047f);
            engine.submitSample(x,y,i);
            std::array<float,2> out{};
            const int sourceSample=i-skell::FastNeuralHopResearch::outputLatencySamples;
            if(sourceSample<0) continue;
            if(engine.readSample(sourceSample,out)) {
                assert(std::abs(out[0]-0.24f*std::sin(sourceSample*0.032f))<1e-6f);
                assert(std::abs(out[1]-0.13f*std::cos(sourceSample*0.047f))<1e-6f);
                ++verified;
            }
        }
        if (offline) {
            assert(verified > 15000);
            assert(engine.missedHops() < 4);
            assert(engine.droppedHops()==0);
            assert(engine.processedHops()>100);
        } else {
            // Synthetic callback racing the worker is allowed to miss. Its
            // fallback must never return time-shifted or partial audio.
            // The unpaced callback may overrun or may be serviced on a
            // particularly fast scheduler. Exact misses are not deterministic.
            assert(engine.rejectedHops()==0);
        }
        engine.stop();
        std::printf("Fast neural transport %s: 384 PDC, %d aligned samples, %llu misses, %llu dropped, %llu late\n",
            offline?"offline":"unpaced live stress", verified,
            (unsigned long long)engine.missedHops(),
            (unsigned long long)engine.droppedHops(),
            (unsigned long long)engine.skippedLateHops());
    }
}
