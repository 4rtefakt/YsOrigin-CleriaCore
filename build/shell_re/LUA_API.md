# Lua scripting for mods: the `cleria.*` API (mod API 3)

CleriaCore mods can carry a Lua script (MODS.md §10: the user chose Lua). This is the reference for modders
and the design record. None of it is retail: the game stays a faithful port; scripts are CleriaCore's own
layer, like the F1 shell. With no scripted mod turned on nothing here runs (every hook returns at once) and
the game, its saves and its frames are what they were.

| piece | where |
|---|---|
| Lua 5.4.9 (lua.org, MIT), vendored verbatim | `third_party/lua` (gitignored like the other vendored code); CMake target `lua54`, built **as C++** |
| the runtime: state, sandbox, budget, errors, the game's calls into a mod | `src/cleria/modscript.{hpp,cpp}` (target `cleria-script`) |
| the `cleria.*` API | `src/cleria/modscript_api.cpp` |
| the game side (the `Host`): live game, content layer, ui kit, shell | `apps/cleria-view/view_lua.inl` + one-line hooks in `view_game.inl` |
| tests | `apps/cleria-luatest` (ctest `luatest`, 56 checks), `tools/aplua_regress.py` (6 drive cases), `drive_regress shell_subpages` |
| samples | `samples/mods/hello`, `samples/mods/archipelago_lua` (§9) |
| the modder's guide | `samples/mods/README.md` |

Tags: **[CODE]** read in CleriaCore's code, **[H]** a choice of this design.

## 1. Opting in

```ini
; mod.ini
[mod]
Id=me.my_mod
Name="My Mod"
Version=1.0
ApiVersion=3          ; mods::kApiVersion is now 3 (2 = content/, 3 = Script=)
Script=main.lua       ; the entry file, relative to the mod folder
```

- The script runs once at startup (after the mods are mounted and `content/` is merged, before the first
  room), when the mod is **Active** in the Mods tab. It registers handlers, its page, layers; then the game
  calls them. Changes apply on restart, like every mod change (MODS.md §2).
- `Script=` with `ApiVersion` 1 or 2 is ignored with a note in the Mods tab ("needs ApiVersion=3").
- One **lua_State per mod**: mods never see each other's globals.
- `print(...)` and `cleria.log(...)` write `  [lua:<mod id>] text` to the console (and the mod's log).

## 2. The sandbox and the limits

| available | removed |
|---|---|
| base (`pairs`, `pcall`, `error`, `setmetatable`, `tostring`, `select`, ...), `string`, `table`, `math`, `utf8`, `coroutine` | `io`, `package`, `debug`, `dofile`, `loadfile`, `string.dump` |
| `os.clock`, `os.time`, `os.date`, `os.difftime` | every other `os` function (`execute`, `getenv`, `remove`, `exit`, ...) |
| `load(text [, name [, mode [, env]]])` (text chunks only) | binary (precompiled) chunks |
| `require("a.b")` -> `<mod>/a/b.lua`, once, cached | anything outside the mod folder (`..`, absolute paths, drives, links leaving it) |
| `collectgarbage("count" / "collect" / "step")` | `collectgarbage("stop")` and the other options |

- **Errors never reach the game.** Every call into a mod (its main chunk, an event, a filter, a layer, a
  page, a command) is a protected call. The first error is printed with its file:line and a traceback,
  shown as a notice card ("Mod stopped"), and in the Mods tab (the mod's row reads **Stopped**, its
  description the error, e.g. `script stopped: tick: main.lua:6: attempt to index a nil value (local 't')`).
  The mod is then **disabled for the session**: no more calls, its layers and pages stop (the page says so),
  its sockets close. Other mods and the game go on.
- **Instruction budget** [H]: 20 000 000 VM instructions per call (400 000 000 for the main chunk). An
  endless loop is stopped with "the script ran too long". Catching that error with `pcall` does not help: once
  spent, the budget hook runs on every instruction and raises again, and the mod is disabled when the call
  returns.
- **Heap cap** [H]: 96 MB per mod (Lua's own allocator; "out of memory" is an ordinary error).
- **Files** [H]: a file read or written by a mod is at most 16 MB; at most 4 websockets per mod.
- [not modelled] a pathological string pattern (`string.find` backtracking) runs inside C and is not
  interrupted by the budget; a mod that does this freezes the game until it finishes.
- Lua is built as C++, so a Lua error is a C++ exception that unwinds the API's own C++ frames (no `longjmp`
  over C++ objects).

## 3. Events: `cleria.events`

```lua
local id = cleria.events.on("room_enter", function(e) cleria.log("now in " .. e.room) end)
cleria.events.off(id)
```

| kind | when | fields of `e` (always `kind`, `room`) |
|---|---|---|
| `room_enter` | a room was entered (boot room, door, warp, load) | `prev_room` |
| `flag_changed` | a game flag changed (per-tick diff) | `index`, `old`, `new` |
| `chest_opened` | a chest's box flag went 0 -> set | `index`, `script` ("s_box01"), `object` ("box_01") |
| `item_obtained` | an item was granted (chest, script, pickup, rule, a mod...) | `item`, `count`, `source`, `script` |
| `death` | the player's HP reached 0 | |
| `new_game` / `load` | a session started / a save was loaded | `character` / `slot` |
| `save` | a slot was written (the save sidecar too) | `slot` |
| `tick` | every fixed game tick (60 a second, not while the F1 panel holds the game) | (no argument) |
| `frame` | every rendered frame, also while the panel is open | `dt` (seconds) |

These are the event bus of CONTENT.md §3, unchanged. **Order to know**: the first room of a session is
entered *before* `new_game` / `load` arrive (they come at the first tick); the save storage is already the
new session's when that `room_enter` runs (see `samples/mods/hello`).

`cleria.events.emit("item_obtained", {item = 0x59, count = 1, source = "mymod"})` reports a grant the mod
made itself (only `item_obtained` may be emitted).

## 4. The game: `cleria.game` and `cleria.player`

| call | |
|---|---|
| `game.in_game()` | a game session runs (after `new_game` / `load`) |
| `game.flag(i)`, `game.set_flag(i, v)` | the game flags `g_flags[0..511]` (`nil` / no-op with no game). Item counts are flags 0..127 (-1 never had) |
| `game.item(id)` | an item's count: retail ids 0..0x7F (the flag), mod items 0x100..0x1FF (the save sidecar) |
| `game.item_name(id)` | its name (INVINFO, with `content/items.ini` patches) |
| `game.upgrade(i)`, `game.set_upgrade(i, v)` | the 54 gear-upgrade cells (`XsoVm::item_arr`, retail 0x76A654) |
| `game.warp_unlocked(i)`, `game.set_warp_unlocked(i, on)`, `game.warp_to(i)` | the statue warp registry (0..23, the order of table 0x68c190); `warp_to` is op 0xa6 GoWarpPoint |
| `game.room()`, `game.scene()` | `"S_1001"`, `1001` |
| `game.character()` | 1 Yunica, 2 Hugo, 3 Toal (g_flags[150]) |
| `game.frozen()` | a cutscene or a box holds the world (grant things later) |
| `game.chests()` | the current room's chests: `{s_box01 = {flag = 302, object = "box_01"}, ...}` |
| `game.exp_factor(fn)` | `fn(level) -> factor` multiplies the EXP of every kill (with the Cheats rate, the AP seed's) |
| `player.hp()`, `max_hp()`, `set_hp(n)` | `set_hp(0)` kills (the game-over flow follows) |
| `player.sp()`, `add_sp(n)` | the SP wallet (the shop currency), 0..999999 |
| `player.level()`, `set_level(n)`, `exp()` | `set_level` = the AP bind's [H]: EXP at the level's threshold, max HP recomputed |
| `player.weapon_tier()`, `set_weapon_tier(t)` | g_flags[148] and the four weapon slots (0x7f SetWeaponLevel) |
| `player.refresh()` | recompute the stats after a grant (max HP) |

Writes go through the same paths the game and the C++ Archipelago glue use (`game.g_flags`, `player`, the
bind lambdas in `view_game.inl`). [H] Nothing checks that a write makes sense for the story: a mod that sets
flag 226 has beaten the final boss.

## 5. Content: the grant filter and gifts

```lua
-- every VM grant passes through here (CONTENT.md §4): q = {kind = "store" | "icon" | "window", sub, index,
-- old, value, text, script, object, owner, room}
cleria.content.add_filter(function(q)
    if q.kind == "store" and q.index == 0x59 and q.value > math.max(q.old, 0) then
        cleria.content.give(0x63, 1, "mymod")              -- give the Bronze Key instead
        return "suppress"                                  -- swallow the Panacea
    end
    if q.kind == "window" and q.index == 0x59 then         -- the "Acquired" box
        return {action = "replace", index = 0x63, text = cleria.content.treasure_text(q.text, "Bronze Key", 1)}
    end
    return nil                                             -- pass
end)
```

- Return `nil` / `"pass"`, `"suppress"`, or `{action = "replace", index =, value =, text =}` (missing fields keep
  the query's). Store: `index` / `value` = the cell written instead. Icon / Window: the item shown (an icon id
  of §7 works too) and the box text.
- Order: the Lua mods (load order), then `content/chests.ini`; the first decision that is not "pass" wins.
- `content.give(id, count, source)`: the inventory's own rules (count + n, gold -> SP, mod items 0x100.. in the
  sidecar); emits `item_obtained`.
- `content.treasure_text(text, name, count)`: the original box text with the item name (and ` x N`) replaced.

## 6. Storage

| API | where | lifetime |
|---|---|---|
| `cleria.save.get(key [, default])`, `save.set(key, value)`, `save.keys()`, `save.ready()` | the save sidecar `yso_NN.cleria`, section `[mod.<id>]` | per save: written with the game's save, read by its load, empty on a new game |
| `cleria.settings.get(key [, default])`, `settings.set(key, value)` | `cleria.ini` section `[cleria.mod.<id>]` | per install, saved at once |
| `cleria.data.read(name)`, `write(name, text)`, `exists(name)`, `remove(name)` | `<settings folder>/modsdata/<id>/<name>` (sub-folders allowed) | files: caches, exports for trackers |

- Save values are any JSON-able Lua value (tables included), stored as JSON text (%-escaped in the INI so any
  text survives). `set(key, nil)` removes. Keys: 1-64 of `A-Z a-z 0-9 _ . -`.
- `save.ready()` is false while a New Game / Load is still queued (the sidecar was just replaced): do not write
  then (the Archipelago mod's guard).
- Settings are one line of text; `get` converts to the default's type (`get("Port", 38281)` -> a number,
  `get("On", false)` -> a boolean; stored `1` / `0`).
- In a run without a settings file (headless tests) mods are off anyway (MODS.md §2).

## 7. UI: hud layers, the mod's page, icons

```lua
cleria.hud.layer("corner", function(d)           -- every frame, over the game (layers of a mod in name order)
    local s = d.scale                            -- d.w, d.h: the window; d.scale = height / 1000 (the shell's S)
    d:rect(14 * s, 14 * s, 200 * s, 30 * s, {color = 0x100F0B, alpha = 0.7, rounding = 4 * s})
    d:text(26 * s, 20 * s, "Hello", {size = 15 * s, color = "gold", font = "medium"})
end)

cleria.ui.page("My Mod", function(p)              -- its page: F1 > Mods > My Mod, in the shell's look
    p:section("Options")
    local changed
    MyOn, changed = p:toggle("Enabled", MyOn, "What this does. Not a retail option.")
    Volume, changed = p:slider("Volume", Volume or 5, 0, 10, 1, "%.0f", "From 0 to 10.")
    Mode, changed = p:choice("Mode", Mode or 1, {"Easy", "Hard"}, "1-based index.")
    Name, changed = p:text_field("Name", Name or "", {desc = "Typed text.", secret = false, empty = "(none)"})
    if p:button("Do it", {desc = "Runs it.", enabled = MyOn}) then cleria.notice("Done!") end
    p:info("Status", "ready", {color = "good", desc = "A read-only row."})
end)

cleria.ui.icon(0x2A1, "icons/MY_ICON.DDS")        -- an item-icon id for the chest sprite / treasure box
cleria.notice("Something happened", "My Mod")     -- a notice card (5 s)
```

- Draw calls (only inside a layer function; elsewhere they are an error): `d:text(x, y, s, opts)`,
  `d:text_size(s, opts) -> w, h`, `d:rect(x, y, w, h, opts)` (`fill = false` + `thickness` for an outline),
  `d:line(x0, y0, x1, y1, opts)`, `d:circle(x, y, r, opts)`, `d:image("path/in/mod.dds", x, y, w, h, {alpha})`.
- Colours: `color = 0xRRGGBB` or a kit name — `gold`, `label`, `value`, `muted`, `desc`, `good`, `warn`,
  `section`, `panel`, `edge`, `row`, `white`, `black` (also as numbers in `cleria.ui.colors`); `alpha` 0..1.
  Fonts: `body` (default), `medium`, `bold`, `cond`, `cond_medium` (Fira Sans / Condensed).
- **Where a page shows** (since 2026-10-07): a mod never gets an F1 tab of its own (SHELL_CORE.md §9, the tab
  policy). `cleria.ui.page(title, fn)` draws `fn` on the mod's own page, **F1 > Mods > <mod name>** (the Mods
  list's row of the mod opens it: OK / a click; a gear on the row marks a mod with settings), above that page's
  On / Off, load order and details. The page's first row is the way back ("<  MODS / <MOD NAME>"; Esc, Backspace
  and the pad's Back also return). Start the page with your own `p:section(...)`. A mod that calls `cleria.ui.page`
  more than once gets every page on that one page, each under a section titled with its `title`; with a single
  page the title is not shown (the breadcrumb shows the mod's `Name`). The signature is unchanged.
  `CLERIA_SHELL_PAGE=Mods:<mod id>` (or the drive's `shell=Mods:<mod id>`) opens it in a headless capture.
- Page widgets return the new value and whether it changed (`button` returns whether it was pressed). The last
  argument is the description string or an options table (`desc`, `enabled`, `secret`, `empty`, `color`).
  Store the returned value yourself (the page is redrawn every frame).
- Pages and icons must be registered by the main chunk (the Mods tab and the icon caches are built after it);
  layers may be added or removed any time (`cleria.hud.layer(name, nil)` removes).
- Icon ids for mods: 0x200..0x3FF (outside every item id and flag). The DDS loader takes DXT1/DXT3 and
  uncompressed images (MODS.md §3). Images and icons come from the mod's own folder.

## 8. Net, JSON, commands, speedrun

```lua
local ws = cleria.net.websocket()                 -- the WinHTTP WebSocket (src/cleria/net/websocket)
ws:open("wss://example.org:443/")                 -- or "host:port": tries wss://, then ws://
cleria.events.on("frame", function()
    for _, e in ipairs(ws:poll()) do              -- never blocks: {kind = "open" | "message" | "close", text, error}
        if e.kind == "message" then local v = cleria.json.decode(e.text) end
    end
end)
ws:send(cleria.json.encode({cmd = "Hello", tags = cleria.json.array()}))
-- ws:state() -> "idle" | "connecting" | "open" | "closing" | "closed"; ws:url(); ws:close()
```

- `cleria.json.encode(v [, {indent = n}])`, `cleria.json.decode(s) -> v | nil, err` (src/cleria/json: int64
  exact, NaN accepted). JSON `null` is `cleria.json.null`; decoded arrays keep an array mark, so they re-encode
  as arrays; `cleria.json.array(t)` marks a table (an empty table is otherwise an object); integer keys become
  strings; `cleria.json.is_array(t)`.
- `cleria.command(name, fn(args))`: a command for tests and tools: `CLERIA_DRIVE="...;300:lua=<name> <args>"`.
- `cleria.speedrun.modification(fn | "text")`: a line (or a list) for speedrun mode's "modified run" list,
  e.g. "Archipelago session (Lua)". Every active mod already marks a run as modded (MODS.md §5).
- Also: `cleria.api_version` (3), `cleria.mod` (`id`, `name`, `version`), `cleria.time()` (seconds, monotonic),
  `cleria.unix_time()`, `cleria.read(path)` / `cleria.exists(path)` (the mod's own files).

## 9. The proof: Archipelago as a Lua mod

`samples/mods/archipelago_lua` is **CleriaCore's Archipelago client** for the Ys Origin apworld, written in Lua
on the API above **only** (no AP-specific C++). It replaced the built-in C++ client (AP_GAME.md, ARCHIPELAGO.md:
both superseded), which was removed on 2026-10-07 once these cases proved the Lua mod equivalent. It is
self-contained (its own `README.md` for players, its table and icon) so it can be published on its own.
`ap/client.lua` (the protocol state machine: RoomInfo, GetDataPackage / DataPackage
with a cache, Connect, Connected / ConnectionRefused, ReceivedItems with the index-0 reset, overlap and gap ->
Sync + LocationChecks, LocationChecks kept and re-sent after each login, LocationScouts / LocationInfo,
StatusUpdate with the final goal, Bounce / Bounced DeathLink with the echo filter, the keep-alive, Say,
PrintJSON, RoomUpdate; the session with reconnect backoff and the status line), `ap/text.lua` (PrintJSON
colours), `ap/ys.lua` (the Ys Origin rules: slot_data options, the registry from `location_signals` /
`location_detect`, flag / item_arr / bit / scene / floor detection, claims, suppression, every receipt kind,
the New Game loadout, warp locks, the goal, EXP scaling, the save block), `ap/ui.lua` (its page, **F1 > Mods >
Archipelago**, and the overlays). Its table `data/ys_origin.json` is written there by `tools/ap_import.py` (from
the unchanged apworld); its icon `icons/AP_ITEM.DDS` by `tools/make_ap_icon.py`. The location / item mapping and
the slot_data contract it implements are ARCHIPELAGO.md §5 and AP_GAME.md §3-6 (kept as the reference).

**Verified** against `tools/ap_mock_server.py` (slot_data fixture `tests/ap/test_slot_data.json`):
`tools/aplua_regress.py` plays `aplua_chest`, `aplua_items_save`, `aplua_deathlink`, `aplua_goal_traps`,
`aplua_seed_guard`, `aplua_off` — all pass. When the built-in C++ client still existed, these were the six
`ap_*` scenarios of its `tools/ap_regress.py` (same rooms, same drive timing), and beyond the cases' checks the
mod's whole event log (connect, stamp, loadout, room checks, scouts, suppression, the store check, the box text,
every grant, DeathLink in and out, the traps, the goal, the seed guard) was **line-for-line identical** to the
C++ client's `[ap]` log in all five connected scenarios, and the packets the server received were identical
too (uuid and times masked).

**What changed from the removed C++ client** (all deliberate or cosmetic; a player of the C++ client starts
over in the mod's settings):
- Its settings are `[cleria.mod.ysorigin.archipelago]` (the same keys as the old `[cleria.archipelago]`,
  which is now ignored); its save block is the sidecar's `[mod.ysorigin.archipelago]` `state` (JSON), not
  `[ap]` (an old sidecar's `[ap]` block is ignored and dropped at the next save). Its caches (data packages,
  slot_data) and `state.json` live in `modsdata/ysorigin.archipelago/`, not `archipelago/`.
- Its foreign-item icon id is 0x2A1 and comes from the mod folder (no `IMAGE/AP_ITEM.DDS` vfs reskin: replace
  the mod's file instead).
- The fog trap's haze sorts under the mod's own overlays only; the status line reads "AP".
- Chaos Warp picks its statue with its own LCG (the C++ used `std::mt19937` seeded 0xA9). The uuid comes from
  `math.random` seeded by the clock.

**API gaps found and filled** while porting (each now generic API): per-save storage (the sidecar had only
the `[ap]` block -> `[mod.<id>]` sections), per-mod settings (`SettingsStore::cleria_doc_mut`), a data folder
(the caches and the tracker file), mod icons in the icon caches (`cleria.ui.icon`), the EXP factor hook,
`events.emit("item_obtained")`, `save.ready()` (the C++ glue's `ap_save_pending`), test commands, and a
"Stopped" state for a mod whose script failed (`mods::Mod::stopped`).

## 10. Tests

```
cmake --build build_diff --config Release --target cleria-luatest cleria-nettest cleria-view
ctest --test-dir build_diff -C Release -R "luatest|nettest"
python tools/drive_regress.py --exe build_diff/Release/cleria-view.exe --assets <assets> aplua_chest aplua_items_save aplua_deathlink aplua_goal_traps aplua_seed_guard aplua_off shell_subpages
```

`cleria-nettest`: the WebSocket under `cleria.net` (address parsing; a loopback echo server: wss -> ws fallback,
echo, a 2 MB message, idle beyond the handshake timeout, a kick, a polite close, a close the server ignores,
nothing listening, a bad address). `cleria-luatest` (56 checks): the sandbox (no io / package / debug / dofile / os.execute / getenv /
string.dump, bytecode refused, `require` / `cleria.read` / `cleria.data` cannot leave their folders, print to
the log); a runaway handler stopped in milliseconds, also through `pcall`, a runaway main chunk, a heap bomb,
errors with file:line (main chunk, handler, syntax, a module), a disabled mod getting no calls, other mods
unaffected; every API family round-tripped through a fake host (events, flags, player, warps, give, emit,
chests, treasure_text, save storage incl. quotes / backslashes / newlines / % through a real sidecar,
settings typing, data files, json incl. null / int64 / empty arrays, the filter's three decisions, icons,
hud layers in order with colours / alpha / kit names / images, page widgets, exp_factor, speedrun,
commands, draw calls refused outside a layer); websockets (the 4-socket limit, a bad address, a refused
connection reported by `poll`); the manifest's `Script=`; both samples start; archipelago_lua's own table (336
locations, 147 items), its page "Archipelago", its README and icon (self-contained).
