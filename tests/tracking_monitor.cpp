#include "../Source/dsp/TrackingMonitor.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <cstdio>
int main() {
    static_assert(skell::TrackingMonitor::reportedLatency(true, 2880) == 0);
    static_assert(skell::TrackingMonitor::reportedLatency(false, 2880) == 2880);
    // Impulses and consonant-like bursts survive sample-by-sample: no delayed tap.
    // Variable host buffers should not affect the identity operation.
    for (int block : {1, 64, 128, 256, 512, 2048}) {
        for (int i=0;i<10000;i+=block)
            for (int j=i;j<i+block&&j<10000;++j) {
                const float left = j == 127 ? 1.f : (j%97==0 ? -0.75f : 0.f);
                const float right = j == 127 ? -1.f : (j%131==0 ? 1.3f : 0.f);
                const auto out = skell::TrackingMonitor::sample(left, right);
                assert(out[0] == left && out[1] == right);
            }
    }
    const float inf=std::numeric_limits<float>::infinity();
    const float nan=std::numeric_limits<float>::quiet_NaN();
    const auto guard=skell::TrackingMonitor::sample(inf,nan);
    assert(guard[0] == 0.f && guard[1] == 0.f);
    const auto hot=skell::TrackingMonitor::sample(4.5f,-2.25f);
    assert(hot[0] == 4.5f && hot[1] == -2.25f); // retain floating headroom
    std::puts("tracking monitor: 0 samples PDC, bit-exact finite stereo, no clipping, nonfinite guarded");
}
