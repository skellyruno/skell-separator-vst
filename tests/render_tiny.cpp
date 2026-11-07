// Offline 48-kHz stereo float32 renderer for real, user-licensed reference data.
// Runs the SAME TinyVocalNet implementation deterministically without a host
// callback or timing races. Sample-aligned output length input+2880 samples.
#include "dsp/TinyVocalNet.h"
#include "dsp/LatencyAlignedStems.h"
#include "dsp/SpectralAnalyzer.h"
#include "dsp/Separator.h"
#include "dsp/NeuralVoiceBlend.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 6) {
        std::cerr << "usage: render_tiny input.f32 voice.f32 ambience.f32 reverb.f32 strength_0to1\n";
        return 2;
    }
    const float strength = std::strtof(argv[5], nullptr);
    if (!std::isfinite(strength) || strength < 0 || strength > 1) return 2;
    std::ifstream source(argv[1], std::ios::binary | std::ios::ate);
    if (!source) return 3;
    const auto size = source.tellg();
    if (size < 0 || size % (2 * static_cast<int>(sizeof(float))) != 0) return 4;
    const int64_t total = static_cast<int64_t>(size) / (2 * static_cast<int>(sizeof(float)));
    std::vector<float> data(static_cast<size_t>(total * 2));
    source.seekg(0);
    source.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
    if (!source) return 5;
    const int64_t length = total + LatencyAlignedStems::skellLatency();
    std::vector<float> neuralVoice(static_cast<size_t>(length * 2), 0.0f);
    std::vector<uint8_t> neuralAvailable(static_cast<size_t>(length), 0);
    skell::TinyVocalNet model;
    skell::NeuralFrame frame{}, estimated{};
    for (int64_t f = 0; f * skell::neuralFrameSize < total + skell::neuralFrameSize; ++f) {
        for (int i = 0; i < skell::neuralFrameSize; ++i) {
            const auto sample = f * skell::neuralFrameSize + i;
            for (int ch = 0; ch < 2; ++ch) {
                frame.audio[static_cast<size_t>(ch)][static_cast<size_t>(i)] =
                    sample < total ? data[static_cast<size_t>(sample * 2 + ch)] : 0.0f;
            }
        }
        model.process(frame, estimated);
        if (f < 1) continue;
        for (int i = 0; i < skell::neuralFrameSize; ++i) {
            const int64_t out = (f - 1) * skell::neuralFrameSize + i + LatencyAlignedStems::skellLatency();
            if (out >= length) break;
            neuralAvailable[static_cast<size_t>(out)] = 1;
            for (int ch=0;ch<2;++ch)
                neuralVoice[static_cast<size_t>(out*2 + ch)] = estimated.audio[static_cast<size_t>(ch)][static_cast<size_t>(i)];
        }
    }
    std::ofstream voice(argv[2], std::ios::binary), ambience(argv[3], std::ios::binary), reverb(argv[4], std::ios::binary);
    if (!voice || !ambience || !reverb) return 6;
    SpectralAnalyzer analyzer;
    Separator separator(analyzer);
    separator.prepare(48000.0);
    LatencyAlignedStems alignment;
    alignment.reset();
    skell::NeuralVoiceBlend blender;
    blender.reset();
    double worst = 0.0;
    for (int64_t sample = 0; sample < length; ++sample) {
        const float inL = sample < total ? data[static_cast<size_t>(sample*2)] : 0.0f;
        const float inR = sample < total ? data[static_cast<size_t>(sample*2+1)] : 0.0f;
        Separator::Stems stems{};
        std::array<float, 2> dry{};
        separator.processSample(inL, inR, 0.0f, 1, stems, dry);
        alignment.process(stems, dry);
        const bool ready = neuralAvailable[static_cast<size_t>(sample)] != 0;
        std::array<float, 2> candidate{};
        if (ready)
            for (int ch = 0; ch < 2; ++ch) {
                const auto c = static_cast<size_t>(ch);
                candidate[c] = (1.0f-strength)*stems[1][c] + strength*neuralVoice[static_cast<size_t>(sample*2+ch)];
            }
        blender.apply(stems, ready, candidate);
        for (int ch = 0; ch < 2; ++ch) {
            const float reconstructed = stems[0][static_cast<size_t>(ch)] + stems[1][static_cast<size_t>(ch)] + stems[2][static_cast<size_t>(ch)];
            worst = std::max(worst, std::abs(static_cast<double>(reconstructed - dry[static_cast<size_t>(ch)])));
        }
        voice.write(reinterpret_cast<const char*>(stems[1].data()), 2*sizeof(float));
        ambience.write(reinterpret_cast<const char*>(stems[0].data()), 2*sizeof(float));
        reverb.write(reinterpret_cast<const char*>(stems[2].data()), 2*sizeof(float));
    }
    std::cerr << "48k neural offline render: " << total << " input frames, delay "
              << LatencyAlignedStems::skellLatency() << " samples; reconstruction maximum " << worst << '\n';
    return worst < 1e-4 ? 0 : 7;
}
