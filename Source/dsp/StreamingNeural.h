#pragma once
// Fixed-contract 48 kHz streaming inference transport. All nontrivial model
// work is confined to the worker thread, never the host's audio callback.
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

namespace skell {
constexpr int neuralFrameSize = 480;                  // 10 ms at 48 kHz
constexpr int neuralContractLatency = 2880;          // 60 ms, all paths in neural build
constexpr int neuralQueueCapacity = 32;

struct NeuralFrame {
    std::int64_t startSample = -1;
    bool valid = true; // A corrupted inference result invalidates the WHOLE 10 ms frame.
    std::array<std::array<float, neuralFrameSize>, 2> audio {};
};

// Contract: one call per 480-sample frame; result is for the PREVIOUS input
// frame (RNNoise's built-in one-frame delay). The transport labels results.
struct NeuralFrameBackend {
    virtual ~NeuralFrameBackend() = default;
    virtual bool valid() const noexcept = 0;
    virtual void reset() noexcept = 0;
    virtual void process(const NeuralFrame& input, NeuralFrame& output) noexcept = 0;
};

// Creates null if the optional BSD-3-Clause RNNoise SDK was not linked.
std::unique_ptr<NeuralFrameBackend> createRNNoiseBackend();

// Single-producer/single-consumer bounded queues; no mutexes, heap allocations,
// waits, or model inference on the audio callback. Only call start()/stop()
// from prepare/release (host non-real-time thread).
class StreamingNeural final {
public:
    StreamingNeural() = default;
    ~StreamingNeural() { stop(); }
    StreamingNeural(const StreamingNeural&) = delete;
    StreamingNeural& operator=(const StreamingNeural&) = delete;

    bool start(std::unique_ptr<NeuralFrameBackend>);
    void stop() noexcept;
    void submitSample(float left, float right, std::int64_t sampleIndex) noexcept;
    // Request the output corresponding to this original input sample index.
    // A late result must never shift the input/output timeline.
    bool readSample(std::int64_t originalSample, std::array<float, 2>& out) noexcept;
    bool active() const noexcept { return enabled.load(std::memory_order_relaxed); }
    std::uint64_t droppedFrames() const noexcept { return dropped.load(std::memory_order_relaxed); }
    std::uint64_t missedDeadlines() const noexcept { return missed.load(std::memory_order_relaxed); }
    std::uint64_t completedFrames() const noexcept { return completed.load(std::memory_order_relaxed); }
    std::uint64_t rejectedFrames() const noexcept { return rejected.load(std::memory_order_relaxed); }
    double meanWorkerFrameUs() const noexcept {
        const auto n = workerFrames.load(std::memory_order_relaxed);
        return n ? static_cast<double>(workerTimeUs.load(std::memory_order_relaxed)) / n : 0.0;
    }
    std::uint64_t maxWorkerFrameUs() const noexcept { return workerMaxUs.load(std::memory_order_relaxed); }

private:
    template<int Capacity> struct Queue {
        std::array<NeuralFrame, Capacity> frames {};
        std::atomic<std::uint64_t> head { 0 }, tail { 0 };
        bool push(const NeuralFrame& f) noexcept {
            const auto h = head.load(std::memory_order_relaxed);
            if (h - tail.load(std::memory_order_acquire) >= Capacity) return false;
            frames[static_cast<std::size_t>(h % Capacity)] = f;
            head.store(h + 1, std::memory_order_release);
            return true;
        }
        bool pop(NeuralFrame& f) noexcept {
            const auto t = tail.load(std::memory_order_relaxed);
            if (t == head.load(std::memory_order_acquire)) return false;
            f = frames[static_cast<std::size_t>(t % Capacity)];
            tail.store(t + 1, std::memory_order_release);
            return true;
        }
        void clear() noexcept {
            head.store(0, std::memory_order_relaxed);
            tail.store(0, std::memory_order_relaxed);
        }
    };
    void threadMain() noexcept;
    Queue<neuralQueueCapacity> incoming, outgoing;
    std::array<NeuralFrame, 16> ready {};   // audio-thread-owned cache
    NeuralFrame collecting {};
    std::unique_ptr<NeuralFrameBackend> backend;
    std::thread worker;
    std::atomic<bool> running { false }, enabled { false };
    std::atomic<std::uint64_t> dropped { 0 }, missed { 0 }, completed { 0 }, rejected { 0 };
    std::atomic<std::uint64_t> workerFrames { 0 }, workerTimeUs { 0 }, workerMaxUs { 0 };
    int fill = 0;
    std::int64_t selectedFrame = -1;
    bool selectedValid = false;
};
}
