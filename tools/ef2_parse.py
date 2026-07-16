import struct, glob, os
class EF2:
    def __init__(s, path):
        s.path=path; s.name=os.path.basename(path)
        b=open(path,'rb').read(); s.b=b
        s.magic=b[:4]; s.ver=struct.unpack_from('<I',b,4)[0]
        s.psz=struct.unpack_from('<I',b,0x10)[0]
        s.nA=struct.unpack_from('<I',b,0x14)[0]
        s.nB=struct.unpack_from('<I',b,0x18)[0]
        assert len(b)==32+8*s.nA+2580*s.nB, s.name
        s.aOff=0x20; s.b1Off=0x20+8*s.nA; s.b2Off=s.b1Off+s.nB*0xf0
    def A(s,i): return s.b[s.aOff+i*8 : s.aOff+i*8+8]
    def B1(s,i): return s.b[s.b1Off+i*0xf0 : s.b1Off+(i+1)*0xf0]
    def B2(s,i): return s.b[s.b2Off+i*0x924 : s.b2Off+(i+1)*0x924]
def files():
    return [EF2(p) for p in sorted(glob.glob(r"C:\Users\kores\repos\YsOrigin-CleriaCore\build\sidecar_re\eft\*.EF2"))]
def show(rec, base=0, cols=4):
    for o in range(0, len(rec)-3, 4):
        u=struct.unpack_from('<I',rec,o)[0]; f=struct.unpack_from('<f',rec,o)[0]
        yield o, u, f

# ---- decoded views (see build/ef2_re/EF2_FORMAT.md for evidence tags) ----
def frames(f, i):
    """[(tex_id, (u0,v0,u1,v1)), ...] animation frame table of emitter i."""
    b2 = f.B2(i)
    n = struct.unpack_from('<I', b2, 0x48)[0]
    if n == 0 or n > 16:
        return []
    ids = struct.unpack_from(f'<{n}I', b2, 0x4c)
    return [(ids[k], struct.unpack_from('<4f', b2, 0x8c + k*16)) for k in range(n)]

def quad(f, i):
    """4 billboard verts: [(x,y,z, argb, u,v), ...] (D3D TRIANGLESTRIP order)."""
    b1 = f.B1(i)
    out = []
    for v in range(4):
        o = 0x7c + v*0x1c
        x, y, z = struct.unpack_from('<3f', b1, o)
        c = struct.unpack_from('<I', b1, o+12)[0]
        u, vv = struct.unpack_from('<2f', b1, o+20)
        out.append((x, y, z, c, u, vv))
    return out

def track(f, i, k):
    """Track k (0..16) of emitter i -> [(value, inv_dt, t_end), ...]; last is (v,-1,0) terminator."""
    b2 = f.B2(i)
    o = 0x1a0 + k*0x68
    n = struct.unpack_from('<I', b2, o)[0]
    return [struct.unpack_from('<3f', b2, o+8+j*12) for j in range(n)]

def eval_track(keys, t):
    """Piecewise-linear eval of a track at normalized time t in [0,1]."""
    if not keys:
        return 0.0
    if len(keys) == 1:
        return keys[0][0]
    prev_t, prev_v = 0.0, keys[0][0]
    for j in range(len(keys)-1):
        v, inv, t_end = keys[j]
        if t <= t_end or j == len(keys)-2:
            nxt = keys[j+1][0]
            a = 0.0 if inv in (0.0,) else min(max((t - prev_t) * inv, 0.0), 1.0)
            return v + (nxt - v) * a
        prev_t, prev_v = t_end, v
    return keys[-1][0]
