# Ultimate Vocal Remover architecture review (supplied source archive)

Review reference: `ultimatevocalremovergui-master.zip` uploaded by the user. The original archive is **not** redistributed as part of Skell Separator.

## What the source actually provides

- `demucs/apply.py`: an offline model application pipeline including **overlapping chunks**, optional shifts, and aggregation of model outputs. Its own introductory comment explicitly describes chunking with overlap and the shift trick.
- `demucs/hdemucs.py`, `demucs/htdemucs.py`: hybrid time/frequency and transformer architectures designed for learned separation, not lightweight DSP approximations.
- `lib_v5/mdxnet.py`, `lib_v5/spec_utils.py`: MDX-style STFT and spectrogram preparation driven by model-specific `n_fft` and `hop_length` settings.
- `lib_v5/vr_network/`: spectrogram-based vocal removal networks and frequency-band model parameter configurations.
- `separate.py`: offline Python orchestration for model inference and audio files. Not a realtime VST3 callback implementation.

The supplied code does **not** establish that the models directly produce the desired three **Ambience / Dry Voice / Voice Reverb** outputs. Typical singing-vocal/instrumental models do not equate to dereverberation. The included `UVR-DeNoise-Lite.pth` file is not an ONNX model for the Skell streaming inference contract.

## Applied in Skell Separator 1.2

The classical STFT path remains active; none of the neural code or weights above is copied or run. Improvements that are safe and inspired by the general separation workflow:

1. Shared stereo masks on complex FFT spectra to reduce L/R image instability.
2. Three complementary stem signals which reconstruct the **1024-sample delayed** input at unity gain. The ambience stem is the sample-domain residual, so mixture consistency does not depend on any model's imperfect normalization.
3. Sample-rate-aware attack/release and tail time constants; improved minimum-statistics robustness in quiet frames.
4. A conservative **voice-conditioned** spectral tail estimate that requires both earlier voice evidence and a declining spectral envelope, reducing reverb false positives on stationary signals.
5. Silence reset, real-time-safe allocation behavior, and deterministic saved-state startup.

These changes are engineering improvements, **not evidence of Demucs/MDX-level separation**. There are no objective SDR/SIR/PESQ numbers or user listening scores yet.

## Why the uploaded offline models aren't embedded

- Their preferred input chunks, receptive fields and GPU/CPU runtime costs have not been demonstrated to meet a 256-sample streaming deadline.
- A GPU/Python/PyTorch graph called from the audio callback would violate real-time safety. A worker-thread queue requires explicit, fixed look-ahead, underrun policy, stereo consistency, lifetime and shutdown rules.
- The provided ZIP has no top-level `LICENSE` file; the Demucs source refers to a separate `LICENSE` in its root that isn't included in the archive. **Redistribution rights for the combined code and model weights cannot be established from the upload alone.** Verify license/version/weight provenance before reusing or shipping anything.
- Choosing a neural model is an architectural and licensing decision, not a matter of copying Python files into a C++ plugin.

## Practical path to a real neural separator

A **new, licensed and streaming-capable** model should explicitly predict three masks, or predict dry voice and a reverb component separately, using a precisely specified sample rate, normalization, spectrogram convention and fixed-lookahead time. Profile its per-hop inference **on target Windows machines** before integration. Keep ONNX Runtime and model initialization off the audio thread, use fixed-size SPSC queues with timeout/underrun policy, and account for all extra delay with `setLatencySamples` and a matching bypass FIFO. State and bundle formats should remain stable; ship no pretrained weights until their terms are known. Detailed future API guidance is in `MODEL_INTEGRATION.md`.

## Test against UVR responsibly

Compare offline UVR and live Skell at matching sample rate and output levels on a **held-out licensed** evaluation set. Measure dry voice leakage from ambience, voice-reverb confusions, objective reconstruction, transient preservation, perceived artifacts, 99th-percentile CPU per block and real device/host latency. UVR should be treated as a *quality reference*, not a drop-in realtime engine.
