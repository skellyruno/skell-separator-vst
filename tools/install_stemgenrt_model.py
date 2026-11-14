#!/usr/bin/env python3
"""Install a separately obtained StemgenRT research model after checking rights.

Never downloads third-party data, never bypasses noncommercial model terms.
"""
import argparse
import hashlib
from pathlib import Path
import shutil
import os

EXPECTED_SHA = '0a49388dd2a47986ab9a161c8764c22853b4d601c188ac052eb530f076d513c7'
EXPECTED_SIZE = 31946241


def verify_model(path):
    if path.stat().st_size != EXPECTED_SIZE:
        raise ValueError(f'wrong model size: {path.stat().st_size} != {EXPECTED_SIZE}')
    digest = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            digest.update(block)
    if digest.hexdigest() != EXPECTED_SHA:
        raise ValueError('SHA-256 mismatch: wrong or modified model')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('onnx', type=Path, help='Your existing independent.onnx')
    ap.add_argument('--acknowledge-noncommercial', action='store_true',
                    help='Confirm research-only/noncommercial usage and acceptance of upstream weight terms')
    ap.add_argument('--dest', type=Path, help='Override Windows app data destination (for testing)')
    args = ap.parse_args()
    if not args.acknowledge_noncommercial:
        ap.error('Use --acknowledge-noncommercial only after reviewing the CC BY-NC-SA 4.0 weights notice.')
    verify_model(args.onnx)
    dest = args.dest
    if dest is None:
        appdata = os.environ.get('APPDATA')
        if not appdata:
            ap.error('APPDATA is not set. This installer targets Windows; supply --dest for testing.')
        dest = Path(appdata) / 'Skell' / 'Separator' / 'Models' / 'independent.onnx'
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.resolve() != args.onnx.resolve():
        shutil.copyfile(args.onnx, dest)
    verify_model(dest)
    print('StemgenRT model installed (research only):', dest)

if __name__ == '__main__':
    main()
