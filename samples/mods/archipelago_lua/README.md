# Archipelago for Ys Origin (a CleriaCore mod)

Play Ys Origin in an [Archipelago](https://archipelago.gg) multiworld with
CleriaCore (the clean-room Ys Origin engine). This mod is the
Archipelago client: it connects to the room, sends your checks, receives your items, and keeps the
seed's rules in your saves. It is written in Lua on CleriaCore's mod API; nothing in CleriaCore is
Archipelago-specific.

It plays seeds of the **Ys Origin apworld** by 4rtefakt,
[github.com/4rtefakt/ys-origin-archipelago](https://github.com/4rtefakt/ys-origin-archipelago),
unchanged: generate and host the seed with that apworld, then connect with this mod.

## Requirements

- CleriaCore with mod API 3 (Lua scripts), on Windows (the connection uses Windows' own WebSocket and
  TLS; no other download).
- Your Ys Origin game data set up in CleriaCore (F1 > Prelaunch).
- A room made with the Ys Origin apworld (archipelago.gg or your own MultiServer).

## Install

1. Drop this folder (or a `.zip` of it) into CleriaCore's **mods folder**:
   `Saved Games\CleriaCore\mods` (F1 > Mods > *Open mods folder* opens it).
2. **F1 > Mods**: select **Archipelago**, open it, set **Enabled** to On, and restart CleriaCore.
3. **F1 > Mods > Archipelago**: enter the **Server** (e.g. `archipelago.gg:38281`; `ws://` or `wss://`
   may be given, otherwise the secure connection is tried first), your **Slot name** and the
   **Password** if the room has one, then **Connect**. *Connect at start* reconnects by itself next time.
4. Start a New Game with the seed's character, or load a save of that seed. A save is stamped with
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
- **Traps**: EXP Leech, Chaos Warp, Butterfingers, Blinding Fog.
- **DeathLink** (option, or on when the seed has it): your deaths are sent, theirs kill you (not during
  a story duel).
- **Goal**: Darm (or every boss, by the seed's goal) sends the goal to the server.
- **Overlays**: a connection line, an item / chat feed, a per-area tracker (each can be turned off).
- **Seed guard**: a save of one seed refuses a room of another seed; offline, a stamped save keeps its
  seed's rules from the cached slot_data.

## slot_data options

| option | |
|---|---|
| `character`, `goal`, `death_link`, `location_signals` / `location_detect`, `suppress_items`, `suppress_give_ids`, `item_index`, `skill_grants`, `sp_items`, `progressive_gear` / `progressive_skills` / `progressive_blessings`, `start_items` / `start_weapon` / `start_level`, `level_scaling` and its EXP keys (`level_margin`, `exp_base_mult`, `exp_catchup_mult`, `exp_catchup_margin`, `scene_levels`), `scene_floors`, `scene_names`, `statue_warp_locks` + `statue_unlocks` + `start_statue_scene` | supported |
| `random_start` (the start-statue warp after the intro) | **not supported** (listed on the page when the seed has it) |
| `blessing_items` (the purchase is the check, the effect becomes an item) | **not supported**: the purchase still checks, but the vanilla effect is also given |
| `blessing_costs`, `blessing_vanilla_price_map`, `blessing_shop_unlock`, `shop_hints` | **not supported** (the shop's prices and pacing stay vanilla) |
| `weapon_requirements`, `item_tiers`, `scene_locations`, `floor_locations`, `blessing_names` | logic / tracker data; not needed by the client |

For now, generate with `random_start`, `blessing_items` and shuffled blessing costs off.

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
