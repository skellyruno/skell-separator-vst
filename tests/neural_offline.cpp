#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <thread>
#include <chrono>
#include "dsp/StreamingNeural.h"

// Simulates a model which produces one 480-sample frame of look-behind.
struct DelayedModel final : skell::NeuralFrameBackend {
    skell::NeuralFrame previous {};
    bool valid() const noexcept override { return true; }
    void reset() noexcept override { previous = {}; }
    void process(const skell::NeuralFrame& in, skell::NeuralFrame& out) noexcept override {
        // Deliberately expensive compared with a fast offline render.
        std::this_thread::sleep_for(std::chrono::microseconds(150));
        out.audio = previous.audio;
        previous = in;
    }
};

std::array<double, 2> reference(int sample) {
    const double t = static_cast<double>(sample);
    return {0.3 * std::sin(t * .013) + 0.11 * std::sin(t * .037),
            -0.21 * std::cos(t * .017) + 0.08 * std::sin(t * .049)};
}

void testOfflineNeverMisses() {
    skell::StreamingNeural stream;
    assert(stream.start(std::make_unique<DelayedModel>(), true));
    assert(stream.active() && stream.rendersOffline());
    constexpr int samples = 80 * skell::neuralFrameSize;
    int ready = 0;
    for (int i = 0; i < samples; ++i) {
        const auto in = reference(i);
        stream.submitSample(static_cast<float>(in[0]), static_cast<float>(in[1]), i);
        std::array<float, 2> out {};
        const int source = i - skell::neuralContractLatency;
        const bool available = stream.readSample(source, out);
        if (source >= 0) {
            assert(available);
            const auto expected = reference(source);
            assert(std::fabs(out[0] - static_cast<float>(expected[0])) < 1e-6f);
            assert(std::fabs(out[1] - static_cast<float>(expected[1])) < 1e-6f);
            ++ready;
        } else assert(!available);
    }
    assert(ready == samples - skell::neuralContractLatency);
    assert(stream.droppedFrames() == 0);
    assert(stream.missedDeadlines() == 0);
    assert(stream.staleFrames() == 0);
    assert(stream.completedFrames() == 79);
    stream.stop();
    std::cout << "accelerated offline render: complete neural output at exact 2880-sample delay: PASS\n";
}

struct InvalidModel final : skell::NeuralFrameBackend {
    skell::NeuralFrame previous {};
    bool valid() const noexcept override { return true; }
    void reset() noexcept override { previous = {}; }
    void process(const skell::NeuralFrame& input, skell::NeuralFrame& output) noexcept override {
        output.audio = previous.audio;
        if (input.startSample == 10 * skell::neuralFrameSize)
            output.audio[0][9] = std::numeric_limits<float>::quiet_NaN();
        previous = input;
    }
};

void testOfflineCorruptionResync() {
    skell::StreamingNeural stream;
    assert(stream.start(std::make_unique<InvalidModel>(), true));
    int absent = 0, valid = 0;
    for (int i = 0; i < 24 * skell::neuralFrameSize; ++i) {
        stream.submitSample(0.3f, -0.2f, i);
        std::array<float, 2> out { -2.0f, -2.0f };
        const int source = i - skell::neuralContractLatency;
        const bool present = stream.readSample(source, out);
        if (source >= 0) {
            if (!present) {
                ++absent;
                assert(out[0] == -2.0f && out[1] == -2.0f);
            } else {
                ++valid;
                assert(std::isfinite(out[0]) && std::isfinite(out[1]));
            }
        }
    }
    assert(stream.rejectedFrames() == 1);
    assert(absent >= skell::neuralFrameSize);
    assert(valid > 2 * skell::neuralFrameSize);
    assert(stream.droppedFrames() == 0);
    std::cout << "offline invalid-model frame: rejected, then re-primed without corrupted output: PASS\n";
}

int main() {
    testOfflineNeverMisses();
    testOfflineCorruptionResync();
}
