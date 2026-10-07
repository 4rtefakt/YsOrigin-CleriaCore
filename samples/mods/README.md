# Making mods for CleriaCore

A short guide. The references: `build/shell_re/MODS.md` (packages, load order, the file overrides),
`build/shell_re/CONTENT.md` (`content/*.ini`), `build/shell_re/LUA_API.md` (Lua scripts).

## The samples here

| folder | shows |
|---|---|
| `checker_floor` | a reskin: two floor textures replaced (`files/`) |
| `chest_swap` | content: a new item in a chest, a rule, a pickup, reworded text (`content/`) |
| `hello` | the smallest Lua mod: an event, per-save storage, a setting, a corner overlay, its page under F1 > Mods |
| `archipelago_lua` | a big Lua mod: CleriaCore's Archipelago multiworld client for the Ys Origin apworld (its own `README.md`) |

## 1. Install and try one

**A `.cleriamod` file**: double-click it. CleriaCore installs it (into the mods folder, replacing an older
version of the same mod) and turns it on; if CleriaCore is already running, restart it to load the mod. F1 >
Mods > *Install a mod file...* does the same from inside the game. (Double-click works once CleriaCore has
been started once with F1 > Mods > *Open .cleriamod files with CleriaCore* On, the default.)

**A folder or a `.zip`**:

1. Copy the mod's folder (or a `.zip` of it) into the **mods folder**: `Saved Games\CleriaCore\mods`
   (next to `cleria.ini`; F1 > Mods > *Open mods folder* opens it).
2. F1 > **Mods**: select it and press OK (or click it) to open its page, **Mods > <mod>**, and turn **Enabled**
   On (a click on a row's On / Off also turns it on or off). Left / Right move it in the load order (lower in the
   list wins when two mods change the same file). A gear on the row means the mod has settings: they are on its
   page.
3. Restart CleriaCore. New mods start Off, so a dropped-in folder never changes your game by itself.

The Mods tab shows each mod's errors (a bad `mod.ini`, a missing dependency, a script that stopped); its page
lists them all. **Mods > Content additions** shows what the active mods add (items, chests, pickups, rules, text).

## 2. Folder layout

```
my_mod/
    mod.ini                         the manifest (required)
    files/IMAGE/STATUS_512.DDS      replacement game files, by their game path (reskins, music, scripts)
    files/music/YSO_005.ogg         ... also the loose music / movie folders
    content/items.ini               data patches: items.ini, chests.ini, pickups.ini, rules.ini, text.ini
    main.lua                        a Lua script (Script=main.lua), and any modules it requires
    lib/util.lua                    require("lib.util")
    icons/MY_ICON.DDS               your own images (cleria.ui.icon, d:image)
```

## 3. mod.ini

```ini
[mod]
Id=yourname.my_mod                ; required: a-z 0-9 . _ - ; unique
Name="My Mod"
Version=1.0.0
Author="You"
Description="What it does, shown in the Mods tab."
ApiVersion=3                      ; 1: files/ only, 2: + content/, 3: + Script=
Script=main.lua                   ; optional: the Lua entry file
Requires=other.mod>=1.2           ; optional, comma list
Conflicts=some.other              ; optional
```

## 4. Replacing files (`files/`)

Put a file at its game path under `files/`: `files/MAP/S_10/COMMON/H/10YUKA04.DDS` replaces that texture.
Textures must be DDS (DXT1, DXT3 or uncompressed); replace the `H/` (high-resolution) copy. Sounds are WAV,
music Ogg Vorbis, scripts and tables in the game's own formats. Nothing else to declare.

## 5. Content patches (`content/*.ini`, ApiVersion 2)

```ini
; content/chests.ini — the first chest of 2F gives a Roda Fruit x2
[S_1001/S_BOX01]
Item=0x57
Count=2
```

Items (names, icons, new items 0x100..0x1FF), chests, pickups, rules ("when flag X, give Y and show a text")
and dialogue text: see CONTENT.md §5 and `chest_swap`.

## 6. Lua scripts (`Script=`, ApiVersion 3)

```lua
-- main.lua
cleria.events.on("room_enter", function(e) cleria.log("entered " .. e.room) end)

cleria.ui.page("My Mod", function(p)             -- F1 > Mods > My Mod (a mod never gets its own tab)
    p:section("My Mod")
    local changed
    Enabled, changed = p:toggle("Enabled", Enabled, "Turns the thing on.")
    if changed then cleria.settings.set("Enabled", Enabled) end
end)
```

Everything a script can do is `cleria.*` (LUA_API.md): game events, flags and items, the player, the grant
filter (change what chests give), per-save storage, settings, overlays and a settings page (F1 > Mods > your mod)
in the shell's look, websockets and JSON, speedrun notes. Scripts are sandboxed (no file or OS access outside the mod's own
folder and its data folder). If a script errors, that mod alone is stopped for the session and the Mods tab
shows the file and line; the game goes on.

Tips:
- Keep state in locals; save what must survive a reload with `cleria.save.set` (it is written with the game's
  own save, and a load brings it back).
- Do work in `tick` (the game's 60 Hz) and network polling in `frame`; grant items when
  `not cleria.game.frozen()` (no cutscene or box on screen).
- Sizes in overlays: multiply by `d.scale` so they follow the window.
- Gameplay changes should default to the retail behaviour and say so ("Not a retail option.").

## 7. Sharing it: a `.cleriamod`

```
python tools/pack_mod.py samples/mods/my_mod -o dist
```

writes `dist/yourname.my_mod-1.0.0.cleriamod` (`<Id>-<Version>`): a zip with one folder named by the id, checked
(the manifest, the Script file) and byte-for-byte reproducible. Players double-click it to install. A `.cleriamod`
is a plain zip, so a `.zip` of the folder also works (dropped into the mods folder, or through *Install a mod
file...*). See `build/shell_re/MODFILE.md`.

## 8. Testing

- Watch the console: every `cleria.log` / `print` line is `[lua:<id>] ...`, every error has file:line.
- Scripted runs: `cleria.command("name", fn)` + `CLERIA_DRIVE="...;300:lua=name args"` drive a headless game
  (see `tools/drive_regress.py`, `tools/aplua_regress.py` for examples), with `CLERIA_INI=<scratch>\yso_win.ini`
  so the run uses a scratch settings and mods folder.
- `cleria-luatest` shows how to run a script against a fake game (apps/cleria-luatest).
- Your page in a headless shot: `CLERIA_LAUNCHER=settings CLERIA_SHELL_PAGE=Mods:<your mod id>` with `CLERIA_DRIVE`
  set (it writes `launcher.ppm` to `CLERIA_SHOT_DIR`), or the drive command `shell=Mods:<your mod id>` in a game.
