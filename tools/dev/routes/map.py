"""map.py <room> [x0,z0,x1,z1] [step]: top-down map of the top standable layer (y rounded, 2 digits; '..' none, '##' blocked only)."""
import os, pickle, sys
import numpy as np
HERE = os.environ.get("CLERIA_ROUTE_DIR") or os.path.dirname(os.path.abspath(__file__))   # the data folder (rooms/, *.json)
rm = pickle.load(open(os.path.join(HERE, "rooms", sys.argv[1] + ".nav"), "rb"))
if len(sys.argv) > 2 and sys.argv[2] != "-": x0, z0, x1, z1 = map(float, sys.argv[2].split(","))
else: x0, z0, x1, z1 = rm.X.min(), rm.Z.min(), rm.X.max(), rm.Z.max()
st = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
top = {}
for i in range(len(rm.cell)):
    k = (round(rm.X[i] / st), round(rm.Z[i] / st))
    v = (not rm.blocked[i], rm.y[i])
    if k not in top or v > top[k]: top[k] = v
xs = range(int(round(x0 / st)), int(round(x1 / st)) + 1)
print("     " + "".join(f"{int(x * st) % 100:3d}" if x % 2 == 0 else "   " for x in xs))
for z in range(int(round(z1 / st)), int(round(z0 / st)) - 1, -1):
    row = ""
    for x in xs:
        v = top.get((x, z))
        row += "  ." if v is None else (" ##" if not v[0] else f"{int(round(v[1])):3d}")
    print(f"{z * st:5.0f}" + row)
