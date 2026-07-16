# AIA — Ys Origin animated sprite container

The 2D actors of the game's **2.5D** world: characters, enemies, NPCs render as
sprite billboards over 3D YMO rooms (only bosses are 3D). Each `.AIA` in
`ANI/` (147 files, + low-res variants in `ANI/L/`) is a palette-indexed,
run-length-encoded sprite sheet holding a set of animations.

Reverse-engineered from `yso_win.exe` (loader `FUN_00444a50`, source
`gfx/ani_dat.cpp`; 32-bit RLE decoder `FUN_00445220`) and validated: the C++
decoder (`src/cleria/aia.cpp`) is **pixel-exact** to the Python reference
(`tools/aia_decode.py`) across every tested frame.

## Container (version 0x0150)

Header — fixed 0x28 bytes:

| Off | Type | Field |
|---|---|---|
| 0x00 | u32 | magic `"AIA\0"` (0x00414941) |
| 0x04 | u16 | **version — loader requires `== 0x0150`** |
| 0x06 | u16 | pad |
| 0x08 | u32 | numPalettes |
| 0x0C | u32 | numImages (animations) |
| 0x10 | u32 | numRegions (frames, in body/layer pairs) |
| 0x14 | u16,u16 | canvas w, h |
| 0x18 | u16,u16 | base tile (64,64) |
| 0x1C | f32 | scale A |
| 0x20 | f32 | scale B |
| 0x24 | u32 | pixel-data byte count (stream runs to EOF) |

Tables follow immediately, back to back:

```
image table   @ 0x28                         numImages  * 16
region table  @ 0x28 + numImages*16          numRegions * 32
palette block @ regtab + numRegions*32       numPalettes * 256 * 4
pixel data    @ palbase + numPalettes*1024   RLE stream (pixelDataSize bytes)
```

**Image entry (16 B):** `u32 animTag, u32 firstRegion, u32 nDir, u32 nFrame`.
A frame's body region = `firstRegion + (nFrame*dir + frame) * 2` — the `*2`
because frames are stored as **pairs**: a body region and an optional second
layer (drawn when the body region's `flags` bit 7 is *clear*).

**Region record (32 B):**

| Off | Type | Field |
|---|---|---|
| 0x00 | u32 | imageOfs — byte offset into the pixel data |
| 0x04 | u16 | palIndex — which 256-color sub-palette |
| 0x06 | u8 | flags — **bit0 set on a body region ⇒ it HAS a second layer** (see below) |
| 0x08 | f32[4] | billboard placement (scale, ox, oy, oz) — constant per file |
| 0x18 | u16×4 | tight atlas bbox `x0,y0,x1,y1` → texture is `(x1-x0)×(y1-y0)` |

### The second layer — the flag is bit **0**, not bit 7

Frames occupy region *pairs*: an even **body** region and an odd **layer** slot.
Whether that slot is real is decided by **bit 0 of the BODY's flags**:

| body flags | meaning | odd slot |
|---|---|---|
| `0x00` | no second layer | placeholder: flags `0x80`, bbox `(canvasW,canvasH,canvasW,canvasH)` ⇒ 0×0, `imageOfs = 0` |
| `0x01` | **has a second layer** | a real region with its own bbox and pixel data |

The `0x80` on the empty slot is what makes "bit7 = no second layer" *look* right —
it is a property of the placeholder, not the test to use. Read **bit 0 of the body**.

**The layer's palette is always the body's palette + 1** — verified across every
file that uses layers (18→19, 28→29, 24→25, 8→9, …, no exceptions). Same artwork,
next palette — a **palette swap**, not a weapon or a separate prop. C_115 (Toal's
Boost/demon form) draws its body in `palIdx 0` and its layer — a dark purple
silhouette of the same pose — in `palIdx 1`.

> **⚠️ What the engine DOES with the layer is [UNKNOWN] — do not assume "overlay".**
> An earlier revision of this section called it a palette-swapped *overlay*. The
> description above is right; that word is not established, and the obvious reading
> is refuted. Reproduce it:
>
> ```
> build/Release/cleria-aiatest.exe assets/.../C_115.AIA 0 c115.ppm --composite
> ```
> Region 0 (body) and region 1 (layer) have the **identical bbox** — `73x99 @(102,13)`
> — and near-identical coverage: **3408 vs 3406** non-transparent pixels. The layer is
> not a sparse glow sitting *on* the body; it is the **whole silhouette**, flat purple.
> Drawing it over the body with straight alpha therefore erases the body entirely,
> which is plainly not what the game renders.
>
> The likelier reading, untested: the layer is a **state variant the engine swaps in**
> (a Boost or hit flash), not something drawn *with* the body. The distribution fits —
> C_115 and M_340/M_370/M_540 are 100% layered, while C_104's layers appear on only
> two special animations (tags 408 and 419) and on **none** of his locomotion, so a
> walking Toal would never show one. Settle this before drawing anything: the renderer
> currently draws the body only, on purpose.

The layer carries its **own bbox**, which may or may not match the body's (C_115:
1599/1728 identical; C_112 and M_340: never identical), so it must be positioned
from its own record rather than assumed coincident.

**Corpus:** 60,844 layer slots examined, **5,906 real (28 files)**. Notably
**C_115 = 1728/1728** and **M_340/M_370 = 100%**, while **C_102 (Yunica's base set)
uses none at all** — which is why a renderer that ignores the layer looks correct
until it loads a gameplay or Boost set.

**Palette entry:** stored `B8 G8 R8 x8`; the loader forces alpha to `0xFF`.
Entry 0 is transparent black, entry 1 white.

A region's compressed blob length is the distance from its `imageOfs` to the
next distinct offset (the last runs to end-of-pixel-data).

## Pixel codec (32-bit path, `FUN_00445220`)

Decoded **row by row**: `W = x1-x0` pixels per row, `H` rows. Each span starts
with a **big-endian u16** control word `w = (b0<<8) | b1`:

| Test on `b0` | Op | Payload | Emit |
|---|---|---|---|
| `& 0x80` | LITERAL | `n = w & 0x0FFF` index bytes | `pal[idx]`, opaque |
| `& 0x40` | ALPHA | `n = w & 0x0FFF` (alpha,index) pairs | `(pal[idx] & 0x00FFFFFF) \| (alpha<<24)` |
| else | SKIP | none | `n = w` transparent pixels |

Pixels fill left→right; a new row begins when x reaches W. The stream ends when
all H rows are filled. (The 16-bit/ARGB4444 variant `FUN_00445380` is identical
but stores `alpha>>4`; the 32-bit path keeps full 8-bit alpha.)

## Identity

Character IDs → file stems live in the exe name table at `0x0068A800`. Prefixes:
`C_` = characters, `M_` = monsters, `F_` = misc. **User-confirmed against the real
game**, the base playable trio is **`C_102` / `C_103` / `C_104` = Yunica / Hugo /
Toal** — these are the normal-play sprites (Yunica: brown hair, tan tunic, plain
sword). `C_110`–`C_115` are late-game / weapon-state variants of the same
characters (e.g. C_111 and C_114 are Yunica with her big glowing red sword;
C_115 is Toal's Boost/demon form). `C_101`/`C_110` (red hair, axe) are Adol —
*not* playable in Origin.

Visual eyeballing of sprites is unreliable for identity — it produced two wrong
calls here (Yunica's red *sword* misread as red *hair*, and a weapon-variant
shipped as the default skin). Trust the name table and in-game comparison.

## Animation tags

For the `C_10x` player sprites, verified by decoding frame spreads:

| image | tag | animation |
|---|---|---|
| 0 | 0 | **idle** (8 frames — it does animate) |
| 1 | 1 | **walk** (8 frames) |
| 3 | 2 | alt stance (8 frames) |
| 2 | 3 | attack / large motion (11 frames) |
| 4+ | 0x190+ | specific actions & skills |

`.ASO` sidecars (loaded separately, `data\ani\%s.aso`) carry per-frame timing and
hitbox metadata — parse those for accurate animation speeds.

## Tools

| Tool | Does |
|---|---|
| `tools/aia_decode.py <f> <region> <out.png>` | reference decoder |
| `tools/aia_sheet.py` | roster contact sheet (first frame of each `C_1*`) |
| `tools/aia_frames.py <f> <out> <n>` | frame spread of one file (find idle poses) |
| `cleria-aiatest <f> [region] [out.ppm]` | C++ decoder + stats (validation) |
| `src/cleria/aia.{hpp,cpp}` | the engine decoder |
