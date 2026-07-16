"""Validate the YMO vertex-buffer decode against the raw corpus.

From FUN_005f4420, a VB descriptor is 20 bytes:
    +0x00 ptr (runtime)   +0x04 ptr (runtime)
    +0x08 vertex_count    +0x0C stride    +0x10 FVF

If that's right, then scanning for 20-byte windows where
    stride == fvf_stride(FVF)
should light up only on real descriptors -- an independent check that never
touches the loader. Also cross-checks the specular offset the loader uses
when it halves specular (0x10 for FVF 0x1c2, 0x1c for 0x1d2/0x2d2).
"""
import struct
import sys
from collections import Counter
from pathlib import Path

# D3D9 FVF bits
XYZ, XYZRHW = 0x002, 0x004
NORMAL, PSIZE, DIFFUSE, SPECULAR = 0x010, 0x020, 0x040, 0x080
POSMASK, TEXMASK = 0x00E, 0xF00


def fvf_layout(f):
    """-> (stride, {field: offset}) or None if the code is nonsense."""
    off, lay = 0, {}
    pos = f & POSMASK
    if pos == XYZ:
        lay["POSITION"] = off; off += 12
    elif pos == XYZRHW:
        lay["POSITIONT"] = off; off += 16
    elif pos in (0x006, 0x008, 0x00A, 0x00C, 0x00E):   # XYZB1..XYZB5
        nb = (pos - 0x004) // 2
        lay["POSITION"] = off; off += 12
        lay[f"BLEND{nb}"] = off; off += 4 * nb
    else:
        return None
    if f & NORMAL:
        lay["NORMAL"] = off; off += 12
    if f & PSIZE:
        lay["PSIZE"] = off; off += 4
    if f & DIFFUSE:
        lay["DIFFUSE"] = off; off += 4
    if f & SPECULAR:
        lay["SPECULAR"] = off; off += 4
    ntex = (f & TEXMASK) >> 8
    if ntex > 8:
        return None
    for i in range(ntex):
        lay[f"TEX{i}"] = off; off += 8      # default 2 floats
    return off, lay


root = Path(sys.argv[1])
files = sorted({p.resolve() for p in root.rglob("*.[Yy][Mm][Oo]")})

fvfs = Counter()
files_hit = Counter()
verts = Counter()
total = 0

for p in files:
    b = p.read_bytes()
    seen = set()
    for o in range(0, len(b) - 20, 4):
        cnt, stride, fvf = struct.unpack_from("<3I", b, o + 8)
        if not (0 < cnt < 200000 and 0 < stride <= 128 and 0 < fvf < 0x10000):
            continue
        lay = fvf_layout(fvf)
        if not lay or lay[0] != stride:
            continue
        # require the payload to actually fit in the file
        if o + 20 + cnt * stride > len(b):
            continue
        fvfs[fvf] += 1
        verts[fvf] += cnt
        seen.add(fvf)
        total += 1
    for f in seen:
        files_hit[f] += 1

print(f"corpus: {len(files)} YMO files")
print(f"self-consistent VB descriptors found: {total}\n")
print("%-8s %8s %8s %10s   %-7s %s" %
      ("FVF", "DESCS", "FILES", "VERTS", "STRIDE", "LAYOUT"))
print("-" * 96)
for f, c in fvfs.most_common(12):
    stride, lay = fvf_layout(f)
    txt = " ".join(f"{k}@{v}" for k, v in lay.items())
    print("%-8s %8d %8d %10d   %-7d %s" %
          (hex(f), c, files_hit[f], verts[f], stride, txt))

print("\ncross-check vs loader's specular-halving offsets:")
for f, want in ((0x1c2, 0x10), (0x1d2, 0x1c), (0x2d2, 0x1c)):
    r = fvf_layout(f)
    got = r[1].get("SPECULAR") if r else None
    print(f"  FVF {f:#05x}: loader uses {want:#04x}, decode says "
          f"{got:#04x} -> {'MATCH' if got == want else 'MISMATCH'}")
