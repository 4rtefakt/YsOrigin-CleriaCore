"""Decode SOB object-placement records.

Container (verified 197/197): magic "SOB\0" / u32 zero / u32 stride=0x2A8 / u32
count / record[count].

From FUN_00433110, the loader touches these record offsets:
    +0x000  passed as a pointer     -> the ymo path (char[0x104] == MAX_PATH)
    +0x104  passed as a pointer  \
    +0x110  passed as a pointer   >- three vec3s, 12 bytes apart
    +0x11C  passed as a pointer  /
    +0x128  strncpy(.., 0x20)       -> name
    +0x1A8  strncpy(.., 0x20)       -> name
    +0x1D4  strncpy(.., 0x20)       -> name
    ... scalars at 0x14C..0x16C, 0x1C8..0x1D0, 0x1F4, 0x1F8, 0x2A4

Hypotheses tested here:
    S1  +0x000 is always a "...ymo" path
    S2  +0x11C is a SCALE   (clusters at 1,1,1)
    S3  +0x104 is a POSITION (metres, wide spread)
    S4  +0x110 is a ROTATION (radians |v| <= 2pi, or degrees)
"""
import math
import struct
import sys
from collections import Counter
from pathlib import Path

STRIDE = 0x2A8


def cstr(b, o, n):
    s = b[o:o + n].split(b"\0")[0].decode("cp932", "replace")
    # Windows consoles are cp1252; keep CP932 names printable.
    return s.encode("ascii", "backslashreplace").decode("ascii")


root = Path(sys.argv[1])
files = sorted({p.resolve() for p in root.rglob("*.[Ss][Oo][Bb]")})

paths = Counter()
ext = Counter()
recs = 0
v104, v110, v11c = [], [], []
samples = []

for p in files:
    b = p.read_bytes()
    magic, z, stride, count = struct.unpack_from("<4sIII", b, 0)
    assert magic == b"SOB\0" and stride == STRIDE, p.name
    for i in range(count):
        r = 16 + i * STRIDE
        recs += 1
        path = cstr(b, r + 0x000, 0x104)
        paths[path] += 1
        ext[path.rsplit(".", 1)[-1].lower() if "." in path else "(none)"] += 1
        a = struct.unpack_from("<3f", b, r + 0x104)
        c = struct.unpack_from("<3f", b, r + 0x110)
        d = struct.unpack_from("<3f", b, r + 0x11C)
        if all(map(math.isfinite, a + c + d)):
            v104.append(a); v110.append(c); v11c.append(d)
        if len(samples) < 6 and count > 1:
            samples.append((p.name, path, a, c, d,
                            cstr(b, r + 0x128, 0x20),
                            cstr(b, r + 0x1A8, 0x20),
                            cstr(b, r + 0x1D4, 0x20)))

print(f"files {len(files)}   records {recs}\n")
print("S1 -- +0x000 extension:", dict(ext.most_common(5)))
print(f"     distinct paths: {len(paths)}")


def summarize(label, vs):
    flat = [x for v in vs for x in v]
    ones = sum(1 for v in vs if all(abs(x - 1.0) < 1e-4 for x in v))
    zeros = sum(1 for v in vs if all(abs(x) < 1e-6 for x in v))
    mx = max(abs(x) for x in flat)
    within2pi = sum(1 for v in vs if all(abs(x) <= 6.2832 for x in v))
    print(f"  {label}: n={len(vs)}  |max|={mx:9.2f}   "
          f"all-ones={100*ones/len(vs):5.1f}%   all-zero={100*zeros/len(vs):5.1f}%   "
          f"all|x|<=2pi={100*within2pi/len(vs):5.1f}%")


print("\nS2/S3/S4 -- the three vec3s:")
summarize("+0x104", v104)
summarize("+0x110", v110)
summarize("+0x11C", v11c)

print("\nsamples:")
for fn, path, a, c, d, n1, n2, n3 in samples:
    print(f"  {fn}: {path}")
    print(f"      +0x104 ({a[0]:9.3f},{a[1]:9.3f},{a[2]:9.3f})"
          f"  +0x110 ({c[0]:8.3f},{c[1]:8.3f},{c[2]:8.3f})"
          f"  +0x11C ({d[0]:6.3f},{d[1]:6.3f},{d[2]:6.3f})")
    print(f"      names: {n1!r} {n2!r} {n3!r}")
