#include "StreamingNeural.h"
#include <chrono>
#include <cmath>
#include <utility>

#if defined(SKELL_WITH_RNNOISE) && SKELL_WITH_RNNOISE && !defined(SKELL_NEURAL_TEST_STUB)
#include <rnnoise.h>
#endif

namespace skell {
#if defined(SKELL_WITH_RNNOISE) && SKELL_WITH_RNNOISE && !defined(SKELL_NEURAL_TEST_STUB)
namespace {
class RNNoiseBackend final : public NeuralFrameBackend {
public:
    RNNoiseBackend() { for (auto& state : states) state = rnnoise_create(nullptr); }
    ~RNNoiseBackend() override { for (auto* state : states) if (state) rnnoise_destroy(state); }
    bool valid() const noexcept override {
        return states[0] != nullptr && states[1] != nullptr
            && rnnoise_get_frame_size() == neuralFrameSize;
    }
    void reset() noexcept override {
        for (auto* state : states) if (state) rnnoise_init(state, nullptr);
    }
    void process(const NeuralFrame& in, NeuralFrame& out) noexcept override {
        for (int channel = 0; channel < 2; ++channel) {
            // RNNoise expects float samples on the int16 PCM scale, NOT [-1, 1].
            for (int i = 0; i < neuralFrameSize; ++i)
                scaled[static_cast<std::size_t>(i)] =
                    in.audio[static_cast<std::size_t>(channel)][static_cast<std::size_t>(i)] * 32768.0f;
            rnnoise_process_frame(states[static_cast<std::size_t>(channel)],
                                  result.data(), scaled.data());
            for (int i = 0; i < neuralFrameSize; ++i) {
                const float value = result[static_cast<std::size_t>(i)] * (1.0f / 32768.0f);
                out.audio[static_cast<std::size_t>(channel)][static_cast<std::size_t>(i)] =
                    std::isfinite(value) ? value : 0.0f;
            }
        }
    }
private:
    std::array<DenoiseState*, 2> states { nullptr, nullptr };
    std::array<float, neuralFrameSize> scaled {}, result {};
};
}
std::unique_ptr<NeuralFrameBackend> createRNNoiseBackend() {
    auto engine = std::make_unique<RNNoiseBackend>();
    return engine->valid() ? std::move(engine) : nullptr;
}
#else
std::unique_ptr<NeuralFrameBackend> createRNNoiseBackend() { return {}; }
#endif

bool StreamingNeural::start(std::unique_ptr<NeuralFrameBackend> newBackend) {
    stop();
    incoming.clear(); outgoing.clear();
    for (auto& r : ready) r.startSample = -1;
    collecting = {};
    fill = 0;
    selectedFrame = -1;
    selectedValid = false;
    dropped.store(0, std::memory_order_relaxed);
    missed.store(0, std::memory_order_relaxed);
    completed.store(0, std::memory_order_relaxed);
    workerFrames.store(0, std::memory_order_relaxed);
    workerTimeUs.store(0, std::memory_order_relaxed);
    workerMaxUs.store(0, std::memory_order_relaxed);
    if (!newBackend || !newBackend->valid()) return false;
    backend = std::move(newBackend);
    running.store(true, std::memory_order_release);
    worker = std::thread([this] { threadMain(); });
    enabled.store(true, std::memory_order_release);
    return true;
}

void StreamingNeural::stop() noexcept {
    enabled.store(false, std::memory_order_release);
    running.store(false, std::memory_order_release);
    if (worker.joinable()) worker.join();
    backend.reset();
}

void StreamingNeural::threadMain() noexcept {
    NeuralFrame frame, result;
    std::int64_t previousStart = -1;
    while (running.load(std::memory_order_acquire)) {
        if (!incoming.pop(frame)) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
            continue;
        }
        if (previousStart < 0 || frame.startSample != previousStart + neuralFrameSize) {
            backend->reset();
            previousStart = -1;
        }
        const auto started = std::chrono::steady_clock::now();
        backend->process(frame, result);
        const auto durationUs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started).count());
        workerTimeUs.fetch_add(durationUs, std::memory_order_relaxed);
        workerFrames.fetch_add(1, std::memory_order_relaxed);
        auto maxUs = workerMaxUs.load(std::memory_order_relaxed);
        while (durationUs > maxUs &&
               !workerMaxUs.compare_exchange_weak(maxUs, durationUs, std::memory_order_relaxed)) {}
        if (previousStart >= 0) {
            result.startSample = previousStart;
            if (!outgoing.push(result)) dropped.fetch_add(1, std::memory_order_relaxed);
            else completed.fetch_add(1, std::memory_order_relaxed);
        }
        previousStart = frame.startSample;
    }
}

void StreamingNeural::submitSample(float left, float right, std::int64_t sampleIndex) noexcept {
    if (!active()) return;
    if (fill == 0) collecting.startSample = sampleIndex;
    const auto index = static_cast<std::size_t>(fill);
    collecting.audio[0][index] = std::isfinite(left) ? left : 0.0f;
    collecting.audio[1][index] = std::isfinite(right) ? right : 0.0f;
    if (++fill == neuralFrameSize) {
        if (!incoming.push(collecting)) dropped.fetch_add(1, std::memory_order_relaxed);
        fill = 0;
    }
}

bool StreamingNeural::readSample(std::int64_t originalSample,
                                std::array<float, 2>& out) noexcept {
    if (!active() || originalSample < 0) return false;
    const auto frameStart = (originalSample / neuralFrameSize) * neuralFrameSize;
    if (frameStart != selectedFrame) {
        // Freeze each frame's availability at its FIRST output sample: no
        // partial frame switching when an inference result arrives late.
        NeuralFrame newFrame;
        while (outgoing.pop(newFrame))
            ready[static_cast<std::size_t>((newFrame.startSample / neuralFrameSize) % ready.size())] = newFrame;
        selectedFrame = frameStart;
        const auto& cached = ready[static_cast<std::size_t>((frameStart / neuralFrameSize) % ready.size())];
        selectedValid = cached.startSample == frameStart;
        if (!selectedValid) missed.fetch_add(1, std::memory_order_relaxed);
    }
    if (!selectedValid) return false;
    const auto index = static_cast<std::size_t>(originalSample - frameStart);
    const auto& cached = ready[static_cast<std::size_t>((frameStart / neuralFrameSize) % ready.size())];
    out = { cached.audio[0][index], cached.audio[1][index] };
    return true;
}
}
