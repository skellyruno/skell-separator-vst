# v1.9 Neural source separation engineering notes

## What the update actually adds

The new **TinyVocalNet** is a genuine trained two-source neural spectral-mask
estimator, not RNNoise and not a classical noise gate. It is a 64-band 64→48→64
MLP (ReLU hidden layer; sigmoid output), with weights generated locally by
`models/train_tiny_voice.py`. All training input/reference labels come from
original, procedurally synthesized voice-like harmonic tones and musical
accompaniment. **No pretrained high-quality music weights, actual singing
recordings, or third-party audio assets are in the ZIP.**

Its C++ inference uses 960-sample sqrt-Hann overlap-add at 48 kHz, FFT length
1024, hop 480, with left/right channels sharing an energy-informed neural
mask. Inference occurs on the same existing worker thread used for RNNoise.
Each call consumes frame N and outputs frame N−1; the audio callback plays
that prediction at a fixed position 2,880 samples after the corresponding
input, with classical fallback for missed deadlines. The worker reports time
stats and dropped/rejected frames. The low-latency model *architecture* and
training data cannot be compared with Spleeter/Demucs/UMX benchmark results.

## How to use

1. Build the default CMake project (Tiny Vocal compiled by default, RNNoise
   off), preferably at 48 kHz for the **experimental model**.
2. In the plugin's **...** menu choose **Processing Engine → Neural
   (experimental)**. The default is **Classical** to avoid unexpectedly
   changing an older saved session. Select **Neural Strength → 65%** to begin.
3. Solo Voice to listen for mask artifacts, then compare Classical and Neural.
   In stereo, side-panned audio is not summed to mono for output.
4. Switch to 100% strength only if the separation sounds better. Because the
   model never saw real singing during training, stronger may simply sound
   worse. Voice Reverb remains the separate classical late-energy estimate.
5. At any sample rate other than 48 kHz, neural inference is unavailable, so
   the actual output uses the time-aligned classical processor even if the
   stored engine choice is Neural. The menu disables selection until available.

## Results (local Linux engineering snapshot; not a real-song benchmark)

| Verification | Result |
|---|---|
| 1600 held-out procedural 960-sample patches: weighted neural mask MSE | 0.045568 |
| Same patches: constant 50% mask MSE | 0.186163 |
| Stereo unity-mask WOLA maximum sample difference | 2.38419e-7 |
| C++ tiny backend: 48 kHz, 10 ms frame, worker-only mean | ~54 µs/frame on this Linux machine |
| Three procedural mixtures: model vs unseparated mix SI-SDR gain | +2.18 / +3.48 / +0.62 dB |
| Existing classical reconstruction / neural handoff tests | pass |
| Windows full JUCE build / FL Studio load | NOT VERIFIED |
| Real singing SDR, SI-SDR and perceptual artifacts | NOT MEASURED |

Frame timing is for the tiny inference backend only, not the entire VST3 or a
pretrained third-party model. **No trained singing-vocal performance claim is
supported** by these measurements.

## How this fits the three stems

A learned model estimates a **Voice** waveform from the input stereo mix.
The engine retains its existing classical **Voice Reverb** tail estimator.
**Ambience** is always the input delayed signal minus Voice minus Voice Reverb,
ensuring additive 0 dB reconstruction despite changing neural masks. Therefore
Voice Reverb is NOT a neural dry-vs-wet separation result, and the source
semantic separation is limited by the model's original training task.

## Licensing and stronger trained models

Our code and model weights here are generated from original procedural signals.
The training script uses PyTorch/NumPy as development-time dependencies only;
its weights are emitted directly as a C++ array for runtime inference.

Candidates for future *real-music* evaluation:

- Deezer Spleeter (MIT code) has published 2-stem singing-vocal weights, but
  its U-Net spectrogram context is much longer than our 60 ms contract and its
  actual selected checkpoint's redistribution terms must be verified.
  https://github.com/deezer/spleeter
- Open-Unmix (MIT code) offers a pretrained music-separation baseline, but
  some models such as `umxl` have explicitly **noncommercial** weights. Its
  bidirectional LSTMs are not a causal 10 ms drop-in.
  https://sigsep.github.io/open-unmix/
- Avoid presenting UVR/Demucs/BS-RoFormer offline models as a 60 ms streaming
  plugin without an actual latency/CPU comparison and verified model rights.

The next true quality milestone is to license or train a causal model on
isolated **real singing and accompaniment**, with reference stems and full
MIxture SDR / SI-SDR / source-leakage benchmark tests. Its scheduling, thread
budget, memory and delay must be validated rather than assumed.

## Reproduction / validation

```
python models/train_tiny_voice.py             # requires torch and numpy; optional
bash tests/run_tiny_vocal.sh                 # dependency-free C++ test
bash tests/run_tiny_benchmark.sh             # original procedural mixtures
python tests/offline_tiny_render_smoke.py   # real C++ model -> WAV reconstruction
bash tests/run_neural_contract.sh            # 2880-sample timeline and late frames
bash tests/run_neural_blend.sh               # dropout/recovery complement
bash tests/run_dsp_tests.sh                  # classical/regression
```

A Windows/FL Studio smoke test needs to confirm VST3 loading, model-choice
automation, preset/state restore, bypass, 48 kHz timing, session reopening,
stereo and mono, underrun recovery, and no allocations on the callback.

## External reference audio benchmarking

Render a 48 kHz stereo WAV with the actual trained C++ model, preserving its
2,880 sample latency in the rendered file:

```
python tests/render_tiny_wav.py your_mix_48k_stereo.wav \
  my_case/renders/tiny --strength 0.65
python tests/benchmark_vocals.py my_case --variant tiny --latency-samples 2880
```

For the second command, the directory must contain **your own licensed**
`my_case/refs/{ambience,voice,reverb}.wav` reference stems, all 48 kHz stereo.
The offline tool bypasses streaming scheduling and hence measures ideal neural
model inference quality, **not** FL Studio missed-deadline performance.
