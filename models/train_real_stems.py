#!/usr/bin/env python3
"""Train a *candidate*, never auto-ship, neural mask on rights-verified real stems.

Input manifest JSON:
{
  "tracks": [
    {"split":"train", "artist":"A", "source_url":"https://...",
     "license":"CC-BY-4.0", "dry_voice":"/path/voice.wav",
     "instrumental":"/path/music.wav", "wet_only_reverb":"/path/reverb.wav"},
    {"split":"validation", "artist":"B", ...}
  ]
}

Never point train and validation at the same singer. This code intentionally
produces a *candidate* header and evidence JSON only. Production promotion
requires a separately held-out singer and recorded instrumental/leakage tests.
"""
import argparse
import json
from pathlib import Path
from collections import Counter

import numpy as np
import torch
from scipy.signal import resample_poly
import soundfile as sf

SR=48000; FFT=1024; WIN=960; BANDS=64; HIDDEN=48
WINDOW=np.sin(np.pi*(np.arange(WIN,dtype=np.float32)+.5)/WIN)
BAND=np.clip(np.floor(np.log(np.maximum(np.arange(FFT//2+1,dtype=np.float32)*SR/FFT,55)/55)/np.log(22000/55)*(BANDS-1)).astype(np.int32),0,BANDS-1)


def load_wav(path):
    audio, rate=sf.read(str(path),dtype='float32',always_2d=True)
    if audio.ndim!=2 or audio.shape[1]>2 or not np.isfinite(audio).all() or len(audio)<SR//2:
        raise ValueError(f'Bad stem: {path}')
    if rate!=SR:
        from math import gcd
        factor=gcd(SR,rate)
        audio=resample_poly(audio,SR//factor,rate//factor,axis=0).astype(np.float32)
    return np.repeat(audio,2,axis=1) if audio.shape[1]==1 else audio


def load_manifest(path):
    data=json.loads(Path(path).read_text())
    if not isinstance(data.get('tracks'),list):raise ValueError('manifest requires tracks[]')
    tracks=[]
    for i,row in enumerate(data['tracks']):
        for key in ('split','artist','source_url','license','dry_voice','instrumental'):
            if not row.get(key):raise ValueError(f'tracks[{i}] requires {key}')
        if row['split'] not in ('train','validation'):raise ValueError('split must be train or validation')
        paths={key:load_wav(row[key]) for key in ('dry_voice','instrumental')}
        if row.get('wet_only_reverb'):paths['wet_only_reverb']=load_wav(row['wet_only_reverb'])
        n=min(map(len,paths.values()))
        if n < SR: raise ValueError('Need 1+ sec aligned audio per source')
        tracks.append({'artist':row['artist'],'split':row['split'],'license':row['license'],
                       'source_url':row['source_url'],'audio':{k:v[:n] for k,v in paths.items()}})
    counts=Counter(x['split'] for x in tracks)
    if counts['train']<1 or counts['validation']<1:raise ValueError('Need train and validation sources')
    for a in {t['artist'] for t in tracks}:
        if len({t['split'] for t in tracks if t['artist']==a})>1:
            raise ValueError('Artist overlap between train and validation: '+a)
    return tracks


def feature_frames(tracks,seed,n_samples):
    rng=np.random.default_rng(seed)
    frames=[]
    for j in range(n_samples):
        t=tracks[int(rng.integers(len(tracks)))]['audio']
        start=int(rng.integers(0,len(t['dry_voice'])-WIN))
        dry=t['dry_voice'][start:start+WIN]
        inst=t['instrumental'][start:start+WIN]
        rev=t.get('wet_only_reverb')
        rev=rev[start:start+WIN] if rev is not None else 0.0
        # Independent source gains and sources absent; includes NULL vocal
        # examples to explicitly penalize instrument leakage.
        voice_gain=0.0 if rng.random()<.22 else float(10**(rng.uniform(-6,6)/20))
        inst_gain=0.0 if rng.random()<.14 else float(10**(rng.uniform(-6,6)/20))
        dry=dry*voice_gain
        music=inst*inst_gain + rev*voice_gain
        mix=dry+music
        def bandmag(samples):
            spec=np.abs(np.fft.rfft(samples*WINDOW[:,None],n=FFT,axis=0)).mean(axis=1)
            accum=np.bincount(BAND,weights=spec,minlength=BANDS)
            cnt=np.maximum(1,np.bincount(BAND,minlength=BANDS))
            return accum/cnt
        total=bandmag(mix)
        src=bandmag(dry); noise=bandmag(music)
        feat=np.log1p(total)
        feat=np.clip(feat-feat.mean(),-6,6)
        target=src/(src+noise+1e-6)
        weight=np.sqrt(total+.1)
        weight/=max(np.mean(weight),1e-7)
        # An extra HF weight is NOT an arbitrary HF gain: it learns real
        # reference masks on sibilants and penalizes leakage when absent.
        high=(np.arange(BANDS)>40)
        weight[high]*=1.25
        frames.append((feat.astype(np.float32),target.astype(np.float32),weight.astype(np.float32)))
    return [torch.from_numpy(np.stack([frame[i] for frame in frames])) for i in range(3)]


class Model(torch.nn.Module):
    def __init__(self):
        super().__init__()
        self.l1=torch.nn.Linear(BANDS,HIDDEN)
        self.l2=torch.nn.Linear(HIDDEN,BANDS)
    def forward(self,x):return torch.sigmoid(self.l2(torch.relu(self.l1(x))))


def write_header(model,path):
    state=model.state_dict()
    header=['// Candidate weights derived from external recordings. NOT cleared for release.\n',
            '// Check every source license and evaluation results before promotion.\n',
            '#pragma once\n#include <array>\nnamespace skell::tiny_weights {\n']
    for field in ('l1.weight','l1.bias','l2.weight','l2.bias'):
        arr=state[field].detach().numpy().reshape(-1)
        header.append(f'inline constexpr std::array<float, {len(arr)}> {field.replace(".","_")} = {{\n')
        for i in range(0,len(arr),8):
            header.append('    '+', '.join(f'{float(v):.9g}f' if '.' in f'{float(v):.9g}' or 'e' in f'{float(v):.9g}' else f'{float(v):.9g}.0f' for v in arr[i:i+8])+',\n')
        header.append('};\n')
    header.append('}\n')
    path.write_text(''.join(header),encoding='utf-8')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest',type=Path,required=True)
    p.add_argument('--candidate',type=Path,required=True,help='non-production C++ weights header')
    p.add_argument('--report',type=Path,required=True)
    p.add_argument('--steps',type=int,default=1000)
    p.add_argument('--samples',type=int,default=16000)
    args=p.parse_args()
    if args.steps < 1 or args.samples<20:p.error('Require steps>=1, samples>=20')
    torch.manual_seed(17313)
    torch.set_num_threads(min(4,torch.get_num_threads()))
    tracks=load_manifest(args.manifest)
    train=[x for x in tracks if x['split']=='train'];val=[x for x in tracks if x['split']=='validation']
    x,y,w=feature_frames(train,17313,args.samples)
    xv,yv,wv=feature_frames(val,17314,max(64,args.samples//5))
    net=Model();opt=torch.optim.Adam(net.parameters(),lr=.001)
    rng=np.random.default_rng(17313)
    best=float('inf');snapshot=None
    for step in range(args.steps):
        ix=torch.from_numpy(rng.integers(len(x),size=128))
        loss=(w[ix]*(net(x[ix])-y[ix])**2).mean()
        opt.zero_grad();loss.backward();opt.step()
        if step % 25==0 or step==args.steps-1:
            with torch.no_grad():score=float((wv*(net(xv)-yv)**2).mean())
            if score<best:
                best=score;snapshot={key:t.detach().clone() for key,t in net.state_dict().items()}
    net.load_state_dict(snapshot)
    with torch.no_grad():
        candidate=float((wv*(net(xv)-yv)**2).mean())
        half=float((wv*(0.5-yv)**2).mean())
        # Independent null-vocal validation to prevent training a music leak.
        null=0.0
        vx=net(xv)
        silent=yv.mean(dim=1)<.05
        if silent.any(): null=float(vx[silent].mean())
    report={'train_artists':len({t['artist'] for t in train}),
            'held_out_validation_artists':len({t['artist'] for t in val}),
            'candidate_weighted_mask_mse':candidate,'half_mask_mse':half,
            'null_vocal_predicted_mean':null,
            'training_sources':[{'source_url':t['source_url'],'license':t['license'],
                                 'split':t['split'],'artist':t['artist']} for t in tracks],
            'approved_for_commercial_release':False,
            'warning':'Never automatically replace plugin weights: needs separate FULL-song singing, leakage, consonant and reverb benchmarks.'}
    args.candidate.parent.mkdir(parents=True,exist_ok=True)
    args.report.parent.mkdir(parents=True,exist_ok=True)
    write_header(net,args.candidate)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='training_sources'},indent=2))

if __name__=='__main__':main()
