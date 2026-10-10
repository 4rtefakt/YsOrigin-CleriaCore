# Shell core: settings + input (the model the overlay edits)

The data model and input layer under the CleriaCore settings shell. The retail spec is `CONFIG.md`
(same folder). This file covers the CleriaCore modules: their APIs, where the files live, what applies
live, and what is hand-chosen. Tags: **[BYTES]** read from `yso_win.exe`, **[EXPORT]** the Ghidra
export, **[H]** hand-chosen, **[not modelled]**.

## 1. Files

| File | Where | Content |
|---|---|---|
| `yso_win.ini` | `CLERIA_INI`, else `%USERPROFILE%\Saved Games\CleriaCore\` (`FOLDERID_SavedGames`, created on first run) | Every retail key in retail's dialect and order (`IniVersion=0x100`, `%0.8f` floats, `"quoted"` strings, binding names with spaces stripped, CRLF). Lines the model does not know (retail-only keys, comments, unknown sections) round-trip byte for byte. |
| `cleria.ini` | next to `yso_win.ini` | CleriaCore-only keys (§3). QoL defaults are OFF = retail behaviour. |

**First run.** If `yso_win.ini` is missing from the CleriaCore folder, CleriaCore imports retail's file
once: it copies the lines verbatim, then saves its own copy. It never writes retail's file. Retail's
location is resolved the way retail resolves it (`FUN_005de1d0`):
- `common.ini` in `CLERIA_RETAIL_DIR`. With `UserProfileHere=1`, the file is `<BaseDir or install>\user\<user name>\yso_win.ini`.
- Otherwise, and when there is no `common.ini`, it is `Saved Games\FALCOM\yso_win\yso_win.ini`.

Without a retail file, CleriaCore writes the retail defaults. Steam-library discovery is
[not modelled]: set `CLERIA_RETAIL_DIR` only when the install uses `UserProfileHere=1`.

**Which runs touch the files.** Only the interactive game: `render_window` without `CLERIA_DRIVE`.
Headless modes and `CLERIA_DRIVE` runs use the retail defaults in memory and never read or write a file.
An explicit `CLERIA_INI` is the exception. Without it, tests and goldens never see the player's
settings. `--config-test` requires `CLERIA_INI`.

## 2. `src/cleria/settings.hpp` (pure apart from the folder lookup)

- `Ini`: the line-preserving document.
  - `parse` / `load` / `save` / `text`.
  - Reads: `get_int` (`0x` / `0b` / decimal like `FUN_005d07f0`; garbage reads as 0), `get_float`
    (`atof`), `get_string` (`FUN_005d0590`: drops the first byte, then cuts at the last `"`), `raw`, `has`.
  - Writes: `set_int(…, hex)`, `set_float`, `set_string`, `set_raw`, `erase`. An existing line is replaced
    in place; a new key goes at the end of its section; a new section goes at the end of the file.
  - Section and key names compare case-insensitively.
- `Settings`: one typed field per retail key, defaulted to the game's missing-key defaults (CONFIG.md §2).
  - `kernel`, `game`, `graphics`, `sound`, `input` (with `assign`, the 34 binding codes), `debug`.
  - `cleria` holds the cleria.ini keys.
  - Optional fields are written only if they were read: `device_identifier`, `input.file_index`,
    `debug.no_initialize_game_ctrl`. `GameFirstPlay` is written only when non-zero.
- `read_retail` / `write_retail` / `read_cleria` / `write_cleria`.
  - The IniVersion gate (`gated_key`) is modelled.
  - FileIndex semantics: when FileIndex is present, a missing or empty `Assign{}` is unbound.
  - An unknown binding name keeps the default.
  - When any slot is unbound, the writer adds FileIndex so the unbound slot survives a reload.
- `mapper_config(settings, book_mode)`: builds the `input::Config` from the settings. With
  `NativePadBinds`, it takes the PAD_* slots from cleria.ini.
- `default_ini_path()`, `retail_ini_path(install)`, and the testable `retail_ini_path_from(...)`.
- `SettingsStore`:
  - `load(ini, import_from)` returns Loaded / Imported / Defaults.
  - `use_memory_defaults()`, `save()`.
  - `touch()` sets the dirty flag and bumps `revision()`. This is the change notification.
  - `s` is the live `Settings`.

**In the view** (`apps/cleria-view/view_input.inl`):
- `settings()` returns the one `SettingsStore`.
- To change a setting, the overlay does `settings().s.<field> = v; settings().touch(); settings().save();`.
- The mapper reconfigures on the next tick when `revision()` changes.
- `sync_settings()` in `view_game.inl` reloads the Option page's copy and re-applies the volumes, once per frame and once per title tick.
- `options_load` / `options_save` (the in-game Options page) go through the same store.

## 3. cleria.ini keys

| Section / key | Default | Effect | Applies |
|---|---|---|---|
| `[cleria.video] DebugHud` | 0 | start with the debug overlay (today's `CLERIA_DEBUG_HUD`); **F3** still toggles it for the session | next start |
| `[cleria.dev] DebugKeys` | 0 | the fixed debug keys F / R / T / K / L / B, Ctrl+Tab, Ctrl+1..9 (F1 > Debug > "Debug keys"); off, they do nothing, so a player's own binding on one of those keys never trips them. F3 always works. `CLERIA_DRIVE` commands are unaffected | live |
| `[cleria.qol] UnlockMovieSkip` | 0 | any demo movie can be skipped, seen or not | next movie |
| `[cleria.input] LinearStickMagnitude` | 0 | stick magnitude = \|v\|/1024 (a half tilt walks) instead of retail's `sqrt(x⁴+y⁴)/1024` | live |
| `[cleria.input] NativePadBinds` | 0 | the 8 PAD_* slots come from `PadBind{}` instead of retail's `Assign{PAD_*}` | live |
| `[cleria.input] PadBind{PAD_ACTION..PAD_SWORD_REVU}` | a, b, x, y, start, lefttrigger, leftshoulder, rightshoulder [H] | SDL gamepad names (`SDL_GetGamepadStringForButton` spelling, plus `lefttrigger` / `righttrigger`) | live |

`[cleria.mods]` is reserved for the mod API and has no keys yet.

## 4. `src/cleria/input_map.hpp` (pure)

- **Slots**: `Slot` enum (`KEY_ACTION`..`MOUSE_WALK`, 34 slots), `slot_name` / `slot_label`.
- **Codes**:
  - DIK `0x01..0xdd`; mouse `kMouseL/R/M` (`0x100..0x102`); the `---` placeholders `0x103..0x106`;
    pad buttons `kPadButton1 + n`; `kUnbound`.
  - `code_name` (table spelling), `code_ini_name` (spaces stripped), `code_from_name` (first match,
    whitespace- and case-insensitive), `code_table_size` / `code_table_entry` (to list every code),
    `bindable(code)` (false for F1).
- **Defaults**: `preset1()` (the game default) and `preset2()`.
- **Bits**: `kAttack 1`, `kJump 2`, `kMagic 4`, `kBoost 8`, `kMenu 0x10`, `kWalk 0x20`, `kPrevSkill 0x40`,
  `kNextSkill 0x80`, `kSkill1..3 0x100..0x400`, `kDoubleClick 0x800`, `kMouseDir 0x1000`, `kOk 0x2000`,
  `kCancel 0x4000`, `kMouseAttack 0x8000`, `kClick 0x10000`. Directions: `kDown 1`, `kLeft 2`, `kRight 4`, `kUp 8`.
- **SDL translation**: `dik_from_sdl_scancode` / `sdl_scancode_from_dik` (SDL scancodes are USB HID
  usages; the view static_asserts the numbering). Pad: `pad_button_from_sdl`, `sdl_pad_from_name`,
  `sdl_pad_name`, `pad_code_from_sdl_name`.
- **`Mapper`**: `configure(Config)`, then `step(Snapshot)` once per logic tick. It returns a `Frame`:
  - `held`, `pressed`, `repeat`;
  - `dx` / `dy` (the menu auto-repeat steps);
  - `dir`, `angle`, `magnitude`;
  - `stick_x` / `stick_y`, `wheel` (the raw ±120 delta);
  - `x` / `y` (+0x00 / +0x04): the cursor through `mouse::cursor_from_client` (MOUSE.md §1.1), from
    `Snapshot.cursor_x/y` (the client position in back-buffer pixels) and `Snapshot.display`;
  - `analog` (true when the angle is the stick's own).
- The cursor, its sprite, click-to-move, the skill switch and the menu hit tests live in
  `src/cleria/mouse.hpp` (build/shell_re/MOUSE.md §11).

**Capturing a key for rebinding** (the overlay). From an SDL event:
- `SDL_EVENT_KEY_DOWN`: the code is `input::dik_from_sdl_scancode(e.key.scancode)`. Refuse 0, or a
  code where `!bindable(code)`.
- Mouse button 1 / 3 / 2: `kMouseL` / `kMouseR` / `kMouseM`.
- Gamepad button: `kPadButton1 + pad_button_from_sdl(e.gbutton.button)`.

**In the view** (`view_input.inl`):
- `g_sdl_in` (`SdlInput`) is the single place where SDL state becomes a `Snapshot`.
  - `on_event(e)` must see every event. Every event loop calls it first; this records keys tapped within
    one tick, the wheel, and gamepad hot-plug.
  - Set `g_sdl_in.suppressed = true` while the overlay has focus: the game then sees nothing pressed.
- `input_tick(book_mode)` steps the mapper once and stores the result in `g_in`. Each tick of every loop
  calls it: the title, the movie, the pause book (`book_mode`: 0.8 deadzone, 4-way), and the game tick.
- The gamepad opened is the `GamePadID`-th of `SDL_GetGamepads()`, or none under `[debug] NoInitializeGameCtrl`.

## 5. What reads the masks now

| Consumer | Reads |
|---|---|
| Movement | `dir` (digital: the camera basis exactly as the keys always composed it) or `angle` (analog). Run = magnitude ≥ `RunWalkThreshold` and no `kWalk` held (FUN_00465170 l.432). Running drives the speed, the turn rate 0.35 / 0.18 ([EXPORT] l.2539) and the locomotion section run 1 / walk 0. |
| Attack | `pressed & kAttack` runs the action probe first ([BYTES 0x465abf]), and swings only if no script started. `held & kAttack` is the combo / charge hold. |
| Jump / magic / boost | `held & kJump` (with the existing edge), `held & kMagic`, `pressed & kBoost` |
| Pause book open | `pressed & kMenu` (Esc is fixed; also the Camp Menu binding) |
| Book / title / options | `pressed & kOk` / `kCancel`, `dx` / `dy`, `dir & kLeft/kRight` for the sliders |
| Windows (talk, script menus, game over) | confirm held `0x12000`, cancel held / repeat `0x4000`, `dy`. The OK edge sets `csHost.advance`. Cancel or Esc over a script menu sets `wnd_cancel`; Esc elsewhere skips the cutscene. |
| Movie skip | `held & 0x1001` |

The CLERIA_DRIVE commands are unchanged and land alongside the mapper. Drive runs behave identically
(verified: drive_regress 4/4 and seven before/after stdout comparisons, the S_1005 combo included).

**Behaviour changes for players** (the retail bindings replace CleriaCore's old hard-coded keys):
- **E** no longer interacts: the attack button talks and opens.
- **X** no longer boosts: Use Boost does.
- Enter / Space / E / Z no longer confirm: OK does. Esc / Backspace no longer cancel: Cancel does.
- The pause book closes with Cancel, as retail's navigator does ([EXPORT FUN_005a68f0] state 1). Esc
  opens it, and Esc during a cutscene still skips it.
- Tab cycles the character only with **Ctrl**, and the exit hotkeys moved to **Ctrl+1..9**.
- F / R / T / K / L / B are fixed debug keys, **off by default** since 2026-10-07 (`[cleria.dev] DebugKeys`,
  the Debug tab's "Debug keys"); F3 (the Interface tab's Debug text) always works.
- Free-fly keeps its raw WASD / E / Q keys.

## 6. Apply classes (CleriaCore today)

- **Live**: everything in `[input]` and `cleria.ini [cleria.input]` (bindings, deadzone, RunWalk,
  DoubleClick, AlwaysOkCancel, PadAnalog, GamePad); BGM / SFX volume; AlwaysDashOK (the dash FSM reads
  the Options copy); `UnlockMovieSkip`.
- **Persisted, not consumed by CleriaCore yet**: `Reverb`; the `[game]` bookkeeping keys; the keys listed as
  not applicable or open in `CONFIG_PARITY.md`. `GameFirstPlay` is read. (Since 2026-10-10 `DashControl`,
  `AtkBtnMagic`, `PlayBgm`, `PlayEffect`, the sound `Device` and `Language` are consumed: CONFIG_PARITY.md.)
- **Needs a restart**: `GamePadID` and `NoInitializeGameCtrl` (the pad is opened at start and on
  hot-plug); `DebugHud` (its initial state).

## 7. Hand-chosen and not modelled

- [H] The SDL→DInput pad order: A, B, X, Y, LB, RB, Back, Start, LS, RS = Button 1..10. This is the
  standard XInput-through-DInput order. It makes retail's default `PAD_MENU = Button 10` the right-stick
  click, `PAD_WALK = Button 9` the left-stick click, and `PAD_SWORD_REVU = Button 8` Start.
  `NativePadBinds` exists to fix that.
- [H] The pad's other inputs:
  - CleriaCore buttons 11..16: LT, RT (past a quarter pull), Guide, Misc1, paddle 1, paddle 2.
  - Axis scaling: SDL ±32767 → ±1024, linear.
  - The d-pad becomes POV 0.
- [H] A key or mouse press that starts and ends inside one tick still reads as held for that tick (the
  tap latch). Retail's poll would miss such a press.
- [H] Not retail: F1 never reaches the mapper.
- [H] Not retail: debug keys need Ctrl.
- [H] Not retail: Esc skips cutscenes.
- [H] Two readers model the IniVersion gate as one: PadAnalog and Reverb are read ungated (FUN_0040bf50
  reads them again).
- [H] Around `key=value`, spaces are trimmed.
- [H] A relative `BaseDir` resolves against the install folder.
- [not modelled]:
  - the per-bit press timestamps `0x7647b0[]`;
  - direction slots bound to codes ≥ 0x100 (retail reads the bytes behind the keyboard array);
  - MOUSE_DIR codes `0x104..0x106` (retail reads joystick bytes);
  - the L/R tab step of the book;
  - rumble;
  - the actor flag 0x40000 that forces walk.

Since 2026-10-06 (MOUSE.md §11) the cursor transform, click-to-move, the menu hover / click tests and the
skill bits (the bracelet switch FUN_0046cb00 and its apply, traced) are modelled: Previous / Next Skill and
Skill 1–3 now switch the bracelet from the keyboard and the pad as well as the wheel.

## 8. RE findings made while building this

- **Retail's stick never walks.** The magnitude is `sqrt(x⁴ + y⁴) / 1024` [BYTES 0x5d2f37..0x5d2f6f:
  `x*x` squared and `y*y` squared before the sqrt]. The axes are ±1024 (DIPROP_RANGE, [BYTES
  0x5d5ad7..0x5d5aff]). Past the 0.35 deadzone the magnitude is ≥ ~125, far above `RunWalkThreshold`
  0.5. Only the Walk binding walks. The speed never scales with the stick: `local_38 = 1.0` [EXPORT
  00465170.c l.404].
- **`"---"` collapses to `0x103`.** The parser takes the first table match. On a reload, every `---`
  slot becomes 0x103, which for MOUSE_DIR is the mouse's 4th button (X1).
- **A missing `Assign{}` is also unbound when FileIndex is present.** The read buffer is zeroed first,
  so a missing key and an empty value read the same.
- **The attack press is the action probe's trigger** ([BYTES 0x465abf], `+0x50c & 1` = slot 0
  KEY_ACTION). This resolves BARRIER_OBJECTS.md's [UNKNOWN] "which physical button bit 0x1 is".
- **`FUN_005d0590` drops the first byte unconditionally.** An unquoted string value loses its first character.

## 9. The F1 panel's tabs: the tab policy

The top level is fixed at ten tabs: **PRELAUNCH, VIDEO, INPUT, AUDIO, GAMEPLAY, CHEATS, SPEEDRUN, MODS,
INTERFACE, DEBUG** (2026-10-07; the CONTENT and ARCHIPELAGO tabs and the one-tab-per-Lua-mod rule were
folded into MODS).

- **No new top-level tabs.** A new feature goes under an existing tab, as a section of its page or as a
  sub-page of it.
- **Mods never get a tab.** A Lua mod's `cleria.ui.page` is drawn on its own page, **Mods > <mod name>**
  (with its On / Off, load order and details); content patches are **Mods > Content additions**
  (`apps/cleria-view/view_mods.inl`).
- **A sub-page** starts with a `ui::back_row` ("<  MODS / HELLO LUA": OK / click goes back) and sets the
  tab's `ui::Page::back` hook, so Esc / Backspace / the pad's Back return to the tab's page instead of
  closing the panel (a second Back closes it). A tab that can open a sub-page by name sets
  `ui::Page::open_sub`; `CLERIA_SHELL_PAGE=<Tab>:<sub>` and the drive's `shell=<Tab>:<sub>` reach it
  through `ui::Panel::select` (`Input:bind`, `Input:pad`, `Mods:<mod id>`, `Mods:content`). The drive's
  `shellback` presses Back on the current tab (`drive_regress shell_subpages`).
- `ui::Panel::add` replaces a tab of the same name in place (the Debug page is re-added after each Return
  to Title; it no longer duplicates).
- **If tabs must be trimmed later, PRELAUNCH is the first candidate to remove**: its few options (the game
  data, the logo movie, movie skipping) would move to other tabs. Not done now.
