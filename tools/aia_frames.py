#!/usr/bin/env python3
"""Decode a spread of body frames from one AIA file to find idle/standing poses.

Body frames are even regions (each frame = body region + optional 2nd-layer region).
Lays them out labeled by region index.
"""
import os, sys
from PIL import Image, ImageDraw
from aia_decode import Aia

path = sys.argv[1]
out = sys.argv[2] if len(sys.argv) > 2 else r'C:\Users\kores\repos\YsOrigin-CleriaCore\build\aia_frames.png'
count = int(sys.argv[3]) if len(sys.argv) > 3 else 24

a = Aia(path)
print("file=%s nImg=%d nReg=%d" % (os.path.basename(path), a.nImg, a.nReg))
for i in range(min(a.nImg, 8)):
    im = a.image(i)
    print("  image[%d]: tag=%#x first=%d nDir=%d nFrame=%d" % (i, im['tag'], im['first'], im['nDir'], im['nFrame']))

cells = []
r = 0
while len(cells) < count and r < a.nReg:
    try:
        W, H, rgba, _ = a.decode_region(r)
        if W > 12 and H > 12:
            cells.append((r, Image.frombytes('RGBA', (W, H), bytes(rgba))))
    except Exception:
        pass
    r += 2  # bodies are even regions

cols = 8
cw, ch = 110, 130
rows = (len(cells) + cols - 1) // cols
sheet = Image.new('RGBA', (cols * cw, rows * ch), (40, 44, 58, 255))
d = ImageDraw.Draw(sheet)
for i, (ri, img) in enumerate(cells):
    cx, cy = (i % cols) * cw, (i // cols) * ch
    s = min((cw - 6) / img.width, (ch - 20) / img.height, 1.4)
    sw, sh = int(img.width * s), int(img.height * s)
    spr = img.resize((sw, sh), Image.NEAREST)
    sheet.alpha_composite(spr, (cx + (cw - sw) // 2, cy + (ch - 18 - sh)))
    d.text((cx + 4, cy + ch - 16), "r%d" % ri, fill=(255, 255, 255, 255))
sheet.save(out)
print("wrote %s (%d frames)" % (out, len(cells)))
