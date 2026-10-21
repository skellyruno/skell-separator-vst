# v1.4: Licensed streaming neural integration, latency and evaluation

**Status: integration implemented, production model build and host test NOT yet verified.**
The default GitHub Windows build remains classical-only and adds **no** third-party
model dependencies. A separate build flag enables a *linked* RNNoise library.
The source ZIP does **not** contain a pretrained model, model weights, RNNoise source
or binary, nor does it guarantee actual vocal isolation. Installing the SDK is
required for inference to run.

## 1. Why RNNoise?

Xiph RNNoise (<https://github.com/xiph/rnnoise>) is a small 48 kHz recurrent
**speech-noise suppressor**. Its repository's `COPYING` grants BSD-3-Clause
permission for redistribution provided notice/conditions are preserved.
It expects 480 samples per frame and floating-point samples scaled roughly
like 16-bit PCM (hence the adapter's `*32768` and `/32768`).

It can suppress background noise behind *speech*. It is **not** trained to
split full musical mixes into lead vocal, accompaniment, and vocal reverb.
The third (Voice Reverb) stem remains an approximate classical tail estimate.
This backend is therefore a functional neural-processing baseline, **not**
comparable to UVR/Demucs/MDX separation quality without real tests.

## 2. Build the optional model backend

Install/build RNNoise from the upstream Xiph repository in a dedicated prefix.
Follow upstream instructions (`./autogen.sh`, `./configure`, `make`, etc.).
On Windows, prepare compatible `rnnoise.h`, a `rnnoise` import/static library,
and any DLL it needs; check bitness, MSVC ABI and runtime packaging.

```powershell
cmake -S . -B build-neural -A x64 `
  -DSKELL_ENABLE_RNNOISE=ON `
  -DRNNOISE_ROOT="C:/sdk/rnnoise" 
cmake --build build-neural --config Release --parallel 2
```

`RNNOISE_ROOT/include/rnnoise.h` and a corresponding `lib/rnnoise(.lib)` must
be present. If a shared DLL is linked, include it in the distribution as
required by your license and the target host. `build.yml` remains unchanged;
it builds the classical baseline. The SDK is **not** downloaded implicitly.
Do not redistribute RNNoise code/binaries without retaining upstream notices.
Training dataset licenses and any *replacement* model licenses must be
checked separately. See upstream `README`, `COPYING`, and `datasets.txt`.

## 3. Fixed-delay contract, boundaries and host reporting

| Build | Sample rate | Neural path | Host reported delay |
|---|---|---|---|
| Default (RNNoise OFF) | 44.1/48/96/192 kHz | Disabled | 1,024 samples |
| RNNoise ON | 48,000 Hz | Worker-thread RNNoise | 2,880 samples = 60 ms |
| RNNoise ON | Other supported rates | Classical fallback | 2,880 samples (time depends on sample rate) |

At 48 kHz each model input frame is 480 samples. RNNoise is assumed to emit
samples corresponding to the *previous* input frame, per the upstream example
that skips the initial frame. A sample can only be inferred after receiving
two 480-sample frames. The plugin supplies an additional worker scheduling
budget and publishes each model result at `inputSample + 2880`, never earlier.

The classical STFT path has 1,024 samples of delay; in an RNNoise-enabled build,
`LatencyAlignedStems` adds 1,856 samples. Dry bypass, wet reverb and fallback
are all aligned. The host is notified via `setLatencySamples(2880)` during
`prepareToPlay`; quality and bypass never change this value.

The worker consumes 480-sample frames through bounded SPSC queues. It never
runs inference on the audio thread. If a result is late at the **first**
sample of its output frame, that entire frame is treated as unavailable;
an equal-timeline classical Voice estimate is used instead. A 5 ms blend
reduces sudden changes. The audio thread neither allocates nor waits on the
worker. The resulting complement is always recomputed:

`Ambience = delayed processed input - Voice - Voice Reverb`.

**Caveat:** RNNoise's exact audible impulse alignment with a *real linked
model* is not yet measured in this environment. The contract is verified
against a one-frame-delayed test model and the RNNoise C ABI test double.
Additional windows DLL, VST3 and loopback impulse measurements are required.
A missed inference deadline can cause a crossfade artifact; it must not cause
a host latency discontinuity. CPU load and dropout rates depend on hardware.

## 4. Real vocal ground truth and objective measures

A credible test requires at least 20 seconds each of actual, licensed/owned
isolated **dry vocal**, **ambience/instrumental/noise**, and **isolated wet vocal
reverb** recordings from the same sample rate and starting time. Record with
sources stem-exported before final mix; make `mixture = sum(references)`.
Keep clean test and training sources disjoint, and include panned, anti-phase,
whispered, deep/harmonic, breathy, and reverb-heavy examples. Commercial songs
without stem rights cannot be packaged or published in a test set.

Example case:

```
cases/case01/
  refs/ambience.wav  refs/voice.wav  refs/reverb.wav
  renders/classical/ambience.wav  voice.wav  reverb.wav
  renders/rnnoise/ambience.wav    voice.wav  reverb.wav
```

For each render: 0 dB all gains, no Solo/Mute, stereo, not bypassed, original
capture t=0 start preserved, enough tail to include reported latency.
**Render stems one at a time with only that stem Solo active**, resetting the
processor for each new recording and allowing for model warmup. Always start
from the same source with identical block size/sample rate and initial state.
The script only compares estimates against references; it does not perform
DAW rendering or infer missing stem truth.

```bash
pip install numpy soundfile
python tests/benchmark_vocals.py --self-test
python tests/benchmark_vocals.py cases/case01 --variant classical --latency-samples 1024
python tests/benchmark_vocals.py cases/case01 --variant rnnoise --latency-samples 2880
```

Metrics per stem: SI-SDR (scale invariant), SDR (level sensitive), and
mixture reconstruction maximum/RMS sample error. Voice interference RMS is
reported too. Compare against the input-mixture-as-vocal and classical
baseline, with matched input levels, and listen for musical noise, consonant
loss and late reverb leakage. SI-SDR can conceal gain errors; inspect SDR
and aligned waveform too. A higher score on synthetic sinusoid tests proves
nothing about human singing. A reference set is not in this archive.

## 5. CPU and real-time profiling

```bash
bash tests/run_dsp_tests.sh
bash tests/run_neural_contract.sh
bash tests/run_neural_api_mock.sh
SKELL_SANITIZE=1 bash tests/run_neural_contract.sh
bash tests/run_cpu_profile.sh 48000 256 12
bash tests/run_cpu_profile.sh 48000 64 12
bash tests/run_cpu_profile.sh 96000 256 12
```

The profiling tool reports mean/p95/p99/max callback microseconds versus
block deadline and count of misses. It benchmarks the actual **classical
separator DSP only** with deterministic tones on this machine, not JUCE host
callbacks and not model inference. When RNNoise is connected, use the About
menu's *worker mean/max processing time* plus *queue drop/deadline miss count*.
At 48 kHz RNNoise frames must finish faster than one 10 ms interval over the
long run, with sufficient headroom for bursts. Record system/CPU model, block
size, clock scaling and power mode; repeat 30-second runs at 48 kHz and during
transport start/stop in FL Studio. Do not equate average throughput with
real-time safety.

## 6. Still needed before shipping

- Build upstream RNNoise natively on Windows and verify its binary/notice.
- Measure real linked model transient/impulse alignment and host PDC.
- Measure first-load, save/restore, close/reopen in FL Studio.
- Confirm model runtime peak CPU, queue misses and waveform continuity under
  64/128/256 samples, during CPU contention and *faster-than-realtime exports*.
- Run on real **licensed isolated vocal references**; compare objective scores
  and level-matched listening to v1.3, and fail on meaningful regressions.
- To obtain true vocal/music/reverb quality, replace the speech enhancer with
  a separately licensed **causal music-source separation** model that emits
  all three stems, using the same fixed-delay transport with its new frame/
  lookahead contract and host latency verified anew.
