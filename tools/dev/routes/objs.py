"""objs.py <room> [regex]: bbox of each object's collision triangles in the mesh dump (and in mesh2 when it moved)."""
import sys, re, os, nav, numpy as np
HERE = os.environ.get("CLERIA_ROUTE_DIR") or os.path.dirname(os.path.abspath(__file__))   # the data folder (rooms/, *.json)
o, T, s = nav.load(os.path.join(HERE, "rooms", sys.argv[1] + ".mesh"))
p2 = os.path.join(HERE, "rooms", sys.argv[1] + ".mesh2")
T2 = nav.load(p2)[1] if os.path.exists(p2) else None
pat = re.compile(sys.argv[2] if len(sys.argv) > 2 else ".")
for n, t0, t1 in o:
    if t1 <= t0 or not pat.search(n): continue
    V = T[t0:t1, 2:].reshape(-1, 3); a, b = V.min(0), V.max(0)
    line = f"{n:14s} x {a[0]:7.2f} .. {b[0]:7.2f}  y {a[1]:7.2f} .. {b[1]:7.2f}  z {a[2]:7.2f} .. {b[2]:7.2f}  surf {sorted({s.get(int(q), '?') for q in set(T[t0:t1, 1])})}"
    if T2 is not None and len(T2) == len(T):
        W = T2[t0:t1, 2:].reshape(-1, 3)
        if np.abs(W - V).max() > 0.05: c, d = W.min(0), W.max(0); line += f"  | t460: x {c[0]:.2f} .. {d[0]:.2f} y {c[1]:.2f} .. {d[1]:.2f} z {c[2]:.2f} .. {d[2]:.2f}"
    print(line)
