"""Rasterize a cleria-dump .obj to a PNG with a Z-buffer and flat shading.

Software renderer, Pillow only. Orthographic 3/4 camera. For eyeballing a
reconstructed room and for a visual regression baseline.
"""
import math
import sys
from pathlib import Path

from PIL import Image

src = Path(sys.argv[1])
out = Path(sys.argv[2]) if len(sys.argv) > 2 else src.with_suffix(".png")
W, H = 1280, 860

verts, faces = [], []
for line in src.read_text().splitlines():
    if line.startswith("v "):
        verts.append(tuple(map(float, line.split()[1:4])))
    elif line.startswith("f "):
        faces.append([int(t.split("/")[0]) - 1 for t in line.split()[1:4]])

yaw, pitch = math.radians(35), math.radians(-26)
cy, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)


def cam(p):
    x, y, z = p
    x1, z1 = x * cy - z * sy, x * sy + z * cy
    y2, z2 = y * cp - z1 * sp, y * sp + z1 * cp
    return x1, y2, z2


cv = [cam(p) for p in verts]
xs = [c[0] for c in cv]
ys = [c[1] for c in cv]
sc = 0.9 * min(W / (max(xs) - min(xs) + 1e-6), H / (max(ys) - min(ys) + 1e-6))
ox, oy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2


def scr(c):
    return (W / 2 + (c[0] - ox) * sc, H / 2 - (c[1] - oy) * sc, c[2])


zmin = min(c[2] for c in cv)
zmax = max(c[2] for c in cv)
img = Image.new("RGB", (W, H), (14, 17, 22))
px = img.load()
zbuf = [[1e30] * W for _ in range(H)]


def edge(ax, ay, bx, by, cx, cy):
    return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax)


drawn = 0
for f in faces:
    if max(f) >= len(cv):
        continue
    a, b, c = scr(cv[f[0]]), scr(cv[f[1]]), scr(cv[f[2]])
    area = edge(a[0], a[1], b[0], b[1], c[0], c[1])
    if abs(area) < 1e-6:
        continue
    # crude lambert-ish: brighter facing camera + nearer
    facing = 0.55 + 0.45 * (1.0 if area > 0 else 0.45)
    minx = max(0, int(min(a[0], b[0], c[0])))
    maxx = min(W - 1, int(max(a[0], b[0], c[0])) + 1)
    miny = max(0, int(min(a[1], b[1], c[1])))
    maxy = min(H - 1, int(max(a[1], b[1], c[1])) + 1)
    for y in range(miny, maxy + 1):
        for x in range(minx, maxx + 1):
            w0 = edge(b[0], b[1], c[0], c[1], x + 0.5, y + 0.5)
            w1 = edge(c[0], c[1], a[0], a[1], x + 0.5, y + 0.5)
            w2 = edge(a[0], a[1], b[0], b[1], x + 0.5, y + 0.5)
            if (w0 >= 0 and w1 >= 0 and w2 >= 0) or (w0 <= 0 and w1 <= 0 and w2 <= 0):
                z = (w0 * a[2] + w1 * b[2] + w2 * c[2]) / area
                if z < zbuf[y][x]:
                    zbuf[y][x] = z
                    t = (z - zmin) / (zmax - zmin + 1e-6)
                    sh = (0.4 + 0.6 * (1 - t)) * facing
                    px[x, y] = (int(95 * sh), int(155 * sh), int(205 * sh))
    drawn += 1

img.save(out)
print(f"wrote {out}  ({len(verts)} verts, {drawn} tris rasterized)")
