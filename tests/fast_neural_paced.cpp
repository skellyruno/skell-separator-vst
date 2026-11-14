#include "../Source/dsp/FastNeuralHopResearch.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <thread>

struct PacedModel final : skell::StemgenHopModel {
    explicit PacedModel(int workMicros) : work(workMicros) {}
    skell::StemgenStereoHop previous {};
    bool primed = false;
    int work;
    bool valid() const noexcept override { return true; }
    void reset() noexcept override {previous={};primed=false;}
    bool infer(const skell::StemgenStereoHop& in, skell::StemgenStereoHop& out) noexcept override {
        // Mock CPU cost, not ONNX. Run in the worker; never stall the callback.
        if (work) std::this_thread::sleep_for(std::chrono::microseconds(work));
        out = previous;
        previous = in;
        const bool wasPrimed = primed;
        primed = true;
        return wasPrimed;
    }
};
int main() {
    constexpr int fs=44100, hop=128, count=256;
    for (const int work : {0, 750, 1600, 3400}) {
        skell::FastNeuralHopResearch pipe;
        assert(pipe.start(std::make_unique<PacedModel>(work), false));
        const auto start=std::chrono::steady_clock::now();
        int correct=0, invalid=0;
        for (int h=0; h<count; ++h) {
            const auto due = start+std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>((h+1)*double(hop)/fs));
            for (int j=0;j<hop;++j) {
                const int i=h*hop+j;
                pipe.submitSample(.2f*std::sin(i*.025f), .17f*std::cos(i*.037f), i);
                const int original=i-skell::FastNeuralHopResearch::outputLatencySamples;
                if (original<0) continue;
                std::array<float,2> out{};
                if (pipe.readSample(original,out)) {
                    assert(std::abs(out[0]-.2f*std::sin(original*.025f))<1.e-6f);
                    assert(std::abs(out[1]-.17f*std::cos(original*.037f))<1.e-6f);
                    ++correct;
                } else ++invalid;
            }
            std::this_thread::sleep_until(due);
        }
        pipe.stop();
        const auto missed=pipe.missedHops();
        std::printf("paced 44.1k/128 mock infer=%d us: good=%d missing=%d (%llu missed hops), late=%llu, drops=%llu\n",
                    work,correct,invalid,(unsigned long long)missed,
                    (unsigned long long)pipe.skippedLateHops(),
                    (unsigned long long)pipe.droppedHops());
        assert(correct+invalid==(count-3)*hop);
        assert(pipe.rejectedHops()==0);
        if (work==0) assert(correct>(count-8)*hop);
        // An intentionally over-budget model must not shift output or block
        // callback; it is expected to miss some deadlines on paced simulation.
        if (work==3400) assert(missed>0);
    }
    puts("Paced neural scheduling research: PASS (mock model, not ONNX)");
}
