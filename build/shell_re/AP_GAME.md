# Archipelago in the game: CleriaCore as a client for the Ys Origin apworld

The game side of Archipelago (AP): ARCHIPELAGO.md §7 items 1-10, on top of the client library
(ARCHIPELAGO.md §1-6) and the content hooks (CONTENT.md §6). CleriaCore plays a seed of the user's
**unchanged** apworld (`C:\Users\kores\repos\YsOrigin-Archipelago`, game "Ys Origin").

None of this is retail; it is CleriaCore's own layer. With no slot set nothing opens a socket, no
grant is filtered, nothing is written (the uuid is made at the first connect) and no frame changes
(`drive_regress ap_off`, goldens). Tags: **[MOD]** the retail mod `mod/src/hook_ap.cpp` of that repo,
**[CODE]** CleriaCore code, **[H]** a choice of this design.

| part | where |
|---|---|
| Ys Origin rules (slot_data, registry, detection, suppression, grants, save block, goal, EXP) | `src/cleria/ap/ys_game.{hpp,cpp}` (`cleria-ap`), test `cleria-apgametest` |
| `[cleria.archipelago]` | `src/cleria/ap_settings.{hpp,cpp}` (`cleria`), test `cleria-settingstest` |
| the glue in the live game, the F1 tab, the overlays, state.json, drive commands | `apps/cleria-view/view_archipelago.inl` |
| the text field of the ui kit; the tab bar fitting many tabs | `apps/cleria-view/view_ui.inl` (`ui::text_field`, `Panel::draw`) |
| the foreign-item icon | `res/ap/AP_ITEM.DDS`, drawn by `tools/make_ap_icon.py` |
| a scripted local AP server | `tools/ap_mock_server.py` (stdlib only, 127.0.0.1) |
| a real default seed's slot_data | `res/ap/test_slot_data.json`, made by `tools/ap_slotdata.py` from the apworld |
| drive_regress cases | `tools/ap_regress.py` (6 cases, merged into `tools/drive_regress.py`) |

Hooks in `view_game.inl` (one line each, plus one bind block): `ap_startup` (after `content_startup`),
`ap_shell` (after the Content tab), the `ApBind` block (after the content bind block), `ap_seed_icons`
(after the book renderer), `ap_drive` (beside `speedrun_drive`), `ap_frame` (each frame, before the
event poll), `ap_tick` (each tick, after `content_tick`), and `ap_exp_factor` in `award_exp`.
`main.cpp` includes `view_archipelago.inl` after `view_video.inl`; CMake links `cleria-ap` into
`cleria-view`.

## 1. Settings: cleria.ini `[cleria.archipelago]`

| key | default | |
|---|---|---|
| `Server` | `archipelago.gg:38281` [H] | `host:port`; `ws://` / `wss://` optional (a bare address tries wss, then ws: ARCHIPELAGO.md §2) |
| `Slot`, `Password` | empty | the password is never printed, logged or shown (bullets in the tab) |
| `Uuid` | empty | made once at the first connect (`ap::make_uuid`) and kept |
| `DeathLink`, `AutoConnect` | 0 | a seed made with DeathLink turns it on too (as the mod: slot_data `death_link`) |
| `ShowStatus`, `ShowFeed`, `ShowTracker`, `ExportState` | 1, 1, 0, 1 | the overlays and `state.json` |

Files beside cleria.ini, in `archipelago/`: `datapackage/` (the item / location names by checksum),
`slot_<seed>_<slot>.json` (the slot_data, for offline play of that seed's saves) and `state.json`.

## 2. The session and the save

- One `ap::Session` lives for the program; `ap_frame` updates it once a frame (also while the F1
  panel holds the game's ticks). [not modelled] the title / launcher loops do not update it (events wait
  for the game loop).
- **Connected**: `Logic::configure(slot_data)`, the slot_data cached, `LocationScouts` for every
  active location [MOD hook_ap.cpp:1754], `ConnectUpdate` with the DeathLink tag when the seed or the
  setting wants it.
- **The save block** is the content sidecar's `[ap]` (CONTENT.md §2): `seed`, `slot`, `slot_name`,
  `applied` (received items granted into this save), `checks` (found locations, as id ranges),
  `claimed`, `goal`, `started`, `gameplay`, `ore`, `prog` (progressive skill receipts), `statues`. It is
  written through on every change, so `do_save` writes the current one; NewGame / Load (the bus) read
  it back.
- **Seed guard**: a save without a block is stamped with the room's seed and slot the first time it
  is played connected. A connect while a stamped save is loaded sets `Config::expected_seed`, so a room
  of another seed is refused before `Connect` (`auth failed: this room runs a different seed than the
  save`); loading a save of another seed / slot while connected closes the session. Either way the
  save keeps the rules of its own seed from the cached slot_data.
- **Offline**: a stamped save with a cached slot_data keeps its rules: vanilla items stay swallowed,
  checks go into the save and are re-sent after the next login (the server ignores duplicates).
- The slot's character vs the game's (g_flags[150]): a mismatch only warns (feed + log).

## 3. Detection (ARCHIPELAGO.md §5.2)

The registry is `slot_data.location_signals` (the ACTIVE locations) with `location_detect` converted
from the retail form (`offset` strings, `S_xxxx` tags: the arithmetic of `tools/ap_import.py`), falling
back to `res/ap/ys_origin.json` per id; with no slot_data, every table location.

| method | fired by |
|---|---|
| flag | the VM store that raises the cell from < 1 to >= 1 (the grant filter, at once); the per-tick sweep as a safety net. A store of 0 is not a check (§7.3.4) |
| item_arr | the sweep (`item_arr[i] >= 1`: the blessing shop's gear upgrade) |
| bit | a VM store setting the bit of g_flags[217]; the sweep (the shop's 0xAF sets the bit outside the store family) |
| scene | `RoomEnter` to that scene, and the current room when the save is adopted (the boot room) |
| floor | `RoomEnter` to a scene whose `scene_floors` entry is that floor (membership) |

Cells and bits **our own grants** write are *claimed*: the sweep does not report them (a statue
unlock sets the statue's purify flag, a progressive blessing sets a blessing bit), but the player's own
VM store still checks the location [MOD hook_ap.cpp:1174-1197]. Claims are saved.

## 4. Vanilla suppression and the treasure box

`content_add_filter(ap_filter)`, consulted before chests.ini, only while the save is adopted:
- **Store**: a rise of a `suppress_items` cell is swallowed (`-1 -> 1` and `1 -> 2`; a decrease
  passes). A chest that is not an AP location (S_2005/S_BOX03, §5.4) keeps its vanilla item: the
  running chest is known (`g_content.chests` + the room). With warp locks, a locked statue's purify
  store is swallowed too.
- **Icon** (0x116): `suppress_give_ids` swallowed; an AP chest's rising sprite becomes the scouted
  item: our own item's INVINFO icon when it has one (item cells, the ore, the artifact, the next gear
  tier, a gold piece for SP [H]), else the AP icon (`kApIcon` = 0x2A0 [H], outside every g_flags cell
  and content item id, seeded into both icon caches).
- **Window** (0xD5): the box text and icon. Own item: the original text with the name replaced
  (`content::treasure_text`), e.g. "Acquired Bronze Key.". Another world's: "Sent <item> to
  <player>." with the item coloured by class (progression plum, useful slate blue, trap salmon, other
  cyan). Not scouted (offline): "Found an Archipelago item.". A non-chest event box is relabelled when
  its script's own store just found a location (10 s window [H]).
- The icon: `res/ap/AP_ITEM.DDS` (64 x 64 A8R8G8B8), CleriaCore's own drawing: a gold-rimmed night-blue
  disc, five islands in the AP text colours linked to a pale centre. A mod may replace it at
  `IMAGE/AP_ITEM.DDS` (vfs). The overlays draw the same geometry live (`ap_draw_logo`).

## 5. Receiving

Each tick, while connected, `items_in_sync()`, the save adopted and the world **not frozen** (no
cutscene, no open box): `received()[applied..]` are granted in order and `applied` advances (saved).
A reload of an older save re-grants exactly what that save never had; nothing is granted twice
(`ap_items_save`). Per kind (`Logic::grant`, ARCHIPELAGO.md §5.2):

| kind | grant |
|---|---|
| item | key items set to 1 (never 2: the scripts test `==` [MOD 940-950]); 0x57/0x58/0x59 and filler outside 0x40..0x76 add; the companion power / bracelet cell too |
| sp | the live wallet `player.sp` |
| cleria_ore | the ore count -> weapon tier {1,2,4,6,8}: g_flags[148] and the four weapon slots (`InvState::tier`, the 0x7f SetWeaponLevel twin); re-enforced each tick |
| progressive_gear | the first tier of the slot's ladder (else the table's, by the played character) not owned |
| progressive_skill | 1st: artifact + power; later: the level cell + 1, max 3. The count is in the save (§7.3.2 fixed) |
| gem | the level cell + 1, max 3, also with progressive skills off (§7.3.1 fixed) |
| blessing / progressive_blessing | the bit / the next unset bit of the chain in g_flags[217] |
| statue_unlock | the warp registry byte + the purify flag (claimed) |
| trap | EXP Leech: -1 level. Chaos Warp: to a random registered statue (0xa6 GoWarpPoint). Butterfingers: weapon tier 0 for 8 s. Blinding Fog: a haze for 30 s (the `0ap.fog` layer, under every overlay). [H] a trap already in the list at login is skipped (it happened while away), like the mod's replay rule |
| unknown id | skipped and logged; the index still advances (an unknowable item cannot block the rest) |

Then the stat recompute (`player.max_hp` from `compute_stats`). Item cells also emit `ItemObtained`
(source `ap`) on the event bus.

## 6. Goal, DeathLink, EXP, slot_data options

- **Goal**: (g_flags[150] 1/2 and g_flags[226] >= 1) or (150 == 3 and 227 >= 2) or scene 7002 after
  gameplay was seen; with `goal == 1` also 220..225 >= 1 [MOD 476-519] -> `StatusUpdate(Goal)`, once
  per save (re-sent by the library after each login).
- **DeathLink**: our death (the `PlayerDeath` bus event; a story duel's g_flags[133] path never emits
  it) sends a Bounce with "<slot> ran out of HP", debounced 6 s [MOD 1906]. A received one kills on the
  next tick that is in play, not frozen and not in a duel [H], and is not sent back.
- **EXP**: `level_scaling` 2/3 multiply each kill's EXP by `exp_base_mult`, or `exp_catchup_mult` while
  the level is at most the deepest visited scene's `scene_levels` + `exp_catchup_margin` [MOD 2537-2560]
  (on top of the Cheats page's EXP rate). 1/3 raise the level to the room's expected level - margin.

| slot_data option | state |
|---|---|
| character, goal, death_link, location_signals / location_detect, suppress_items, suppress_give_ids, item_index (via the table), skill_grants (via the table), sp_items, progressive_gear / skills / blessings, start_items / start_weapon / start_level, level_scaling and the EXP keys, scene_levels, scene_floors, scene_names, statue_warp_locks + statue_unlocks + start_statue_scene | supported |
| random_start (the start-statue warp after the intro) | **not supported**; listed in the tab when the seed has it |
| blessing_items (the effect is suppressed at the 0xAF purchase, the purchase is the check) | **not supported**: needs a grant-filter query at 0xAF in `xso.cpp`; the purchase still checks by its bit, but the vanilla effect is also given |
| blessing_costs / blessing_vanilla_price_map / blessing_shop_unlock / shop_hints | **not supported** (the shop's prices and pacing) |
| weapon_requirements, item_tiers, scene_locations / floor_locations / blessing_names | logic / overlay data only; not needed |

## 7. UI

- **`ui::text_field`** (the kit): OK / click edits; typed text through SDL text input (any layout:
  `PlatformImeData.WantTextInput` makes the backend call `SDL_StartTextInput`), Ctrl+V pastes,
  Backspace / Ctrl+Backspace, Enter or a click elsewhere keeps, Esc restores. While editing
  `read_nav()` yields nothing, so W/A/S/D/Q/E/Space/Tab type. `secret` draws a bullet per character.
- **The tab bar**: with many tabs the gaps narrow, then the type, so the row stays clear of the close
  cross (unchanged with the tabs that fit).
- **F1 "Archipelago"**: status, Server, Slot name, Password, Connect / Disconnect, Connect at start,
  DeathLink; This game: seed, items received (applied / server), locations checked, goal, unsupported
  options; Overlays: connection line, item feed, tracker, tracker file; recent events.
- **Shell::hud**: `ap.status` (top left: a dot and the status / mismatch, checks found), `ap.feed` (top
  right: the last 6 PrintJSON lines with their colours and the logo, 10 s [H]), `ap.tracker` (left:
  checked / total per zone, in tower order), `0ap.fog` (the trap).
- **state.json** (`ExportState`, at most once a second when something changed): `format`, `game`,
  `status`, `connected`, `seed`, `slot`, `slot_name`, `room`, `scene`, `goal`, `death_link`, `applied`,
  `received[{index,item,name,from,flags}]`, `checked[]`, `missing[]`.
- **Speedrun**: an AP session (or an AP save) is listed as a modification ("Archipelago session").

## 8. Verification

```
cmake --build build_diff --config Release --target cleria-view cleria-apgametest cleria-aptest cleria-settingstest
ctest --test-dir build_diff -C Release -R "apgametest|aptest|jsontest|settingstest"
python tools/drive_regress.py --exe build_diff/Release/cleria-view.exe --assets <assets> ap_chest ap_items_save ap_deathlink ap_goal_traps ap_seed_guard ap_off
python tools/ap_slotdata.py --check     # the fixture against the apworld (read-only)
```

- `cleria-apgametest` (78 checks) on the real default seed's slot_data: options, the retail detect
  forms, the registry, suppression, store / sweep / bit / item_arr / scene / floor detection, every
  receipt kind (keys never 2, stacking, ore tiers, ladders, skills capped, blessing chains, claims),
  the loadout, warp locks, the save block round trip, the goal (incl. Toal's and all-bosses), EXP.
- The drive cases run `tools/ap_mock_server.py` (a two-player room: "Clicker" in game "Clique" and the
  slot): **ap_chest** (the S_1001 chest: Panacea swallowed, check 5857605 sent, box "Sent Button
  Activation to Clicker."), **ap_items_save** (receive, save, receive, load: no double grant),
  **ap_deathlink** (in and out), **ap_goal_traps** (statue unlock, fog, chaos warp to S_1000, goal),
  **ap_seed_guard** (a reseeded room is refused; offline rules from the cache), **ap_off** (no slot:
  retail chest, no AP line).

## 9. Playing a real multiworld

1. Generate with the apworld as usual (the YAML's `Ys Origin` options; avoid `random_start`,
   `blessing_items` and shuffled blessing costs for now). Host it (archipelago.gg or a local
   MultiServer).
2. In CleriaCore: F1 -> Archipelago: Server (`archipelago.gg:<port>`), Slot name, Password -> Connect.
   Start a New Game with the seed's character (or load that seed's save); the save is stamped.
3. Save at statues as usual; the AP state rides in `yso_NN.cleria` beside the save.

Untested without a real server: TLS to archipelago.gg in a full session (the library's handshake test
reached it), a real MultiServer's packet order / PrintJSON varieties, a release / collect flood, hints,
other games' item names, the real generator's slot_data for non-default options (the fixture is the
default Yunica seed), warp locks against the real statue scripts, and the title-screen Load path with AP
(drive runs skip the title).
