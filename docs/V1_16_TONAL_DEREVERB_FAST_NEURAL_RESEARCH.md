# Skell Separator — v1.16 research gates (not production features)

This revision is **research-only** on `research/stemgenrt-vocal-backend`.
It does not alter the LIVE/SEPARATE UI, saved parameters, established
zero-added-latency LIVE path, or 2,880-sample SEPARATE routing.
The stable `main` branch remains unchanged.

## 1. Low-mid overlap: stationary tonal cancellation

`Source/dsp/TonalNoiseResearch.h` demonstrates a zero-lookahead, two-tone
quadrature least-mean-squares canceller. It must be given the **exact tone
frequencies** and must **learn coefficients only during a vocal-free interval**.
It then freezes the profile while singing. The separate C++ regression verifies
48000/44100/96000 Hz, stereo anti-phase, invalid input, 470 and 950 Hz tones,
and even a singing voice sharing the same 470 Hz carrier. In the idealized
known-frequency, non-drifting fixture, >90 dB cancellation is measured. **This is
an upper-bound numerical test, not measured real-recording quality.**

The test is intentionally disconnected from the plugin until frequency
identification, drift tolerance, safe automatic vocal-absence detection, pitch
protection, and real-room clips have been demonstrated. Never allow the
coefficients to learn during active vocals. It does not remove broadband noise
or arbitrary noise occupying the same vocal bands.

## 2. Active-vocal dereverberation: measured-room inverse

`Source/dsp/MeasuredRoomDereverbResearch.h` implements a causal, bounded sparse
inverse for a **known two-reflection room response**. Its sample-aligned test
runs active synthetic vowels/transients with overlapping reflection energy.
Matched delays/gains recover the dry samples within ~6e-8 peak error in the
fixture. Unmeasured room response, continuous/late diffuse reverb, changes in
mic placement, and learned blind dereverb are NOT handled. Do not market this
as SPL De-Verb Plus and do not put it in LIVE yet. No measured room impulse
response has been collected from a user microphone.

## 3. Fast-neural scheduling vs real-model inference

`tests/fast_neural_paced.cpp` adds a real-time-*paced* 44.1-kHz, 128-sample-hop
simulation around `FastNeuralHopResearch`. A synthetic worker cost of 0, 750 or
1600 microseconds completed without missed output hops on the test runner; a
3400-microsecond worker cost missed most. These thresholds do not prove any
specific Windows host, audio buffer, or real ONNX model can meet the deadline.
The proposed prototype reports 384 samples / ~8.71 ms at 44.1 kHz; it is not
wired to the production processor.

For **real Windows ONNX evidence**, `SkellStemgenHopTiming` is an optional native
benchmark target that calls the actual 128-hop `OrtHopModel` factory, hashes
and checks the externally obtained model, measures warmup + 1200 streaming hops,
and prints mean/p50/p95/p99/max and the number of hops above the 2.90-ms hop
period. The research CI downloads the upstream 31,946,241-byte model only into
an ephemeral Windows runner, checks exact SHA-256, does not upload the model
or package it with any plugin, and deletes it when finished.

The model uses **CC BY-NC-SA 4.0** from the publisher; the dataset permissions
and commercial model use remain unresolved. This test is noncommercial research
and gives no commercial distribution grant. A green native inference test is
not the same as deadline-safe concurrent worker + host callback operation.
Final approval still needs 44.1-kHz FL Studio 64/128/256/512 buffer tests and
multiple instances on the user's Windows machine, plus real isolated singing
measurements.

## Regenerate portable tests

```
bash tests/run_tonal_noise_research.sh
bash tests/run_measured_room_dereverb.sh
bash tests/run_fast_neural_paced.sh
CXX=clang++ SKELL_SANITIZE=1 bash tests/run_tonal_noise_research.sh
CXX=clang++ SKELL_SANITIZE=1 bash tests/run_measured_room_dereverb.sh
CXX=clang++ SKELL_SANITIZE=1 bash tests/run_fast_neural_paced.sh
```

Existing LIVE, SEPARATE, neural transport, latency and UI tests must also stay
green. Results measured with synthetic tests are **not** real-singing scores.
