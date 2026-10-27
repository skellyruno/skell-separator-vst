# v1.6 — Uploaded model-source audit, real-time dropout safety, and benchmarking

## Source and weights licensing

The following points were checked against the **uploaded archives**, not against an online repository version. They are a source-licensing review, not legal advice. Source permission does **not** grant a license to model weights, training recordings, or benchmark datasets.

| Uploaded archive | Source license in archive | Architecture / interface inspected | Live deployment assessment |
|---|---|---|---|
| `voiceolation-main.zip` | MIT, copyright 2022 voiceolation | `model.py` Keras encoder/decoder U-Net, `config.py` `SR=11025`, `hop_length=256`, `patch_size=128`; `predict.py` reads 128-frame chunks and reconstructs offline WAVs | ~2.97 s input patch span at 11.025 kHz. No trained weights in this ZIP. Offline chunked prediction cannot meet the existing 60 ms model deadline as-is |
| `vocal-music-separation-master.zip` | MIT-style permission grant, copyright 2019 Ingmars Daniels Melkis | `model.py` Keras CNN + fully connected bin mask, `config.py` defaults 22050 Hz, hop 256 and 25-frame features; `README.MD` documents a reported short-output truncation problem | ~290 ms feature span at defaults; needs a *causal* redesign, overlap-safe synthesis, measured timing, model conversion and licensed checkpoint. No pretrained checkpoint in archive |
| `vocal-remover-develop.zip` | MIT, copyright 2019 tsurumeso | `lib/nets.py` cascaded spectrogram U-Net/LSTM, `CascadedNet.offset=64`; `inference.py` default 2048 FFT, 1024 hop, 256-frame crops at 44.1 kHz, optionally TTA | High quality *architecture reference*, but a 64-frame edge-context at default hop equals ~1.49 s and a 256-frame chunk spans ~5.94 s. Cannot use unchanged as a fixed 60 ms streaming model. No checkpoint in supplied archive |

No model weights, ONNX graphs, or license evidence for *trained* singing-voice separation models were found in the three archives. **No archive code, weights or reference audio was copied into the distributable plugin**. The new code is original glue built around the existing JUCE project. The real-time optional model remains RNNoise when an external SDK is explicitly supplied; it is a speech denoiser, not a genuine three-stem music or vocal-reverb separator.

## Defect fixed: silence injection after an asynchronous model deadline miss

Before v1.6, when a neural result was missing, `neuralMix` decayed over 5 ms but was multiplied by the zero-initialized `neuralVoice`. Thus the *first* missing sample could abruptly change from the previous neural voice value to `(1-neuralMix)*classicalVoice`, which is nearly zero immediately after a stable neural run. The complementary Ambience stem would receive the displaced energy. This could produce an audible dropout/click even when the model itself performed correctly.

`Source/dsp/NeuralVoiceBlend.h` now handles model availability changes. On a miss it holds the **last time-aligned correction** (not last model waveform or a silence frame) and linearly fades the correction to zero over 240 samples at 48 kHz, recovering the classical Voice estimate. On recovery, it fades in the *current samples* from the model; it does not low-pass-filter the actual steady-state neural waveform. The Ambience stem always remains the exact residual of the delayed mixture. Non-finite and out-of-range model results are rejected.

This transition is a short error concealment mechanism, not hallucinated speech restoration. Continuous complete model frames are still necessary for correct inference. It does not improve RNNoise's source-separation accuracy.

## Benchmarks

`tests/benchmark_vocals.py` now reports the original-mixture Voice SDR and SI-SDR **baseline**, the processed Voice improvement against that baseline, and an optional paired-run comparison (with independent fixed-latency alignment):

```bash
python tests/benchmark_vocals.py /path/to/case \
  --variant rnnoise --latency-samples 2880 \
  --baseline-variant classical --baseline-latency-samples 1024 \
  --output results.json
```

The data layout is unchanged: `refs/{ambience,voice,reverb}.wav`, `renders/<variant>/{ambience,voice,reverb}.wav`. All test WAVs need the same sample rate, channel count and starts; rendered files must include the plugin's reported startup delay. Full three-stem **isolated reference audio** is required. The metric self-test creates temporary *synthetic fixtures* to check alignment, gains and comparison calculations; it does not benchmark real vocals.

Recommended evaluation: obtain explicit rights to multi-singer clean-vocal, instrumental and separate late-reverb reference recordings; keep female/male/low-register/breathy/raspy/choral cases separate, and hold out artists from model training. Measure CPU mean, p99, max, deadline misses and quality at 44.1/48/96 kHz and 64/128/256 sample host buffers, on Windows FL Studio plus a reference host.

## Reproducibility and limitations

```bash
bash tests/run_neural_blend.sh
SKELL_SANITIZE=1 bash tests/run_neural_blend.sh
bash tests/run_dsp_tests.sh
bash tests/run_neural_contract.sh
bash tests/run_neural_api_mock.sh
python tests/benchmark_vocals.py --self-test
bash tests/run_cpu_profile.sh 48000 256 4
```

Local development environment is Linux with an independent JUCE API/FFT test shim. A successful C++ unit test does **not** establish Windows VST3 build success, DAW load/close safety, production RNNoise performance, or perceptual vocal quality. Keep the previously defined 1024-sample classical-only and 2880-sample RNNoise-enabled host latency contracts; compile flags and existing automation IDs are unchanged.

### One reproducible local CPU snapshot (not an optimization claim)

The v1.6 classical CPU shim profile JSON is saved at `docs/V1_6_CPU_LINUX_SNAPSHOT.json`. Each run processed 4 seconds of synthetic audio. This run recorded 0/3000 deadline misses at 48 kHz / 64 samples, 0/750 at 48 kHz / 256, and **1/1500 at 96 kHz / 256**. Peak scheduling jitter and callback tails matter despite low average deadline use (~2.4%–4.8%). These numbers are machine-specific, include neither RNNoise CPU cost nor a live Windows VST3 host, and are not comparative model benchmarks.
