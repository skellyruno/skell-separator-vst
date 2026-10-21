#pragma once
// TEST-ONLY synthetic ABI double. Not a trained RNNoise model.
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DenoiseState DenoiseState;
typedef struct RNNModel RNNModel;
int rnnoise_get_frame_size(void);
DenoiseState* rnnoise_create(RNNModel*);
int rnnoise_init(DenoiseState*, RNNModel*);
void rnnoise_destroy(DenoiseState*);
float rnnoise_process_frame(DenoiseState*, float*, const float*);
#ifdef __cplusplus
}
#endif
