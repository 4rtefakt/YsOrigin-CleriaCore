# CleriaCore

A clean-room reimplementation of the **Ys Origin** engine — a purpose-built
native PC engine that reads the original Falcom data files, in the spirit of
devilutionX and OpenRCT2. It boots to the real title screen, lets you pick a
character, and drops you into the tower with the game's own rooms, cameras,
enemies, HUD, and music — all driven by the shipped data, not by constants we
picked.

> **Why not "a port like Project Dusklight"?** Dusklight is the Twilight Princess
> decompilation compiled natively against a GameCube SDK compatibility layer — it
> exists to cross a PowerPC→x86 boundary and replace console hardware. Ys Origin
> is *already* a native 32-bit x86 Direct3D 9 game, so neither problem exists, and
> there is no Ys Origin decompilation to build on. CleriaCore chases Dusklight's
> **outcomes** — ultrawide, decoupled framerate, no modern-OS friction, a real mod
> API — via the only path that fits: reading the data files and rewriting the
> engine.

## The rule

**Reverse-engineer the whole engine from the binary before building on it, and
only wire in what is code-traced.** Every value in the running engine is meant to
be traceable to a disassembled function or a validated data file — the camera, the
fog, the enemy stats, the damage formula, the physics. Where something is still a
stand-in (a hand-picked number, a placeholder behaviour), it is called out as one,
here and in the code, rather than dressed up as fact. The [honesty ledger](#honesty-ledger)
at the bottom is the running list of what is *not* yet real.

## What it does now

`cleria-view` is a real-time engine on **SDL3 + wgpu-native** (WebGPU →
D3D12/Vulkan/Metal — native, no browser). It currently plays end to end:

- **Title → character select → difficulty → world**, the real menu assets and flow.
- **Rooms** render textured and lit (baked vertex lighting, per-material DDS,
  alpha-test cutouts, UV-scrolling water), with the game's **collision hulls**,
  **authored cameras** (SCM), and **scene fog** (SFO).
- **A grounded player** — walk/run/jump/gravity, wall & floor collision, 8-way
  sprite facing — driven by the character's own **ASO animation scripts** (the
  scripts decide clip, frame timing, and hit-volumes, so the three characters
  differ because their data differs).
- **Combat**: melee swings and combo chains, **projectiles** spawned by the ASO
  (`anime_FireBullet` → real EF2 effects), the disassembled **damage formula**, and
  enemies that **run their own ASO** to wind up and land animated attacks with real
  hitboxes.
- **Enemies** placed from each room's SCT with their **own** level/HP/ATK/DEF from
  ENEMYINFO, and **barrier fights**: kill the script-spawned guards and the chest
  they lock opens (op `0x131 Chr_IsSeedLiving`, wired).
- **The real HUD** at its authored 1024×768 coordinates: HP bar with damage-lag
  trail, the segmented green MP gauge, the EX/Boost arc around the portrait, the
  SP counter, the portrait and equipment-slot frames.
- **An in-game pause book** (Escape) — Equip / Item / Topic / Save-Load pages, the
  real menu art, tab-navigable.
- **Chests** (E) for named loot, with per-character and event gating.
- **Music** (per-room BGM from SCENELIST) and **footsteps** (surface → cadence,
  the exe's own rule).
- **SEN room transitions** — walk through a doorway to warp to the connected room.

Everything above is fed by the game's own files. See the status table for how each
layer was validated, and the ledger for what is still a stand-in.

### Status

| Layer | Status |
|---|---|
| NNI/NA archive (cipher, TOC) | ✅ (from the sibling Archipelago project) |
| **YMO** models + skeleton + animation | ✅ **1006/1006 byte-exact** — rigid FVF *and* the skinned boss path, both drawn |
| **SOB** object placement | ✅ 1129/1129 records |
| **SEN** room graph | ✅ 348 edges, walked at runtime |
| DDS textures / WAV audio | ✅ standard formats |
| Differential harness (C++ vs Python) | ✅ **exact vertex match, 197 rooms** (5.6M verts / 2.2M tris) |
| Real-time renderer (SDL3 + wgpu-native) | ✅ rooms on GPU; play mode + headless `--shot`/`--fps`/`--play` |
| Textured materials (BC1/BC2/RGB565/…) | ✅ per-submesh textures + alpha-test cutouts |
| Boss models + skinning (D3DX indexed palette) | ✅ all 8 render textured, skinned, posed |
| **ASO animation-script VM** | ✅ **115/115 files, 52,280 instructions, 0 decode errors**; drives player *and* enemies |
| Animation clips (parse + pose + play) | ✅ 303/303 stored clip AABBs reproduced |
| AIA character/monster sprites | ✅ codec cracked, pixel-exact; 8-way facing + idle/walk/jump + the additive 2nd layer |
| The game's own camera (SCM) + fog (SFO) | ✅ 659 camera regions (follow/orbit/blend); per-region fog, cross-faded |
| Grounded player controller + physics | ✅ walk/run/jump; closed-form vertical (`dy = 0.5·9.81·t·|t|`), per-cast radius/height/jump |
| Collision hulls (`_.YMO`) | ✅ the game's authored hulls, not render geometry |
| Footsteps (surface + cadence + audio) | ✅ hull material → surface → `f_stone0..3`, exe's tick cadence |
| **Enemy stats / curves / levels / drops** | ✅ **externally validated: 67/68 match the monster journal** on HP/level/STR/DEF; drop tables corpus-proven |
| **Damage formula** | ✅ both directions re-verified 2026-10-05 and tested (`src/cleria/hit.hpp`, `cleria-combattest`): player→enemy `FUN_004fa550` (resist multiplier, min 1), enemy→player `FUN_004fa740`, probabilistic rounding |
| Equipment → stats | ✅ INVINFO DEF column + GAMEINFO weapon-ATK table (there is no ATK column in INVINFO) |
| Difficulty | ✅ five settings (a 3-valued index + two booleans), effects `[C-DIS]` |
| **XSO event VM** | ✅ flags/tests/give-item/acquire-box/compare/show-hide, **spawn `0x12c`**, **alive `0x13f`**, **barrier `0x131`** |
| **Combat: melee combos + projectiles + enemy attacks** | ✅ ASO-driven — see [build/attack_re](build/attack_re) / [build/combat_re](build/combat_re) |
| **The real HUD (STATUS_512)** | ✅ HP + damage-lag trail, segmented MP, EX/Boost arc, SP digits, portrait, slots |
| **In-game pause book + menus** | ✅ Escape opens Equip/Item/Topic/Save-Load; assets pixel-matched |
| **Per-room BGM (SCENELIST)** | ✅ each room picks its `.ogg`; loose files via `CLERIA_MUSIC_DIR` |
| Boss stats (EEX) | ⚠️ table cracked; how a boss reaches its record is `[UNKNOWN]` |
| Enemy AI | ⚠️ **stand-in** — chase + ASO attack; the game's ~90 bespoke handlers are not ported |
| The full archive | ✅ 15,945/15,945 extracted |

## Build

Requires CMake ≥ 3.24 and a C++20 compiler (MSVC 2022 tested).

`third_party/` is gitignored — its contents are fetched, not authored. SDL3 3.4.12
and wgpu-native v29.0.1.1 are prebuilt drops; the third is a single source file:

```sh
curl -L -o third_party/stb/stb_vorbis.c \
  https://raw.githubusercontent.com/nothings/stb/master/stb_vorbis.c   # music decoder, public domain
```

(The game ships its own `vorbisfile.dll`, but those are 32-bit like `yso_win.exe`,
so an x64 build cannot load them.)

```sh
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### Assets

`<asset_root>` is a tree of extracted assets (the `MAP/`, `ENEMY/` … tree). Extract
**both** archives — `data.ni` then `data_us.ni`, in that order: the US overlay
overwrites 525 scripts and the English text.

```sh
python tools/extract_assets.py <install>/data.ni    assets
python tools/extract_assets.py <install>/data_us.ni assets   # overlay, second
```

**Music** is not in those archives — the tracks are loose `.ogg` files in the
install's `release/music/`. Point CleriaCore at them with `CLERIA_MUSIC_DIR`, or put
them where it already looks (`<asset_root>/music`, e.g. `mklink /J assets\music
"<install>\release\music"`). Without it everything still runs, silently.

## Play

```sh
build/Release/cleria-view.exe                      # no args -> title screen / S_1000
build/Release/cleria-view.exe <asset_root> <room.SOB>   # jump straight into a room
```

With no room argument it finds the asset tree (`$CLERIA_ASSETS`, then next to the
exe) and boots to the title screen.

| Control | Action |
|---|---|
| **WASD** / arrows | move (8-way, camera-relative) |
| **LMB** | attack (melee / combo) |
| **Space** | jump |
| **E** | interact / open chest |
| **Tab** | switch character (Yunica / Hugo / Toal) |
| **Escape** | open/close the pause book — or **skip a playing cutscene** (the window **X** quits) |
| **Backspace** | skip a playing cutscene |
| **Q / E** | previous / next page in the pause book |
| **1–9** | take exit N |
| **F** | free-fly debug camera |
| mouse wheel | fly speed |
| **R** | respawn at the room's spawn |
| **K / L** | *(debug)* −60 HP / full heal |
| **B** | *(debug)* +Boost |

### Headless render modes

`--shot`, `--play` and `--fps` render offscreen to a PPM (verifiable with no
display attached — the interactive window needs one). `--fps` draws the HUD, which
is the fast way to eyeball HUD changes without a live session.

```sh
build/Release/cleria-view.exe <asset_root> <room.SOB> --shot out.ppm 1280 860
build/Release/cleria-view.exe <asset_root> <room.SOB> --fps  hud.ppm 1280 860
build/Release/cleria-view.exe <asset_root> <room.SOB> --walk 6 tour     # headless N-room tour
build/Release/cleria-view.exe <asset_root> <file.ymo> --model out.ppm   # orbit any model
```

A handful of `CLERIA_*` env switches drive diagnostics (`CLERIA_MAT`, `CLERIA_VB`,
`CLERIA_SURF`, `CLERIA_STEPS`, `CLERIA_ENEMY`, `CLERIA_CHAR`, `CLERIA_LEVEL`, …) —
grep `std::getenv` in `apps/cleria-view/main.cpp` for the current set.

## Layout

```
src/cleria/        asset core — ymo, sob, sen, scm (cameras), sfo (fog), sct
                   (actors), sef (effects), aia (sprites), aso/xso (VMs), world, physics
apps/cleria-view/  the engine: real-time renderer + play mode + HUD
apps/cleria-dump/  load a room's SOB, export it to .obj
apps/cleria-xso/   disassemble / run scripts; `--grants` scans the corpus
docs/formats/      the reverse-engineering specs (YMO, AIA, ASO, XSO, SIDECARS)
tools/             Python validators, probes and Ghidra scripts
build/*_re/        raw RE notes per subsystem (source_map, hud_re, attack_re, event_re, …)
```

## How much of the game is actually reversed?

The exe has 6,708 functions, but **4,399 of them (66%) are not game code** — the CRT,
3,025 Steamworks/GOG delay-load stubs, and 2,198 import thunks. The Falcom-authored
body is `0040e150`–`005eeae0`: **2,097 functions, 1.74 MB**. That is the real
denominator, and it is what the coverage metric (`tools/re_coverage.py`) measures.

> **Caveat (2026-10-05 audit):** that metric counts a function as "documented" when its
> address appears in any doc. Sampled by code size, only **~16%** of engine code is explained
> well enough to reimplement; ~77% has a known role. See `build/audit/engine_arch.md`.

We can be precise about *where* code lives because the binary's assert strings leak
Falcom's own source tree — 37 `__FILE__` paths under
`i:\ys_origin\...\yso_win\src\`, and the `main/` translation units are laid out **in
alphabetical order by source path**. That turns any address into a directory and an
alphabetical *range* the true filename must lie in — a bracket, not a name (asserts
are sparse; `lib/` doesn't obey the law). The still-largest untouched target is the
`main/object` gap `00465170`–`00501a30` (305 functions, 24.7% of `.text`) — the
actor/enemy/battle heart, bracketed between `go_actor_player.cpp` and `go_mesh.cpp`,
and the source of the enemy-AI stand-in. See `build/srcmap_re/`.

## Roadmap

- **M0 — asset pipeline** ✅
- **M1 — render + walk** ✅: renderer, textures + alpha, baked lighting, SEN room
  transitions, grounded controller. Ultrawide falls out for free.
- **M2 — scripts** ✅: the XSO event VM — parser, decoder, control flow, the flag /
  test / give-item / acquire-box / compare / show-hide ops, plus the actor ops
  (`0x12c` spawn, `0x13f` alive, `0x131` barrier). Chests, HUD, character select and
  per-character chest gating are wired.
- **M3 — combat & entities** *(largely in)*: authored camera (SCM), fog (SFO), the
  real HUD, AIA sprites, enemies from SCT with real stats, torch flames, ASO-driven
  **melee combos, projectiles and enemy attacks**, the disassembled **damage
  formula**, and **barrier fights** (op `0x131`, now wired — the earlier "blocked on
  a live is-that-enemy-alive answer" is resolved). *Open:* enemy AI is a chase +
  attack stand-in, not the game's ~90 bespoke handlers; hitstun/interrupt and combo
  chaining have known bugs (see the ledger).
- **M4 — the mod API**: the Archipelago mod becomes the first CleriaCore mod.

### Honesty ledger

Things known to be a stand-in, unfinished, or unknown — kept here so they don't get
mistaken for facts. (Removed as they are traced and validated.)

| thing | status |
|---|---|
| **the RE corpus itself** | **partly wrong.** The 2026-10-05 audit ([build/audit/README.md](build/audit/README.md)) found 35% of 371 load-bearing claims wrong and 11% overclaimed. Check a claim there before building on it |
| **enemy AI** | **stand-in.** Chase (at walk speed — the engine promotes to run) + ASO attack; the 25-unit activation gate is applied to every enemy but only ~8 of 90 handlers have it. The ~90 bespoke brains are not ported, and their docs never pinned the per-cast constants |
| **actor vertical placement** | **origin treated as feet**; the engine floats the collision centre at floor + `+0x324` (1.075 for the player) with a 4-tick coyote grace |
| **Boost** | per-hit gains and the ×0.5 incoming are now the engine's (hit.hpp); the passive per-tick fill and the 60-i-frame entry are not yet, and the gauge only charges once story flag 187 is set |
| **EF2 effects** | **single flipbook quad per emitter.** The engine samples 17 curve tracks per particle per tick (speed/alpha/colour/spin/scale); orientation and blend modes are partly heuristic |
| **chest gating** | **inverted** for some rooms — op `0x169` deletes the named objects, the code treats them as the ones to show |
| **cutscene runtime** | opt-in (`CLERIA_EVENTS=1`); `Chr_MoveTo` speed argument misread; speaker names missing on lines with an empty name operand; `$I/$F/$S` script variables unhandled |
| **items** | granted into `item_arr` (that is the item-level array); the engine grants by setting the item's flag |
| **HUD details** | HP-lag rate 0.006 (engine: static 0.02); bars stretched instead of cropped; buff/combo timers, magic icon and world popups not drawn |
| **`HUD %3d/%3d` HP counter font** | drawn with F_MINI (font_A); the engine uses font_B, a runtime GDI glyph font we don't build yet |
| **equip STAT aggregation** | base `curve + weapon + armor/foot + ring` is `[C-DIS]`; the permanent +1..+7 residuals in real saves are the statue blessings (0x76bc80 is the blessing bitset) |
| **boss stats (EEX) lookup** | `eex_index_for_cast` is our invention; the doc's lookup chain was wrong (the name match is in the mesh factory `FUN_00501a30`) |
| **bosses in the viewer** | rendered/skinned/posed and AABB-validated, but nothing drives them per-tick in play yet |
| **`BOSS06A/B`, `PIC_TOGE`, `BOSS01_` clip AABBs** | don't reproduce (Δ1.3–11.4) while 59 others are exact — suspected 2nd animation path (`type != 0`), never observed |

## Credit

Built on the reverse-engineering foundation of the sibling **Ys Origin
Archipelago** project (NNI cipher, XSO event-VM, item/scene maps, Ghidra work).
