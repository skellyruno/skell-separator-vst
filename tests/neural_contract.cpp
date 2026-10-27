#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <limits>
#include <thread>
#include <chrono>
#include "dsp/StreamingNeural.h"
#include "dsp/LatencyAlignedStems.h"

struct FrameDelayTestModel : skell::NeuralFrameBackend {
    skell::NeuralFrame previous {};
    void reset() noexcept override { previous = {}; }
    bool valid() const noexcept override { return true; }
    void process(const skell::NeuralFrame& input,
                 skell::NeuralFrame& output) noexcept override {
        output = previous;
        previous = input;
    }
};

// This model deliberately corrupts TWO different delayed output frames.
// A single bad sample must invalidate the entire frame on the worker thread,
// never be converted to a sudden zero in the audio callback.
struct CorruptTestModel : skell::NeuralFrameBackend {
    skell::NeuralFrame previous {};
    bool valid() const noexcept override { return true; }
    void reset() noexcept override { previous = {}; }
    void process(const skell::NeuralFrame& input,
                 skell::NeuralFrame& output) noexcept override {
        output = previous;
        if (output.startSample == 4 * skell::neuralFrameSize)
            output.audio[0][17] = std::numeric_limits<float>::quiet_NaN();
        if (output.startSample == 7 * skell::neuralFrameSize)
            output.audio[1][29] = 10.0f;
        previous = input;
    }
};

static void testWholeFrameCorruptionPolicy()
{
    skell::StreamingNeural stream;
    assert(stream.start(std::make_unique<CorruptTestModel>()));
    int validBefore = 0, validAfter = 0;
    int invalidDelivered = 0;
    constexpr int totalSamples = 24 * skell::neuralFrameSize;
    for (int i = 0; i < totalSamples; ++i) {
        stream.submitSample(0.25f, -0.125f, i);
        std::array<float, 2> out { -777.0f, -777.0f };
        const auto t = i - skell::neuralContractLatency;
        const bool ready = stream.readSample(t, out);
        if (t >= 0) {
            const auto index = t / skell::neuralFrameSize;
            if (index == 4 || index == 7) {
                if (ready) ++invalidDelivered;
                assert(!ready);
                // Failed reads must not overwrite the caller's output.
                assert(out[0] == -777.0f && out[1] == -777.0f);
            } else if (ready) {
                assert(std::isfinite(out[0]) && std::isfinite(out[1]));
                if (index < 4) ++validBefore;
                if (index > 7) ++validAfter;
            }
        }
        // As in the positive timing contract, provide scheduling headroom.
        if (i % 96 == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(invalidDelivered == 0);
    assert(stream.rejectedFrames() == 2);
    assert(validBefore > 100 && validAfter > 100);
    std::cout << "whole-frame NaN/range rejection with recovery: PASS\n";
    stream.stop();
}

// A backend that forgets to write part of its frame must never silently
// inject zero voice. Worker NaN sentinels detect incomplete model output.
struct IncompleteOutputTestModel : skell::NeuralFrameBackend {
    bool valid() const noexcept override { return true; }
    void reset() noexcept override {}
    void process(const skell::NeuralFrame&, skell::NeuralFrame& output) noexcept override {
        output.audio[0][0] = 0.2f;
        output.audio[1][0] = -0.2f;
    }
};
static void testIncompleteOutputRejected()
{
    skell::StreamingNeural stream;
    assert(stream.start(std::make_unique<IncompleteOutputTestModel>()));
    for (int i = 0; i < 18 * skell::neuralFrameSize; ++i) {
        stream.submitSample(0.2f, -0.2f, i);
        std::array<float, 2> out {};
        assert(!stream.readSample(i - skell::neuralContractLatency, out));
        if (i % 96 == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(stream.rejectedFrames() >= 3);
    stream.stop();
    std::cout << "incomplete neural output rejected: PASS\n";
}

static void testAlignedClassicalPath()
{
    static_assert(LatencyAlignedStems::skellLatency() == 2880,
                  "Neural build must advertise the true fixed alignment delay");
    SpectralAnalyzer a;
    Separator processor(a);
    processor.prepare(48000.0);
    LatencyAlignedStems alignment;
    alignment.reset();
    float maxReconstructionError = 0.0f;
    constexpr int frames = 18000;
    std::array<float, frames> original {};
    for (int sample = 0; sample < frames; ++sample) {
        // Wideband + impulse stimulus, suitable for exposing frame shift.
        const float x = (sample % 997 == 7 ? 0.8f : 0.0f)
                      + 0.07f * std::sin(0.008f * static_cast<float>(sample));
        original[static_cast<std::size_t>(sample)] = x;
        Separator::Stems stems {};
        std::array<float, 2> dry {};
        processor.processSample(x, -0.5f * x, 0.0f, 1, stems, dry);
        alignment.process(stems, dry);
        const float expected = sample >= LatencyAlignedStems::skellLatency()
              ? original[static_cast<std::size_t>(sample - LatencyAlignedStems::skellLatency())] : 0.0f;
        const float reconstructed = stems[0][0] + stems[1][0] + stems[2][0];
        maxReconstructionError = std::max(maxReconstructionError, std::fabs(reconstructed - expected));
        assert(std::fabs(dry[0] - expected) < 2e-6f);
    }
    std::cout << "aligned classical path 2880 samples; max reconstruction error "
              << maxReconstructionError << "\n";
    assert(maxReconstructionError < 2e-6f);
}

static void testAsynchronousNeural()
{
    skell::StreamingNeural stream;
    assert(stream.start(std::make_unique<FrameDelayTestModel>()));
    constexpr int frames = 55;
    constexpr int size = frames * skell::neuralFrameSize;
    int present = 0, missing = 0;
    // Feed slightly slower than a live stream to keep OS scheduling noise out
    // of the positive-contract test. The worker never runs on this thread.
    for (int sample = 0; sample < size; ++sample) {
        const auto s = static_cast<float>(sample);
        stream.submitSample(0.6f * std::sin(0.021f * s),
                            -0.35f * std::cos(0.018f * s), sample);
        std::array<float, 2> output {};
        const auto origin = sample - skell::neuralContractLatency;
        if (stream.readSample(origin, output)) {
            ++present;
            assert(std::fabs(output[0] - 0.6f * std::sin(0.021f * static_cast<float>(origin))) < 1.0e-5f);
            assert(std::fabs(output[1] + 0.35f * std::cos(0.018f * static_cast<float>(origin))) < 1.0e-5f);
        } else if (origin >= 0) ++missing;
        if (sample % 96 == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(present > 10000); // no assumption that every OS deadline will pass
    std::cout << "asynchronous mock model: readySamples=" << present << " lateSamples="
              << missing << " missedFrames=" << stream.missedDeadlines() << "\n";
    stream.stop();

    // Simulate accelerated offline rendering: inference cannot keep up and the
    // plugin must return absent frames, not stall the audio callback or time-shift.
    assert(stream.start(std::make_unique<FrameDelayTestModel>()));
    for (int sample = 0; sample < 65000; ++sample) {
        stream.submitSample(0.2f, -0.2f, sample);
        std::array<float, 2> output {};
        (void) stream.readSample(sample - skell::neuralContractLatency, output);
    }
    assert(stream.missedDeadlines() > 0);
    stream.stop();
    std::cout << "accelerated-render deadline policy passed\n";
}

int main() {
    testAlignedClassicalPath();
    testAsynchronousNeural();
    testWholeFrameCorruptionPolicy();
    testIncompleteOutputRejected();
#if defined(SKELL_TEST_RNNOISE_API)
    auto model = skell::createRNNoiseBackend();
    assert(model && model->valid());
    skell::NeuralFrame in {}, out {};
    for (int i = 0; i < skell::neuralFrameSize; ++i) {
        in.audio[0][static_cast<size_t>(i)] = 0.25f;
        in.audio[1][static_cast<size_t>(i)] = -0.375f;
    }
    model->reset();
    model->process(in, out); // the previous frame is zero
    model->process(in, out); // mock model returns the previous input frame
    for (int i = 0; i < skell::neuralFrameSize; ++i) {
        assert(std::fabs(out.audio[0][static_cast<size_t>(i)] - 0.25f) < 1e-6f);
        assert(std::fabs(out.audio[1][static_cast<size_t>(i)] + 0.375f) < 1e-6f);
    }
    std::cout << "RNNoise C ABI adapter mock contract passed\n";
#endif
    std::cout << "neural transport regression passed\n";
}
