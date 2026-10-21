# skell-separator-vst

**Skell Separator** — original dark-themed, three-stem JUCE audio effect, inspired by the supplied reference images (not branded as GOYO).

**v1.4 status:** A **BSD-3-Clause RNNoise neural speech-denoising backend** is available as an
**optional external SDK integration** at 48 kHz; this source ZIP bundles no RNNoise
binary or model. The default GitHub Windows build continues using the classical
spectral fallback. **RNNoise is not a singing/music stem separator**, and Voice
Reverb remains a classical estimate. See
[Streaming neural integration and benchmarking](docs/V1_4_STREAMING_NEURAL.md).


## Features

- JUCE 8.0.12 CMake project, Windows VST3 and Standalone targets (matching the uploaded successful StopDial build structure).
- Three overlapping-mask stems: **Ambience, Voice, Voice Reverb**. Gains -40 dB (-inf at min) to +40 dB, all automatable, per-stem **Solo** / **Mute**.
- **Stereo** shared-mid masking (L/R image preserved) and **Mono** sum-to-mono mode.
- Dynamic spectral energy silhouettes, radial tick knobs, responsive idle/active glows, contextual menu, factory presets, and resizeable 1047:597 window.
- STFT front-end: **1024-point FFT / 256-hop sqrt-Hann**, basic adaptive Wiener-style noise estimation and late-energy reverb estimation.
- **Exact complement architecture:** ambience = delayed processed input - (dry voice + voice reverb). At 0 dB gains without mute/solo, the sum should equal the delayed input up to float rounding, even while masks are changing. Default preset intentionally has Ambience and Reverb at -17 dB.
- Host-reported fixed **1024-sample latency** in the fallback path. "Gentle", "Balanced", "Strong" adjust estimation aggressiveness, **not the algorithmic delay**. A switch to a different model / window size would require latency re-reporting and host re-synchronization.
- Bypass is click-free 10 ms crossfade to the **latency-matched original dry input**, retaining the original stereo channels even when Mono mode is selected; it cannot output instantaneous dry while preserving plugin delay compensation.
- All processing state allocated before playback; no I/O, allocations, blocking or model loading in the audio callback. RNNoise uses a worker thread and preallocated bounded queues; late outputs fall back to aligned classical processing.

## Build (Windows / GitHub Actions)

1. Upload the contents of this ZIP to a GitHub repository named `skell-separator-vst`.
2. Open **Actions → Build Skell Separator → Run workflow**. Pushing to `main` will also build automatically.
3. Download **SkellSeparator-Windows** from the successful workflow's Artifacts. It contains `Skell Separator.vst3` and the standalone `.exe`.
4. Install the VST3 bundle into `C:\Program Files\Common Files\VST3\` and rescan in FL Studio. Test with a new project first.

The exact CMake build locally is:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel 2
```

By default, CMake downloads JUCE 8.0.12. For offline local JUCE:

```powershell
cmake -S . -B build -A x64 -DJUCE_PATH="C:/JUCE"
```

`build.yml` preserves the original template's checkout/configure/build/package flow, with names updated for this plugin.

## Optional neural build (v1.4)

The default `build.yml` is **unchanged** and compiles classical only. To enable
RNNoise, build/install the upstream C library separately and configure
`-DSKELL_ENABLE_RNNOISE=ON -DRNNOISE_ROOT=<prefix>` as documented in
[`docs/V1_4_STREAMING_NEURAL.md`](docs/V1_4_STREAMING_NEURAL.md).
RNNoise-linked builds use **2,880-sample fixed host latency**, including bypass
and fallback (60 ms at 48 kHz); classical-only builds retain 1,024 samples.

## Controls

- Drag the outer dial vertically; Shift-drag for fine motion, double-click resets, mouse wheel changes 0.5 dB, right-click or click the dB readout to type a value.
- `S` solos a stem; any Solo silences all non-soloed stems. `M` mutes its stem. Both are smoothed for click-free transitions.
- Header: Mono/Stereo, Bypass, previous/next of four startup presets, `···` menu (reset, copy values, input mono sum, separation quality, knob drag style, scale, about).
- The orange glow appears when live audio is processed; dark idle look when no input or bypassed.

## Limitations / validation

- **Not yet compiled in this session.** The supplied StopDial archive is the template and had reportedly built successfully. This new source requires a Windows CI run to verify JUCE build, FL Studio plugin loading, and runtime stability.
- Frequency-domain fallback cannot reliably pull singing out of a dense mix, distinguish rooms, or perform state-of-the-art AI dereverberation. The reverb stem is especially approximate.
- No external pretrained weights are redistributed. If you want actual neural source separation, the DSP model backend must be integrated and measured; simply dropping an ONNX file into a folder currently has no effect.
- The spectrum meters show approximate analysis-band energy, not precision calibrated dBFS meters.
- Automatic tests and in-DAW evaluation must be completed before production use.

## Licensing

The interface uses standard system fonts and does not bundle third-party font files. JUCE is downloaded under its own licensing terms; comply with those terms when distributing the plugin.

## Version 1.1 DSP hardening and regression coverage

- Introduced `Source/dsp/MaskEstimator.h`, a bounded, fixed-memory noise-floor and
  late-energy tracker with frequency/time-smoothed masks. This aims to reduce
  rapidly flickering isolation and chirpy artifacts. It remains a heuristic,
  **not** an AI model and not reliable isolation of singing from full mixes.
- Precomputed the 513-bin-to-48-display-band frequency lookup once at plugin
  construction instead of evaluating logarithms on every STFT frame.
- Updated stem meters/rim activity to follow Solo, Mute and gain adjustments.
  Solo/Mute button state remains visible while transport is idle.
- Clamps nonfinite or extreme input before the spectral history and bypass
  delay lines to protect the DSP from poisoned state. Values beyond +/-8.0
  full scale are clipped as a protective boundary, not a mastering limiter.
- Added C++ regression tests that exercise the actual separator/mixer source on
  44.1, 48, and 96 kHz and verify all three stems sum to the **delayed** input
  (when unity mixed) within floating-point tolerance. These tests run without
  a JUCE download: `bash tests/run_dsp_tests.sh` (Linux/WSL/macOS).
- Added `.github/workflows/dsp-regression.yml`. The original Windows VST3
  packaging workflow is preserved.

**Validation level:** DSP regression tests passed locally using the test FFT
shim. A complete JUCE 8 Windows compile, FL Studio opening/closing tests, and
level-matched audio listening tests **have not** run in this environment.

- Mono/Stereo automation now uses a 12 ms crossfade to prevent an abrupt
  stereo-field jump; bypass still crossfades to the original delayed stereo.
- FFT synthesis waits until the initial history frame is full, avoiding
  partial-window preroll artifacts before the first delayed audio sample.
- Quality option labels now describe mask *strength* (Gentle / Balanced /
  Strong), not an unsupported low-latency feature. The parameter ID and its
  existing three numeric choices remain stable for saved projects.


## Version 1.2 — Insights from the uploaded UVR archive

The source was reviewed to evaluate model families, overlap/chunking and realistic live inference constraints. No UVR third-party code or pretrained models were copied into this plugin. See [`docs/UVR_ARCHITECTURE_REVIEW.md`](docs/UVR_ARCHITECTURE_REVIEW.md).

- The classical fallback now uses time constants expressed in seconds, a guarded noise-floor history, a voice-conditioned late-energy estimate requiring spectral decay, and state reset after sustained silence.
- Editor activity uses an atomic audio-block sequence rather than an OS-clock call in the realtime callback.
- Recalled gains, Solo/Mute and Bypass are applied **at startup** before processing the first sample, rather than ramping from factory defaults.
- Added tests for reverb false positives on stationary spectra, synthetic decay tracking, silence recovery, saved bypass and meter reset. Automated sanitizer checks run on Linux CI.
- No plugin parameter IDs were changed. Existing saved-project parameter automation remains compatible.

**Limitations:** Neither trained AI isolation nor reliable dry-vocal/reverb decomposition is implemented. Models and deployment licensing remain separate future work. The updated DSP tests pass, but only a Windows CI build and actual FL Studio evaluation can validate the complete plugin.

## Version 1.3 — Critical compile fix and stereo signal handling

- Correct JUCE DSP complex FFT buffer types: the older test shim incorrectly
  allowed `float*` for `FFT::perform`; JUCE 8 requires `Complex<float>*`.
  The fixed test shim enforces the same real API signature, so this regression
  is harder to reintroduce. A Windows JUCE compile is **still required**.
- A polarity-inverted stereo tone (L = −R), which the old mid-only analysis
  never detected as Voice, now gives a nonzero Voice estimate. For the
  included synthetic tone probe, the estimated Voice energy ratios were:
  centered 0.163, polarity-inverted 0.086, hard-left 0.097. These are
  synthetic test results **not measured separation quality on vocals**.
- Fixed-duration noise tracking/silence recovery for sample rates through
  192 kHz; logarithmic UI display bands correspond to actual frequencies.
- Native host `processBlockBypassed` now uses the plugin's latency-aligned
  processing timeline, as well as the built-in Bypass button.
- Preserved 1024 sample host latency and all existing parameter IDs.

**Current verification:** GCC and Clang standalone core-DSP tests pass,
including sanitizer checks. No Windows VST3/Standalone build or FL Studio
smoke test has been completed in this environment. The host build workflow
is retained. A model-free spectral heuristic remains the only separation
backend; no UVR-quality model is bundled or claimed.

**Font note (v1.3):** The project uses system fonts rather than embedding third-party font files; typography may vary slightly by host OS.

## Version 1.4 — Licensed streaming neural path + evaluation harness

- Added a real RNNoise C API wrapper (optional SDK linkage) running on a
  dedicated worker thread using bounded lock-free frame queues. Model loading
  and memory allocation occur only during preparation, never in `processBlock`.

- Introduced a fixed **2,880-sample** host delay in RNNoise-enabled builds,
  with identical delay for classical fallback, reverb and dry bypass. On a
  missed frame deadline the plugin fades to an aligned fallback voice estimate.
- Added worker inference time (mean/max), queue-drop and deadline-miss
  telemetry to the About menu.
- Added test-only one-frame neural model, mock RNNoise C ABI, and 2,880-sample
  impulse/reconstruction checks, tested with GCC/Clang and sanitizers.
- Added a standalone real-vocal reference benchmark (`tests/benchmark_vocals.py`)
  and callback CPU profiler (`tests/run_cpu_profile.sh`), plus a new GitHub CI
  workflow for these checks. No isolated real-vocal recordings are bundled;
  real-vocal scores remain **unmeasured**.
- **No model source/binaries or model weights are bundled**. The Windows VST3
  and actual RNNoise linking and live host measurements remain unverified here.
