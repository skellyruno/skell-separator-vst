# Neural model integration contract (future work, not enabled)

**This repository does not contain ONNX Runtime, trained weights, or a functioning neural separator.** The current effect is a classical spectral approximation. A real voice/source separator needs a trained, legally distributable model, an inference backend, profiling, and end-to-end validation.

## Model placement

For a future integration, place a model at the **suggested default path**:

- Windows: `%APPDATA%/Skell Audio/Skell Separator/separator.onnx`
- macOS: `~/Library/Application Support/Skell Audio/Skell Separator/separator.onnx`
- Linux: `~/.config/Skell Audio/Skell Separator/separator.onnx`

The current release **does not load or execute** a file from this path, even if it exists. Path configuration is a proposed integration convention, not a current feature.

## Suggested model tensor shapes

The following is a proposed contract **for a model you train or license**. It is not the tensor layout of any supplied weights.

- STFT: 1024 samples, hop 256, periodic sqrt-Hann analysis/synthesis.
- Input per inference chunk: `[1, T, 513]` float32 magnitudes (normalized mid spectrum), with `T` streaming time frames. Stereo side information may be provided by a separate model-specific tensor.
- Output: `[1, T, 3, 513]` float32 masks for (ambience, dry voice, voice reverb), constrained non-negative and normalized per time-frequency tile to sum to 1.
- Look-ahead and recurrent states must be explicitly documented and preallocated by the model adapter; masks from the past cannot be requested after an audio deadline.
- For stable stereo: share masks between channels, apply to their *complex* input spectra, and overlap-add each stem with sqrt-Hann. Validate exact-sum at 0 dB.
- At 44.1 kHz, a 1024-sample frame is ~23.2 ms. Future model look-ahead must be reported *in addition* to actual overlap/add latency.

## Real-time architecture

1. Construct ONNX session in a non-audio background thread, or load it prior to activating realtime processing. Verify shapes and sample-rate contract.
2. Allocate fixed-size SPSC input / output FIFOs and all inference tensors at prepare, never `new`, lock, or wait in the audio callback.
3. Push analysis frames to the worker, pop predicted masks from the worker. If a mask misses its deadline, use the fallback predictor or last safe mask and count underruns. The audio thread must never wait for inference.
4. Stage a newly loaded model only after validation; swap on an audio-block boundary with a safe crossfade. Never unload active memory the audio callback can still access.
5. Tests: dry speech and singing, music with lead vocals, stereo width, transient leakage, mixed reverb, true silence, rapid changes, peak CPU, arbitrary buffer sizes, 44.1/48/96 kHz, host latency, toggling bypass, and plugin reopen/close in FL Studio.

**Important:** Mask-based stereo separation can be an approximation even with a model. Ambience vs reverb is an especially ill-posed distinction in music. Only use weights and runtimes with appropriate distribution licenses.
