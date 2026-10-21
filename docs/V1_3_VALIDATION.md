# v1.3 validation and remaining work

## What was executed

- `bash tests/run_dsp_tests.sh`: pass (GCC on Linux).
- `CXX=clang++ bash tests/run_dsp_tests.sh`: pass.
- `SKELL_SANITIZE=1 SKELL_TEST_BINARY=/tmp/skell_v13_asan bash tests/run_dsp_tests.sh`: pass (GCC, ASan + UBSan).
- `CXX=clang++ SKELL_SANITIZE=1 SKELL_TEST_BINARY=/tmp/skell_v13_clangasan bash tests/run_dsp_tests.sh`: pass (Clang, ASan + UBSan).
- Synthetic polarity test at 48 kHz and Strong: Voice energy / dry energy, centred 0.163, L=-R 0.086, hard-left 0.097. **This is a regression probe, not a vocal separation benchmark**.
- Three-stem complement reconstruction in the test DSP: max absolute error around 1.2e-7 on synthetic tests at 44.1, 48 and 96 kHz.
- 192 kHz estimator decay/silence test: pass.

## Cannot yet claim

- Windows/MSVC VST3 and Standalone compile.
- Native JUCE FFT backend numeric parity with the test shim.
- FL Studio open/close or save/restore runtime behavior.
- Acoustic or perceptual separation improvement on real vocals.
- Real-time deadline / CPU headroom under realistic sessions.
- Any ONNX / UVR / neural inference capability.

An attempt to configure CMake using the official JUCE FetchContent dependency failed **before compilation** because this sandbox could not resolve `github.com`. This is not evidence of a source compile failure.

## Next gates

1. Run the existing `.github/workflows/build.yml` Windows build to compile against the official JUCE 8.0.12 headers and deliver both formats.
2. Run `.github/workflows/dsp-regression.yml` and inspect first failing stack trace if any.
3. Use `docs/HOST_TEST_PLAN.md` in a fresh FL Studio session, focusing on native bypass, polarity-inverted stereo, session restore, host delay compensation and live buffer switching.
4. Benchmark level-matched, phrase-aligned spoken/sung stems against a separate licensed model backend before describing any improvement as better vocal isolation.
