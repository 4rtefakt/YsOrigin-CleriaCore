"""Dump the Ys Origin shader-name table from yso_win.exe .data.

Actual entry layout (12 bytes), verified against the raw .data bytes:
    +0  char* name      (-> .rdata ascii, lowercase shader name)
    +4  u32   kind
    +8  u32   zero
NOTE: the field order is (name, kind, 0) -- NOT (0, name, kind).

The table is located by scanning outward from a known-good entry
(b_ndst1_water @ 0x7463DC) until entries stop validating.

    python tools/dump_shader_table.py [path-to-yso_win.exe]
"""
import os
import struct, sys
from collections import Counter, defaultdict

SEED_VA = 0x7463DC          # name field of the b_ndst1_water entry


def load(path):
    d = open(path, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    optsz = struct.unpack_from("<H", d, pe + 20)[0]
    base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
    secs, off = [], pe + 24 + optsz
    for _ in range(nsec):
        vs, va, rs, ra = struct.unpack_from("<IIII", d, off + 8)
        secs.append((va, vs, ra, rs))
        off += 40
    def v2f(v):
        r = v - base
        for va, vs, ra, rs in secs:
            if va <= r < va + max(vs, rs):
                o = ra + (r - va)
                if o < len(d): return o
        return None
    return d, v2f


def cstr(d, off, maxlen=48):
    if off is None or off >= len(d): return None
    e = d.find(b"\0", off)
    if e < 0 or e - off > maxlen or e == off: return None
    s = d[off:e]
    return s.decode("ascii") if all(0x20 <= c < 0x7F for c in s) else None


def entry(d, v2f, va):
    o = v2f(va)
    if o is None or o + 12 > len(d): return None
    p, kind, z = struct.unpack_from("<III", d, o)
    name = cstr(d, v2f(p))
    if name is None or z != 0 or kind > 64: return None
    return name, kind


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else \
        os.environ.get("YSO_EXE", "D:/SteamLibrary/steamapps/common/Ys Origin/yso_win.exe")
    d, v2f = load(path)
    assert entry(d, v2f, SEED_VA), "seed entry did not validate"

    start = SEED_VA
    while entry(d, v2f, start - 12):
        start -= 12
    end = SEED_VA
    while entry(d, v2f, end + 12):
        end += 12

    rows = []
    va = start
    while va <= end:
        n, k = entry(d, v2f, va)
        rows.append((va, n, k))
        va += 12

    print("shader-name table  %08X .. %08X   (%d entries, 12-byte stride)"
          % (start, end + 11, len(rows)))
    print("entry layout: +0 char* name, +4 u32 kind, +8 u32 zero\n")
    print("idx  VA        kind  name")
    for i, (va, n, k) in enumerate(rows):
        print("%3d  %08X  %4d  %s" % (i, va, k, n))

    print("\nkind histogram: %s" % dict(sorted(Counter(k for _, _, k in rows).items())))
    byname = defaultdict(list)
    for _, n, k in rows: byname[n].append(k)
    dup = {n: v for n, v in byname.items() if len(v) > 1}
    print("duplicate names: %s" % (dup if dup else "(none)"))
    print("\nnames grouped by kind:")
    bykind = defaultdict(list)
    for _, n, k in rows: bykind[k].append(n)
    for k in sorted(bykind):
        print("  kind %d (%d): %s" % (k, len(bykind[k]), ", ".join(bykind[k])))


main()
