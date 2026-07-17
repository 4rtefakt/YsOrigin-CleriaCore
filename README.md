# CleriaCore

A clean-room reimplementation of the **Ys Origin** engine — a purpose-built
native PC engine that reads the original Falcom data files, in the spirit of
devilutionX and OpenRCT2.

> **Why not "a port like Project Dusklight"?** Dusklight is the Twilight Princess
> decompilation compiled natively against a GameCube SDK compatibility layer — it
> exists to cross a PowerPC→x86 boundary and replace console hardware. Ys Origin
> is *already* a native 32-bit x86 Direct3D 9 game, so neither problem exists, and
> there is no Ys Origin decompilation to build on. CleriaCore chases Dusklight's
> **outcomes** — ultrawide, decoupled framerate, no modern-OS friction, a real mod
> API — via the only path that fits: reading the data files and rewriting the
> engine.

## Status

The entire **static-room asset pipeline is reverse-engineered and validated**,
and a C++20 asset core renders real rooms.

| Layer | Status |
|---|---|
| NNI/NA archive (cipher, TOC) | ✅ (from the sibling Archipelago project) |
| **YMO** models + skeleton + animation | ✅ **1006/1006 byte-exact** (rigid FVF path; skinning specced, not drawn) |
| **SOB** object placement | ✅ 1129/1129 records |
| **SEN** room graph | ✅ 348 edges, 86.8% reciprocal |
| DDS textures / WAV audio | ✅ standard formats, free |
| C++20 asset core + OBJ exporter | ✅ builds `/W4` clean |
| Differential harness (C++ vs Python) | ✅ **exact match, 197 rooms** |
| **Real-time renderer (SDL3 + wgpu-native)** | ✅ **rooms render on GPU; orbit window + headless `--shot`** |
| **Textured materials (DDS: BC1/BC2/RGB565/…)** | ✅ **per-submesh material textures + alpha-test cutouts** |
| **Boss models + skinning (D3DX indexed palette)** | ✅ **all 8 render textured, skinned and posed — BOSS01 coils, BOSS04/08 assemble** |
| **ASO animation-script VM** | ✅ **the game's own scripts drive the player: 115/115 files, 52,280 instructions, 0 decode errors; every opcode and call count matches the RE to the unit** |
| **Animation clips (parse + pose + play)** | ✅ **270/303 stored clip AABBs reproduced to ≤1e-3 — the 33 misses are the 5 known-outlier files, matching an independent Python pass to 5 s.f. `CLERIA_CLIP`/`CLERIA_FRAME` pose any skinned model** |
| **Free-fly walkthrough + SEN room transitions** | ✅ **fly through doorways to warp between connected rooms** |
| **Baked vertex lighting** | ✅ **per-vertex DIFFUSE (lava glow, ambient) — the Falcom "lightmap"** |
| **Grounded player controller** | ✅ **walk/jump/gravity + wall & floor collision; F toggles fly** |
| **The game's own collision hulls (`_.YMO`)** | ✅ **396 tris vs 14,295 render in S_1000 — matches the authored SCT spawn in 7 more rooms than render geometry, 0 regressions** |
| **Footsteps (surface + cadence + EFFECTDEF + SDL3 audio)** | ✅ **fully the game's rule now** — hull material name → surface → `f_stone0..3`, and the cadence is the exe's tick countdown (walk 20 / run 13, gated on the ASO section id) |
| **AIA** character/monster sprites | ✅ **codec cracked, pixel-exact; 8-way facing + idle/walk/jump** |
| **The game's own camera (SCM)** | ✅ **659 authored camera regions, follow + orbit + blend — no hand-tuning** |
| **Scene fog (SFO)** | ✅ **per-region colour/near/far, cross-faded as the game does** |
| **The real HUD (STATUS_512)** | ✅ **authored 1024×768 coords, Hor+ scaling, the game's own digit emitter (builder A), real bar cluster, 4 equipment slots** |
| **Fixed-step 60 Hz logic** | ✅ **decoupled from rendering — measured 165 fps / 60 tps**; timeScale-aware, uncapped catch-up like the exe |
| **Enemy drop tables (ENEMYINFO)** | ✅ **8 slots/enemy: item id + percent chance, corpus-proven — enemies drop only tools and gold, never equipment** |
| **Enemies (SCT placement)** | ✅ **1106 placements / 85 rooms render as sprites; no AI yet** |
| **Torch flames (SEF + EF2)** | ✅ **placements + the 16-frame flipbook, additive** |
| **The full archive** | ✅ **15,945/15,945 extracted — incl. 77 shaders + 820 WAVs nothing had touched** |

The differential test is the headline: two independent implementations of the
whole pipeline agree to the vertex — **5,659,727 vertices / 2,183,191 triangles**
across every room in the game.

### What YMO turned out to be

A serialized DirectX `.X` mesh hierarchy: `D3DXFRAME` nodes, `D3DXMESHCONTAINER`
meshes stored as **raw D3D9 FVF vertex buffers + `D3DFMT_INDEX16` triangle
lists**, and per-node scale/rotation/translation animation keys. See
[docs/formats/YMO.md](docs/formats/YMO.md).

> **Rooms and props use legacy FVFs (`0x1D2`/`0x2D2`) with rigid node binding, and
> that path is validated bit-exact. But that is not the whole format.** The bosses
> under `ENEMY/` use `fvf = 0` — a D3D9 **vertex declaration**, and the declarations
> are not in the file: they are eight static `D3DVERTEXELEMENT9` arrays inside the
> exe, chosen by a shader table. **Ys Origin does skin**, via D3DX indexed-palette
> blending (`_1w`…`_4w` = influence counts, `decl = 3 + N`), so the "no skinning"
> claim this README used to make was simply wrong. We don't draw bosses yet, but the
> format is no longer a mystery — see [docs/formats/YMO.md](docs/formats/YMO.md).

## Layout

```
src/cleria/        asset core — ymo, sob, sen, scm (cameras), sfo (fog), sct
                   (actors), sef (effects), aia (sprites), xso (script VM), physics
apps/cleria-view/  the engine: real-time renderer + play mode
apps/cleria-dump/  load a room's SOB, export it to .obj
apps/cleria-xso/   disassemble / run scripts; `--grants` scans the corpus
docs/formats/      the reverse-engineering specs (YMO, AIA, XSO, SIDECARS)
tools/             Python validators, probes and Ghidra scripts
build/*_re/        raw RE notes per subsystem (source_map, ef2_re, xso_ops, …)
```

## Build

Requires CMake ≥ 3.24 and a C++20 compiler (MSVC 2022 tested).

```sh
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## Run

`cleria-dump` needs a directory of extracted assets (the `MAP/`, `ENEMY/` … tree
produced by the Archipelago project's `ni_unpack.py`).

```sh
build/Release/cleria-dump.exe <asset_root> <asset_root>/MAP/S_00/S_0000/S_0000.SOB out.obj
python tools/render_png.py out.obj out.png    # quick-look raster (Pillow)
```

## Render

`cleria-view` draws a room in real time via **SDL3 + wgpu-native** (WebGPU →
D3D12/Vulkan/Metal). Native library, no browser.

It plays the game's way: the camera, fog, enemy placement and spawn point all come
from the room's own sidecars, not from constants we picked.

```sh
build/Release/cleria-view.exe                                         # no args -> S_1000
build/Release/cleria-view.exe <asset_root> <room.SOB>                 # a specific room
build/Release/cleria-view.exe <asset_root> <room.SOB> --shot out.ppm 1280 860
build/Release/cleria-view.exe <asset_root> <room.SOB> --walk 6 tour   # headless tour
```

With no arguments it finds the asset tree (`$CLERIA_ASSETS`, then next to the exe)
and starts in the tower base (S_1000) — just run the exe.

In the window: **WASD** moves the character (8-way, camera-relative), **LMB** attacks,
**Space** jumps, **E** opens chests, **Tab** switches character, **1–9** take exit N,
**F** drops to a free-fly debug camera, **ESC** quits. Fly into a doorway (a SEN trigger volume) to warp
to the connected room. `--shot` renders one frame offscreen to a PPM (verifiable
headless); `--walk N` auto-traverses N connected rooms, shooting each. Prebuilt
deps live under `third_party/` (gitignored) — SDL3 3.4.12 + wgpu-native v29.0.1.1.

`<asset_root>` is a tree of extracted assets (`tools/extract_assets.py <data.ni> assets`).
Extract **both** archives — `data.ni` then `data_us.ni`, in that order: the US
overlay overwrites 525 scripts and the English text.

### Debug switches

| switch | does |
|---|---|
| `--model <file.ymo> out.ppm` | orbit-shot any standalone model (bosses, props) |
| `CLERIA_SPAWN="x,y,z"` | drop the character somewhere specific |
| `CLERIA_CLIP=<name>` `CLERIA_FRAME=<n>` | pose a skinned model at an animation frame (`--model`); list clips with `cleria-animtest <f.ymo> --clips` |
| `CLERIA_SKIP=<substr>` | omit SOB objects whose path/name matches — bisect artifacts |
| `CLERIA_COLL=render` | collide against render geometry again (pre-`_.YMO` behaviour) |
| `CLERIA_NOSCROLL=1` | disable the water UV-scroll integrator |
| `CLERIA_SCROLL_TICKS=<n>` | pre-advance the UV-scroll phase by n ticks before a `--play` shot (that path renders one frame and never ticks, so the water is otherwise frozen at phase 0) |
| `CLERIA_COLLCHECK=1` | grid-compare the collision hulls against the render mesh |
| `CLERIA_SURF=1` | per-surface triangle counts, split by floor-like vs wall |
| `CLERIA_STEPS=1` | trace each footstep: surface → table id → WAV |
| `CLERIA_VB=1` | print each mesh's vertex format; flags buffers with no UV0 |
| `CLERIA_MAT=1` | dump every material: flags, blend mode, diffuse, UV-scroll velocity, all 3 texture slots |
| `CLERIA_CULL=off\|cw\|ccw` | probe face culling (D3D9's `D3DCULL_CCW` does not map directly to WebGPU winding) |
| `build/Release/cleria-animtest.exe assets` | reproduce every stored clip AABB from our own animation + skinning |
| `python tools/xso_audit.py` | count script branches our VM would get wrong at runtime |
| `python tools/sct_enemies.py <ROOM>` | list a room's enemies + positions + sprites |
| `python tools/aia_gif.py <f.aia> <o.gif>` | animation contact sheet, for identifying anims |

> **If the interactive window segfaults with no output, check that a display is attached.**
> SDL cannot create a window/swapchain with no active display, so `cleria-view` dies at
> startup. `--shot` / `--play` / `--model` render offscreen and are unaffected — use them
> for headless verification. (Diagnosed the slow way once: an old commit crashed identically
> despite having run fine an hour earlier. Same binary, different result = environment.)

## Roadmap

- **M0 — asset pipeline** ✅
- **M1 — render + walk** ✅: real-time renderer, textures + alpha, baked lighting,
  SEN room transitions, and a grounded player controller (walk/jump/collide).
  Ultrawide falls out for free (aspect-correct resizable window).
- **M2 — scripts** ✅: the XSO event VM — parser, decoder, control flow, flag /
  test / give-item / acquire-box ops, the comparison family, and object show/hide
  (static grant scan: **7,505** — an earlier 7,496 was measured before the archives
  were fully extracted, when `assets/` was a JP/US mix; `data_us.ni` overwrites 525
  scripts and the US build grants 9 more flags). Integrated into play: **open chests** (E) for named loot, an
  **on-screen HUD**, **character selection** (Tab: Yunica/Hugo/Toal), and
  **per-character chest gating** — the scene-main script hides the wrong
  character's chests (model + interactable). Presentation-heavy ops (cutscene
  camera, entity animation, dialog) drive systems that belong to M3. See
  [docs/formats/XSO.md](docs/formats/XSO.md).
- **M3 — combat & entities** *(in progress)*: the game's **authored camera** (SCM),
  **scene fog** (SFO), the **real HUD** at its authored coordinates, **character
  sprites** (AIA) with 8-way facing and idle/walk/jump, **enemies** placed from SCT
  (1106 across 85 rooms), and **torch flames** from SEF + the EF2 flipbook. All of it
  driven by the game's own data — the camera constants we hand-tuned for a day were
  thrown away the moment SCM was decoded.
  **Blocked on the entity system:** enemies have no AI (the game's is ~90 bespoke C++
  routines, not a table), and **barrier fights** need a live "is that enemy alive?"
  answer — `KEKKAIZAKO_01.XSO` polls op `0x131` in a loop, so no constant can fake it.
- **M4 — the mod API**: the Archipelago mod becomes the first CleriaCore mod.

### Honesty ledger

Things we know are still invented or unknown, kept here so they don't get mistaken
for facts:

| thing | status |
|---|---|
| enemy HP / damage / reach | **our numbers still, but the data is now mapped.** GAMEINFO holds an 11-entry (not 7) curve array — idx 10 = (150, 18150), the obvious HP candidate — and ENEMYINFO carries a six-wide log2 stat block. What's missing is the damage *formula* and the level denominator; an agent is on it |
| YMO animation keys | **doc was wrong until now**: the scale/translation labels were swapped (the correct composition reproduces the rest pose in 5832/7016 nodes; the old labelling in **0**), and the key's trailing field is a `u32` frame number, not a float — the old "monotonic time" test passed only because denormal ordering matches integer ordering |
| animation frame rate | **fixed — the game's own scripts drive it now.** The ASO VM holds each `anime_Pattern` for its own `anime_Wait(n)`. No constants left |
| player sprite choice | **now the script's, not ours.** `C_010.ASO` gates `anime_File(6)`=C_102 behind `Flag_IsEqualInt(161,1)` and falls through to `anime_File(15)`=**C_111**. With flag 161 clear (normal play) the game uses **C_111/C_112/C_113** — the gameplay sets. We had hardcoded C_102/103/104, which are the **cutscene** sets |
| HUD `[EX]`/`[MP]`/`[SP]` labels + `60/ 60` | **not drawn yet.** The `f_mini` font and its exe-derived metrics are in (`minifont.hpp`), but nothing calls them. `60/ 60` needs text system B, whose font object is built at runtime — its constructor is an [H-medium] lead (`FUN_005a3850`, a 1024×256 buffer) |
| footstep `+0x4b9` surface latch | **the last hop is open.** Everything from the actor's latched surface byte onward is proven; what fills it (`probe+0xAA`, from the ground raycast `FUN_0041e420`) is reached through an indirect callback in the physics lib and wasn't traced statically. It affects *which* surface, never *when* a step fires. A watchpoint on `EDX+0xAA` after `0x0045b03a` would settle it |
| collision surfaces not in the table | **resolved.** `default`/`door`/`10switch`/`dafault` aren't in the 21-entry table, and the exe clamps an unmatched lookup to **1 = `stone`** (`FUN_005610e0`: both the not-found jump and the `idx<0` test land on `MOV ESI,1`). We do the same now |
| water animation | **found — it's the material, not the shader.** Each material carries a UV-scroll velocity at `+0x44/+0x48` (`20zwater3` = +0.0006,-0.0018) gated by flag bit `0x4`; the shader can't animate itself (its phase input has no time term). We parse it now; the per-frame integrator that advances the phase is still unlocated |
| bosses | **rendered, skinned, clips parsed and validated — but not yet played in the viewer.** `Model::pose()` works and is verified against 303 stored AABBs; nothing drives it per-tick yet |
| `BOSS06A/B`, `PIC_TOGE`, `BOSS01_` clip AABBs | **don't reproduce** (Δ1.3–11.4) while the other 59 files are exact. Suspected second animation path (`type != 0`), which is [UNKNOWN] — never observed, since `type` is 0 for all 28,161 tracks |
| AIA second layer | decoded exactly, **deliberately not drawn.** What it *is* isn't established: for C_115 the layer has the same bbox as the body and near-identical coverage (3408 vs 3406 px) — it's the whole silhouette in flat purple, not an aura, so compositing it erases the body. Likely a state variant (Boost/hit flash). See [AIA.md](docs/formats/AIA.md) |
| 300 XSO branch sites | run `tools/xso_audit.py` — unimplemented sub-ops leave `acc` stale, so those branches are wrong at runtime |

## Credit

Built on the reverse-engineering foundation of the sibling **Ys Origin
Archipelago** project (NNI cipher, XSO event-VM, item/scene maps, Ghidra work).
