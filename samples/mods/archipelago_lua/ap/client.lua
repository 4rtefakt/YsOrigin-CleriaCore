-- The Archipelago protocol in Lua: the client state machine and the session (reconnect, keep-alive,
-- status line). First written as CleriaCore's (since removed) C++ client, ARCHIPELAGO.md §3-4, and ported
-- onto the generic cleria.net.websocket and cleria.json; nothing here is Ys Origin specific.
--
--   RoomInfo -> GetDataPackage (missing / stale games) -> DataPackage -> Connect -> Connected | ConnectionRefused
--   ReceivedItems (index 0 = the whole list; a gap = Sync + LocationChecks), LocationChecks (kept, re-sent
--   after every login), LocationScouts -> LocationInfo, StatusUpdate (the goal is final), Bounce / Bounced
--   (DeathLink, keep-alive), Say, PrintJSON (ap.text), RoomUpdate.

local J = cleria.json
local text = require("ap.text")

local M = {}
M.kProgression, M.kUseful, M.kTrap = 1, 2, 4
M.Goal = 30

local function str(v) return type(v) == "string" and v or "" end
local function num(v, def) return math.type(v) and v or def end
local function arr(v) return type(v) == "table" and v or {} end
local function ids_list(set)
    local t = {}
    for id in pairs(set) do t[#t + 1] = id end
    table.sort(t)
    return J.array(t)
end
local function list(t) return J.array(t) end

-- ---- the client --------------------------------------------------------------------------------------
local Client = {}
Client.__index = Client

-- cfg: {game, name, password, uuid, version = {major, minor, build}, items_handling, tags, expected_seed}
function M.client(cfg, store)
    cfg.game = cfg.game or "Ys Origin"
    cfg.tags = cfg.tags or {}
    cfg.version = cfg.version or {major = 0, minor = 6, build = 4}
    cfg.items_handling = cfg.items_handling or 7
    return setmetatable({
        cfg = cfg, store = store, state = "disconnected",
        received = {},              -- the slot's whole received list (1-based; AP index i = received[i + 1])
        local_checks = {}, scout_requests = {}, scouted = {}, checked = {}, missing = {},
        players = {}, slot_info = {}, games = {}, held = {}, out = {}, events = {},
        status = 0, desync = false, last_death_sent = -1, team = 0, slot = 0, hint_points = 0,
        refused = {}, room_info = nil, slot_data = {},
    }, Client)
end

function Client:event(e) self.events[#self.events + 1] = e end
function Client:take_events() local e = self.events self.events = {} return e end
function Client:take_outgoing() local o = self.out self.out = {} return o end
function Client:push(cmd) self.out[#self.out + 1] = "[" .. J.encode(cmd) .. "]" end
function Client:queue(cmd)
    if self.state == "connected" then self:push(cmd) else self.held[#self.held + 1] = cmd end
end
function Client:items_in_sync() return not self.desync end
function Client:seed_name() return self.room_info and str(self.room_info.seed_name) or "" end

function Client:on_open() self.state = "await_room_info" self.refused = {} end
function Client:on_close() self.state = "disconnected" self.out = {} end

function Client:on_message(frame)
    local v, err = J.decode(frame)
    if v == nil then
        self:event({kind = "ProtocolError", text = "server sent invalid JSON: " .. err})
    elseif J.is_array(v) then
        for _, cmd in ipairs(v) do self:handle(cmd) end
    elseif type(v) == "table" then
        self:handle(v)
    else
        self:event({kind = "ProtocolError", text = "server frame is not a list of commands"})
    end
end

function Client:handle(p)
    if type(p) ~= "table" then return end
    local cmd = str(p.cmd)
    if cmd == "RoomInfo" then self:on_room_info(p)
    elseif cmd == "DataPackage" then self:on_data_package(p)
    elseif cmd == "Connected" then self:on_connected(p)
    elseif cmd == "ConnectionRefused" then
        self.state = "refused"
        self.refused = {}
        for _, e in ipairs(arr(p.errors)) do self.refused[#self.refused + 1] = str(e) end
        self:event({kind = "Refused", errors = self.refused, data = p})
    elseif cmd == "ReceivedItems" then self:on_received_items(p)
    elseif cmd == "LocationInfo" then
        local items = {}
        for _, v in ipairs(arr(p.locations)) do
            local it = M.read_item(v)
            self.scouted[it.location] = it
            items[#items + 1] = it
        end
        self:event({kind = "LocationInfo", items = items})
    elseif cmd == "RoomUpdate" then self:on_room_update(p)
    elseif cmd == "Bounced" then self:on_bounced(p)
    elseif cmd == "PrintJSON" then
        self:event({kind = "Print", line = text.render_print_json(p, self), data = p})
    elseif cmd == "Retrieved" or cmd == "SetReply" then
        self:event({kind = cmd, data = p})
    elseif cmd == "InvalidPacket" then
        self:event({kind = "InvalidPacket", text = str(p.type) .. " problem in " .. (type(p.original_cmd) == "string" and p.original_cmd or "?") ..
                    ": " .. str(p.text), data = p})
    else
        self:event({kind = "ProtocolError", text = cmd == "" and 'server command without a "cmd"' or "unknown server command " .. cmd, data = p})
    end
end

function M.read_item(v)
    return {item = num(v.item, 0), location = num(v.location, 0), player = num(v.player, 0), flags = num(v.flags, 0)}
end

function Client:on_room_info(p)
    self.room_info = p
    self:event({kind = "RoomInfo", data = p})
    local seed = str(p.seed_name)
    if self.cfg.expected_seed and self.cfg.expected_seed ~= "" and seed ~= self.cfg.expected_seed then
        self.state = "refused"
        self.refused = {"SeedMismatch"}
        self:event({kind = "SeedMismatch", text = seed, errors = self.refused})
        return
    end
    -- the games whose names are missing or stale (CommonClient prepare_data_package)
    local games, have_ap = {}, false
    for _, g in ipairs(arr(p.games)) do games[#games + 1] = str(g) have_ap = have_ap or g == "Archipelago" end
    if not have_ap then games[#games + 1] = "Archipelago" end
    local sums = type(p.datapackage_checksums) == "table" and p.datapackage_checksums or {}
    local needed = {}
    for _, g in ipairs(games) do
        local sum = sums[g]
        if type(sum) == "string" then
            if sum == "" then needed[#needed + 1] = g
            elseif not (self.games[g] and self.games[g].checksum == sum) then
                local cached = self.store and self.store.load(g, sum)
                if cached then self:add_game_names(g, cached) else needed[#needed + 1] = g end
            end
        end
    end
    if #needed > 0 then
        self:push({cmd = "GetDataPackage", games = list(needed)})
        self.state = "await_data_package"
        return
    end
    self:send_connect()
end

function Client:add_game_names(game, gd)
    local n = {checksum = str(gd.checksum), items = {}, item_ids = {}, locations = {}, location_ids = {}}
    for name, id in pairs(type(gd.item_name_to_id) == "table" and gd.item_name_to_id or {}) do n.items[id] = name n.item_ids[name] = id end
    for name, id in pairs(type(gd.location_name_to_id) == "table" and gd.location_name_to_id or {}) do
        n.locations[id] = name
        n.location_ids[name] = id
    end
    self.games[game] = n
end

function Client:on_data_package(p)
    local games = type(p.data) == "table" and type(p.data.games) == "table" and p.data.games or {}
    for game, gd in pairs(games) do
        self:add_game_names(game, gd)
        if self.store and str(gd.checksum) ~= "" then self.store.store(game, gd.checksum, gd) end
    end
    if self.state == "await_data_package" then self:send_connect() end
end

function Client:send_connect()
    local c = self.cfg
    self:push({cmd = "Connect", password = c.password or "", game = c.game, name = c.name or "", uuid = c.uuid or "",
               version = {major = c.version.major, minor = c.version.minor, build = c.version.build, class = "Version"},
               items_handling = c.items_handling, tags = list(c.tags), slot_data = true})
    self.state = "await_connected"
end

function Client:retry_connect(cfg)
    self.cfg = cfg
    if self.state == "refused" or self.state == "await_connected" then
        self.refused = {}
        if cfg.expected_seed and cfg.expected_seed ~= "" and self:seed_name() ~= cfg.expected_seed then return end
        self:send_connect()
    end
end

function Client:read_players(l)
    self.players = {}
    for _, pl in ipairs(arr(l)) do
        if J.is_array(pl) then   -- the NamedTuple list form
            self.players[#self.players + 1] = {team = num(pl[1], 0), slot = num(pl[2], 0), alias = str(pl[3]), name = str(pl[4])}
        else
            self.players[#self.players + 1] = {team = num(pl.team, 0), slot = num(pl.slot, 0), alias = str(pl.alias), name = str(pl.name)}
        end
    end
end

function Client:on_connected(p)
    self.state = "connected"
    self.refused = {}
    self.team, self.slot, self.hint_points = num(p.team, 0), num(p.slot, 0), num(p.hint_points, 0)
    self:read_players(p.players)
    self.missing, self.checked = {}, {}
    for _, v in ipairs(arr(p.missing_locations)) do self.missing[v] = true end
    for _, v in ipairs(arr(p.checked_locations)) do self.checked[v] = true self.missing[v] = nil end
    self.slot_data = type(p.slot_data) == "table" and p.slot_data or {}
    self.slot_info = {}
    for k, v in pairs(type(p.slot_info) == "table" and p.slot_info or {}) do
        local s
        if J.is_array(v) then s = {name = str(v[1]), game = str(v[2]), type = num(v[3], 1), group_members = arr(v[4])}
        else s = {name = str(v.name), game = str(v.game), type = num(v.type, 1), group_members = arr(v.group_members)} end
        self.slot_info[tonumber(k)] = s
    end
    self:event({kind = "Connected", data = p})
    self:flush_after_connect()
end

function Client:flush_after_connect()
    self:send_checks(true)
    local scouts = {}
    for id in pairs(self.scout_requests) do if not self.scouted[id] then scouts[id] = true end end
    if next(scouts) then self:queue({cmd = "LocationScouts", locations = ids_list(scouts), create_as_hint = 0}) end
    if self.status ~= 0 then self:queue({cmd = "StatusUpdate", status = self.status}) end
    local held = self.held
    self.held = {}
    for _, h in ipairs(held) do self:queue(h) end
end

function Client:send_checks(all_local)
    local ids = {}
    for id in pairs(self.local_checks) do if all_local or not self.checked[id] then ids[id] = true end end
    if not next(ids) or self.state ~= "connected" then return end
    self:queue({cmd = "LocationChecks", locations = ids_list(ids)})
end

function Client:on_received_items(p)
    local index = num(p.index, -1)
    local items = {}
    for _, v in ipairs(arr(p.items)) do items[#items + 1] = M.read_item(v) end
    local have = #self.received
    if index == 0 then
        self.received = items
        self.desync = false
        self:event({kind = "ItemsReset", index = 0, items = items})
        return
    end
    if index < 0 or index > have then   -- a gap: resync (Sync, then every local check)
        self.desync = true
        self:queue({cmd = "Sync"})
        self:send_checks(true)
        return
    end
    local fresh = {}
    for k, it in ipairs(items) do
        local pos = index + k - 1
        if pos < have then self.received[pos + 1] = it
        else self.received[#self.received + 1] = it fresh[#fresh + 1] = it end
    end
    if #fresh > 0 then self:event({kind = "ItemsReceived", index = have, items = fresh}) end
end

function Client:on_room_update(p)
    if p.players ~= nil then self:read_players(p.players) end
    if math.type(p.hint_points) then self.hint_points = p.hint_points end
    local newly = {}
    for _, v in ipairs(arr(p.checked_locations)) do
        if not self.checked[v] then self.checked[v] = true newly[#newly + 1] = v end
        self.missing[v] = nil
    end
    if self.room_info then
        for k, v in pairs(p) do if k ~= "cmd" and k ~= "players" and k ~= "checked_locations" then self.room_info[k] = v end end
    end
    if #newly > 0 then self:event({kind = "LocationsChecked", locations = newly}) end
    self:event({kind = "RoomUpdate", data = p})
end

function Client:on_bounced(p)
    local deathlink = false
    for _, t in ipairs(arr(p.tags)) do if t == "DeathLink" then deathlink = true end end
    if deathlink then
        local d = type(p.data) == "table" and p.data or {}
        local t = type(d.time) == "number" and d.time or 0
        if self.last_death_sent >= 0 and math.abs(t - self.last_death_sent) < 1e-3 then return end   -- our own echo
        if not self:has_tag("DeathLink") then return end
        self:event({kind = "DeathLink", time = t, source = str(d.source), text = str(d.cause), data = p})
        return
    end
    if p.data == nil then return end   -- a keep-alive echo
    self:event({kind = "Bounced", data = p})
end

-- ---- game actions ----
function Client:check_locations(ids)
    local fresh = {}
    for _, id in ipairs(ids) do
        if not self.local_checks[id] then self.local_checks[id] = true fresh[id] = true end
    end
    if not next(fresh) or self.state ~= "connected" then return end   -- sent after the next login
    self:queue({cmd = "LocationChecks", locations = ids_list(fresh)})
end

function Client:scout_locations(ids, create_as_hint)
    create_as_hint = create_as_hint or 0
    local ask = {}
    for _, id in ipairs(ids) do
        self.scout_requests[id] = true
        if create_as_hint ~= 0 or not self.scouted[id] then ask[id] = true end
    end
    if not next(ask) or (self.state ~= "connected" and create_as_hint == 0) then return end
    self:queue({cmd = "LocationScouts", locations = ids_list(ask), create_as_hint = create_as_hint})
end

function Client:set_status(s)
    if self.status == M.Goal then return end   -- the goal is final
    self.status = s
    if self.state ~= "connected" then return end
    self:queue({cmd = "StatusUpdate", status = s})
end

function Client:say(t) self:queue({cmd = "Say", text = t}) end

function Client:has_tag(tag)
    for _, t in ipairs(self.cfg.tags) do if t == tag then return true end end
    return false
end

function Client:send_death(now_unix, cause)
    if not self:has_tag("DeathLink") or self.state ~= "connected" then return false end
    self.last_death_sent = now_unix
    local data = {time = now_unix, source = self:player_name(self.slot)}
    if cause and cause ~= "" then data.cause = cause end
    self:queue({cmd = "Bounce", tags = list({"DeathLink"}), data = data})
    return true
end

function Client:set_tags(tags)
    self.cfg.tags = tags
    if self.state ~= "connected" then return end   -- the next Connect carries them
    self:queue({cmd = "ConnectUpdate", tags = list(tags)})
end

function Client:keep_alive()
    if self.state ~= "connected" then return end
    self:queue({cmd = "Bounce", slots = list({self.slot})})
end

-- ---- names ----
function Client:player_name(slot)
    if slot == 0 then return "Archipelago" end
    for _, p in ipairs(self.players) do
        if p.team == self.team and p.slot == slot then return p.alias ~= "" and p.alias or p.name end
    end
    local s = self.slot_info[slot]
    if s and s.name ~= "" then return s.name end
    if slot == self.slot and (self.cfg.name or "") ~= "" then return self.cfg.name end
    return "Player " .. slot
end

function Client:game_of(slot)
    local s = self.slot_info[slot]
    if s then return s.game end
    if slot == self.slot then return self.cfg.game end
    return ""
end

function Client:is_self(slot)
    if slot == self.slot then return true end
    local s = self.slot_info[slot]
    if not s then return false end
    for _, m in ipairs(s.group_members) do if m == self.slot then return true end end
    return false
end

function Client:item_name(item, slot)
    local game = self:game_of(slot)
    if game == "" then game = self.cfg.game end
    for _, g in ipairs({game, "Archipelago"}) do
        local n = self.games[g]
        if n and n.items[item] then return n.items[item] end
    end
    return "Unknown item (ID: " .. tostring(item) .. ")"
end

function Client:location_name(loc, slot)
    local game = self:game_of(slot)
    if game == "" then game = self.cfg.game end
    for _, g in ipairs({game, "Archipelago"}) do
        local n = self.games[g]
        if n and n.locations[loc] then return n.locations[loc] end
    end
    return "Unknown location (ID: " .. tostring(loc) .. ")"
end

-- ---- the session: the socket, reconnects with backoff, the keep-alive, the status line ----------------
local Session = {}
Session.__index = Session

local refusals = {
    InvalidSlot = "no slot with that name in this room", InvalidGame = "that slot is not a Ys Origin slot",
    IncompatibleVersion = "the server wants a newer client version", InvalidPassword = "wrong or missing password",
    InvalidItemsHandling = "the server refused the item settings", SeedMismatch = "this room runs a different seed than the save",
}
function M.describe_refusal(e) return refusals[e] or e end

function M.session(store)
    return setmetatable({ws = cleria.net.websocket(), store = store, client = M.client({}, store), status = "idle",
                         events = {}, reason = "", delay = 2, retry_at = -1, want_open = false, last_send = 0, now = 0,
                         address = "", first_retry = 2, max_retry = 30, keepalive = 100}, Session)
end

function Session:connect(address, cfg)
    self.ws:close()
    self.address = address
    self.client = M.client(cfg, self.store)
    self.events, self.reason = {}, ""
    self.delay, self.retry_at, self.want_open = self.first_retry, -1, true
    self:open_socket(self.now)
end

function Session:open_socket(now)
    self.retry_at = -1
    self.status = "connecting"
    self.last_send = now
    if not self.ws:open(self.address) then
        self.status, self.reason, self.want_open = "disconnected", "not a valid server address", false
    end
end

function Session:disconnect()
    self.want_open, self.retry_at = false, -1
    self.ws:close()
    self.status, self.reason = "idle", ""
end

function Session:retry_login(cfg)
    self.client:retry_connect(cfg)
    if self.client.state == "await_connected" then self.status = "logging_in" end
end

function Session:take_events() local e = self.events self.events = {} return e end

function Session:update(now)
    self.now = now
    for _, e in ipairs(self.ws:poll()) do
        if e.kind == "open" then
            self.status = "logging_in"
            self.client:on_open()
        elseif e.kind == "message" then
            self.client:on_message(e.text)
        elseif e.kind == "close" then
            local refused = self.client.state == "refused"
            self.client:on_close()
            if not refused then self.reason = e.text end
            if not self.want_open then
                self.status = "idle"
            else
                self.status = refused and "refused" or "disconnected"
                if not refused then
                    self.retry_at = now + self.delay
                    self.delay = math.min(self.delay * 2, self.max_retry)
                end
            end
        end
    end
    for _, e in ipairs(self.client:take_events()) do
        if e.kind == "Connected" then
            self.status = "connected"
            self.delay = self.first_retry
        elseif e.kind == "Refused" or e.kind == "SeedMismatch" then
            self.status = "refused"
            local r = {}
            for _, x in ipairs(e.errors or {}) do r[#r + 1] = M.describe_refusal(x) end
            self.reason = table.concat(r, ", ")
        end
        self.events[#self.events + 1] = e
    end
    if self.status == "connected" and now - self.last_send >= self.keepalive then self.client:keep_alive() end
    for _, f in ipairs(self.client:take_outgoing()) do
        if self.ws:send(f) then self.last_send = now end
    end
    if self.want_open and self.retry_at >= 0 and now >= self.retry_at then
        local st = self.ws:state()
        if st == "closed" or st == "idle" then self:open_socket(now) end
    end
end

function Session:status_text(now)
    local s, c = self.status, self.client
    if s == "idle" then return "not connected" end
    if s == "connecting" then return "connecting to " .. self.ws:url() end
    if s == "logging_in" then
        return (c.state == "await_data_package" and "downloading item names from " or "logging in to ") .. self.ws:url()
    end
    if s == "connected" then return "connected as " .. c:player_name(c.slot) .. " (slot " .. c.slot .. ")" end
    if s == "refused" then return "auth failed: " .. self.reason end
    local t = "disconnected: " .. self.reason
    if self.retry_at >= 0 then t = t .. " (retrying in " .. math.max(0, math.ceil(self.retry_at - now)) .. " s)" end
    return t
end

-- A uuid v4 (made once per install and kept in the mod's settings).
function M.make_uuid()
    math.randomseed(math.floor(cleria.unix_time() * 1000) ~ math.floor(os.clock() * 1e6))
    local function hex(n) local t = {} for i = 1, n do t[i] = string.format("%x", math.random(0, 15)) end return table.concat(t) end
    return hex(8) .. "-" .. hex(4) .. "-4" .. hex(3) .. "-" .. string.format("%x", math.random(8, 11)) .. hex(3) .. "-" .. hex(12)
end

return M
