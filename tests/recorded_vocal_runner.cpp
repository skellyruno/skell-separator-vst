// Offline scientific benchmark of Skell's actual causal DSP, not a fake DSP model.
// Raw stereo f32le stream on stdin/stdout. No JUCE dependencies.
#include "../Source/dsp/LiveVocalCleanup.h"
#include "../Source/dsp/MeasuredRoomDereverbResearch.h"
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
    if (argc != 6) {
        std::fprintf(stderr, "usage: runner <cleanup|dereverb> rate clean_or_d1 gate_or_g1 tails_or_d2\n");
        return 2;
    }
    const bool cleanup = std::strcmp(argv[1], "cleanup") == 0;
    const bool dereverb = std::strcmp(argv[1], "dereverb") == 0;
    if (!cleanup && !dereverb) return 2;
    const double rate = std::atof(argv[2]);
    skell::LiveVocalCleanup clean;
    skell::MeasuredRoomDereverbResearch room;
    if (cleanup) {
        clean.prepare(rate);
        clean.setControls(std::atof(argv[3]), std::atof(argv[4]), std::atof(argv[5]));
    } else if (!room.prepare({{{std::atoi(argv[3]), static_cast<float>(std::atof(argv[4]))},
                                {std::atoi(argv[5]), 0.18f}}})) {
        return 3;
    }
    std::array<float, 2> frame {};
    std::size_t n = 0;
    while (std::fread(frame.data(), sizeof(float), 2, stdin) == 2) {
        const auto out = cleanup ? clean.process(frame[0], frame[1])
                                 : room.process(frame[0], frame[1]);
        if (!std::isfinite(out[0]) || !std::isfinite(out[1])) return 4;
        if (std::fwrite(out.data(), sizeof(float), 2, stdout) != 2) return 5;
        ++n;
    }
    std::fprintf(stderr, "processed_frames=%zu\n", n);
    return 0;
}
