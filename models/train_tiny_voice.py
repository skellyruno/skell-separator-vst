#!/usr/bin/env python3
"""Deterministic original *synthetic* mask-model training.

This trains a small demonstration 64-band feed-forward neural mask, NOT an
industry vocal separator. No third-party recordings, weights or checkpoints.
Requires numpy and torch for *training only*; inference uses self-contained C++.
"""
import argparse
from pathlib import Path
import numpy as np
import torch

SR = 48000
WIN = 960
FFT = 1024
BANDS = 64
HIDDEN = 48


def band_map():
    hz = np.arange(FFT // 2 + 1, dtype=np.float32) * SR / FFT
    # Matches TinyVocalNet.cpp (log map 55..22000 Hz).
    pos = np.log(np.maximum(hz, 55.0) / 55.0) / np.log(22000.0 / 55.0)
    return np.clip(np.floor(pos * (BANDS - 1)).astype(np.int32), 0, BANDS - 1)


def synth(n, seed):
    rng = np.random.default_rng(seed)
    t = np.arange(WIN, dtype=np.float32)[None, :] / SR
    # Glottal-like harmonic source, vibrato and vowel-like formant emphasis.
    f0 = rng.uniform(85, 850, (n, 1)).astype(np.float32)
    phase = rng.uniform(0, 2*np.pi, (n, 1)).astype(np.float32)
    vibrato = rng.uniform(0, 5, (n, 1)).astype(np.float32)
    ang = 2*np.pi*f0*t + .035*np.sin(2*np.pi*vibrato*t) + phase
    formant1 = rng.uniform(250, 900, (n, 1))
    formant2 = rng.uniform(900, 2700, (n, 1))
    vocal = np.zeros((n, WIN), np.float32)
    for h in range(1, 28):
        freq = f0*h
        amp = (np.exp(-.5*((freq-formant1)/200)**2) +
               .65*np.exp(-.5*((freq-formant2)/350)**2) + .06) / h**.9
        vocal += (amp*np.sin(h*ang + rng.uniform(0, 6.28, (n, 1)))).astype(np.float32)
    vocal *= rng.uniform(.07, .75, (n, 1)).astype(np.float32)
    # Instrumental: several chord notes, bass, percussive/hissy texture.
    music = np.zeros((n, WIN), np.float32)
    for h in range(5):
        base = rng.choice(np.array([55, 65, 82, 110, 147, 196, 246, 330, 440, 587], dtype=np.float32), (n, 1))
        detune = rng.uniform(.97, 1.03, (n, 1))
        amp = rng.uniform(.03, .28, (n, 1))
        music += (amp*np.sin(2*np.pi*base*detune*(h+1)*t + rng.uniform(0, 6.28, (n, 1)))).astype(np.float32)
    transient = np.exp(-t*rng.uniform(15, 85, (n, 1))).astype(np.float32)
    noise = rng.normal(0, 1, (n, WIN)).astype(np.float32)
    music += noise*(rng.uniform(.005, .18, (n, 1)).astype(np.float32) + transient*.11)
    # Full mixtures where the vocal is absent are important for reducing music leak.
    absent = rng.random((n, 1)) < .16
    vocal *= (~absent)
    window = np.sin(np.pi*(np.arange(WIN, dtype=np.float32)+.5) / WIN)
    def spec(x):
        return np.abs(np.fft.rfft(x*window, n=FFT, axis=1)).astype(np.float32)
    v, m = spec(vocal), spec(music)
    mix = spec(vocal+music)
    band = band_map()
    count = np.bincount(band, minlength=BANDS).astype(np.float32)
    def pool(a):
        dst = np.zeros((n, BANDS), np.float32)
        for b in range(BANDS):
            dst[:, b] = np.mean(a[:, band == b], axis=1) if count[b] > 0 else 0.0
        return dst
    features = np.log1p(pool(mix))
    features = np.clip(features - np.mean(features, axis=1, keepdims=True), -6., 6.)
    vband, mband = pool(v), pool(m)
    target = vband/(vband+mband+1.e-5)
    # prevent empty bins overwhelming loss; weight bands by observed mixture energy
    weights = np.sqrt(pool(mix) + .1)
    weights /= np.mean(weights, axis=1, keepdims=True)
    return features.astype(np.float32), target.astype(np.float32), weights.astype(np.float32)

class Model(torch.nn.Module):
    def __init__(self):
        super().__init__()
        self.l1 = torch.nn.Linear(BANDS, HIDDEN)
        self.l2 = torch.nn.Linear(HIDDEN, BANDS)
    def forward(self, x):
        return torch.sigmoid(self.l2(torch.relu(self.l1(x))))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--train-samples', type=int, default=14000)
    parser.add_argument('--steps', type=int, default=750)
    parser.add_argument('--output', type=Path, default=Path(__file__).parent/'TinyVocalWeights.h')
    args = parser.parse_args()
    torch.manual_seed(39217)
    torch.set_num_threads(min(4, torch.get_num_threads()))
    x, y, w = synth(args.train_samples, 39217)
    xv, yv, wv = synth(1600, 39218)
    x, y, w = [torch.from_numpy(a) for a in (x,y,w)]
    xv, yv, wv = [torch.from_numpy(a) for a in (xv,yv,wv)]
    net = Model()
    opt = torch.optim.Adam(net.parameters(), lr=.002)
    rng = np.random.default_rng(39217)
    best = float('inf')
    best_state = None
    for step in range(args.steps):
        ix = torch.from_numpy(rng.integers(0,len(x),size=192))
        pred = net(x[ix])
        loss = (w[ix]*(pred-y[ix])**2).mean()
        opt.zero_grad(); loss.backward(); opt.step()
        if step % 50 == 0 or step == args.steps - 1:
            with torch.no_grad():
                val = (wv*(net(xv)-yv)**2).mean().item()
            if val < best:
                best = val
                best_state = {k:t.detach().clone() for k,t in net.state_dict().items()}
    net.load_state_dict(best_state)
    with torch.no_grad():
        neural = (wv*(net(xv)-yv)**2).mean().item()
        constant = (wv*(torch.full_like(yv, .5)-yv)**2).mean().item()
        zero = (wv*(torch.zeros_like(yv)-yv)**2).mean().item()
    print(f'held-out synthetic weighted mask MSE: neural={neural:.6f} constant=.5:{constant:.6f} silence:{zero:.6f}')
    # Reproducible deterministic values; not a 'licensed pretrained singing model'.
    weights = []
    for name in ('l1.weight','l1.bias','l2.weight','l2.bias'):
        vals = best_state[name].contiguous().numpy().flatten()
        weights.append((name.replace('.', '_'), vals))
    header = ['// GENERATED by models/train_tiny_voice.py; original procedural data only.\n'
              '// Experimental demonstration weights; not trained on actual singing recordings.\n'
              '#pragma once\n'
              '#include <array>\n'
              'namespace skell::tiny_weights {\n']
    for name, vec in weights:
        header.append(f'inline constexpr std::array<float, {len(vec)}> {name} = {{\n')
        for i in range(0, len(vec), 8):
            header.append('    ' + ', '.join(f'{float(v):.9g}f' if '.' in f'{float(v):.9g}' or 'e' in f'{float(v):.9g}' else f'{float(v):.9g}.0f' for v in vec[i:i+8]) + ',\n')
        header.append('};\n')
    header.append('}\n')
    args.output.write_text(''.join(header))
    print('exported', args.output)

if __name__ == '__main__':
    main()
