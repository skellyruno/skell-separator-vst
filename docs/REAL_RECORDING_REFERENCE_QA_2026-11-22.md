# Real-recording isolation and effect-reference quality audit — 2026-11-22

Status: **measured, limited-scope candidate; NOT independently validated multitrack separation**.

## Why this change
The original three-stem classical mask can assign spectral energy simultaneously to direct Voice and Voice Reverb. Two existing private owner-provided recorded-vocal takes, with same-take dry and effect-enabled renders, showed measurable false Reverb output even when the source was dry. The current experimental neural model also underperformed Classical on these examples; this change does not retune or promote neural weights.

## Reproducibility and rights
- Source before change: `6c9d961b9219db8f740a7b47bc128623314e3301` (functionally identical DSP to `f3533310`); same production C++ renderers, fixed 48 kHz, same source and render latency.
- Private dataset: **two owner-supplied real recorded vocal takes**. Do not add, attach, upload, republish, or distribute their audio on this **public** GitHub repository.
- Reference for wet: identical-timeline difference between a dry vocal print and a processed vocal/reverb print. This is an **effects-print differential**, not independently recorded wet-only natural reverb; it may include processing other than reverb.
- **No independently verified isolated instrumental is available in this set. Instrumental-only leakage, instrumental SDR and song-level source separation are NOT MEASURED.** Never replace an absent reference with zero or a synthetic accompaniment and describe it as real.
- Private recordings were read locally; candidate comparisons use the same fixed, un-shifted 48 kHz resampling and fixed STFT latency, with per-comparison normalization shared by all sources.

## Primary real-recording comparisons
Lower dry-only false-Reverb RMS (relative to input dry Voice) is better; higher effect-reference SDR is better. The table uses the 8-second tuning window for each owner recording. Scores are **not universal quality claims**.

| Controlled comparison | Previous | New | Delta |
| --- | ---: | ---: | ---: |
| Recording A dry-only false Reverb / dry Voice | -30.91 dB | -34.03 dB | -3.12 dB |
| Recording B dry-only false Reverb / dry Voice | -28.75 dB | -32.15 dB | -3.40 dB |
| Recording A effects-print reference SDR | -1.73 dB | -0.97 dB | +0.76 dB |
| Recording B effects-print reference SDR | -0.34 dB | -0.09 dB | +0.25 dB |

**Strictly nonoverlapping** holdouts (A tuned on 3–11 s and held out 13–21 s; B tuned on 5–13 s and held out 0–5 s) showed: A dry-only false Reverb -30.88→-34.53 dB and effects SDR -1.53→-0.76 dB; B false Reverb -28.89→-33.41 dB and effects SDR -0.33→-0.04 dB. The B holdout is five seconds, not eight. These are only **two** recordings and SDR remains poor, especially for distinguishing actual reverberation from other effect differences.

Direct Voice SI-SDR moved by less than 0.01 dB in these mixed effect-print comparisons. Complementary reconstruction remained within approximately `6e-8` sample max error after reported latency. These results do **not** show that genuine reverb is accurately separated.

## DSP change and safeguards
In `MaskEstimator::process`, reduce the heuristic wet mask proportionally to simultaneous direct voice evidence with `wetRaw *= 1 - 0.85 * dryRaw`. The mask remains bounded, causal, fixed-size, allocation-free, and leaves a small overlap allowance. Preserve **all** plugin class IDs, public automation IDs, preset tables, saved state format, UI, and host latency. A regression threshold catches the prior dry-voice false-Reverb tendency; current DSP tests still require nonzero measured tail response.

Local candidate checks: production C++ offline comparison on the recorded references; `tests/run_dsp_tests.sh` normal and ASan/UBSan; neural blend/offline contracts; `tests/real_vocal_quality.py --self-test`; 48 kHz / 256-sample short CPU profile showed zero missed blocks in both before/after (~166.5µs vs ~168.0µs mean per block; diagnostic, not a performance claim). Exact-head GitHub Windows VST3/pluginval and regression CI are distinct required gates.

## Operator command (outside public repo)
```sh
python tests/real_vocal_quality.py \
  --voice /private/dry_take.wav \
  --printed-reverb /private/same_take_with_reverb.wav \
  --offset-seconds 3 --max-seconds 8 \
  --license USER-PROVIDED-PRIVATE --attribution 'Private owner recording — do not redistribute' \
  --output /private/quality.json
```
Omit `--instrumental` if unavailable: the script must report instrumental metrics as unavailable. For a true three-stem test supply synchronized `--instrumental` plus an independently obtained wet-only `--reverb` source with documented rights.

## Release blockers
A rights-cleared independent music multitrack with isolated dry vocal, instrumental, and wet-only reverb; multiple independent singers/songs; blind A/B; FL Studio saved-state and automation acceptance; measured target-hardware CPU/latency; valid private delivery and licensing. The proposed fix remains **draft** and is not a product release.
