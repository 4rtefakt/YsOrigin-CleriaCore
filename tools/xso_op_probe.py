#!/usr/bin/env python3
"""xso_op_probe.py - validate recovered class-2 sub-op semantics against the XSO corpus.

Research tool for the sub-op RE pass (build/xso_ops/). For each sub-op of interest
it reports the observed operand-count distribution and the value distribution of
each operand slot, so a decoded signature from the disassembly can be checked
against what the shipped scripts actually encode.

Usage:
  python tools/xso_op_probe.py <asset_root> [--ops 5c,5d,61,...] [--sites SUB]
"""
import argparse, os, struct, sys
from collections import Counter, defaultdict


def parse(path):
    """Return (code words, string pool) or None if not an XSR container."""
    with open(path, 'rb') as f:
        d = f.read()
    if len(d) < 0x24 or d[:4] != b'XSR\0':
        return None
    n = struct.unpack_from('<I', d, 0x1c)[0]
    if 0x24 + n * 4 > len(d):
        return None
    code = list(struct.unpack_from('<%di' % n, d, 0x24))
    # string pool
    scount = struct.unpack_from('<I', d, 0x20)[0]
    strs = []
    ce = 0x24 + n * 4
    try:
        offs = struct.unpack_from('<%dI' % scount, d, ce)
        base = ce + scount * 4
        for o in offs:
            e = d.index(b'\0', base + o)
            strs.append(d[base + o:e].decode('latin-1'))
    except Exception:
        strs = []
    return code, strs


def walk(code):
    """Yield (pc, cls, sub, operands) for every class-2 call; linear decode."""
    pc = 0
    n = len(code)
    while pc < n:
        w = code[pc]
        cls = (w >> 24) & 0xFF
        if cls == 2:
            sub = (w >> 12) & 0xFFF
            skip = (w >> 8) & 0xF
            cnt = w & 0xFF
            ops = code[pc + 1 + skip: pc + 1 + skip + cnt]
            yield pc, cls, sub, ops
            pc += 1 + skip + cnt
        elif cls in (0, 1, 0xC, 0xD):
            pc += 1
        elif cls <= 0x13:
            pc += 2
        else:
            pc += 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('root')
    ap.add_argument('--ops', default='5c,5d,5f,60,61,62,63,81,90,9a,131,139,13d,13f')
    ap.add_argument('--sites', default=None, help='hex sub-op: list call sites w/ file')
    a = ap.parse_args()
    want = set(int(x, 16) for x in a.ops.split(','))

    argc = defaultdict(Counter)                    # sub -> Counter(operand count)
    slot = defaultdict(lambda: defaultdict(Counter))  # sub -> slot -> Counter(value)
    files = defaultdict(Counter)
    sites = []
    nfiles = 0
    for dp, _, fns in os.walk(a.root):
        for fn in fns:
            if not fn.upper().endswith('.XSO'):
                continue
            p = os.path.join(dp, fn)
            r = parse(p)
            if not r:
                continue
            code, strs = r
            nfiles += 1
            for pc, cls, sub, ops in walk(code):
                if sub not in want:
                    continue
                argc[sub][len(ops)] += 1
                for i, v in enumerate(ops):
                    slot[sub][i][v] += 1
                files[sub][fn] += 1
                if a.sites and sub == int(a.sites, 16):
                    sites.append((p, pc, ops, strs))

    print('scanned %d XSO files\n' % nfiles)
    for sub in sorted(want):
        tot = sum(argc[sub].values())
        if not tot:
            print('sub 0x%x : (no call sites)\n' % sub)
            continue
        print('=== sub 0x%x : %d call sites in %d files ===' % (sub, tot, len(files[sub])))
        print('  operand counts: %s' % dict(argc[sub]))
        for i in sorted(slot[sub]):
            c = slot[sub][i]
            top = c.most_common(12)
            rng = '[%d..%d]' % (min(c), max(c))
            print('    op%d  distinct=%-4d range=%-16s top=%s'
                  % (i, len(c), rng, ', '.join('%d x%d' % (v, n) for v, n in top)))
        print('  top files: %s' % ', '.join('%s x%d' % (f, n) for f, n in files[sub].most_common(6)))
        print()

    if a.sites:
        print('==== call sites for sub 0x%s ====' % a.sites)
        for p, pc, ops, strs in sites[:80]:
            s = ''
            print('  %-58s pc=%-4d ops=%s %s' % (os.path.relpath(p, a.root), pc, ops, s))


if __name__ == '__main__':
    main()
