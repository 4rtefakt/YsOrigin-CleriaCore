#!/usr/bin/env python3
"""xso_spawns.py - decode event-VM sub-op 0x12c (ACTOR SPAWN) call sites from XSO scripts.

Signature recovered from FUN_00570400 (yso_win.exe) [C-DIS]:
    0x12c(entity_id, cast_type, x, y, z, yaw_deg, group)
  entity_id : int   0 => engine auto-assigns via FUN_00434db0; 998 (0x3e6) => "self"
  cast_type : int   index into the runtime cast table @DAT_0074c11c (stride 0x200)
  x,y,z     : float spawn position
  yaw_deg   : float spawn yaw in degrees
  group     : int   0 = untagged; 1..4 => OR (group<<8) into entity+0x130 and set
                    the marker colour at entity+0x2b4. Only 0 and 1 occur in the
                    shipped corpus. Script op 0x131(n) later asks
                    "is any live entity tagged with (n<<8)?" -- the barrier poll.

Usage:
  python tools/xso_spawns.py <asset_root> [--group N] [--room S_1000]
"""
import argparse, os, struct, sys
from collections import Counter


def parse(path):
    with open(path, 'rb') as f:
        d = f.read()
    if len(d) < 0x24 or d[:4] != b'XSR\0':
        return None
    n = struct.unpack_from('<I', d, 0x1c)[0]
    if 0x24 + n * 4 > len(d):
        return None
    return list(struct.unpack_from('<%di' % n, d, 0x24))


def f32(i):
    return struct.unpack('<f', struct.pack('<i', i))[0]


def spawns(code):
    """Yield (pc, operands) for every class-2 sub-op 0x12c call site.

    Encoding per build/xso_ops (same linear decode as tools/xso_op_probe.py):
      word = [cls:8][sub:12][skip:4][cnt:8]; operands follow after `skip` words.
    """
    out = []
    pc, n = 0, len(code)
    while pc < n:
        w = code[pc]
        cls = (w >> 24) & 0xFF
        if cls == 2:
            sub = (w >> 12) & 0xFFF
            skip = (w >> 8) & 0xF
            cnt = w & 0xFF
            ops = code[pc + 1 + skip: pc + 1 + skip + cnt]
            if sub == 0x12c and len(ops) == 7:
                out.append((pc, ops))
            pc += 1 + skip + cnt
        elif cls in (0, 1, 0xC, 0xD):
            pc += 1
        elif cls <= 0x13:
            pc += 2
        else:
            pc += 1
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('root')
    ap.add_argument('--group', type=int, default=None)
    ap.add_argument('--room', default=None)
    a = ap.parse_args()

    rows = []
    for dirpath, _, files in os.walk(a.root):
        for fn in files:
            if not fn.upper().endswith('.XSO'):
                continue
            if a.room and a.room.upper() not in fn.upper():
                continue
            code = parse(os.path.join(dirpath, fn))
            if not code:
                continue
            for pc, ops in spawns(code):
                eid, cast, x, y, z, yaw, grp = ops
                if a.group is not None and grp != a.group:
                    continue
                rows.append((fn, pc, eid, cast, f32(x), f32(y), f32(z), f32(yaw), grp))

    print('%-28s %6s %5s %5s %10s %10s %10s %8s %5s'
          % ('file', 'pc', 'id', 'cast', 'x', 'y', 'z', 'yaw', 'grp'))
    for r in sorted(rows):
        print('%-28s %6d %5d %5d %10.2f %10.2f %10.2f %8.1f %5d' % r)
    print('\n%d spawn sites in %d files' % (len(rows), len(set(r[0] for r in rows))))
    if rows:
        print('group histogram:', dict(Counter(r[8] for r in rows)))


if __name__ == '__main__':
    main()
