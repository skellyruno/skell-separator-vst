#include "../Source/dsp/StemgenRTBridge.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>

struct DelayedHop : skell::StemgenHopModel {
    skell::StemgenStereoHop previous {};
    bool primed = false;
    int calls = 0;
    bool good = true;
    bool valid() const noexcept override { return good; }
    void reset() noexcept override { primed = false; previous = {}; }
    bool infer(const skell::StemgenStereoHop& input, skell::StemgenStereoHop& output) noexcept override {
        ++calls;
        output = previous;
        const bool wasPrimed = primed;
        previous = input;
        primed = true;
        return wasPrimed;
    }
};

static skell::NeuralFrame makeFrame(int n) {
    skell::NeuralFrame f;
    f.startSample = static_cast<std::int64_t>(n) * skell::neuralFrameSize;
    for (std::size_t i = 0; i < skell::neuralFrameSize; ++i)
        for (int ch = 0; ch < 2; ++ch)
            f.audio[static_cast<size_t>(ch)][i] = 0.27f * std::sin(static_cast<float>(n * skell::neuralFrameSize + i) * (ch ? .081f : .071f));
    return f;
}

int main() {
    auto m = std::make_unique<DelayedHop>();
    auto* raw = m.get();
    skell::StemgenRTBridge b(std::move(m));
    assert(b.valid());
    for (int n = 0; n < 200; ++n) {
        const auto in = makeFrame(n);
        skell::NeuralFrame out;
        b.process(in, out);
        assert(out.valid);
        if (n > 0) {
            const auto previous = makeFrame(n - 1);
            for (size_t i = 0; i < skell::neuralFrameSize; ++i)
                for (size_t ch = 0; ch < 2; ++ch)
                    assert(std::abs(out.audio[ch][i] - previous.audio[ch][i]) < 1e-8f);
        }
    }
    assert(raw->calls == 750); // 200*480 / 128
    b.reset();
    assert(raw->calls == 750);
    for (int n = 0; n < 12; ++n) {
        auto input = makeFrame(n);
        skell::NeuralFrame output;
        b.process(input, output);
        assert(output.valid);
        if (n > 0) {
            const auto previous = makeFrame(n - 1);
            assert(output.audio[0][0] == previous.audio[0][0]);
        }
    }
    {
        auto bad = makeFrame(12);
        bad.audio[0][10] = std::numeric_limits<float>::quiet_NaN();
        skell::NeuralFrame output;
        b.process(bad, output);
        assert(!output.valid);
        b.reset();
        b.process(makeFrame(0), output);
        assert(output.valid);
    }
    puts("StemgenRT 44.1kHz/128-hop -> Skell 480-frame latency, reset and invalid-input contract: PASS");
}
