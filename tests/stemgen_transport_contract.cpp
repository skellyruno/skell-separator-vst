#include "../Source/dsp/StemgenRTBridge.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>

struct HopDelay final : skell::StemgenHopModel {
    skell::StemgenStereoHop previous {};
    bool primed = false;
    bool valid() const noexcept override { return true; }
    void reset() noexcept override { previous = {}; primed = false; }
    bool infer(const skell::StemgenStereoHop& current,
               skell::StemgenStereoHop& output) noexcept override {
        const bool wasPrimed = primed;
        output = previous;
        previous = current;
        primed = true;
        return wasPrimed;
    }
};
int main() {
    skell::StreamingNeural engine;
    assert(engine.start(std::make_unique<skell::StemgenRTBridge>(std::make_unique<HopDelay>()), true));
    assert(engine.rendersOffline());
    int verified = 0;
    // Offline rendering is synchronous and invokes real 128-hop boundary code.
    constexpr int samples = 480 * 70;
    for (int i = 0; i < samples; ++i) {
        const float left = .18f * std::sin(float(i) * .011f);
        const float right = .11f * std::cos(float(i) * .039f);
        engine.submitSample(left, right, i, true);
        std::array<float, 2> out {};
        const int orig = i - skell::neuralContractLatency;
        if (orig < 0) continue;
        if (engine.readSample(orig, out)) {
            ++verified;
            assert(std::abs(out[0] - .18f * std::sin(float(orig) * .011f)) < 1.e-6f);
            assert(std::abs(out[1] - .11f * std::cos(float(orig) * .039f)) < 1.e-6f);
        }
    }
    assert(verified > 15000);
    assert(engine.rejectedFrames() == 0);
    assert(engine.droppedFrames() == 0);
    assert(engine.missedDeadlines() < 6);
    engine.stop();
    std::printf("StemgenRT model-hop bridge + Skell 2880 sample transport: PASS (%d verified samples)\n", verified);
}
