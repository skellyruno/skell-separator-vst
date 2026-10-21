// CPU profiling of the fallback DSP *audio path*, not RNNoise model inference.
// Reports per-host-block callback utilization against the buffer deadline.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <vector>
#include "dsp/Separator.h"

using Clock = std::chrono::steady_clock;

int main(int argc, char** argv)
{
    const int sampleRate = argc > 1 ? std::atoi(argv[1]) : 48000;
    const int blockSize = argc > 2 ? std::atoi(argv[2]) : 256;
    const int totalSeconds = argc > 3 ? std::atoi(argv[3]) : 12;
    if (sampleRate < 8000 || sampleRate > 192000 || blockSize < 1 || blockSize > 8192
        || totalSeconds < 1 || totalSeconds > 60) return 2;
    SpectralAnalyzer spectra;
    Separator separator(spectra);
    separator.prepare(sampleRate);
    std::vector<double> microseconds;
    const int sampleCount = totalSeconds * sampleRate;
    microseconds.reserve(static_cast<std::size_t>((sampleCount + blockSize - 1) / blockSize));
    double totalCallbackUs = 0.0;
    int deadlineMissed = 0;
    const double deadlineUs = 1.0e6 * blockSize / sampleRate;
    double checksum = 0.0;
    int counter = 0;
    for (int first = 0; first < sampleCount; first += blockSize) {
        const auto begin = Clock::now();
        const int last = std::min(sampleCount, first + blockSize);
        for (int i = first; i < last; ++i) {
            const float time = static_cast<float>(i) / sampleRate;
            const float l = 0.20f * std::sin(2 * 3.14159265f * 183.0f * time)
                          + 0.13f * std::sin(2 * 3.14159265f * 925.0f * time);
            const float r = 0.22f * std::sin(2 * 3.14159265f * 221.0f * time)
                          - 0.08f * std::sin(2 * 3.14159265f * 430.0f * time);
            Separator::Stems stems {};
            std::array<float, 2> dry {};
            separator.processSample(l, r, 0, 1, stems, dry);
            checksum += stems[1][0] + stems[2][1];
        }
        const double elapsedUs = std::chrono::duration<double, std::micro>(Clock::now() - begin).count();
        microseconds.push_back(elapsedUs);
        totalCallbackUs += elapsedUs;
        if (elapsedUs > deadlineUs) ++deadlineMissed;
        ++counter;
    }
    std::sort(microseconds.begin(), microseconds.end());
    const auto percentile = [&](double p) {
        return microseconds[static_cast<std::size_t>((microseconds.size() - 1) * p)];
    };
    const double meanUs = totalCallbackUs / counter;
    std::cout << std::fixed << std::setprecision(3)
              << "{\"engine\":\"classical_only\",\"sample_rate\":" << sampleRate
              << ",\"block_size\":" << blockSize << ",\"audio_seconds\":" << totalSeconds
              << ",\"callback_mean_us\":" << meanUs
              << ",\"callback_p95_us\":" << percentile(0.95)
              << ",\"callback_p99_us\":" << percentile(0.99)
              << ",\"callback_max_us\":" << microseconds.back()
              << ",\"block_deadline_us\":" << deadlineUs
              << ",\"mean_deadline_pct\":" << 100.0 * meanUs / deadlineUs
              << ",\"deadline_misses\":" << deadlineMissed
              << ",\"blocks\":" << counter << ",\"checksum\":" << checksum
              << "}\n";
}
