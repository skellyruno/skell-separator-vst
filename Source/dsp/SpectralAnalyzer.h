#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>

// Written on audio thread at hop rate; polled by editor. 48 logarithmic-ish bands.
class SpectralAnalyzer final
{
public:
    static constexpr int bands = 48;
    static constexpr int stems = 3;
    struct Frame
    {
        std::array<std::array<float, bands>, stems> magnitudes {};
        std::array<float, stems> levels {};
    };
    void publish(const Frame& frame) noexcept;
    void reset() noexcept;
    float getBand(int stem, int band) const noexcept
    {
        return bins[static_cast<size_t>(stem)][static_cast<size_t>(band)].load(std::memory_order_relaxed);
    }
    float getLevel(int stem) const noexcept
    {
        return levels[static_cast<size_t>(stem)].load(std::memory_order_relaxed);
    }
private:
    std::array<std::array<std::atomic<float>, bands>, stems> bins {};
    std::array<std::atomic<float>, stems> levels {};
};
