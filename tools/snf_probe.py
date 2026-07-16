#!/usr/bin/env python3
"""Decode the .SNF sidecar (Ys Origin) and profile it corpus-wide.

Unlike its siblings, every .SNF file has container count == 0, so the declared
0x3C stride is never instantiated. What the file actually carries is a single
fixed 40-byte block at +0x10.

Usage:  python tools/snf_probe.py [--assets PATH] [--dump]
"""
import argparse
import glob
import math
import os
import struct
import sys
from collections import Counter


def d3dcolor(u):
    return "A%02X R%02X G%02X B%02X" % ((u >> 24) & 255, (u >> 16) & 255,
                                        (u >> 8) & 255, u & 255)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--assets', default='assets')
    ap.add_argument('--dump', action='store_true')
    a = ap.parse_args()

    files = sorted(glob.glob(os.path.join(a.assets, 'MAP', '**', '*.SNF'),
                             recursive=True))
    print("files: %d" % len(files))

    hdr_bad = []
    sizes = Counter()
    magics, f04, strides, counts = Counter(), Counter(), Counter(), Counter()
    dirs, norms = [], []
    c1c, c20, c24, tail, flag = Counter(), Counter(), Counter(), Counter(), Counter()
    rows = []

    for f in files:
        b = open(f, 'rb').read()
        sizes[len(b)] += 1
        magic, z, stride, count = struct.unpack_from('<4sIII', b, 0)
        magics[magic] += 1
        f04[z] += 1
        strides[stride] += 1
        counts[count] += 1
        if len(b) != 16 + 40 + stride * count:
            hdr_bad.append((f, len(b), stride, count))
        p = b[16:]
        dx, dy, dz = struct.unpack_from('<3f', p, 0)
        n = math.sqrt(dx * dx + dy * dy + dz * dz)
        dirs.append((dx, dy, dz))
        norms.append(n)
        u1c, u20, u24 = struct.unpack_from('<3I', p, 0x0C)
        c1c[u1c] += 1
        c20[u20] += 1
        c24[u24] += 1
        tail[p[0x18:0x24]] += 1
        fl = struct.unpack_from('<I', p, 0x24)[0]
        flag[fl] += 1
        rows.append((os.path.basename(f), dx, dy, dz, u1c, u20, u24, fl))

    print("sizes           : %s" % dict(sizes))
    print("magic           : %s" % {k: v for k, v in magics.items()})
    print("+0x04           : %s" % dict(f04))
    print("stride (+0x08)  : %s" % {hex(k): v for k, v in strides.items()})
    print("count  (+0x0C)  : %s" % dict(counts))
    print("container 16+40+stride*count holds: %d/%d" %
          (len(files) - len(hdr_bad), len(files)))

    print("\n-- payload+0x00 float3 --")
    off = [abs(n - 1.0) for n in norms]
    print("unit-length (|n-1|<1e-4): %d/%d   max dev %.3g" %
          (sum(1 for e in off if e < 1e-4), len(off), max(off)))
    print("all-zero vectors        : %d" % sum(1 for d in dirs if d == (0, 0, 0)))
    print("y<0 (points downward)   : %d/%d" %
          (sum(1 for d in dirs if d[1] < 0), len(dirs)))
    print("distinct vectors        : %d" % len(set(dirs)))

    for name, ctr in (("+0x0C", c1c), ("+0x10", c20), ("+0x14", c24)):
        print("\n-- payload%s as D3DCOLOR --" % name)
        for v, n in ctr.most_common():
            print("  %-3d  0x%08X  %s" % (n, v, d3dcolor(v)))

    print("\n-- payload+0x18..0x23 (12 B) --")
    for v, n in tail.most_common():
        print("  %-3d  %s" % (n, v.hex()))
    print("\n-- payload+0x24 u32 --")
    for v, n in flag.most_common():
        print("  %-3d  %d" % (n, v))

    if a.dump:
        print("\n%-14s %-24s %-10s %-10s %-10s %s" %
              ("file", "direction", "+0x0C", "+0x10", "+0x14", "+0x24"))
        for r in rows:
            print("%-14s (%7.4f,%7.4f,%7.4f)  %08X   %08X   %08X   %d" % r)


if __name__ == '__main__':
    main()
