#!/usr/bin/env python3
"""Decode one standing frame from each C_1xx character AIA into a labeled sheet."""
import glob, os, sys
from PIL import Image, ImageDraw
from aia_decode import Aia

ANI = r'C:\Users\kores\repos\YsOrigin-CleriaCore\assets\ANI'
OUT = r'C:\Users\kores\repos\YsOrigin-CleriaCore\build\aia_roster.png'

files = sorted(glob.glob(os.path.join(ANI, 'C_1*.AIA')))
cells = []
for f in files:
    stem = os.path.splitext(os.path.basename(f))[0]
    try:
        a = Aia(f)
        # region 0 is the first frame of the first anim (a clean standing/idle pose)
        W, H, rgba, _ = a.decode_region(0)
        img = Image.frombytes('RGBA', (W, H), bytes(rgba))
    except Exception as e:
        print("skip %s: %s" % (stem, e)); continue
    cells.append((stem, img))
    print("%s: %dx%d" % (stem, W, H))

if not cells:
    sys.exit("no sprites decoded")

# Layout: grid, each cell padded, sprite bottom-aligned, label under it.
cols = 6
cw, ch = 96, 150
rows = (len(cells) + cols - 1) // cols
sheet = Image.new('RGBA', (cols * cw, rows * ch), (40, 44, 58, 255))
d = ImageDraw.Draw(sheet)
for i, (stem, img) in enumerate(cells):
    cx = (i % cols) * cw
    cy = (i // cols) * ch
    # scale up 2x for visibility, keep within cell
    s = min((cw - 8) / img.width, (ch - 26) / img.height, 2.0)
    sw, sh = int(img.width * s), int(img.height * s)
    spr = img.resize((sw, sh), Image.NEAREST)
    px = cx + (cw - sw) // 2
    py = cy + (ch - 22 - sh)
    sheet.alpha_composite(spr, (px, py))
    d.text((cx + 4, cy + ch - 18), stem, fill=(255, 255, 255, 255))

sheet.save(OUT)
print("wrote %s (%d chars)" % (OUT, len(cells)))
