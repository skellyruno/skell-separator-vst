# LIVE / SEPARATE — three shared knob locations

Research branch: `research/stemgenrt-vocal-backend`. This is a **working-source experiment**, not commercial approval for any pretrained neural weights.

## User workflow

The original header button location (previously Mono/Stereo) selects **LIVE** or **SEPARATE**. Change modes when FL Studio's transport is stopped, because the host-reported PDC changes. Mono/stereo remains in the settings menu. No panel or knob has been moved or resized.

| Existing panel location | LIVE (zero added samples) | SEPARATE (original latency) |
|---|---|---|
| Left | **CLEAN** — gentle adaptive downward expansion, 0–100% | **AMBIENCE** — original gain, −40…+40 dB |
| Center | **GATE** — stronger adaptive below-threshold suppression, 0–100% | **VOICE** — original gain, −40…+40 dB |
| Right | **TAILS** — longer hold/release at higher values, 0–100% | **VOICE REVERB** — original gain, −40…+40 dB |

In LIVE, Solo/Mute controls disappear, because those actions only make sense on separated stems. Existing gain parameters and automation remain attached to the original SEPARATE knobs; LIVE has independent `trackClean`, `trackGate`, `trackTails` parameters, so automation cannot change meanings. LIVE meters show mic activity, gain reduction, and Tails amount rather than stale stem spectra. In SEPARATE, the original Solo/Mute, meters and levels are restored without parameter remapping.

New **Gate** defaults to 0%. Existing Clean defaults to 0%; a fresh LIVE session with both at zero passes finite input samples exactly. Tails defaults to 35%. Zero-lookahead cleanup is an adaptive expander/gate, **not** speech-in-noise source separation or true during-vocal dereverberation.

## Technical notes

* The LIVE audio callback skips FFT separation and neural inference. Its reported added latency is zero samples. The audio interface and other FL Studio plugins still add monitoring latency.
* SEPARATE retains the previous 2,880-sample neural-build alignment (60 ms at 48 kHz; about 65.31 ms at 44.1 kHz). Classical/Neural mode selection and saved parameter IDs are unchanged.
* The gate and expander share a stereo detector so panning is preserved. Strong onsets reopen promptly. Higher Tails holds longer and releases more gradually. Automation bypass returns smoothly to full-volume mic monitoring to avoid sudden gain jumps.
* The external StemgenRT model weights remain noncommercial and are not included in Windows binaries or research CI artifacts. A fast neural separation mode is **not** enabled by this UI change.

## Validation

Run `bash tests/run_live_cleanup.sh` and `python3 tests/ui_layout_contract.py`; full regression also includes `run_dsp_tests.sh`, `run_neural_contract.sh`, `run_neural_offline.sh`, `run_tiny_vocal.sh`, `run_stemgen_bridge.sh` and `run_stemgen_transport.sh`. GitHub's Windows build and actual JUCE screenshot workflow must pass before distribution. Audio-interface round-trip timing and vocal recording in FL Studio are separate real-hardware tests, not established by these tests.
