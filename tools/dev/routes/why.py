"""why.py <room> <x,y,z | from S_xxxx> <x,y,z | target label>: for a NO WAY of table.py — the part of the room reached
from the start, the part the target is reached from, and the nearest links between the two (any width, any rise)."""
import json, os, pickle, sys
import numpy as np
import nav
HERE = os.environ.get("CLERIA_ROUTE_DIR") or os.path.dirname(os.path.abspath(__file__))   # the data folder (rooms/, *.json)
R = json.load(open(os.path.join(HERE, "rooms.json")))
room = sys.argv[1]
rm = pickle.load(open(os.path.join(HERE, "rooms", room + ".nav"), "rb"))
def point(s, start):
    if s[0] in "-0123456789": return rm.node_at([float(x) for x in s.split(",")], 4, 4)
    if start:
        for e in R[room]["exits"]:
            if "from " + e["dest"] == s: return rm.node_at(e["pos"], 6, 5)
    for e in R[room]["exits"]:
        if "to " + e["dest"] == s: return rm.node_at(e["pos"], 4, 4)
    for o in R[room]["objs"]:
        if o["name"] == s: return rm.node_at(o["pos"], 3.5, 4)
a, b = point(sys.argv[2], True), point(sys.argv[3], False)
print("start node", a, None if a is None else (rm.X[a], rm.y[a], rm.Z[a]), "target node", b, None if b is None else (rm.X[b], rm.y[b], rm.Z[b]))
dbl = room not in ("S_4000", "S_4003", "S_4001", "S_4002")
E = rm.jump_edges(dbl)
F = set(rm.search(a, E))
rev = {}
for (p, q), e in E.items(): rev.setdefault(q, []).append(p)
B = {int(rm.comp[b])}; st = [int(rm.comp[b])]
while st:
    c = st.pop()
    for p in rev.get(c, ()):
        if p not in B: B.add(p); st.append(p)
print("reached from the start:", len(F), "parts,", sum(rm.size[c] for c in F), "cells; reaching the target:", len(B), "parts,", sum(rm.size[c] for c in B), "cells; common", len(F & B))
ef = np.nonzero(rm.edge & np.isin(rm.comp, list(F)))[0]; eb = np.nonzero(rm.edge & np.isin(rm.comp, list(B)))[0]
best = []
for s in range(0, len(ef), 500):
    i = ef[s:s + 500]
    d = np.hypot(rm.X[i][:, None] - rm.X[eb][None, :], rm.Z[i][:, None] - rm.Z[eb][None, :])
    h = rm.y[eb][None, :] - rm.y[i][:, None]
    score = d + 3 * np.maximum(h - 2.0, 0)
    for q in np.argsort(score, axis=None)[:40]:
        I, K = divmod(int(q), len(eb))
        best.append((float(score[I, K]), float(d[I, K]), float(h[I, K]), int(i[I]), int(eb[K])))
best.sort()
seen = set()
for sc, d, h, i, j in best:
    key = (round(rm.X[i] / 3), round(rm.Z[i] / 3), round(rm.y[i]), round(rm.y[j]))
    if key in seen: continue
    seen.add(key)
    print(f"  link: ({rm.X[i]:.1f}, {rm.y[i]:.2f}, {rm.Z[i]:.1f}) -> ({rm.X[j]:.1f}, {rm.y[j]:.2f}, {rm.Z[j]:.1f}) width {max(d - 0.5, 0):.1f} rise {h:.2f} clear {rm.flight_clear(i, j)} "
          f"Hugo JJ reach {float(nav.reach('2', '0', True, h)):.1f}")
    if len(seen) >= 8: break
