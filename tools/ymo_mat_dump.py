"""Dump per-material detail for one or more YMO files, with the loader's bucket.

Usage: python tools/ymo_mat_dump.py <file.ymo> [file2.ymo ...]
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from ymo_mat import materials, bucket


def f(u):
    return struct.unpack("<f", struct.pack("<I", u))[0]


for arg in sys.argv[1:]:
    p = Path(arg)
    b = p.read_bytes()
    print(f"\n########## {p.name} ##########")
    print(f"{'#':>3} {'name':<14} {'flags':>10} {'d1':>3} {'d2':>3} {'enum':>4} "
          f"{'diffuse rgba':<26} {'pow':>6} {'d12':>5} {'d13':>6} {'scrollU':>10} {'scrollV':>10} {'bkt':>3}  textures")
    for i, name, tex, dw, raw in materials(b):
        rgba = f"({f(dw[4]):.3f},{f(dw[5]):.3f},{f(dw[6]):.3f},{f(dw[7]):.3f})"
        t = " | ".join(x for x in tex if x)
        print(f"{i:>3} {name:<14} {dw[0]:#10x} {dw[1]:>3} {dw[2]:>3} {dw[3]:>4} "
              f"{rgba:<26} {f(dw[11]):>6.1f} {f(dw[12]):>5.2f} {dw[13]:>6} "
              f"{f(dw[17]):>10.5f} {f(dw[18]):>10.5f} {bucket(dw):>3}  {t}")
