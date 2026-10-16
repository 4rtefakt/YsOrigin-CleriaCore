# Developing CleriaCore

Everything a contributor needs that a player does not: building from source on each platform, where the
game data goes, the debug keys and headless modes, the layout of the code, and the running list of what is
still a stand-in. Players start at the [README](../README.md).

The reverse-engineering behind the engine (the notes, the specifications, the decompilation and how much of
the game is covered) lives in the private **[yso](https://github.com/4rtefakt/yso)** repository; its
[PROGRESS.md](https://github.com/4rtefakt/yso/blob/main/PROGRESS.md) is the current measure. The notes under
`build/*_re` here are the working copies it collects.

## The rule

**Reverse-engineer the whole engine from the binary before building on it, and
only wire in what is code-traced.** Every value in the running engine is meant to
be traceable to a disassembled function or a validated data file — the camera, the
fog, the enemy stats, the damage formula, the physics. Where something is still a
stand-in (a hand-picked number, a placeholder behaviour), it is called out as one,
here and in the code, rather than dressed up as fact. The [honesty ledger](#honesty-ledger)
at the bottom is the running list of what is *not* yet real.

## Build

Requires CMake ≥ 3.24 and a C++20 compiler (MSVC 2022 tested).

`third_party/` is gitignored — its contents are fetched, not authored. SDL3 3.4.12
and wgpu-native v29.0.1.1 are prebuilt drops; the third is a single source file:

```sh
curl -L -o third_party/stb/stb_vorbis.c \
  https://raw.githubusercontent.com/nothings/stb/master/stb_vorbis.c   # music decoder, public domain
```

The F1 shell overlay uses Dear ImGui v1.92.9b (MIT): from
`https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.zip`, copy `imgui*.cpp/.h`, `imconfig.h`,
`imstb_*.h`, `LICENSE.txt` and `backends/imgui_impl_{sdl3,wgpu}.{cpp,h}` into `third_party/imgui/`
(same layout).

(The game ships its own `vorbisfile.dll`, but those are 32-bit like `yso_win.exe`,
so an x64 build cannot load them.)

The archive reader inflates with miniz 3.0.2 (MIT): from
`https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip`, copy the
amalgamated `miniz.c`, `miniz.h` and `LICENSE` into `third_party/miniz/`.

```sh
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

`python tools/fetch_deps.py` fetches all of the above (and Lua 5.4.9, the Fira Sans fonts) at those
versions into `third_party/`; it keeps any component already there (`--force` replaces it).

### Linux / macOS

Experimental (issue #46): it builds and passes the asset-free tests in CI
(`.github/workflows/build.yml`: Windows MSVC and Ubuntu gcc + clang on every push; macOS clang on
demand, `gh workflow run build.yml -f macos=true`). Prerequisites:
CMake ≥ 3.24, Ninja, a C++20 gcc / clang, Python 3, and for SDL3 (built from source by the fetch
script) on Ubuntu / Debian:

```sh
sudo apt install ninja-build pkg-config libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev \
  libxi-dev libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev wayland-protocols libegl1-mesa-dev \
  libdrm-dev libgbm-dev libasound2-dev libpulse-dev libudev-dev libdbus-1-dev
# optional: movies (FFmpeg) and the Lua mods' WebSocket (libcurl with WebSockets)
sudo apt install libavformat-dev libavcodec-dev libswscale-dev libavutil-dev libssl-dev
```

On macOS: `brew install cmake ninja pkgconf` (+ `ffmpeg curl` for the options). Then:

```sh
python3 tools/fetch_deps.py                 # SDL3 3.4.12 built into third_party/sdl3, wgpu-native .so / .dylib,
                                            # on Linux a static libcurl 8.11.1 (WebSockets) into third_party/curl
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCLERIA_FFMPEG=ON -DCLERIA_CURL=ON
cmake --build build
ctest --test-dir build -LE assets           # the tests that need no game data
build/cleria-view                           # SDL3 + wgpu-native are copied next to it
```

`SDL3_DIR` / `CMAKE_PREFIX_PATH` point at another SDL3 3.4 install, `-DWGPU_NATIVE_DIR=<dir>` at
another wgpu-native release folder (`include/`, `lib/`). Without `CLERIA_FFMPEG` the demo movies are
skipped (as when retail cannot open one); without `CLERIA_CURL` `cleria.net.websocket` reports
"unsupported on this platform". On Linux `CLERIA_CURL` links the libcurl the fetch script built
(`third_party/curl`, needs `libssl-dev`); without that folder, and on macOS, the installed libcurl, which
must have WebSockets (8.11+ enables them by default, as Homebrew's does; Ubuntu 24.04's 8.5 has none). The `.cleriamod` association is a freedesktop `.desktop` + mime type
under `$XDG_DATA_HOME` on Linux and unavailable on macOS (use `--install-mod <file>`). Settings live in
`$XDG_CONFIG_HOME/CleriaCore` (macOS: `~/Library/Application Support/CleriaCore`), the unpacked data in
`$XDG_DATA_HOME/CleriaCore/data`; `cleria-gamedata` finds the Steam (Proton) install under
`~/.steam/steam`, `~/.local/share/Steam`, Flatpak Steam, or `~/Library/Application Support/Steam`.

## Game data

CleriaCore reads your own copy of Ys Origin. `cleria-gamedata` finds the install and
unpacks its archives (`release/data.ni/.na`, then the English overlay `data_us.ni/.na`
over it) into CleriaCore's data folder — `%LOCALAPPDATA%\CleriaCore\data`, or
`$CLERIA_DATA` — natively, in ~20 s; no Python needed. The install is only read.

```sh
build/Release/cleria-gamedata.exe --find                  # the Steam install(s) it sees
build/Release/cleria-gamedata.exe --install               # unpack the first one found
build/Release/cleria-gamedata.exe --install "D:\SteamLibrary\steamapps\common\Ys Origin" [--out <dir>]
build/Release/cleria-gamedata.exe --status                # none / installing / ready
```

That folder is an `<asset_root>` (the `MAP/`, `ENEMY/` … tree) plus a
`cleria-gamedata.manifest`, written last, that records the install. `cleria-view` uses it
when no asset root is given (after `$CLERIA_ASSETS`, before the legacy `assets/` next to
the exe/repo), and so does `ctest` (`CLERIA_TEST_ASSETS`). Details:
`build/shell_re/GAMEDATA.md`.

Started with no asset root and no game data yet, `cleria-view` opens a setup screen instead: the
installs found in your Steam libraries, a folder dialog or a typed folder, then the same unpack and
straight on into the game (`apps/cleria-view/view_firstrun.inl`).

**Music, movies and the menu font** are loose files in the install's `release/` and are
not copied: through the manifest, an unpacked data folder resolves them from
`<install>/release/music`, `<install>/release/*.avi|*.dat` and
`<install>/release/yso_ins04.dat`. The older routes still work — `CLERIA_MUSIC_DIR`,
`CLERIA_MOVIE_DIR`, `CLERIA_FONT_FILE`, or copies under `<asset_root>/music`,
`<asset_root>/movie`. Movies decode through Windows Media Foundation; a missing file is
skipped like the game's own open failure. `CLERIA_MOVIE=0` turns them off (see
`build/event_re/MOVIES.md`).

(The old route, `tools/extract_assets.py` into `assets/`, still produces an equivalent
tree; see GAMEDATA.md for its one known defect.)

## Controls and debug keys


```sh
build/Release/cleria-view.exe                      # no args -> title screen / S_1000
build/Release/cleria-view.exe <asset_root> <room.SOB>   # jump straight into a room
```

With no room argument it finds the asset tree (`$CLERIA_ASSETS`, then the unpacked data
folder, then `assets/` next to the exe) and boots to the title screen.

The game controls are retail's own bindings, read from CleriaCore's `yso_win.ini`
(`%USERPROFILE%\Saved Games\CleriaCore\`, imported once from the retail game's file; see
`build/shell_re/SHELL_CORE.md`). A gamepad works too. Without a retail file the defaults are
retail's **Preset 1**; retail's **Preset 2** (WASD / mouse) is shown for comparison.

| Action | Preset 1 (default) | Preset 2 | Pad |
|---|---|---|---|
| move | numpad 1–9, arrows (always) | W A S D, Q E Z X diagonals, arrows | left stick / d-pad |
| attack / talk / open; menu **OK** | **Z** | **.** · LMB | Button 1 (A) |
| jump; menu **Cancel** | **X** · RMB | **/** · RMB | Button 2 (B) |
| magic (skill) | C | _ · MMB | Button 3 (X) |
| boost | V · MMB | Space | Button 4 (Y) |
| pause book | Space · **Esc** (always) | Tab · **Esc** | Button 10 (RS click) |
| walk (hold) | L-Shift | L-Shift | Button 9 (LS click) |

The pause book closes with **Cancel**; **Esc** during a cutscene skips it (the window **X** quits).
**F1** is reserved for the settings overlay.

The debug keys are **off by default**: turn them on with F1 > Debug > **Debug keys** (cleria.ini
`[cleria.dev] DebugKeys=1`). F3 always works.

| Debug key | Action |
|---|---|
| **Ctrl+Tab** | switch character (Yunica / Hugo / Toal) |
| **Ctrl+1–9** | take exit N |
| **F** | free-fly debug camera (WASD / E Q, drag to look) |
| mouse wheel | fly / walk speed multiplier |
| **R** | respawn at the room's spawn |
| **T** | test cutscene |
| **F3** | debug overlay |
| **K / L** | −60 HP / full heal |
| **B** | +Boost |

## Headless render modes

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
apps/cleria-gamedata/  find / validate a Ys Origin install, unpack it into the data folder
apps/cleria-dump/  load a room's SOB, export it to .obj
apps/cleria-xso/   disassemble / run scripts; `--grants` scans the corpus
docs/              this guide; the file-format specs moved to yso (formats/)
tools/             Python validators, probes and Ghidra scripts
build/*_re/        raw RE notes per subsystem (source_map, hud_re, attack_re, event_re, …)
```

> **These three sections lag behind.** They were last revised in early October 2026, before floors 4 to 7,
> the remaining monster brains and the last five bosses landed. The current picture, area by area, is the
> "Estimated: by area" table of yso's [PROGRESS.md](https://github.com/4rtefakt/yso/blob/main/PROGRESS.md).

## Status

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
| **Combat: melee combos + projectiles + enemy attacks** | ✅ ASO-driven — see [build/attack_re](../build/attack_re) / [build/combat_re](../build/combat_re) |
| **The real HUD (STATUS_512)** | ✅ HP + damage-lag trail, segmented MP, EX/Boost arc, SP digits, portrait, slots |
| **In-game pause book + menus** | ✅ Escape opens Equip/Item/Topic/Save-Load; assets pixel-matched |
| **Per-room BGM (SCENELIST)** | ✅ each room picks its `.ogg`; loose files via `CLERIA_MUSIC_DIR` |
| Boss stats (EEX) | ⚠️ table cracked; how a boss reaches its record is `[UNKNOWN]` |
| Enemy AI | ⚠️ **stand-in** — chase + ASO attack; the game's ~90 bespoke handlers are not ported |
| The full archive | ✅ 15,945/15,945 extracted |

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

## Honesty ledger

Things known to be a stand-in, unfinished, or unknown — kept here so they don't get
mistaken for facts. (Removed as they are traced and validated.)

| thing | status |
|---|---|
| **the RE corpus itself** | **partly wrong.** The 2026-10-05 audit ([build/audit/README.md](../build/audit/README.md)) found 35% of 371 load-bearing claims wrong and 11% overclaimed. Check a claim there before building on it |
| **enemy AI** | the shared machinery (sight, acquisition, line of sight, action player, walk/run mover, idle wander) and the **M100 Riffligan brain** are the engine's (`src/cleria/enemy_ai.cpp`); every other enemy runs the M100 brain as a stand-in until its own is ported |
| **actor vertical placement** | the player's movement now runs on the engine's floating origin (floor + 1.075, `physics_player.hpp`); sprites, hitboxes and the camera still anchor to a derived `feet` with an INVENTED 2.0 body height until they are re-derived |
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
