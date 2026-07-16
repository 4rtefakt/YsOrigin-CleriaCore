"""Determine YMO animation keyframe layouts empirically.

From FUN_005f4110 a track is:
    +0x00 char[0x60] node name   +0x60 countA  +0x64 countB
    +0x68 countC                 +0x6C type    (header 0x70)
  type == 0 : countA x 0x10 , countB x 0x14 , countC x 0x10
  type != 0 : type x 0x50

Hypotheses to falsify:
  H1  0x10 record = 4 floats = time + vec3      (translation / scale)
  H2  0x14 record = 5 floats = time + quat      (rotation)  -> |q| == 1
  H3  time is field 0 and increases monotonically
  H4  0x50 record = 20 floats = 4x4 matrix + 4  (baked)

The quaternion unit-length test is the decisive one: random bytes are not
unit quaternions.
"""
import math
import struct
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ymo_parse import parse_mesh  # noqa: E402


def tracks(b):
    """Yield (name, countA, countB, countC, type, payload_offset)."""
    n_mat, n_s5, n_mesh, n_node = struct.unpack_from("<4I", b, 0x10)
    o = 0xA3 * 4 + n_mat * 0xBB * 4 + n_node * 0x2A0
    for _ in range(n_mesh):
        o, _, _ = parse_mesh(b, o)
    for _ in range(n_s5):
        clip = o
        o += 0x28 * 4
        n_trk = struct.unpack_from("<I", b, clip + 0x27 * 4)[0]
        for _ in range(n_trk):
            t = o
            o += 0x70
            cA, cB, cC, typ = struct.unpack_from("<4I", b, t + 0x60)
            name = b[t:t + 0x60].split(b"\0")[0].decode("cp932", "replace")
            yield name, cA, cB, cC, typ, o
            if typ == 0:
                o += cA * 0x10 + cB * 0x14 + cC * 0x10
            else:
                o += typ * 0x50


def mono(vals):
    return all(x <= y for x, y in zip(vals, vals[1:]))


root = Path(sys.argv[1])
files = sorted({p.resolve() for p in root.rglob("*.[Yy][Mm][Oo]")})

stat = Counter()
q_norm_first = []   # |q| assuming [t, x,y,z,w]
q_norm_last = []    # |q| assuming [x,y,z,w, t]
a_mono_first = a_mono_last = a_tot = 0
c_mono_first = c_mono_last = c_tot = 0
b_mono_first = b_tot = 0
t_max = []
samples = []

for p in files:
    b = p.read_bytes()
    for name, cA, cB, cC, typ, o in tracks(b):
        stat["tracks"] += 1
        stat[f"type={typ}" if typ < 3 else "type>=3"] += 1
        if typ != 0:
            stat["keys_matrix"] += typ
            continue
        oa = o
        ob = oa + cA * 0x10
        oc = ob + cB * 0x14

        if cA:
            recs = [struct.unpack_from("<4f", b, oa + i * 0x10) for i in range(cA)]
            a_tot += 1
            if mono([r[0] for r in recs]): a_mono_first += 1
            if mono([r[3] for r in recs]): a_mono_last += 1
            stat["keys_A"] += cA
            if len(samples) < 3 and cA > 2:
                samples.append((p.name, name, "A(0x10)", recs[:3]))
        if cC:
            recs = [struct.unpack_from("<4f", b, oc + i * 0x10) for i in range(cC)]
            c_tot += 1
            if mono([r[0] for r in recs]): c_mono_first += 1
            if mono([r[3] for r in recs]): c_mono_last += 1
            stat["keys_C"] += cC
        if cB:
            recs = [struct.unpack_from("<5f", b, ob + i * 0x14) for i in range(cB)]
            b_tot += 1
            if mono([r[0] for r in recs]): b_mono_first += 1
            stat["keys_B"] += cB
            for r in recs[:64]:
                if all(map(math.isfinite, r)):
                    q_norm_first.append(math.sqrt(sum(v * v for v in r[1:5])))
                    q_norm_last.append(math.sqrt(sum(v * v for v in r[0:4])))
                    t_max.append(r[0])
            if len(samples) < 6 and cB > 2:
                samples.append((p.name, name, "B(0x14)", recs[:3]))


def pct(hits, tot):
    return f"{hits}/{tot} ({100*hits/tot:.1f}%)" if tot else "n/a"


print(f"corpus: {len(files)} files")
print(f"tracks: {stat['tracks']}   "
      f"type==0: {stat['type=0']}  type==1: {stat['type=1']}  "
      f"type==2: {stat['type=2']}  type>=3: {stat['type>=3']}")
print(f"keys: A={stat['keys_A']:,}  B={stat['keys_B']:,}  "
      f"C={stat['keys_C']:,}  matrix={stat['keys_matrix']:,}\n")

print("H3 — time is field 0, monotonically increasing:")
print(f"  A (0x10)  field0 mono: {pct(a_mono_first, a_tot)}"
      f"   |  field3 mono: {pct(a_mono_last, a_tot)}")
print(f"  C (0x10)  field0 mono: {pct(c_mono_first, c_tot)}"
      f"   |  field3 mono: {pct(c_mono_last, c_tot)}")
print(f"  B (0x14)  field0 mono: {pct(b_mono_first, b_tot)}")


def report(label, norms):
    if not norms:
        print(f"  {label}: no data")
        return
    good = sum(1 for n in norms if abs(n - 1.0) < 1e-3)
    print(f"  {label}: {good}/{len(norms)} ({100*good/len(norms):.2f}%) unit"
          f"   [min {min(norms):.4f} max {max(norms):.4f}]")


print("\nH2 — the 0x14 record contains a UNIT quaternion:")
report("fields 1-4  (layout [t, x,y,z,w])", q_norm_first)
report("fields 0-3  (layout [x,y,z,w, t])", q_norm_last)

if t_max:
    print(f"\ntime field 0 range: {min(t_max):.3f} .. {max(t_max):.3f}")

print("\nsamples:")
for fn, nm, kind, recs in samples:
    print(f"  {fn} :: {nm} :: {kind}")
    for r in recs:
        print("      " + "  ".join(f"{v:10.4f}" for v in r))
