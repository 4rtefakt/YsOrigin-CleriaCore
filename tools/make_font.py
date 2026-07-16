"""Bake a monospace ASCII font atlas to a raw RGBA blob for CleriaCore's HUD.

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

FONT = "C:/Windows/Fonts/consola.ttf"
SIZE = 28
FIRST, LAST = 32, 126
COLS = 16

out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("res/hud_font.bin")
out.parent.mkdir(parents=True, exist_ok=True)

font = ImageFont.truetype(FONT, SIZE)
asc, desc = font.getmetrics()
cell_h = asc + desc
cell_w = font.getbbox("M")[2] + 2  # monospace advance (+pad)
n = LAST - FIRST + 1
rows = (n + COLS - 1) // COLS
W, H = COLS * cell_w, rows * cell_h

img = Image.new("RGBA", (W, H), (255, 255, 255, 0))
d = ImageDraw.Draw(img)
for i in range(n):
    c = chr(FIRST + i)
    cx, cy = (i % COLS) * cell_w, (i // COLS) * cell_h
    d.text((cx + 1, cy), c, font=font, fill=(255, 255, 255, 255))

# A solid white cell (index `n`) for drawing filled rects (HUD bars). The HUD
# samples this cell's centre for opaque quads.
sx, sy = (n % COLS) * cell_w, (n // COLS) * cell_h
d.rectangle([sx, sy, sx + cell_w - 1, sy + cell_h - 1], fill=(255, 255, 255, 255))

hdr = struct.pack("<8I", 0x544E4F46, W, H, cell_w, cell_h, FIRST, n, COLS)
out.write_bytes(hdr + img.tobytes())
print(f"wrote {out}: {W}x{H}, cell {cell_w}x{cell_h}, {n} glyphs")
