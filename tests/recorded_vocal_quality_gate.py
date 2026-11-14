#!/usr/bin/env python3
"""Measure LIVE Clean and known-room dereverb on separately supplied recorded vocals.

Example: python tests/recorded_vocal_quality_gate.py --dry dry_voice.flac \
         --noise recorded_room_noise.wav --report /tmp/score.json

No music or dataset bytes are embedded, uploaded or written to GitHub. This is
not a full-song neural separation benchmark and must not be reported as one.
Requires offline ffmpeg, a C++17 compiler, and numpy. No plugin dependencies.
"""
import argparse
import json
import math
from pathlib import Path
import shutil
import subprocess
import tempfile

import numpy as np


def decode(path: Path, rate: int, start_sec: float, duration_sec: float) -> np.ndarray:
    if not shutil.which('ffmpeg'):
        raise RuntimeError('ffmpeg is required for FLAC/WAV decoding')
    result = subprocess.run([
        'ffmpeg', '-nostdin', '-v', 'error', '-ss', str(start_sec), '-i', str(path), '-t', str(duration_sec),
        '-ar', str(rate), '-ac', '2', '-f', 'f32le', '-acodec', 'pcm_f32le', '-'
    ], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    samples = np.frombuffer(result.stdout, dtype='<f4').copy().reshape(-1, 2)
    if len(samples) < rate or not np.isfinite(samples).all():
        raise ValueError('Input needs at least 1 second of finite stereo audio')
    return samples


def rms(x: np.ndarray) -> float:
    return float(np.sqrt(np.mean(x.astype(np.float64)**2) + 1e-18))


def sisdr(pred: np.ndarray, reference: np.ndarray) -> float:
    a = pred.astype(np.float64).ravel()
    b = reference.astype(np.float64).ravel()
    a -= np.mean(a)
    b -= np.mean(b)
    scale = float(np.dot(a, b) / (np.dot(b, b) + 1e-15))
    projected = scale * b
    residual = a - projected
    return float(10*np.log10((np.dot(projected, projected)+1e-15) /
                             (np.dot(residual, residual)+1e-15)))


def process(exe: Path, mode: str, audio: np.ndarray, rate: int, args) -> np.ndarray:
    raw = np.ascontiguousarray(audio, dtype='<f4').tobytes()
    result = subprocess.run([str(exe), mode, str(rate), *map(str, args)],
                            input=raw, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise RuntimeError(result.stderr.decode(errors='replace'))
    data = np.frombuffer(result.stdout, dtype='<f4').copy().reshape(-1, 2)
    if data.shape != audio.shape or not np.isfinite(data).all():
        raise RuntimeError('DSP output invalid, wrong dimensions or nonfinite')
    return data


def benchmark(dry, noise, rate, exe, target_snr_db):
    if len(noise) < len(dry) + rate:
        noise = np.tile(noise, (math.ceil((len(dry) + rate)/len(noise)), 1))
    silence = np.zeros((rate, 2), dtype='float32')
    vocal = np.vstack((silence, dry))
    noise = noise[:len(vocal)].astype('float32').copy()
    # Only source-active windows can measure overlapping noise. Never scale
    # digital silence into a fake reference by dividing by ~zero RMS.
    source_voice_rms = rms(dry)
    source_noise_rms = rms(noise[rate:])
    if source_voice_rms < 1e-5 or source_noise_rms < 1e-5:
        return {'scorable': False,
                'reason': 'Original vocal or instrument stem is essentially silent',
                'sample_rate': rate, 'duration_sec': len(dry)/rate,
                'segment_source_voice_rms': source_voice_rms,
                'segment_source_instrument_rms': source_noise_rms,
                'reference_snr_db': target_snr_db, 'results': [], 'dereverb': None}
    ratio = 10**(target_snr_db/20)
    noise *= np.float32(source_voice_rms/(ratio*source_noise_rms))
    peak = float(np.max(np.abs(vocal + noise)))
    common_gain = min(1., 0.93/max(peak, 1e-12))
    vocal *= common_gain
    noise *= common_gain
    mixture = vocal + noise
    before = sisdr(mixture[rate:], vocal[rate:])
    # The controlled reference should measure close to its target SNR.
    # Massive SI-SDR (>100 dB) means nearly silent or degenerate stems.
    if not np.isfinite(before) or abs(before - target_snr_db) > 1.5:
        return {'scorable': False,
                'reason': 'Baseline SI-SDR inconsistent with the declared mixture SNR',
                'sample_rate': rate, 'duration_sec': len(dry)/rate,
                'reference_snr_db': target_snr_db,
                'input_sisdr_db': before, 'results': [], 'dereverb': None}
    cases=[]
    for clean,gate,tails in [(0,0,0.35),(.4,0,.35),(.7,0,.35),(.9,0,.35),(.7,.3,.35),(.7,.7,.35)]:
        got = process(exe,'cleanup',mixture,rate,[clean,gate,tails])
        isolated = process(exe,'cleanup',noise,rate,[clean,gate,tails])
        after = sisdr(got[rate:],vocal[rate:])
        cases.append({'clean':clean,'gate':gate,'tails':tails,
                      'input_sisdr_db':round(before,3),
                      'output_sisdr_db':round(after,3),
                      'delta_sisdr_db':round(after-before,3),
                      'noise_only_reduction_db':round(20*math.log10(rms(noise[rate:])/rms(isolated[rate:])),3),
                      'max_abs_output':round(float(np.max(np.abs(got))),4)})
    # One controlled room with two known synthetic reflection taps.
    d1,d2=round(.013*rate),round(.037*rate)
    wet=dry.copy()
    wet[d1:]+=.26*dry[:-d1]
    wet[d2:]+=.18*dry[:-d2]
    recovered=process(exe,'dereverb',wet,rate,[d1,.26,d2])
    wrong=process(exe,'dereverb',wet,rate,[d1+37,.26,d2])
    room={'room_impulse_response':'synthetic, known two-tap reflections',
          'dry_source':'real provided recording',
          'before_sisdr_db':round(sisdr(wet,dry),3),
          'after_matched_sisdr_db':round(sisdr(recovered,dry),3),
          'after_mismatched_sisdr_db':round(sisdr(wrong,dry),3),
          'matched_error_rms':rms(recovered-dry)}
    return {'source_audio':'external recorded vocal; singing must be independently confirmed',
            'scorable':True, 'segment_source_voice_rms':source_voice_rms,
            'segment_source_instrument_rms':source_noise_rms,
            'source_audio_redistributed':False,
            'noise_recording':'external recording',
            'reference_snr_db':target_snr_db,
            'sample_rate':rate,'duration_sec':len(dry)/rate,
            'results':cases,'dereverb':room}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--dry',type=Path,required=True)
    p.add_argument('--noise',type=Path,required=True)
    p.add_argument('--report',type=Path,required=True)
    p.add_argument('--rate',type=int,default=44100)
    p.add_argument('--snr-db',type=float,default=6.)
    p.add_argument('--compiler',default='g++')
    p.add_argument('--start-sec',type=float,default=0.)
    p.add_argument('--duration-sec',type=float,default=15.)
    a=p.parse_args()
    root=Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='skell-real-voice-') as t:
        runner=Path(t)/'recorded_vocal_runner'
        subprocess.run([a.compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror',
                        str(root/'tests/recorded_vocal_runner.cpp'),'-o',str(runner)],check=True)
        dry=decode(a.dry,a.rate,a.start_sec,a.duration_sec)
        noise=decode(a.noise,a.rate,a.start_sec,a.duration_sec)
        report=benchmark(dry,noise,a.rate,runner,a.snr_db)
        report['segment_start_sec'] = a.start_sec
    a.report.parent.mkdir(parents=True,exist_ok=True)
    a.report.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    if not report['scorable']:
        print(f"UNSCORABLE {a.start_sec:g}s: {report['reason']}. Source activity was too low.")
        print(f'JSON report: {a.report}')
        return
    print(f"Recorded voice {report['duration_sec']:.2f}s, {a.rate} Hz, baseline {report['results'][0]['input_sisdr_db']} dB SI-SDR")
    for x in report['results']:
        print(f"Clean {x['clean']:4.2f}, Gate {x['gate']:4.2f}: delta {x['delta_sisdr_db']:+.3f} dB, isolated noise {x['noise_only_reduction_db']:+.3f} dB")
    print(f"Known-room dereverb matched RMSE: {report['dereverb']['matched_error_rms']:.9f}")
    print(f'JSON report: {a.report}')

if __name__=='__main__':
    main()
