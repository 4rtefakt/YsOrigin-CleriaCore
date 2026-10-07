# Archipelago: the client library and the Ys Origin apworld mapping

> **Superseded (2026-10-07): the C++ client library was removed** with the built-in client (AP_GAME.md). The
> Archipelago client is the Lua mod **`samples/mods/archipelago_lua`** (LUA_API.md §9): `ap/client.lua` is the
> protocol state machine and session of §3-4, `ap/text.lua` the PrintJSON model of §3.5, `ap/ys.lua` the Ys
> Origin rules. Still current: **§5, the apworld contract** (the id spaces, the location / item mapping and its
> `detect` methods, the slot_data keys), which the mod implements, and the retail-mod bugs not to copy (§7.3).
> Kept in C++ for the Lua API: `src/cleria/json` (§1, `cleria.json`) and the WebSocket of §2, moved to
> `src/cleria/net/websocket` (`cleria.net.websocket`; test `cleria-nettest`, the loopback part of the former
> `cleria-aptest`). The table is now written by `tools/ap_import.py` to
> `samples/mods/archipelago_lua/data/ys_origin.json` (was `res/ap/ys_origin.json`). File names below are
> historical.

CleriaCore's Archipelago (AP) support: phase 2b of MODS.md §8, tasks §12.1 (json), §12.8 (transport),
§12.9 (protocol client) and the data side of §12.10. This part is a **library only**: nothing in
the game calls it yet. A later agent wires it to chests, items, the HUD and the F1 shell (§7).

The game stays a faithful port. AP is CleriaCore's own layer, and none of it RE's retail behaviour.
The only retail facts are the addresses and flag meanings taken over from the retail mod and from
CleriaCore's own RE (§5, §6), and each one is cited.

Evidence tags: **[DOC]** = the official AP protocol document. **[REF]** = the reference
CommonClient.py / NetUtils.py. **[MOD]** = the retail Ys Origin mod and apworld in
`C:\Users\kores\repos\YsOrigin-Archipelago` (read-only). **[CODE]** = CleriaCore code.
**[H]** = a choice made by this design.

| part | files | state |
|---|---|---|
| JSON value / parser / writer | `src/cleria/json.{hpp,cpp}` (in `cleria`) | built, `cleria-jsontest` |
| WebSocket over WinHTTP | `src/cleria/ap/websocket.{hpp,cpp}` | built, loopback-tested |
| protocol state machine | `src/cleria/ap/client.{hpp,cpp}` | built, tested on recorded exchanges |
| PrintJSON -> text model | `src/cleria/ap/text.{hpp,cpp}` | built |
| session: reconnect, keep-alive, status line | `src/cleria/ap/session.{hpp,cpp}` | built, loopback-tested |
| Ys Origin location / item table | `tools/ap_import.py` -> `res/ap/ys_origin.json`, loader `src/cleria/ap/ys_data.{hpp,cpp}` | built |
| game glue, UI, sidecar save | `src/cleria/ap/ys_game`, `apps/cleria-view/view_archipelago.inl` | built: **AP_GAME.md** |

Build: `cleria-ap` is a static library linking `cleria` and `winhttp`; `cleria-view` links it (AP_GAME.md).

---

## 1. JSON (`cleria::json`)

The AP protocol is JSON. No third-party library is vendored; this one is about 600 lines.

- **Values**: null, bool, Int (int64), Double, String (UTF-8), Array, Object.
  - Objects are a `std::map`, so keys come out sorted and the writer's output is deterministic.
  - A duplicate key keeps the last value, as Python's json does.
- **Numbers**:
  - An integer literal that fits int64 is an Int; anything else is a Double.
  - AP ids go up to ±2^53-1 [DOC], and they stay exact.
  - `as_int()` also accepts an integral double. The server sends unix times as floats.
- **Strings**:
  - `\u` escapes are decoded, including surrogate pairs.
  - Raw bytes are validated as UTF-8. Overlong forms, surrogates and code points above U+10FFFF are refused.
  - A lone surrogate escape becomes U+FFFD instead of an error [H]: one odd player name must not drop the connection.
- **Limits**: nesting depth defaults to 256 and input size to 64 MB. An error carries the line,
  column and byte offset, e.g. `line 3, column 7: expected ':' after an object key, got '2'`.
- **Options**:
  - `allow_nan` accepts `NaN` / `Infinity`. The AP server is Python, and its `json` emits them; the Client turns this on.
  - A UTF-8 BOM is always skipped.
  - The writer can write compact, pretty or ASCII-only output (`\u` escapes with surrogate pairs).
- **API gotcha**: on a **non-const** `Value`, `v["key"]` is the builder. It inserts the key and turns a
  non-object into an object. Read through a `const Value&`. Indexing, `v[0]`, is always a read.

## 2. Transport (`ap::WebSocket`)

- **WinHTTP**, part of Windows. TLS goes through SChannel and uses the OS certificate store.
- **Threads**: one worker thread per connection does the handshake, then blocks in
  `WinHttpWebSocketReceive`. A second thread drains the send queue, because WinHTTP allows one
  pending send and one pending receive at a time.
- **Game-thread calls**: `open`, `send`, `close` and `poll` never wait on the network.
  - `close()` sends a close frame. If the server does not answer within 2 s, `poll()` aborts the handles.
  - The destructor waits at most about 2.5 s.
- **Addresses** (`candidate_urls`):
  - `ws://`, `wss://`, and `archipelago://` (treated as `ws://`, as in [REF] `server_loop`) are accepted.
  - A **bare `host[:port]` tries `wss://` first, then `ws://`**. That is apclientpp's order, and the
    retail mod's F8 menu also prefixes `wss://` [MOD menu.cpp:93-101].
  - The reference CommonClient does the reverse: `ws://` first, and `wss://` when the reply is not HTTP [REF CommonClient.py `server_loop`].
  - Either way, the first scheme that completes the handshake wins.
  - The default port is 38281 [REF]. IPv6 literals `[::1]:p` work.
  - URL credentials `user:pass@host` are refused [H]; the slot and password come from the UI.
- **Timeouts**: resolve 10 s, connect 10 s, send 15 s, handshake 15 s. After the upgrade, the
  receive timeout is set to infinite, because an idle room is normal. The tests check this with a
  0.8 s handshake timeout followed by 1.5 s idle.
- **Limits**: one message may be up to 16 MB, matching the reference client's `max_size`.
  Fragments are reassembled; the tests send a 2 MB message.
- **Events**: `Opened(url)`, `Message(text)`, `Closed(reason, error)`. Example reasons:
  - "could not connect: wss://h:p/: TLS failure ...; ws://h:p/: cannot connect ..."
  - "connection lost: connection reset"
  - "server closed the connection (1000)"
- **Not supported: permessage-deflate.** WinHTTP has no WebSocket extensions. [DOC] says clients
  "should support per-message compression" and calls uncompressed connections deprecated; servers
  still accept them. If AP ever requires compression, replace the transport with our own framing
  over winsock plus SChannel, using miniz for raw deflate. `Client` does not change.
- **Proxy**: `WINHTTP_ACCESS_TYPE_DEFAULT_PROXY` [H]. This is direct unless `netsh winhttp` sets a
  proxy, and it avoids slow WPAD discovery.

## 3. Protocol (`ap::Client`), a pure state machine

`Client` takes the server's text frames (`on_open`, `on_message`, `on_close`). It hands back the
frames to send (`take_outgoing`) and events (`take_events`). It never touches a socket or a clock,
so tests feed it recorded exchanges.

### 3.1 Flow [DOC "Archipelago Connection Handshake"]

`open` -> **RoomInfo**
-> **GetDataPackage** for the games whose names are missing or stale (see below)
-> **DataPackage**
-> **Connect**
-> **Connected** or **ConnectionRefused**.

The data-package step follows [REF] `prepare_data_package`:
- The relevant games are every game in the room plus "Archipelago".
- A game is fetched only if the server lists a checksum for it, and that checksum differs from what
  is in memory or in the `DataPackageStore`.
- `FileDataPackageStore(dir)` caches each game as `<game>_<fnv>_<checksum>.json`. After the first
  connection, logins skip the download; the loopback test shows no second GetDataPackage after a
  reconnect.

**Connect** fields:

| field | value |
|---|---|
| `game` | `"Ys Origin"` |
| `name` | the slot name |
| `password` | as entered |
| `uuid` | `make_uuid()`, generated once and persisted by the caller |
| `version` | `{0,6,4,"class":"Version"}`, what the retail mod sends [MOD apclient.hpp:14] |
| `items_handling` | `0b111` (other worlds, own world, starting inventory), as the retail mod and the legacy client [MOD] |
| `tags` | `["DeathLink"]` when DeathLink is on |
| `slot_data` | `true` |

`Config::expected_seed` (the seed stamped in a save) refuses a room with another `seed_name` before
anything is sent: state Refused, event `SeedMismatch`.

### 3.2 Items: index handling [DOC "Synchronizing Items"]

`received()` is the slot's whole received list, aligned with the server's `ReceivedItems.index`.

| packet `index` | action |
|---|---|
| `0` | replaces the list (every login, and the answer to Sync); event `ItemsReset` |
| `== size()` | appended; event `ItemsReceived{index, new items}` |
| `< size()` | overlap: the repeated part is overwritten, and only the new tail is reported |
| `> size()` (a gap) | **not stored**; `Sync` + `LocationChecks(all local)` are sent and `items_in_sync()` goes false until the index-0 answer arrives [H] |

On the gap row: the doc allows accepting such items, but keeping the list gap-free means an index is
always a position.

**The game's contract**: keep `applied` (the count of items already granted) in the save sidecar.
Each frame, while `items_in_sync()`, grant `received()[applied..]` and advance `applied`. A login
re-sends the whole list from index 0, so nothing is granted twice. This is the protocol's intended
design, and it is what the retail mod's `g_replay_through` emulates [MOD hook_ap.cpp:783-826].

### 3.3 Checks, scouts, goal: kept across reconnects

- `check_locations(ids)` remembers every id for the session. It sends only ids it has not reported
  before, and **re-sends every local check after each Connected** (the server ignores duplicates [DOC]).
  The game can therefore report checks while offline.
- `scout_locations` is remembered too; anything not yet answered is re-asked after login.
  `LocationInfo` fills `scouted()`.
- `set_status(Goal)` is final and is re-sent after every login.
- `RoomUpdate.checked_locations` moves ids from missing to checked, with event `LocationsChecked`.
  This covers coop partners in the same slot [DOC Coop].
- Chat (`say`), `get`, `set` and `set_notify` are held while offline and sent after login.
  A death is not held: a stale death is dropped.

### 3.4 DeathLink [DOC "DeathLink"]

- **Sending**: `send_death(unix_time, cause)` sends
  `Bounce{tags:["DeathLink"], data:{time, source: own alias, cause}}`. It only does so with the tag
  set and while connected.
- **Receiving**: a `Bounced` whose tags include DeathLink becomes event `DeathLink{source, cause, time}`.
  - Our own echo, identified by the same `time` within 1 ms, is ignored, as in [REF].
  - The event is also dropped once the tag has been turned off.
- **Toggling**: `set_tags()` sends `ConnectUpdate` when connected.
- **Keep-alive**: `Session` sends an empty `Bounce{slots:[own]}` after 100 s without traffic
  [REF `keep_alive`]. Its echo is silent.

### 3.5 PrintJSON -> text model (`ap::Line` / `ap::Span`)

`render_print_json(packet, names)` gives a `Line{type, spans}`. Each span has text, a kind
(Text / Player / Item / Location / Entrance / HintStatus), an RGB colour or "default", an optional
background, bold, underline, and the id / player / flags.

Colours follow [REF NetUtils `JSONtoTextParser`]:

| part | colour |
|---|---|
| own player (or own group) | magenta EE00EE |
| other players, `player_name` | yellow FAFAD2 |
| item: progression | plum AF99EF |
| item: useful | slate blue 6D8BE8 |
| item: trap | salmon FA8072 |
| item: other | cyan 00EEEE |
| location | green 00FF7F |
| entrance | blue 6495ED |
| hint status | found green, unspecified white, no priority slate blue, avoid salmon, priority plum |

- `color` parts may chain several codes, e.g. `"bold;red_bg;green"`.
- Unknown part types render as plain text, as [DOC] asks.
- Ids are resolved in the slot's game (`slot_info`), falling back to the "Archipelago" game.
  Unresolved ids read `Unknown item (ID: n)`, as [REF] does.
- `Line::plain()` gives the uncoloured string.

### 3.6 Also handled

- `Retrieved`, `SetReply` and `RoomUpdate` are passed through as events with the packet.
- `InvalidPacket` and bad frames (invalid JSON, unknown commands) become events and never throw.
- A frame may hold several commands. Both the object form and the NamedTuple list form of
  `NetworkPlayer` / `NetworkSlot` are read.

## 4. Session (`ap::Session`)

The game owns one `Session` and calls `update(now_s)` once a frame on the main thread.

- **`connect(address, Config)`** starts a fresh Client (no items, no checks) and opens the socket.
- **Reconnects** happen automatically after an unexpected close. Backoff: 2, 4, 8, 16, 30, 30 ... s
  [H]. For comparison, apclientpp uses 1.5 to 15 s, and CommonClient 5 s doubling with no cap.
- **No reconnect** after a refusal or for an unparsable address. After a refusal,
  `retry_login(Config)` sends a new Connect on the same socket.
- **`status_text(now)`** gives the one-line status for the UI. Each line was seen in the loopback test:

| status | text |
|---|---|
| Idle | `not connected` |
| Connecting | `connecting to wss://127.0.0.1:57958/` |
| LoggingIn | `logging in to <url>`, or `downloading item names from <url>` |
| Connected | `connected as Hugo (slot 2)` |
| Refused | `auth failed: no slot with that name in this room` (also wrong password, wrong game, version, seed mismatch) |
| Disconnected | `disconnected: connection lost: connection reset (retrying in 1 s)` |

## 5. The apworld contract (`YsOrigin-Archipelago/ys_origin`, game "Ys Origin")

CleriaCore is a client for the **unchanged** apworld.

### 5.1 Ids [MOD data_tables.py:33-34, 120-123, 510-515]

- **Location ids**: `0x596000 + i` over the **sorted names** of all 336 locations in `data/locations.json`.
- **Item ids**: `0x595000 + i` over the sorted item universe, 147 names.
  - The universe is the location variants, filler, traps, Devil Medallion, the 21 statue unlocks,
    Progressive Armor/Boots, the progressive skills and the progressive blessings.
  - Gold (1G..5000G) is **not** in the universe.
- Some names contain mojibake from the source data, e.g. `"... Outer Corridor 3 â€” Mantid Medallion"`.
  They are kept byte-exact; ids are what matter.
- `tools/ap_import.py` does **not** copy ids by hand. It loads the apworld's own `data_tables.py`
  (pure Python, no Archipelago import), with bytecode writing off so the read-only repo is untouched.

### 5.2 `res/ap/ys_origin.json` (format 1)

The source commit and the sha256 of each input file are recorded in the table.

```
python tools/ap_import.py            # regenerate (default apworld path C:/Users/kores/repos/YsOrigin-Archipelago)
python tools/ap_import.py --check    # exit 1 if the committed table is stale
```

Every **location** has these fields:
- `id`, `name`, `key` (the locations.json id, e.g. `S_1001/S_BOX01`)
- `type`: chest / event / statue / blessing / boss / floor / room
- `zone`, `floor`, `room`, `scene` (S_ leaf number)
- `excluded`: the apworld keeps these filler-only
- `vanilla[]`: `{item: INVINFO / g_flags index, name, class}`
- `chars`: when the location exists for only some characters
- `statue_scene`: statues only
- `chest`: `{box_flag, script}` for the 61 chests

Each location also has a `detect`. Retail addresses are module-relative; VA = 0x400000 + offset:

| `detect.method` | count | meaning in CleriaCore | from (retail) |
|---|---|---|---|
| `flag` `{flag}` | 95 | `XsoVm::g_flags[flag] >= 1` (New Game sets item cells to -1, so -1 and 0 = not checked) | offset `0x36B91C + 4*flag` [MOD hook_ap.cpp:203-204, 1465, 2045-2081] |
| `item_arr` `{index}` | 36 | `XsoVm::item_arr[index] >= 1` (mirrored in `InvState::raval`), the "Divine Blessing: Strengthen <gear>" upgrades | offsets 0x36A654.. = the "Raval" u32[54] [MOD hook_ap.cpp:308-312; build/menu_re/BLESSING_SHOP.md:165-177] |
| `scene` `{scene}` | 160 | fires once when the current scene **changes to** `scene` (`g_flags[505]`, written in `room_entry_flags`). Room checks, boss arenas (entry, not the kill), 8 events, and chest S_1014/S_BOX01 (no box flag) | [MOD hook_ap.cpp:1521-1528, 1939-2010] |
| `bit` `{flag:217, bit}` | 24 | `(g_flags[0xD9] >> bit) & 1`, the blessing bitfield | offset 0x36BC80 [MOD 1488-1509] |
| `floor` `{floor}` | 21 | the tower floor `floor` has been seen, from `slot_data.scene_floors[current scene]` (membership, not `>=`) | [MOD 2208-2240] |

**Items** carry `id`, `name`, `class` (the apworld default; `slot_data` / item flags may promote it),
`kind`, the kind's fields, and `chars` (character-restricted gear). What receiving each kind does,
as the retail mod does it [MOD hook_ap.cpp:1757-1898]:

| `kind` (count) | fields | grant |
|---|---|---|
| `item` (79) | `flag`, `companion_flag`? | `g_flags[flag]`: set to 1 if not stacking (from -1 or 0; no-op at 1). Stacking: `max(cur,0)+1`. Only 0x57 Roda Fruit, 0x58 and 0x59 Panacea stack in 0x40..0x76; outside that band, filler stacks [MOD 925-973]. Then the stat recompute. `companion_flag` (bracelet power 116..118, Gold Bracelet 0xA3, Silver Bracelet 0xB5, Evil Ring 0x5E) is granted too [MOD 1855-1878] |
| `sp` (3) | `amount` | add to the **live SP wallet** (`player.sp`, retail 0x76A75C), **not** g_flags[0xD8], which is the zenny shadow [MOD 351-355, 1784-1790; BLESSING_SHOP.md:24-28]. Skip on replay |
| `trap` (4) | — | EXP Leech: lose a level (FUN_004200F0). Chaos Warp: warp to a random unlocked statue. Butterfingers: weapon forced to tier 0 for 8 s. Blinding Fog: screen haze for 30 s. Skip on replay [MOD 658-673, 2686-2726] |
| `statue_unlock` (21) | `scene`, `flag` | (statue_warp_locks) unlock that statue: its warp-registry byte (CleriaCore `warp_on[]`, retail 0x76C11C) and `g_flags[flag] = 1`, re-enforced each tick [MOD 1804-1813, 2522-2530] |
| `progressive_gear` (2) | `ladder{char:[flags]}` | the first tier in the character's ladder whose cell is `< 1` |
| `progressive_skill` (3) | `artifact`, `power`, `level_cell` | first receipt: artifact + power cells = 1. Later receipts: `level_cell` + 1, capped at 3 [MOD 1199-1220] (**not replay-safe in the mod**: §7.3) |
| `progressive_blessing` (7) | `bits[]` | set the next unset bit of `bits` in g_flags[0xD9] [MOD 1159-1172] |
| `cleria_ore` (1) | `flag` 0x58 | **not** the item cell. The Nth ore sets the weapon tier g_flags[0x94] = {1,2,4,6,8}[min(N,5)-1] plus the stat refresh, and is re-enforced after load [MOD 629-641, 2571-2590, 2714-2758]. CleriaCore: build/equip_re/EQUIP_EFFECTS.md:98-111 |
| `gem` (3) | `index` 0x300.., `level_cell` 0xB6..0xB8 | `level_cell` + 1, capped at 3. **The retail mod drops these when progressive_skills is off** (§7.3) |
| `blessing` (24) | `bit` | set `bit` of g_flags[0xD9] (index − 0x200) |

### 5.3 `slot_data` (what `Connected` brings) [MOD ys_origin/__init__.py `fill_slot_data`]

The game glue reads these from `client.slot_data()`. The table above is the offline mirror.

| key | meaning |
|---|---|
| `character` | 0 yunica / 1 hugo / 2 toal (g_flags[150] uses 1/2/3) |
| `goal` | 0 defeat Darm, 1 all bosses |
| `death_link` | DeathLink on for this slot |
| `location_signals` | `{name: id}` of the ACTIVE locations. Register only these |
| `location_detect` | `{name: detect}` for the active locations, in the **retail** form (`offset` strings). With statue_warp_locks, statues come as `{"method":"scene"}`. Convert with the §5.2 rules: same arithmetic as ap_import.py |
| `item_index` | `{name: g_flags index}` |
| `skill_grants` | companion cells |
| `sp_items` / `sp_flag_idx` | SP amounts (ignore `sp_flag_idx`; see the `sp` row in §5.2) |
| `progressive_gear` / `progressive_skills` / `progressive_blessings` | as §5.2, for the chosen character / options |
| `statue_warp_locks`, `statue_unlocks` `{name:{scene,flag}}`, `start_statue_scene`, `random_start`, `statue_scenes` | the warp-lock mode |
| `start_items` (g_flags indices), `start_level`, `start_weapon` | New Game minimums |
| `level_scaling`, `level_margin`, `exp_base_mult`, `exp_catchup_mult`, `exp_catchup_margin`, `scene_levels`, `scene_floors`, `weapon_requirements` | catch-up scaling |
| `suppress_items` | g_flags indices: vanilla grants to swallow |
| `suppress_give_ids` | give-item ids (gems 0x80..0x82, 0x5E) to swallow at 0x116 only. **0x82 is also boss flag 130**: never suppress it as a g_flags cell |
| `blessing_items`, `blessing_costs`, `blessing_shop_unlock`, `blessing_vanilla_price_map`, `blessing_names`, `shop_hints` | the blessing shop |
| `scene_names`, `scene_locations`, `floor_locations`, `item_tiers` | tracker / overlay data |

### 5.4 Not AP locations

`S_2005/S_BOX03` (Water Dragon's Scales, box flag 0x14B) is in chests.json but has no location. Its
chest keeps its vanilla item; the grant filter must pass it through.

## 6. Where it lands in CleriaCore (sites for the wiring agent) [CODE]

| need | CleriaCore site |
|---|---|
| flag cells | `XsoVm::g_flags[0x200]` (`src/cleria/xso.hpp`); the live VM is `game` in `view_game.inl` |
| gear-upgrade cells | `XsoVm::item_arr[54]` / `InvState::raval[54]` |
| every VM flag store (the grant filter's hook point) | the store switch in `xso.cpp` (`case 0x64 Flag_SetInt` ... `0x77`); each write is recorded as `XsoVm::Grant`. Retail twin: the mod's hooks at 0x567D17 (0x64) / 0x567DB3 (0x67) [MOD hook_vm.cpp:74-171] |
| the item popup | `0x116 Effect_GetItem` -> `XsoHost::effect_get_item`; `0xD5 Window_Treasure` -> `XsoHost::acquire_item` |
| blessing purchase | `0xAF` / `0x9A` in `xso.cpp` (index -> bit: 0..6 same bit, 7/8 = the armour/leggings raval rows, 9..33 -> index−2 [MOD hook_ap.cpp:445-463; BLESSING_SHOP.md:90-131]) |
| current scene | `game.g_flags[505]`, set in `room_entry_flags` (`view_game.inl`) |
| death | `if (player.hp < 1) player_death();` and the `[death] HP ...` lambda (`view_game.inl`). With g_flags[133] set (a losable duel), it ends the fight at HP 1 instead: **do not send a DeathLink then** [H] |
| SP wallet | `player.sp` (`gold_get_fn` / `gold_set_fn`) |
| weapon tier | g_flags[0x94] |
| warp registry | `warp_on[24]` (`XsoHost::warp_enable`, op 0xA7) |
| level / EXP | `Player::level` / `Player::exp` |

## 7. Next: the game-wiring agent (precise task list)

1. **Link and own a Session.** Link `cleria-ap` into `cleria-view`. Create one `ap::Session` with a
   `FileDataPackageStore("<settings folder>/archipelago/datapackage")`, and call `update(now)` each frame.
   - Persist the uuid (`make_uuid()` once) in `[cleria.archipelago]` along with server, slot,
     DeathLink and auto-connect.
   - Set `Config::expected_seed` from the loaded save's sidecar.
2. **Sidecar save** (MODS.md §12.2): store `seed`, `slot`, `applied` (the received-items count),
   the local checks, `goal_sent` and the ore count. Apply `received()[applied..]` only when
   `items_in_sync()` and the world is not frozen (`csMgr.world_frozen()`). Queue items otherwise.
3. **Location detection** from `slot_data.location_signals` + `location_detect`, converted by the
   §5.2 arithmetic (or matched by name against `res/ap/ys_origin.json`).
   - Evaluate each tick while the scene is > 0, as the mod does.
   - Report via `client().check_locations()`.
   - Scene method: fire when `g_flags[505]` changes to the scene.
   - Floor method: membership in a seen-floors set fed by `scene_floors`.
   - Prefer firing on the VM store (the grant filter) for flag locations, with the per-tick sweep as a safety net.
4. **Grant filter** (MODS.md §12.4):
   - Before every VM item-cell store, swallow writes to `suppress_items` cells with value ≥ 1.
     A location's own flag (box flag) must pass through: it IS the check.
   - At `0x116`, swallow `suppress_give_ids`.
   - At `0xD5`, relabel with the scouted item: `LocationScouts` for all registered locations right
     after login, as the mod does; `scouted()` has `{item, player}`. Use the foreign-item art and
     "Sent <item> to <player>" when `player != slot()`.
   - **Do not port these retail mod details**: the 0x74..0x76 altar abort window and the 0x82 / flag 130 hazard.
5. **Item receipt** per §5.2 kinds (`ys::Table::item(id)`, or by name through `slot_data`).
   - SP goes to `player.sp`.
   - Cleria Ore sets the weapon tier.
   - Traps, SP, gems, Roda Fruit / Ore / Panacea counters are skipped on replay. `applied` makes
     replay impossible anyway if the sidecar is saved with the game.
   - Latch progression / useful grants so a save reload cannot drop them (the retail 1.8.0 fix
     [MOD hook_ap.cpp:1105-1144]). In CleriaCore, prefer re-applying from `received()` after each
     load over a per-tick latch.
6. **Goal** (`set_status(ClientStatus::Goal)`), triggered by either:
   - (g_flags[150] ∈ {1,2} and g_flags[226] ≥ 1) or (g_flags[150] == 3 and g_flags[227] ≥ 2);
   - entering scene 7002 after gameplay has been seen.

   With `goal == 1`, g_flags[220..225] must also all be ≥ 1 [MOD hook_ap.cpp:476-519, 1962-1977].
7. **DeathLink**:
   - On `player_death()` (not in a duel, g_flags[133]), call `send_death(unix_now, "<slot> ran out of HP")`.
     Debounce 6 s, as the mod does.
   - On event `DeathLink`, set HP to 0 on the next tick that is in gameplay and not in a cutscene.
     Then suppress our own send for that death.
8. **Options in slot_data** (§5.3): statue_warp_locks / random_start / start loadout / level and
   EXP scaling / blessing shop. Each is a separate feature; port it from the cited mod code when wanted.
9. **UI** (MODS.md §12.11):
   - The ui kit text field, and the "Archipelago" F1 tab: server, slot, password, DeathLink,
     connect / disconnect, `status_text()`.
   - `Shell::hud` layers: `ap.status`, `ap.feed` (render `Line` spans with their RGB), `ap.tracker`.
   - The foreign-item icon, and `state.json`.
10. **Tests**: a `CLERIA_DRIVE` case against a local scripted server. The `MiniServer` in
    `apps/cleria-aptest` can be lifted into a tool. Cover: open S_1001/S_BOX01 -> `LocationChecks`
    [0x596000 + idx of "Wailing Blue: 2F Path 1"]; receive the Bronze Key -> g_flags[0x63] == 1.

### 7.1 Facts this library could not establish (for the wiring agent)

- **What g_flags[207] (0xCF) really is.** The retail mod and its notes call it the current floor
  [MOD RE_FINDINGS.md:146-151]. CleriaCore's RE shows room scripts writing area-caption indices to it
  (2 at S_1001, 4 at S_2000, 8 at Gelaldy; boss_re/*, BARRIER_OBJECTS.md:259). Use
  `slot_data.scene_floors[g_flags[505]]` for floor checks, as the mod does in practice, and do not
  rely on flag 207.
- **The retail dirty bit 0x76B914.** Whether CleriaCore has an equivalent "recompute stats" request
  (the mod ORs 0x75 / 0x10 into it after grants) is unknown. In CleriaCore, call `compute_stats` /
  the equip refresh directly.
- **The goal scene.** Whether scene 7002 is reliable for Yunica and Hugo is unknown; the mod keeps
  the 226/227 flags as a backup.
- **The `bless_idx_N` cells of the retail F5 shop** are un-RE'd in the mod itself, and its write
  target (0x76A634 + 8·idx) is actually CleriaCore's per-character weapon tier `InvState::tier`.
  Do not port it.

### 7.2 What the library leaves open

- No live test against a real AP room (one needs a hosted seed). One handshake to
  `wss://archipelago.gg:443/` confirmed TLS through SChannel. The site answered `HTTP 200 instead of
  a WebSocket upgrade`, as expected: there is no room on that port.
  (`cleria-aptest --live <address>` repeats this check by hand.)
- permessage-deflate (§2).
- `CreateHints` / `UpdateHint` have no helpers; `send_packet()` covers them.

### 7.3 Retail-mod bugs, so they are not copied

Found while reading the mod [MOD]:
1. With `progressive_skills` off, a received gem (Emerald / Ruby / Topaz, index 0x300+) hits the
   `>= 0x200` blessing branch, and `grant_blessing_bit(0x100..)` returns without effect
   (hook_ap.cpp:1831-1835, 434-436).
2. `grant_progressive_skill` has no replay guard, and its counter resets on connect. A reconnect
   re-raises the level cells, up to 3 (1199-1220, 1331-1332).
3. An item name that does not resolve is skipped, but the applied index still advances (1772, 1880-1890).
4. `DecideStore` fires a flag location on any store to its cell, even a store of 0 (hook_vm.cpp:83-88).
5. Auto-connect from `yso_ap.cfg` prefixes `ws://`, not `wss://` (2838-2841).
6. The uuid is the constant `"YsOrigin-Mod"` (apclientpp constructor, 2844). CleriaCore sends a real per-install uuid.

## 8. Verification

```
cmake --build build_diff --config Release --target cleria-jsontest cleria-aptest
ctest --test-dir build_diff -C Release -R "jsontest|aptest"      # 2/2, ~6 s
python tools/ap_import.py --check
```

- **`cleria-jsontest`**: 84 checks.
  - Literals, number ranges (int64 edges, 2^53+1 exact, overflow / underflow), NaN with and without
    the option, leading zeros, malformed numbers.
  - Escapes, surrogate pairs and lone surrogates, UTF-8 validation (overlong, surrogate, > U+10FFFF,
    truncated), control characters.
  - Containers, duplicate keys, trailing commas, depth limits (300 nested refused; 100000 fails
    cleanly), the size limit, error positions and messages.
  - The writer (compact, pretty, ASCII, doubles keep `.0`, NaN -> null), round trips, deep copies,
    and a 50000-entry data package.
- **`cleria-aptest`**: 110 checks, about 6 s, repeated runs all green.
  - The handshake with a data-package download and file cache.
  - Items: index 0 / append / repeat / overlap / gap -> Sync + LocationChecks -> index-0 recovery.
  - Checks, RoomUpdate, scouts, DeathLink send / echo filter / receive / tag off, keep-alive,
    ConnectUpdate, goal.
  - A reconnect re-sending checks, goal, scouts and held chat.
  - Get / Set / SetNotify, Retrieved / SetReply, bad frames, InvalidPacket, refusal + retry, seed mismatch.
  - PrintJSON names and colours, address parsing, the Ys Origin table.
  - The **loopback server**: winsock on 127.0.0.1, RFC 6455 handshake, a scripted AP server. Covered:
    - wss fails and ws logs in;
    - a 2 MB message;
    - 1.5 s idle beyond a 0.8 s handshake timeout;
    - DeathLink echo;
    - kick -> `disconnected: connection lost: connection reset (retrying in 1 s)` -> reconnect with
      cached names and no duplicate items;
    - a polite close;
    - a wrong slot name -> `auth failed: no slot with that name in this room` with no retry loop,
      then `retry_login` -> connected;
    - a server that ignores the close frame;
    - nothing listening;
    - an invalid address.
- **No change to the game**: `tools/goldens.py compare` 7/7 and `tools/drive_regress.py` 4/4. The
  only change to `cleria` is the added `json.cpp`; nothing calls into `cleria-ap` yet.
- **`tools/ap_import.py`**: 336 locations (scene 160, flag 95, item_arr 36, bit 24, floor 21; none
  unmapped) and 147 items (no `unknown`). All 61 chest locations are detected by their own box flag
  (S_1014/S_BOX01 by scene). The apworld repo's `git status` was clean after the runs.
