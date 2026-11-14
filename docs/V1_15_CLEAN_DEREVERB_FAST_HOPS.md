# Research update: LIVE Clean, dereverb scoring, 128-hop neural scheduler

**Status: experimental on `research/stemgenrt-vocal-backend`, not a release.**
Do not claim commercial quality or that fast neural/dereverb is active in the
plugin. All existing UI layouts, three LIVE/SEPARATE knob positions, parameter
IDs, supported rates and reported host latencies remain unchanged.

## LIVE Clean: narrower causal frequency bands

The Clean path uses **eight** complementary IIR bands with crossovers at
150 / 360 / 750 / 1500 / 2600 / 4300 / 7200 Hz, rather than four bands.
This retains **zero lookahead**, stereo-linked gain, independent Gate and Tails,
phrase onset protection, no FFT, no model weights and no audio-thread allocation.
The filtered bands recombine as a complement, so with Clean=Gate=0 the incoming
mic audio is unchanged. Clean still has adaptive per-band noise estimates; no
new knobs or automation IDs are introduced.

Measured on this Linux CI-style runner with deterministic synthetic fixtures:

| Reference fixture | Previous four-band | New eight-band |
| --- | ---: | ---: |
| 220 Hz strong vowel + 8300 Hz steady hiss; hiss attenuation | ~3.3 dB | ~17.0 dB |
| 220 Hz strong vowel + 3900 Hz steady tone; attenuation | not separately verified | ~4.8 dB |
| 220 Hz strong vowel + 470 or 950 Hz steady tone; attenuation | not established | ~0 dB |
| 220 Hz strong vowel, level change alongside 8300 Hz hiss | ~0.02 dB | ~0.05 dB |

The two low-mid examples show why **frequency-aware is not source separation**:
when the desired voice and noise land in the same band, an untrained causal
expander cannot distinguish them. Silence noise reduction and the Gate's release
and timing assertions still pass. This is NOT real singing, NS1/Bertom parity,
or a listening evaluation. The real vocal benchmark remains necessary.

CPU microprobe (Linux container, 48k/256, Clean=Gate=100%): ~39.7 microseconds
per 256-sample chunk versus ~18.9 microseconds previously, or 0.74% versus
0.35% of that buffer deadline. No inference is performed in this mode. This
single-machine result does **not** prove Windows/FL Studio CPU performance.

## Neural 128-hop scheduler: independent prototype only

`Source/dsp/FastNeuralHopResearch.h` is a portable **research transport** for
StemgenRT's 44.1 kHz stereo, 128-sample streaming model. It does not replace
`StreamingNeural`, does not load noncommercial weights, and is not called by the
plugin's audio callback. Its model API is the existing `StemgenHopModel`.
It includes bounded SPSC queues, a nonblocking live worker, sample-aligned
fallback on missed deadlines, full-hop availability freezing, and a deterministic
offline path. It intentionally chooses **3 hops = 384 samples = 8.71 ms at
44.1 kHz** to permit one full output-hop inference reserve after the model's
previous-hop response is ready. This is a *proposed processing latency*, not
a tested FL Studio monitoring latency or a guaranteed deadline. The actual
separation engine remains 2880 samples (65.31 ms at 44.1 kHz).

`tests/fast_neural_hops.cpp` uses a mock previous-hop estimator and proves
stereo timing, zero offline drops and alignment; an unpaced live stress test
verifies that model deadlines are not met in a rushed host. Tests do **not**
prove actual ONNX inference will finish every 2.9 ms. Upstream's published
Windows underruns are a blocking qualification issue. The next gate is a
separate model-weight/local-license compatible Windows/FL Studio test with
measured worst-case inference, 64/128/256/512 buffers, sample-rate behavior,
and multi-instance CPU. No pretrained weights are bundled or shipped.

## True dereverberation: build the benchmark, then choose a model

`tests/dereverb_reference.py` is an aligned dry / reverberant / candidate scorer
for SI-SDR and room-tail reduction. Its self-test uses a **known** two-echo
impulse response and a causal oracle inverse, which verifies that the metric
sees a real reduction in delayed reflections. It is *not* a dereverb plugin or
model; unknown live room responses, long diffuse tails and musical reverb are
far harder, and spectral gating cannot reliably remove tails beneath singing.

Examples:

```
python tests/dereverb_reference.py --self-test
python tests/dereverb_reference.py --dry dry.wav --room room.wav --candidate dereverb_output.wav
```

Inputs must have exactly matching sample count, sample rate and channel count;
no implicit alignment or gain fitting is applied. For real-vocal benchmarking,
use licensed dry/room reference recordings, include consonants and long
reverberant tails, and listen for modulation and transient loss before proposing
an optional dereverb model. SPL De-Verb Plus is a behavior reference, not code
or weights to copy. Voice Reverb in SEPARATE remains an estimated component.
