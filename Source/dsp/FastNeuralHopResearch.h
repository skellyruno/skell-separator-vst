#pragma once
// Experimental low-latency scheduling transport for a 44.1 kHz 128-hop
// stateful vocal predictor. NOT active in the shipping audio callback.
// A one-hop-lookbehind predictor + one complete 128-sample inference reserve
// requires 384 samples of host PDC (8.71 ms at 44.1 kHz). This design is
// deliberately more conservative than promising 256 PDC with no worker time.
// Only prepare/start/stop allocate or join. submitSample/readSample are bounded,
// lock-free SPSC operations and never call the predictor on a live callback.
#include "StemgenRTBridge.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>

namespace skell {

class FastNeuralHopResearch final {
public:
    static constexpr int hopSize = stemgenHopSize;
    static constexpr int outputLatencySamples = hopSize * 3;
    static constexpr int queueSize = 32;
    static_assert(outputLatencySamples == 384);
    FastNeuralHopResearch() = default;
    ~FastNeuralHopResearch() { stop(); }
    FastNeuralHopResearch(const FastNeuralHopResearch&) = delete;
    FastNeuralHopResearch& operator=(const FastNeuralHopResearch&) = delete;

    bool start(std::unique_ptr<StemgenHopModel> backend, bool offline = false) {
        stop();
        input.reset();
        output.reset();
        cache = {};
        collecting = {};
        selectedStart = -1;
        selectedValid = false;
        fill = 0;
        previousStart = -1;
        missed.store(0);
        dropped.store(0);
        rejected.store(0);
        processed.store(0);
        late.store(0);
        latestInputStart.store(-1);
        if (!backend || !backend->valid()) return false;
        model = std::move(backend);
        inlineMode = offline;
        running.store(true, std::memory_order_release);
        if (!offline) worker = std::thread([this] { workerLoop(); });
        return true;
    }
    void stop() noexcept {
        running.store(false, std::memory_order_release);
        if (worker.joinable()) worker.join();
        model.reset();
    }
    bool active() const noexcept { return running.load(std::memory_order_acquire); }
    bool offline() const noexcept { return inlineMode; }
    // The absolute sample index must be contiguous from zero or a 128-sample
    // boundary. Discontinuities drop the partial hop, and the backend resets.
    void submitSample(float l, float r, std::int64_t sample) noexcept {
        if (!active()) return;
        if (fill == 0) {
            if (sample < 0 || sample % hopSize != 0) return;
            collecting.start = sample;
        } else if (sample != collecting.start + fill) {
            fill = 0;
            return;
        }
        const auto at = static_cast<std::size_t>(fill);
        collecting.audio[0][at] = std::isfinite(l) ? std::clamp(l, -8.f, 8.f) : 0.f;
        collecting.audio[1][at] = std::isfinite(r) ? std::clamp(r, -8.f, 8.f) : 0.f;
        if (++fill == hopSize) {
            latestInputStart.store(collecting.start, std::memory_order_release);
            if (inlineMode) inferHop(collecting);
            else if (!input.push(collecting)) dropped.fetch_add(1, std::memory_order_relaxed);
            fill = 0;
        }
    }
    bool readSample(std::int64_t originalSample, std::array<float, 2>& sample) noexcept {
        if (!active() || originalSample < 0) return false;
        const auto start = (originalSample / hopSize) * hopSize;
        if (selectedStart != start) {
            Hop done;
            while (output.pop(done)) {
                // No late frame may overwrite a still-usable future frame.
                if (done.start >= start)
                    cache[static_cast<std::size_t>((done.start / hopSize) % cache.size())] = done;
            }
            selectedStart = start;
            const auto& frame = cache[static_cast<std::size_t>((start / hopSize) % cache.size())];
            selectedValid = frame.start == start && frame.valid;
            if (!selectedValid) missed.fetch_add(1, std::memory_order_relaxed);
        }
        if (!selectedValid) return false;
        const auto& frame = cache[static_cast<std::size_t>((start / hopSize) % cache.size())];
        const auto i = static_cast<std::size_t>(originalSample - start);
        sample = {frame.audio[0][i], frame.audio[1][i]};
        return true;
    }
    std::uint64_t missedHops() const noexcept { return missed.load(); }
    std::uint64_t droppedHops() const noexcept { return dropped.load(); }
    std::uint64_t rejectedHops() const noexcept { return rejected.load(); }
    std::uint64_t processedHops() const noexcept { return processed.load(); }
    std::uint64_t skippedLateHops() const noexcept { return late.load(); }

private:
    struct Hop {
        std::int64_t start = -1;
        bool valid = false;
        StemgenStereoHop audio {};
    };
    template<int N> struct Queue {
        std::array<Hop, N> frames {};
        std::atomic<std::uint64_t> head{0}, tail{0};
        void reset() noexcept { head.store(0); tail.store(0); }
        bool push(const Hop& hop) noexcept {
            const auto h = head.load(std::memory_order_relaxed);
            if (h - tail.load(std::memory_order_acquire) >= N) return false;
            frames[static_cast<std::size_t>(h % N)] = hop;
            head.store(h + 1, std::memory_order_release);
            return true;
        }
        bool pop(Hop& hop) noexcept {
            const auto t = tail.load(std::memory_order_relaxed);
            if (t == head.load(std::memory_order_acquire)) return false;
            hop = frames[static_cast<std::size_t>(t % N)];
            tail.store(t + 1, std::memory_order_release);
            return true;
        }
    };
    void workerLoop() noexcept {
        while (running.load(std::memory_order_acquire)) {
            Hop hop;
            if (!input.pop(hop)) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                continue;
            }
            // The model's output is for the previous hop; skip input that
            // cannot publish the corresponding delayed result in time.
            const auto latest = latestInputStart.load(std::memory_order_acquire);
            if (latest - hop.start >= outputLatencySamples - hopSize) {
                previousStart = -1;
                late.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            inferHop(hop);
        }
    }
    void inferHop(const Hop& hop) noexcept {
        if (previousStart < 0 || hop.start != previousStart + hopSize)
            model->reset();
        StemgenStereoHop vocal {};
        const bool valid = model->infer(hop.audio, vocal);
        // False is legal for the initial pre-roll only. A later failure
        // invalidates model history; resync on the next hop.
        if (!valid && previousStart >= 0) {
            rejected.fetch_add(1, std::memory_order_relaxed);
            model->reset();
            previousStart = -1;
            return;
        }
        if (!model->valid()) {
            rejected.fetch_add(1, std::memory_order_relaxed);
            previousStart = -1;
            return;
        }
        if (valid && previousStart >= 0) {
            Hop result;
            result.start = previousStart;
            result.valid = true;
            result.audio = vocal;
            for (const auto& channel : result.audio)
                for (const float s : channel)
                    if (!std::isfinite(s) || std::abs(s) > 8.f) result.valid = false;
            if (!result.valid) {
                rejected.fetch_add(1, std::memory_order_relaxed);
                model->reset();
                previousStart = -1;
                return;
            }
            if (!output.push(result)) dropped.fetch_add(1, std::memory_order_relaxed);
            else processed.fetch_add(1, std::memory_order_relaxed);
        }
        previousStart = hop.start;
    }
    Queue<queueSize> input, output;
    std::array<Hop, 8> cache {};
    Hop collecting {};
    std::unique_ptr<StemgenHopModel> model;
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<std::uint64_t> missed{0}, dropped{0}, rejected{0}, processed{0}, late{0};
    std::atomic<std::int64_t> latestInputStart{-1};
    std::int64_t selectedStart = -1, previousStart = -1;
    bool selectedValid = false, inlineMode = false;
    int fill = 0;
};

} // namespace skell
