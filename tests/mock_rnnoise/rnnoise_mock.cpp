#include "rnnoise.h"
#include <algorithm>
#include <array>
#include <new>
struct DenoiseState { std::array<float, 480> previous {}; };
extern "C" {
int rnnoise_get_frame_size(void) { return 480; }
DenoiseState* rnnoise_create(RNNModel*) { return new(std::nothrow) DenoiseState; }
int rnnoise_init(DenoiseState* p, RNNModel*) { if (!p) return -1; p->previous.fill(0.0f); return 0; }
void rnnoise_destroy(DenoiseState* p) { delete p; }
float rnnoise_process_frame(DenoiseState* p, float* out, const float* in) {
    std::copy(p->previous.begin(), p->previous.end(), out);
    std::copy(in, in + 480, p->previous.begin());
    return 0.5f;
}
}
