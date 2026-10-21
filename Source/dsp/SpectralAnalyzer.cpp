#include "SpectralAnalyzer.h"

void SpectralAnalyzer::publish(const Frame& frame) noexcept
{
    for (size_t stem = 0; stem < stems; ++stem)
    {
        levels[stem].store(frame.levels[stem], std::memory_order_relaxed);
        for (size_t band = 0; band < bands; ++band)
            bins[stem][band].store(frame.magnitudes[stem][band], std::memory_order_relaxed);
    }
}

void SpectralAnalyzer::reset() noexcept
{
    for (size_t stem = 0; stem < stems; ++stem)
    {
        levels[stem].store(0.0f, std::memory_order_relaxed);
        for (size_t band = 0; band < bands; ++band)
            bins[stem][band].store(0.0f, std::memory_order_relaxed);
    }
}
