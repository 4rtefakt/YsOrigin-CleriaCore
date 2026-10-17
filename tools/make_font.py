"""Bake a monospace ASCII font atlas to a raw RGBA blob for CleriaCore's HUD.

    python tools/make_font.py <FiraMono-Medium.ttf> [res/hud_font.bin]

The font is Fira Mono (SIL Open Font License 1.1, res/hud_font-OFL.txt), from google/fonts:
https://raw.githubusercontent.com/google/fonts/5e8a3ba899557829a76cfdac30fa512bda91d7ca/ofl/firamono/FiraMono-Medium.ttf
The cell stays 17x29 (the size the HUD was laid out with), and the font size is the largest that fits it.

Output res/hud_font.bin:
    u32 magic 'FONT'  u32 width  u32 height
    u32 cell_w  u32 cell_h  u32 first_char  u32 num_chars  u32 cols
    RGBA8[width*height]   (white glyphs, transparent background)

Glyph for char c sits at index (c-first_char); cell col = idx%cols, row = idx//cols.
No per-glyph metrics needed — monospace.
"""
import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

CELL_W, CELL_H = 17, 29
FIRST, LAST = 32, 126
COLS = 16

if len(sys.argv) < 2:
    sys.exit(__doc__)
ttf = sys.argv[1]
out = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("res/hud_font.bin")
out.parent.mkdir(parents=True, exist_ok=True)

def fits(size):
    f = ImageFont.truetype(ttf, size)
    asc, desc = f.getmetrics()
    return f.getlength("M") + 2 <= CELL_W and asc + desc <= CELL_H

size = max(s for s in range(8, 64) if fits(s))
font = ImageFont.truetype(ttf, size)
asc, desc = font.getmetrics()
cell_w, cell_h = CELL_W, CELL_H
top = (cell_h - asc - desc) // 2
n = LAST - FIRST + 1
rows = (n + COLS - 1) // COLS
W, H = COLS * cell_w, rows * cell_h

img = Image.new("RGBA", (W, H), (255, 255, 255, 0))
d = ImageDraw.Draw(img)
left = (cell_w - font.getlength("M")) / 2
for i in range(n):
    c = chr(FIRST + i)
    cx, cy = (i % COLS) * cell_w, (i // COLS) * cell_h
    d.text((cx + left, cy + top), c, font=font, fill=(255, 255, 255, 255))

# A solid white cell (index `n`) for drawing filled rects (HUD bars). The HUD
# samples this cell's centre for opaque quads.
sx, sy = (n % COLS) * cell_w, (n // COLS) * cell_h
d.rectangle([sx, sy, sx + cell_w - 1, sy + cell_h - 1], fill=(255, 255, 255, 255))

hdr = struct.pack("<8I", 0x544E4F46, W, H, cell_w, cell_h, FIRST, n, COLS)
out.write_bytes(hdr + img.tobytes())
print(f"wrote {out}: {W}x{H}, cell {cell_w}x{cell_h}, {n} glyphs, {size} px")
