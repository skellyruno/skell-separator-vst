#pragma once
#include "Separator.h"
#include <array>
#include <cstdint>

// Delay the 1024-sample classical path to the neural build's contract.
// This also delays bypass, reverb and fallback identically. The extra delay
// is not inserted in non-neural builds.
class LatencyAlignedStems {
public:
#if defined(SKELL_WITH_RNNOISE) && SKELL_WITH_RNNOISE
    static constexpr int extraDelay = 2880 - Separator::latency;
#else
    static constexpr int extraDelay = 0;
#endif
    static constexpr int capacity = 4096;
    static constexpr int skellLatency() noexcept {
#if defined(SKELL_WITH_RNNOISE) && SKELL_WITH_RNNOISE
        return 2880;
#else
        return Separator::latency;
#endif
    }
    void reset() noexcept {
        cursor = 0;
        for (auto& frame : stemHistory) for (auto& channel : frame) channel.fill(0.0f);
        for (auto& channel : bypassHistory) channel.fill(0.0f);
    }
    void process(Separator::Stems& stems, std::array<float, 2>& bypass) noexcept {
        if constexpr (extraDelay > 0) {
            const auto write = static_cast<std::size_t>(cursor & (capacity - 1));
            const auto read = static_cast<std::size_t>((cursor - extraDelay) & (capacity - 1));
            for (int s = 0; s < 3; ++s)
                for (int ch = 0; ch < 2; ++ch) {
                    auto& ring = stemHistory[static_cast<std::size_t>(s)][static_cast<std::size_t>(ch)];
                    ring[write] = stems[static_cast<std::size_t>(s)][static_cast<std::size_t>(ch)];
                    stems[static_cast<std::size_t>(s)][static_cast<std::size_t>(ch)] = ring[read];
                }
            for (int ch = 0; ch < 2; ++ch) {
                auto& ring = bypassHistory[static_cast<std::size_t>(ch)];
                ring[write] = bypass[static_cast<std::size_t>(ch)];
                bypass[static_cast<std::size_t>(ch)] = ring[read];
            }
        }
        ++cursor;
    }
private:
    std::array<std::array<std::array<float, capacity>, 2>, 3> stemHistory {};
    std::array<std::array<float, capacity>, 2> bypassHistory {};
    std::int64_t cursor = 0;
};
