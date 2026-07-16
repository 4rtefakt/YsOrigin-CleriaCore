"""End-to-end YMO parser -- the falsification test for the whole format.

Every stride below came from the loader (FUN_005f31d0 / FUN_005f4420 /
FUN_005f3c70). If the understanding is right, parsing must land EXACTLY on a
known boundary for all 1006 files. Any byte of drift means something is wrong.

On-disk order (the order the loader consumes, NOT header field order):
    header    652 bytes (0xA3 dwords)
    materials count[0x10] x 748   (0xBB dwords)
    nodes     count[0x1C] x 672   (0x2A0)
    meshes    count[0x18] x variable
    section5  count[0x14] x (160 + subrecords)   <- stride unknown; derived here
"""
import struct
import sys
from collections import Counter
from pathlib import Path

POSMASK, TEXMASK = 0x00E, 0xF00


def fvf_stride(f):
    off = 0
    pos = f & POSMASK
    if pos == 0x002: off += 12
    elif pos == 0x004: off += 16
    elif pos in (0x006, 0x008, 0x00A, 0x00C, 0x00E):
        off += 12 + 4 * ((pos - 0x004) // 2)
    else: return None
    if f & 0x010: off += 12
    if f & 0x020: off += 4
    if f & 0x040: off += 4
    if f & 0x080: off += 4
    ntex = (f & TEXMASK) >> 8
    if ntex > 8: return None
    return off + 8 * ntex


def parse_mesh(b, o):
    """Consume one mesh record; return new offset + stats."""
    h = o
    o += 0xB4                                    # mesh header
    n9c = struct.unpack_from("<I", b, h + 0x9C)[0]
    n88 = struct.unpack_from("<I", b, h + 0x88)[0]
    n94 = struct.unpack_from("<I", b, h + 0x94)[0]
    n8c = struct.unpack_from("<I", b, h + 0x8C)[0]
    n90 = struct.unpack_from("<I", b, h + 0x90)[0]

    o += n9c * 0x60                              # texture-binding records
    o += n88 * (0x20 + n94 * 4)                  # submesh descs + per-group lists

    vbs, ibs = [], []
    if n8c:
        base = o
        o += n8c * 0x14                          # VB descriptors
        for i in range(n8c):
            cnt, stride, fvf = struct.unpack_from("<3I", b, base + i * 0x14 + 8)
            vbs.append((cnt, stride, fvf))
            if cnt:
                o += stride * cnt                # VB payload
    if n90:
        base = o
        o += n90 * 0x14                          # IB descriptors
        for i in range(n90):
            cnt, stride = struct.unpack_from("<2I", b, base + i * 0x14 + 8)
            ibs.append((cnt, stride))
            if cnt:
                o += stride * cnt                # IB payload
    return o, vbs, ibs


root = Path(sys.argv[1])
files = sorted({p.resolve() for p in root.rglob("*.[Yy][Mm][Oo]")})

exact = 0
derived = Counter()
fvf_seen = Counter()
ib_stride = Counter()
fails = []
tot_v = tot_t = 0
no_s5_exact = 0

for p in files:
    b = p.read_bytes()
    try:
        n_mat, n_s5, n_mesh, n_node = struct.unpack_from("<4I", b, 0x10)
        o = 0xA3 * 4                             # header
        o += n_mat * 0xBB * 4                    # materials
        o += n_node * 0x2A0                      # nodes
        for _ in range(n_mesh):
            o, vbs, ibs = parse_mesh(b, o)
            for cnt, stride, fvf in vbs:
                fvf_seen[fvf] += 1
                tot_v += cnt
                # FVF==0 => D3D9 vertex *declaration* (shader path), not
                # fixed-function. Stride is authoritative; nothing to derive.
                if fvf != 0 and fvf_stride(fvf) != stride:
                    raise ValueError(f"FVF {fvf:#x} stride {stride} != "
                                     f"{fvf_stride(fvf)}")
            for cnt, stride in ibs:
                ib_stride[stride] += 1
                tot_t += cnt // 3
        if o > len(b):
            raise ValueError(f"overran: {o} > {len(b)}")
        # section[5] -- animation clips (FUN_005f31d0 tail + FUN_005f4110)
        for _ in range(n_s5):
            clip = o
            o += 0x28 * 4                        # 160-byte clip header
            n_trk = struct.unpack_from("<I", b, clip + 0x27 * 4)[0]
            for _ in range(n_trk):
                t = o
                o += 0x70                        # 112-byte track header
                cA, cB, cC, typ = struct.unpack_from("<4I", b, t + 0x60)
                if typ == 0:
                    o += cA * 0x10 + cB * 0x14 + cC * 0x10
                else:
                    o += typ * 0x50
        if o > len(b):
            raise ValueError(f"overran in section5: {o} > {len(b)}")
        rest = len(b) - o
        if rest == 0:
            exact += 1
            if n_s5 == 0:
                no_s5_exact += 1
        else:
            raise ValueError(f"{rest} bytes left over (count5={n_s5})")
    except Exception as e:
        if len(fails) < 8:
            fails.append((p.name, str(e)))

print(f"corpus            : {len(files)} files")
print(f"parsed byte-exact : {exact}/{len(files)}  ({100*exact/len(files):.2f}%)")
print(f"  of which count5==0 and landed exactly on EOF : {no_s5_exact}")
print(f"failures          : {len(files) - exact}")
for n, e in fails:
    print(f"    ! {n}: {e}")
print(f"\ntotals: {tot_v:,} vertices, {tot_t:,} triangles")
print(f"\nFVF (from a real parse, no scanning): "
      f"{ {hex(k): v for k, v in fvf_seen.most_common()} }")
print(f"index strides: { dict(ib_stride) }")
print(f"\nderived section[5] stride (remainder / count5):")
for s, c in derived.most_common(6):
    print(f"    {s:6d} bytes  x{c} files")
