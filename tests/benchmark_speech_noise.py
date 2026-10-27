#!/usr/bin/env python3
"""Deterministic *speech/noise*, not vocal/music, regression benchmark.

Supply your own legally usable fixture ZIP. It is never embedded in the release.
This tool never tries to estimate 'true singing voice' performance from speech.
"""
import argparse
import io
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile
import wave
import zipfile

import numpy as np
from scipy.signal import resample_poly


def load_wav(z, suffix):
    names = [x for x in z.namelist() if x.endswith(suffix)]
    if len(names) != 1:
        raise ValueError(f"wanted one ZIP entry ending with {suffix}, found {len(names)}")
    with wave.open(io.BytesIO(z.read(names[0])), 'rb') as wav:
        if wav.getsampwidth() != 2:
            raise ValueError('expected 16-bit PCM reference')
        fs = wav.getframerate()
        data = np.frombuffer(wav.readframes(wav.getnframes()), dtype='<i2').astype('float64') / 32768.
        data = data.reshape(-1, wav.getnchannels()).mean(axis=1)
    import math as _math
    g = _math.gcd(fs, 48000)
    return resample_poly(data, 48000//g, fs//g).astype('float64')


def si_sdr(ref, estimate):
    ref = np.asarray(ref, dtype=np.float64)
    estimate = np.asarray(estimate, dtype=np.float64)
    ref = ref - ref.mean()
    estimate = estimate - estimate.mean()
    proj = np.dot(ref, estimate) / (np.dot(ref, ref) + 1e-20) * ref
    e = estimate - proj
    return float(10 * np.log10((np.dot(proj, proj) + 1e-20) / (np.dot(e, e) + 1e-20)))


def run(binary, fixture, speech="data/clean/clean_speech.wav", noises=("office", "white"), seconds=5):
    report = []
    with zipfile.ZipFile(fixture) as z:
        clean = load_wav(z, speech)
        # Use first five seconds, input at -20 dBFS for repeatability
        n = min(len(clean), seconds*48000)
        clean = clean[:n]
        clean *= 0.1 / max(1e-10, float(np.sqrt(np.mean(clean**2))))
        for name in noises:
            raw_noise = load_wav(z, f'data/noise/{name}.wav')
            noise = raw_noise[48000:48000+n].copy()
            if len(noise) != n: raise ValueError('reference noise is too short')
            for snr_db in (0, 5, 10):
                noise *= (np.sqrt(np.mean(clean**2)) / max(1e-12, np.sqrt(np.mean(noise**2)))) / 10**(snr_db/20)
                mixture = clean + noise
                input_pcm = np.repeat(mixture[:,None], 2, axis=1).astype('<f4')
                with tempfile.TemporaryDirectory() as d:
                    d = Path(d)
                    inp, voice, amb, rev = (d/'in.f32', d/'voice.f32',d/'amb.f32',d/'rev.f32')
                    input_pcm.tofile(inp)
                    subprocess.run([binary, str(inp), str(voice), str(amb), str(rev)], check=True)
                    output = np.fromfile(voice, '<f4').reshape(-1,2)[:,0].astype('float64')
                    residual = np.fromfile(amb,'<f4').reshape(-1,2)[:,0].astype('float64')
                    wet = np.fromfile(rev,'<f4').reshape(-1,2)[:,0].astype('float64')
                latency = 1024
                start = 48000  # leave 1 second for noise estimator warmup
                ref = clean[start:n-latency]
                dry = output[start+latency:n]
                original_mix = mixture[start:n-latency]
                reconstructed = (output+residual+wet)[latency:]
                err = float(np.max(np.abs(reconstructed - mixture[:-latency])))
                score_input = si_sdr(ref,original_mix)
                score_voice = si_sdr(ref,dry)
                report.append(dict(noise=name, snr_db=snr_db,
                    input_si_sdr_db=round(score_input, 3),
                    voice_si_sdr_db=round(score_voice, 3),
                    improvement_db=round(score_voice-score_input, 3),
                    voice_rms=round(float(np.sqrt(np.mean(dry**2))),6),
                    reconstruction_peak_error=round(err, 9)))
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--fixture-zip', required=True, help='External reference ZIP, not redistributed')
    parser.add_argument('--render-binary', required=True)
    parser.add_argument('--out', help='Optional JSON results path')
    parser.add_argument('--speech', default='data/clean/clean_speech.wav')
    parser.add_argument('--noises', default='office,white', help='Comma separated fixture noise names')
    parser.add_argument('--seconds', default=5, type=int)
    args = parser.parse_args()
    rows = run(args.render_binary, args.fixture_zip, args.speech, tuple(args.noises.split(',')), args.seconds)
    payload = {'kind':'clean-spoken-speech plus synthetic noise',
               'reference_licensing':'verify upstream before any redistribution',
               'sample_rate':48000, 'latency_samples':1024, 'speech': args.speech, 'noises': args.noises,
               'method':'48k resampled speech, levels normalized -20 dBFS, mono stereo-duplicated; first second excluded from SI-SDR',
               'results':rows,
               'mean_improvement_db':round(float(np.mean([r['improvement_db'] for r in rows])),3)}
    out = json.dumps(payload,indent=2)
    print(out)
    if args.out: Path(args.out).write_text(out+'\n',encoding='utf-8')
