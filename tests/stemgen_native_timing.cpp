// Real ONNX Runtime native model timing, not the mock 128-hop transport test.
// This CLI requires the exact separately-installed noncommercial StemgenRT graph.
// Report is diagnostic: a Windows CI runner does not qualify a user's DAW CPU.
#include "../Source/dsp/StemgenRTBridge.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <numeric>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2 || argc > 4) {
        std::fprintf(stderr, "Usage: SkellStemgenHopTiming verified-independent.onnx [hops] [threads]\n");
        return 2;
    }
    const int hops = argc == 3 ? std::atoi(argv[2]) : 1200;
    if (hops < 200 || hops > 20000) return 2;
    const int threads = argc == 4 ? std::atoi(argv[3]) : 1;
    if (threads < 1 || threads > 3) return 2;
    std::fprintf(stderr, "[Skell timing] before model factory\n"); std::fflush(stderr);
    auto model = skell::createStemgenRTHopModel(argv[1], threads);
    std::fprintf(stderr, "[Skell timing] after model factory\n"); std::fflush(stderr);
    if (!model || !model->valid()) {
        std::fprintf(stderr, "ERROR: no valid, hash-verified, locally supplied StemgenRT graph\n");
        return 3;
    }
    const auto hopNs = static_cast<double>(skell::stemgenHopSize) * 1.e9 / skell::stemgenSampleRate;
    skell::StemgenStereoHop input {}, out {};
    std::vector<double> us;
    us.reserve(static_cast<size_t>(hops));
    int rejected = 0;
    for (int i=0; i<hops+32; ++i) {
        for (int ch=0; ch<2; ++ch)
            for (int j=0; j<skell::stemgenHopSize; ++j)
                input[static_cast<size_t>(ch)][static_cast<size_t>(j)] =
                    static_cast<float>(.11 * std::sin(.004 * (i*skell::stemgenHopSize+j) + ch*.3));
        const auto start = std::chrono::steady_clock::now();
        if (i == 0) { std::fprintf(stderr, "[Skell timing] first model hop\n"); std::fflush(stderr); }
        const bool good = model->infer(input, out);
        if (i == 0) { std::fprintf(stderr, "[Skell timing] first model hop returned\n"); std::fflush(stderr); }
        const auto stop = std::chrono::steady_clock::now();
        if (i > 0 && !good) ++rejected;
        for (const auto& channel : out)
            for (float sample : channel)
                if (!std::isfinite(sample) || std::abs(sample) > 8.f) {
                    std::fprintf(stderr, "Invalid output at model hop %d\n", i);
                    return 4;
                }
        if (i>=32) us.push_back(std::chrono::duration<double,std::micro>(stop-start).count());
    }
    if (rejected || us.size() != static_cast<size_t>(hops)) {
        std::fprintf(stderr, "Model inference rejected %d hops\n", rejected);
        return 4;
    }
    std::sort(us.begin(),us.end());
    const auto percentile=[&](double p) {return us[static_cast<size_t>((us.size()-1)*p)];};
    const double mean=std::accumulate(us.begin(),us.end(),0.) / us.size();
    const double hopUs=hopNs / 1000.;
    const auto over=std::count_if(us.begin(),us.end(),[&](double t){return t>hopUs;});
    std::printf("NATIVE REAL MODEL | 44.1k stereo | 128 samples/hop | %d measured hops | intra-op threads %d\n",hops,threads);
    std::printf("Mean %.1f us, p50 %.1f us, p95 %.1f us, p99 %.1f us, max %.1f us\n",
                mean, percentile(.50), percentile(.95), percentile(.99), us.back());
    std::printf("Hop deadline %.1f us; inference hops above deadline %lld / %d (%.2f%%)\n",
                hopUs, static_cast<long long>(over), hops, 100.*over/hops);
    std::printf("Inference-only P99 within budget: %s. Full ASYNC Windows DAW deadline: UNVERIFIED.\n",
                percentile(.99) <= hopUs ? "YES" : "NO");
    return 0;
}
