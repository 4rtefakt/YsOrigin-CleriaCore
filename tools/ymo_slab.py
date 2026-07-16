"""Per-submesh diagnostic: material, flags, loader bucket, and the BAKED vertex
DIFFUSE statistics. Finds geometry that renders black under plain
`texture x diffuse` modulation.

Usage: python tools/ymo_slab.py <file.ymo>
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from ymo_mat import materials, bucket


def f(u):
    return struct.unpack("<f", struct.pack("<I", u))[0]


def parse(b):
    n_mat, n_s5, n_mesh, n_node = struct.unpack_from("<4I", b, 0x10)
    mats = list(materials(b))
    o = 0xA3 * 4 + n_mat * 0x2EC + n_node * 0x2A0
    meshes = []
    for _ in range(n_mesh):
        h = o
        name = b[h:h + 0x60].split(b"\0")[0].decode("latin1")
        n_sub = struct.unpack_from("<I", b, h + 0x88)[0]
        n_vb = struct.unpack_from("<I", b, h + 0x8C)[0]
        n_ib = struct.unpack_from("<I", b, h + 0x90)[0]
        n_idxlen = struct.unpack_from("<I", b, h + 0x94)[0]
        n_nb = struct.unpack_from("<I", b, h + 0x9C)[0]
        o += 0xB4 + n_nb * 0x60
        subs = []
        ss = 0x20 + n_idxlen * 4
        for s in range(n_sub):
            sr = o + s * ss
            subs.append(struct.unpack_from("<5I", b, sr))
        o += n_sub * ss
        vbs = []
        desc = o
        o += n_vb * 0x14
        for v in range(n_vb):
            cnt, stride, fvf = struct.unpack_from("<3I", b, desc + v * 0x14 + 8)
            vbs.append((cnt, stride, fvf, o))
            o += cnt * stride
        desc = o
        o += n_ib * 0x14
        for v in range(n_ib):
            cnt, stride = struct.unpack_from("<2I", b, desc + v * 0x14 + 8)
            o += cnt * stride
        meshes.append((name, subs, vbs))
    return mats, meshes


p = Path(sys.argv[1])
b = p.read_bytes()
mats, meshes = parse(b)

print(f"{p.name}: {len(mats)} materials, {len(meshes)} meshes")
print(f"\n{'mesh':<22} {'sub':>3} {'material':<14} {'flags':>9} {'bkt':>3} {'alpha':>5} "
      f"{'nvert':>6} {'meanRGB':>18} {'blackfrac':>9} {'meanA':>6}")

for mname, subs, vbs in meshes:
    if not vbs:
        continue
    for si, (mi, ft, tc, fv, vc) in enumerate(subs):
        cnt, stride, fvf, off = vbs[0]
        if fvf not in (0x1D2, 0x2D2):
            continue
        # DIFFUSE @24 for both real FVFs
        lo, hi = fv, min(fv + vc, cnt)
        if hi <= lo:
            continue
        rs = gs = bs = as_ = 0
        black = 0
        n = hi - lo
        for vi in range(lo, hi):
            c = struct.unpack_from("<I", b, off + vi * stride + 24)[0]
            a = (c >> 24) & 0xFF; r = (c >> 16) & 0xFF; g = (c >> 8) & 0xFF; bl = c & 0xFF
            rs += r; gs += g; bs += bl; as_ += a
            if r < 8 and g < 8 and bl < 8:
                black += 1
        _, name, tex, dw, _raw = mats[mi]
        print(f"{mname[:22]:<22} {si:>3} {name:<14} {dw[0]:#9x} {bucket(dw):>3} "
              f"{f(dw[7]):>5.2f} {n:>6} "
              f"({rs/n:>5.1f},{gs/n:>5.1f},{bs/n:>5.1f}) {black/n:>9.2f} {as_/n:>6.1f}")
