// Offline render for reproducible speech/noise QA; uses the production C++ DSP.
// Raw interleaved float32 input/output avoids vendoring audio codecs into the VST.
#include "dsp/Separator.h"
#include "dsp/SpectralAnalyzer.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    if (argc != 5) {
        std::cerr << "usage: render_classical input.f32 voice.f32 ambience.f32 reverb.f32\n";
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary | std::ios::ate);
    if (!in) return 3;
    const auto bytes = in.tellg();
    if (bytes < 0 || bytes % (2 * static_cast<int>(sizeof(float))) != 0) return 4;
    const size_t samples = static_cast<size_t>(bytes) / sizeof(float);
    std::vector<float> stereo(samples);
    in.seekg(0);
    in.read(reinterpret_cast<char*>(stereo.data()), static_cast<std::streamsize>(bytes));
    if (!in) return 5;
    std::ofstream voice(argv[2], std::ios::binary), ambient(argv[3], std::ios::binary), reverb(argv[4], std::ios::binary);
    if (!voice || !ambient || !reverb) return 6;
    SpectralAnalyzer analyzer;
    Separator separator(analyzer);
    separator.prepare(48000.0);
    for (size_t i=0; i<samples; i+=2) {
        Separator::Stems stems {};
        std::array<float, 2> delayed {};
        separator.processSample(stereo[i], stereo[i+1], 0.0f, 1, stems, delayed);
        voice.write(reinterpret_cast<const char*>(stems[1].data()), sizeof(float)*2);
        ambient.write(reinterpret_cast<const char*>(stems[0].data()), sizeof(float)*2);
        reverb.write(reinterpret_cast<const char*>(stems[2].data()), sizeof(float)*2);
    }
    return 0;
}
