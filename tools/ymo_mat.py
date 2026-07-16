"""YMO material record profiler.

The disk material record is 0x2EC (748 B). The loader (FUN_005f31d0) copies
disk dwords 0..0x12 verbatim into a 100-byte (0x64) RENDER struct:

    rt[0]      = material index          (runtime only)
    rt[1..13]  = disk dwords 0..12       (rt +0x04 .. +0x34)
    rt[14]     = 0                       (runtime only, +0x38)
    rt[15]     = 1.0f                    (runtime only, +0x3c)
    rt[16..21] = disk dwords 13..18      (rt +0x40 .. +0x54)
    rt[22..24] = 0                       (runtime only)

and the on-disk tail is:
    +0x4C  char name[0x60]
    +0xAC  char texture[3][0xC0]     <- stride 0xC0, NOT 0x30: 0xAC+3*0xC0 = 0x2EC

The bucket chain in FUN_005f4420 @0x5f4780 reads, on the RENDER struct:
    +0x04 flags   (bits 0x4000, 0x8040, 0x10)
    +0x10 enum    (disk dword 3)   -- tested <=1 and !=0
    +0x20 float   (disk dword 7)   -- tested against 1.0f

Usage:  python tools/ymo_mat.py <assets_root> [--room S_1000]
"""
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

MAT_REC = 0x2EC
NDW = 0x13  # disk dwords 0..0x12 copied to the render struct


def cstr(b, o, n):
    s = b[o:o + n]
    z = s.find(b"\0")
    return s[:z if z >= 0 else len(s)].decode("latin1")


def materials(b):
    """Yield (index, name, tex[3], dwords[0x13], raw) for one YMO."""
    n_mat = struct.unpack_from("<I", b, 0x10)[0]
    base = 0xA3 * 4
    for i in range(n_mat):
        o = base + i * MAT_REC
        dw = struct.unpack_from("<%dI" % NDW, b, o)
        tex = [cstr(b, o + 0xAC + k * 0xC0, 0xC0) for k in range(3)]
        yield i, cstr(b, o + 0x4C, 0x60), tex, dw, b[o:o + MAT_REC]


def bucket(dw):
    """Reproduce FUN_005f4420 @0x5f4780 bucket assignment. dw = disk dwords."""
    flags = dw[0]
    enum = dw[3]
    alpha = struct.unpack("<f", struct.pack("<I", dw[7]))[0]
    if flags & 0x4000:
        return 0
    if flags & 0x8040:
        return 1
    if (flags & 0x10) and enum <= 1:
        return 3
    if alpha < 1.0 or enum != 0:
        return 2
    return 0


def main():
    root = Path(sys.argv[1])
    only = None
    if "--room" in sys.argv:
        only = sys.argv[sys.argv.index("--room") + 1]

    files = sorted({p.resolve() for p in root.rglob("*.[Yy][Mm][Oo]")})
    if only:
        files = [p for p in files if only.lower() in str(p).lower()]

    print(f"files: {len(files)}")

    # per-dword value profile
    vals = defaultdict(Counter)
    fvals = defaultdict(Counter)
    nmat = 0
    buckets = Counter()
    tex_slot_used = [0, 0, 0]
    trailing_nonzero = Counter()

    for p in files:
        b = p.read_bytes()
        for i, name, tex, dw, raw in materials(b):
            nmat += 1
            for k in range(NDW):
                vals[k][dw[k]] += 1
                f = struct.unpack("<f", struct.pack("<I", dw[k]))[0]
                fvals[k][round(f, 4)] += 1
            buckets[bucket(dw)] += 1
            for k in range(3):
                if tex[k]:
                    tex_slot_used[k] += 1
            # is anything between name-end and 0xAC, or after the 3 tex names, nonzero?
            gap = raw[0x4C + 0x60:0xAC]
            if any(gap):
                trailing_nonzero["name_gap"] += 1

    print(f"materials: {nmat}")
    print(f"texture slots used: slot0={tex_slot_used[0]} slot1={tex_slot_used[1]} slot2={tex_slot_used[2]}")
    print(f"bucket histogram (0=opaque..3): {dict(sorted(buckets.items()))}")
    print(f"gap nonzero: {dict(trailing_nonzero)}")

    print("\n=== per-dword profile (disk dwords 0..0x12) ===")
    for k in range(NDW):
        c = vals[k]
        distinct = len(c)
        top = c.most_common(6)
        fl = fvals[k].most_common(4)
        looks_float = all(
            (v == 0) or (0x30000000 < v < 0x50000000) or (0xB0000000 < v < 0xD0000000)
            for v in c
        )
        print(f"  d{k:<2} (+0x{k*4:02x}) distinct={distinct:<5} float?={'Y' if looks_float else 'n'}")
        print(f"        u32 top: {[(hex(v), n) for v, n in top]}")
        if looks_float:
            print(f"        f32 top: {fl}")


if __name__ == "__main__":
    main()
