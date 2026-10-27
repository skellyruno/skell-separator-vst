#!/usr/bin/env python3
"""Simple audio render-contract smoke test; Python stdlib only."""
import array
import math
from pathlib import Path
import struct
import subprocess
import tempfile

SAMPLES = 8192
DELAY = 1024

def read(path):
    data = Path(path).read_bytes()
    assert len(data) == SAMPLES*2*4, (path, len(data))
    return struct.unpack('<' + 'f'*(SAMPLES*2),data)


def main():
    samples = []
    for i in range(SAMPLES):
        left = (0.8 if i == 1600 else 0.0) + 0.15*math.sin(i * 0.031)
        right = (-0.5 if i == 1600 else 0.0) + 0.12*math.cos(i*0.023)
        samples.extend((left,right))
    with tempfile.TemporaryDirectory() as temp:
        p = Path(temp)
        args = [p/x for x in ('input.f32','voice.f32','ambience.f32','reverb.f32')]
        args[0].write_bytes(struct.pack('<' + 'f'*len(samples),*samples))
        subprocess.run(['bash',str(Path(__file__).with_name('run_offline_render.sh')), *map(str,args)], check=True)
        voice, amb, rev = map(read,args[1:])
    error = 0.0
    for idx in range(SAMPLES):
        for ch in range(2):
            off = idx*2+ch
            expected = samples[off-DELAY*2] if idx >= DELAY else 0.0
            reconstructed = voice[off]+amb[off]+rev[off]
            error = max(error,abs(reconstructed-expected))
            assert math.isfinite(reconstructed)
            if idx < DELAY:
                assert voice[off] == 0.0 and rev[off] == 0.0 and amb[off] == 0.0
    assert error < 1e-5, error
    print(f'offline-render smoke: 1024 samples fixed delay; max complement error {error:.3g}; PASS')

if __name__ == '__main__': main()
