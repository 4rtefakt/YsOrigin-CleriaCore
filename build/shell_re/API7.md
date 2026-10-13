# Mod API 7: the plan and where it stands

Branch `feat/mod-api-7`. The list was agreed with the user on 2026-10-13 after fourteen public mods were written on
API 6 (github.com/4rtefakt/cleriacore-*): every gap below was either hit while writing them or asked for.
The reference stays `LUA_API.md`; this file is the work list. Status: **done** (code, luatest, a drive case, the
reference), **part**, or blank.

Order of work: the runtime-only pieces first (no game state), then reads of the live game, then writes, then new
content, then multiplayer, then the tools.

## A. Runtime (no game needed)

| # | What | Lua | Status |
|---|---|---|---|
| 12 | Timers | `cleria.after(s, fn [, {play = true}])`, `cleria.every(...)`, `cleria.cancel(id)` | done |
| 57a | A seeded random generator | `cleria.rng(seed)` -> `:int(a, b)`, `:float()`, `:shuffle(t)` | done |
| 10 | Mods see each other | `cleria.mods.list()`, `mods.is_active(id)`, `cleria.bus.publish / subscribe`, `bus.get / set` | done |
| 17 | A modifier of x0 is a modifier | `player.set_modifier("max_hp", 0, 0)` | done |
| 57b | Clipboard, a web link, translations | `cleria.clipboard(text)`, `cleria.open_url(url)`, `cleria.locale()` | done |

## B. Knowing what the game is doing

| # | What | Lua | Status |
|---|---|---|---|
| 6 | The state of the game | `game.state()`: title, play, cutscene, dialog, book, shop, panel, gameover; the event `state_changed` | done |
| 7 | Floor and zone | `game.floor()`, `game.zone()`, `game.difficulty()`, `game.play_time()`; the event `floor_changed` | done |
| 9 | Retry | the event `retry`; `load` says `retry = true` when it is the continue snapshot | done |
| 14 | Plain events | `level_up`, `player_hit`, `skill_used`, `boost`, `statue_used`, `enemy_hit` | done (`level_up`, `player_hit`, `enemy_hit`, `skill_used`, `boost`, `statue_used`, `interact`) |
| 13 | More in the damage query | `actor` (an id), `hp`, `max_hp`, `kills` | done (`actor`, `hp`, `max_hp`) |
| 3 | The monsters of the room | `cleria.world.actors()` -> id, cast, name, level, x y z, hp, max_hp, state | done |
| 34 | Bosses | `cleria.world.boss()` -> name, hp, max_hp, phase, parts | part: `world.boss()` reads the gauge (hp, max_hp, actor); no phases or parts (each module keeps its own) |
| 33a | Projectiles | `cleria.world.bullets()` | |
| 35a | Drop tables | `cleria.data.drops(cast)` | |
| 2 | World to screen, the camera | `cleria.world.to_screen(x, y, z)`, `cleria.camera.get()` | done (`world.to_screen`, `camera.get`) |

## C. Changing the player and the flow

| # | What | Lua | Status |
|---|---|---|---|
| 15 | Gauges and equipment | `player.set_mp`, `set_boost`, `set_exp`; `player.equipment()`, `player.equip(slot, item)` | done |
| 16 | Position, rooms | `player.set_position(x, y, z)`, `game.goto_room(room [, entrance])` | done (`set_position`, `goto_room`, `exits`, `take_exit`) |
| 22 | Status effects, invulnerability | `player.status()`, `set_status(name, ticks)`, `player.set_invulnerable(ticks)` | done |
| 23 | Ability switches | `player.abilities()`, `player.set_ability(name, on)` | done (`double_jump`, `boost`) |
| 21 | Movement | `player.set_movement{speed =, jump =, gravity =, air_jumps =}` | done as `player.tune{speed =, jump =, air_jumps =}`; gravity not (a constant of the integrator) |
| 24 | More stat modifiers | `set_modifier` for `luck`, `mp_cost`, `mp_regen`, `boost_gain`, `speed` | done as `player.tune{mp_cost =, mp_regen =, boost_gain =}`; luck is the `"drop"` filter |
| 32 | Damage and healing from Lua | `cleria.combat.damage(actor, n [, opts])`, `combat.heal(n)` | done (`combat.damage` with the real kill, `combat.heal`, `combat.hurt`) |
| 46 | Game speed | `game.set_speed(x)`, `game.pause(on)` | done (`game.set_speed`, `game.pause`) |
| 45 | Flow | `game.load(slot)`, `game.retry()`, `game.to_title()`, `game.new_game(character)` | part: `load`, `retry`, `to_title`; `new_game` not yet |
| 47 | The game's random numbers | `game.seed_random(n)` | done |
| 48 | Text on a save card | `cleria.save.card(text)` | |

## D. Drawing and input

| # | What | Lua | Status |
|---|---|---|---|
| 1 | Key and pad binds | `cleria.input.action(id, {label =, key =, pad =}, fn)`, `input.down(id)` | done (the Keys section is a text field per action; a press-to-bind capture comes with #29) |
| 25 | Drawing in the world | a `world` hud layer: `d:world_text / world_line / world_circle / world_image` | |
| 26 | The camera | `cleria.camera.set{zoom =, offset =}`, `camera.shake(...)`, `camera.reset()` | done (`camera.set{distance, fov, x, y, z}`, `shake`, `look_at`, `reset`) |
| 27 | Screen effects | `cleria.screen.flash(color, s)`, `screen.fade(...)`, `screen.tint(...)` | done (`screen.fade`, `screen.flash`; no tint that stays under the HUD yet) |
| 28 | The game's HUD | `cleria.hud.show(part, on)` | done (`hud.show("status" / "boss" / "messages" / "all", on)`) |
| 30 | Page widgets | `p:progress`, `p:image`, `p:table`, `p:columns`, `p:list` | part: `p:text`, `p:progress` |
| 11 | Asking the player | `cleria.ui.message(text)`, `cleria.ui.choose(text, options, fn)` | done (`ui.choose`, `ui.message`: in the shell's look, not the game's text window) |
| 29 | Windows outside F1 | `cleria.ui.window(id, {title =, key =}, fn(p))` | done |
| 20 | Sound | `cleria.audio.play(file)`, `audio.music(file)`, `audio.se(id)` | part: the game's own sounds and music (`audio.se`, `music`, `stop_music`); a mod's own files not yet |

## E. Monsters and combat

| # | What | Lua | Status |
|---|---|---|---|
| 31 | A filter on each monster as its room loads | `cleria.world.add_filter("spawn", fn(q))` -> cast, level, hp / atk / def multipliers | done (checked on a capture: a swapped monster is drawn and fights) |
| 4 | Spawning and removing | `cleria.world.spawn(cast, x, y, z [, opts])`, `world.remove(actor)`, `world.set_actor(actor, {...})` | done (`spawn`, `remove`, `set_actor`) |
| 33b | Spawning a projectile | `cleria.world.shoot(kind, from, dir, opts)` | |
| 35b | Changing drop tables | `cleria.data.set_drops(cast, list)` | |
| 37 | Monster variants | `cleria.content.define_monster(id, {base =, name =, hp =, atk =, ...})` | done (`content.define_monster`, `content.morph`, `world.form`, `world.spawn(name, ...)`) |
| 59 | Custom bosses | a monster variant with a gauge, phases and a Lua brain: `define_boss` | done (`content.define_boss`: the gauge, `phases`, `on_hit` / `on_death`, `ai = false`); the brain stays the base cast's or the mod's Lua |

## F. New content

| # | What | Lua | Status |
|---|---|---|---|
| 36 | Items with behaviour | `cleria.content.define_item(id, {name =, icon =, use = fn, equip = fn})` | |
| 38 | Doors | `cleria.world.add_filter("exit", fn(q))` | done |
| 43 | Scripts | `cleria.script.run(name)`, `script.add_filter(fn(q))`, the event `script_started` | done (`script.run`, `script.add_filter`, `script_started`) |
| 44 | Shops and menus | `cleria.ui.shop{...}` | |
| 39 | Rooms | `cleria.content.define_room(id, {...})` | |
| 40 | Skills | `cleria.content.define_skill(slot, {mp =, cooldown =, cast = fn})` | |
| 41 | Character variants | `cleria.content.define_character(id, {base =, ...})` | |
| 42 | New characters and monsters with their own models | the model pipeline | |

## G. Network

| # | What | Lua | Status |
|---|---|---|---|
| 19 | HTTP, TCP | `cleria.net.http{...}`, `cleria.net.tcp()` | done (`net.http(opts, fn)`, `net.tcp()` with `connect` / `listen` / `accept`) |
| 54 | UDP | `cleria.net.udp()` | done (`net.udp()`) |

## H. Multiplayer

| # | What | Lua | Status |
|---|---|---|---|
| 49 | A puppet character | `cleria.world.puppet(character)` -> `:set{x, y, z, facing, anim}` | done (`world.puppet`, `set_puppet`, `remove_puppet`; Actor::sprite_override; checked on captures) |
| 53 | The player's own actions | the events `attack`, `skill_used`, `jump`, `land`; `player.pose()` | done (`attack`, `skill_used`, `jump`, `land`, `player.pose()`) |
| 50 | Hits between players | a hit shape on a puppet; `source` in the damage query | |
| 51 | Monster state | `cleria.world.snapshot()`, `world.apply(snapshot)` | |
| 52 | Bosses and room scripts | the same for the boss modules and the script VM | |

## I. For the people who write mods

| # | What | Status |
|---|---|---|
| 18 | The F1 panel in a headless capture; a drive command that presses a page's widget | |
| 55 | Reloading a mod's script without a restart | |
| 56 | A console for mod commands | |
| 58 | The mods' test runner in `tools/` | |

## Added along the way

(Anything found while doing the above goes here with its own number, from 60.)
