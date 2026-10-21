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
