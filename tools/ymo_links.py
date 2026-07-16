"""Pin down (a) node link semantics and (b) which keyframe array is which.

Both are answered by cross-referencing against ground truth already in the file:

NODE LINKS -- node has name[1..3] resolving to other nodes. Test:
  P1  name[1] == parent  -> following it yields a forest with no cycles, and
                            every node reachable from a root
  P2  name[2] == first child / name[3] == next sibling (classic binary encoding)
      -> if A.name[2] == B then B.name[1] == A

KEYFRAME ARRAYS -- a track targets a node by name, and that node carries a rest
TRS (translation @0x260, scale @0x26c, quat @0x278). The first keyframe of each
array should match the corresponding rest component. That identifies A and C
without any guessing.
"""
import math
import struct
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ymo_parse import parse_mesh  # noqa: E402

NODE = 0x2A0


def load(b):
    n_mat, n_s5, n_mesh, n_node = struct.unpack_from("<4I", b, 0x10)
    o = 0xA3 * 4 + n_mat * 0xBB * 4
    nodes = []
    for i in range(n_node):
        r = o + i * NODE
        names = [b[r + k * 0x60: r + k * 0x60 + 0x60].split(b"\0")[0]
                 .decode("cp932", "replace") for k in range(5)]
        nodes.append({
            "name": names[0], "links": names[1:4], "mesh": names[4],
            "t": struct.unpack_from("<3f", b, r + 0x260),
            "s": struct.unpack_from("<3f", b, r + 0x26C),
            "q": struct.unpack_from("<4f", b, r + 0x278),
        })
    o += n_node * NODE
    for _ in range(n_mesh):
        o, _, _ = parse_mesh(b, o)
    trk = []
    for _ in range(n_s5):
        clip = o
        o += 0x28 * 4
        n = struct.unpack_from("<I", b, clip + 0x27 * 4)[0]
        for _ in range(n):
            t = o
            o += 0x70
            cA, cB, cC, typ = struct.unpack_from("<4I", b, t + 0x60)
            nm = b[t:t + 0x60].split(b"\0")[0].decode("cp932", "replace")
            oa, ob, oc = o, o + cA * 0x10, o + cA * 0x10 + cB * 0x14
            trk.append({
                "node": nm,
                "A": [struct.unpack_from("<4f", b, oa + i * 0x10) for i in range(min(cA, 1))],
                "B": [struct.unpack_from("<5f", b, ob + i * 0x14) for i in range(min(cB, 1))],
                "C": [struct.unpack_from("<4f", b, oc + i * 0x10) for i in range(min(cC, 1))],
            })
            o += cA * 0x10 + cB * 0x14 + cC * 0x10 if typ == 0 else typ * 0x50
    return nodes, trk


def close(a, bb, tol=1e-3):
    return all(abs(x - y) <= tol * max(1.0, abs(x), abs(y)) for x, y in zip(a, bb))


root = Path(sys.argv[1])
files = sorted({p.resolve() for p in root.rglob("*.[Yy][Mm][Oo]")})

L = [Counter() for _ in range(3)]     # per link slot: empty / resolves / dangling
roots = Counter()
p2_hits = p2_tot = 0
cyc = 0
match = Counter()
tot = Counter()

for p in files:
    try:
        nodes, trk = load(p.read_bytes())
    except Exception:
        continue
    by = {n["name"]: i for i, n in enumerate(nodes)}

    for i, n in enumerate(nodes):
        for k in range(3):
            v = n["links"][k]
            L[k]["empty" if not v else
              ("resolves" if v in by else "dangling")] += 1
    nr = sum(1 for n in nodes if not n["links"][0])
    roots[nr] += 1

    # P2: if A.link[1] (first child) == B, does B.link[0] (parent) == A?
    for i, n in enumerate(nodes):
        c = n["links"][1]
        if c and c in by:
            p2_tot += 1
            if nodes[by[c]]["links"][0] == n["name"]:
                p2_hits += 1

    # cycle check on link[0] as parent
    for i in range(len(nodes)):
        seen, j, n_ = set(), i, 0
        while True:
            if j in seen:
                cyc += 1
                break
            seen.add(j)
            par = nodes[j]["links"][0]
            if not par or par not in by:
                break
            j = by[par]
            n_ += 1
            if n_ > 500:
                cyc += 1
                break

    # keyframe identity: first key vs the node's rest TRS
    for t in trk:
        if t["node"] not in by:
            continue
        nd = nodes[by[t["node"]]]
        for arr, lbl in (("A", "A"), ("C", "C")):
            if t[arr]:
                v = t[arr][0][:3]
                tot[lbl] += 1
                if close(v, nd["t"]): match[f"{lbl}~translation"] += 1
                if close(v, nd["s"]): match[f"{lbl}~scale"] += 1
        if t["B"]:
            q = t["B"][0][:4]
            tot["B"] += 1
            if close(q, nd["q"]): match["B~quat"] += 1

print("NODE LINK SLOTS")
for k in range(3):
    tt = sum(L[k].values())
    print(f"  name[{k+1}]: " + "  ".join(
        f"{w}={L[k][w]} ({100*L[k][w]/tt:.1f}%)"
        for w in ("empty", "resolves", "dangling")))
print(f"\n  files by count of nodes with empty name[1] (root candidates):")
for n, c in roots.most_common(5):
    print(f"      {n} root(s): {c} files")
print(f"  cycles following name[1] as parent: {cyc}")
print(f"  P2  A.name[2]==B implies B.name[1]==A : "
      f"{p2_hits}/{p2_tot} " + (f"({100*p2_hits/p2_tot:.1f}%)" if p2_tot else ""))

print("\nKEYFRAME ARRAY IDENTITY (first key vs the target node's rest TRS)")
for lbl in ("A", "C", "B"):
    if not tot[lbl]:
        continue
    print(f"  {lbl}: {tot[lbl]} tracks")
    for k, v in match.items():
        if k.startswith(lbl + "~"):
            print(f"      {k:16s} {v}/{tot[lbl]} ({100*v/tot[lbl]:.1f}%)")
