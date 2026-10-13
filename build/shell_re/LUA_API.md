# Lua scripting for mods: the `cleria.*` API (mod API 3, extended at 4, 5 and 6: §11)

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
| tests | `apps/cleria-luatest` (ctest `luatest`, 65 checks), `tools/aplua_regress.py` (16 drive cases), `drive_regress shell_subpages`, the `lua_*` drive cases (API 4-6) |
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
ApiVersion=3          ; 2 = content/, 3 = Script=; mods::kApiVersion is now 6 (§11), 3 to 5 still run unchanged
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
| `boss_defeated` (API 4) | a boss fight was won, at the tick its flag is written (below) | `flag` (220..227, absent for a duel / mid-boss) |
| `item_used` (API 4) | an item was used from the book and used up: the Celcetan Panacea (0x59), the only item the book consumes | `item` |
| `door_opened` (API 4) | a locked door was opened with its key or medallion: the room's `UseKey` script stored the door's flag (0 -> set), and no filter changed that store | `flag` (the door's flag), `script` ("usekey") |
| `blessing_bought` (API 5) | a purchase at the blessing shop reached its effect (op 0xAF), after the shop filters answered (§5.1) | `index` (the blessing, §5.1), `price` (the SP the script deducted), `suppressed` (a filter kept the effect from being applied) |
| `enemy_killed` (API 6) | a monster died of a hit of the player's (his swing, a sword trail, a shot, a skill's effect), after its EXP and SP were credited and before its drops are rolled | `cast` (the CASTINFO id), `level`, `exp` and `sp` (what was credited, after the filters of §5.2; both 0 for a revived monster killed again), `x`, `y`, `z` (its origin) |
| `pickup_collected` (API 6) | a world pickup was taken (an enemy's drop, a script's, a mod's) | `item`, `what` and `value` and `ticks` (what it applied: §5.2 "pickup"), `suppressed` (a filter kept it from applying anything), `x`, `y`, `z` |
| `new_game` / `load` | a session started / a save was loaded | `character` / `slot` |
| `save` | a slot was written (the save sidecar too) | `slot` |
| `tick` | every fixed game tick (60 a second, not while the F1 panel holds the game) | (no argument) |
| `frame` | every rendered frame, also while the panel is open | `dt` (seconds) |

These are the event bus of CONTENT.md §3, unchanged. **Order to know**: the first room of a session is
entered *before* `new_game` / `load` arrive (they come at the first tick); the save storage is already the
new session's when that `room_enter` runs (see `samples/mods/hello`).

`cleria.events.emit("item_obtained", {item = 0x59, count = 1, source = "mymod"})` reports a grant the mod
made itself (only `item_obtained` may be emitted).

**`enemy_killed`** [CODE `view_game.inl` `player_hit_enemy`, the kill block FUN_004fbb40 0x4fcaf5..] is raised for monsters
only: a boss is `boss_defeated` (its EXP and SP still pass the filters of §5.2 with `source = "boss"`), and a
monster removed by a script (`killall`, a scene change) or by its own brain is not a kill and credits nothing.
**`pickup_collected`** comes after the `item_obtained` (`source = "pickup"`) the same touch raises.

**`boss_defeated`** (`view_content.inl` `content_boss_watch`) [CODE] is raised from the same per-tick flag
diff as `flag_changed`, just before it, so it never fires on a load, a Retry or a new game (they re-prime the
diff), and never on a lost fight. `e.room` is the room of the fight. Two rules:
- **A floor boss**: its flag 220..227 goes 0 -> 1. `e.flag` is that flag. S_1099 220, S_2099 221, S_3099 222,
  S_4099 223, S_5099 224, S_6082 225 (Zava's module, or Toal's `BATTLE_THOR` store), S_7080 226, S_7099 227
  (Darm's later 227 = 2 is not a second event). Not in boss rush: the modules write no flag there and clear
  156 themselves, so a rush fight would end by the second rule in its own room [H: not driven].
- **A duel / mid-boss**: `g_flags[156]` ("a boss fight runs", retail 0x76bb8c) goes 1 -> 0 in the room where
  it went 0 -> 1, the room is not one of the eight above, and the clear was not the player's KO in a losable
  duel (flag 133: Hugo's and Yunica's S_3080). No `e.flag`. Every such fight's script sets 156 = 1 and waits
  for the opponent: S_1080 (Vagullion / Epona / Galleon), S_2080 (the three `BATTLE_n`), S_3080 (Kishgal at
  half HP / Toal / Shion), S_4005 (the three `BATTLE_*`), S_4013 (Jenocres: Hugo, Yunica), S_6080, S_6099,
  and the smaller fights that use the same flag (S_1000's `BATTLE_n`, S_3002's `KEKKAIZAKO_02`).
- No event, because there is no 156 fight: Toal's S_4013 (two talks with Epona, flags 244 and 281; Jenocres
  is Hugo's and Yunica's), S_5080 (its only fight is the ward `KEKKAIZAKO_01`, which stores 423 = 1: watch
  that flag) and S_4080 (no fight in its scripts: the room script starts two `roll_edge` traps only).
- A mod that writes these flags itself raises the event too (the diff does not know who wrote).

**`door_opened`** covers the twelve doors that have a `UseKey.XSO` [DATA]: S_1005 315 (Bronze Key), S_1011 323,
S_2012 342, S_3014 376, S_4001 383, S_4014 440, S_4020 405, S_5005 448, S_5009 449 (Dragonbone Key), S_5014 428
(Mantid Medallion), S_6010 473, S_6053 472. The key's own cell is set to 0 by the same script right after (a
`store` query and a `flag_changed`). Switch doors, wards and the evil door are other scripts: no event.
The tools the book starts as scripts (`@UseHammer`, `@UseHarmonica`, the Dreaming Idol, the wing) are not
consumed and raise no `item_used`.

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
| `game.save(slot)` (API 4) | save the game now into slot 0..63; `true` if it saved, `false` if not (below) |
| `game.unlock_character(c)` (2026-10-09; test `cleria.game.unlock_character ~= nil`) | Character Select offers character `c` for the rest of this session whatever the clear data says (1 Yunica, 2 Hugo, 3 Toal; only Toal is ever locked on a New Game). Not saved; callable on the title — "frame" handlers run there too. The player's own switch is `[cleria.qol] UnlockCharacters`. |
| `game.exp_factor(fn)` | `fn(level) -> factor` multiplies the EXP of every kill (with the Cheats rate, the AP seed's); a factor outside 0..1000 counts as 1. It multiplies **before** the engine floors the award: for a per-kill rule use the `"exp"` filter (§5.2) |
| `game.spawn_pickup(item, x, y, z [, {value = n}])` (API 6) | a collectable pickup thrown up from that point as an enemy's drop is; `true` if it was spawned (§5.2) |
| `player.hp()`, `max_hp()`, `set_hp(n)` | `set_hp(0)` kills (the game-over flow follows) |
| `player.sp()`, `add_sp(n)` | the SP wallet (the shop currency), 0..999999 |
| `player.level()`, `set_level(n)`, `exp()` | `set_level` = the AP bind's [H]: EXP at the level's threshold, max HP recomputed |
| `player.weapon_tier()`, `set_weapon_tier(t)` | g_flags[148] and the four weapon slots (0x7f SetWeaponLevel) |
| `player.refresh()` | recompute the stats after a grant (max HP) |
| `player.add_exp(n)` (API 6) | `n` > 0 EXP added to the total, then the game's level-up loop (max HP, the full heal, the popup); no filter, no factor |
| `player.stats()` (API 6) | a table of everything below (§4.1), `nil` with no game |
| `player.set_modifier(stat, add [, mul])`, `player.modifier(stat)` (API 6) | this mod's modifier of `"max_hp"`, `"atk"` or `"def"` (§4.1) |

Writes go through the same paths the game and the C++ Archipelago glue use (`game.g_flags`, `player`, the
bind lambdas in `view_game.inl`). [H] Nothing checks that a write makes sense for the story: a mod that sets
flag 226 has beaten the final boss.

**`game.save(slot)`** [CODE `view_game.inl` `g_lua.b.save`] writes the slot's record `yso_NN.bin` and the mods'
sidecar `yso_NN.cleria`, and it raises `save` (`e.slot`). It does NOT move the game over's Retry point (the
continue snapshot, which the book's Save does take): retail moves it at statue rooms, battle starts, saves and
loads only (build/gameflow_re/DEATH_RETRY.md), and a mod's autosave in an ordinary room would turn Retry into
"start again in this room". The player can ask for that: Gameplay > Game over > "Retry from mod saves"
(`[cleria.access] RetryAtModSave`, off by default) makes the call take the snapshot too (it did always, before
2026-10-13). `slot` is the file number: 0..63, the book's "No.01".."No.64" minus one (the same number `save` /
`load` events carry). It never raises an error; it returns `false` and writes nothing unless all of these hold:
- a session runs and its `new_game` / `load` was delivered (`game.in_game()`, `save.ready()`);
- `flag(185) == 1`, `flag(156) == 0`, `flag(190) == 0` and the room was entered at least 60 ticks ago (the
  retail Archipelago mod's autosave conditions: the player has the controls, no boss fight, no arena);
- no cutscene or box holds the world (`not game.frozen()`), the player is alive, and no door or scene change
  is staged;
- `slot` is 0..63 and the file could be written.
A load of that slot (the title's or the book's) arrives where the player stood, on the floor below that point.

### 4.1 The player's numbers and the stat modifiers (API 6)

`cleria.player.stats()` returns what the game holds [CODE `view_game.inl` `g_lua.b.info`; `src/cleria/inventory.cpp`
`compute_stats` = FUN_00420c40]:

| field | |
|---|---|
| `level`, `exp`, `exp_next` | the level (1..60), the running EXP total (a float: a kill adds fractions), the total the next level needs (0 at level 60) |
| `hp`, `max_hp`, `sp`, `weapon_tier` | as `player.hp()` ... |
| `atk`, `def` | STR and DEF as every hit reads them: the level curve + the character's offset + the Strength / Defense Drop buff + the weapon tier, the armor, the leggings and the accessory, **with** the mods' modifiers |
| `base_max_hp`, `base_atk`, `base_def` | the same without the modifiers |
| `luck` | FUN_00420c40's luck (0 in every game: its base 0x76b90c has no writer). [not modelled] the drop rolls do not read it yet |
| `mp`, `max_mp` | the skill gauge 0x76a738 / 0x76a734 (3000 full) |
| `boost`, `boosting` | the Boost gauge (30000 = full) and whether a Boost runs |
| `atk_buff`, `def_buff`, `mp_buff`, `atk_buff_ticks`, `def_buff_ticks`, `mp_buff_ticks` | the Strength / Defense / MP Drop buffs (+ATK up to 5, +DEF up to 3, the MP regen factor 1..3, 0 = none) and their ticks left (60 a second) |
| `x`, `y`, `z` | the player's feet (world units, Y up) |

Armor and leggings have no level of their own: each piece is "strengthened" or not, `game.upgrade(item id)` 0 / 1.

`cleria.player.set_modifier(stat, add, mul)` [H]: the stat becomes **(the game's value + add) x mul** wherever the game
reads it (every hit dealt and taken, the book's status page, max HP). `stat` is `"max_hp"`, `"atk"` or `"def"`; `add`
within -9999..9999, `mul` within 0..100 (default 1); anything else is a Lua error. `set_modifier(stat, 0, 1)` (or
`set_modifier(stat)`) removes it. `player.modifier(stat)` returns this mod's `add, mul`.
- It is applied at **every recompute** [CODE `player_stats`, `lua_stat_modifiers`]: a level-up, an equipment change, a
  buff, a load, a new game keep it. It lasts until the mod changes it or CleriaCore exits, and is **not saved**: a mod
  that wants it per save keeps its numbers in `cleria.save` and sets them again on `load` / `new_game`.
- Several mods: the adds are summed, the muls multiplied. Max HP stays within 1..999 (the game's own cap), ATK at 0 or
  above. Max HP follows at once; a lowered max takes the HP above it, a raised one does not heal.
- [H] There is no modifier for luck (nothing reads it), the MP gauge (not a derived stat: 3000 in every game) or the
  Boost gauge. The `"damage"` filter (§5.2) covers what a modifier cannot.

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

### 5.1 The blessing shop: `cleria.shop` (API 5)

The goddess statues' blessing menu is three kinds of script [DATA `assets/MAP/S_COMMON`; BLESSINGS.md §2]:
`@GrowMenu` builds the rows (op 0xDD `Menu_AddSpShop(text, script, price)` for a row on sale, op 0xD8 for a
"[Done]" row), and the chosen row's script (`@Grow00`..`@Grow25`, `@GrowAr`, `@GrowLe`) asks "already applied?"
(op 0x9A), tests the wallet (op 0x61 `flag 216 >= price`), deducts (op 0x69 `flag 216 -= price`) and applies the
effect (op 0xAF). A mod can answer each of those:

```lua
cleria.shop.add_filter(function(q)
    -- q = {kind = "price" | "row" | "owned" | "buy", index, value, sub, text, script, room}
    if q.kind == "price" then return {action = "replace", value = 500} end           -- every blessing costs 500 SP
    if q.kind == "row" and q.value >= 0 then                                          -- a row on sale
        return {action = "replace", text = "Mystery blessing - [SP:]" .. q.value}
    end
    if q.kind == "owned" then return {action = "replace", value = MyBought[q.index] and 1 or 0} end
    if q.kind == "buy" then MyBought[q.index] = true; return "suppress" end           -- grant something else
    return nil                                                                        -- pass
end)
cleria.events.on("blessing_bought", function(e)       -- e.index, e.price, e.suppressed, e.room
    if e.suppressed then cleria.content.give(0x59, 1, "mymod") end
end)
```

`q.index` is the blessing as the scripts number it: `nn` of `@Grownn`, **0..6 and 9..25** (the bit of flag 217 is
`nn` for 0..6 and `nn - 2` for 9..25; the list with names and vanilla prices is BLESSINGS.md §2), **7** = "Strengthen
current armor" (`@GrowAr`) and **8** = "Strengthen current leggings" (`@GrowLe`).

| `q.kind` | when | `q.value` | `q.sub` | what a decision does |
|---|---|---|---|---|
| `"price"` | a script reads a blessing's price | the script's (vanilla) price | 0xDD (221) the menu row, 0x61 (97) the "enough SP?" test, 0x69 (105) the deduction; for the armor / leggings 0xB0 (176), the cost their test and deduction then read from flag 218 | `{action = "replace", value = n}`: the price is `n` at that step. Answer the same number at every step (key it on `q.index`), or the row shows one price and the script charges another |
| `"row"` | a row is added to the menu | the price shown (after `"price"`), or -1 for a "[Done]" row | 0xDD / 0xD8 (216) | `{action = "replace", text = s}`: the row reads `s`. `q.text` is the vanilla text, e.g. `"Increase item drop rate - [SP:]24000"` or `"... - [Done]"` |
| `"owned"` | an `@Grow*` script asks whether blessing `q.index` is applied: the menu (which tier's row to show, "[Done]") and the row's script ("already applied") | the game's answer, 0 or 1 | 0x9A (154) | `{action = "replace", value = 0 | 1}`: the answer the script gets. The game's own readers of the blessing bits (the effects) are not asked |
| `"buy"` | the purchase's effect is about to be applied, after the SP was deducted | the SP deducted | 0xAF (175) | `"suppress"`: flag 217's bit (or the armor / leggings level) is not written; `{action = "replace", index = n}`: blessing `n` is applied instead |

- Return `nil` / `"pass"` to keep the game's own value. `"suppress"` means something for `"buy"` only.
- `q.script` is the script instance (`"@GrowMenu"`, `"@Grow03"`: the case its starter used), `q.room` the room;
  `q.text` is empty except for `"row"`.
- Order: the Lua mods in load order; the first decision that is not "pass" wins.
- **`blessing_bought`** is raised right after the `"buy"` decision, also when no mod has a shop filter.
  `e.index` is the blessing the script bought (not a replaced one), `e.price` the SP deducted, `e.suppressed`
  whether a filter suppressed the effect.
- A suppressed blessing leaves no trace in the game's state: keep what was bought in `cleria.save` and answer
  `"owned"` from it, or the row stays on sale and can be bought again. With tiers (LV1 -> LV2 -> LV3) the menu
  shows the first tier whose `"owned"` answer is 0.
- These queries do **not** pass through `cleria.content.add_filter` (a filter written for API 3 / 4 never sees
  them). The deduction is still an ordinary store of flag 216 for the grant filter (`sub` 0x69, the new value).
- [H] The wallet test and the deduction are the script's own: a price above the wallet answers "You don't have
  enough SP." like any other.

### 5.2 EXP, SP, damage, drops and pickups: `cleria.combat` (API 6)

```lua
local M = 100                                             -- the seed's EXP multiplier
cleria.game.exp_factor(function(level) return M end)
cleria.combat.add_filter("exp", function(q)               -- every kill gives at least M / 2
    if q.source == "kill" then return math.max(q.value, M / 2) end
end)
cleria.combat.add_filter("pickup", function(q)
    if q.what == "sp" then return q.value * 2 end         -- gold is worth double
    if q.what == "atk_buff" then return {action = "replace", value = 1, ticks = 3600} end
end)
```

`cleria.combat.add_filter(kind, fn)` [H] registers `fn(q)` for one kind of number the game is about to apply:
`"exp"`, `"sp"`, `"damage"`, `"drop"`, `"pickup"` (any other kind is a Lua error). The game asks at each occurrence,
inside the fixed tick: keep the function cheap. With no filter of a kind the game builds no query and calls no Lua
[CODE `view_lua.inl` `lua_combat_on`], and a run without filters is bit for bit what it was.

**The answer**: `nil` / `"pass"` keeps the game's value; `"suppress"` makes it nothing (0 EXP / SP / damage, no drop,
a pickup that applies nothing); a **number** is the value to use instead; `{action = "replace", value =, item =,
ticks =}` replaces what the kind allows (missing fields keep the query's). A value that is not finite (NaN, inf) is
ignored (pass); a value outside the kind's range is clamped. A table without `action` passes. Order: the Lua mods in
load order, a mod's filters in registration order; the first answer that is not "pass" wins. A filter that raises an
error stops its mod like any other call (§2) and the game's value stands.

Every query has `kind`, `value`, `room`. Whole numbers arrive as Lua integers.

**`"exp"`** [CODE `view_game.inl` `award_exp`; `src/cleria/exp_award.cpp` = FUN_004fa2f0, FUN_004fbb40 0x4fcb45..0x4fcc13,
FUN_004fa4a0]: every EXP award above 0. The engine's order, each step a field:

| step | field | |
|---|---|---|
| | `source` | `"kill"` (a monster) or `"boss"` (FUN_004fa4a0; none in time attack) |
| | `cast`, `boss` | a kill: the monster's cast id; a boss: its EEX record 0..6 |
| | `level`, `enemy_level` | the player's level, the monster's |
| 1 | `base` | the monster's EXP (ENEMYINFO, victim +0xb0) |
| 2 | `scaled` | the level difference: `base` / F^(level - enemy_level) when the player is above (F = 1.5 Normal, **1.65 Hard**, 1.8 Nightmare), `base` x 1.5^min(enemy_level - level, 3) when below; then x 0.6666 |
| 3 | `rate`, `factor` | x the Cheats' EXP rate, x the mods' `exp_factor(level)` (retail's rate 0x76a5fc = 1) |
| 4 | `diff_mult` | x 1.1 on Easy, x 2 on Very Easy |
| 5 | `floored` | floor, then **at least 1**: `max(1, floor(scaled x rate x factor x diff_mult))` |
| 6 | `combo`, `chain` | a kill: x `chain` = 1 + 0.01 x `combo` (the hit combo meter, 0..99); a boss: `chain` 1 |
| | `value` | `floored` x `chain`: what is added to the EXP total (a float) |

The answer replaces `value` (0..9999999). The multipliers of step 3 come **before** the floor of step 5, so "at least 1"
protects nothing once a factor is set: 0.147 x 100 is floored to 14 (drive case `lua_exp_floor`). Not asked when the
award is 0 (a revived monster killed again, a boss without EXP).

**`"sp"`**: every gain of the SP wallet the game makes. `source`: `"kill"` (the monster's SP, `cast`), `"boss"`,
`"pickup"` (a gold piece, after the `"pickup"` filter), `"give"` (a gold item granted by `content.give`, a content
rule or chest), `"script"` (0xb2 AddPlayerSP: an SP chest, a reward; EventXX 13, the clear data's SP bonus). `base` is the amount
before the game's own scale (a kill / boss: FUN_004fa390's Easy x 1.5, Very Easy x 2, Nightmare x 0.01, the blessing's
x 1.25, the Cheats' SP rate), `value` what would be credited. The answer replaces it (0..999999). Not asked for
`player.add_sp`, for a spend, or for a script that stores flag 216 directly (that is a `store` of §5).

**`"damage"`** [CODE `player_hit_enemy` = FUN_004fbb40, `hurt_player` = FUN_004fd350; `src/cleria/hit.hpp`]: the final
damage of one landed hit, just before it leaves the HP.

| field | `dir = "dealt"` (the player's hit) | `dir = "taken"` (a hit on the player) |
|---|---|---|
| `cast`, `level` | the monster hit; absent for a boss part | the monster whose hit volume it is; absent for a boss, a trap, a shot, a script |
| `atk`, `power` | the player's ATK (`stats().atk`) and the swing's own power | the attacker's ATK and its swing's power |
| `def` | the victim's DEF | the player's DEF (- 5 while cursed) |
| `class` | the damage class 0..7 (3 melee, the others the elements; hit.hpp) | absent |
| `formula` | FUN_004fa550's result: ATK^0.66 x (ATK - 0.98 DEF + 10 + power) / 22.92, x (1 - resist), the swing's minimum, the random rounding, at least 1, Yunica's x 1.1 | FUN_004fa740's result with the difficulty's tail |
| `value` | after the victim's guard (0), the Cheats' rate, the juggle factor, one-hit-kill | after the Boost halving, the Cheats' rate, the skill armour's x 0.75 |

The answer replaces `value` (0..999999, truncated). A guarded hit arrives with `value` 0 (a mod may still give it
damage); the player's guards (Hugo's shield and barrier, the MP armour) end the hit before the filter. Everything after
the damage follows the answer: the kill, the combo meter (a hit of 0 does not count), the reaction, the Boost gained.
[H] Hits on scene objects (breakables, switches) are not asked.

**`"drop"`** [CODE `src/cleria/pickup.cpp` `roll_enemy_drops` = FUN_00478510]: one drop of a killed monster that
**passed its roll**. `item` (the item id) and `name`, `slot` (0..7 the ENEMYINFO drop slot, 8 / 9 / 10 the Strength /
Defense / MP Drop scatter), `chance` (the percentage its roll was under: the slot's rate with luck, the launch and
combo bonuses and the Cheats' drop rate; 0 for the scatter), `cast`, `level`, `x`, `y`, `z`; `value` is -1.
`"suppress"`: nothing drops. `{action = "replace", item =, value =}`: another item drops (0..0x7F, a mod item
0x100..0x1FF; any other id keeps the original), and `value` >= 0 becomes that pickup's **own value**, used instead of
the game's when it is taken (below). A drop that failed its roll is not asked: to add drops, spawn them from
`enemy_killed`. [H] A drop has no value of its own in the game: what an item gives is decided when it is taken.

**`"pickup"`** [CODE `src/cleria/inventory.cpp` `pickup` = FUN_00585ff0, `PickupFx`]: a world pickup is being taken.
`item`, `name`, `x`, `y`, `z`, and by `what`:

| `what` | items | `value` (replaceable, 0..999999) | `ticks` |
|---|---|---|---|
| `"sp"` | gold 0x78..0x7F | the SP credited (the piece x the Cheats' SP rate, x 0.01 on Nightmare) | |
| `"hp"` | Recovery Drops 0x42..0x47 | the HP healed (the blessings' x 1.25 / x 1.5 included) | |
| `"atk_buff"`, `"def_buff"`, `"mp_buff"` | 0x48..0x4D | the step added to the buff (0.5 / 0.3 / 0.2, doubled by the larger drop; the buffs' caps 5 / 3 / 3 stay) | the buff's duration (1800, 2000 or 2200 with the blessings); replaceable |
| `"boost"` | Vitality Seed 0x72 | the Boost gauge it is set to (30000; applied only once Boost is unlocked, flag 187) | |
| `"oxygen"` | Air Seed 0x73 | the oxygen it is set to (6000) | |
| `"item"` | anything else | 1 (the count gained; not replaceable) | |

`"suppress"`: the pickup is taken and applies nothing (grant something else from `pickup_collected`, whose
`suppressed` is then true). A pickup with its own value (a `"drop"` answer's, `spawn_pickup`'s) arrives with it as
`value`. A mod item (0x100..) taken from the world is not asked (it is `content.give`'s). The item cannot be replaced
here: replace it when it drops.

**`cleria.game.spawn_pickup(item, x, y, z [, {value = n}])`** [H]: one collectable pickup thrown up from that point with
an enemy drop's arc and lifetime (FUN_00587010's default arguments). `item` as for `content.give`; `value` its own value
(above). `false` with no game. Use `stats()` for the player's position, `enemy_killed` for a monster's.

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

### 6.1 Save profiles: `cleria.save.set_profile` (2026-10-09; test `cleria.save.set_profile ~= nil`)

CleriaCore's, not retail's. A mod keeps a run's saves apart from the player's own (the Archipelago mod: one
folder per seed and slot).

| call | |
|---|---|
| `cleria.save.set_profile(name)` -> boolean | the save folder becomes `<save dir>/profiles/<folder>/`; `""` or `nil` returns to `<save dir>` itself. `true` = the profile is now that one (also when it already was), `false` = refused, nothing changed |
| `cleria.save.profile()` -> string | the active profile's folder name, `""` = the normal saves |

- `<save dir>` is `CLERIA_SAVE_DIR`, else `./saves` (the folder of `yso_NN.bin`). Everything that reads or
  writes a slot follows the profile: the statue / book save, `game.save`, the Load lists of the title and of
  the book (they list the profile's saves only), the book's delete, the sidecars `yso_NN.cleria`, and the
  launcher's "auto" backdrop when a profile is already set as it opens. The continue snapshot (Retry) is in
  memory, not a file. `cleria.ini`, `cleria.settings` and `cleria.data` do not move.
- The folder name [CODE `save_profile_name`, `src/cleria/sidecar.cpp`]: the first 64 bytes of `name`, every
  byte outside `A-Z a-z 0-9 . _ -` replaced by `_` (so a `/`, a `\`, a `:`, a space and each byte of a
  non-ASCII character become `_`), a trailing `.` replaced by `_`. `"AP_" .. seed` stays as it is;
  `"../x"` becomes `.._x`. Two names that differ only in replaced characters share a folder.
- Refused (`false`, no change): a name that is only dots (`"."`, `".."`), and any call while a game is bound —
  from the moment a New Game / Load leaves the title until Return to Title. A profile changes only on the
  launcher and the title ("frame" handlers run there), so a running game's saves never change folder.
  A non-string, non-nil `name` raises a Lua error.
- The folder is created by the first save, not by `set_profile`. The profile lasts for the process (it stays
  through Return to Title) and is not stored: set it at each start. The log gets
  `[save] profile "<folder>" -> <full path>` (or `[save] profile cleared`) and the player a notice card.
- Without a mod: the environment variable `CLERIA_SAVE_PROFILE=<name>` (same rule) sets the profile at start.
- [H] A Load page that is already open keeps the cards it scanned; set the profile before the player opens it.

```lua
-- on the title, once the server has answered (seed and slot are known)
cleria.events.on("frame", function()
  if not want_profile or cleria.save.set_profile == nil or cleria.game.in_game() then return end
  if cleria.save.set_profile("AP_" .. seed .. "_" .. slot) then want_profile = nil end
end)
```

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
- Also: `cleria.api_version` (7: §11), `cleria.mod` (`id`, `name`, `version`), `cleria.time()` (seconds, monotonic),
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

**Mod 0.2.0** (2026-10-09; the mod's repository, branch `apworld-2.0`, synced here): the apworld 2.0.1 table
(`tools/ap_import.py --check` passes against the apworld at db8273f; the fixture carries `roo_flags`) and the
retail mod's 2.0 rules. Its eight cases joined `tools/aplua_regress.py`: `aplua_roda`, `aplua_random_start`,
`aplua_weapon_clamp`, `aplua_boss_on_kill`, `aplua_boss_on_entry`, `aplua_page`, `aplua_sp_chest`,
`aplua_invariants` (a case's `aplua=dict(slot_data=..., settings=...)` lays keys over the fixture and adds
lines to the mod's settings section), beside `aplua_retry`, and `aplua_hints` (the mock server keeps the slot's
hint list under `_read_hints_0_2`: `Get`, `SetNotify` -> `SetReply`, the chat command `!hint <location>`).

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

## 12. API 7

The work list and its state are in `API7.md`. What is done is described here, by family. Test `cleria.api_version >= 7`.

### 12.1 Timers

```lua
local id = cleria.after(2.5, function() cleria.notice("2.5 seconds later") end)
local tick = cleria.every(1, function() ... end, {play = true})      -- once a second of PLAY time
cleria.cancel(tick)                                                    -- true when it was still pending
```

- `cleria.after(seconds, fn [, {play = true}])` runs `fn` once; `cleria.every` runs it again every `seconds` (at least one
  tick, 1 / 60). Both return an id for `cleria.cancel(id)`.
- **Real time** by default (`cleria.time()`'s clock): the timer runs with the frames, also on the title and while the F1
  panel is open. With **`play = true`** it counts play time: one sixtieth per game tick while a game runs and no cutscene
  or box holds the world (`game.in_game() and not game.frozen()`), the clock the mods' own tick counters used.
- A timer runs at most once per frame (or tick). A repeating timer that fell behind (a long load) runs once and waits a
  full period: no burst. A timer may cancel itself or add timers; those start at the next frame.
- At most 4096 timers per mod. Timers are not saved.

### 12.2 `cleria.rng(seed)`: a generator of the mod's own

```lua
local r = cleria.rng("daily-2026-10-13")      -- a string or an integer
r:int(6)            -- 1..6          r:int(10, 20)   -- 10..20
r:float()           -- [0, 1)        r:pick(list)    -- one element (nil for an empty list)
r:shuffle(list)     -- in place, returns the list
```

The same seed gives the same sequence on every machine and every CleriaCore (xoshiro256** seeded through splitmix64;
a string seed is hashed with FNV-1a 64). It never touches the game's generator or `math.random`.

### 12.3 The mods see each other: `cleria.mods`, `cleria.bus`

```lua
for _, m in ipairs(cleria.mods.list()) do ... end           -- {id, name, version, active, scripted}, load order
if cleria.mods.is_active("4rtefakt.solorando") then ... end

-- mod A
cleria.bus.subscribe("rando.layout", function(value, from) ... end)
-- mod B
local heard = cleria.bus.publish("rando.layout", {seed = "abc", chests = 42})      -- how many handlers ran

cleria.bus.set("rando.seed", "abc")                         -- a value every mod can read
cleria.bus.get("rando.seed", "none")                        -- its value, or the default
```

- A message is any JSON-able value (it crosses between two Lua states as JSON, at most 1 MB). `publish` calls the
  handlers of the **other** mods at once, in load order, and returns how many ran; the sender's own handlers are not
  called. A handler that fails stops its own mod, not the sender.
- `bus.set` / `bus.get` is a table shared by every mod for the life of the process (not saved; `set(key)` or
  `set(key, nil)` removes). Topics and keys are 1-64 characters of `A-Z a-z 0-9 _ . - :`; start them with your mod's
  name.
- A mod that needs another declares it with `Requires=` in `mod.ini` (MODS.md); the bus is for the ones that only
  cooperate when both are there.

### 12.4 Events with a name: `retry`, `level_up`

API 7's events arrive like the others (`cleria.events.on(name, fn)`; `e.kind`, `e.room`) with the fields listed here.

| event | when | fields |
|---|---|---|
| `retry` | the player chose Retry on the game-over screen, before anything is restored | `snapshot`: 1 = the continue snapshot comes back (a `load` follows, with `retry = true`), 0 = there was none and the player is revived in place (no `load`) |
| `level_up` | a level was gained (once per level), after max HP and the full heal | `level`, `max_hp` |

**`load`** now carries `retry` (a boolean): true when it is the continue snapshot coming back after a Retry rather
than a slot. A Retry from the snapshot brings the mods' save storage (`cleria.save`) back **as it was when the
snapshot was taken**: what a mod stored since then is gone, exactly like the flags. A mod that must keep something
across it (a death counter) holds it in a Lua variable at `death` and writes it back at that `load`.

### 12.5 Small things

- `cleria.player.set_modifier(stat, add, 0)`: a multiplier of 0 is now a multiplier of 0 (max HP then stays at the
  game's floor of 1). Before API 7 `(0, 0)` was read as "no modifier".
- `cleria.clipboard(text)` puts text (at most 64 KB) on the clipboard; true when it worked.
- `cleria.open_url("https://...")` opens the player's browser. It is an error anywhere but inside a page's function
  (a button of the mod's own page was pressed: the player asked for it); http and https only. In a headless run it
  opens nothing.
- `cleria.locale()`: the game's text language, `"ja"`, `"en"`, `"es"`, `"fr"`, `"de"` or `"it"`.

### 12.6 What the game is doing: `cleria.game.state` and friends

| call | |
|---|---|
| `game.state()` | `"title"` (no game runs), `"play"`, `"cutscene"` (a script holds the world), `"dialog"` (a text window), `"menu"` (a script's menu: a statue's blessings, a choice), `"book"` (the pause book), `"panel"` (the F1 panel), `"gameover"`. The most covering one wins: the panel over the book over a window over a cutscene |
| `game.room_title()` | the room's own name as the game's data has it (`"4F Forward Passage 1"`, `"Outer Corridor 1"`); `""` for a room without one |
| `game.floor()` | the floor that name starts with, 1..25, or `nil` when it names none (the outer corridors, Rado's Annex, the summit) |
| `game.zone()` | `1..7` and the zone's name (`1, "Wailing Blue"` ... `7, "Tower Summit"`), from the room number; `nil` elsewhere |
| `game.difficulty()` | `0..4` and its name: Very Easy, Easy, Normal, Hard, Nightmare |
| `game.play_time()` | seconds of play, the game's own counter (the one on the save card) |

Two events follow them, raised from the tick that sees the change:

| event | fields |
|---|---|
| `state_changed` | `state`, `old` |
| `floor_changed` | `floor`, `old` (0 when the game started in a room without a floor). A room whose name has no floor keeps the last one: walking through an outer corridor raises nothing |

### 12.7 The room's monsters: `cleria.world`

```lua
for _, a in ipairs(cleria.world.actors()) do
    -- a.id, a.cast, a.name, a.level, a.hp, a.max_hp, a.atk, a.def, a.x, a.y, a.z, a.height, a.yaw, a.hittable
end
local one = cleria.world.actor(id)                      -- the same table for one id, or nil

cleria.hud.layer("bars", function(d)                    -- a name over every monster
    for _, a in ipairs(cleria.world.actors()) do
        local fx, fy = cleria.world.to_screen(a.x, a.y + a.height, a.z)
        if fx then d:text(fx * d.w, fy * d.h, a.name, {size = 16 * d.scale}) end
    end
end)
```

- `world.actors()` lists the living enemy actors of the room: never the player, an NPC, a puppet, or the parts of a
  boss module (those are not actors). `id` is the actor's instance id, stable while it lives; `x, y, z` its feet and
  `height` its collision height (the head is at `y + height`); `hittable` is false while it is faded out or immune.
- `world.to_screen(x, y, z)` returns where a world point was on the **last drawn frame**, as fractions of the picture
  (0..1 from its top left: multiply by a layer's `d.w`, `d.h`), or `nil` behind the camera. A point outside 0..1 is
  off the screen.
- The `"damage"` filter's query gains `actor` (the monster hit, for `dir = "dealt"`), and `hp` / `max_hp`: the
  victim's HP before the hit (the monster's, or the player's for `dir = "taken"`). `q.value >= q.hp` is a kill.

| event | when | fields |
|---|---|---|
| `enemy_hit` | a hit of the player's landed on a monster, after the HP changed | `actor`, `cast`, `damage`, `hp`, `max_hp`, `killed` (1 / 0) |
| `player_hit` | a hit landed on the player, after the HP changed | `damage`, `hp`, `max_hp`; `cast`, `level` when a monster's hit volume did it |

### 12.8 Changing the player and the flow

Each of these does what a script of the game (or a drive command) already does to that state. They return `true`
when it was done and `false` when the game refused (no game, the player is dead, a cutscene holds the world, an
item that is not owned). A value out of its range is a Lua error.

| call | |
|---|---|
| `player.set_position(x, y, z [, degrees])` | the player's feet, put on the floor under `(x, y + 1, z)` (or at `y` when there is none), falling state cleared; `degrees` turns them (0 = +Z) |
| `player.set_mp(n)`, `player.set_boost(n)` | the skill gauge (0..`stats().max_mp`) and the Boost gauge (0..30000) |
| `player.set_exp(n)` | the EXP total; the levels it reaches are gained (full heal, the popup, `level_up`). A level is never taken back: use `set_level` for that |
| `player.set_invulnerable(ticks)` | at least that many ticks during which hits do not land (the same counter a hit grants) |
| `player.equipment()` | `{weapon =, armor =, leggings =, accessory =, magic =, held =}`: item ids, -1 = none |
| `player.equip(slot, item)` | equips an **owned** item of the slot's own kind (weapon 0..5, armor 6..29, leggings 30..53, accessory 54..65, magic 116..119, held 66..115); `nil` unequips an accessory, the magic or the held tool. Max HP and the stats follow |
| `player.status()` | `{poison =, heavy =, confuse =, curse =}`: ticks left, 0 = not on |
| `player.set_status(name, on)` | `true`: inflicted as a monster's hit does (its timer, effect, sound, popup; the blessings shorten it). `false`: it ends at the next tick |
| `player.abilities()`, `player.set_ability(name, on)` | the abilities the game keeps as flags: `double_jump` (flag 163) and `boost` (flag 187) |
| `game.exits()` | the room's exits: `{{index = 0, room = "S_1003"}, ...}` (index for `take_exit`) |
| `game.take_exit(i)` | walks through exit `i` of this room: the arrival point and facing of that door |
| `game.goto_room("S_1003")` | changes room as a script's ChangeScene does (the room's own arrival point); false for a room that does not exist |
| `game.set_speed(x)` | this mod's game-speed factor, 0.1..4 (1 removes it). The mods' factors and the shell's own Game speed multiply, the product kept within 0.1..4. Not saved |
| `game.retry()` | the game-over screen's Retry; false while the player is alive |
| `game.to_title()` | Return to Title |
| `game.load(slot)` | loads slot 0..63 as the book's Load does; false when the slot has no save |
| `game.seed_random(n)` | seeds the game's own generator (drops, AI rolls, damage rounding): the same seed and the same inputs give the same run |

[H] Nothing checks that a position is reachable or that a room change makes sense for the story. `take_exit` and
`goto_room` are refused during a cutscene and while dead.

### 12.9 Keys and pad buttons: `cleria.input`

```lua
cleria.input.action("menu", {label = "Open my menu", key = "F5", pad = "back"}, function()
    if cleria.game.state() == "play" then open_menu() end
end)

cleria.events.on("tick", function()
    if cleria.input.down("menu") then ... end            -- held right now
end)
```

- `input.action(id, {label =, key =, pad =} [, fn])` declares an action of the mod: `fn` runs once each time its key or
  its pad button goes down. `key` and `pad` are the defaults; **the player's own choice wins**. Declaring an id again
  replaces it. At most 64 per mod; an id is 1-32 characters of `A-Z a-z 0-9 _`.
- Names are SDL's: keys `"F5"`, `"Tab"`, `"Left Shift"`, `"A"`, `"1"`, `"Keypad 0"`; pad buttons `"a"`, `"b"`, `"x"`,
  `"y"`, `"back"`, `"start"`, `"leftshoulder"`, `"rightshoulder"`, `"leftstick"`, `"dpup"` ... An unknown name is a Lua
  error.
- **The player's keys**: every mod with actions gets a **Keys** section at the end of its page (F1 > Mods > the mod), a
  text field per action. The choice is the mod's setting `Key.<id>` (and `Pad.<id>`); empty = no key.
  `input.bind(id, key [, pad])` sets it from a script (false for an unknown name), `input.binding(id)` returns the
  key and the button in use.
- `input.down(id)` is true while the action's key or button is held; `input.key(name)` / `input.pad(name)` read any
  key or button.
- Actions run on the frame of the press, in every state of the game but one: while the F1 panel is open it has the
  keyboard, and no action runs. A mod decides for itself whether the moment is right (`game.state()`).
- [H] Nothing stops a mod from choosing a key the game uses: the game still gets it. Prefer the function keys the
  game leaves free (F2, F4 to F12; F1 is the panel, F3 the debug text).

| event | when | fields |
|---|---|---|
| `input` | a key or a pad button went down (not a repeat), the F1 panel closed | `key` or `pad`: its name |

A drive run presses for the mods with `modkey=<name>` and `modpad=<name>`.

### 12.10 Windows of the mod's own, and asking the player

```lua
cleria.ui.window("tools", {title = "My tools", width = 0.4}, function(p)      -- the page widgets, over the game
    p:text("A paragraph of text. It wraps at the panel's edge.")
    p:progress("Health", cleria.player.hp() / cleria.player.max_hp(), "half")
    if p:button("Heal") then cleria.player.set_hp(cleria.player.max_hp()) end
    if p:button("Close") then p:close() end
end)
cleria.input.action("tools", {label = "Open my tools", key = "F2"}, function()
    if cleria.ui.is_open("tools") then cleria.ui.close() else cleria.ui.open("tools") end
end)

cleria.ui.choose("Pick a reward.", {"Might", "Guard", "Vigor"}, function(index, option)
    if index then take(index) end                                               -- nil: the player backed out
end, {title = "Perk", cancel = true})
cleria.ui.message("The run is over.", function() ... end, {title = "Permadeath", ok = "Fine"})
```

- `ui.window(id, {title =, width =, pause =}, fn(p))` declares a window (again: replaces it); `ui.open(id)` shows it,
  `ui.close()` hides whatever is open, `ui.is_open()` returns the open window's id (`"dialog"` for a dialog, `nil` for
  none) and `ui.is_open(id)` a boolean. `fn(p)` is called every frame while it is open, with the page widgets of §7,
  the two new ones below and `p:close()`. `width` is a fraction of the screen (0.2..0.9, default 0.42); the window is as
  tall as its rows, within the screen.
- **One window at a time per mod.** While one is open the mod has the keyboard, the pad and the mouse exactly as the F1
  panel has (up / down, OK, left / right on a slider, Back or Esc to close it); the game gets no input.
  `pause = true` (the default) also stops the game's clock: `game.state()` is `"window"`, no tick runs, the mods' `tick`
  events and play-time timers wait, `frame` goes on. With `pause = false` the game runs on underneath. F1 still opens
  the panel over it.
- The mod's own actions (§12.9) still run while its window is open, so the key that opened it can close it.
- `ui.choose(text, options, fn [, {title =, cancel =}])` asks: 1 to 12 options, `fn(index, option)` when one is picked,
  `fn(nil)` when the player backs out; `cancel = false` makes Back do nothing. `ui.message(text [, fn [, {title =, ok =}]])`
  is the one-button form. Both are a paused window; opening one replaces the mod's open window, and the function may
  open the next one.
- New widgets, on pages and in windows: `p:text(str [, {color =}])`, a wrapped paragraph; `p:progress(label, fraction
  [, text [, {color =}]])`, a bar from 0 to 1 with its text.
- A drive run presses for the open window with `modwin=ok` / `back` / `up` / `down` / `left` / `right`, and its `shot`
  shows the window.

### 12.11 The world's filters: monsters as they are built, doors as they are taken

```lua
cleria.world.add_filter("spawn", function(q)        -- q.cast, q.name, q.level, q.id, q.x, q.y, q.z, q.group, q.room
    if q.cast == 17 then return {cast = 16, level = q.level + 2, hp = 2, exp = 1.5} end      -- another monster, tougher
    if q.cast == 43 then return "suppress" end                                               -- not spawned at all
end)

cleria.world.add_filter("exit", function(q)         -- q.from, q.to, q.index, q.door, q.room
    if q.to == "S_1003" then return {to = "S_1009"} end                                      -- this door leads elsewhere
end)
```

`world.add_filter(kind, fn)`: `fn(q)` answers `nil` / `"pass"`, `"suppress"`, or a table (the decision; `action` may be
given, `"replace"` is the default for a table). The mods are asked in load order, a mod's filters in registration
order; the first answer that is not "pass" wins.

**`"spawn"`**: every **enemy** about to be built, from the room's placement list or by a script (the wards' monsters,
an arena's waves), before its stats are computed. Never the player, an NPC, or a boss module.
- `cast` and `name` (CASTINFO's), `level` (its own, or the script's), `id` (0 = it gets a fresh one), the point, `group`
  (the ward group 1..4, 0 = none).
- The answer's fields: `cast` (another enemy cast: anything else keeps the original), `level` (1..60), and the
  multipliers `hp`, `atk`, `def`, `exp`, `sp` (0.01..100, default 1) applied to what the game computes for that cast
  and level. Its sprites, its brain and its drops are the new cast's.
- `"suppress"`: it is not built. [H] A room's scripts may wait for a monster that never comes (a ward opens only when
  its group is dead: a suppressed group counts as dead); test what you remove.

**`"exit"`**: every room change about to happen: a door (`index` = the exit's index in `game.exits()`, `door` its
label), a script's ChangeScene, `game.goto_room` (`index` -1). The answer's `to` names another room (one that exists,
else the original stands); the player then arrives at that room's own arrival point, not at a door's. `"suppress"`
cancels the change.

Spawning by hand:

| call | |
|---|---|
| `world.spawn(cast, x, y, z [, {level =, yaw =, group =}])` | builds an enemy as a script's spawn does and returns its id; `nil` for a cast that is not an enemy. It passes the `"spawn"` filter like any other |
| `world.remove(id)` | the monster is gone at once: no EXP, no drops, no `enemy_killed` |
| `world.set_actor(id, {hp =, x =, y =, z =, yaw =})` | a living monster's HP (1..its max: this is not a way to kill), position, heading in degrees |

### 12.12 Sound and the screen

| call | |
|---|---|
| `cleria.audio.se(id [, volume])` | plays one of the game's sounds (an EFFECTDEF id, as a script's Se_Play), `volume` 0..1 |
| `cleria.audio.music(id)` | switches to one of the game's BGMs (a bgmdef id, as a script's Bgm_Play) |
| `cleria.audio.stop_music([seconds])` | fades the music out (1 second by default) |
| `cleria.screen.fade(color, from, to [, seconds])` | the game's own fader: the screen covered by `color` (0xRRGGBB) from opacity `from` to `to` (0..1), 0.33 s by default. It stays at `to`: fade back yourself |
| `cleria.screen.flash(color [, seconds [, strength]])` | covered at `strength` (0.6), clear again after `seconds` (0.25) |

[H] The fader is the one the game's scripts use: a room change or a cutscene sets it again, and a mod's fade set
during a scene's own fade replaces it. A mod's own sound files are not played yet (API7.md #20).

### 12.13 A second character: puppets

```lua
local id = cleria.world.puppet(2, x, y, z, 180)         -- Hugo, standing there, facing 180 degrees
cleria.world.set_puppet(id, {x = x2, y = y2, z = z2, yaw = 90, sheet = 16, tag = 301, frame = 3})
cleria.world.remove_puppet(id)

local me = cleria.player.pose()                         -- {x, y, z, yaw, sheet, tag, frame}: send this to the other side
```

- `world.puppet(character, x, y, z [, degrees])` puts one of the three characters (1 Yunica, 2 Hugo, 3 Toal) in the room
  as an actor with **no brain, no stats and no collision**: the mod owns its place, heading and picture. It returns
  the actor's id (`nil` with no game). The puppet is drawn from the character's own gameplay sheet, standing with
  its weapon, shadow included (checked on captures: Hugo next to Yunica).
- `world.set_puppet(id, {...})`: any of `x`, `y`, `z` (its feet), `yaw` (degrees, 0 = +Z, as `player.set_position`),
  and the picture: `sheet`, `tag` and `frame`. The sheet is a sprite file by its id (15 Yunica, 16 Hugo, 17 Toal for
  moving about; the character's script switches to others for its attacks and skills, 11 for Yunica's swings), the
  tag one of that sheet's animations (**305 standing, 301 running, 302 jumping**, 257 / 258 Yunica's first swings),
  the frame clamped to the animation. A sheet id the game does not have is ignored. False once the puppet is gone.
- Puppets live in the room: a room change removes them (spawn them again on `room_enter`). They are not saved.
- `cleria.player.pose()` is the local player's own `x, y, z`, `yaw` and the `sheet` / `tag` / `frame` shown this
  tick: a puppet fed with it every tick copies the player exactly (checked on captures standing, running, jumping
  and through two swings). It does not carry what is drawn around the sprite: skill effects, Toal's trail.

[H] This is the first of the multiplayer pieces (API7.md H). Not there yet: a hit shape on a puppet (#50), and any
sharing of the monsters (#51): with only this, two players see each other and each fights their own monsters.

### 12.14 What the player does, and the game's scripts

Plain events (`cleria.events.on`), raised where the game does the thing:

| event | fields | |
|---|---|---|
| `attack` | `section`, `air` (0 / 1) | a new swing starts (each step of a combo is one) |
| `skill_used` | `slot` (1..3), `item` (the bracelet), `level` (2 = charged), `cost` (MP), `section` | a skill is cast, before its MP is spent |
| `jump` | `double`, `water` (0 / 1) | a ground jump, a double jump, a jump in water |
| `land` | | the player touches the ground after a fall or a jump |
| `boost` | `on` (1 / 0) | the Boost starts, ends |
| `interact` | `object`, `script` | the action button started an object's script (a chest, a door, a statue) |
| `statue_used` | `object`, `cleansed` (0 / 1) | that object is a goddess statue |
| `script_started` | `name`, `owner` | one of the game's scripts starts by name (not a room's own scripts at its load) |

```lua
cleria.script.run("@Save")                              -- start one of the game's scripts; true when it started
cleria.script.add_filter(function(q)                    -- q = {name, room, owner}
    if q.name == "ev_boss_intro" then return "suppress" end
end)
```

- `cleria.script.run(name)` starts a script as the action button would, the player its owner; `@name` is a script of
  S_COMMON. False when there is no such script or it is already running.
- `cleria.script.add_filter(fn)`: `"suppress"` refuses the start, as if the script did not exist (the same filter as
  `world.add_filter` with the kind `"script"`). [H] A room's logic lives in these scripts: refusing one that sets a
  flag or opens a door leaves the room as it was. Refuse only what you know.

### 12.15 Hits of the mod's own, and the player's tuning

```lua
cleria.combat.damage(actor, 25)                         -- 25 HP off that monster, as a hit of the player's
cleria.combat.damage(actor, 25, {knock = 1.5, class = 1})
cleria.combat.hurt(10)                                  -- 10 HP off the player, as it is
cleria.combat.heal(10)

cleria.player.tune({speed = 1.5, jump = 1.2, air_jumps = 1, mp_cost = 0.5, mp_regen = 2, boost_gain = 2})
```

- `combat.damage(actor, n [, {knock =, class =}])` queues a hit on a living monster (an id of `world.actors()`), landed
  at the next hit scan (the same tick or the next) through the game's own hit function: the damage is `n` **as given**
  (no formula, no guard, past the `"damage"` filters), then everything a hit of the player's does: the white number,
  the flinch, the knock (`knock`, 0 by default), `enemy_hit`, and on a kill the EXP, the SP, `enemy_killed` and the
  drops. `class` is the damage class 0..3 (3, none, by default). False for an id that is not a living monster.
  [H] A boss module's parts are not actors (§12.6): this does not reach them.
- `combat.hurt(n)` / `combat.heal(n)` take or give HP directly: no formula, no i-frames, no `"damage"` filter, no
  `player_hit`. HP stops at the maximum; 0 is the death.
- `player.tune{...}` sets **this mod's** factors; a key left out keeps its value, `1` (0 for `air_jumps`) clears it.
  Several mods multiply (their extra air jumps add up).

| key | what it scales | limits |
|---|---|---|
| `speed` | the walk speed (run, dash and knock scale with it, as they are multiples of it) | product 0.1..5 |
| `jump` | the lift-off speed of a jump (the height goes with its square) | product 0.1..4 |
| `air_jumps` | jumps in the air **added** to the game's (0 without the double jump, 1 with it) | 0..8 a mod |
| `mp_cost` | a skill's MP cost (0.5 = half; the cast is allowed on the scaled cost) | 0..10 |
| `mp_regen` | the MP refill of each tick | 0..10 |
| `boost_gain` | everything added to the Boost gauge | 0..10 |

Checked in a drive run: 30 ticks of walking cover 2.5 then 5.0 with `speed = 2`; a second jump in the air without
the game's double jump; a skill at 400 MP costs 200 with `mp_cost = 0.5`. The tuning is the process's, like
`set_modifier`: it is not saved, set it again at start. Not there: gravity (a constant inside the integrator).

### 12.16 Sockets and HTTP

```lua
local u = cleria.net.udp()
local port = u:bind(7777)                               -- or u:bind() for any free port
u:send("192.168.1.20", 7777, "hello")
for _, d in ipairs(u:poll()) do print(d.host, d.port, d.data) end

local srv = cleria.net.tcp()                            -- a host
srv:listen(7777)
local cli = cleria.net.tcp()                            -- a guest
cli:connect("192.168.1.20", 7777)
for _, e in ipairs(cli:poll()) do
    if e.kind == "open" then cli:send("hi") elseif e.kind == "data" then print(e.text) end
end

cleria.net.http({url = "https://example.org/api", method = "POST", body = cleria.json.encode(t),
                 headers = {["Content-Type"] = "application/json"}}, function(r)
    if r.ok and r.status == 200 then ... r.body ... else ... r.error ... end
end)
```

None of these calls waits on the network: poll the sockets from a `frame` or `tick` handler.

| call | |
|---|---|
| `cleria.net.udp()` | a UDP socket |
| `u:bind([port [, any]])` | opens it on a local port (0 or nothing = a free one); `any` false = this machine only. Returns the port, or `false, why` |
| `u:send(host, port, data)` | one datagram (at most 65507 bytes, binary safe). Not bound yet: bound to a free port first. A host **name** is looked up once, and that first call waits for the lookup: use addresses in a game loop. `true`, or `false, why` |
| `u:poll()` | the datagrams that arrived: `{host, port, data}` each (at most 256 a call) |
| `u:port()`, `u:close()` | the local port (0 when closed) |
| `cleria.net.tcp()` | a TCP socket: a client, or a listener |
| `t:connect(host, port [, seconds])` | starts connecting (10 s by default); `open` or `close` follows in `poll()` |
| `t:listen([port [, any]])` | accepts connections on a local port. Returns the port, or `false, why` |
| `t:send(data)` | sends bytes (binary safe); what the socket does not take at once goes out at the next `poll()`. False when not open, or when 16 MiB already wait |
| `t:poll()` | events `{kind, text, error, socket}`: `open` (text = the peer), `data` (text = the bytes, in the order sent but cut anywhere: frame your messages), `close` (text = why; `error` when it was not a plain close), and for a listener `accept` (`socket` = the new connection, already open; text = the peer) |
| `t:state()` | `idle`, `connecting`, `open`, `listening`, `closed` |
| `t:port()`, `t:close()` | the local port; `close` tries once to send what is still queued |
| `cleria.net.http(opts, fn)` | one request on a worker thread; `fn(res)` runs on a later frame |

- `http` options: `url` (`http://` or `https://`, required), `method` (GET, POST, PUT, PATCH, DELETE, HEAD), `body`,
  `headers` (a table), `timeout` (seconds, 15). The answer: `ok` (the exchange completed, whatever the status),
  `status`, `body` (cut at 8 MiB: `truncated`), `error`. Redirections are followed. At most 8 requests at once a mod.
- A mod may keep 16 UDP / TCP sockets (the WebSockets count apart). A socket left to the garbage collector is closed.
- TCP is a stream: two `send`s may arrive as one `data` event or one as two. Prefix each message with its length, or
  end it with a newline. UDP keeps the datagram whole but may lose it, repeat it or deliver it out of order.
- IPv4 only [H]. `bind` / `listen` on every address make Windows ask the player to allow the game through the firewall,
  once. HTTP uses the system's own client (WinHTTP; libcurl on Linux / macOS when built with CLERIA_CURL, else `ok`
  is false with the reason) [the libcurl path is not built on Windows: unverified here].

Checked in `cleria-luatest` on the loopback: a datagram, a listener and its client, and an HTTP POST answered by the
mod's own TCP listener.

### 12.17 The camera, the game's HUD, holding the game, the boss gauge

```lua
local c = cleria.camera.get()                           -- {x, y, z, tx, ty, tz, fov, distance, fov_scale}
cleria.camera.set({distance = 1.5})                     -- the eye half as far again from its subject
cleria.camera.set({fov = 0.8, y = 1})                   -- a narrower view, looking 1 higher
cleria.camera.shake(0.3, 0.5)                           -- strength, seconds
cleria.camera.look_at(x, y, z, 1)                       -- the subject glides there in 1 s; look_at() comes back
cleria.camera.reset()

cleria.hud.show("status", false)                        -- "status", "boss", "messages", "all"
cleria.game.pause(true)                                 -- the game waits; pause(false) lets it go
local b = cleria.world.boss()                           -- {hp, max_hp, actor} while a boss gauge is up, else nil
```

- `camera.get()` is the camera of the last step: the eye, its subject (`tx, ty, tz`), the field of view in degrees,
  and the mods' own `distance` and `fov_scale`. `nil` with no game.
- `camera.set{...}` is **one layer over the game's camera**, shared by the mods (the last call wins, a key left out
  keeps its value): `distance` scales how far the eye stands from its subject (0.2..5), `fov` scales the field of
  view (0.3..2.5), `x, y, z` move eye and subject together. The game's own camera (regions, cutscenes, boss cameras)
  keeps running under it. `camera.reset()` clears the layer. Checked in a drive run: `distance = 2` doubles the
  measured eye distance, `reset` brings it back.
- `camera.shake(strength [, seconds])` (or `{x =, y =, z =}` amplitudes) is a script's own Camera_Shake;
  `camera.look_at(x, y, z [, seconds])` its Scroll_MoveTo, `look_at()` its Scroll_Return. A cutscene uses the same
  two: it overrides a mod's and is overridden by it.
- `hud.show(part, on)` hides or shows a part of the **game's** HUD for this mod (a part is hidden while any mod hides
  it): `"status"` the gauges, the portrait and the equipment icons; `"boss"` the boss gauge; `"messages"` the lines
  on the left; `"all"`. The mods' own layers and the dialogue windows are not touched.
- `game.pause(on)`: the game stops ticking while any mod holds it, exactly as under a mod's window (§12.10):
  `game.state()` is `"held"`, `tick` handlers stop, `frame` handlers and timers of real time go on. Let it go
  yourself: nothing else will.
- `world.boss()` reads the boss gauge on screen: `hp`, `max_hp`, and `actor` when the boss is an actor of
  `world.actors()` (the bosses run by their own modules have none). [H] No phase or part list: each boss module
  keeps its own, there is no common shape to give.

One more event: `enemy_spawned` (`actor`, `cast`, `level`, `x`, `y`, `z`), raised when an enemy actor comes to exist:
the room's placements at its load, a script's spawn, a monster's call for help, `world.spawn`.

### 12.18 Monsters of the mod's own, and bosses

```lua
cleria.content.define_monster("brute", {
    base = 43,                                          -- the enemy cast it is built from (sprites, brain, drops)
    name = "Brute", level = 20,
    hp = 3, atk = 1.5, def = 1, exp = 5, sp = 2,        -- multipliers of what the game computes for that cast and level
    scale = 1.6, tint = 0xFF5050,
    on_spawn = function(actor) end,
    on_hit = function(e) end,                           -- the enemy_hit event of that monster
    on_death = function(e) end,                         -- its enemy_killed event
})
cleria.content.define_boss("king", {
    base = 43, name = "The King", hp = 20, scale = 2.2,
    phases = {{at = 0.66, enter = function(actor, i) ... end}, {at = 0.33, enter = function(actor, i) ... end}},
    on_death = function(e) ... end,
})

local id = cleria.world.spawn("brute", x, y, z)         -- a variant by its name, where a cast id goes
cleria.content.morph(actor, "brute")                    -- or a monster that is already there becomes one
cleria.world.form(actor, {atk = 2, tint = 0xFF2020})    -- the same changes on any living monster
```

A variant is **a monster of the game with other numbers and another look**: its sprites, its brain, its attacks and
its drops are its base cast's. What a mod adds is the name, the stats, the size, the colour, and Lua called at its
birth, on each hit, at each phase and at its death. Everything else is the API you already have: `world.spawn` for
its helpers, `combat.hurt` and `screen.flash` for an attack of your own, `world.set_actor` to move it, `audio.music`
for its theme, `camera.shake`.

- `define_monster(id, def)`: `base` is required. `level` 0 = the cast's own. The multipliers are 0.01..1000 (1 by
  default); `hp` also refills it. `scale` 0.2..5 scales the sprite (not its collision or its reach); `tint` is
  0xRRGGBB. `ai = false` holds its handler: it stands where it is, can be hit, and does nothing until the mod moves
  it. `boss = true` puts the boss gauge on its HP (`define_boss` is `define_monster` with that by default). Defining
  an id again replaces it for the monsters built afterwards. At most 256 a mod.
- `phases`: up to 16 `{at = fraction, enter = fn(actor, index)}`; a phase is entered once, when a hit of the player's
  leaves the monster alive at or under that fraction of its HP. A hit that crosses several enters each, highest first.
- `world.spawn(id, x, y, z [, opts])` builds the base cast (through the `"spawn"` filter, like any spawn), applies
  the variant and calls `on_spawn`. `content.morph(actor, id)` applies it to a living monster (its cast stays what
  it was): from an `enemy_spawned` handler it turns the room's own monsters into variants.
- `world.form(actor, {...})` is the same set of changes without a definition: `hp`, `atk`, `def`, `exp`, `sp`
  (multipliers of the **current** values), `scale`, `tint`, `ai`, `boss`, `name`. Use it inside a phase.
- `world.actors()` entries gain `variant` (`"<mod id>:<variant id>"`, absent for a plain monster), `boss` and `ai`;
  `name` is the variant's. `enemy_hit` and `enemy_killed` gain `variant` (`""` for a plain monster) and
  `enemy_killed` gains `actor`.
- The callbacks follow the monster, not the room: a room change, a load or a new game forgets the living ones.
  `on_death` runs for a death by a hit of the player's (`enemy_killed`), not for `world.remove`.

[H] A variant's hit on the player is its base cast's hit volume with the variant's ATK. A boss here is not one of the
game's boss modules (those are C++ handlers of their own): no intro, no arena script. Build those from `script.run`,
`camera.look_at`, `ui.message` and the `"exit"` filter. Checked in a drive run (`lua_api7_monsters`) and on a capture.

### 12.19 Two machines: hits on a puppet, hits from afar, monsters that match

The pieces a multiplayer mod is built from, with §12.13 (puppets, `player.pose`) and §12.16 (sockets). The engine
simulates **one** player: these let a mod show the other one, exchange hits with them and keep the monsters alike.

```lua
cleria.world.set_puppet(id, {hittable = true})          -- my hits can land on the other player's puppet
cleria.events.on("puppet_hit", function(e)              -- {puppet, atk, power, knock, class, heading, section}
    send({t = "hit", atk = e.atk + e.power})            -- tell their machine
end)
cleria.combat.strike(atk)                               -- their machine: a hit on its own player, priced by the game

local snap = cleria.world.snapshot()                    -- the host: the room's monsters, as plain tables
cleria.world.apply(snap)                                -- a guest: the room made to match
```

- A **hittable** puppet has a body the player's swings, sword trails, shots and skill effects find (once a swing, as
  for a monster). It has no HP: the hit raises `puppet_hit` and nothing else. `atk` is the attacker's ATK, `power`
  the attack's own bonus, `class` its damage class, `heading` its direction in degrees.
- `set_puppet(id, {target = true})`: the room's monsters treat the puppet as a player: each chases the nearest of the
  player and the target puppets, and a monster's swing or shot that lands on one raises `puppet_struck` (`puppet`,
  `atk`, `power`, and the monster's `actor`, `cast`, `level`; a shot has no actor) and takes nothing: send it to
  that player's machine, which calls `combat.strike(atk, power)`. [H] The far-monster freeze, the camera and the
  boss modules still read the local player only.
- `combat.strike(atk [, power])` is a script's own hit on the player: refused while the i-frames run, priced against the
  player's DEF, halved in Boost, through the `"damage"` filters, raising `player_hit`. (`combat.hurt` takes HP as it
  is.) Friendly fire is the mod's choice: send the hit or do not.
- The event `player_effect` (`id`, `fwd`, `angle`, `voff`, `scale`, `attach`) is raised for each effect the player's own
  script makes (a skill's blast, a swing's flash); `world.puppet_effect(puppet, e)` makes the same effect at a
  puppet, placed from the puppet's position and heading, hitting nobody. Send the event's table as it is. [H] Not
  covered: sword trails, Hugo's shots and eyes, the charge glow (they are not script effects).
- `combat.boss_bonus(n)` adds `n` to a pool that the player's **next hit on a boss part** carries on top of its own
  damage (past the `"damage"` filters, which see a boss hit as `dir = "dealt"` with no `actor`); `boss_bonus(0)`
  empties it, `boss_bonus()` reads it. This is how two players share a boss: each machine runs its own boss and the
  one whose gauge (`world.boss()`) is fuller puts the difference in its pool. [H] The pool is spent by the next hit
  that does damage, whatever it lands on: on Velagunder's armour it goes to the stun gauge, not to the HP (seen in
  a drive run), so set it from the gauges' difference again rather than once. A player who never lands a hit keeps
  a full boss.
- `world.snapshot()` lists the living monsters: `id`, `cast`, `level`, `x, y, z`, `yaw` (degrees), `hp`, `max_hp`, and
  the picture: `sheet`, `tag`, `frame` (as a puppet's). It is plain data: `cleria.json.encode` it.
- `world.apply(snapshot [, {ai =, remove =}])` makes this room match and returns how many monsters matched. For
  each entry: the monster with that `id` is moved, turned and given that HP; one that is missing is built (that
  cast, level and id); one whose HP is under 1 dies here too (its death animation and sound; no EXP, no SP: share
  the rewards yourself). By
  default the monsters are **held** (`ai = false`: their handlers stop and they show the snapshot's picture, so they
  animate as on the host) and the monsters the snapshot does not name die the same way (`remove = true`). At most 256.
- Ids: a room's own monsters get the same ids on two machines that load the same room the same way; monsters built
  later get the ids the snapshot carries.

The shape that works: one machine owns a room's monsters (it runs them, sends a snapshot 10 to 20 times a second);
the others hold theirs and apply it, send their own hits as messages (the `"damage"` filter gives the value; the
owner lands it with `combat.damage`), and everybody sends `player.pose()` for the puppets.

[H] Not there, and why: the owner's monsters see and chase only the owner's player (the handlers read one player);
a held monster's hit volumes are stopped with its handler, so a guest is hurt only by what the owner sends
(`combat.strike`); the boss modules, the room scripts and the pickups are not part of a snapshot (API7.md #52):
each machine runs its own. Checked in a drive run (`lua_api7_sync`): the snapshot of S_1002 (11 monsters), applied
with one moved and hurt and one left out, gives 10 monsters, the first at its new place with its new HP, held.

### 12.20 Drawing in the world, more page widgets, the player's look

```lua
cleria.hud.layer("marks", function(d)
    d:world_text(x, y + 2, z, "Here", {size = 18, color = 0xFFD040})   -- centred on the point
    d:world_line(x1, y1, z1, x2, y2, z2, {thickness = 2})
    d:world_circle(x, y, z, 3, {color = 0xFF4040})                     -- a ring of radius 3 on the ground
end)

p:table({{"Name", "Kills", "Best"}, {"Slime", 12, "0:41"}, {"Bat", 3, "1:02"}})   -- the first row is the header
p:list({"one line", "another"})
p:image("art/map.png", {height = 160})

cleria.player.set_look({scale = 1.4, glow = 0x203018})                 -- glow = false clears it
```

- `d:world_text / world_line / world_circle` are `d:text / line` at a world point: each returns false (and draws
  nothing) when a point is behind the camera. They use the last drawn frame's camera, as `world.to_screen`.
- `p:table(rows [, {header = false, color =}])`: up to 200 rows of up to 8 equal columns, cut at the column's edge.
  `p:list(items)`: one bulleted, wrapped line each. `p:image(file [, {height =}])`: a PNG of the mod's folder, its
  width following its proportions; false when the file does not load.
- `player.set_look{scale =, glow =}`: the player's sprite, 0.3..3 times its size (not its collision), and a colour
  **added** to it (the game's own glow colour, as a charged skill's): dark values tint, bright ones wash the sprite
  out (0x80FF80 turns it into a light; checked on a capture). It is the process's, like `tune`. A charged skill's
  glow shows in its place while it lasts.
- `world.form(actor, {sprite = "file.aia"})` draws a monster from a sheet of the mod's own folder in place of its
  cast's (the game's AIA format; the tags its script asks for must exist in it). Checked on a capture: a monster
  given another monster's sheet copied into the mod's folder is drawn from it. [H] There is no converter from PNG
  to AIA here: this serves a sheet taken from the game or made with a tool of your own (API7.md #42).

### 12.21 The built-in Lua layer: a shop, items, skills, rooms, characters

These are written in Lua over the calls above (`src/cleria/modscript_prelude.inl`): every mod has them, and a mod
could have written them. They keep their state in the mod's own storage.

```lua
cleria.ui.shop({title = "Wandering merchant", text = "What will it be?",
    items = {{name = "Potion", price = 20, desc = "Heals 5.", buy = function() cleria.content.add_item("potion") end},
             {name = "Mystery box", price = 100, stock = 1, buy = open_box}},
    on_buy = function(item, index) end, on_close = function() end})
```

- `ui.shop(def)` opens a window (id `def.id` or `"shop"`) that shows the SP wallet and one button per item: `name`,
  `price` (SP), `desc`, `stock` (counts down; `nil` = no limit), `enabled` (a boolean or `fn(item)`), `buy =
  fn(item, index)`. A purchase takes the SP then calls `buy` and `on_buy`. A row is greyed when the SP or the stock
  is short.

```lua
local C = cleria.content
C.define_item("potion", {name = "Potion", desc = "Heals 5.", max = 9,
    use = function() if cleria.player.hp() >= cleria.player.max_hp() then return false end cleria.combat.heal(5) end})
C.define_item("charm", {name = "Lucky charm", on_change = function(n) cleria.player.set_modifier("atk", 2 * n) end})
C.add_item("potion", 2)      C.use_item("potion")      C.item_count("potion")      C.items()      C.open_bag()
```

- **Items of the mod's own** live in the save (`cleria.save`, key `cleria.items`), not in the game's inventory:
  the game's item screen does not show them, `C.open_bag([{title =, empty =}])` does. `add_item(id [, n])` gives (or
  takes, with a negative `n`), within 0..`max` (99), shows a notice (`notice = false`: none) and returns the new
  count (`false` while the save is not ready). `use_item(id)` calls `use`; a `use` that returns `false` spends
  nothing; `keep = true` never spends. `on_change(count)` is called on every change **and after each load or new
  game**: apply a held item's effect there.

```lua
C.define_skill(3, {mp = 100, cooldown = 2, cast = function(c)        -- c = {slot, level, charged, air, cost}
    for _, a in ipairs(cleria.world.actors()) do cleria.combat.damage(a.id, 30) end
    cleria.screen.flash(0xFFFFFF, 0.2)
end})
```

- `define_skill(slot, def)` replaces what bracelet `slot` (1..3) casts: when the game would cast, `cast` runs
  instead, `mp` (default: the game's cost) is paid and the game's own cast does not happen (no `skill_used`).
  `cooldown` in seconds of play; `character = 1..3` limits it to one character; `cast` returning `false` cancels
  (nothing paid); `keep = true` lets the game's cast follow (then only the game's cost is paid). It rests on the
  world filter `"skill"` (`{slot, item, level, cost, mp, air}`; `"suppress"` refuses the cast), which a mod may use
  directly. `C.undefine_skill(slot)`.

```lua
C.define_room("arena1", {base = "S_1002",
    monsters = {{43, 0, 2, 5, level = 20}, {id = "brute", x = 3, y = 2, z = 5}},
    exits = {[0] = "arena2", [1] = false, default = "S_1009"},
    music = 18, on_enter = function(room) end, on_clear = function(room) end})
C.enter_room("arena1")      C.room()
```

- A **room variant** is one of the game's rooms under another identity: its geometry, camera and doors are the
  base room's. While the player is in it, the base room's own monsters are not built (`keep_monsters = true` keeps
  them; a mod's own `world.spawn` always passes: the `"spawn"` query's `by` names the mod that asked, `""` for the
  room's own), `monsters` are built instead (a cast id or a variant's id, then x, y, z), and each exit goes where
  `exits` says: by its index or its door label, to a room, to another variant, `false` = closed, absent = where it
  really leads. `on_clear` runs once when the last monster dies. One enters by `C.enter_room(id)` or through
  another variant's exit; leaving by a plain exit ends it. [H] The base room's scripts still run: pick rooms whose
  scripts do not wait for their own monsters.

```lua
C.define_character("giant", {name = "Giant", base = 1, hp = 2, atk = 1.5, speed = 0.8, jump = 1.1, air_jumps = 0,
    scale = 1.4, glow = 0x301010, on_apply = function(def) end})
C.set_character("giant")      C.character()      C.set_character(nil)
```

- A **character variant** is one of the three characters with other numbers and another look: `hp` / `atk` / `def`
  (multipliers; `hp_add`, `atk_add`, `def_add` to add), the `player.tune` keys, `scale`, `glow`. `base` limits it to
  one character (`set_character` then answers false for another). The choice is kept in the save and applied again
  after a load or a new game. It uses this mod's `set_modifier`, `tune` and `set_look`: a mod that defines
  characters leaves those three to them. [H] A new character with its own moves or sheet is not this: the three
  characters' scripts and sheets are the game's (API7.md #42).

Checked in a drive run (`lua_api7_layer`): a slot-3 skill of the mod's own casts and pays 100 MP with no
`skill_used`; an item given twice and used once; a variant with twice the HP; a shop purchase by the window's OK
(50 SP -> 30); a variant of S_1002 entered from S_1009 holds only the variant's monster, and `on_clear` runs at its
death.

### 12.22 Sounds of the mod's own, a new game, a line on the save card

```lua
cleria.audio.play("sfx/ding.wav", 0.8)                  -- a WAV of the mod's folder
cleria.audio.music("music/theme.ogg")                   -- an Ogg Vorbis file of the mod's folder
cleria.game.new_game(2)                                 -- a new game as Hugo, now
cleria.save.card("Seed 4F2A  floor 12")                 -- shown on this save's card
```

- `audio.play(file [, volume])`: a `.wav` inside the mod's folder, any format SDL reads, converted once and kept
  (at most 512 files a run). It plays flat (no position), as often as asked, through the SFX volume and the SFX
  switch. False when the file is missing or does not load.
- `audio.music(file)`: an `.ogg` inside the mod's folder in place of a BGM id: it replaces the playing track, loops
  when it carries the `LOOPSTART` / `LOOPLENGTH` comments the game's own tracks use (else it plays once), and
  follows the BGM volume. The next room whose music differs switches back to the game's: start yours again on
  `room_enter` if it must stay.
- `game.new_game([character [, difficulty]])`: leaves the running game for the title, which starts a new game at
  once (no menu): the character (1..3, default the current one), the difficulty as `game.difficulty()` gives it
  (0..4, default the current one), the normal outfit. Nothing is saved first. The `new_game` event follows.
  `game.to_title()` and this one end the game loop on the tick they are called.
- `save.card(text)`: up to 40 characters, kept in the save and shown after the area's name on the save's card in the
  Save and Load pages (the pause book's and the title's). `card(nil)` removes it. False while the save is not ready.
  [H] The card's line is not widened for it: a long area name and a long text run past the card's edge.

Checked: the files' rules in `cleria-luatest`; `lua_api7_newgame` (a game as Yunica, `new_game(2)`: the first game
stops on that tick, the `new_game` event says 2, `game.character()` is 2). [H] The sounds themselves were not
listened to here (the tests run muted): the calls reach `Audio::play_file` / `play_bgm`, which the game's own
sounds use.

### 12.23 Tools for writing mods

```lua
cleria.mods.command("give", "potion 3")                 -- a cleria.command of ANY scripted mod; true when one ran
cleria.mods.commands()                                  -- every command's name, sorted
cleria.mods.log_lines(20)                               -- the last lines the mods logged ("[id] text")
cleria.mods.reload()                                    -- this mod's script, started afresh at the next frame
cleria.ui.console()                                     -- a window: type a command, read the log
```

- **Reloading a script without a restart.** `mods.reload()` (bind it to a key with `cleria.input.action`), the drive
  command `modreload=<mod id>`, or the environment variable `CLERIA_MOD_WATCH=1`, under which a mod is reloaded when
  its main script file changes on disk (checked once a second): edit, save, look at the game. A reload closes the
  mod's Lua state (its handlers, filters, timers, sockets, windows and definitions go with it), drops what it left
  with the game (its stat modifiers, its `tune`, its game speed, its hold, its hidden HUD parts) and runs the main
  file again. A mod that had stopped on an error starts again. What it built in the room (monsters, puppets) stays
  until the room changes; `cleria.save` and `cleria.settings` are files and are still there. The `new_game` / `load`
  events are **not** raised again: a script that sets itself up there should also do it at its start when
  `cleria.game.state() ~= "title"`.
- **The console.** `ui.console()` opens a window with a text field (Enter runs `name arguments` as a command of any
  mod), the list of commands and the log's last lines. It does not pause the game. Give it a key in a mod of your
  own; the Debug Menu showcase mod does.
- **`tools/mod_test.py <mod folder> [case ...]`** runs a mod's own cases (`tests/cases.py` or `.dev/cases.py`:
  a room, a drive script, the lines expected in the output, captures) through the headless harness, each in its own
  scratch settings and saves with only that mod turned on. Its header documents the case format. The drive
  commands a mod's test needs: `lua=<command> <args>`, `modkey=<key>` / `modpad=<button>` (the mod's bound actions),
  `modwin=ok|back|up|down|left|right` (the mod's open window: the same widgets as its F1 page, so a page is tested by
  showing it in a window), `modreload=<id>`, `shot`.

Checked in a drive run (`lua_api7_tools`): a command run through `mods.command`, the console and the bag opened,
and a `modreload=` while the mod held the game with `tune{speed = 3}`: the script starts afresh, the game is no
longer held and 30 ticks of walking cover 2.5 again.

## 10. Tests

```
cmake --build build_diff --config Release --target cleria-luatest cleria-nettest cleria-view
ctest --test-dir build_diff -C Release -R "luatest|nettest"
python tools/drive_regress.py --exe build_diff/Release/cleria-view.exe --assets <assets> aplua_chest aplua_items_save aplua_deathlink aplua_goal_traps aplua_seed_guard aplua_off shell_subpages
```

`cleria-nettest`: the WebSocket under `cleria.net` (address parsing; a loopback echo server: wss -> ws fallback,
echo, a 2 MB message, idle beyond the handshake timeout, a kick, a polite close, a close the server ignores,
nothing listening, a bad address). `cleria-luatest` (65 checks): the sandbox (no io / package / debug / dofile / os.execute / getenv /
string.dump, bytecode refused, `require` / `cleria.read` / `cleria.data` cannot leave their folders, print to
the log); a runaway handler stopped in milliseconds, also through `pcall`, a runaway main chunk, a heap bomb,
errors with file:line (main chunk, handler, syntax, a module), a disabled mod getting no calls, other mods
unaffected; every API family round-tripped through a fake host (events, flags, player, warps, give, emit,
chests, treasure_text, save storage incl. quotes / backslashes / newlines / % through a real sidecar,
settings typing, data files, json incl. null / int64 / empty arrays, the filter's three decisions, icons,
hud layers in order with colours / alpha / kit names / images, page widgets, exp_factor, speedrun,
commands, draw calls refused outside a layer); websockets (the 4-socket limit, a bad address, a refused
connection reported by `poll`); API 6 (each combat filter's answers, the two events, stats, modifiers, add_exp, spawn_pickup, bad arguments, a failing
filter); the manifest's `Script=`; both samples start; archipelago_lua's own table (336
locations, 147 items), its page "Archipelago", its README and icon (self-contained).

## 11. API levels

`cleria.api_version` is the level of the running CleriaCore. A mod's `ApiVersion` is the level it needs: a
higher one than the build's is refused ("needs a newer CleriaCore"); 3 is enough for a script, and a level-3
mod can test `cleria.api_version >= 4` (or the function for `nil`) before it uses what a later level added.

| level | added |
|---|---|
| 3 | everything above not marked otherwise |
| 4 (2026-10-09) | the events `boss_defeated`, `item_used`, `door_opened` (§3); `game.save(slot)` (§4) |
| 5 (2026-10-09) | the blessing shop: `cleria.shop.add_filter` and the event `blessing_bought` (§5.1) |
| 6 (2026-10-10) | `cleria.combat.add_filter` for `"exp"`, `"sp"`, `"damage"`, `"drop"`, `"pickup"` (§5.2); the events `enemy_killed`, `pickup_collected` (§3); `player.stats`, `player.set_modifier` / `modifier`, `player.add_exp` (§4, §4.1); `game.spawn_pickup` (§5.2) |
| 7 (2026-10-13) | §12: timers (`cleria.after`, `every`, `cancel`), `cleria.rng`, `cleria.mods`, `cleria.bus`, the events `retry`, `level_up`, `state_changed`, `floor_changed`, `enemy_hit`, `player_hit`, `load.retry`, `game.state` / `room_title` / `floor` / `zone` / `difficulty` / `play_time`, `cleria.world.actors` / `actor` / `to_screen`, `cleria.input` (§12.9), `cleria.ui.window` / `choose` / `message`, `p:text`, `p:progress` (§12.10), `cleria.world.add_filter` (`"spawn"`, `"exit"`), `world.spawn` / `remove` / `set_actor` (§12.11), `cleria.audio`, `cleria.screen` (§12.12), `cleria.world.puppet` / `set_puppet` / `remove_puppet`, `player.pose` (§12.13), the events of §12.14, `cleria.script.run` / `add_filter` (§12.14), `cleria.combat.damage` / `heal` / `hurt`, `cleria.player.tune` (§12.15), `cleria.net.udp` / `tcp` / `http` (§12.16), `cleria.camera`, `hud.show`, `game.pause`, `world.boss`, `enemy_spawned` (§12.17), `content.define_monster` / `define_boss` / `morph`, `world.form` (§12.18), `puppet_hit`, `combat.strike`, `world.snapshot` / `apply` (§12.19), `d:world_*`, `p:table` / `list` / `image`, `player.set_look`, the `"skill"` filter (§12.20), `ui.shop`, `content.define_item` / `define_skill` / `define_room` / `define_character` (§12.21), `audio.play`, `audio.music(file)`, `game.new_game`, `save.card` (§12.22), `mods.command` / `commands` / `log_lines` / `reload`, `ui.console` (§12.23), the writes of §12.8 (`player.set_position`, `equip`, `set_status`, `game.goto_room`, `set_speed`, `load` ...), a x0 stat modifier, `cleria.clipboard`, `open_url`, `locale`; more as `API7.md` fills |
| no level (2026-10-09) | `game.unlock_character` (§4), `save.set_profile` / `save.profile` (§6.1): test the function for `nil` |
