# v1.13 — deterministic offline neural export, no new model claims

## Problem
The live worker is deliberately nonblocking and skips expired frames; when a DAW exports faster than real-time, the classical fallback can dominate even with Neural enabled. That makes offline output depend on export speed and worker scheduling.

## Change
`StreamingNeural::start(model, offlineRender)` selects a dedicated synchronous mode **only when the host has announced non-real-time before `prepareToPlay`**. No worker thread is launched in offline mode. It invokes the same validated `processFrame` logic and `NeuralFrameBackend` once for each complete 480-sample frame. Since the host allows non-real-time work in the processing callback, it can finish the model before the fixed 2,880-sample output deadline. Live playback still submits frames to the bounded SPSC queue; the realtime callback does not run inference.

Exact contract: 48 kHz model frame = 480 samples; neural + classical + bypass delay = 2,880 samples. Model outputs have one-frame lookbehind and continue to require complete finite frame output. NaN/Inf, out-of-range or partial frames invalidate the whole frame; fallback remains time-aligned. APVTS IDs, existing presets, UI default 785 x 448, gains, MIDI and host latency are preserved.

## New verification

- `bash tests/run_neural_offline.sh`: accelerated 80-frame fake model, exact sample-by-sample stereo alignment and zero deadline misses, plus corrupt-frame rejection/recovery.
- Repeat `SKELL_SANITIZE=1 bash tests/run_neural_offline.sh` on GCC and Clang.
- Existing async tests must still prove late worker fallback, stale pruning and no inference while Classical is selected.
- GitHub Windows VST3 compilation + native DAW export compatibility remain necessary.

## Limitations

- JUCE hosts may change `isNonRealtime()` after `prepareToPlay()` without re-preparing; this update does **not** automatically switch threads or model ownership mid-callback. Those hosts retain the safe asynchronous behavior until re-preparation.
- This improves deterministic *use* of the already-trained experimental model during accelerated export, not the separation *accuracy* of the model itself. TinyVocalNet is trained on procedural mixtures and vocal-reverb remains heuristic.
- No FL Studio bounce has been independently verified. Static UI checks are not actual renders.
