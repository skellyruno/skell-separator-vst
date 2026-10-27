# v1.7 — Causal noise-prior experiment, uploaded-source review and speech benchmark

## Five archive reviews (archive contents, not upstream website claims)

| Archive | Relevant inspected code/data | Rights evidenced in archive | Decision for Skell Separator |
|---|---|---|---|
| `IS-NMF-master.zip` | Offline R STFT and Itakura–Saito nonnegative matrix factorization with repeated multiplicative updates, `optimfun.R`, plus a song and output WAV files | No root license in archive; song/recordings have no verified redistribution grant | **Do not embed** code or audio. Iterative offline factorization adds unbounded / expensive processing compared with the existing 1024-sample live path. Spectral temporal regularity is useful as a conceptual reference. |
| `audiokeep-claude-new-platform-tool-011CV3BKnQbUBShQ15W98ewW.zip` | FastAPI + Celery, Torch/Torchaudio, cached models and background processing | No root license file in archive | Server/offline job management, not host-realtime inference. No code copied. |
| `Noise-Reduction-and-Speech-Enhancement-main.zip` | Adaptive/static Wiener, spectral subtraction, PCA, ICA, clean spoken-speech and noise samples | No root license in archive; dataset sound recordings cite LibriSpeech and DEMAND sources, whose redistribution rights must be checked individually | Useful **algorithmic** guidance for a lightweight causal Wiener-like a-priori SNR estimate and a *local-only* speech/noise benchmark. Original C++ added, no third-party source, datasets, weights, or WAVs distributed. |
| `AcoustiClean-main.zip` | Backend launches external Demucs subprocess to separate offline files | No root license file in archive | Not suitable in the VST3 audio callback; no code copied. |
| `legends-labs-main.zip` | Offline audio-separator models, DeepFilterNet and multi-model management; `README.md` states MIT | README asserts MIT; no root license file located; each dependent model and checkpoint may have a distinct license | No pretrained streaming stem model. Offline workflows only; no code copied. |

No newly uploaded archive provides a licensed, trained, three-stem low-latency model suitable for this VST3 as-is. RNNoise is still optional (separately installed SDK), is a speech denoiser rather than a music-vocal separator, and has its own BSD-3-Clause code license. The plugin **has not gained an AI music-separation backend**.

## DSP change and latency guarantee

`Source/dsp/MaskEstimator.h` now tracks a *bounded, decision-directed estimate* of a priori speech SNR per spectral bin (independent attack/release coefficients expressed in **seconds**). A small soft Wiener prior attenuates weak isolated frequency-bin noise masks, preserving prominent sustained speech magnitudes. This adds one `float[513]` of fixed state, **no FFTs, allocations, locking, look-ahead, or host parameters**. The dry/reverb/ambience masks remain complementary: ambience = delayed input − voice − reverb, independent of model quality. Quality modes, GUI dimensions and default 0 dB knobs are unchanged.

The classical engine latency remains exactly **1024 samples** (~21.33 ms at 48 kHz). The RNNoise-enabled build retains its existing fixed **2880 sample** (60 ms at 48 kHz) alignment including bypass and fallback; the uploaded projects did not change neural inference.

## Measured quality, and why improvement is modest

Test fixtures: input spoken-speech and noise WAV files from the separately supplied `Noise-Reduction-and-Speech-Enhancement-main.zip`. Resampled to 48 kHz, clean speech normalized to -20 dBFS RMS, noisy mixture synthesized at indicated levels, first second excluded from SI-SDR, and the plugin's exact 1024-sample delay compensated. The six *original* conditions used clean_speech.wav with office and white noise at input SNR 0, 5 and 10 dB. Then six *transfer* conditions used speaker1.wav with traffic and environmental noise. Scores measure **spoken speech denoising only**, not music stem separation or reverberation.

| Dataset | v1.6 mean SI-SDR improvement over unprocessed mixture | v1.7 mean SI-SDR improvement | Delta |
|---|---:|---:|---:|
| clean_speech × office/white, six conditions | +7.250 dB | +7.364 dB | **+0.114 dB** |
| speaker1 × traffic/environment, six transfer conditions | +6.632 dB | +6.632 dB | **0.000 dB** |

Results vary per condition. One transfer case fell by 0.054 dB. These tiny differences **do not demonstrate a perceptually significant or general source-separation improvement**. The processing is an experiment kept behind automated reconstruction/numerical regression tests; further tuning requires licensed singing-vocal reference stems, and may justify reverting the prior if it performs worse.

Reproduce on Linux with the **external user-supplied** ZIP, dependencies `numpy scipy`, and an offline rendering binary built from this source:

```bash
# C++ standalone audio render (production Separator source + local JUCE FFT test shim)
SKELL_RENDER_BINARY=/tmp/skell-render bash tests/run_offline_render.sh /path/to/stereo-input.f32 voice.f32 ambience.f32 reverb.f32
# Or build the offline executable without rendering:
bash tests/run_offline_render.sh
python3 tests/benchmark_speech_noise.py --fixture-zip /path/to/Noise-Reduction-and-Speech-Enhancement-main.zip --render-binary /tmp/skell-render --out results.json
python3 tests/benchmark_speech_noise.py --fixture-zip /path/to/Noise-Reduction-and-Speech-Enhancement-main.zip --render-binary /tmp/skell-render --speech data/speech/speaker1.wav --noises traffic,environment --seconds 4 --out transfer.json
```

The WAV assets in the uploaded archive are **not present in the distributable**. Verify source/dataset permissions before sharing derivative audio. The scoring harness compiles the real Separator source against a local portable FFT shim; it does not establish a true JUCE Windows build or listening quality.

## CPU profile and build tests

v1.7 `docs/V1_7_CPU_LINUX_SNAPSHOT.json` holds three 5-second runs. At 48 kHz / 256 samples mean measured callback use was ~**2.31%** of the buffer deadline, with zero missed deadlines in this run. At 96 kHz / 256 it was ~**4.66%** with **one recorded deadline miss**. Differences from v1.6 are small enough that Linux scheduling jitter prevents a strong regression or performance-gain claim. Profile is classical only, not RNNoise, JUCE Windows or FL Studio.

- GCC + Clang C++ DSP reconstruction, stereo polarity and sample-rate regression tests: pass.
- ASan and UBSan runs for DSP and neural mock transport: pass.
- `tests/offline_render_smoke.py`: uses stdlib and actual Separator render, confirms 1024-sample delayed reconstruction on stereo impulse/tones, and is included in GitHub DSP regression workflow.
- Neural mock contract, crossfade fallback and benchmark-metric self-test: pass.
- ZIP source integrity: independently checked.
- **Still unverified:** Actual Windows VST3/Standalone compilation, FL Studio load/close, real RNNoise linked inference, dedicated Windows CPU traces, isolated singing-vocal subjective/objective evaluation.

## Next step for a genuinely strong separator

Acquire or train a legally distributable **causal vocal/instrumental separation model with known weights license** and label dry/wet reverb explicitly. Validate *model* latency including receptive field, buffering and actual inference—not merely 1024-sample classical or 2880-sample transport alignment. Use held-out singers, contrasting voice registers and mixes at 44.1/48/96 kHz. Reject models whose 99.9th-percentile inference stalls host delivery. Fixed host latency must include the entire wet reconstruction path.
