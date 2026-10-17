---
name: menu-screen
description: Implement or pixel-match a CleriaCore menu/UI screen (title, char-select, pause-book tabs, popups, shop) against a real-game reference grab, using the real Falcom DDS assets. Use when building a new menu screen or fixing one to match reference/*.png.
---

# CleriaCore menu-screen implementation

All menu drawing lives in `apps/cleria-view/main.cpp` in `struct TitleFx` (+ `MenuFont`, `draw_screen_image`).
The single source of truth for values is yso's `notes/menu/MENU_IMPLEMENTATION.md` (`../yso` beside this checkout; the RE notes moved there, `build/README.md`) — read it first, and update it after.
Reference grabs are gitignored in `reference/*.png` (real-game screenshots = ground truth). **Never guess an
asset — extract the real DDS and confirm** (the user has repeatedly insisted on this).

## The loop
1. **Look at the reference** (`reference/<screen>.png`, 2559×1439). Identify each element + which real asset draws it.
2. **Find/extract the asset.** Assets are in `assets/MENU/` and `assets/IMAGE/`. Decode a DDS with PIL
   (`Image.open(path).convert("RGBA")`), composite over a grey bg to see alpha, and **measure its content
   bounds** (`alpha>25`) — textures often have baked black padding, or pack several sprites (buttons in two
   dim/lit columns, icons off to one side). Crop the UV to just the piece you want. A gridded overlay (lines
   every 64px, labels every 128) reads sub-rect coords fast.
3. **Measure the reference layout** into authored coords (see model below) with a quick numpy script.
4. **Draw** with `draw_screen_image(g, cview, dview, clear, tex, l,t,r,b, z, u0,v0,u1,v1, bgr,bgg,bgb, wtint)`
   — positions in NDC, UVs are the sub-rect. Text via `mf.text(s, px_left, py_top, pxH, sw,sh, r,g,b,a)` /
   `mf.text_centered`; flush once per frame with `mf.draw(g, cview)`.
5. **Add an offscreen mode** (`render_title` `mode==N` + a `--<name>` CLI arg) so you can render headless:
   `cleria-view assets _ --<name> out.ppm 2559 1439`. Then pixel-compare the PPM against the reference
   (stack them, view via Read). Iterate coords until it matches. **This works with displays off**; the
   interactive SDL window does not — so verify screens offscreen, and leave in-game wiring for the user to test.
6. Full build (`cmake --build build`), refresh `build/titletest6/`, update MENU_IMPLEMENTATION.md, commit + push.

## Coordinate model (the #1 pitfall)
- **Pillar-boxed screens** (title, char-select, all pause-book pages) use an **authored 1024×768 4:3 field**,
  fit-to-height and centred in 16:9:
  `fwN=(4/3)*sh/sw; AX(x)=(2x/1024-1)*fwN; AY(y)=1-2y/768; SX(x)=(AX(x)+1)*0.5*sw; SY(y)=y/768*sh;`
  The goddess side-art (`ALTPILLAR16-9`, drawn full-screen v-crop 0.749) fills the letterbox margins.
- **Reference → authored** (for measuring `reference/*.png`, size W×H): `ax=(sx/W-0.125)/0.75*1024`,
  `ay=sy/H*768`. (The 4:3 field is the centre 75% of the 16:9 width.)
- **Full-screen screens** (the Bonus Shop) are NOT pillar-boxed — use direct screen fractions (`0.44f*sw` …).

## Recurring pitfalls
- **Plaque cartouches** (OPTION/EQUIP/INVENTORY/TOPIC.DDS, 256×128, two states): the gold cartouche is only
  **u0..0.598 of the upper half** (v0..0.492); the rest is opaque black padding — crop or you draw a black box.
- **`wtint` only lightens** (mixes toward white); it can't darken. To "dim vs lit", lighten the focused one.
- **Sampler is `AddressMode_Repeat`** — a UV that runs past a sprite into an adjacent one (or a gap) shows a
  stray stub. Clamp UVs to the sprite's true content edge (measure it).
- **Surface/atlas format is linear `RGBA8Unorm`**, not sRGB (sRGB double-encodes → washout). The font atlas is
  rasterised at the 960-internal em size then bilinear-upscaled (matches the game's soft 2D overlay).
- **Item icons**: `ItemTable::icon_path(asset_root,id)` → `MENU/ICON/<stem>.DDS`; cache uploaded textures
  (a `std::unordered_map<int,SpriteTex> iconCache`) — don't reload per frame.
- **Stats/data are real**: `GameInfo` curves (`curves[kPlayerMaxHp/Def].at(lvl)`, `player_atk`, `exp_to_level`)
  and `ItemTable` (name/desc) — use them, only stub true runtime state (equipped items, inventory, gold) that
  CleriaCore doesn't track yet, and say so.
- **Region mean-abs-diff under-weights small high-contrast features** (cursor, glyph colour, drop-shadow) — a
  blind pixel-diff loop will optimise them wrong. Sample specific features + trust the eye for those.
- **Additive-blend glows** (e.g. TOPIC_CUR teal wash) can't be done per-draw yet — draw behind, or add an
  additive pipeline. Note the limitation rather than covering baked labels with an opaque glow.

## RE gaps
When layout/state isn't in an existing `build/*_re/*_out.txt`, dump the function with the **`ghidra-decompile`
skill** and cite the `FUN_`/`DAT_` addresses. Extract the real asset; don't invent one.
