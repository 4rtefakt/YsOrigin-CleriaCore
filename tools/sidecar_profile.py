#!/usr/bin/env python3
"""Corpus-wide per-offset field profiler for Ys Origin map sidecars.

For a given extension, walks every record in every room and reports, for each
byte offset, whether it is ever non-zero, and whether the dword at that offset
behaves like a float, a small int, an ASCII string, etc.

Usage:  python tools/sidecar_profile.py SEF [--assets PATH]
"""
import argparse
import glob
import os
import struct
import sys
from collections import Counter

STRIDES = {
    'SOB': 0x2A8, 'SEN': 0x25C, 'SEV': 0x25C, 'SCM': 0x410,
    'SCT': 0x2B0, 'SEF': 0x5C, 'SFO': 0x40, 'SBE': 0x1B0, 'SNF': 0x3C,
}


def records(assets, ext):
    """Yield (path, index, bytes) for every record of every file of this ext."""
    for f in sorted(glob.glob(os.path.join(assets, 'MAP', '*', '*', '*.' + ext))):
        d = open(f, 'rb').read()
        if len(d) < 16:
            continue
        magic, zero, stride, count = struct.unpack('<4sIII', d[:16])
        if len(d) != 16 + stride * count:
            print('!! container mismatch %s: %d != 16+%d*%d' %
                  (f, len(d), stride, count), file=sys.stderr)
            continue
        for i in range(count):
            yield f, i, d[16 + i * stride: 16 + (i + 1) * stride]


def looks_float(v):
    """Is this dword a plausible 'real world' float (not a tiny denormal / huge)?"""
    try:
        x = struct.unpack('<f', v)[0]
    except Exception:
        return False
    if x != x or x in (float('inf'), float('-inf')):
        return False
    if x == 0.0:
        return True
    return 1e-6 < abs(x) < 1e7


def classify(vals):
    """Given the list of dword-bytes seen at one offset, guess a type."""
    ints = [struct.unpack('<I', v)[0] for v in vals]
    nz = [i for i in ints if i != 0]
    if not nz:
        return 'zero', {}
    fl = [struct.unpack('<f', v)[0] for v in vals]
    n_float = sum(1 for v in vals if looks_float(v))
    frac = n_float / len(vals)
    stats = {
        'nonzero': len(nz), 'total': len(ints),
        'distinct': len(set(ints)),
        'imin': min(ints), 'imax': max(ints),
        'fmin': min(fl), 'fmax': max(fl),
        'float_frac': frac,
    }
    # small ints -> id/enum/count
    if max(ints) < 0x10000 and frac < 0.99:
        return 'int', stats
    if frac > 0.98:
        return 'float', stats
    return 'mixed', stats


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ext')
    ap.add_argument('--assets', default='assets')
    ap.add_argument('--max-off', type=int, default=None)
    args = ap.parse_args()

    ext = args.ext.upper()
    recs = [r for _, _, r in records(args.assets, ext)]
    if not recs:
        print('no records for .' + ext)
        return
    stride = len(recs[0])
    print('# .%s  files-scanned records=%d stride=%d (0x%x)' %
          (ext, len(recs), stride, stride))

    # byte-level nonzero map
    nzbyte = [0] * stride
    for r in recs:
        for o in range(stride):
            if r[o]:
                nzbyte[o] += 1
    print('\n## byte non-zero map (. = always zero, # = sometimes non-zero)')
    for base in range(0, stride, 64):
        row = ''.join('#' if nzbyte[o] else '.'
                      for o in range(base, min(base + 64, stride)))
        print('  +0x%03x  %s' % (base, row))

    print('\n## dword classification')
    print('  %-8s %-6s %-10s %-9s %s' % ('off', 'type', 'nonzero', 'distinct', 'range'))
    maxo = args.max_off or stride
    for o in range(0, min(maxo, stride - 3), 4):
        vals = [r[o:o + 4] for r in recs]
        t, s = classify(vals)
        if t == 'zero':
            print('  +0x%03x   zero' % o)
            continue
        if t == 'float':
            rng = 'f[%.4g .. %.4g]' % (s['fmin'], s['fmax'])
        else:
            rng = 'i[%d .. %d] (0x%x)' % (s['imin'], s['imax'], s['imax'])
        print('  +0x%03x   %-6s %4d/%-5d %-9d %s' %
              (o, t, s['nonzero'], s['total'], s['distinct'], rng))


if __name__ == '__main__':
    main()
