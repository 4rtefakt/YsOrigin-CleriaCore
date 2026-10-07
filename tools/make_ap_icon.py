#!/usr/bin/env python3
"""CleriaCore's foreign-item icon for Archipelago (build/shell_re/AP_GAME.md §5): res/ap/AP_ITEM.DDS.

Drawn from scratch here (nothing copied from any logo): a night-blue disc with a gold rim, five
coloured "islands" in a ring - the colours the AP text model gives item classes (plum, slate blue,
salmon, cyan) and locations (green) - linked to a pale centre, i.e. a small connected archipelago.
The same geometry is drawn live by ap_draw_logo() in apps/cleria-view/view_archipelago.inl (the feed
and the tab). 64 x 64 like the game's MENU/ICON/*.DDS, uncompressed A8R8G8B8, which load_dds reads.

    python tools/make_ap_icon.py [--png preview.png]
"""
import argparse
import math
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "res", "ap", "AP_ITEM.DDS")
N = 64
SS = 4   # supersampling per axis

# Geometry in units of the icon size (1.0 = 64 px), centre (0.5, 0.5). Kept in step with ap_draw_logo.
DISC_R, RIM_W = 0.47, 0.045
RING_R, ISLAND_R, CORE_R, LINK_W = 0.265, 0.1, 0.075, 0.035
NAVY, GOLD, CORE, LINK = (27, 34, 64), (214, 176, 62), (244, 240, 230), (196, 204, 232)
ISLANDS = [(0xAF, 0x99, 0xEF), (0x6D, 0x8B, 0xE8), (0xFA, 0x80, 0x72), (0x00, 0xEE, 0xEE), (0x00, 0xFF, 0x7F)]


def island_centres():
    return [(0.5 + RING_R * math.cos(math.radians(-90 + 72 * k)), 0.5 + RING_R * math.sin(math.radians(-90 + 72 * k)))
            for k in range(5)]


def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - ax - t * dx, py - ay - t * dy)


def sample(x, y):
    """The colour (r, g, b, a) at icon coordinates x, y in [0, 1]."""
    d = math.hypot(x - 0.5, y - 0.5)
    if d > DISC_R:
        return (0, 0, 0, 0)
    col = GOLD if d > DISC_R - RIM_W else NAVY
    cs = island_centres()
    for (cx, cy) in cs:
        if seg_dist(x, y, 0.5, 0.5, cx, cy) < LINK_W / 2:
            col = LINK
    for (cx, cy), c in zip(cs, ISLANDS):
        dd = math.hypot(x - cx, y - cy)
        if dd < ISLAND_R:
            col = c if dd < ISLAND_R - 0.02 else (20, 24, 44)   # a thin dark outline
    if d < CORE_R:
        col = CORE
    return col + (255,)


def render():
    px = []
    for j in range(N):
        for i in range(N):
            acc = [0, 0, 0, 0]
            for sj in range(SS):
                for si in range(SS):
                    r, g, b, a = sample((i + (si + 0.5) / SS) / N, (j + (sj + 0.5) / SS) / N)
                    acc[0] += r * a; acc[1] += g * a; acc[2] += b * a; acc[3] += a
            a = acc[3] / (SS * SS)
            if acc[3]:
                px.append((round(acc[0] / acc[3]), round(acc[1] / acc[3]), round(acc[2] / acc[3]), round(a)))
            else:
                px.append((0, 0, 0, 0))
    return px


def write_dds(path, px):
    hdr = struct.pack("<4sIIIIIII44x", b"DDS ", 124, 0x100F, N, N, N * 4, 0, 0)
    pf = struct.pack("<IIIIIIII", 32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    caps = struct.pack("<IIIII", 0x1000, 0, 0, 0, 0)
    body = b"".join(struct.pack("<BBBB", b, g, r, a) for (r, g, b, a) in px)
    with open(path, "wb") as f:
        f.write(hdr + pf + caps + body)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=OUT)
    ap.add_argument("--png", help="also write a PNG preview (needs PIL)")
    a = ap.parse_args()
    px = render()
    write_dds(a.out, px)
    if a.png:
        from PIL import Image
        im = Image.new("RGBA", (N, N))
        im.putdata(px)
        im.resize((N * 4, N * 4), Image.NEAREST).save(a.png)
    print("make_ap_icon: %s" % os.path.relpath(a.out, ROOT))


if __name__ == "__main__":
    main()
