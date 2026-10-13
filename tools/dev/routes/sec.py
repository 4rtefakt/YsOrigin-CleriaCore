"""sec.py <room> x0,z0,x1,z1 [step]: the floor layers along a segment (y, * = blocked by a wall, part id), from the nav pickle."""
import os, pickle, sys, math
HERE = os.environ.get("CLERIA_ROUTE_DIR") or os.path.dirname(os.path.abspath(__file__))   # the data folder (rooms/, *.json)
rm = pickle.load(open(os.path.join(HERE, "rooms", sys.argv[1] + ".nav"), "rb"))
x0, z0, x1, z1 = map(float, sys.argv[2].split(","))
st = float(sys.argv[3]) if len(sys.argv) > 3 else 0.5
n = max(1, int(round(math.hypot(x1 - x0, z1 - z0) / st)))
import numpy as np
for k in range(n + 1):
    x, z = x0 + (x1 - x0) * k / n, z0 + (z1 - z0) * k / n
    c = int(round((x - rm.x0) / 0.5)) * rm.nz + int(round((z - rm.z0) / 0.5))
    idx = np.nonzero(rm.cell == c)[0]
    w = sorted((round(a, 1), round(b, 1)) for a, b in rm.walls.get(c, ()))
    wm = []
    for a, b in w:
        if wm and a <= wm[-1][1] + 0.3: wm[-1] = (wm[-1][0], max(wm[-1][1], b))
        else: wm.append((a, b))
    print(f"({x:.1f}, {z:.1f}): " + "  ".join(f"{rm.y[i]:.2f}{'*' if rm.blocked[i] else ''}#{rm.comp[i] % 1000}{'' if rm.obj[i] < 0 else ':' + rm.objs[rm.obj[i]][0]}" for i in idx) + (f"   walls {wm}" if wm else ""))
