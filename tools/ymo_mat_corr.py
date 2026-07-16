"""Correlate material flag bits / dwords against artist naming conventions.

The artists named materials `_add`, `_mul`, `_mult`, `scroll`, etc. Those names
are independent ground truth for what the flags must mean. Also tests whether
d1/d2 == 2 identifies collision (`*_.YMO`) materials.

Usage: python tools/ymo_mat_corr.py <assets_root>
"""
import re
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from ymo_mat import materials

root = Path(sys.argv[1])
files = sorted({p.resolve() for p in root.rglob("*.[Yy][Mm][Oo]")})

rows = []
for p in files:
    b = p.read_bytes()
    for i, name, tex, dw, raw in materials(b):
        rows.append((p, p.stem.endswith("_"), name.lower(), tex, dw))

print(f"materials: {len(rows)}\n")


def bits(v):
    return {k for k in range(32) if v & (1 << k)}


def corr(label, pred):
    """For a predicate over rows, show which flag bits it implies / is implied by."""
    sel = [r for r in rows if pred(r)]
    if not sel:
        print(f"{label}: 0 rows"); return
    print(f"=== {label}: {len(sel)} materials ===")
    bc = Counter()
    for r in sel:
        for k in bits(r[4][0]):
            bc[k] += 1
    tot_bit = Counter()
    for r in rows:
        for k in bits(r[4][0]):
            tot_bit[k] += 1
    for k in sorted(bc):
        # precision: of all materials with this bit, how many are in sel
        print(f"   bit {k:>2} (0x{1<<k:08x}): {bc[k]:>4}/{len(sel):<4} of sel "
              f"| this bit total {tot_bit[k]:>4} -> precision {100*bc[k]/tot_bit[k]:5.1f}%")
    print(f"   d1 : {Counter(r[4][1] for r in sel).most_common()}")
    print(f"   d2 : {Counter(r[4][2] for r in sel).most_common()}")
    print(f"   d3 : {Counter(r[4][3] for r in sel).most_common()}")
    print()


corr("name contains 'add'", lambda r: re.search(r"(^|[_0-9])add", r[2]))
corr("name contains 'mul'", lambda r: re.search(r"(^|[_0-9])mul", r[2]))
corr("collision  (*_.YMO)", lambda r: r[1])
corr("NOT collision", lambda r: not r[1])
corr("no texture in slot0", lambda r: not r[3][0])
corr("slot2 texture present", lambda r: bool(r[3][2]))

# d1/d2 vs collision -- direct contingency
print("=== d1 value vs collision file ===")
t = Counter((r[1], r[4][1]) for r in rows)
for k in sorted(t, key=lambda x: (x[0], x[1])):
    print(f"   collision={k[0]!s:<5} d1={k[1]} : {t[k]}")
print("=== d2 value vs collision file ===")
t = Counter((r[1], r[4][2]) for r in rows)
for k in sorted(t, key=lambda x: (x[0], x[1])):
    print(f"   collision={k[0]!s:<5} d2={k[1]} : {t[k]}")

# bit4 vs bit19 correlation (both counted 530)
print("\n=== bit4 (0x10) vs bit19 (0x80000) contingency ===")
t = Counter((bool(r[4][0] & 0x10), bool(r[4][0] & 0x80000)) for r in rows)
for k in sorted(t):
    print(f"   bit4={k[0]!s:<5} bit19={k[1]!s:<5} : {t[k]}")
