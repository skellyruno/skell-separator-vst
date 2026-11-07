# Skell Separator v1.11 — objective quality and leakage safety

## Engineering decision

Do not silently promote TinyVocalNet as a high-quality neural singing separator. The small model's weights **still come from procedural sounds**, not licensed sung recordings. The original v1.10 blend was tested against two *real spoken* recordings from a user-supplied speech-enhancement archive, mixed with a deterministic synthesized chord, chimes and filtered noise. This was **not** a singing-vocal benchmark and is **not** proof of high-fidelity real music separation.

The v1.11 revision adds a **bounded, smooth neural-correction guard** to TinyVocalNet's stem blend (`NeuralVoiceBlend::apply(..., true)`). The correction is limited relative to the smoothed classical vocal envelope without a hard waveform clip, and the Ambience residual is recomputed to maintain exact three-stem reconstruction. RNNoise-only builds retain their old correction policy. The user-selectable Neural Strength remains functional but conservative; the guard cannot turn an untrained model into a competent source separator.

## Recorded-spoken-voice regression results (48 kHz, 8 s each)

The tests used `clean_speech.wav` and `speaker2.wav` from the user's uploaded **Noise-Reduction-and-Speech-Enhancement** archive. Musical accompaniment was synthesized deterministically (not taken from studio stems). A controlled synthetic reverb send was derived from the recorded voice; these are not real vocal-reverb references. Same audio, gains, latency alignment and model weights before/after. Results are the mean of the two recordings.

| Metric / model at strength | Previous v1.10 | New bounded v1.11 | Meaning |
|---|---:|---:|---|
| Voice SI-SDR on mixed speech/instrument, Neural 65% | −2.68 dB | +0.75 dB | +3.43 dB relative to old neural, but still below Classical |
| Voice SI-SDR on mixed speech/instrument, Neural 100% | −4.80 dB | +0.70 dB | +5.50 dB relative to old neural |
| Instrument-only false Voice energy, Neural 65% | −9.38 dB | −14.71 dB | 5.33 dB less erroneous Voice energy (lower is better) |
| Instrument-only false Voice energy, Neural 100% | −6.93 dB | −14.68 dB | 7.75 dB less erroneous Voice energy |
| Vocal-only HF onset SI-SDR, Neural 65% | 19.69 dB | 19.45 dB | Small 0.24 dB decline: do not claim improved consonants |
| Vocal-only HF onset SI-SDR, Neural 100% | 8.75 dB | 17.69 dB | Better transient protection relative to old 100% |
| Reverb-stem SDR on controlled wet-only send | ~0.4 dB | ~0.4 dB | No improvement; still classical/heuristic |
| Voice SI-SDR on mixed speech/instrument, Classical | +1.45 dB | +1.45 dB | Still outperforms Neural in these two examples |

All scores are diagnostic on these *two speakers*. They are not statistically generalizable and must not be described as MUSDB18, studio-song, licensed singing, or reverb benchmark scores. No third-party audio is redistributed.

## Real singing-vocal benchmark harness

`tests/real_vocal_quality.py` compiles and executes the production **C++ classical** renderer and **C++ TinyVocalNet** renderer on the same 48 kHz stereo input, using audio the operator supplies with explicit provenance:

```sh
python -m pip install numpy scipy soundfile
python tests/real_vocal_quality.py --voice /data/artist1/dry_voice.wav \
    --instrumental /data/artist1/instrumental.wav \
    --simulated-reverb --max-seconds 12 \
    --license CC-BY-4.0 --attribution 'Artist / exact source URL' \
    --output /tmp/skell-artist1-quality.json
```

The benchmark measures:

- **Instrument-only false vocal leakage** in dB: voice RMS / instrument RMS (lower is better). Do not use SI-SDR on a zero reference.
- **Voice SI-SDR and SDR** for real isolated dry vocals, with an unprocessed mixture baseline.
- **High-frequency consonant-onset SI-SDR** derived from the isolated vocal reference (3–10 kHz; reference-only onset selection). Must measure short words/sibilants, not long stationary vowels alone.
- **Vocal reverb isolation SDR**, only when there is an independent wet-only reverb stem or a clearly labeled **simulated** reference send. A full reverberant vocal is **not** a wet-only reference.
- **Mixture-consistency maximum error** for the three recombined stems at precisely the renderer-reported latency.

Controls: no predicted stem is used to define the onset windows. Files are not automatically time-shifted to artificially raise scores. A two-stem dataset without reverb marks the reverb score **unavailable**. Real songs should be grouped by artist and evaluated on a separate hold-out set; do not cherry-pick favorable lines or tracks. A 12-second clip is a smoke test, not a full-album accuracy claim.

Suggested properly licensed *sources to download outside the repository*:

- **ccMixter / Mr_Yesterday — “Just Lucky, I Guess”** (CC BY 4.0, original vocal FX/raw mics and instrumental stems). Verify stems are on the same timeline, and distinguish `vocalsFx` from dry mics and wet-only reverb. https://ccmixter.org/files/Mr_Yesterday/70315
- **OnAir Music Dataset** (CC BY-SA 4.0, stems plus mixes). Confirm a chosen track contains real singing and isolate only mutually compatible timelines. https://github.com/sevagh/OnAir-Music-Dataset
- **Medley-Solos-DB** (CC BY 4.0, single-voice and instrument snippets) can create reference-controlled cross-instrument mixtures, but is not a naturally mixed song dataset. https://zenodo.org/records/2582103

We did **not** download these external datasets in the current environment; no real singing reference-based metrics have been run or invented.

## Model training: require real audio *and* independent speakers

`models/train_real_stems.py` reads a JSON manifest of separately licensed dry voice/instrumental WAV paths (optionally an isolated wet-only send), verifies **no overlapping artists** between train and validation splits, resamples 48 kHz, and generates candidate 64-band mask weights with vocal-absent examples and extra high-band loss emphasis. It writes `candidate.h` and a provenance/quality JSON, **not** the compiled `TinyVocalWeights.h`; it explicitly marks `approved_for_commercial_release: false`. This is a deliberately cautious starting point, not a fully validated trained separator.

The required promotion decision is: full recorded-singing benchmark must show gains over **Classical** and v1.11 Neural on independent songs; no material loss in HF onsets; reduced instrumental-only leakage; no worsening in exact reconstruction or host latency; legal review of training rights and derivative weights; Windows/FL Studio performance. A model that fails any major check stays experimental.

## Remaining blockers

1. Existing TinyVocalNet only has one spectral mask head, trained on synthetic data with limited temporal context. It cannot reliably separate actual singing, music and reverb into three true stems.
2. Real isolated singing and wet-only reverb references have not yet been run; the harness can now quantify both when available.
3. The Voice Reverb stem is still an estimator, not a learned neural dereverb output. Its baseline controlled reverb SDR remains poor.
4. Full Windows FL Studio runtime stability and listening tests need a host session despite automated VST3 compilation.
