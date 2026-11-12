#pragma once
// Zero-added-latency live microphone monitoring. This does NOT isolate vocals.
// Keep valid floating-point samples bit-identical (including legal >0 dBFS
// values) and suppress only non-finite host input. No lookahead or allocation.
#include <array>
#include <cmath>
namespace skell {
struct TrackingMonitor final {
    static constexpr int reportedLatency(bool tracking, int mixLatency) noexcept {
        return tracking ? 0 : mixLatency;
    }
    static std::array<float, 2> sample(float left, float right) noexcept {
        return {{ std::isfinite(left) ? left : 0.0f,
                  std::isfinite(right) ? right : 0.0f }};
    }
};
} // namespace skell
