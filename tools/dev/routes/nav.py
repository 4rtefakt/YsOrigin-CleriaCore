"""nav.py: the static pass of the route tables. For a room's collision mesh (collect.py): floor layers on a 0.5 grid
(the port's floor_at rule: any face with |n.y| >= 0.5, water skipped), walls (|n.y| <= 0.6) widened by the body, walk
components (a step of <= 0.6 between neighbours), and between components the cheapest jump / drop. Every start
(the room's arrival points) to every target (exits, scripted objects) by a min-max search: the hardest gate on the
easiest way, as a fraction of Hugo's double-jump reach at that rise (jump.json, measured).
Gates assumed open: objects named door* / kekkai* (their flags are in ROUTE_F47.md). [approx]: +-0.5 on a width."""
import json, math, os, re, sys, heapq
import numpy as np
HERE = os.environ.get("CLERIA_ROUTE_DIR") or os.path.dirname(os.path.abspath(__file__))   # the data folder (rooms/, *.json)
G = 0.5
STEP = 0.6
BODY_R = 0.2
OPEN = re.compile(r"^(door|kekkai)")
J = json.load(open(os.path.join(HERE, "jump.json")))

def reach_table(key, double):
    """R[h] for h = -30 .. +5.5 step 0.1: the farthest x with feet >= h (after the apex or on the way up)."""
    hs = np.round(np.arange(-30, 5.6, 0.1), 1)
    best = np.zeros(len(hs))
    for dj, tr in J[key].items():
        if (dj != "0") != double: continue
        a = np.array(tr)
        for i, h in enumerate(hs):
            ok = np.nonzero(a[:, 1] >= h)[0]
            if len(ok): best[i] = max(best[i], a[ok[-1], 0])
    return hs, best
REACH = {}
for ch in "123":
    for dash in "01":
        for dbl in (False, True):
            REACH[(ch, dash, dbl)] = reach_table(ch + dash, dbl)[1]
HS = np.round(np.arange(-30, 5.6, 0.1), 1)
def reach(ch, dash, dbl, h):
    i = np.clip(np.round((np.asarray(h) + 30) / 0.1).astype(int), 0, len(HS) - 1)
    r = REACH[(ch, dash, dbl)][i]
    return np.where(np.asarray(h) > 5.5, 0.0, r)

def load(path):
    objs, tris, surf = [], [], {}
    for l in open(path):
        p = l.split()
        if p[0] == "o": objs.append((p[1] if len(p) > 3 else "", int(p[-2]), int(p[-1])))
        elif p[0] == "t": tris.append([float(x) for x in p[1:]])
        elif p[0] == "s": surf[int(p[1])] = p[2] if len(p) > 2 else ""
    return objs, np.array(tris).reshape(-1, 11), surf

class Room:
    def __init__(self, name, meshpath, scripted=()):
        self.name = name
        objs, T, self.surf = load(meshpath)
        self.objs = objs
        nt = len(T)
        tobj = np.full(nt, -1, int)
        for k, (n, t0, t1) in enumerate(objs): tobj[t0:t1] = k
        keep = T[:, 0] == 0
        self.opened = sorted({objs[k][0] for k in set(tobj[keep]) if k >= 0 and OPEN.match(objs[k][0])})
        for k, (n, t0, t1) in enumerate(objs):
            if OPEN.match(n): keep[t0:t1] = False
        self.scripted = {k for k, (n, t0, t1) in enumerate(objs) if n in scripted}
        T, tobj = T[keep], tobj[keep]
        V = T[:, 2:].reshape(-1, 3, 3)
        self.V = V
        n = np.cross(V[:, 1] - V[:, 0], V[:, 2] - V[:, 0])
        ln = np.linalg.norm(n, axis=1); ok = ln > 1e-9
        ny = np.zeros(len(n)); ny[ok] = np.abs(n[ok, 1]) / ln[ok]
        self.x0 = math.floor(V[:, :, 0].min()) - 1.0; self.z0 = math.floor(V[:, :, 2].min()) - 1.0
        self.nx = int((V[:, :, 0].max() - self.x0) / G) + 3; self.nz = int((V[:, :, 2].max() - self.z0) / G) + 3
        # floors
        cx, cz, cy, co, cs = [], [], [], [], []
        for t in np.nonzero(ok & (ny >= 0.5))[0]:
            a, b, c = V[t]
            i0 = int((min(a[0], b[0], c[0]) - self.x0) / G); i1 = int((max(a[0], b[0], c[0]) - self.x0) / G) + 1
            k0 = int((min(a[2], b[2], c[2]) - self.z0) / G); k1 = int((max(a[2], b[2], c[2]) - self.z0) / G) + 1
            xs = self.x0 + G * np.arange(i0, i1 + 1); zs = self.z0 + G * np.arange(k0, k1 + 1)
            X, Z = np.meshgrid(xs, zs, indexing="ij")
            d = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2])
            if abs(d) < 1e-8: continue
            wa = ((b[2] - c[2]) * (X - c[0]) + (c[0] - b[0]) * (Z - c[2])) / d
            wb = ((c[2] - a[2]) * (X - c[0]) + (a[0] - c[0]) * (Z - c[2])) / d
            wc = 1 - wa - wb
            m = (wa >= -1e-3) & (wb >= -1e-3) & (wc >= -1e-3)
            if not m.any(): continue
            I, K = np.nonzero(m)
            cx.append(I + i0); cz.append(K + k0); cy.append((wa * a[1] + wb * b[1] + wc * c[1])[m])
            co.append(np.full(len(I), tobj[t])); cs.append(np.full(len(I), int(T[t, 1])))
        cx, cz, cy, co, cs = (np.concatenate(v) for v in (cx, cz, cy, co, cs))
        cell = cx * self.nz + cz
        order = np.lexsort((-cy, cell))
        cell, cy, co, cs = cell[order], cy[order], co[order], cs[order]
        # merge layers of a cell closer than 0.3 (keep the higher)
        keepm = np.ones(len(cell), bool); lasti = 0
        cl, yl = cell.tolist(), cy.tolist()
        for i in range(1, len(cl)):
            if cl[i] == cl[lasti] and yl[lasti] - yl[i] < 0.3: keepm[i] = False
            else: lasti = i
        self.cell, self.y, self.obj, self.sf = cell[keepm], cy[keepm], co[keepm], cs[keepm]
        # walls
        wcell, wlo, whi = [], [], []
        for t in np.nonzero(ok & (ny <= 0.6))[0]:
            a, b, c = V[t]
            lo, hi = min(a[1], b[1], c[1]), max(a[1], b[1], c[1])
            i0 = int((min(a[0], b[0], c[0]) - BODY_R - self.x0) / G); i1 = int((max(a[0], b[0], c[0]) + BODY_R - self.x0) / G) + 1
            k0 = int((min(a[2], b[2], c[2]) - BODY_R - self.z0) / G); k1 = int((max(a[2], b[2], c[2]) + BODY_R - self.z0) / G) + 1
            xs = self.x0 + G * np.arange(i0, i1 + 1); zs = self.z0 + G * np.arange(k0, k1 + 1)
            X, Z = np.meshgrid(xs, zs, indexing="ij")
            dmin = np.full(X.shape, 1e9)
            for p, q in ((a, b), (b, c), (c, a)):
                ex, ez = q[0] - p[0], q[2] - p[2]; L2 = ex * ex + ez * ez
                tt = np.clip(((X - p[0]) * ex + (Z - p[2]) * ez) / L2, 0, 1) if L2 > 1e-12 else np.zeros(X.shape)
                dmin = np.minimum(dmin, np.hypot(X - (p[0] + tt * ex), Z - (p[2] + tt * ez)))
            m = dmin < BODY_R
            if not m.any(): continue
            I, K = np.nonzero(m)
            wcell.append((I + i0) * self.nz + (K + k0)); wlo.append(np.full(len(I), lo)); whi.append(np.full(len(I), hi))
        self.walls = {}
        if wcell:
            wcell, wlo, whi = (np.concatenate(v) for v in (wcell, wlo, whi))
            for c_, l_, h_ in zip(wcell.tolist(), wlo.tolist(), whi.tolist()): self.walls.setdefault(c_, []).append((l_, h_))
        # standable nodes
        blocked = np.zeros(len(self.cell), bool)
        for i, (c_, y_) in enumerate(zip(self.cell.tolist(), self.y.tolist())):
            for l_, h_ in self.walls.get(c_, ()):
                if l_ < y_ + 1.6 and h_ > y_ + STEP + 0.05: blocked[i] = True; break
        self.blocked = blocked
        self.layers = {}
        for i, c_ in enumerate(self.cell.tolist()):
            if not blocked[i]: self.layers.setdefault(c_, []).append(i)
        # walk components
        par = list(range(len(self.cell)))
        def find(a):
            while par[a] != a: par[a] = par[par[a]]; a = par[a]
            return a
        nb = [(1, 0), (0, 1), (1, 1), (1, -1)]
        nlinks = np.zeros(len(self.cell), int)
        ys = self.y
        for c_, L in self.layers.items():
            ix, iz = divmod(c_, self.nz)
            for dx, dz in nb:
                c2 = (ix + dx) * self.nz + iz + dz
                L2 = self.layers.get(c2)
                if not L2: continue
                lim = STEP * (1.42 if dx and dz else 1.0)
                for i in L:
                    for j in L2:
                        if abs(ys[i] - ys[j]) <= lim:
                            nlinks[i] += 1; nlinks[j] += 1
                            a, b = find(i), find(j)
                            if a != b: par[a] = b
        self.comp = np.array([find(i) if not blocked[i] else -1 for i in range(len(self.cell))])
        self.edge = (~blocked) & (nlinks < 8)
        self.size = {}
        for c_ in self.comp.tolist(): self.size[c_] = self.size.get(c_, 0) + 1
        self.X = self.x0 + G * (self.cell // self.nz); self.Z = self.z0 + G * (self.cell % self.nz)

    def node_at(self, p, rxz=3.0, ry=3.0, minsize=1):
        ok = np.nonzero((~self.blocked) & (np.abs(self.y - p[1]) <= ry) & (np.hypot(self.X - p[0], self.Z - p[2]) <= rxz))[0]
        if not len(ok): return None
        d = np.hypot(self.X[ok] - p[0], self.Z[ok] - p[2]) + 0.5 * np.abs(self.y[ok] - p[1])
        return int(ok[np.argmin(d)])

    def door_node(self, p, rxz=6.0):
        """the floor under a door trigger (its position is the volume's centre, 2 .. 7 over the floor)."""
        ok = np.nonzero((~self.blocked) & (self.y <= p[1] - 0.4) & (self.y >= p[1] - 8.0) & (np.hypot(self.X - p[0], self.Z - p[2]) <= rxz))[0]
        if not len(ok): return self.node_at(p, rxz, 5.0)
        d = np.hypot(self.X[ok] - p[0], self.Z[ok] - p[2]) + 0.2 * np.abs(self.y[ok] - p[1])
        return int(ok[np.argmin(d)])

    def flight_clear(self, i, j):
        xa, za, ya, xb, zb, yb = self.X[i], self.Z[i], self.y[i], self.X[j], self.Z[j], self.y[j]
        d = math.hypot(xb - xa, zb - za)
        n = max(2, int(d / 0.25))
        top = max(ya, yb)
        for k in range(1, n):
            f = k / n
            x, z = xa + (xb - xa) * f, za + (zb - za) * f
            c_ = int(round((x - self.x0) / G)) * self.nz + int(round((z - self.z0) / G))
            # the body over the gap: at least the higher end's level, at most the double jump's apex above the take-off
            lo = top + 0.3; hi = max(ya + 4.4, top + 0.5)
            walls = [(l_, h_) for l_, h_ in self.walls.get(c_, ()) if l_ < hi + 1.0 and h_ > lo]
            if walls:
                free = lo
                for l_, h_ in sorted(walls):
                    if l_ - free >= 1.3: break
                    free = max(free, h_)
                else:
                    if hi + 1.0 - free < 1.3: return False
                if free > hi: return False
        return True

    def jump_edges(self, dbl=True, dash="0", minsize=1):
        """{(ca, cb): (ratio, i, j, d, h)}: the best jump / drop from component ca to cb (ratio vs Hugo's double jump)."""
        E = np.nonzero(self.edge)[0]
        E = E[[self.size[self.comp[i]] >= minsize for i in E]] if len(E) else E
        if not len(E): return {}
        X, Z, Y, C = self.X[E], self.Z[E], self.y[E], self.comp[E]
        best = {}
        B = 2000
        for s in range(0, len(E), B):
            xs, zs, ys, cs_ = X[s:s + B, None], Z[s:s + B, None], Y[s:s + B, None], C[s:s + B, None]
            d = np.hypot(X[None, :] - xs, Z[None, :] - zs)
            m = (d <= 16.0) & (C[None, :] != cs_)
            I, K = np.nonzero(m)
            if not len(I): continue
            dd = np.maximum(d[I, K] - G, 0.0); h = Y[K] - Y[s + I]
            r = reach("2", dash, dbl, h)
            ratio = np.where(r > 0, dd / np.maximum(r, 1e-6), np.inf)
            ratio = np.maximum(ratio, np.maximum(h, 0.0) / (5.37 if dbl else 2.63))   # a rise's share of the jump's top
            okm = np.isfinite(ratio) & (ratio <= 1.3)
            I, K, dd, h, ratio = I[okm], K[okm], dd[okm], h[okm], ratio[okm]
            for q in range(len(I)):
                i_, j_ = int(E[s + I[q]]), int(E[K[q]])
                key = (int(C[s + I[q]]), int(C[K[q]]))
                bk = (int(self.X[i_] // 2), int(self.Z[i_] // 2), int(self.y[i_] // 1), int(self.y[j_] // 1))
                cur = best.setdefault(key, {})
                c0 = cur.get(bk)
                if c0 is None or ratio[q] < c0[0]: cur[bk] = (float(ratio[q]), i_, j_, float(dd[q]), float(h[q]))
        out = {}
        for key, cands in best.items():
            for (ratio, i, j, dd, h) in sorted(cands.values()):
                if self.flight_clear(i, j): out[key] = (ratio, i, j, dd, h); break
        return out

    def search(self, start, edges):
        """min-max from node `start`: {comp: (worst ratio, [edge keys])}."""
        adj = {}
        for (a, b), e in edges.items(): adj.setdefault(a, []).append((b, e))
        s = int(self.comp[start]); best = {s: (0.0, [])}; pq = [(0.0, 0, s)]
        while pq:
            w, n_, a = heapq.heappop(pq)
            if best[a][0] < w or (best[a][0] == w and len(best[a][1]) < n_): continue
            for b, e in adj.get(a, ()):
                w2 = max(w, e[0]); path = best[a][1] + [(a, b)]
                if b not in best or (w2, len(path)) < (best[b][0], len(best[b][1])):
                    best[b] = (w2, path); heapq.heappush(pq, (w2, len(path), b))
        return best

    def walk_path(self, i, j):
        """BFS inside a component from node i to node j, thinned to the nodes a straight walk cannot skip."""
        if i == j: return [i]
        from collections import deque
        prev = {i: None}; dq = deque([i]); ys = self.y
        while dq:
            a = dq.popleft()
            if a == j: break
            ix, iz = divmod(int(self.cell[a]), self.nz)
            for dx in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    if not dx and not dz: continue
                    for b in self.layers.get((ix + dx) * self.nz + iz + dz, ()):
                        if b not in prev and abs(ys[a] - ys[b]) <= STEP * (1.42 if dx and dz else 1.0): prev[b] = a; dq.append(b)
        if j not in prev: return None
        p = []; a = j
        while a is not None: p.append(a); a = prev[a]
        p.reverse()
        out = [p[0]]; k = 0
        while k < len(p) - 1:
            m = len(p) - 1
            while m > k + 1 and not self.walk_clear(p[k], p[m]): m -= 1
            out.append(p[m]); k = m
        return out

    def walk_clear(self, i, j):
        xa, za, xb, zb = self.X[i], self.Z[i], self.X[j], self.Z[j]
        n = max(2, int(math.hypot(xb - xa, zb - za) / 0.25)); y = self.y[i]; c = self.comp[i]
        for k in range(1, n + 1):
            f = k / n; x, z = xa + (xb - xa) * f, za + (zb - za) * f
            c_ = int(round((x - self.x0) / G)) * self.nz + int(round((z - self.z0) / G))
            L = [b for b in self.layers.get(c_, ()) if self.comp[b] == c and abs(self.y[b] - y) <= 1.0]
            if not L: return False
            y = self.y[L[0]]
        return True
