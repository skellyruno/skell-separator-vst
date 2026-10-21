# v1.4 — streaming neural integration and measurement framework

- Optional BSD-3-Clause RNNoise speech denoiser worker (48kHz/480-frame), with
  bounded SPSC queues, frame-level deadline policy, and no audio-thread model work.
- Explicit 2880-sample latency contract in neural-enabled builds, plus a
  1856-sample classical stem/bypass alignment delay and complementary stems.
- Added worker frame timing, deadline miss, queue drop counters and About status.
- Added actual/source-independent regression tests, mock RNNoise C ABI adapter
  compile check, objective real reference vocal benchmark and CPU profiler.
- Default Windows build unchanged/classical; no RNNoise weights/libraries bundled.
- Limitation: RNNoise is a speech suppressor, NOT a singing/ambience/reverb
  separator; actual Windows+linked RNNoise+FL Studio build unverified.

# Changelog

## 1.3.0 — JUCE FFT compatibility, stereo polarity and host bypass

- **Compile blocker removed:** the 1024-point forward/inverse buffers now use
  `juce::dsp::Complex<float>` and the documented `FFT::perform(Complex*,
  Complex*, bool)` interface. Earlier DSP test shims incorrectly accepted
  `float*`, concealing a likely Windows JUCE compile error. The shim now uses
  the *real* method signature so the same mistake fails in Linux CI.
- Voice analysis uses stereo Parseval energy (sqrt(mid²+side²)) rather than
  mid-only magnitudes. This preserves measurable voice detection for L=-R
  polarity-inverted and hard-panned audio. Complex masks still apply to the
  original L/R spectra, preserving output stereo position and complementary
  reconstruction. **This is not proof of perceptual vocal-separation quality.**
- Noise-history window rotation and exact-silence recovery now use durations
  expressed in seconds (instead of being hard-coded to 48 kHz frame counts).
- Display bands are logarithmically distributed in actual Hz, recalculated in
  prepareToPlay and averaged by bin count, not determined only by FFT bin index.
- Added `processBlockBypassed` to maintain host-native bypass on the same
  1024-sample timeline and 10 ms mixer transition as the plugin Bypass control.
- Added stereo polarity and hard-pan regression tests; verified the DSP with
  both GCC and Clang plus AddressSanitizer/UndefinedBehaviorSanitizer.
- Replaced embedded font binaries with common system font fallbacks so the
  project builds without redistributed font files or BinaryData font assets.
- All existing APVTS parameter IDs and factory preset indexes are unchanged.
  Windows CI workflow retained without modification.

**Verification limit:** The JUCE checkout cannot be downloaded in this runtime,
so compiling the VST3/Standalone against the actual JUCE 8 library and testing
in FL Studio must be done on the Windows runner. The standalone DSP regression
shim proves core numerical properties but cannot replace real host testing.

---

## 1.2.0 — UVR-informed real-time reliability update

- Reviewed uploaded UVR source architecture; added `docs/UVR_ARCHITECTURE_REVIEW.md`. No third-party models, weight files or ambiguously licensed code added.
- Sample-rate-dependent attack/release and voice tail time constants (44.1–192 kHz tested).
- Minimum-statistics estimator uses a guarded order statistic and ignores essentially silent windows; resets historical masks after sustained actual silence.
- Separates late-reverb evidence from generic spectral fluctuations: reverb now needs voice history **and** spectral decay, reducing stationary-tone misclassification in regression tests.
- Removes host clock queries from `processBlock` for editor activity detection; uses an atomic block counter instead.
- `StemMixer::snapToTargets` initializes gains, gates and bypass to recalled project values on `prepareToPlay`, avoiding unintended startup ramps.
- Resets stale meter activity when host re-prepares audio. Normalizes preset indices before array access and clamps recalled preset state.
- Adds synthetic decay/silence tests at 44.1, 48, 96 and 192 kHz, meter reset and saved bypass tests. Enables an ASan/UBSan CI regression step.
- Retains all existing APVTS parameter IDs, 1024-sample latency, VST3/Standalone formats and Windows `build.yml`.

**No trained model included.** The fallback remains an approximation, and Windows JUCE compilation / FL Studio testing still require CI and host validation.

---


## 1.1.0 — DSP stability, smoother masks, accurate UI feedback

- Replaced the naive per-bin noise-floor rise/fall logic with fixed-memory
  minimum-window tracking, temporal/frequency mask smoothing and a conservative
  late-energy estimate.
- Added rate-aware frequency weighting in the mask estimator.
- Precomputed the log-spectrum UI band mapping; avoided logarithms per hop.
- Prevented STFT startup synthesis before a complete input window exists.
- Smoothed Mono/Stereo parameter changes (12 ms) while retaining the original
  stereo signal for latency-aligned bypass (10 ms fade).
- Capped malformed or pathological input to a finite +/-8.0 amplitude before
  it enters persistent FFT history.
- Updated gain/Solo/Mute meters and button illumination to match audible state.
- Renamed the three quality choices to Gentle/Balanced/Strong to avoid the
  misleading claim that one mode has lower host latency. Parameter ID/index
  and all other parameter identifiers are unchanged.
- Added a standalone C++ DSP regression harness and a separate Actions job.
- Updated project version to 1.1.0. Preserved the existing Windows build job.

Known limits: The separator is a classical heuristic. No licensed neural model,
ONNX backend or learned voice/reverb stem separation is bundled. The fixed host
latency is 1024 samples. Windows JUCE/VST3 building and FL Studio integration
remain unverified until CI and DAW testing are run.
