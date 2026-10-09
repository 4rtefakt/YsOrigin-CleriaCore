-- Archipelago: CleriaCore as a client for the Ys Origin apworld (github.com/4rtefakt/ys-origin-archipelago),
-- written as an ordinary Lua mod on CleriaCore's generic cleria.* API (CleriaCore's build/shell_re/LUA_API.md).
-- It replaced CleriaCore's former built-in C++ client, whose drive tests it matched line for line (README.md).
--
--   ap/client.lua   the AP protocol (RoomInfo, DataPackage, Connect, ReceivedItems with index resync,
--                   LocationChecks, LocationScouts, StatusUpdate, Bounce / DeathLink, PrintJSON) + the session
--   ap/text.lua     PrintJSON -> coloured spans
--   ap/ys.lua       the Ys Origin rules (detection, suppression, receipts, loadout, goal, EXP)
--   ap/ui.lua       the page (F1 > Mods > Archipelago) and the overlays (status, item feed, tracker, the fog trap)
--   data/ys_origin.json   the location / item table (CleriaCore's tools/ap_import.py, from the apworld's data)
--   icons/AP_ITEM.DDS     the foreign-item icon (CleriaCore's tools/make_ap_icon.py)

local client = require("ap.client")
local text = require("ap.text")
local ys = require("ap.ys")
local J = cleria.json
local G = cleria.game

local kApIcon = 0x2A1   -- this mod's icon id for "another world's item" (cleria.ui.icon: mod icons are 0x200..0x3FF)

-- ---- settings: cleria.ini [cleria.mod.ysorigin.archipelago] ----------------------------------------------
local S = cleria.settings
local cfg = {
    server = S.get("Server", "archipelago.gg:38281"), slot = S.get("Slot", ""), password = S.get("Password", ""),
    uuid = S.get("Uuid", ""), death_link = S.get("DeathLink", false), auto_connect = S.get("AutoConnect", false),
    show_status = S.get("ShowStatus", true), show_feed = S.get("ShowFeed", true), show_tracker = S.get("ShowTracker", false),
    export_state = S.get("ExportState", true),
    -- quality of life (each off or neutral by default unless it only adds information)
    show_room = S.get("ShowRoom", false), room_spoil = S.get("RoomSpoilers", false), tracker_mode = S.get("TrackerMode", 1),
    boss_on_kill = S.get("BossOnKill", false), notice_prog = S.get("NoticeProgression", true),
    hint_alerts = S.get("HintAlerts", true), exp_mult = S.get("ExpMultiplier", 0),
    autosave = S.get("Autosave", true), autosave_slot = S.get("AutosaveSlot", 8),
}
local cfg_keys = {server = "Server", slot = "Slot", password = "Password", uuid = "Uuid", death_link = "DeathLink",
                  auto_connect = "AutoConnect", show_status = "ShowStatus", show_feed = "ShowFeed",
                  show_tracker = "ShowTracker", export_state = "ExportState", show_room = "ShowRoom",
                  room_spoil = "RoomSpoilers", tracker_mode = "TrackerMode", boss_on_kill = "BossOnKill",
                  notice_prog = "NoticeProgression", hint_alerts = "HintAlerts", exp_mult = "ExpMultiplier",
                  autosave = "Autosave", autosave_slot = "AutosaveSlot"}
local function persist(k) S.set(cfg_keys[k], cfg[k]) end

-- ---- the runtime state ---------------------------------------------------------------------------------------
local A = {
    cfg = cfg, logic = ys.logic(), in_game = false, adopted = false, mismatch = "", room = "", chests = {},
    live_from = 0, await_login_items = false, fired_script = "", fired_loc = 0, ticks = 0, fired_tick = 0,
    death_pending = false, death_from = "", death_cause = "", death_at = -100, death_ours = false, death_ours_tick = 0,
    fog_t = 0, butter_t = 0, butter_tier = 0, chaos_pending = 0, leech_pending = 0,
    feed = {}, log = {}, state_dirty = true, state_at = -10, rng = 0xA9,
    hints = {}, chat_text = "", hint_text = "",
}

local function log(s)
    cleria.log(s)
    A.log[#A.log + 1] = s
    while #A.log > 14 do table.remove(A.log, 1) end
    A.state_dirty = true
end
A.logf = log

local function feed_line(line)
    line.t = cleria.time()
    A.feed[#A.feed + 1] = line
    while #A.feed > 40 do table.remove(A.feed, 1) end
end
local function feed_text(s, rgb) feed_line(text.line(s, rgb)) end

local tbl, err = ys.load_table("data/ys_origin.json")
if not tbl then error("no location table: " .. tostring(err)) end
A.table = tbl

-- the data-package cache and the slot_data cache live in the mod's data folder (cleria.data)
local function safe(s) return (s:gsub("[^%w%-%.]", "_")) end
local store = {
    load = function(game, sum)
        local t = cleria.data.read("datapackage/" .. safe(game) .. "_" .. safe(sum) .. ".json")
        local v = t and J.decode(t)
        if type(v) == "table" and v.checksum == sum then return v end
        return nil
    end,
    store = function(game, sum, gd) cleria.data.write("datapackage/" .. safe(game) .. "_" .. safe(sum) .. ".json", J.encode(gd)) end,
}
A.session = client.session(store)

local function connected() return A.session.status == "connected" end
local function active() return A.in_game and A.adopted and A.logic.configured end
A.connected, A.active = connected, active
local function death_link_on() return cfg.death_link or (A.logic.configured and A.logic.opt.death_link) end
A.death_link_on = death_link_on

local function store_state()
    if not A.in_game or not cleria.save.ready() then return end
    cleria.save.set("state", ys.state_to_table(A.logic.st))
    A.state_dirty = true
end
local function slot_cache(seed, slot)
    if seed == "" then return nil end
    return "slot_" .. seed:gsub("[^%w]", "_") .. "_" .. slot .. ".json"
end

-- ---- autosave (mod API 4: cleria.game.save) --------------------------------------------------------------
-- After anything worth keeping (a check, a received item, a key door, a Panacea) the game is saved to the
-- player's autosave slot. A request only marks the tick; the tick handler saves 1.5 s later and keeps
-- asking until the engine agrees (it refuses in a cutscene, a boss fight, an arena, a room just entered).
A.can_save = type(G.save) == "function"
local function autosave_request()
    if cfg.autosave and A.can_save then A.autosave_at = A.ticks end
end

-- ---- checks -------------------------------------------------------------------------------------------------
local function report(ids, how)
    if #ids == 0 then return end
    autosave_request()
    for _, id in ipairs(ids) do
        local r = A.logic:reg(id)
        log("check " .. id .. " " .. (r and r.name or "?") .. " (" .. how .. ")")
    end
    A.session.client:check_locations(ids)
    store_state()
end

-- ---- the save <-> the room ----------------------------------------------------------------------------------
local names = {"Yunica", "Hugo", "Toal"}
local function sync_save()
    if not A.in_game then return end
    local st = A.logic.st
    A.mismatch = ""
    local online = connected()
    if online then
        local c = A.session.client
        if st.seed == "" then
            st.seed, st.slot, st.slot_name = c:seed_name(), c.slot, c:player_name(c.slot)
            log("save stamped: seed " .. st.seed .. " slot " .. st.slot .. " (" .. st.slot_name .. ")")
        elseif st.seed ~= c:seed_name() or st.slot ~= c.slot then
            A.mismatch = "this save belongs to seed " .. st.seed .. " slot " .. st.slot .. "; the room is seed " .. c:seed_name() ..
                         " slot " .. c.slot
            log("seed mismatch: " .. A.mismatch .. " -- disconnected")
            feed_text("Archipelago: this save belongs to another seed. Disconnected.", 0xFA8072)
            A.session:disconnect()
            online = false
        end
        if online then
            A.adopted = true
            if next(st.checks) then
                local ids = {}
                for id in pairs(st.checks) do ids[#ids + 1] = id end
                table.sort(ids)
                c:check_locations(ids)
            end
            if not st.started then log("slot loadout: " .. tostring(A.logic:start_loadout())) end
            A.logic.boss_on_kill = cfg.boss_on_kill
            report(A.logic:on_scene(ys.scene_number(A.room)), "room")
            local want, playing = A.logic.opt.character, G.character()
            if want >= 0 and playing >= 1 and want + 1 ~= playing then
                local w = "this seed is for " .. names[math.max(0, math.min(want, 2)) + 1] .. "; the game is " ..
                          names[math.max(0, math.min(playing - 1, 2)) + 1] .. "'s"
                log("warning: " .. w)
                feed_text("Archipelago: " .. w .. ".", 0xE6B040)
            end
        end
    end
    if not online then A.adopted = false end
    if not online and st.seed ~= "" then   -- offline: the rules from the cached slot_data, so vanilla items stay swallowed
        local f = slot_cache(st.seed, st.slot)
        local t = f and cleria.data.read(f)
        local v = t and J.decode(t)
        if type(v) == "table" then
            local keep = st
            A.logic:configure(v, tbl)
            A.logic.st = keep
            A.adopted = true
            log("offline: rules of seed " .. st.seed .. " from " .. f)
        end
    end
    store_state()
end

local function on_session(e)
    A.in_game = true
    local st = ys.state_from_table(cleria.save.get("state"))
    A.logic.st = st
    A.logic.expected_hi = 0
    A.death_pending = false
    A.fog_t, A.butter_t, A.chaos_pending, A.leech_pending = 0, 0, 0, 0
    if st.seed ~= "" or A.session.status ~= "idle" then
        log((e.kind == "load" and "loaded save" or "new game") .. (st.seed == "" and ": no AP block" or
            ": seed " .. st.seed .. " applied " .. st.applied .. " checks " .. ys.count(st.checks)))
    end
    sync_save()
end
cleria.events.on("new_game", on_session)
cleria.events.on("load", on_session)

-- ---- the connection -------------------------------------------------------------------------------------------
local function config()
    local c = {name = cfg.slot, password = cfg.password, uuid = cfg.uuid, tags = cfg.death_link and {"DeathLink"} or {}}
    if A.in_game and A.logic.st.seed ~= "" then c.expected_seed = A.logic.st.seed end   -- the save's seed guard
    return c
end

local function connect()
    if cfg.slot == "" or cfg.server == "" then log("connect: no server / slot set") return end
    if cfg.uuid == "" then cfg.uuid = client.make_uuid() persist("uuid") end   -- one per install
    A.mismatch = ""
    log("connecting to " .. cfg.server .. " as " .. cfg.slot)   -- never the password
    A.session:connect(cfg.server, config())
end
local function disconnect()
    A.session:disconnect()
    A.adopted = false
    log("disconnected")
    sync_save()   -- offline rules from the cache
end
A.connect, A.disconnect = connect, disconnect

local function on_connected()
    local c = A.session.client
    local keep = A.logic.st
    A.logic:configure(c.slot_data, tbl)
    A.logic.st = keep
    log("connected as " .. c:player_name(c.slot) .. " (slot " .. c.slot .. ") seed " .. c:seed_name() .. ", " .. #A.logic.regs .. " locations")
    for _, u in ipairs(A.logic.opt.unsupported) do log("not supported: " .. u) end
    local f = slot_cache(c:seed_name(), c.slot)
    if f then cleria.data.write(f, J.encode(c.slot_data)) end   -- for offline play of this seed's saves
    local ids = {}
    for _, r in ipairs(A.logic.regs) do ids[#ids + 1] = r.id end
    c:scout_locations(ids)   -- what every location holds, for the treasure box [MOD hook_ap.cpp:1754]
    if death_link_on() and not c:has_tag("DeathLink") then c:set_tags({"DeathLink"}) end
    -- this slot's hints: the server keeps them under a read-only key and tells us when it changes
    local key = "_read_hints_" .. c.team .. "_" .. c.slot
    A.hint_key, A.hints = key, {}
    c:queue({cmd = "Get", keys = J.array({key})})
    c:queue({cmd = "SetNotify", keys = J.array({key})})
    sync_save()
end

-- the server's hint list -> A.hints (unfound first), names resolved
local function read_hints(v)
    local c = A.session.client
    local out = {}
    for _, h in ipairs(type(v) == "table" and v or {}) do
        if type(h) == "table" and math.type(h.location) and math.type(h.item) then
            out[#out + 1] = {location = h.location, item = h.item, finder = h.finding_player or 0, receiver = h.receiving_player or 0,
                             found = h.found == true, flags = math.type(h.item_flags) and h.item_flags or 0}
        end
    end
    table.sort(out, function(a, b) if a.found ~= b.found then return not a.found end return a.location < b.location end)
    for _, h in ipairs(out) do
        h.item_name = c:item_name(h.item, h.receiver)
        h.location_name = c:location_name(h.location, h.finder)
        h.finder_name, h.receiver_name = c:player_name(h.finder), c:player_name(h.receiver)
    end
    A.hints = out
    log("hints: " .. #out)
end

-- entering a room that holds a hinted item of ours to find
local function hint_alert(sc)
    if not cfg.hint_alerts or not connected() then return end
    local c = A.session.client
    for _, h in ipairs(A.hints) do
        local l = tbl.loc[h.location]
        if not h.found and h.finder == c.slot and l and l.scene == sc and not A.logic.st.checks[h.location] then
            feed_text("Hinted here: " .. h.item_name .. (h.receiver == c.slot and "" or " for " .. h.receiver_name) ..
                      " (" .. (l.room or h.location_name) .. ")", 0xF2EA8C)
        end
    end
end

local function handle(e)
    local k = e.kind
    if k == "Connected" then
        A.await_login_items = true
        on_connected()
    elseif k == "SeedMismatch" then
        A.mismatch = "the room is seed " .. e.text .. "; this save belongs to seed " .. A.logic.st.seed
        log("seed mismatch: " .. A.mismatch)
        feed_text("Archipelago: this save belongs to another seed.", 0xFA8072)
    elseif k == "Refused" then
        local why = {}
        for _, x in ipairs(e.errors) do why[#why + 1] = client.describe_refusal(x) end
        log("refused: " .. table.concat(why, ", "))
    elseif k == "ItemsReset" then
        -- the login list arrived before this session: a trap in it already happened [H]
        if A.await_login_items then A.live_from = #A.session.client.received end
        A.await_login_items = false
        log("received list: " .. A.live_from .. " item(s)")
    elseif k == "ItemsReceived" then log("received " .. #e.items .. " item(s) at #" .. e.index)
    elseif k == "LocationInfo" then log("scouted " .. #e.items .. " location(s)")
    elseif k == "Print" then
        cleria.log("print " .. e.line.type .. ": " .. text.plain(e.line))
        feed_line(e.line)
    elseif k == "DeathLink" then
        if death_link_on() then
            A.death_pending, A.death_from, A.death_cause = true, e.source, e.text
            log("deathlink from " .. e.source .. (e.text == "" and "" or ": " .. e.text))
        end
    elseif k == "Retrieved" then
        local keys = type(e.data.keys) == "table" and e.data.keys or {}
        if A.hint_key and keys[A.hint_key] ~= nil then read_hints(keys[A.hint_key]) end
    elseif k == "SetReply" then
        if A.hint_key and e.data.key == A.hint_key then read_hints(e.data.value) end
    elseif k == "InvalidPacket" then log("server: invalid packet: " .. e.text)
    elseif k == "ProtocolError" then log("protocol error: " .. e.text)
    end
    A.state_dirty = true
end

-- ---- the grant filter: vanilla suppression, the relabelled treasure box --------------------------------------
local function rgb_tag(rgb) return string.format("<color:0xff%06x>", rgb & 0xFFFFFF) end
local function class_rgb(flags)
    if flags & client.kTrap ~= 0 then return 0xFA8072 end
    if flags & client.kProgression ~= 0 then return 0xAF99EF end
    if flags & client.kUseful ~= 0 then return 0x6D8BE8 end
    return 0x00EEEE
end

-- the icon of a scouted item: our own item's INVINFO icon when it has one, else the AP icon
local function icon_of(it)
    local c = A.session.client
    if it.player ~= c.slot then return kApIcon end
    local ti = tbl.item[it.item]
    if not ti then return kApIcon end
    if ti.kind == "item" or ti.kind == "cleria_ore" then return (ti.flag >= 0 and ti.flag < 0x80) and ti.flag or kApIcon end
    if ti.kind == "sp" then return 0x78 end   -- [H] a gold piece
    if ti.kind == "progressive_skill" then return (ti.artifact >= 0 and ti.artifact < 0x80) and ti.artifact or kApIcon end
    if ti.kind == "progressive_gear" then
        local ch = G.character()
        for _, cell in ipairs(ti.ladder[ch == 2 and "hugo" or ch == 3 and "toal" or "yunica"] or {}) do
            if cell >= 0 and cell < 0x80 and G.flag(cell) < 1 then return cell end
        end
    end
    return kApIcon
end

-- the location whose box this is: a chest by its key, else the location the same script just found
local function box_location(q)
    if q.script ~= "" then
        local id = tbl.chest_loc[q.room .. "/" .. string.upper(q.script)]
        if id and A.logic:reg(id) then return id end
        if q.script == A.fired_script and A.ticks - A.fired_tick < 600 then return A.fired_loc end   -- [H] 10 s
    end
    return nil
end

cleria.content.add_filter(function(q)
    if not active() then return nil end
    local key = q.room .. "/" .. string.upper(q.script)
    -- a chest that is not an AP location (S_2005/S_BOX03) keeps its vanilla item (ARCHIPELAGO.md §5.4)
    local chest = A.chests[q.script] ~= nil
    local ap_chest = chest and tbl.chest_loc[key] ~= nil and A.logic:reg(tbl.chest_loc[key]) ~= nil
    if q.kind == "store" then
        if (not chest or ap_chest) and A.logic:suppress_store(q.index, q.old, q.value) then
            log(string.format("suppressed g_flags[0x%X] %d -> %d (%s)", q.index, q.old, q.value, key))
            -- an inventory item (not the skill powers or the drained ring that ride along) swallowed outside
            -- a chest: if no check comes with it, the scene's "you got it" is a lie (withheld_notice)
            if not chest and q.index >= 0x48 and q.index <= 0x73 and q.index ~= 0x5E then
                A.withheld = {index = q.index, tick = A.ticks}
            end
            return "suppress"
        end
        local f = A.logic:on_store(q.index, q.old, q.value)
        if #f > 0 and ys.sp_chest[q.index] then   -- an SP chest that is a location: its vanilla SP goes back
            local back = math.floor(math.min(ys.sp_chest[q.index], cleria.player.sp()))
            cleria.player.add_sp(-back)
            log("SP chest: took back the vanilla " .. back .. " SP (" .. key .. ")")
        end
        if #f > 0 then
            A.fired_script, A.fired_loc, A.fired_tick = q.script, f[#f], A.ticks
            report(f, "store")
        end
        return nil
    end
    if q.kind == "icon" and A.logic:suppress_give(q.index) and (not chest or ap_chest) then return "suppress" end
    local loc = box_location(q)
    if not loc then return nil end
    local c = A.session.client
    local s = c.scouted[loc]
    local d = {action = "replace"}
    if not s then   -- not scouted (offline): what it is shows at the server
        d.index = kApIcon
        d.text = "Found an\\n" .. rgb_tag(0xF2EA8C) .. "Archipelago item<color:>."
    else
        local name = c:item_name(s.item, s.player)
        d.index = icon_of(s)
        if s.player == c.slot then d.text = cleria.content.treasure_text(q.text, name, 1)
        else
            -- One line when it fits (the box grows to its text; the renderer never wraps), else break
            -- before "to <player>". [H] 48 characters ~ 530 of the 1024 layout units.
            local who = c:player_name(s.player)
            local to = (#("Sent " .. name .. " to " .. who .. ".") <= 48) and " to " or "\\nto "
            d.text = "Sent " .. rgb_tag(class_rgb(s.flags)) .. name .. "<color:>" .. to .. who .. "."
        end
    end
    if q.kind == "window" then log("box " .. key .. ": " .. (d.text:gsub("<color:[^>]*>", ""))) end
    return d
end)

-- ---- receiving, per tick -------------------------------------------------------------------------------------
local function run_trap(t)
    -- [MOD hook_ap.cpp:658-673, 2686-2726]: EXP Leech -1 level, Chaos Warp a random unlocked statue,
    -- Butterfingers the weapon at tier 0 for 8 s, Blinding Fog a haze for 30 s
    if t == "EXP Leech" then A.leech_pending = A.leech_pending + 1
    elseif t == "Chaos Warp" then A.chaos_pending = A.chaos_pending + 1
    elseif t == "Butterfingers" then
        if A.butter_t <= 0 then A.butter_tier = cleria.player.weapon_tier() end
        A.butter_t = 8 * 60
        cleria.player.set_weapon_tier(0)
    elseif t == "Blinding Fog" then A.fog_t = 30 * 60 end
    feed_text("Trap: " .. t, 0xFA8072)
end

local function grant_pending()
    local c = A.session.client
    if not c:items_in_sync() then return end
    local st = A.logic.st
    local any = false
    while st.applied < #c.received do
        local it = c.received[st.applied + 1]
        local index = st.applied
        local g = A.logic:grant(it.item, it.flags)
        st.applied = st.applied + 1
        any = true
        log("grant #" .. index .. " " .. g.text .. " (from " .. c:player_name(it.player) .. ")")
        if cfg.notice_prog and index >= A.live_from and it.flags & client.kProgression ~= 0 and not g.trap then
            cleria.notice(c:item_name(it.item, c.slot) .. (it.player == c.slot and "" or "  (from " .. c:player_name(it.player) .. ")"),
                          "Archipelago")
        end
        if g.trap then
            if index < A.live_from then log("trap " .. g.trap .. " arrived while away: skipped")   -- [H] like the mod's replay rule
            else run_trap(g.trap) end
        end
        local ti = tbl.item[it.item]
        if ti and ti.kind == "item" and ti.flag >= 0 then
            cleria.events.emit("item_obtained", {item = ti.flag, count = 1, source = "ap"})
        end
    end
    if any then store_state() end
    if any and st.applied > A.live_from then autosave_request() end   -- not for the login list of a loaded save
end

-- A story beat that hands over a pool item (the Zelkarons "charging" the Evil Ring) still plays its dialogue
-- while the store is swallowed. A chest is fine: its check fires in the same script and the box says what was
-- there. When a swallowed store has no check around it and the player still lacks the item, say so
-- [MOD report_withheld]. [H] 4 s either side.
local kWithheldTicks = 4 * 60
local function withheld_notice()
    local w = A.withheld
    if not w or A.ticks - w.tick < kWithheldTicks then return end
    A.withheld = nil
    if A.fired_tick > 0 and math.abs(A.fired_tick - w.tick) <= kWithheldTicks then return end   -- a check: the box told
    if G.flag(w.index) >= 1 then return end                                                    -- already owned
    local name = G.item_name(w.index) or string.format("item 0x%X", w.index)
    log("withheld: told the player about " .. name)
    feed_text("The game's own " .. name .. " is withheld: yours comes from the multiworld.", 0xE6B040)
end

cleria.events.on("room_enter", function(e)
    A.room = e.room
    A.chests = G.chests()
    if not active() then return end
    A.logic.boss_on_kill = cfg.boss_on_kill
    report(A.logic:on_scene(ys.scene_number(e.room)), "room")
    hint_alert(ys.scene_number(e.room))
end)

if (cleria.api_version or 3) >= 4 then
    cleria.events.on("boss_defeated", function(e)
        if not active() or not cfg.boss_on_kill then return end
        report(A.logic:boss_defeated(ys.scene_number(e.room)), "boss defeated")
    end)
    cleria.events.on("item_used", function() if active() then autosave_request() end end)     -- a Panacea
    cleria.events.on("door_opened", function() if active() then autosave_request() end end)   -- a key / medallion door
end

cleria.events.on("death", function()
    local ours = A.death_ours and A.ticks - A.death_ours_tick < 120
    A.death_ours = false
    if ours then return end                                   -- a received DeathLink is not sent back
    if not connected() or not death_link_on() or not A.adopted then return end
    local now = cleria.time()
    if now - A.death_at < 6.0 then return end                 -- the mod's debounce [MOD hook_ap.cpp:1906]
    A.death_at = now
    local c = A.session.client
    c:send_death(math.floor(cleria.unix_time()) + 0.0, c:player_name(c.slot) .. " ran out of HP")
    log("deathlink sent")
end)

-- the EXP of a kill: the player's own multiplier when set (the page), else the seed's
cleria.game.exp_factor(function(level)
    if not active() then return 1 end
    if cfg.exp_mult > 0 then return cfg.exp_mult end
    return A.logic:exp_factor(level)
end)
cleria.speedrun.modification(function()
    if A.session.status ~= "idle" or A.logic.st.seed ~= "" then return "Archipelago session" end
end)

-- ---- state.json for external trackers (in the mod's data folder) ----------------------------------------------
local function write_state()
    if not A.state_dirty or not cfg.export_state then return end
    local now = cleria.time()
    if now - A.state_at < 1.0 then return end   -- [H] at most once a second
    A.state_at, A.state_dirty = now, false
    local c, st = A.session.client, A.logic.st
    local rec = J.array()
    for i, it in ipairs(c.received) do
        rec[#rec + 1] = {index = i - 1, item = it.item, name = c:item_name(it.item, c.slot), from = c:player_name(it.player), flags = it.flags}
    end
    local chk, miss = J.array(), J.array()
    for id in pairs(st.checks) do chk[#chk + 1] = id end
    table.sort(chk)
    for _, r in ipairs(A.logic.regs) do if not st.checks[r.id] and not c.checked[r.id] then miss[#miss + 1] = r.id end end
    cleria.data.write("state.json", J.encode({
        format = 1, game = "Ys Origin", status = A.session:status_text(now), connected = connected(),
        seed = st.seed ~= "" and st.seed or c:seed_name(), slot = st.slot ~= 0 and st.slot or c.slot, slot_name = cfg.slot,
        room = A.room, scene = A.logic.scene, goal = st.goal_sent, death_link = death_link_on(), applied = st.applied,
        received = rec, checked = chk, missing = miss}, {indent = 1}))
end

-- ---- each frame / each tick --------------------------------------------------------------------------------------
cleria.events.on("frame", function()
    A.session:update(cleria.time())
    for _, e in ipairs(A.session:take_events()) do handle(e) end
end)

cleria.events.on("tick", function()
    A.ticks = A.ticks + 1
    if A.fog_t > 0 then A.fog_t = A.fog_t - 1 end
    if not A.in_game or not cleria.save.ready() then write_state() return end
    local frozen = G.frozen()
    if A.butter_t > 0 then
        A.butter_t = A.butter_t - 1
        if A.butter_t == 0 then cleria.player.set_weapon_tier(A.butter_tier) end
    end
    if active() then
        A.logic.boss_on_kill = cfg.boss_on_kill
        report(A.logic:sweep(), "sweep")                    -- flags set behind the VM (a safety net)
        if connected() and not frozen then grant_pending() end   -- never into a cutscene
        if A.butter_t <= 0 then A.logic:enforce() end
        if A.autosave_at and A.ticks - A.autosave_at >= 90 and A.ticks % 15 == 0 then
            local slot = math.max(1, math.min(64, math.floor(cfg.autosave_slot)))
            store_state()
            if G.save(slot - 1) then   -- the file number is the book's "No.NN" minus one
                A.autosave_at = nil
                log(string.format("autosave: wrote No.%02d", slot))
                feed_text(string.format("Autosaved to No.%02d", slot), 0x9FD0A0)
            end
        end
        local fixed = A.logic:repair(A.logic.scene, A.ticks)
        if fixed then log("repair: " .. fixed) end
        withheld_notice()
        if not frozen then
            -- random start: the first time a new game stands in a real room, go to the seed's start statue
            if A.logic.st.started and A.logic.scene >= 1000 and A.logic.scene <= 6999 then
                local w, sc = A.logic:spawn_warp()
                if w then
                    G.set_warp_unlocked(w, true)
                    A.logic:spawn_done(sc)
                    log("random start: warp to S_" .. sc .. " (warp " .. w .. ")")
                    G.warp_to(w)
                    store_state()
                end
            end
            local lv = A.logic:level_floor(cleria.player.level())
            if lv > 0 then
                cleria.player.set_level(lv)
                log("level floor: Lv " .. lv)
            end
            if A.leech_pending > 0 then
                A.leech_pending = A.leech_pending - 1
                if cleria.player.level() > 1 then cleria.player.set_level(cleria.player.level() - 1) end
            end
            if A.chaos_pending > 0 then
                A.chaos_pending = A.chaos_pending - 1
                local on = {}
                for i = 0, 21 do if G.warp_unlocked(i) then on[#on + 1] = i end end
                if #on > 0 then
                    A.rng = (A.rng * 1103515245 + 12345) % 2147483648   -- [H] its own stream, never the game's RNG
                    local w = on[A.rng % #on + 1]
                    log("chaos warp to S_" .. ys.scene_of_warp_index(w))
                    G.warp_to(w)
                end
            end
        end
        if not A.logic.st.goal_sent and A.logic:goal_reached(A.logic.scene) then   -- the goal (ARCHIPELAGO.md §7.6)
            A.logic.st.goal_sent = true
            A.session.client:set_status(client.Goal)
            log("goal reached: StatusUpdate(Goal)")
            feed_text("Goal complete!", 0x00FF7F)
            store_state()
        end
        -- DeathLink in: on the next tick in play, not in a cutscene, not in a story duel (g_flags[133]) [H]
        if A.death_pending and not frozen and G.flag(133) == 0 and cleria.player.hp() > 0 then
            A.death_pending, A.death_ours, A.death_ours_tick, A.death_at = false, true, A.ticks, cleria.time()
            feed_text("DeathLink: " .. (A.death_cause == "" and A.death_from .. " died" or A.death_cause), 0xFA8072)
            cleria.player.set_hp(0)
        end
    end
    write_state()
end)

-- ---- the UI (F1 > Mods > Archipelago, the overlays) and the icon --------------------------------------------------------------
cleria.ui.icon(kApIcon, "icons/AP_ITEM.DDS")
require("ap.ui").install(A, persist)

-- ---- commands (CLERIA_DRIVE lua=<command> <args>) ------------------------------------------------------------
cleria.command("apconnect", function(args)
    local i = 0
    for part in string.gmatch(args, "[^,]+") do
        i = i + 1
        if i == 1 then cfg.server = part elseif i == 2 then cfg.slot = part elseif i == 3 then cfg.password = part end
    end
    connect()
end)
cleria.command("apdisconnect", function() disconnect() end)
cleria.command("apsay", function(args) A.session.client:say(args) end)
cleria.command("apfeed", function(args) feed_text(args) end)
cleria.command("apflag", function(args)
    local i = math.tointeger(tonumber(args)) or -1
    if i >= 0 and i < 0x200 and G.in_game() then cleria.log(string.format("flag 0x%X = %d", i, G.flag(i))) end
end)
cleria.command("apopt", function(args)
    local k, v = args:match("^(%w+)=(.*)$")
    local on = v ~= nil and v ~= "0"
    local map = {ShowFeed = "show_feed", ShowTracker = "show_tracker", ShowStatus = "show_status", DeathLink = "death_link",
                 BossOnKill = "boss_on_kill", ShowRoom = "show_room"}
    if k and map[k] then cfg[map[k]] = on end
end)
cleria.command("apsp", function() cleria.log(string.format("sp = %d", math.floor(cleria.player.sp() or 0))) end)
cleria.command("apstate", function()
    local c, st = A.session.client, A.logic.st
    cleria.log(string.format("state '%s' seed %s slot %d applied %d/%d checks %d goal %d active %d deathlink %d",
        A.session:status_text(cleria.time()), st.seed, st.slot, st.applied, #c.received, ys.count(st.checks),
        st.goal_sent and 1 or 0, active() and 1 or 0, death_link_on() and 1 or 0))
end)

log("loaded: " .. #tbl.locations .. " locations, " .. #tbl.items .. " items")
if cfg.auto_connect and cfg.slot ~= "" then connect() end
