"""Build the Ys Origin room graph from SEN warp triggers and validate it."""
import re
import struct
import sys
from collections import defaultdict
from pathlib import Path

files = sorted({q.resolve() for q in Path(sys.argv[1]).rglob("*.[Ss][Ee][Nn]")})


def leaf(path):
    """'S_10\\S_1000\\' -> 'S_1000' (last non-empty path component)."""
    parts = [p for p in re.split(r"[\\/]", path) if p]
    return parts[-1].upper() if parts else ""


edges = defaultdict(set)
rooms = set()
for p in files:
    b = p.read_bytes()
    magic, _, st, cnt = struct.unpack_from("<4sIII", b, 0)
    if magic != b"SEN\0":
        continue
    src = p.stem.upper()
    rooms.add(src)
    for i in range(cnt):
        r = 16 + i * 0x25C
        dest = b[r + 0x128:r + 0x128 + 0x60].split(b"\0")[0].decode("cp932", "replace")
        d = leaf(dest)
        if d:
            edges[src].add(d)

tot = sum(len(v) for v in edges.values())
recip = 0
oneway = []
for a, vs in edges.items():
    for bb in vs:
        if a in edges.get(bb, ()):
            recip += 1
        elif len(oneway) < 8:
            oneway.append((a, bb))

seen, stack = set(), ["S_0000"]
while stack:
    x = stack.pop()
    if x in seen:
        continue
    seen.add(x)
    stack += list(edges.get(x, ()))

print(f"rooms with a .SEN          : {len(rooms)}")
print(f"rooms with >=1 exit        : {len(edges)}")
print(f"total directed edges       : {tot}")
print(f"reciprocal (A->B & B->A)   : {recip}/{tot} ({100*recip/tot:.1f}%)")
print(f"reachable from S_0000      : {len(seen)}")
print(f"dest rooms with no .SEN    : "
      f"{sorted({d for vs in edges.values() for d in vs} - rooms)[:12]}")
print(f"one-way samples            : {oneway[:6]}")
print(f"S_0000 exits -> {sorted(edges['S_0000'])}")
print(f"S_1000 exits -> {sorted(edges['S_1000'])}")
