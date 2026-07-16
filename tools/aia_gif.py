#!/usr/bin/env python3
"""Render an animated GIF grid of every animation in an AIA sprite file.

Each cell plays one animation on loop, labelled with its image index + tag, so a
human can identify them at a glance (idle / walk / run / attack / ...). Reading
animations off a static frame sheet is unreliable — watch them move instead.

    python aia_gif.py <file.aia> <out.gif> [dir] [cols]

`dir` is the facing to render (default 6 = front view on the C_10x player ring).
"""
import os
import sys

from PIL import Image, ImageDraw

from aia_decode import Aia

path = sys.argv[1]
out = sys.argv[2]
want_dir = int(sys.argv[3]) if len(sys.argv) > 3 else 6
COLS = int(sys.argv[4]) if len(sys.argv) > 4 else 6

a = Aia(path)
anims = []
for i in range(a.nImg):
    im = a.image(i)
    d = want_dir if im["nDir"] > want_dir else 0
    frames = []
    for f in range(im["nFrame"]):
        region = im["first"] + (im["nFrame"] * d + f) * 2
        try:
            W, H, rgba, _ = a.decode_region(region)
            if W > 4 and H > 4:
                frames.append(Image.frombytes("RGBA", (W, H), bytes(rgba)))
        except Exception:
            pass
    if frames:
        anims.append((i, im["tag"], frames))

if not anims:
    sys.exit("no animations decoded from %s" % path)

CW, CH = 120, 140
rows = (len(anims) + COLS - 1) // COLS
NF = 24  # gif frames; each cell cycles its own animation independently
stem = os.path.splitext(os.path.basename(path))[0]

gif = []
for t in range(NF):
    sheet = Image.new("RGB", (COLS * CW, rows * CH), (58, 68, 82))
    d = ImageDraw.Draw(sheet)
    for k, (idx, tag, frames) in enumerate(anims):
        cx, cy = (k % COLS) * CW, (k // COLS) * CH
        img = frames[t % len(frames)]
        s = min((CW - 10) / img.width, (CH - 24) / img.height, 1.4)
        sw, sh = max(1, int(img.width * s)), max(1, int(img.height * s))
        spr = img.resize((sw, sh), Image.NEAREST)
        sheet.paste(spr, (cx + (CW - sw) // 2, cy + CH - 22 - sh), spr)
        d.text((cx + 4, cy + CH - 18), "img%d tag=%#x %dfr" % (idx, tag, len(frames)),
               fill=(255, 235, 120))
    gif.append(sheet)

gif[0].save(out, save_all=True, append_images=gif[1:], duration=100, loop=0)
print("%s: %d animations, dir %d -> %s" % (stem, len(anims), want_dir, out))
