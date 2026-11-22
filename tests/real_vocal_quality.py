#!/usr/bin/env python3
"""Run production C++ classical/TinyVocalNet on separately licensed isolated stems.

Input WAVs MUST be sample-synchronized source stems, not outputs of another
separator. Dry voice and wet-only vocal reverb are different targets. An optional
original algorithmic reverb send tests dereverberation in a controlled mixture;
that case is reported as simulated reverb over REAL source audio.

DO NOT bundle source recordings in this repository. This benchmark only reads
local data and emits metrics, not an inferred 'quality score' without references.
"""
import argparse
import json
import subprocess
import tempfile
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.signal import butter, sosfilt, resample_poly

from benchmark_vocals import si_sdr, sdr

SR = 48000
LATENCY = 2880
STEMS = ('voice','ambience','reverb')


def audio(path):
    signal, rate = sf.read(str(path), dtype='float32', always_2d=True)
    if not np.isfinite(signal).all() or signal.size < 2000:
        raise ValueError(f'Bad or too-short audio: {path}')
    if signal.shape[1] > 2:
        raise ValueError(f'Only mono/stereo sources supported: {path}')
    if rate != SR:
        from math import gcd
        g = gcd(rate, SR)
        signal = resample_poly(signal, SR // g, rate // g, axis=0).astype(np.float32)
    if signal.shape[1] == 1:
        signal = np.repeat(signal, 2, axis=1)
    return signal


def rms(x):
    return float(np.sqrt(np.mean(x.astype(np.float64) ** 2) + 1e-15))


def simulated_wet_only(dry):
    """Controlled reverb for *testing*, not a substitute for real wet stems."""
    n = len(dry)
    ir = np.zeros(16000, dtype=np.float32)
    # Fixed 30/53/87ms early reflections and decaying late-reflection field.
    ir[1440] = .31; ir[2544] = -.19; ir[4176] = .12
    rng = np.random.default_rng(17513)
    late = np.arange(4800, len(ir))
    ir[late] += (rng.standard_normal(len(late)) * .013 * np.exp(-(late-4800)/(SR*.115))).astype(np.float32)
    out = np.zeros_like(dry)
    from scipy.signal import fftconvolve
    for ch in range(2):
        out[:,ch] = fftconvolve(dry[:,ch], ir, mode='full')[:n]
    return out * .45


def hi_pass(x):
    sos = butter(4, [3000, 10000], fs=SR, btype='bandpass', output='sos')
    return sosfilt(sos, x, axis=0)


def consonant_regions(dry):
    """Reference-derived HF onset mask; does not inspect predicted vocals."""
    hp = hi_pass(dry)
    hop = 240
    frames = len(dry) // hop
    n = 3 * hop
    envelope = np.empty(frames, dtype=np.float64)
    for k in range(frames):
        i = k*hop
        envelope[k] = rms(hp[max(0,i-n):i+hop])
    # Select energetic high-band onsets, not stationary vowels or quiet noise.
    positive_flux = np.maximum(0, envelope - np.roll(envelope, 2))
    positive_flux[:2] = 0
    active = envelope > max(float(np.percentile(envelope, 45)) * 1.5, 1e-5)
    threshold = float(np.percentile(positive_flux[active], 80)) if np.any(active) else np.inf
    selected = (positive_flux > max(threshold, 1e-6)) & active
    indices = np.zeros(len(dry), dtype=bool)
    for k in np.flatnonzero(selected):
        start = max(0, k*hop-240)
        indices[start:min(len(dry), k*hop+720)] = True
    return indices


def latency_align(estimate, target, latency):
    if len(estimate) < len(target)+latency:
        raise ValueError(f'Render truncated; expected {len(target)+latency} samples, got {len(estimate)}')
    return estimate[latency:latency + len(target)]


def assess(dry, inst, wet, render, latency, label):
    voice = latency_align(render['voice'],dry,latency)
    ambience = latency_align(render['ambience'],dry,latency)
    reverb = latency_align(render['reverb'],dry,latency)
    mixture = dry + inst + wet
    reconstruction = voice + ambience + reverb
    onset = consonant_regions(dry)
    # For a pure instrument case there is no vocal reference: use false
    # voice energy relative to instrumental energy, not undefined SI-SDR.
    hf_reference = hi_pass(dry)
    hf_prediction = hi_pass(voice)
    onset_score = si_sdr(hf_reference[onset],hf_prediction[onset]) if onset.sum() >= 1000 and rms(hf_reference[onset]) > 1e-5 else None
    result = {
        'variant':label, 'latency_samples':latency,
        'vocal_si_sdr_db':si_sdr(dry,voice) if rms(dry)>1e-6 else None,
        'vocal_sdr_db':sdr(dry,voice) if rms(dry)>1e-6 else None,
        'unprocessed_vocal_si_sdr_db':si_sdr(dry,mixture) if rms(dry)>1e-6 else None,
        'consonant_onset_hf_si_sdr_db':onset_score,
        'reference_hf_onset_coverage':round(float(onset.mean()),5),
        'instrumental_residual_sdr_db':sdr(inst, ambience) if rms(inst)>1e-6 else None,
        'wet_only_reverb_sdr_db':sdr(wet,reverb) if rms(wet)>1e-6 else None,
        'reconstruction_max_error':float(np.max(np.abs(reconstruction-mixture)))
    }
    if result['vocal_si_sdr_db'] is not None and result['unprocessed_vocal_si_sdr_db'] is not None:
        result['vocal_vs_unprocessed_si_sdr_gain_db']= result['vocal_si_sdr_db']-result['unprocessed_vocal_si_sdr_db']
    return result


def compile_engines(root, out, cxx):
    common=[cxx,'-std=c++17','-O2','-pthread', '-I'+str(root/'tests/shim'),'-I'+str(root/'Source')]
    dsp=[str(root/'Source/dsp/Separator.cpp'),str(root/'Source/dsp/SpectralAnalyzer.cpp')]
    subprocess.run(common+[str(root/'tests/render_classical.cpp'),*dsp,'-o',str(out/'classical')],check=True)
    subprocess.run(common+['-DSKELL_WITH_TINY_VOCAL=1',str(root/'tests/render_tiny.cpp'),str(root/'Source/dsp/TinyVocalNet.cpp'),
                           str(root/'Source/dsp/StreamingNeural.cpp'),*dsp,'-o',str(out/'neural')],check=True)


def render(exe, mixture, out, strength=None):
    # Classical renderer does not flush its own 1024-sample tail; feed zeros
    # to obtain the same untrimmed output-length contract as the neural renderer.
    padded = mixture
    if strength is None: padded = np.pad(mixture, ((0,1024),(0,0)))
    (out/'input.f32').write_bytes(np.asarray(padded,dtype='<f4',order='C').tobytes())
    paths=[out/(s+'.f32') for s in STEMS]
    command=[str(exe),str(out/'input.f32'),*[str(x) for x in paths]]
    if strength is not None: command.append(str(strength))
    subprocess.run(command,check=True,stdout=subprocess.DEVNULL)
    return {s:np.fromfile(p,dtype='<f4').reshape(-1,2) for s,p in zip(STEMS,paths)}


def run_benchmark(dry,inst,wet,root,cxx,has_instrument=True):
    # Shared attenuation prevents a clipped mixture without changing ratios.
    peak=float(np.max(np.abs(dry+inst+wet)))
    scale=min(1.0, .95/max(peak,1e-9))
    dry, inst, wet = [x*scale for x in (dry,inst,wet)]
    # Do not fabricate a zero-valued "reference instrumental" from a
    # vocal/effect-only archive. No instrumental reference means no instrument
    # leakage test and no instrument-only condition in the output.
    conditions={'vocal_only':(dry,np.zeros_like(inst),np.zeros_like(wet))}
    if has_instrument:
        conditions['instrument_only']=(np.zeros_like(dry),inst,np.zeros_like(wet))
        conditions['mixed']=(dry,inst,wet)
    if np.any(wet): conditions['dry_plus_wet']=(dry,np.zeros_like(inst),wet)
    results=[]
    with tempfile.TemporaryDirectory(prefix='skell-real-quality-') as folder:
        out=Path(folder);compile_engines(root,out,cxx)
        for name,(v,m,r) in conditions.items():
            mixture=v+m+r
            for variant,strength in [('classical',None),('tiny_65',.65),('tiny_100',1.)]:
                preds=render(out/('classical' if strength is None else 'neural'),mixture,out,strength)
                delay=1024 if strength is None else LATENCY
                metrics=assess(v,m,r,preds,delay,variant)
                if name=='instrument_only':
                    metrics['false_voice_rms_db_relative_to_instrument']=float(20*np.log10((rms(preds['voice'][delay:delay+len(m)])+1e-12)/(rms(m)+1e-12)))
                results.append({'condition':name,**metrics})
    return results


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--voice',type=Path, help='Isolated *dry* vocal WAV recorded by a singer')
    p.add_argument('--instrumental',type=Path,help='Genuine isolated instrumental WAV with NO vocals (omit if unavailable)')
    p.add_argument('--reverb',type=Path,help='Separate wet-only vocal-reverb WAV (not full wet vocals)')
    p.add_argument('--printed-reverb',type=Path,
                   help='Same-take dry+reverb print; subtract voice to get a DIFFERENTIAL effect return, not an independent wet-only stem')
    p.add_argument('--simulated-reverb',action='store_true',help='Add controlled reverb to real vocal for testing')
    p.add_argument('--license', help='License/SPDX identifier for both sources, with rights verified')
    p.add_argument('--attribution',help='Artist and source URL required for attribution datasets')
    p.add_argument('--max-seconds',type=float,default=12.0)
    p.add_argument('--offset-seconds',type=float,default=0.0,
                   help='Start all references at the same fixed timeline offset; never auto-align estimated outputs')
    p.add_argument('--output',type=Path)
    p.add_argument('--compiler',default='c++')
    p.add_argument('--self-test',action='store_true')
    args=p.parse_args()
    if args.self_test:
        import tempfile
        with tempfile.TemporaryDirectory() as folder:
            f=Path(folder)
            sr=48000; t=np.arange(sr*2,dtype=np.float32)/sr
            v=.12*np.sin(2*np.pi*225*t)[:,None]*np.ones((1,2),dtype=np.float32)
            m=.11*np.sin(2*np.pi*390*t)[:,None]*np.ones((1,2),dtype=np.float32)
            for name,x in [('voice',v),('instrumental',m)]: sf.write(str(f/(name+'.wav')),x,sr,subtype='FLOAT')
            result=run_benchmark(v,m,simulated_wet_only(v),Path(__file__).resolve().parent.parent,args.compiler)
            assert len(result)==12
            assert all(r['reconstruction_max_error']<2e-5 for r in result)
            assert any(r['wet_only_reverb_sdr_db'] is not None for r in result)
            partial=run_benchmark(v,np.zeros_like(v),simulated_wet_only(v),
                                  Path(__file__).resolve().parent.parent,args.compiler,
                                  has_instrument=False)
            assert len(partial)==6
            assert all(r['condition']!='instrument_only' and r['instrumental_residual_sdr_db'] is None
                       for r in partial)
        print('Real-stem benchmark self-test passed (procedural fixture only; NOT real singing)')
        return
    if args.voice is None or args.license is None or args.attribution is None:
        p.error('--voice --license --attribution are required for a real-recording benchmark')
    if sum(bool(v) for v in (args.reverb,args.printed_reverb,args.simulated_reverb))>1:
        p.error('Choose just one of --reverb, --printed-reverb, or --simulated-reverb')
    if not np.isfinite(args.offset_seconds) or args.offset_seconds<0 or not np.isfinite(args.max_seconds) or args.max_seconds<=0:
        p.error('Offset must be finite and non-negative; duration must be finite and positive')
    if args.printed_reverb:
        # A simple difference is a valid effect reference only if both prints
        # have identical sample grids. It may still include other processing
        # differences, so never advertise it as independent natural reverb.
        dry_info=sf.info(str(args.voice)); wet_info=sf.info(str(args.printed_reverb))
        if (dry_info.samplerate,dry_info.frames,dry_info.channels)!=(wet_info.samplerate,wet_info.frames,wet_info.channels):
            p.error('--printed-reverb requires identical original rate, frame count and channel count')
    voice=audio(args.voice)
    inst=audio(args.instrumental) if args.instrumental else np.zeros_like(voice)
    wet=audio(args.reverb) if args.reverb else (audio(args.printed_reverb)-voice if args.printed_reverb else np.zeros_like(voice))
    first=int(round(args.offset_seconds*SR))
    length=min(map(len,(voice,inst,wet)))-first
    length=min(length,int(args.max_seconds*SR))
    if length<SR: p.error('Need at least one second of overlapping aligned recordings at this offset')
    voice,inst,wet = (x[first:first+length] for x in (voice,inst,wet))
    if args.simulated_reverb: wet=simulated_wet_only(voice)
    has_instrument=args.instrumental is not None
    result={'dataset_license':args.license,'attribution':args.attribution,'source':'real isolated stems' if has_instrument else 'real isolated vocal with controlled effects',
            'instrumental_reference':'real isolated instrumental' if has_instrument else 'unavailable; NOT measured',
            'reverb_reference':('real independently supplied wet-only stem' if args.reverb else
                                'same-take print differential; effects may include more than reverb' if args.printed_reverb else
                                'algorithmic send on real recording' if args.simulated_reverb else 'unavailable'),
            'sample_rate':SR,'seconds':round(length/SR,3),
            'offset_seconds':args.offset_seconds,
            'important_limit':'2/3-stem objective check, not training-set-independent song-level benchmark',
            'metrics':run_benchmark(voice,inst,wet,Path(__file__).resolve().parent.parent,args.compiler,has_instrument)}
    output=json.dumps(result,indent=2,allow_nan=False)
    if args.output:args.output.write_text(output+'\n',encoding='utf-8')
    print(output)

if __name__=='__main__':main()
