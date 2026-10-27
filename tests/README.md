# DSP regression suite

`bash tests/run_dsp_tests.sh` compiles the **real** Separator/StemMixer/SpectralAnalyzer sources against an intentionally minimal local JUCE API shim. The shim FFT uses the exact `juce::dsp::FFT::perform` complex-pointer signature and its own independent radix-2 implementation, so it verifies numeric properties but **cannot prove compatibility with JUCE's platform FFT backends**.

`SKELL_SANITIZE=1 SKELL_TEST_BINARY=/tmp/skell-dsp-sanitized bash tests/run_dsp_tests.sh` adds AddressSanitizer and UndefinedBehaviorSanitizer checks on Linux.

The tests cover nonfinite mask inputs, three-stem reconstruction, unity bypass, Solo/Mute, transport start, Mono/Stereo transitions, non-power-of-two host block segmentation in model simulations, stereo inverted-polarity/hard-pan source handling, 44.1/48/96 kHz full DSP and 44.1/48/96/192 kHz synthetic decay tracking and silence resets. They do not validate trained AI quality, Windows VST3 packaging, perceptual sound quality or FL Studio integration.

## v1.4 streaming neural and CPU/quality benchmarks

```bash
bash tests/run_neural_contract.sh            # 2880-sample latency and worker timing
bash tests/run_neural_api_mock.sh            # mock RNNoise C API, NOT a real AI model
SKELL_SANITIZE=1 bash tests/run_neural_contract.sh
bash tests/run_cpu_profile.sh 48000 256 12   # callback percentile JSON; classical ONLY
pip install numpy soundfile
python tests/benchmark_vocals.py --self-test
```

The mock RNNoise tests **do not** assess vocal denoising quality or verify
RNNoise's real binary/weights. `benchmark_vocals.py` requires correctly
aligned, licensed isolated reference stems; see `docs/V1_4_STREAMING_NEURAL.md`.
Model-worker CPU timings and missed deadlines are shown in the plugin's
About / Model Status dialog when the optional real RNNoise SDK is linked.

## v1.6 model fallback and paired singing-voice benchmarks

```bash
bash tests/run_neural_blend.sh
SKELL_SANITIZE=1 bash tests/run_neural_blend.sh
python tests/benchmark_vocals.py --self-test
python tests/benchmark_vocals.py /path/to/case --variant rnnoise --latency-samples 2880 --baseline-variant classical --baseline-latency-samples 1024
```

The crossfade test checks that a missing frame is **not** interpreted as a silent model waveform. Benchmark WAV fixtures are synthetic metric validation only; they do not imply neural separation accuracy.

## v1.7 — Full render contract and spoken-speech reference benchmark

`python3 tests/offline_render_smoke.py` compiles the production Separator source with the independent JUCE FFT shim, renders stereo signal frames to real binary files, and checks exactly 1024 samples of delay and reconstruction. It runs in Linux CI. No external audio or Python packages needed.

For real **spoken-speech plus noise**, install `numpy scipy`, compile the offline binary via `bash tests/run_offline_render.sh` (with any arguments to trigger compilation), then use `python3 tests/benchmark_speech_noise.py --fixture-zip /path/to/your/reference.zip --render-binary /tmp/skell-render-classical`. The fixture ZIP must be supplied separately and is not embedded in the project. This is not a singing-vocal, reverb, trained-neural, or FL Studio benchmark. Details in `docs/V1_7_ADAPTIVE_SPEECH_BENCHMARK.md`.

## v1.8 — Invalid/incomplete model output

`bash tests/run_neural_contract.sh` now includes worker-thread whole-frame validation tests for model NaNs, out-of-range values and partially written output, plus recovery at exact original timestamps. `bash tests/run_dsp_tests.sh` injects NaN, Inf, and peak-overrange classical samples directly into Separator and checks that its long-lived FFT history and three-stem reconstruction remain finite. The optional RNNoise C API mock is still not a real neural model.
