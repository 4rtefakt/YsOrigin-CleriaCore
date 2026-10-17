# Mod files: `.cleriamod`

A mod file type of CleriaCore's own, like Archipelago's `.apworld`: **double-clicking a `.cleriamod`
installs it into CleriaCore and turns it on.** CleriaCore's layer, not retail (the retail game has no
mods). Not to be confused with `yso_NN.cleria`, the save sidecar (CONTENT.md).

Evidence tags as in MODS.md: **[CODE]** read in CleriaCore's code, **[H]** a choice of this design.

| piece | where |
|---|---|
| the package checks, the install, the enable queue, the instance key | `src/cleria/modinstall.{hpp,cpp}` |
| the loader taking `.cleriamod` | `src/cleria/mods.cpp` `discover` |
| the file association (behind a `Registry` interface) | `src/cleria/fileassoc.{hpp,cpp}` |
| the command line, the startup, the notice card, the Mods tab rows | `apps/cleria-view/view_modfile.inl` |
| the packer | `tools/pack_mod.py` |
| tests | `cleria-modfiletest` (ctest `modfiletest`), `cleria-settingstest`, `tools/modfile_regress.py` (drive_regress `modfile_install`, `modfile_bad`), the aplua cases with `APLUA_MOD_ZIP=<x>.cleriamod` |

## 1. The format

A `.cleriamod` **is exactly a zip mod** (MODS.md §1): `mod.ini` at the zip root or inside exactly one
top-level folder. `tools/pack_mod.py` always writes the second form, one folder named by the id:

```
ysorigin.archipelago-0.1.0.cleriamod
    ysorigin.archipelago/mod.ini
    ysorigin.archipelago/main.lua
    ysorigin.archipelago/ap/client.lua ...
```

The mods loader treats a `*.cleriamod` in the mods folder like a `*.zip`: the same extraction cache and
the same safety checks; it unpacks to `<mods>/.cache/<file name>.cleriamod/` (the full name, so a
`foo.zip` and a `foo.cleriamod` never share a cache folder; zips keep `.cache/<stem>/`) [CODE]. A `.zip`
dropped into the mods folder still works as before.

## 2. Installing one

`cleria-view.exe <file>.cleriamod` (a bare argument ending in `.cleriamod`, which is what Explorer passes
for a double-click) or `cleria-view.exe [...] --install-mod <file>` (any zip mod, e.g. a `.zip`). The file
argument is taken out of `argv` first thing in `main()` (the wide command line, so any path works), so
`cleria-view.exe x.cleriamod` starts like a bare launch (default assets, the launcher) and
`cleria-view.exe <assets> <room.sob> x.cleriamod` like a room run (the drive cases).

The install runs right after `settings_init`, before `mods_startup`, so the mod loads in this session:

1. **Checks, before anything is written** (`mods::inspect_package`): the file reads, it is a zip, every
   entry name is safe (`mods::safe_zip_entry`: relative, no `..`, no drive), every entry unpacks
   (`mz_zip_validate_archive`: CRC), `mod.ini` is at the root or in the one top-level folder (the
   loader's rule), the manifest parses without errors (an Id, a valid id, well-formed Requires /
   Conflicts), and its `ApiVersion` is not newer than `mods::kApiVersion`. A failure leaves the mods
   folder (it is not even created) and `cleria.ini` untouched.
2. **The copy**: to `<mods>/.<id>.cleriamod.part` (hidden from discovery), its size checked, its write time
   set to now (the cache re-extracts on size + time), renamed over `<mods>/<id>.cleriamod`.
3. **One copy per id**: every other mod in the folder with the same id is taken out after the copy: a
   `.zip` / `.cleriamod` is deleted; a loose folder is **moved to `<mods>/.replaced/<folder>`** [H] (a
   folder may be a modder's working copy; `.`-folders are never discovered). The highest older version
   is reported: "Updated Archipelago 0.1.0 -> 0.2.0"; the same version is "Reinstalled". `.cache` is
   never touched (a running session's files stay; the stamp re-extracts at the next start).
4. **On**: the id is appended to `[cleria.mods] Order` when new (an existing place is kept), added to
   `Active`, and `UseMods=1`; `cleria.ini` saved.

A mod whose dependencies are missing installs and is turned on; the Mods tab then shows its usual error
("needs x (not installed)").

### 2.1 With or without a running CleriaCore

**Single instance** = one CleriaCore per **mods folder**: a named mutex `Local\CleriaCore.<16 hex>`, the
hex an FNV-1a hash of the mods folder's normalised, lowercased path (`mods::instance_key`). Every run with
a settings file holds it for its life [H]. Per folder rather than global: test runs on scratch folders
never see the player's game (or each other), and the instance that matters is the one whose mods folder
the file goes into.

- **None running**: the start goes on normally (the launcher), with a **notice card** bottom-centre
  (a `Shell::hud` layer, `ui::toast`'s look, as wide as its text, 12 s [H]; a drive run keeps it up):
  "MOD INSTALLED / Installed Archipelago 0.1.0 — enabled" or "MOD NOT INSTALLED / Could not install
  x.cleriamod: <why>" (orange edge). The mod is loaded in this session.
- **One running**: the files are installed, then a native message box (`MessageBoxW`, "Installed X 0.1.0 —
  enabled. Restart CleriaCore to load it." or the error) and this process exits before any window. The
  running process may rewrite `cleria.ini` from its memory before it restarts, so the id is also written
  to `<mods>/.cleriamod-enable`; the next start that owns the folder turns those ids on and deletes the
  file (`mods::take_enable_queue`). In a `CLERIA_DRIVE` run the box is printed (`[modfile] message box:
  ...`) instead.
- A run without a settings file (no `CLERIA_INI` in a drive run) cannot install: the card says so.

## 3. The file association (`src/cleria/fileassoc`)

Per user, `HKEY_CURRENT_USER\Software\Classes` (no administrator rights):

| key | value | data |
|---|---|---|
| `.cleriamod` | (default) | `CleriaCore.Mod` |
| `.cleriamod\OpenWithProgids` | `CleriaCore.Mod` | `""` |
| `CleriaCore.Mod` | (default) | `CleriaCore mod` |
| `CleriaCore.Mod\DefaultIcon` | (default) | `"<exe>",0` |
| `CleriaCore.Mod\shell\open\command` | (default) | `"<exe>" "%1"` |

- **Setting**: `[cleria.mods] AssociateFiles` (`Settings::Cleria::mod_associate`), default **1**; the Mods
  tab's Options row **Open .cleriamod files with CleriaCore**.
- **At every normal start** (`fileassoc::sync`): on and any value missing or naming another exe path ->
  (re)register (the exe paths compare case-insensitively); off and the open command runs THIS exe ->
  remove ours (`CleriaCore.Mod` tree, the `.cleriamod` default when it is ours, our `OpenWithProgids`
  value, then the two keys only if empty: another program's entries stay). A registration by another
  exe is never removed. `SHChangeNotify(SHCNE_ASSOCCHANGED)` after a change. Turning the toggle off
  or on applies at once the same way.
- **Several builds** (Release / Debug / an installed copy) sharing one settings folder: each start with
  the setting on points the association at itself, so the last one started wins [H].
- **Never in tests**: every registry access goes through `fileassoc::Registry`; `cleria-modfiletest` uses a
  fake (the five entries, register / no-op when current / re-register for another exe or a changed
  value / unregister only when owned / other programs' values kept). `fileassoc::user_registry()` (the
  real HKCU) returns nullptr in a `CLERIA_DRIVE` run, and `modfile_associate` skips drive runs and runs
  without a settings file before that ("[modfile] file association: skipped (drive run)").
- **Explorer's own choice wins**: if the player once picked another program for `.cleriamod` with
  "Open with > Always", Windows' protected `UserChoice` key overrides this and cannot be set by a program;
  "Open with > CleriaCore" (listed through `OpenWithProgids`) fixes it.
- The exe has no icon resource yet, so `DefaultIcon` shows Windows' generic program icon [not modelled].
- `cleria-view` is a console-subsystem program: a double-click also opens its console window, as any
  launch of it does today [not modelled: a WIN32-subsystem build or a launcher stub].

### 3.1 Linux and macOS

- **Linux**: the same setting writes a freedesktop `.desktop` entry and a `application/x-cleriamod` mime type
  under `$XDG_DATA_HOME` (`fileassoc::xdg`).
- **macOS**: `CleriaCore.app`'s `Info.plist` (`tools/macos_bundle.py`) exports the type
  `io.github.4rtefakt.cleriamod` (extension `cleriamod`, a `public.zip-archive`) and claims it as its owner, so
  Finder opens `.cleriamod` files with CleriaCore once the app has been opened once. SDL hands the file to the
  running app as `SDL_EVENT_DROP_FILE`, installed like a drop (§4). Nothing is registered per user, so the
  setting does nothing there; a bare build (no `.app`) uses `--install-mod <file>`.

## 4. The Mods tab (F1 > Mods > Options)

No new tab (SHELL_CORE.md §9). After **Open mods folder**:

- **Install a mod file...**: a native open-file dialog (`GetOpenFileNameW`, filter `*.cleriamod;*.zip`),
  then the same install; the result shows on the next row ("Installed Hello Lua 1.0.0 — enabled", orange
  when refused), the list is rescanned, and the Status row says "restart to apply". A drive run prints
  instead of opening the dialog; `CLERIA_MODS_UI="install=<file>"` runs the same install headless.
- **Open .cleriamod files with CleriaCore**: the toggle of §3.
- **Drop a file on the window** (any screen: the launcher, the game, the panel): a `.cleriamod` or `.zip` is
  installed the same way and the notice card says so (`modfile_dropped`, from `Shell::event`'s
  `SDL_EVENT_DROP_FILE`); other files are ignored. `CLERIA_MODS_UI="drop=<file>"` runs it headless.

## 5. Packing (`tools/pack_mod.py`)

`python tools/pack_mod.py <mod folder> [-o <out>]` -> `<out>/<id>-<version>.cleriamod`. It checks the
manifest the way `parse_manifest` reads it (Id, Requires / Conflicts syntax, `ApiVersion` <= the
`kApiVersion` it reads from `src/cleria/mods.hpp`, the `Script` file present, a version usable in a file
name), writes one top-level folder named by the id, and is deterministic: sorted entries, `/`
separators, the 1980-01-01 time stamp, no attributes, deflate level 9 (the same folder gives the same
bytes; the files' own line endings are packed as they are on disk). Left out: `.` names, `__pycache__`,
`*.pyc`, `Thumbs.db`, `desktop.ini`, and any `.cleriamod` / `.zip` inside. It reads the result back
(`testzip`) before renaming it into place.

## 6. Verification

- `cleria-modfiletest`: 49 checks: the package checks (root / top-folder manifest, a .zip, and ten
  refusals: not a zip, no mod.ini, two top-level folders, a bad id, no id, `..`, a drive letter, API 99,
  a damaged entry, a missing file); a fresh install (dest, message, Order / Active / UseMods, no part
  file); the loader on `.cleriamod` (discovered, `.cache/<name>.cleriamod/`, Active); an update
  (0.1.0 -> 0.2.0 reported, older zip deleted, older folder moved to `.replaced/`, other mods untouched,
  one copy left, re-extracted); reinstall (same version; the installed file itself); a `.zip` installed as
  `<id>.cleriamod` with UseMods back on; every invalid file refused with the folder byte-for-byte as before
  and prefs untouched, and not even the folder created; a missing dependency installed in Error; the enable
  queue; the instance key; the association on the fake registry.
- `cleria-settingstest`: `AssociateFiles` default on and its round trip.
- `drive_regress modfile_install` / `modfile_bad`: the room run with the packed `hello` sample as an extra
  argument installs it and `[lua] Hello Lua: started` follows in the same run; a broken file is refused
  and the run is unmodded.
- The six `aplua_*` cases with `APLUA_MOD_ZIP=<scratch>/ysorigin.archipelago-0.1.0.cleriamod` (the file
  dropped into the mods folder, read by the loader): 6/6.
- By hand (drive runs, scratch `CLERIA_INI`): the launcher's card for an install, an update and a broken
  file; the Mods tab's rows after `CLERIA_MODS_UI=install=...`; the running-instance path (a long drive
  run on the same scratch folder holds the mutex; the second process installs, prints the message box text,
  exits in 0.5 s without a window; the third start loads the queued mod).
