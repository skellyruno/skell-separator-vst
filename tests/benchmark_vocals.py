#!/usr/bin/env python3
"""Objective stem benchmark for user-owned or properly licensed vocal recordings.

Each case directory:
  refs/voice.wav, refs/ambience.wav, refs/reverb.wav
  renders/<variant>/voice.wav, renders/<variant>/ambience.wav, renders/<variant>/reverb.wav
All exports must share the same sample rate/channels, begin at input t=0,
use stem gain=0 dB, and have every Solo/Mute off. Plugin output MUST include
its reported delay; this script compensates alignment, not a guessed offset.

Unlike artificial reference-free separation scores, SI-SDR requires isolated
reference stems. Run --self-test to verify the metrics without audio files.
"""
import argparse
import json
from pathlib import Path
import sys

import numpy as np
try:
    import soundfile as sf
except ImportError:
    print("Install dependencies: pip install numpy soundfile", file=sys.stderr)
    raise

STEMS = ("ambience", "voice", "reverb")


def read_wav(path):
    data, sample_rate = sf.read(str(path), always_2d=True, dtype="float64")
    if not np.all(np.isfinite(data)):
        raise ValueError(f"nonfinite samples: {path}")
    return data, sample_rate


def align(reference, estimated, latency):
    if latency < 0 or latency >= min(len(reference), len(estimated)):
        raise ValueError("latency outside recording")
    usable = min(len(reference), len(estimated) - latency)
    if usable < 32:
        raise ValueError("too few aligned samples")
    # Remove a common leading 0.25 s only if caller explicitly wants to;
    # by default retain the full time-aligned recording, including starts.
    return reference[:usable], estimated[latency:latency + usable]


def si_sdr(target, output):
    # Channel-dependent SI-SDR, then mean. Silence is marked null, not inflated.
    results = []
    for ch in range(target.shape[1]):
        t = target[:, ch] - np.mean(target[:, ch])
        o = output[:, ch] - np.mean(output[:, ch])
        power = np.dot(t, t)
        if power < 1.0e-12:
            continue
        projected = t * (np.dot(o, t) / power)
        residual = o - projected
        results.append(float(10 * np.log10((np.dot(projected, projected) + 1e-12)
                                           / (np.dot(residual, residual) + 1e-12))))
    return float(np.mean(results)) if results else None


def sdr(target, output):
    energy = float(np.sum(target ** 2))
    if energy < 1.0e-12:
        return None
    error = float(np.sum((target - output) ** 2))
    return float(10 * np.log10((energy + 1e-12) / (error + 1e-12)))


def benchmark(case, variant, latency):
    root = Path(case)
    refs = {}
    estimates = {}
    sample_rate = None
    shape = None
    for stem in STEMS:
        ref, sr = read_wav(root / "refs" / f"{stem}.wav")
        estimate, sr2 = read_wav(root / "renders" / variant / f"{stem}.wav")
        if sample_rate is None:
            sample_rate = sr
            shape = ref.shape
        if sr != sample_rate or sr2 != sample_rate:
            raise ValueError("sample rates differ; resample all stems before benchmarking")
        if ref.shape != shape or estimate.shape[1] != shape[1]:
            raise ValueError("reference channels/lengths differ")
        if estimate.shape[0] < ref.shape[0]:
            raise ValueError("renders must include their initial plugin delay, not be trimmed")
        refs[stem], estimates[stem] = align(ref, estimate, latency)
    min_length = min(len(a) for a in refs.values())
    refs = {k: v[:min_length] for k, v in refs.items()}
    estimates = {k: v[:min_length] for k, v in estimates.items()}
    combined_input = sum(refs.values())
    combined_output = sum(estimates.values())
    diff = combined_input - combined_output
    # A separation score alone cannot tell whether processing improved over
    # leaving the entire mixture untouched. Publish an explicit unprocessed
    # baseline, computed on precisely the same aligned reference samples.
    voice_baseline_si = si_sdr(refs["voice"], combined_input)
    voice_baseline_sdr = sdr(refs["voice"], combined_input)
    voice_si = si_sdr(refs["voice"], estimates["voice"])
    voice_sdr = sdr(refs["voice"], estimates["voice"])
    result = {
        "case": root.name, "variant": variant, "sample_rate": sample_rate,
        "channels": shape[1], "aligned_seconds": round(min_length / sample_rate, 4),
        "latency_samples": latency, "latency_ms": round(1000 * latency / sample_rate, 3),
        "stems": {stem: {"si_sdr_db": si_sdr(refs[stem], estimates[stem]),
                           "sdr_db": sdr(refs[stem], estimates[stem])} for stem in STEMS},
        "unity_reconstruction_max_abs": float(np.max(np.abs(diff))),
        "unity_reconstruction_rms": float(np.sqrt(np.mean(diff * diff))),
        "voice_interference_rms": float(np.sqrt(np.mean(
            (estimates["voice"] - refs["voice"]) ** 2))),
        "voice_vs_unprocessed_mixture": {
            "mixture_si_sdr_db": voice_baseline_si,
            "mixture_sdr_db": voice_baseline_sdr,
            "estimated_si_sdr_improvement_db":
                voice_si - voice_baseline_si if voice_si is not None and voice_baseline_si is not None else None,
            "estimated_sdr_improvement_db":
                voice_sdr - voice_baseline_sdr if voice_sdr is not None and voice_baseline_sdr is not None else None,
        },
    }
    return result


def self_test():
    rng = np.random.default_rng(100)
    signal = rng.normal(0, 0.13, (4096, 2))
    assert sdr(signal, signal) > 120
    assert si_sdr(signal, signal) > 120
    assert sdr(signal, -signal) < 0
    ref, est = align(signal, np.concatenate((np.zeros((1024, 2)), signal)), 1024)
    assert np.array_equal(ref, est)
    assert abs(sdr(signal, 0.5 * signal) - 6.0205999) < 1e-4
    # Make a small WAV fixture using real file I/O to verify baseline and
    # multi-variant latency handling. These are synthetic fixtures, not a
    # substitute for real multi-singer validation.
    import tempfile
    with tempfile.TemporaryDirectory() as folder:
        base = Path(folder)
        for directory in (base / "refs", base / "renders" / "exact", base / "renders" / "poor"):
            directory.mkdir(parents=True)
        stems = {
            "voice": rng.normal(0, .08, (8192, 2)),
            "ambience": rng.normal(0, .03, (8192, 2)),
            "reverb": rng.normal(0, .01, (8192, 2)),
        }
        for name, wav in stems.items():
            sf.write(str(base / "refs" / (name + ".wav")), wav, 48000, subtype="FLOAT")
            sf.write(str(base / "renders" / "exact" / (name + ".wav")),
                     np.pad(wav, ((2880, 0), (0, 0))), 48000, subtype="FLOAT")
        # Wrong attribution to a voice-only stem must fail the improvement check.
        mix = sum(stems.values())
        for name in STEMS:
            wave = mix if name == "voice" else np.zeros_like(mix)
            sf.write(str(base / "renders" / "poor" / (name + ".wav")),
                     np.pad(wave, ((1024, 0), (0, 0))), 48000, subtype="FLOAT")
        exact = benchmark(base, "exact", 2880)
        poor = benchmark(base, "poor", 1024)
        assert exact["stems"]["voice"]["sdr_db"] > 100.0
        assert exact["voice_vs_unprocessed_mixture"]["estimated_sdr_improvement_db"] > 10.0
        assert abs(poor["voice_vs_unprocessed_mixture"]["estimated_sdr_improvement_db"]) < 0.001
        assert exact["unity_reconstruction_max_abs"] < 1e-6
        assert poor["unity_reconstruction_max_abs"] < 1e-6
    print("Benchmark metric, mixture baseline and WAV alignment self-tests passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", nargs="?", type=Path)
    parser.add_argument("--variant", default="classical")
    parser.add_argument("--latency-samples", type=int, default=1024)
    parser.add_argument("--baseline-variant", help="second render to compare with the requested variant")
    parser.add_argument("--baseline-latency-samples", type=int, default=1024)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.case is None:
        parser.error("provide a case directory or --self-test")
    result = benchmark(args.case, args.variant, args.latency_samples)
    if args.baseline_variant:
        baseline = benchmark(args.case, args.baseline_variant, args.baseline_latency_samples)
        improvement = {}
        for stem in STEMS:
            improvement[stem] = {}
            for metric in ("sdr_db", "si_sdr_db"):
                after = result["stems"][stem][metric]
                before = baseline["stems"][stem][metric]
                improvement[stem][metric + "_change"] = (
                    after - before if after is not None and before is not None else None)
        result["comparison"] = {
            "baseline_variant": args.baseline_variant,
            "baseline_latency_samples": args.baseline_latency_samples,
            "stem_metric_changes_db": improvement,
        }
    rendered = json.dumps(result, indent=2, allow_nan=False)
    if args.output:
        args.output.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)

if __name__ == "__main__":
    main()
