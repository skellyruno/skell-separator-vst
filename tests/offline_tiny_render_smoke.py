#!/usr/bin/env python3
import tempfile
from pathlib import Path
import subprocess
import numpy as np
import soundfile as sf

root = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    samples = 48000
    time = np.arange(samples) / 48000
    mix = np.stack((.2*np.sin(2*np.pi*231*time), -.11*np.cos(2*np.pi*109*time)), axis=1)
    sf.write(str(tmp/'mix.wav'), mix, 48000, subtype='FLOAT')
    subprocess.run(['python3', str(root/'tests/render_tiny_wav.py'),
                    str(tmp/'mix.wav'),str(tmp/'out')], check=True)
    outs = []
    for name in ('voice', 'ambience', 'reverb'):
        out, sr = sf.read(str(tmp/'out'/f'{name}.wav'),dtype='float64',always_2d=True)
        assert sr==48000 and len(out)==samples+2880
        assert np.isfinite(out).all()
        outs.append(out)
    summation = sum(outs)
    ref = np.pad(mix, ((2880,0),(0,0)))
    difference = np.max(np.abs(summation-ref))
    print('Neural C++ end-to-end stem reconstruction maximum difference:', difference)
    assert difference < 1e-5
print('tiny neural offline render smoke: PASS')
