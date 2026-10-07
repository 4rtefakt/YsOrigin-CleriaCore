# Mods: packages, the virtual file system, content patches, Archipelago

CleriaCore's mod system. The game stays a faithful port; mods are CleriaCore's own layer (like the
F1 shell). Nothing here changes retail behaviour while no mod is active: with `UseMods=0`, no mod
turned on, or no settings file (every headless test run), the resolver hands back the exact path
the game built and the frame is bit-identical (§9).

Dusklight is the reference for the feel (a mods folder, a list with toggles and an order, clear
errors). The aim goes further: content additions (items, chests, flags, text) and an Archipelago
client built into the engine, in phases.

| phase | what | state |
|---|---|---|
| 1 | package format, discovery, manifest, order + checks, persistence, the VFS (reskins), the Mods tab, `mods::active()`, a sample mod, tests | **built** (this document's §1-§6, §9) |
| 2a | content patches: item text / stats, chest contents, new pickups, flag/event rules, text; the save sidecar, the event bus, the grant filter | **built** (CONTENT.md; §7 is the design it follows) |
| 2b | Archipelago: connection, location checks, item receipt, foreign-item icon, DeathLink, tracker, overlays | designed (§8) |
| 3 | scripting runtime: Lua 5.4 (the user's choice, §10) | **built** (LUA_API.md; mod API 3, `Script=`) |

Evidence tags as elsewhere: **[CODE]** = read in CleriaCore's code, **[DATA]** = checked on the
files, **[H]** = a choice or inference of this design.

---

## 1. The package

A mod is a folder, or a `.zip` of one, in the **mods folder**:

```
<mods>/checker_floor/
    mod.ini                                  the manifest (required)
    files/MAP/S_10/COMMON/H/10YUKA04.DDS     replacement files, by game path (§3)
    files/music/YSO_005.ogg                  ... including the loose install folders
    content/...                              phase 2: data patches (§7)
```

- **The mods folder** is `<the settings folder>/mods`: by default
  `%USERPROFILE%\Saved Games\CleriaCore\mods` (next to `yso_win.ini` / `cleria.ini`), or
  `$CLERIA_MODS_DIR`. With `CLERIA_INI=<x>\yso_win.ini` it is `<x>\mods`, which keeps test runs
  in their scratch folder. [H] Next to the settings rather than the data folder: mods are the
  player's choices, like the settings; the data folder is a disposable unpack of the install.
- **Folders**: every direct sub-folder holding a `mod.ini`. Other folders and files are ignored,
  as are names starting with `.`.
- **Zips**: every `*.zip`. It is extracted once into `<mods>/.cache/<zip stem>/` and re-extracted
  when the zip's size or write time changes (a `.stamp` file records them). `mod.ini` may sit at
  the zip root or inside exactly one top-level folder (what "Send to > Compressed folder" makes).
  An entry with a `..` component, an absolute path or a drive letter refuses the whole zip
  (nothing is written outside the cache). miniz 3.0.2 (already vendored) reads the zip from
  memory, so non-ASCII paths work. [H] Extraction rather than reading from the zip at every open:
  every loader takes a path (§3), and the cost is paid once.

### 1.1 `mod.ini`

The same INI dialect as `cleria.ini` (`src/cleria/settings` `Ini`: case-insensitive names, `;`
comments). One section, `[mod]`. Values may be quoted; unlike retail's `get_string`, an
unquoted value is kept whole.

| key | | meaning |
|---|---|---|
| `Id` | required | `[a-z0-9._-]`, max 64, lowercased on read. Unique; `author.modname` recommended. The id is what `cleria.ini` stores. |
| `Name` | | display name (default: the id) |
| `Version` | | dotted, compared numerically per part (`1.10 > 1.9`, `1.0 == 1`); a non-numeric part compares as text. Default `0`. |
| `Author`, `Description`, `Website` | | shown in the Mods tab |
| `Priority` | | int, default 0: where a NEWLY found mod is first placed (lower loads first). Never reorders what the player ordered. |
| `ApiVersion` | | the mod API it targets (`mods::kApiVersion`, now 3: 2 = `content/`, CONTENT.md; 3 = `Script=`, LUA_API.md). A higher one is an error ("needs a newer CleriaCore"); a mod declaring 1 has its `content/` ignored, one declaring 1 or 2 its `Script=`. Default: the current version. |
| `Script` | | a Lua entry file (`main.lua`), API 3 (LUA_API.md) |
| `Requires` | | comma list of `id`, `id>=1.2`, `id=2`, `id<3` (`>= > = == <= <`) |
| `Conflicts` | | same syntax; an enabled matching mod is an error on the declaring mod |

Problems (no `Id`, an invalid id, a bad list entry, an unreadable manifest or zip) put the mod in
`Error` whether or not it is turned on; it is listed with the reason.

## 2. Load order, choices, checks (`src/cleria/mods.{hpp,cpp}`)

- **Order**: one list. **A mod lower in the list loads later and wins** a file both provide (the
  usual "last wins" of mod managers). The player's order is kept; mods it does not name are
  appended by (`Priority`, id, source name); mods without a readable id go last.
- **New mods start disabled** [H]: dropping a folder in never changes the game until the player
  turns it on (and a speedrun or an Archipelago seed cannot be altered by a stray folder).
- **Persistence**: `cleria.ini`

  ```ini
  [cleria.mods]
  UseMods=1                                      ; 0 = unmodded, whatever is turned on
  Order="sample.checker_floor,test.zip_demo"     ; every known id, load order
  Active="sample.checker_floor"                  ; the enabled ids
  ```
  (`Settings::Cleria::use_mods / mod_order / mod_active`; round trip in `cleria-settingstest`.)
- **Checks** (`mods::arrange`, run to a fixed point over the enabled mods): a duplicate id (the
  second is in error), `ApiVersion` too new, a `Requires` entry missing / turned off / of the
  wrong version / itself in error, an enabled `Conflicts` match. A mod in error does not load; a
  mod that needs it fails in turn. A turned-off mod is not checked.
- **File conflicts** (two active mods providing one path) are not errors: the lower one wins and
  both get a note ("1 file(s) replaced by a.floor" / "replaces 1 file(s) of b.floor"), printed at
  startup and shown in the tab.
- **When it applies**: at startup only. `render_window` calls `mods_startup` right after
  `settings_init`, before the first asset load; the set is then fixed for the session
  (`mods::active()`). Tab edits are saved at once and the status row says "restart to apply".
  [H] No live re-mount: textures, the HUD atlas, fonts, the title, audio banks are loaded once and
  cached in many places; a room reload would apply only part of a change. Restart is honest.

## 3. The virtual file system (`src/cleria/vfs.{hpp,cpp}`)

The game reads an unpacked asset tree (the data folder or `assets/`, GAMEDATA.md §7-8) plus loose
install folders. Each root is **mounted** under a virtual prefix; a mod's `files/` folder is a
**layer** using the same virtual names:

| prefix | mounted at | example virtual name |
|---|---|---|
| `""` | the asset root | `MAP/S_10/COMMON/H/10YUKA04.DDS`, `IMAGE/STATUS_512.DDS`, `MISC/INVINFO.DAT` |
| `music` | `BgmDefs::dir()` (`<install>\release\music`, `<root>/music`, ...) | `music/YSO_001.ogg`, `music/bgmdef.txt` |
| `movie` | the folder `find_movie(root, 0)` found | `movie/yso_ins01.dat` |

`vfs::resolve(p)` takes the real path a loader built (`asset_root / "MAP" / ...`), maps it to its
virtual name through the mounts (the most specific mount folder first), and returns the highest
layer's file for that name, else `p` unchanged. Names compare like the archives: case-insensitive
ASCII, `/` and `\` equivalent, `..` normalised. With no layers it returns `p` before doing any
work; it never touches the disk (the name table is built once by `set_layers`).

**The one choke point.** Every asset open goes through it [CODE]; each call site changed by
wrapping the path, nothing else:

| site | what it reads |
|---|---|
| `dds.cpp load_dds` | every texture (rooms, models, HUD, menus, effects, the title) |
| `apps/cleria-view/main.cpp read_bytes` | XSO scripts, SOB, SFO/SCM/SEF/SCT, YMO, EFT meshes, fonts, the book's files (every `read_bytes` caller) |
| `scene.cpp` (`read_file`, the script slurp, the YMO size probe) | room SOB/YMO/XSO during `build_room` |
| `view_gpu_room.inl` SEN | room exits |
| `aia, aso, cast, eex, ef2, effectdef, enemies, gameinfo, items, minifont, sev, scenelist` loaders | the binary tables (AIA, ASO, CASTINFO, EEX, EF2, EFFECTDEF, ENEMYINFO, GAMEINFO, INVINFO, the HUD mini font, SEV, SCENELIST) |
| `locus.cpp` | `EFFECT/SWORD*.SL2` (the sword trails) |
| `audio.cpp` `SDL_LoadWAV` / `stb_vorbis_open_filename` | sound effects (WAV), music (OGG) |
| `bgmdef.cpp` | `bgmdef.txt`; `track()` uses `vfs::exists`, so a mod can add a track name |
| `movie.cpp` | the demo movies (AVI / `.dat`) |

Not routed, on purpose: CleriaCore's own resources (`res/hud_font.bin`, the Fira fonts, the
launcher), saves and settings, `CLERIA_WND_BG` (a test input), and `yso_ins04.dat` (the Felghana
menu font, found by `find_felghana`; add a `font` mount in phase 2 if a mod needs it).

What a layer can do now:
- **Replace** any file the game opens, in any format the loader accepts: a DDS must be DXT1, DXT3
  or uncompressed 16/24/32-bit (`dds.cpp`; no DXT5, no DX10 header), sounds WAV (SDL), music Ogg
  Vorbis, scripts / tables in the game's own binary formats. Texture names resolve through
  `Gpu::dds_index`, which prefers the `H/` (high-resolution) copy: override the `H/` file.
- **Add** files: `vfs::exists` / `resolve` see a new name under a mount, and `mods_startup` adds a
  mod's NEW `.dds` / `.sob` names to `dds_index` / `sob_index`. A loader that first checks
  `fs::exists` on the base path (room sidecars SFO/SCM/SEF/SCT) does not see new names yet; those
  sites switch to `vfs::exists` when phase 2 needs new rooms.

## 4. The Mods tab (F1 panel, also from the launcher's SETTINGS)

`apps/cleria-view/view_mods.inl`, registered with one line in `view_game.inl`:

- **Status**: "N mods active" (what this session loaded), "Mods off", or "... restart to apply"
  (gold) when the saved choices differ from the loaded set.
- **Installed mods**: one row per mod: its load-order number, name, version, and On / Off /
  Error. OK or a click toggles it (turning one on also turns `UseMods` on). Left / Right, or the
  two arrows on the row, move it up / down. The right pane shows name, version, author,
  description, id, file count, source folder / zip, requirements, conflicts, every error and
  file-conflict note, and the controls.
- **Options**: Use mods (toggle), Open mods folder (`ShellExecuteW`; it creates the folder first;
  a `CLERIA_DRIVE` run only prints), Rescan, and the folder path.
- A run without a settings file (headless tests) shows "Unavailable in this run".
- `CLERIA_MODS_UI="toggle=<id>;up=<id>;down=<id>"`: the tab's own actions, applied once after the
  startup load, for headless checks (they persist like clicks).

## 5. API for other features

```cpp
namespace cleria::mods {
const std::vector<Info>& active();   // {id, name, version} of the loaded mods, load order; empty = unmodded
std::string active_summary();        // "none" or "Checker Floor 1.0.0, Other 2.1"
}
namespace cleria::vfs {
std::filesystem::path resolve(const std::filesystem::path&);   // wrap any new asset open in this
bool exists(const std::filesystem::path&);
std::vector<std::uint8_t> read(const std::filesystem::path&);
std::filesystem::path locate(std::string_view virtual_name);  // "IMAGE/X.DDS" -> the real file
}
```

- **Speedrun mode** (built in parallel): read `mods::active()` once after `render_window`'s
  startup; show "Modded: <active_summary()>" on the timer / the run's summary and mark such runs
  as not comparable. It is fixed for the session, so a mid-run tab edit cannot change it.
- **Any new loader** must open assets through `vfs::resolve` (or `vfs::read`), or mods will not
  reach it.

## 6. The sample mod

`samples/mods/checker_floor/`, generated from scratch by `tools/make_sample_mods.py` (no game
file is read): `mod.ini` (`sample.checker_floor`, "Checker Floor" 1.0.0) and two 64x64
A8R8G8B8 checkerboards replacing `MAP/S_10/COMMON/H/10YUKA04.DDS` (gold / blue) and
`10YUKA14.DDS` (red / cream), the floor textures `S_1009.YMO` names [DATA]. `--zip <dir>` also
writes `checker_floor.zip` (the same mod as a zip). To install: copy the folder or the zip into
the mods folder, turn it on in the tab, restart.

## 7. Content additions (phase 2a, design)

The data-driven path first (§10). A mod adds a `content/` folder of INI files; they are applied
in load order after the vfs is built (a later mod's entry for the same key wins, with a note).
`ApiVersion=2` mods may use them.

### 7.1 What the code has today [CODE]

- **Items**: `MISC/INVINFO.DAT`, **128 fixed records** x 0xb8 (`items.hpp`): name +0x00, magnitude
  +0x24 (DEF for armour / footwear), icon stem +0x34 -> `MENU/ICON/<STEM>.DDS` and
  `MENU/ICON2/<STEM>V.DDS` (the greyed copy), description +0x4c. The record index is the item id.
- **Ownership is the game flags**: `g_flags[0..127]` = count per item id (-1 never had, 0 used up,
  n held), `g_flags[151..153]` = equipped weapon / armour / footwear (`inventory.hpp`). The save
  (`save.hpp`) is a fixed 0x1FC8-byte image of the retail game-state block, so it has no room for
  new ids. The Equip grid (22 cells / character, table 0x68ed70) and the Item page (30 cells,
  0x68ed58) are fixed tables; the four accessory stat bonuses are hardcoded (FUN_004208f0).
- **Chests** are SOB objects with an action script: `Interactable{name ("box_01"), script (the
  XSO), opened_flag (the g_flags index the guard tests), script_name, i0, i1, f0}` (`scene.hpp`).
  The script grants with `Flag_SetInt` / `Flag_AddInt` on the item's count cell (the VM records
  each write as an `XsoVm::Grant{op, index, value}`), shows the item with `0x116 Effect_GetItem(icon,
  flag, mesh)` (`XsoHost::effect_get_item`) and the "obtained" box with `0xD5 Window_Treasure(x, y,
  item, text)` (`XsoHost::acquire_item`). The opened state is the box flag (e.g. S_1001/S_BOX01 =
  flag 0x12E). World pickups are `pickup.hpp` (`make_chest_item`, the FUN_00585ff0 touch rules).
- **Text**: dialogue is in the XSO string pools (`richtext` / `wndtext` / `wnddialog`); item text
  in INVINFO; most menu text is baked into `MENU/*.DDS` images (reskin-only).
- **The existing Ys Origin Archipelago project** (`C:\Users\kores\repos\YsOrigin-Archipelago`, a
  retail-exe mod + apworld) already catalogues 336 locations and the 62 chests with their box
  flags and scripts (`ys_origin/data/locations.json`, `chests.json`) and hooks the same two grant
  instructions (0x64 / 0x67) at the retail VM. CleriaCore should reuse that data, not redo it.

### 7.2 Patch files

```ini
; content/items.ini — existing items (ids 0..127): text, icon, magnitude
[item.0x59]
Name="Greater Panacea"
Description="Restores all HP.\nA modded item."
Icon=MY_PANACEA            ; -> MENU/ICON/MY_PANACEA.DDS + ICON2/MY_PANACEAV.DDS (from the mod's files/)
Magnitude=12

; new items (ids 0x100..0x1FF, CleriaCore's own range) [H]
[item.0x100]
Name="Archipelago Item"
Icon=AP_ITEM
Kind=key                   ; key | heal | buff | gold | flag   (data-driven effects, §7.3)
SetsFlag=0x1F0             ; Kind=flag: the CleriaCore flag it sets when obtained
```

```ini
; content/chests.ini — what a chest gives. Key = <room>/<script stem>, as chests.json.
[S_1001/S_BOX01]
Item=0x63                  ; the Bronze Key instead of the Panacea
Count=1
```

```ini
; content/pickups.ini — new pickups placed in a room
[pickup.my_mod.1]
Room=S_1009
Pos=0.0,2.0,6.0
Item=0x59
Flag=my_mod.p1             ; a CleriaCore save flag (not g_flags): collected once
```

```ini
; content/rules.ini — flag / event hooks (data-driven triggers)
[rule.after_boss1]
On=flag 0x1A0 >= 1         ; | room_enter S_1100 | item 0x63 | chest S_1001/S_BOX01 | death | new_game
Do=give 0x59; text "The tower trembles."; flag 0x1F1 = 1
Once=1
```

```ini
; content/text.ini — dialogue replacements, keyed by script + pool index or the exact original
[MAP/S_10/S_1009/SAVE.XSO]
3="A new line for string 3."
"Original text"="Its replacement"
```

### 7.3 How each lands in the engine

1. **`src/cleria/content.{hpp,cpp}`**: parse every active mod's `content/*.ini` into one table
   (load order, later wins, conflicts noted on the mods like file conflicts). Pure data; tested in
   `cleria-modstest`.
2. **Items**: `ItemTable::load` gets a post-load patch (`content::apply(ItemTable&)`); the table
   grows past 128 for ids >= 0x100. Ownership of new ids lives in a **CleriaCore sidecar save**
   (`yso_NN.cleria`, an INI or JSON beside the retail-format record, written by the same save /
   load paths): counts for new items, CleriaCore flags, the AP state (§8). The retail record stays
   byte-compatible with the real exe. The Item page needs a place for new items: a "Mod items" row
   after the 30 retail cells [H]; equipping new gear is out of scope (the grids and the aggregator
   are hardcoded in retail).
3. **Chests**: a grant filter in the VM: `XsoVm` gets `std::function<GrantDecision(const Grant&)>`
   consulted before every write to an item cell (index < 128) while a chest script runs (the
   chest is known from the `Interactable` that started it). The decision is pass / suppress /
   redirect-to(item, count); the same filter rewrites the item id of `0x116` and `0xD5` so the
   icon and the window match. This is the CleriaCore twin of the retail mod's 0x567D17 / add
   hooks (hook_vm.cpp) and is what Archipelago uses too.
4. **Pickups**: spawned at room load from the table through the existing pickup path
   (`make_chest_item` + the touch rules), gated by their sidecar flag.
5. **Rules**: an event bus `src/cleria/mod_events.hpp` (room_enter, flag_changed(index, old, new)
   from a per-tick g_flags diff, chest_opened, item_obtained, player_death, new_game, save, load),
   with the rule interpreter as one subscriber and Archipelago as another.
6. **Text**: `Xso::parse` callers get the string-pool patch (by script virtual name); INVINFO text
   by `items.ini`.

## 8. Archipelago (phase 2b, design)

### 8.1 Compatibility

Use the existing apworld (`ys_origin`, game "Ys Origin") unchanged: CleriaCore is a client for
it. Location ids and item ids come from its data (locations.json: chest locations detected by box
flag, bosses / statues by their flags; items by INVINFO id). The phase-2 task is to read its
`slot_data` contract and the location/flag table out of that repo and mirror them in
`content/ap_locations.ini` (generated by a tool from `locations.json`, not hand-copied).

### 8.2 Transport

The AP protocol is JSON over WebSocket (`ws://`, or `wss://` for archipelago.gg).

| option | for | against |
|---|---|---|
| **WinHTTP WebSocket** (`WinHttpWebSocketCompleteUpgrade` / `Send` / `Receive` / `Close`, Windows 8+), TLS by SChannel | zero new dependencies; the OS root store for certificates; ~300 lines | Windows-only (the project is Windows-first); async API is callback-based (use the blocking calls on a worker thread) |
| apclientpp + websocketpp + asio + OpenSSL (what the retail mod uses) | a complete AP client | four libraries, a download, OpenSSL to build or vendor |

Recommendation: **WinHTTP**. A worker thread owns the socket; two lock-protected queues carry
packets to / from the game thread, which polls once a frame. Reconnect with backoff.

**JSON** needs a small parser / writer (`src/cleria/json.{hpp,cpp}`, ~400 lines, a value tree;
AP messages are small). No download without the user's OK; writing our own needs none.

### 8.3 Protocol flow

`RoomInfo` -> (`GetDataPackage` if the cached checksum differs) -> `Connect{game "Ys Origin", name,
password, uuid, version, items_handling 0b111, tags ["DeathLink"] when on, slot_data true}` ->
`Connected{slot, checked_locations, missing_locations, slot_data}` / `ConnectionRefused` ->
`ReceivedItems{index, items}` (apply those past the saved index) -> `LocationChecks{locations}` as
checks happen -> `LocationScouts` for the chests of the room being entered (to show what they hold)
-> `StatusUpdate{status 30}` at the goal -> `Bounce` for DeathLink, `Say` / `PrintJSON` for the
chat feed. `Sync` after a reconnect.

### 8.4 Game side

- **Checks**: a location is checked when its detect predicate turns true (box flag set, boss flag,
  statue flag...), seen by the flag-diff event (§7.3.5); sent at once, and kept in the sidecar
  until the server acknowledges.
- **Vanilla suppression**: the chest grant filter (§7.3.3) suppresses the original item in a
  randomized chest; the chest still opens and its box flag is set (that IS the check).
- **Receiving items**: granted through the inventory's own rules (count + 1, gold -> SP, the
  equip refresh), with a popup; `received_index` stored in the sidecar (no double grants after a
  reload or reconnect). Progression keys must be granted even mid-cutscene: queue until the world
  is not frozen (`csMgr.world_frozen()`).
- **The foreign-item icon**: when a scouted chest holds another world's item, `0x116`'s sprite and
  the treasure window use an Archipelago icon (`IMAGE/AP_ITEM.DDS`, CleriaCore's own art drawn
  from scratch, shipped as a built-in layer so a mod can reskin it) and the text "Sent <item> to
  <player>". Progression / useful / filler / trap tint from the item flags.
- **DeathLink** (option, default off): on the player's death (`[death] HP 0` site in
  `view_game.inl`) send a `Bounce{tags:["DeathLink"], data:{time, source, cause}}`; on receipt set
  HP to 0 outside a short grace window and not during a cutscene; ignore our own bounces.
- **Tracker / state export**: `<settings>/archipelago/state.json`, rewritten on change: seed, slot,
  connection, received items, checked / missing locations, current room. Readable by stream
  overlays and external trackers (PopTracker can also read the AP server itself). [H] A localhost
  socket can come later; a file is enough and has no attack surface.
- **Overlays** (`Shell::hud` layers): `ap.status` (connection dot + slot), `ap.feed` (the last
  few items sent / received, fading), `ap.tracker` (per-zone checked / total, toggled). An
  "Archipelago" F1 tab: server, port, slot, password (needs a text field in the ui kit), DeathLink,
  connect / disconnect, and the log.
- **Saves**: the seed and slot are stamped into the sidecar; loading a save of another seed
  warns.

## 9. Verification (phase 1)

- `cleria-modstest` (ctest `modstest`): 50 checks: manifest parsing and errors, versions and
  dependency syntax, list split / join; the vfs (identity with no layers, later layer wins,
  case / separator / `..` normalisation, mounts incl. music, untouched paths outside mounts,
  virtual names, new files, `locate`, the conflict table, `read`); discovery of folders and zips
  (top-folder and root `mod.ini`, a `..` zip refused, a junk zip in error, no re-extraction of an
  unchanged zip); ordering (priority, the player's order, new = disabled, `UseMods=0`); every
  check (version too old, missing, chain to a failing mod, declared conflict, API too new,
  duplicate id, disabled dependency); `load()` end to end (zip mod served, `active()`,
  `active_summary()`, conflict notes, back to unmodded); and an override of a real game path when
  given the asset root.
- `cleria-settingstest`: the `[cleria.mods]` round trip.
- A `CLERIA_DRIVE` run in S_1009 with the sample mod installed: turned off, the frame is
  pixel-identical to the pre-mods build; turned on, the floor is the checkerboard; the zip form
  gives the identical frame to the folder form.
- The Mods tab captured from the launcher (`CLERIA_LAUNCHER=1 CLERIA_SHELL_PAGE=Mods`), including
  an error (missing dependency), a zip mod, and the tab actions through `CLERIA_MODS_UI` with the
  "restart to apply" status and the persisted `cleria.ini`.
- `tools/goldens.py compare` (7/7) and `tools/drive_regress.py` (4/4) unchanged with no mods.

## 10. Scripting (a question for the user)

The data-driven patches of §7 cover reskins, item text / stats, chest contents, new pickups,
simple flag rules and text. A scripting runtime would be needed for: item effects beyond the
fixed kinds, new enemy or boss behaviour, custom event sequences and cutscenes, custom shell UI,
and randomizer logic that is not a lookup table. Options:

- **Lua 5.4** (MIT, ~30 C files to vendor, small and well known by modders): the usual choice.
  It is a **dependency download**, so it needs the user's explicit permission.
- Native plugin DLLs with a C ABI: no download, but unsafe (arbitrary code), and binary
  compatibility to maintain.
- A tiny own expression language for `rules.ini`: no dependency, limited.

**Question for the user**: add Lua (vendored like miniz / stb), plugin DLLs, or stay data-only?

**Answered (2026-10-07): Lua.** Lua 5.4.9 is vendored in `third_party/lua`; a mod opts in with
`Script=main.lua` and `ApiVersion=3`. The sandbox, the `cleria.*` API, the tests and the proof (the
Archipelago client re-made as a Lua mod, `samples/mods/archipelago_lua`) are in **LUA_API.md**; the
modder's guide is `samples/mods/README.md`.

## 11. Limits and open points

- [H] choices: the mods folder location, new mods disabled, "lower wins", restart-to-apply, zip
  extraction to a cache, the 0x100.. range for new item ids, the sidecar save.
- [not modelled] live re-mount without restart; mod-added room sidecar files (`fs::exists`
  sites, §3); a `font` mount for `yso_ins04.dat`; mod preview images in the tab; the Mods tab
  cannot delete or install mods (by design: the folder is opened instead).
- The retail game has no mod system; nothing here is RE'd behaviour, all of it is CleriaCore's.

## 12. Phase-2 task list (for the next agent)

In order; each step its own commit with tests. **Items 2-7 are built: see CONTENT.md** (where the
build differs from this plan it says why: the parsers' tests live in `cleria-contenttest`, the
Item-page row became the F1 Content tab, new chests are not modelled, the json step (1) was not
needed for the INI sidecar and is left to the AP client).

1. **json**: `src/cleria/json.{hpp,cpp}` (parse to a value tree, serialize; UTF-8, numbers as
   double + int64), tests in a new `cleria-jsontest`.
2. **Sidecar save**: `src/cleria/sidecar.{hpp,cpp}` `yso_NN.cleria` (INI) beside each save:
   CleriaCore flags (string ids -> int), new-item counts, the AP block. Wire into the save / load /
   new-game paths in `view_game.inl` (`slot_path`: the slot write and the two slot reads); test in
   `cleria-savetest`.
3. **Event bus**: `src/cleria/mod_events.hpp` + the per-tick `g_flags` diff, room_enter,
   chest_opened (the chest's `Interactable::opened_flag`), item_obtained, player_death (the
   `[death] HP 0` site), new_game, save, load. One hook block in `view_game.inl`, the rest in a
   new `view_content.inl`.
4. **Grant filter**: `XsoVm` callback before item-cell writes (index < 128) and on `0x116` /
   `0xD5`, with the running chest's identity; tests in `cleria-wndtest` or a new VM test using a
   real chest script (S_1001/S_BOX01).
5. **content/ parsers** (`src/cleria/content.{hpp,cpp}`): items.ini, chests.ini, pickups.ini,
   rules.ini, text.ini, merged in load order with notes; `ApiVersion=2`; tests in
   `cleria-modstest`.
6. **Apply**: ItemTable patch + new ids (>= 0x100) + the Item page's mod row; chest redirects via
   the grant filter; pickups at room load; rules on the event bus; XSO string-pool patches.
   Drive_regress cases: a chest giving a patched item (expect the `[probe]` / treasure lines),
   a rule firing on a flag.
7. **Second sample mod** `samples/mods/chest_swap` (chests.ini + text.ini), generated by
   `tools/make_sample_mods.py`.
8. **AP transport**: `src/cleria/ap_socket.{hpp,cpp}` (WinHTTP WebSocket on a worker thread,
   ws/wss, queues, reconnect); a loopback test against a tiny local echo (no internet in tests).
9. **AP client**: `src/cleria/archipelago.{hpp,cpp}` protocol state machine (RoomInfo,
   DataPackage cache, Connect, Connected, ReceivedItems, LocationChecks, LocationScouts, Bounce,
   PrintJSON, StatusUpdate, Sync), tested with recorded JSON exchanges.
10. **AP game glue** (`view_archipelago.inl`): locations from the apworld data (a tool converting
    `YsOrigin-Archipelago/ys_origin/data/locations.json` + `chests.json` into
    `res/ap/locations.ini`), checks from the event bus, suppression + receipt through the grant
    filter / inventory, the sidecar index, DeathLink, goal.
11. **AP UI**: the ui kit's text field; the "Archipelago" F1 tab; `Shell::hud` layers `ap.status`,
    `ap.feed`, `ap.tracker`; the foreign-item icon (`IMAGE/AP_ITEM.DDS` drawn from scratch by a
    script) on `0x116` and the treasure window; `state.json` export.
12. **Ask the user** (§10) before any scripting runtime or any third-party download.
