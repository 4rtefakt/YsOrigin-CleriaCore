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

### The second layer — an ADDITIVE OVERLAY, and bit 0 marks the pair

Frames occupy region *pairs*: an even **body** region and an odd **layer** slot.

**`[C-COR]` Bit 0 sits on BOTH members of a pair — 5906/5906.** It marks *the pair*,
not the body; testing "bit 0 of the body" happens to work but describes the format
wrongly. `0x80` marks *an empty region*, and — the part that kills the old reading —
**1022 BODIES are themselves `0x80` placeholders**, so `0x80` is not a layer-slot
property at all. Full histogram of `flags`, all 60,844 slots:

| flags | bodies | layers | meaning |
|---|---|---|---|
| `0x00` | 53,916 | 53,916 | region present, unpaired |
| `0x01` | 5,906 | 5,906 | **member of a body+layer pair** |
| `0x80` | 1,022 | 1,022 | **empty region** (bbox `(canvasW,canvasH,…)` ⇒ 0×0, `imageOfs = 0`) |

Byte `0x07` is `0` in all 60,844 — **there is no blend-mode field in the region
record.** The blend is global to the sprite path, not per-region.

**`[C-COR]` It is an OVERLAY: a second, independent sprite element with its own
position, own artwork, and own palette, drawn *with* the body.** The decisive fact is
that **the layer's bbox moves independently of the body's** — which a state variant
cannot do:

| file | tag | layer bbox over successive frames | what it is |
|---|---|---|---|
| `M_460` | 300 | `(173,115)→(178,128)→(178,143)→(178,158)`, 27–95 px | a **falling spark**, tracking down while the body stands still |
| `M_910` | 7 | `(52,75)→(73,69)→(103,64)→(95,67)→(82,70)` | **projectile orbs**, flying out and returning |
| `C_114` | 268 | `(119,28)→…→(11,35)→(29,77)→(159,73)` | a **flaming sword effect** sweeping the canvas |

`build/aia_re/strip_c114.png` shows it outright: body-only is Yunica's plain blade;
body+layer is **the blade wreathed in fire**, tracking the swing.

**`[C-COR]` "Same artwork, next palette — a palette swap" is FALSE.** `sameBlob =
0/5906`: the layer's compressed blob is *never* the body's. The content is different
art — a flame, orbs, a slash arc, a flash. Only the palette-index relation survives:
**`layerPal == bodyPal + 1`, 5906/5906**, no exceptions.

Layer presence is **per-frame and direction-independent**, appearing in contiguous
runs and strobes — `M_850` tag 303 is `..#..#..#.....` identically across all 8
directions. It is an authored effect envelope, not a state.

**`[H-high]` The blend is ADDITIVE, not straight alpha.** Layers carry ~7× the
opaque-black pixels bodies do (`build/aia_re/black_test.py`):

| file | body opaque-black | layer opaque-black |
|---|---|---|
| `C_104.AIA` | 0.2% | 11.5% |
| `C_112.AIA` | 0.6% | **22.6%** (38×) |
| `M_860.AIA` | 3.7% | 19.5% |
| **total** | **1.3%** | **8.8%** |

Under straight-alpha OVER an opaque black pixel punches a black hole; **under
additive it is the identity — free.** Authoring 22.6% of an overlay's opaque pixels
as pure black only makes sense against an additive blend. `comp_c115.png` agrees
perceptually: `over` erases the body, `add` yields a purple-glowing Toal.

> **`[UNKNOWN]` The exact blend mode still needs disassembly.** The evidence above is
> strong but circumstantial; the actual render state lives in the exe's 2D sprite
> draw call (`SetRenderState(D3DRS_SRCBLEND/DESTBLEND)`), not in the AIA. Expect
> `SRCALPHA/ONE` or `ONE/ONE` — **C_104's flash is the discriminating frame**
> (whiteout under `add`, subtle under `add × alpha`).

> **Retracted: "the obvious reading is refuted; the layer is a state variant."** An
> earlier revision of this section said exactly that, on the strength of one file:
> C_115, whose layer has the body's identical bbox (`73x99 @(102,13)`) and near-
> identical coverage (3408 vs 3406), so compositing it with straight alpha erased the
> body. Both observations are true and the conclusion was still wrong twice over. The
> erasure was evidence about the **blend mode**, not about whether to draw it — and
> C_115 is the corpus's **most atypical file**: 100% layered, full-silhouette aura,
> and the **only file whose layers contain zero ALPHA opcodes**. Only **1983/5906
> (34%)** of layers share their body's bbox. One weird file is not the corpus, and a
> composite that looks wrong indicts the compositor first.

The layer carries its **own bbox**, which may or may not match the body's (C_115:
1599/1728 identical; C_112 and M_340: never identical), so it must be positioned
from its own record rather than assumed coincident.

**Corpus:** 60,844 layer slots examined, **5,906 real (28 files)**. Notably
**C_115 = 1728/1728** and **M_340/M_370 = 100%**, while **C_102 (Yunica's base set)
uses none at all** — which is why a renderer that ignores the layer looks correct
until it loads a gameplay or Boost set.

A **negative** result worth keeping, since it is the kind that quietly gets dropped:
if layers were additive glows, their palettes ought to be luminance ramps from black.
**They are not** — only 14/94 (15%) are near-monotonic. Entry 0 is black in 94/94,
but that holds for body palettes too (a codec convention), so it proves nothing
either way. This does *not* support the additive reading; the case for it rests on
the opaque-black distribution above.

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
