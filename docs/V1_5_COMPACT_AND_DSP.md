# Skell Separator v1.5 — Compact UI and tonal-mask improvements

## UI changes

- New plugin instances open at 75% of the original 1047 x 597 design: **785 x 448 px** (rounding to whole pixels), while retaining the original aspect ratio and resizability.
- Default APVTS gains for all three stems are **0.0 dB**; Reset All and the first factory preset use unity as well. The slider double-click target is 0 dB.
- Existing saved sessions retain their stored APVTS gains and UI scales. We deliberately do **not** forcibly rewrite saved -17 dB values or stored 100% size because that would alter existing projects.
- Header previous/next/menu icons are JUCE vector paths/shapes, not font-dependent Unicode text, avoiding corrupted glyphs (as in the user's FL Studio screenshot).
- Slider readouts have no rectangular outlines; numeric entry remains available via slider text editing.
- Improved brushed-navy knob face/rim, softer header divider, stem tooltips. No third-party fonts or bitmap UI assets.

**Note:** With all three stems at 0 dB, the combined wet output reconstructs the input at the configured latency. To audition separation, Solo a stem, or mute the unwanted stems. This is intentional and preserves gain-staging neutrality.

## DSP changes and limitations

- Updated the spectral low-frequency prior: reject DC, while retaining more of the 80–160 Hz region where some low-register voice fundamentals sit. This can also increase bass-instrument bleed and is **not a trained music/vocal detector**.
- Replaced naive 0.25/0.50/0.25 mask blur with **energy-conditioned 3-bin smoothing**. Adjacent quiet bins now inherit less dry-voice mask from a strong voiced harmonic; this reduces theoretical adjacent-bin spill without new look-ahead.
- The 1024-sample classical output latency, 2880-sample neural-enabled build contract, and all previous automation IDs are unchanged.
- RNNoise remains optional, at 48 kHz only, and does not become a full three-source neural separator in this release.

## Executed tests (local Linux shim)

- GCC/Clang standalone DSP suite: pass, including 44.1/48/96 kHz reconstruction and stereo polarity checks.
- Low-register synthetic harmonic test: before `voice[2]=0.19974`, adjacent `voice[3]=0.14537`; after `voice[2]=0.42830`, adjacent `voice[3]=0.09120`. Bin 2 at 48 kHz / FFT 1024 is 93.75 Hz. This tests mask behavior, **not subjective separation or real-vocal SI-SDR**.
- Neural transport and RNNoise C API mock: pass. Real RNNoise weights / Windows linking not tested.
- Clang AddressSanitizer + UBSan on classical and mocked neural transport: pass.
- CPU spot run on shared Linux host, -O3, synthetic 48 kHz stereo, 256 samples, 6 audio seconds: before 2.338% mean block deadline; after 2.306%, no deadline misses in either run. These timings include synthetic audio-sample generation; tiny deltas should not be presented as actual efficiency gains.

## Not verified

Full JUCE 8 Windows VST3 compilation, FL Studio host UI rendering, and real-vocal benchmark results were not possible in this runtime. GitHub Action workflows for Windows build and independent DSP tests remain included. Please run them before installing in a production FL Studio project.
