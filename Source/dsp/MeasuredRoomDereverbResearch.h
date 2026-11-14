#pragma once
// Research-only causal inverse for a KNOWN, measured sparse room response.
// It can cancel reflections underneath an active vocal when their delays and
// gains match the room, but does not identify an unknown room response and is
// not a blind music/vocal dereverberation model. No lookahead or allocations.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace skell {
class MeasuredRoomDereverbResearch final {
public:
    static constexpr int maxDelay = 8192;
    static constexpr int tapCount = 2;
    struct Tap { int delay = 0; float gain = 0.f; };
    bool prepare(const std::array<Tap, tapCount>& room) noexcept {
        float sum = 0.f;
        for (const auto& tap : room) {
            if (tap.delay < 1 || tap.delay >= maxDelay || !std::isfinite(tap.gain)) return false;
            sum += std::abs(tap.gain);
        }
        // A sufficient small-gain condition prevents runaway recursion even
        // with imperfect input or profile. Invalid profiles are never applied.
        if (sum >= 0.95f) return false;
        taps = room;
        reset();
        ready = true;
        return true;
    }
    void reset() noexcept {
        for (auto& c : history) c.fill(0.f);
        position = 0;
    }
    std::array<float, 2> process(float left, float right) noexcept {
        const std::array<float, 2> input {{std::isfinite(left) ? left : 0.f,
                                            std::isfinite(right) ? right : 0.f}};
        if (!ready) return input;
        std::array<float, 2> dry {};
        const std::size_t at = static_cast<std::size_t>(position % maxDelay);
        for (int ch = 0; ch < 2; ++ch) {
            const std::size_t c = static_cast<std::size_t>(ch);
            double estimate = input[c];
            for (const auto& tap : taps) {
                const auto delayed = static_cast<std::size_t>((position + maxDelay - tap.delay) % maxDelay);
                estimate -= static_cast<double>(tap.gain) * history[c][delayed];
            }
            dry[c] = std::isfinite(estimate) ? static_cast<float>(std::clamp(estimate, -16., 16.)) : 0.f;
            history[c][at] = dry[c];
        }
        ++position;
        return dry;
    }
private:
    std::array<Tap, tapCount> taps {};
    std::array<std::array<float, maxDelay>, 2> history {};
    std::int64_t position = 0;
    bool ready = false;
};
} // namespace skell
