#!/usr/bin/env python3
"""Isolated dry/wet/candidate dereverb scorer (NOT an integrated dereverb model).

Use the SAME performance/recording with room response added for the reverberant
reference. Strict alignment: no gain-fit or delay search is performed. Report
SI-SDR, dry-silent tail energy, and candidate relative to the wet reference.
Self-test demonstrates that a known impulse response can be causally inverted;
this is an oracle upper bound, not a solution for unknown real-world rooms.
"""
import argparse
import json
from pathlib import Path
import numpy as np


def ensure_samples(name, samples):
    a = np.asarray(samples, dtype=np.float64)
    if a.ndim not in (1, 2) or not np.isfinite(a).all() or not len(a):
        raise ValueError(f"{name} must be a finite mono/stereo audio array")
    if a.ndim == 1:
        a = a[:, None]
    if a.shape[1] not in (1, 2):
        raise ValueError(f"{name} must have one or two channels")
    return a


def energy_db(x, floor=1e-14):
    return float(10 * np.log10(max(float(np.mean(x*x)), floor)))


def si_sdr(estimate, reference):
    estimate = ensure_samples("estimate", estimate)
    reference = ensure_samples("reference", reference)
    if estimate.shape != reference.shape:
        raise ValueError("audio sample count/channel mismatch: no implicit alignment")
    # Center before scale-invariant projection; no automatic delay search.
    a = estimate - estimate.mean(axis=0, keepdims=True)
    r = reference - reference.mean(axis=0, keepdims=True)
    e = float(np.sum(r * r))
    if e < 1e-15:
        raise ValueError("reference is silent")
    factor = float(np.sum(a*r)) / e
    predicted = factor*r
    residual = a-predicted
    return float(10*np.log10((float(np.sum(predicted*predicted))+1e-15) /
                             (float(np.sum(residual*residual))+1e-15)))


def score(dry, wet, candidate, sr):
    dry = ensure_samples("dry", dry)
    wet = ensure_samples("wet", wet)
    candidate = ensure_samples("candidate", candidate)
    if wet.shape != dry.shape or candidate.shape != dry.shape:
        raise ValueError("dry, room recording, and candidate must be exactly aligned")
    if not (8000 <= sr <= 192000):
        raise ValueError("invalid sample rate")
    # Hold dry-active state for 10 ms, then measure room-only decay between
    # phrases. Independent of candidate output; silent dry clips are rejected.
    energy = np.max(np.abs(dry), axis=1)
    if float(np.max(energy)) < 1e-7:
        raise ValueError("dry reference too quiet")
    threshold = max(1e-5, 0.012 * float(np.max(energy)))
    active = energy >= threshold
    hold = max(1, int(round(.010*sr)))
    active_held = np.convolve(active.astype(float), np.ones(hold), mode="full")[:len(active)] > 0
    silent = ~active_held
    if int(silent.sum()) < sr // 5:
        raise ValueError("need at least 200 ms of dry-silent sections to score tails")
    wet_tail = energy_db(wet[silent]); cand_tail = energy_db(candidate[silent])
    before = si_sdr(wet, dry); after = si_sdr(candidate, dry)
    return {"sample_rate": int(sr), "samples": len(dry),
            "si_sdr_wet_db": before, "si_sdr_candidate_db": after,
            "si_sdr_improvement_db": after-before,
            "dry_silent_samples": int(silent.sum()),
            "wet_tail_dbfs": wet_tail, "candidate_tail_dbfs": cand_tail,
            "tail_reduction_db": wet_tail-cand_tail}


def self_test():
    sr = 48000
    n = sr*3
    rng = np.random.default_rng(7)
    dry = np.zeros((n, 2), dtype=np.float64)
    for start in (.10, .65, 1.2, 1.75, 2.3):
        i0 = int(start*sr); length = int(.12*sr)
        t = np.arange(length)/sr
        burst = (.15*np.sin(2*np.pi*193*t) + .035*rng.normal(size=length))
        window = np.sin(np.pi*np.arange(length)/length)**2
        dry[i0:i0+length, 0] = burst*window
        dry[i0:i0+length, 1] = .7*burst*window
    d1 = int(.028*sr); d2 = int(.057*sr)
    wet = dry.copy()
    wet[d1:] += .48*dry[:-d1]
    wet[d2:] += .24*dry[:-d2]
    # Oracle: a known TWO-reflection room transfer can be inverted causally,
    # but we do not have these coefficients for arbitrary FL Studio recordings.
    oracle = np.zeros_like(dry)
    for i in range(n):
        oracle[i] = wet[i]
        if i >= d1: oracle[i] -= .48*oracle[i-d1]
        if i >= d2: oracle[i] -= .24*oracle[i-d2]
    result = score(dry, wet, oracle, sr)
    assert result["si_sdr_improvement_db"] > 40
    assert result["tail_reduction_db"] > 20
    assert np.max(np.abs(oracle-dry)) < 2e-6
    assert len(dry) == result["samples"]
    print(json.dumps({"fixture": "known-two-echo-oracle", **result}, indent=2))
    try:
        score(dry, wet[:-1], oracle, sr)
        raise AssertionError("unaligned input not rejected")
    except ValueError:
        pass
    try:
        score(dry, wet, oracle*np.nan, sr)
        raise AssertionError("NaNs not rejected")
    except ValueError:
        pass
    print("Dereverb reference benchmark: PASS (research scorer, no shipped model)")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--self-test", action="store_true")
    p.add_argument("--dry", type=Path)
    p.add_argument("--room", type=Path)
    p.add_argument("--candidate", type=Path)
    args = p.parse_args()
    if args.self_test:
        self_test()
        return
    if not all([args.dry, args.room, args.candidate]):
        p.error("provide --dry, --room and --candidate, or --self-test")
    import soundfile as sf
    arrays = [sf.read(path, always_2d=True, dtype="float64") for path in
              (args.dry, args.room, args.candidate)]
    sr = arrays[0][1]
    if any(rate != sr for _, rate in arrays):
        p.error("sample rates differ; resample/align explicitly before scoring")
    print(json.dumps(score(*(data for data, _ in arrays), sr), indent=2))


if __name__ == "__main__":
    main()
