#!/usr/bin/env python3
"""Corpus-wide falsification tests for the SEF / SFO / SCM / SCT / SEV layouts.

Every claim in docs/formats/SIDECARS.md that can be checked against bytes is
checked here. Run from the repo root:  python tools/sidecar_validate.py
"""
import glob
import struct
import sys
from collections import Counter

ASSETS = 'assets/MAP/*/*/'


def recs(ext, stride):
    for f in sorted(glob.glob(ASSETS + '*.' + ext)):
        d = open(f, 'rb').read()
        magic, zero, s, cnt = struct.unpack('<4sIII', d[:16])
        assert s == stride, '%s stride %d != %d' % (f, s, stride)
        assert len(d) == 16 + s * cnt, '%s size mismatch' % f
        for i in range(cnt):
            yield f, i, d[16 + i * stride:16 + (i + 1) * stride]


def pct(a, b):
    return '%d/%d (%.1f%%)' % (a, b, 100.0 * a / b if b else 0)


def main():
    print('=' * 68)
    print('SEF  - scene effects   (id, position, rotation-degrees)')
    print('=' * 68)
    tot = nzrot = whole = idok = 0
    tail_nz = 0
    ids = Counter()
    for f, i, r in recs('SEF', 92):
        tot += 1
        eid = struct.unpack('<I', r[:4])[0]
        ids[eid] += 1
        rot = struct.unpack('<3f', r[0x10:0x1C])
        if rot != (0.0, 0.0, 0.0):
            nzrot += 1
            if all(abs(v - round(v)) < 1e-3 for v in rot):
                whole += 1
        if any(r[0x24:]):
            tail_nz += 1
    print('records                        :', tot)
    print('distinct effect ids            :', len(ids))
    print('records with nonzero rotation  :', pct(nzrot, tot))
    print('  ...of those, WHOLE degrees   :', pct(whole, nzrot))
    print('bytes +0x24..+0x5B ever nonzero:', pct(tail_nz, tot), '<- unused tail')

    print()
    print('=' * 68)
    print('SFO  - scene fog       (aabb, near, far, rgb)')
    print('=' * 68)
    tot = aabb = nf = alpha = 0
    for f, i, r in recs('SFO', 64):
        tot += 1
        mn = struct.unpack('<3f', r[0x0C:0x18])
        mx = struct.unpack('<3f', r[0x18:0x24])
        near, far = struct.unpack('<2f', r[0x24:0x2C])
        col = struct.unpack('<I', r[0x2C:0x30])[0]
        if all(a <= b for a, b in zip(mn, mx)):
            aabb += 1
        if near < far:
            nf += 1
        if (col >> 24) == 0xFF:
            alpha += 1
    print('records                        :', tot)
    print('+0x0c <= +0x18 componentwise   :', pct(aabb, tot))
    print('+0x24 < +0x28  (near < far)    :', pct(nf, tot))
    print('color byte +0x2f == 0xff       :', pct(alpha, tot))

    print()
    print('=' * 68)
    print('SEV / SEN  - trigger volumes (identical 604-byte layout)')
    print('=' * 68)
    for ext in ('SEN', 'SEV'):
        tot = mxp = mnn = named = 0
        for f, i, r in recs(ext, 604):
            tot += 1
            mx = struct.unpack('<3f', r[0x110:0x11C])
            mn = struct.unpack('<3f', r[0x11C:0x128])
            if all(v >= 0 for v in mx):
                mxp += 1
            if all(v <= 0 for v in mn):
                mnn += 1
            s = r[0:].split(b'\0')[0]
            if s and all(32 <= c < 127 for c in s):
                named += 1
        print('.%s records %-4d  name@+0x00 %-16s max@+0x110>=0 %-16s min@+0x11c<=0 %s'
              % (ext, tot, pct(named, tot), pct(mxp, tot), pct(mnn, tot)))

    print()
    print('=' * 68)
    print('EFTLIST - effect id -> asset name')
    print('=' * 68)
    sys.path.insert(0, 'tools')
    from eftlist_parse import parse
    tbl = {}
    coll = 0
    for fn in ('EFTLIST.DAT.Z', 'EFTLIST2.DAT.Z', 'EFTLIST3.DAT.Z'):
        _, _, rs = parse('build/sidecar_re/eft/' + fn)
        for i, rid, name, _r in rs:
            if not name:
                continue
            if rid in tbl and tbl[rid] != name:
                coll += 1
            tbl[rid] = name
    print('named effects across 3 lists   :', len(tbl))
    print('id collisions between lists    :', coll)
    unresolved = [k for k in ids if k not in tbl]
    print('SEF ids resolved to an asset   :', pct(len(ids) - len(unresolved), len(ids)))
    if unresolved:
        print('  UNRESOLVED:', unresolved)


if __name__ == '__main__':
    main()
