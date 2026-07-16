"""Ground-truth room geometry totals, for differential-testing cleria-dump.

For every <room>.SOB, resolve each referenced YMO and sum its vertices/triangles.
cleria-dump must reproduce these numbers exactly.
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ymo_parse import parse_mesh  # noqa: E402


def ymo_totals(b):
    n_mat, n_s5, n_mesh, n_node = struct.unpack_from("<4I", b, 0x10)
    o = 0xA3 * 4 + n_mat * 0xBB * 4 + n_node * 0x2A0
    v = t = 0
    for _ in range(n_mesh):
        o, vbs, ibs = parse_mesh(b, o)
        v += sum(c for c, _, _ in vbs)
        t += sum(c // 3 for c, _ in ibs)
    return v, t


root = Path(sys.argv[1])
# case-insensitive lookup table of every extracted YMO by archive-style path
ymos = {}
for p in root.rglob("*.[Yy][Mm][Oo]"):
    rel = str(p.relative_to(root)).replace("\\", "/").lower()
    ymos[rel] = p

tot_v = tot_t = 0
rooms = 0
miss = 0
for sob in sorted(root.rglob("*.[Ss][Oo][Bb]")):
    b = sob.read_bytes()
    if b[:4] != b"SOB\0":
        continue
    rooms += 1
    count = struct.unpack_from("<I", b, 12)[0]
    for i in range(count):
        r = 16 + i * 0x2A8
        path = b[r:r + 0x104].split(b"\0")[0].decode("cp932", "replace")
        key = path.replace("\\", "/").lower()
        if key.startswith("data/"):
            key = key[5:]
        f = ymos.get(key)
        if not f:
            miss += 1
            continue
        v, t = ymo_totals(f.read_bytes())
        tot_v += v
        tot_t += t

print(f"rooms {rooms}   missing-refs {miss}")
print(f"TOTAL vertices={tot_v}  triangles={tot_t}")
