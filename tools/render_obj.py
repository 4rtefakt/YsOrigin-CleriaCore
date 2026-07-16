"""Quick-look renderer: project a cleria-dump .obj to a depth-shaded SVG.

No dependencies. Orthographic 3/4 camera, painter's algorithm, flat shading by
camera depth. Purely for eyeballing that a reconstructed room is coherent.
"""
import math
import sys
from pathlib import Path

src = Path(sys.argv[1])
out = Path(sys.argv[2]) if len(sys.argv) > 2 else src.with_suffix(".svg")
W, H = 1100, 750

verts, faces = [], []
for line in src.read_text().splitlines():
    if line.startswith("v "):
        verts.append(tuple(map(float, line.split()[1:4])))
    elif line.startswith("f "):
        faces.append([int(t.split("/")[0]) - 1 for t in line.split()[1:4]])

# Camera: yaw 35deg, pitch 25deg, orthographic.
yaw, pitch = math.radians(35), math.radians(-25)
cy, sy = math.cos(yaw), math.sin(yaw)
cp, sp = math.cos(pitch), math.sin(pitch)


def cam(p):
    x, y, z = p
    xz1, zz1 = x * cy - z * sy, x * sy + z * cy      # yaw about Y
    yz, zz = y * cp - zz1 * sp, y * sp + zz1 * cp     # pitch about X
    return xz1, yz, zz                                 # screen x, y, depth


cv = [cam(p) for p in verts]
xs = [c[0] for c in cv]
ys = [c[1] for c in cv]
minx, maxx, miny, maxy = min(xs), max(xs), min(ys), max(ys)
scale = 0.88 * min(W / (maxx - minx + 1e-6), H / (maxy - miny + 1e-6))
ox, oy = (minx + maxx) / 2, (miny + maxy) / 2


def screen(c):
    return (W / 2 + (c[0] - ox) * scale, H / 2 - (c[1] - oy) * scale)


tris = []
for f in faces:
    if max(f) >= len(cv):
        continue
    a, b, c = cv[f[0]], cv[f[1]], cv[f[2]]
    depth = (a[2] + b[2] + c[2]) / 3
    # face normal z (screen space) for a cheap light
    ux, uy = b[0] - a[0], b[1] - a[1]
    vx, vy = c[0] - a[0], c[1] - a[1]
    facing = ux * vy - uy * vx
    tris.append((depth, facing, screen(a), screen(b), screen(c)))

tris.sort(key=lambda t: t[0])  # far first (painter's)
dmin = min(t[0] for t in tris)
dmax = max(t[0] for t in tris)

svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" '
       f'viewBox="0 0 {W} {H}">',
       f'<rect width="{W}" height="{H}" fill="#0e1116"/>']
for depth, facing, a, b, c in tris:
    t = (depth - dmin) / (dmax - dmin + 1e-6)      # 0 near .. 1 far
    shade = 0.35 + 0.65 * (1 - t)                   # nearer = brighter
    lit = 0.55 + 0.45 * (1 if facing > 0 else 0.4)  # crude two-tone
    r = int(90 * shade * lit)
    g = int(150 * shade * lit)
    bl = int(200 * shade * lit)
    svg.append(f'<polygon points="{a[0]:.1f},{a[1]:.1f} {b[0]:.1f},{b[1]:.1f} '
               f'{c[0]:.1f},{c[1]:.1f}" fill="rgb({r},{g},{bl})" '
               f'stroke="rgb({r//2},{g//2},{bl//2})" stroke-width="0.3"/>')
svg.append(f'<text x="18" y="{H-20}" fill="#8aa" font-family="monospace" '
           f'font-size="18">CleriaCore  {src.stem}  '
           f'{len(verts)} verts / {len(faces)} tris</text>')
svg.append("</svg>")
out.write_text("\n".join(svg))
print(f"wrote {out}  ({len(verts)} verts, {len(faces)} tris)")
