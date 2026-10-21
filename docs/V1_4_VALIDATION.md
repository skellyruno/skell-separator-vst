# v1.4 verification record (October 21, 2026)

What was **actually executed** here:

| Target | Status | Evidence |
|---|---|---|
| v1.3 classical audio reconstruction at 44.1/48/96 kHz, mono/stereo, quality choices | PASS | `bash tests/run_dsp_tests.sh` |
| worker transport 480-frame one-frame-delayed synthetic model | PASS | `bash tests/run_neural_contract.sh` |
| explicit 2880-sample delay of classical and bypass path | PASS | `neural_contract.cpp` impulse and reconstructed-sum assertions |
| real-time scheduling discontinuity guard / accelerated offline export | PASS (simulated) | worker miss fallback policy tested without waiting on audio |
| RNNoise source wrapper header/API + scaling | PASS (mock C library ONLY) | `bash tests/run_neural_api_mock.sh` |
| AddressSanitizer/UndefinedBehaviorSanitizer | PASS | classical and neural suites |
| Clang and GCC | PASS | classical and neural suites |
| SI-SDR/SDR/latency calculation self-test | PASS | `python tests/benchmark_vocals.py --self-test` |
| classical DSP callback CPU at multiple block sizes | MEASURED | `V1_4_CPU_LINUX_SNAPSHOT.json` |
| actual RNNoise weights linked and inference executed | **NOT DONE** | SDK and pretrained weights unavailable in this runtime |
| full JUCE Windows VST3 compilation, standalone, FL Studio load/close | **NOT DONE** | cannot fetch JUCE or run FL Studio in this runtime |
| real human voice / licensed isolated reference benchmarks | **NOT DONE** | no isolated benchmark reference stems supplied |
| actual RNNoise model latency loopback transient verification | **NOT DONE** | required before asserting model-internal output timing |

CPU snapshot was taken on a shared Linux host with a synthetic stereo signal,
10 seconds per configuration, `-O3 -march=native` and standalone JUCE FFT shim:

| SR | Block | Mean callback / deadline | p99 callback | Max callback | Deadline misses |
|---|---:|---:|---:|---:|---:|
| 48 kHz | 64 | 2.10% | 166 us | 486 us | 0 |
| 48 kHz | 256 | 2.27% | 334 us | 2333 us | 0 |
| 96 kHz | 256 | 4.29% | 261 us | 4720 us | **1** |

The 96 kHz worst-case miss, despite low average utilization, illustrates why
worst-case scheduling matters. Results are influenced by OS interruptions and
shared host contention and **are not** Windows benchmarks or neural-inference
measurements. `docs/V1_4_CPU_LINUX_SNAPSHOT.json` contains the raw JSON.

Do not label this build production ready until the missing host/model/reference
measurements are completed and real source separation quality is evaluated.
