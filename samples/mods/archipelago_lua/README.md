<div align="center">

# Archipelago for Ys Origin (a CleriaCore mod)

[![Latest release](https://img.shields.io/github/v/release/4rtefakt/ys-origin-cleriacore-ap?label=release&color=2f9e44)](https://github.com/4rtefakt/ys-origin-cleriacore-ap/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/4rtefakt/ys-origin-cleriacore-ap/total?color=4c6ef5)](https://github.com/4rtefakt/ys-origin-cleriacore-ap/releases)
[![Archipelago](https://img.shields.io/badge/Archipelago-multiworld-e64980)](https://archipelago.gg)
[![apworld](https://img.shields.io/badge/apworld-2.0.1%2B-f08c00)](https://github.com/4rtefakt/ys-origin-archipelago)
![Runs on](https://img.shields.io/badge/runs%20on-CleriaCore%20%C2%B7%20CleriaOre-7048e8)
![Lua](https://img.shields.io/badge/Lua-5.4-000080?logo=lua)

</div>

> **Status: 0.2.0.** Played in the Steam game (CleriaOre) and in CleriaCore against a local Archipelago
> server, and covered by CleriaCore's drive cases; not yet through a long multiworld: please report anything
> odd. Made for **Ys Origin apworld 2.0.1 or newer**.
>
> **Download:** grab `ysorigin.archipelago-<version>.cleriamod` from the
> [Releases](https://github.com/4rtefakt/ys-origin-cleriacore-ap/releases) page and double-click it
> (see *Install*). The `.zip` there is the same mod for dropping into the mods folder by hand.

Play Ys Origin in an [Archipelago](https://archipelago.gg) multiworld, in the **Steam game** with the
[CleriaOre](https://github.com/4rtefakt/CleriaOre) mod loader or in
[CleriaCore](https://github.com/4rtefakt/YsOrigin-CleriaCore) (the clean-room Ys Origin engine, which also
runs on Linux and macOS). This mod is the Archipelago client: it connects to the room, sends your checks,
receives your items, and keeps the seed's rules in your saves. It is written in Lua on the mod API the two
share; nothing in either is Archipelago-specific.

This is the recommended client for the apworld. It replaces the older `dinput8.dll` client of the apworld's
repository (see *Coming from the dinput8.dll client* below).

It plays seeds of the **Ys Origin apworld** by 4rtefakt,
[github.com/4rtefakt/ys-origin-archipelago](https://github.com/4rtefakt/ys-origin-archipelago),
unchanged: generate and host the seed with that apworld (2.0.1 or newer: its item and location table
is the one in `data/ys_origin.json`), then connect with this mod.

## Requirements

- **One of:**
  - **Ys Origin on Steam** (`yso_win.exe` 1.1.1.0, Windows) with **CleriaOre** 1.0 or newer installed;
  - **CleriaCore** with mod API 3 or newer (API 4 adds the autosave and the boss-defeat event, API 7 the
    cutscene skip and the overlay key; the mod uses them when they are there), with your Ys Origin game data
    set up in it (F1 > Prelaunch).
- A room made with the Ys Origin apworld (archipelago.gg or your own MultiServer).

The connection uses the engine's own WebSocket and TLS: there is nothing else to install and no other program
to keep open.

## Install

### In the Steam game (CleriaOre)

1. **Install CleriaOre**: run `CleriaOre-Setup-<version>.exe` from its
   [latest release](https://github.com/4rtefakt/CleriaOre/releases/latest), then on its **Game** page press
   **Install the loader**.
2. **Double-click `ysorigin.archipelago-<version>.cleriamod`.** CleriaOre installs the mod and turns it on.
   Or, on CleriaOre's **Mods** page, press **Install a mod file...** and pick it.
3. Start the game. Press **F1**, open **Mods > Archipelago**, enter the **Server** (e.g.
   `archipelago.gg:38281`), your **Slot name** and the **Password** if the room has one, then **Connect**.
   *Connect at start* reconnects by itself next time. The same page is on the mod's page in CleriaOre's
   manager, outside the game.
4. Start a **New Game** with the seed's character, or load a save of that seed.

### In CleriaCore

1. **Double-click `ysorigin.archipelago-<version>.cleriamod`.** CleriaCore installs the mod and turns it on
   (an older version is replaced); if CleriaCore was already running, restart it. Or, in CleriaCore:
   F1 > Mods > *Install a mod file...*.
   - Without the `.cleriamod`: drop this folder (or a `.zip` of it) into CleriaCore's **mods folder**,
     `Saved Games\CleriaCore\mods` (F1 > Mods > *Open mods folder* opens it), then **F1 > Mods**: select
     **Archipelago**, open it, set **Enabled** to On, and restart CleriaCore.
   - The `.cleriamod` is made from this folder by CleriaCore's `tools/pack_mod.py`.
2. **F1 > Mods > Archipelago**: enter the **Server** (e.g. `archipelago.gg:38281`; `ws://` or `wss://`
   may be given, otherwise the secure connection is tried first), your **Slot name** and the
   **Password** if the room has one, then **Connect**. *Connect at start* reconnects by itself next time.
3. Start a New Game with the seed's character, or load a save of that seed.

### Then

A save is stamped with the seed the first time it is played connected; save at the goddess statues as usual
(the autosave also writes No.08 after each check). The Archipelago state rides in the `yso_NN.cleria` file
beside each save (the save itself stays a retail save).

The settings live in the engine's ini (`cleriaore\cleriaore.ini` in the game's folder, or CleriaCore's
`cleria.ini`), section `[cleria.mod.ysorigin.archipelago]` (the password in plain text in your own settings,
never shown or logged). The item-name cache, the seed's cached slot_data and `state.json` (for external
trackers) live in `modsdata/ysorigin.archipelago/` beside it.

### Coming from the dinput8.dll client

CleriaOre's loader is itself a `dinput8.dll`, so installing it replaces the older client's file (CleriaOre
keeps it as `dinput8.dll.before-cleriaore` and puts it back if you remove the loader). Its `yso_ap.cfg` is not read: enter the server and
slot once on the mod's page. A run started on the older client keeps its saves in that client's own
`archipelago_<seed>` folder, which this mod does not look in: finish that run with the client it started on,
or start the seed again here.

## What it does

- **Checks**: chests, bosses, statues, the blessing shop's upgrades, rooms and floors, detected the way
  the apworld defines them (`location_signals` / `location_detect`), sent at once, kept in the save and
  re-sent after every login (play offline, connect later).
- **Chests**: the vanilla item of a randomized chest is swallowed; the rising icon and the treasure box
  show what the chest really held ("Sent Button Activation to Clicker.", or your own item).
- **Items**: granted with the game's own rules (keys, consumables, SP, the Cleria Ore weapon tiers,
  progressive gear / skills / blessings, gems, statue unlocks), never into a cutscene, and never twice
  (the received index is saved with the game, so reloading an older save re-grants exactly what it
  missed).
- **Roda Fruit** that cannot be wasted: the Roos can be fed in any order, so the count is kept at "Roos
  still unfed among the ones you have fruits for" (the seed's `roo_flags`). Feeding a late Roo first never
  starves an early one that logic promised.
- **Random start**: a New Game of a `random_start` seed is warped once to the seed's start statue, which
  is lit so it saves and warps.
- **Story repairs**: Hugo's Eyes of Fact left hidden after a cut-short scene are put back; for Yunica, the post-Kishgal "I'm just a burden" state on 1F and a stuck Dreaming Idol
  chain are detected and put right, as in the retail mod.
- **Withheld items**: when a story scene "gives" an item the seed moved elsewhere (the Zelkarons charging
  the Evil Ring), the game's own copy is withheld (a line in the mod's log, nothing on screen).
- **Invariants**: a key item never counts above 1 (the scripts test for exactly 1) and a skill level
  never goes above 3. A weapon tier the seed did not grant is taken back (the 4F Roo's reward raises the
  weapon with no item behind it).
- **SP chests**: the five chests that pay SP do it with a script command, not an item, so the vanilla SP
  (2,000 to 20,000) used to come on top of the seed's item. It is taken back when the chest is a location.
- **Traps**: EXP Leech, Chaos Warp, Butterfingers, Blinding Fog.
- **DeathLink** (option, or on when the seed has it): your deaths are sent, theirs kill you (not during
  a story duel).
- **Goal**: Darm (or every boss, by the seed's goal) sends the goal to the server.
- **Overlays**: a connection line, an item / chat feed, a per-area tracker (each can be turned off). A release
  or a collect (more than 6 items in one go) is one feed line; the chat window has every line.
- **Chat** (mod API 7): **F6** opens a window over the game with the room's last lines and a line to type
  into: chat, or a server command (`!hint <item>`, `!help` ...). The game runs on underneath; the key can be
  changed in the page's Keys section.
- **Warp map counts**: on the statues' warp map, each floor's checks found / total on its bar (lime while
  some are left, gold when all are found); the floors with no statue are listed bottom left. Needs a
  CleriaCore or CleriaOre whose `game.state()` reports the warp map (`"map"`).
- **Seed save folder**: a seed's saves are kept apart, in `profiles/AP_<seed>_<slot>` (set on the title at
  connect). Saves of the retail `dinput8.dll` client live in its own `archipelago_<seed>` folder and are not
  found here: finish a run with the client it started on.
- **Apworld check**: a seed made with a newer apworld than this mod knows is reported in the feed.
- **Seed guard**: a save of one seed refuses a room of another seed; offline, a stamped save keeps its
  seed's rules from the cached slot_data.

## Quality of life (all optional: F1 > Mods > Archipelago)

| option | default | |
|---|---|---|
| **Left in this room** | off | An overlay, bottom left: the current room's locations still to find. A hinted one shows its item. |
| ...with what they hold | off | The same list with the item at each location (a spoiler). |
| **Tracker detail** | Areas | The tracker overlay counts per area of the tower, or per floor. |
| **Autosave** | on | Saves the game by itself after a check, a received item, a door opened with a key or medallion and a Panacea used, at the next safe moment (not in a cutscene, a boss fight or an arena). Needs a CleriaCore with mod API 4; older builds show it greyed. |
| **Autosave slot** | No.08 | The slot the autosave writes, as numbered in the book. It is overwritten without asking. |
| **Boss checks on defeat** | off | A boss room's check is sent when the fight is won, not when you walk in (the floor bosses, the duels and the 20F ward; the 17F room has no fight and stays on entry). |
| **Cutscene skip** | Hold Right Ctrl | Off, held (the "Skip cutscenes" key, rebindable under Controls) or Always. CleriaCore skips a cutscene whole; CleriaOre fast-forwards its waits. Needs mod API 7. |
| **Hide the overlay** | Insert | Hides the feed, the status line, the tracker and the room and map counts until pressed again (rebindable under Controls). The Blinding Fog trap stays. Needs mod API 7. |
| **Skip the intro on a random start** | on | A random-start New Game skips its intro (cutscenes and movies) until you stand at the start statue. Needs mod API 7. |
| **Hint alerts** | on | A feed line when you enter a room that holds a hinted item. |
| **EXP multiplier** | 0 | Your own multiplier for every kill; 0 keeps the seed's. |
| ...base / ...catch-up | 0 | With the EXP multiplier at 0, on a seed whose level scaling multiplies EXP: your own base and catch-up multipliers in place of the seed's (the retail client's `exp_mult_base` / `exp_mult_catchup`); 0 keeps the seed's. |
| **Seed save folder** | on | This seed's saves in a folder of their own. Off: the normal saves. |
| **Warp map counts** | on | Each floor's checks found / total on the warp map. |

The page also has: your **hints** (the server's list, kept up to date) with a field to ask for one and your
hint points; a **chat / command** line with Send, and **Release** / **Collect** buttons once the goal is
complete; and under *This game*, whether you hold the three **elemental skills** the final fight needs, the
**floor bosses** beaten when the seed's goal asks for them, and how many locations are left in the room.

## slot_data options

| option | |
|---|---|
| `character`, `goal`, `death_link`, `location_signals` / `location_detect`, `suppress_items`, `suppress_give_ids`, `item_index`, `skill_grants`, `sp_items`, `progressive_gear` / `progressive_skills` / `progressive_blessings`, `start_items` / `start_weapon` / `start_level`, `level_scaling` and its EXP keys (`level_margin`, `exp_base_mult`, `exp_catchup_mult`, `exp_catchup_margin`, `scene_levels`), `scene_floors`, `scene_names`, `statue_warp_locks` + `statue_unlocks` + `start_statue_scene`, `random_start`, `roo_flags` | supported |
| `blessing_items` (the purchase is the check, the effect becomes an item) | supported with mod API 5 (`cleria.shop`): the purchase sends the check and applies nothing, a bought row reads [Done] and a received effect leaves its slot on sale. Older builds give the vanilla effect with the check |
| `blessing_costs`, `blessing_vanilla_price_map`, `blessing_shop_unlock`, `shop_hints` | supported with mod API 5: the statue menu charges the seed's prices, one more slot goes on sale per floor visited (progression never waits), and a row names what its location holds. Older builds keep the vanilla prices and pacing |
| `weapon_requirements`, `item_tiers`, `scene_locations`, `floor_locations`, `blessing_names` | logic / tracker data; not needed by the client |



## Files

| file | |
|---|---|
| `mod.ini` | the manifest (`Id=ysorigin.archipelago`, `ApiVersion=3`, `Script=main.lua`) |
| `main.lua` | settings, the session, checks / grants / DeathLink / goal glue, state.json, test commands |
| `ap/client.lua` | the Archipelago protocol and session (reconnect, keep-alive, status line) |
| `ap/text.lua` | PrintJSON -> coloured text |
| `ap/ys.lua` | the Ys Origin rules: slot_data, detection, suppression, every receipt kind, the save block, goal, EXP |
| `ap/ui.lua` | the page (F1 > Mods > Archipelago) and the overlays |
| `data/ys_origin.json` | the location / item table: the apworld's ids, made from its data by CleriaCore's `tools/ap_import.py` |
| `icons/AP_ITEM.DDS` | the "another world's item" icon (drawn from scratch by CleriaCore's `tools/make_ap_icon.py`) |

## Testing (developers)

In a CleriaCore checkout: `tools/aplua_regress.py` (run by `tools/drive_regress.py`) plays six scenarios
against the scripted local server `tools/ap_mock_server.py` (a chest check, receiving across a save /
load, DeathLink both ways, a statue unlock + traps + the goal, the seed guard, the mod with no slot), and
`cleria-luatest` starts the mod against a fake game. Untested without a real server: a full TLS session
to archipelago.gg, a real MultiServer's packet order, release / collect floods, hints.

Without CleriaCore: `lua5.4 .dev/shop_test.lua` checks the statue shop's answers (prices, pacing, blessing items)
against a fake game.

## Credits

- The Ys Origin apworld (locations, items, logic, slot_data): 4rtefakt,
  [github.com/4rtefakt/ys-origin-archipelago](https://github.com/4rtefakt/ys-origin-archipelago).
- [Archipelago](https://archipelago.gg) and its network protocol: the Archipelago contributors.
- CleriaCore and this client: the CleriaCore project. Ys Origin is © Nihon Falcom; no game data is
  included here.
