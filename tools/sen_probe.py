"""Decode SEN records -- scene exit / warp trigger volumes.

From FUN_00433a10 (g_scene.cpp), each 0x25C record is used as:
    +0x104  vec3   \  three vectors, args to the RegionEntry ctor FUN_0042d8f0
    +0x110  vec3    >  (record-0x24, -0x18, -0xc relative to the +0x128 cursor)
    +0x11C  vec3   /
    +0x128  char[] destination scene name (string; looked up via FUN_004358b0)
    +0x22C  u64    (2 dwords)
    +0x234  u32
    +0x23C  int -> float, adjusted by DAT_006bdba0/DAT_006bdc74 then *DAT   (angle)
    +0x240  u32
    +0x244  int  -> bool  (local_4c = (x != 0))

So the record has a SOB-like transform header, then a destination + params.
Test what each vec3 is (position / size / spawn) and confirm +0x128 names a scene.
"""
import math
import struct
import sys
from collections import Counter
from pathlib import Path

STRIDE = 0x25C


def cstr(b, o, n):
    s = b[o:o + n].split(b"\0")[0].decode("cp932", "replace")
    return s.encode("ascii", "backslashreplace").decode("ascii")


root = Path(sys.argv[1])
files = sorted({p.resolve() for p in root.rglob("*.[Ss][Ee][Nn]")})

recs = 0
dest_ext = Counter()
v104, v110, v11c = [], [], []
angles = Counter()
flag244 = Counter()
edges = []          # (room, dest, pos, size)
name00 = Counter()

for p in files:
    b = p.read_bytes()
    magic, z, stride, count = struct.unpack_from("<4sIII", b, 0)
    if magic != b"SEN\0" or stride != STRIDE:
        continue
    room = p.stem
    for i in range(count):
        r = 16 + i * STRIDE
        recs += 1
        n0 = cstr(b, r + 0x000, 0x104)
        name00[n0.rsplit('.', 1)[-1] if '.' in n0 else ('(empty)' if not n0 else '(bare)')] += 1
        a = struct.unpack_from("<3f", b, r + 0x104)
        c = struct.unpack_from("<3f", b, r + 0x110)
        d = struct.unpack_from("<3f", b, r + 0x11C)
        dest = cstr(b, r + 0x128, 0x60)
        dest_ext[dest.rsplit('.', 1)[-1] if '.' in dest else ('(empty)' if not dest else 'bare')] += 1
        ang = struct.unpack_from("<i", b, r + 0x23C)[0]
        fl = struct.unpack_from("<i", b, r + 0x244)[0]
        angles[ang] += 1
        flag244[fl] += 1
        if all(map(math.isfinite, a + c + d)):
            v104.append(a); v110.append(c); v11c.append(d)
        if len(edges) < 12:
            edges.append((room, dest or n0, a, d, ang, fl))

print(f"files {len(files)}   records {recs}\n")
print("+0x000 first field:", dict(name00.most_common()))
print("+0x128 dest field  :", dict(dest_ext.most_common()))
print("+0x23C values      :", dict(angles.most_common(8)))
print("+0x244 flag values :", dict(flag244.most_common()))


def summ(label, vs):
    if not vs:
        print(f"  {label}: none"); return
    flat = [x for v in vs for x in v]
    ones = sum(1 for v in vs if all(abs(x - 1) < 1e-4 for x in v))
    zero = sum(1 for v in vs if all(abs(x) < 1e-6 for x in v))
    pos = sum(1 for v in vs if all(x >= -1e-6 for x in v))
    print(f"  {label}: |max|={max(abs(x) for x in flat):8.2f}  "
          f"all-ones={100*ones/len(vs):4.1f}%  all-zero={100*zero/len(vs):4.1f}%  "
          f"all-nonneg={100*pos/len(vs):5.1f}%")


print("\nthree vec3s:")
summ("+0x104", v104)
summ("+0x110", v110)
summ("+0x11C", v11c)

print("\nsample edges (room -> dest):")
for room, dest, pos, sz, ang, fl in edges:
    print(f"  {room:8s} -> {dest:24s} at({pos[0]:7.2f},{pos[1]:6.2f},{pos[2]:7.2f})"
          f"  v11c({sz[0]:6.2f},{sz[1]:6.2f},{sz[2]:6.2f})  a={ang:4d} f={fl}")
