# Recorded-vocal quality gate and Windows ONNX DLL regression (research only)

**Scope:** no changes to the LIVE/SEPARATE UI, existing parameter IDs, zero-added-latency tracking path, or stable `main` branch. No private audio, model weights, or audio reconstruction files are included.

## Real recorded-vocal test (not independently verified sung performance)

The first source was a separately supplied, 15-second, 44.1-kHz, stereo **dry performed vocal** used for Skelly sound-matching. Its content has **not been classified as singing**, so these measurements must not be presented as a singing-vocal benchmark. Noise files were separately supplied recorded AC/office/traffic sounds. All recordings remained in a private evaluation workspace.

`tests/recorded_vocal_quality_gate.py` uses the actual `LiveVocalCleanup` and `MeasuredRoomDereverbResearch` C++ source via a small offline float32 runner. Each recorded noise source is resampled, mixed with the vocal at approximately +6 dB vocal/noise RMS ratio, and evaluated with identical time alignment and one second of noise-only lead-in. No other per-output gain or delay fitting is allowed. The report includes input/output vocal SI-SDR and noise-only attenuation.

| Source noise | Mixture SI-SDR | Best nonzero LIVE Clean/Gate SI-SDR change | Interpretation |
|---|---:|---:|---|
| Recorded AC | 6.01 dB | -0.001 dB | No improvement |
| Recorded traffic | 6.00 dB | approximately 0.000 dB | No meaningful improvement |
| Recorded office | 6.00 dB | approximately 0.000 dB | No meaningful improvement |

**Decision:** keep current eight-band Clean filter in research, with its earlier positive synthetic hiss tests, but **do not claim same-band vocal noise isolation or promote more aggressive cleanup** without a demonstrated improvement on recorded audio. The dry 0% control remains a bit-exact reference for valid samples.

## Known-room dereverb tested on recorded voice

To test the inverse implementation, the same real dry voice was passed through a *synthetic, precisely known* two-tap room: delay 13 ms at gain +0.26 and delay 37 ms at +0.18. With the matching parameters, the inverse recovered the dry recording to about **3e-9 RMS error**; SI-SDR rose from roughly 10.18 dB to 146.6 dB. Deliberately shifting one inverse tap by 37 samples degraded SI-SDR to **6.59 dB**. This shows the current inverse requires an accurately measured room: **no blind or real-room dereverberation has been validated**.

## Reproduce without publishing audio

```bash
python3 tests/recorded_vocal_quality_gate.py \
  --dry /private/dry_vocal.flac --noise /private/ac_noise.wav \
  --report /private/out/real_recorded_quality.json
```

This optional research utility needs `ffmpeg`, `numpy`, and C++17 `g++`. It compiles a temporary offline benchmark executable and runs the real Skell DSP. It never commits, uploads, or embeds the source audio. The external input's actual singing status and appropriate usage rights must be verified independently.

## Next qualification gates

1. A licensed **verified singing** multitrack such as [ccMixter's Mr_Yesterday dry FLAC vocal stems](https://ccmixter.org/files/Mr_Yesterday/70315) (CC BY 4.0). Validate sample-aligned isolation, active-singing noise suppression, consonants, and independent known/wet-only reverb references, without confusing vocals-with-FX for isolated reverb.
2. The Windows ONNX initialization crash was a DLL version collision: SDK 1.26.0 headers linked against a 1.5.2 runtime DLL selected from PATH. Copying/validating 1.26.0 beside the test EXE and checking the runtime version fixed initialization. A 1,200-hop real-model Windows benchmark at 44.1 kHz yielded mean **3.3613 ms**, p95 **3.4569 ms**, p99 **3.6414 ms**, max **4.3398 ms**; **1,200/1,200 hops exceeded the 2.9025-ms deadline**. The model runs, but the proposed 8.71-ms mode **fails this CPU timing gate**. A follow-up compares 1/2/3-thread research-only session options; no released-mode thread policy changed.
3. Before enabling the fast 384-sample research scheduler: obtain real-model 44.1-kHz inference mean/p50/p95/p99/max, worker + host callback deadline misses at FL Studio 64/128/256/512 and multiple simultaneous instances on a target Windows PC. Never assume single-hop timing is a full DAW qualification.
4. No shipping model weights until commercial and underlying-dataset rights are resolved.

## Independent licensed sung-vocal benchmark

An optional research CI job downloads *Mr_Yesterday — Just Lucky, I Guess* (CC BY 4.0: https://ccmixter.org/files/Mr_Yesterday/70315) into an ephemeral runner. Its publicly indexed ZIP archives contain raw recorded vocal microphone FLACs and a separate instrumental saxophone stem. The test measures four **predeclared 15-second windows** (0, 45, 90 and 135 seconds) of controlled 6 dB vocal/instrument mixtures. This is **not the original mastered mix**. The dereverb scoring remains a synthetic known-two-reflection comparison, not a real wet-only signal. The workflow publishes JSON scores only; it never redistributes the audio files. A failed download or incompatible audio must fail visibly, not be replaced by synthetic success.
