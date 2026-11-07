#!/usr/bin/env python3
"""Generate stem WAVs for user-owned 48k stereo mixes with the production C++ model.

Example: python tests/render_tiny_wav.py track.wav renders/tiny --strength .65
Comparison with isolated stems:
 python tests/benchmark_vocals.py case --variant tiny --latency-samples 2880
"""
import argparse
from pathlib import Path
import subprocess
import tempfile
import numpy as np
import soundfile as sf


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('input', type=Path)
    p.add_argument('destination', type=Path)
    p.add_argument('--strength', type=float, default=.65)
    p.add_argument('--compiler', default='c++')
    args = p.parse_args()
    if not (0 <= args.strength <= 1):
        p.error('strength must be between 0 and 1')
    data, sr = sf.read(str(args.input), dtype='float32', always_2d=True)
    if sr != 48000 or data.shape[1] != 2:
        p.error('requires stereo 48 kHz WAV; resample/convert beforehand')
    if not np.isfinite(data).all():
        p.error('input has nonfinite audio')
    root = Path(__file__).resolve().parent.parent
    args.destination.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as d:
        d = Path(d)
        command = [args.compiler, '-std=c++17', '-O2', '-pthread',
                   '-DSKELL_WITH_TINY_VOCAL=1', '-I' + str(root / 'tests/shim'),
                   '-I' + str(root / 'Source'),
                   str(root / 'tests/render_tiny.cpp'),
                   str(root / 'Source/dsp/TinyVocalNet.cpp'),
                   str(root / 'Source/dsp/StreamingNeural.cpp'),
                   str(root / 'Source/dsp/Separator.cpp'),
                   str(root / 'Source/dsp/SpectralAnalyzer.cpp'),
                   '-o', str(d / 'render_tiny')]
        subprocess.run(command, check=True)
        (d / 'input.f32').write_bytes(np.ascontiguousarray(data, dtype='<f4').tobytes())
        paths = [d / f'{name}.f32' for name in ('voice', 'ambience', 'reverb')]
        subprocess.run([str(d / 'render_tiny'), str(d / 'input.f32'),
                        *map(str, paths), str(args.strength)], check=True)
        for name, file in zip(('voice', 'ambience', 'reverb'), paths):
            audio = np.fromfile(file, dtype='<f4').reshape(-1, 2)
            sf.write(str(args.destination / f'{name}.wav'), audio, sr, subtype='FLOAT')
    print('Saved stem WAVs to', args.destination, '(48k; untrimmed 2880-sample delay)')

if __name__ == '__main__':
    main()
