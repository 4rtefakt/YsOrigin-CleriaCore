"""Hypothesis tests over the material corpus.

H1: flags bit 0x1000 == "slot-0 texture present"
H2: `*_.YMO` files are collision/surface-type meshes, not renderable geometry
    (untextured, material names are surface types)
H3: d17/d18 (disk +0x44/+0x48) are UV scroll velocities -- nonzero only on
    materials whose name/texture implies animation

Usage: python tools/ymo_mat_flags.py <assets_root>
"""
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from ymo_mat import materials


def f(u):
    return struct.unpack("<f", struct.pack("<I", u))[0]


root = Path(sys.argv[1])
files = sorted({p.resolve() for p in root.rglob("*.[Yy][Mm][Oo]")})

# --- H1: bit 0x1000 vs texture present
tab = Counter()
# --- H2
under_names = Counter(); under_tex = Counter(); under_files = 0
plain_names = Counter(); plain_tex = Counter(); plain_files = 0
# --- H3
scroll_mats = []
allbits = Counter()

for p in files:
    b = p.read_bytes()
    is_under = p.stem.endswith("_")
    if is_under: under_files += 1
    else: plain_files += 1
    for i, name, tex, dw, raw in materials(b):
        has_tex = bool(tex[0])
        bit = bool(dw[0] & 0x1000)
        tab[(bit, has_tex)] += 1
        for k in range(32):
            if dw[0] & (1 << k): allbits[k] += 1
        if is_under:
            under_names[name.lower()] += 1
            under_tex[has_tex] += 1
        else:
            plain_names[name.lower()] += 1
            plain_tex[has_tex] += 1
        su, sv = f(dw[17]), f(dw[18])
        if su or sv:
            scroll_mats.append((p.name, name, tex[0], su, sv, dw[0], f(dw[7])))

print("=== H1: flags bit 0x1000  vs  slot-0 texture present ===")
print(f"  bit=1 tex=1 : {tab[(True,True)]}")
print(f"  bit=1 tex=0 : {tab[(True,False)]}")
print(f"  bit=0 tex=1 : {tab[(False,True)]}")
print(f"  bit=0 tex=0 : {tab[(False,False)]}")
tot = sum(tab.values())
agree = tab[(True, True)] + tab[(False, False)]
print(f"  agreement   : {agree}/{tot} = {100*agree/tot:.2f}%")

print("\n=== flags bit population (all materials) ===")
for k in sorted(allbits):
    print(f"  bit {k:>2} (0x{1<<k:08x}) : {allbits[k]}")

print(f"\n=== H2: '*_.YMO' ({under_files} files) vs plain ({plain_files} files) ===")
print(f"  *_ : materials with texture: {under_tex[True]}, without: {under_tex[False]}")
print(f"  plain: materials with texture: {plain_tex[True]}, without: {plain_tex[False]}")
print(f"  *_ material names (top 25): {under_names.most_common(25)}")
print(f"  distinct *_ names: {len(under_names)}")

print(f"\n=== H3: materials with nonzero d17/d18 ({len(scroll_mats)}) ===")
for r in scroll_mats[:40]:
    print(f"  {r[0]:<22} {r[1]:<14} tex={r[2]:<18} u={r[3]:>9.5f} v={r[4]:>9.5f} flags={r[5]:#x} a={r[6]:.2f}")
