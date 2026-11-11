#include <array>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
#include "dsp/StreamingNeural.h"
#include "dsp/TinyVocalNet.h"

static std::array<float, 2> inputAt(int sample) {
    const float s = static_cast<float>(sample);
    const float v = 0.28f * std::sin(0.013f * s) + 0.16f * std::sin(0.039f * s);
    const float music = 0.18f * std::sin(0.005f * s) + 0.11f * std::cos(0.052f * s);
    return { v + music, -0.4f*v + 0.8f*music };
}

int main() {
    constexpr int frames = 36;
    constexpr int hop = skell::neuralFrameSize;
    auto referenceModel = skell::createTinyVocalBackend();
    auto streamingModel = skell::createTinyVocalBackend();
    assert(referenceModel && referenceModel->valid() && streamingModel && streamingModel->valid());

    std::vector<skell::NeuralFrame> expected(frames);
    for (int f = 0; f < frames; ++f) {
        skell::NeuralFrame in {}, result {};
        in.startSample = f * hop;
        for (int k = 0; k < hop; ++k) {
            const auto samples = inputAt(f * hop + k);
            in.audio[0][static_cast<size_t>(k)] = samples[0];
            in.audio[1][static_cast<size_t>(k)] = samples[1];
        }
        referenceModel->process(in, result);
        if (f > 0) expected[static_cast<size_t>(f-1)].audio = result.audio;
    }

    skell::StreamingNeural stream;
    assert(stream.start(std::move(streamingModel), true));
    float worstError = 0.0f;
    int verified = 0;
    for (int i = 0; i < frames * hop; ++i) {
        const auto input = inputAt(i);
        stream.submitSample(input[0], input[1], i);
        const int source = i - skell::neuralContractLatency;
        std::array<float, 2> out {};
        const bool present = stream.readSample(source, out);
        if (source < 0) { assert(!present); continue; }
        assert(present);
        const auto& truth = expected[static_cast<size_t>(source / hop)].audio;
        for (int c = 0; c < 2; ++c) {
            const float expectedSample = truth[static_cast<size_t>(c)][static_cast<size_t>(source % hop)];
            worstError = std::max(worstError, std::fabs(out[static_cast<size_t>(c)] - expectedSample));
        }
        ++verified;
    }
    assert(verified == frames*hop - skell::neuralContractLatency);
    assert(worstError < 2e-6f);
    assert(stream.missedDeadlines() == 0 && stream.droppedFrames() == 0);
    std::cout << "real TinyVocalNet offline transport (two independent trained C++ model instances): "
              << "PASS; verified=" << verified << ", worst error=" << worstError << '\n';
}
