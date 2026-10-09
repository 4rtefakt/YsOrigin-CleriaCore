# Archipelago for Ys Origin (a CleriaCore mod)

> **Status: 0.2.0, early.** Tested end to end against a scripted local Archipelago server, not yet
> against a real multiworld: please report anything odd. Made for **Ys Origin apworld 2.0.1 or newer**. **CleriaCore is not public yet**, so this
> mod can't be played until it is.
>
> **Download:** grab `ysorigin.archipelago-<version>.cleriamod` from the
> [Releases](https://github.com/4rtefakt/ys-origin-cleriacore-ap/releases) page and double-click it
> (see *Install*). The `.zip` there is the same mod for dropping into the mods folder by hand.

Play Ys Origin in an [Archipelago](https://archipelago.gg) multiworld with
CleriaCore (the clean-room Ys Origin engine). This mod is the
Archipelago client: it connects to the room, sends your checks, receives your items, and keeps the
seed's rules in your saves. It is written in Lua on CleriaCore's mod API; nothing in CleriaCore is
Archipelago-specific.

It plays seeds of the **Ys Origin apworld** by 4rtefakt,
[github.com/4rtefakt/ys-origin-archipelago](https://github.com/4rtefakt/ys-origin-archipelago),
unchanged: generate and host the seed with that apworld (2.0.1 or newer: its item and location table
is the one in `data/ys_origin.json`), then connect with this mod.

## Requirements

- CleriaCore with mod API 3 (Lua scripts), on Windows (the connection uses Windows' own WebSocket and
  TLS; no other download).
- Your Ys Origin game data set up in CleriaCore (F1 > Prelaunch).
- A room made with the Ys Origin apworld (archipelago.gg or your own MultiServer).

## Install

1. **Double-click `ysorigin.archipelago-<version>.cleriamod`.** CleriaCore installs the mod and turns it on
   (an older version is replaced); if CleriaCore was already running, restart it. Or, in CleriaCore:
   F1 > Mods > *Install a mod file...*.
   - Without the `.cleriamod`: drop this folder (or a `.zip` of it) into CleriaCore's **mods folder**,
     `Saved Games\CleriaCore\mods` (F1 > Mods > *Open mods folder* opens it), then **F1 > Mods**: select
     **Archipelago**, open it, set **Enabled** to On, and restart CleriaCore.
   - The `.cleriamod` is made from this folder by CleriaCore's `tools/pack_mod.py samples/mods/archipelago_lua`.
2. **F1 > Mods > Archipelago**: enter the **Server** (e.g. `archipelago.gg:38281`; `ws://` or `wss://`
   may be given, otherwise the secure connection is tried first), your **Slot name** and the
   **Password** if the room has one, then **Connect**. *Connect at start* reconnects by itself next time.
3. Start a New Game with the seed's character, or load a save of that seed. A save is stamped with
   the seed the first time it is played connected; save at the goddess statues as usual. The
   Archipelago state rides in the `yso_NN.cleria` file beside each save (the save itself stays a
   retail save).

The settings live in `cleria.ini`, section `[cleria.mod.ysorigin.archipelago]` (the password in
plain text in your own settings folder, never shown or logged). The item-name cache, the seed's
cached slot_data and `state.json` (for external trackers) live in `modsdata/ysorigin.archipelago/`
beside `cleria.ini`.

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
- **Story repairs** (Yunica): the post-Kishgal "I'm just a burden" state on 1F and a stuck Dreaming Idol
  chain are detected and put right, as in the retail mod.
- **Withheld items**: when a story scene "gives" an item the seed moved elsewhere (the Zelkarons charging
  the Evil Ring), the feed says the game's own copy was withheld.
- **Invariants**: a key item never counts above 1 (the scripts test for exactly 1) and a skill level
  never goes above 3. A weapon tier the seed did not grant is taken back (the 4F Roo's reward raises the
  weapon with no item behind it).
- **SP chests**: the five chests that pay SP do it with a script command, not an item, so the vanilla SP
  (2,000 to 20,000) used to come on top of the seed's item. It is taken back when the chest is a location.
- **Traps**: EXP Leech, Chaos Warp, Butterfingers, Blinding Fog.
- **DeathLink** (option, or on when the seed has it): your deaths are sent, theirs kill you (not during
  a story duel).
- **Goal**: Darm (or every boss, by the seed's goal) sends the goal to the server.
- **Overlays**: a connection line, an item / chat feed, a per-area tracker (each can be turned off).
- **Seed guard**: a save of one seed refuses a room of another seed; offline, a stamped save keeps its
  seed's rules from the cached slot_data.

## Quality of life (all optional: F1 > Mods > Archipelago)

| option | default | |
|---|---|---|
| **Left in this room** | off | An overlay, bottom left: the current room's locations still to find. A hinted one shows its item. |
| ...with what they hold | off | The same list with the item at each location (a spoiler). |
| **Tracker detail** | Areas | The tracker overlay counts per area of the tower, or per floor. |
| **Boss checks on defeat** | off | A boss room's check is sent when the fight is won, not when you walk in (the floor bosses and the duels; the 17F and 20F rooms stay on entry). |
| **Progression notices** | on | A notice card when a progression item arrives. |
| **Hint alerts** | on | A feed line when you enter a room that holds a hinted item. |
| **EXP multiplier** | 0 | Your own multiplier for every kill; 0 keeps the seed's. |

The page also has: your **hints** (the server's list, kept up to date) with a field to ask for one and your
hint points; a **chat / command** line with Send, and **Release** / **Collect** buttons once the goal is
complete; and under *This game*, whether you hold the three **elemental skills** the final fight needs, the
**floor bosses** beaten when the seed's goal asks for them, and how many locations are left in the room.

## slot_data options

| option | |
|---|---|
| `character`, `goal`, `death_link`, `location_signals` / `location_detect`, `suppress_items`, `suppress_give_ids`, `item_index`, `skill_grants`, `sp_items`, `progressive_gear` / `progressive_skills` / `progressive_blessings`, `start_items` / `start_weapon` / `start_level`, `level_scaling` and its EXP keys (`level_margin`, `exp_base_mult`, `exp_catchup_mult`, `exp_catchup_margin`, `scene_levels`), `scene_floors`, `scene_names`, `statue_warp_locks` + `statue_unlocks` + `start_statue_scene`, `random_start`, `roo_flags` | supported |
| `blessing_items` (the purchase is the check, the effect becomes an item) | **not supported**: the purchase still checks, but the vanilla effect is also given |
| `blessing_costs`, `blessing_vanilla_price_map`, `blessing_shop_unlock`, `shop_hints` | **not supported** (the shop's prices and pacing stay vanilla) |
| `weapon_requirements`, `item_tiers`, `scene_locations`, `floor_locations`, `blessing_names` | logic / tracker data; not needed by the client |

For now, generate with `blessing_items` and shuffled blessing costs off.

Not here yet: the retail mod's autosave after a check or a received item (CleriaCore's mod API has
no "save now" call yet).

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

## Credits

- The Ys Origin apworld (locations, items, logic, slot_data): 4rtefakt,
  [github.com/4rtefakt/ys-origin-archipelago](https://github.com/4rtefakt/ys-origin-archipelago).
- [Archipelago](https://archipelago.gg) and its network protocol: the Archipelago contributors.
- CleriaCore and this client: the CleriaCore project. Ys Origin is © Nihon Falcom; no game data is
  included here.
