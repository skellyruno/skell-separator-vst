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
    print("Benchmark metric and alignment self-test passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", nargs="?", type=Path)
    parser.add_argument("--variant", default="classical")
    parser.add_argument("--latency-samples", type=int, default=1024)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.case is None:
        parser.error("provide a case directory or --self-test")
    result = benchmark(args.case, args.variant, args.latency_samples)
    rendered = json.dumps(result, indent=2, allow_nan=False)
    if args.output:
        args.output.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)

if __name__ == "__main__":
    main()
