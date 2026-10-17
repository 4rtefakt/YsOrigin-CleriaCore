-- The Ys Origin side of an Archipelago game, in Lua: the location / item table (data/ys_origin.json, made
-- by CleriaCore's tools/ap_import.py from the unchanged apworld's data), the
-- slot_data options, the location registry and its detection, vanilla suppression, every receipt kind,
-- the New Game loadout, the save block, the goal and the EXP scaling. First written as CleriaCore's (since
-- removed) C++ ys_game.cpp (CleriaCore's AP_GAME.md, ARCHIPELAGO.md §5), ported onto cleria.game / cleria.player.
-- [MOD] = the retail mod YsOrigin-Archipelago/mod/src/hook_ap.cpp, as cited by the C++ original.

local M = {}

local kGFlagsRel, kItemArrRel = 0x36B91C, 0x36A654   -- retail g_flags / item_arr, module-relative
local kBlessFlag, kWeaponFlag, kRodaFruit = 217, 148, 0x57
-- the retail warp registry's statue order (table 0x68c190) [MOD hook_ap.cpp:1661-1664]
local kWarpScenes = {1000, 1009, 1011, 2000, 2013, 2100, 2012, 3000, 3006, 3015, 3014,
                     4000, 4104, 4020, 5000, 5010, 5014, 6000, 6010, 6082, 6053, 7000}

-- the flag each boss room's fight sets when it is won (the shipped scripts: the floor bosses' brains set
-- 220..224, the duels' BATTLE_* scripts 230 / 231 / 232 / 235 / 236, and S_5080's only fight, the ward of
-- four Zeruena, 423). S_4080 has no fight at all (two trap scripts): its check stays on entry. With mod
-- API 4 the engine's boss_defeated event says the same thing sooner (Logic:boss_defeated).
local kBossFlag = {[1099] = 220, [2099] = 221, [3099] = 222, [4099] = 223, [5099] = 224,
                   [1080] = 230, [2080] = 231, [3080] = 232, [6080] = 235, [6099] = 236, [5080] = 423}

-- the five SP chests: their script pays the SP with 0xB2 AddPlayerSP (pc 32) and then sets the box flag
-- (pc 35). The grant filter never sees that command, so the vanilla SP came on top of the seed's item:
-- box flag -> SP paid. S_3003/S_BOX02, S_4003/S_BOX04, S_4015/S_BOX01, S_5002/S_BOX01, S_6014/S_BOX01.
M.sp_chest = {[470] = 2000, [380] = 5000, [391] = 5000, [447] = 10000, [461] = 20000}

local G, P = cleria.game, cleria.player

local function to_int(v, def)
    if math.type(v) == "integer" then return v end
    if math.type(v) == "float" then return math.tointeger(math.floor(v)) or def end
    if type(v) == "string" and v ~= "" then return math.tointeger(tonumber(v:match("^%s*(%-?%d+)")) or def) or def end
    return def
end
local function ints(a)
    local t = {}
    for i, x in ipairs(type(a) == "table" and a or {}) do t[i] = to_int(x, -1) end
    return t
end
local function obj(v) return type(v) == "table" and v or {} end
local function hex_offset(v)
    if math.type(v) then return to_int(v, nil) end
    if type(v) ~= "string" or v == "" then return nil end
    local h = v:match("^%s*0[xX](%x+)") or v:match("^%s*(%x+)")
    return h and tonumber(h, 16) or nil
end
local function flag_of_offset(off)
    local d = off - kGFlagsRel
    if d < 0 or d % 4 ~= 0 or d // 4 >= 0x200 then return nil end
    return d // 4
end
function M.scene_number(s)
    local n = type(s) == "string" and s:match("^[Ss]_(%d+)")
    return n and tonumber(n) or 0
end
function M.warp_index_of_scene(scene)
    for i, s in ipairs(kWarpScenes) do if s == scene then return i - 1 end end
    return -1
end
function M.scene_of_warp_index(i) return kWarpScenes[i + 1] or 0 end
local function ore_tier(ore)   -- [MOD hook_ap.cpp:640]: 1..5 ore -> Lv2..Lv6
    local k = {1, 2, 4, 6, 8}
    return ore <= 0 and 0 or k[math.min(ore, 5)]
end
local function counted_item_cell(i) return i == 0x57 or i == 0x58 or i == 0x59 end   -- [MOD 925-937]
local function level_cell(i) return i >= 0xB6 and i <= 0xB8 end                       -- [MOD 929-931]
local function char_key(c) return c == 2 and "hugo" or c == 3 and "toal" or "yunica" end

-- ---- the table ------------------------------------------------------------------------------------------
function M.load_table(path)
    local text, err = cleria.read(path)
    if not text then return nil, err end
    local doc, perr = cleria.json.decode(text)
    if not doc then return nil, path .. ": " .. perr end
    if doc.format ~= 1 or doc.game ~= "Ys Origin" then return nil, "not a Ys Origin table (format 1)" end
    local t = {locations = {}, items = {}, loc = {}, item = {}, scene_names = {}, scene_floors = {}, chest_loc = {}, roo_loc = {}}
    for k, v in pairs(obj(doc.scene_names)) do t.scene_names[tonumber(k)] = v end
    for k, v in pairs(obj(doc.scene_floors)) do t.scene_floors[tonumber(k)] = to_int(v, 0) end
    for _, j in ipairs(doc.locations) do
        local d = obj(j.detect)
        local l = {id = j.id, name = j.name, key = j.key, type = j.type, zone = j.zone or "", floor = j.floor or "",
                   room = j.room, scene = to_int(j.scene, 0), detect = d.method or "unmapped", flag = to_int(d.flag, -1),
                   index = to_int(d.index, -1), bit = to_int(d.bit, -1), detect_scene = 0, detect_floor = 0}
        if l.detect == "scene" then l.detect_scene = to_int(d.scene, 0) end
        if l.detect == "floor" then l.detect_floor = to_int(d.floor, 0) end
        if type(j.chest) == "table" then
            l.box_flag, l.script = to_int(j.chest.box_flag, -1), j.chest.script
            t.chest_loc[string.upper(l.key)] = l.id
        end
        -- a Roo trade (S_nnnn/AGERU/<flag>): its room, and the items its vanilla "Acquired" box can show
        local roo_room = string.upper(l.key or ""):match("^(S_%d+)/AGERU/")
        if roo_room then
            local items = {}
            for _, v in ipairs(type(j.vanilla) == "table" and j.vanilla or {}) do
                local n = to_int(obj(v).item, -1)
                if n >= 0 and n < 0x80 then items[n] = true end   -- a game item (the Topaz, 0x301, is not one)
            end
            t.roo_loc[roo_room] = {id = l.id, items = items}
        end
        t.locations[#t.locations + 1] = l
        t.loc[l.id] = l
    end
    for _, j in ipairs(doc.items) do
        local it = {id = j.id, name = j.name, cls = j.class, kind = j.kind, flag = to_int(j.flag, -1),
                    companion_flag = to_int(j.companion_flag, -1), amount = to_int(j.amount, 0), scene = to_int(j.scene, 0),
                    bit = to_int(j.bit, -1), bits = ints(j.bits), index = to_int(j.index, -1), level_cell = to_int(j.level_cell, -1),
                    artifact = to_int(j.artifact, -1), power = to_int(j.power, -1), ladder = {}}
        for ch, lad in pairs(obj(j.ladder)) do it.ladder[ch] = ints(lad) end
        t.items[#t.items + 1] = it
        t.item[it.id] = it
    end
    table.sort(t.locations, function(a, b) return a.id < b.id end)
    return t
end

-- slot_data.location_detect (the retail form) -> a registry entry; false when nothing to watch
local function convert_detect(d, r)
    local m = d.method
    if m == "flag" then
        if d.flag ~= nil then r.detect, r.flag = "flag", to_int(d.flag, -1) return r.flag >= 0 end
        if d.index ~= nil then r.detect, r.index = "item_arr", to_int(d.index, -1) return r.index >= 0 end
        local off = hex_offset(d.offset)
        if not off then return false end
        local f = flag_of_offset(off)
        if f then r.detect, r.flag = "flag", f return true end
        local s = off - kItemArrRel
        if s >= 0 and s % 4 == 0 and s // 4 < 54 then r.detect, r.index = "item_arr", s // 4 return true end
        return false
    end
    if m == "item_arr" then r.detect, r.index = "item_arr", to_int(d.index, -1) return r.index >= 0 end
    if m == "scene" then
        r.detect = "scene"
        r.scene = math.type(d.scene) and d.scene or M.scene_number(d.scene)
        return r.scene > 0
    end
    if m == "bit" then
        local f = to_int(d.flag, -1)
        if f < 0 then local off = hex_offset(d.offset) f = off and flag_of_offset(off) or -1 end
        r.detect, r.flag, r.bit = "bit", f, to_int(d.bit, -1)
        return f >= 0 and r.bit >= 0 and r.bit < 32
    end
    if m == "floor" then r.detect, r.floor = "floor", to_int(d.floor, 0) return r.floor > 0 end
    return false
end

local function parse_slot_options(sd)
    local o = {character = sd.character ~= nil and to_int(sd.character, -1) or -1, goal = to_int(sd.goal, 0),
               suppress_items = {}, suppress_give_ids = {}, progressive_gear = {}, progressive_skills = {},
               progressive_blessings = {}, statue_unlock_flag = {}, unsupported = {}}
    if type(sd.death_link) == "boolean" then o.death_link = sd.death_link else o.death_link = to_int(sd.death_link, 0) ~= 0 end
    for _, i in ipairs(ints(sd.suppress_items)) do if i >= 0 and i < 0x200 then o.suppress_items[i] = true end end
    for _, i in ipairs(ints(sd.suppress_give_ids)) do o.suppress_give_ids[i] = true end
    for k, v in pairs(obj(sd.progressive_gear)) do o.progressive_gear[k] = ints(v) end
    for k, v in pairs(obj(sd.progressive_skills)) do
        o.progressive_skills[k] = {artifact = to_int(v.artifact, -1), power = to_int(v.power, -1), level_cell = to_int(v.level_cell, -1)}
    end
    for k, v in pairs(obj(sd.progressive_blessings)) do o.progressive_blessings[k] = ints(v) end
    o.statue_warp_locks = sd.statue_warp_locks == true
    for _, v in pairs(obj(sd.statue_unlocks)) do
        local scene, off = to_int(v.scene, 0), hex_offset(v.flag)
        local f = off and flag_of_offset(off)
        if scene > 0 and f then o.statue_unlock_flag[scene] = f end
    end
    o.start_statue_scene = to_int(sd.start_statue_scene, 0)
    o.start_items = ints(sd.start_items)
    o.start_level, o.start_weapon = to_int(sd.start_level, 0), to_int(sd.start_weapon, 0)
    o.level_scaling, o.level_margin = to_int(sd.level_scaling, 0), to_int(sd.level_margin, 0)
    o.exp_base_mult = math.max(1, to_int(sd.exp_base_mult, 1))
    o.exp_catchup_mult = math.max(1, to_int(sd.exp_catchup_mult, 1))
    o.exp_catchup_margin = to_int(sd.exp_catchup_margin, 0)
    o.scene_levels, o.scene_floors, o.scene_names = {}, {}, {}
    for k, v in pairs(obj(sd.scene_levels)) do o.scene_levels[tonumber(k)] = to_int(v, 0) end
    for k, v in pairs(obj(sd.scene_floors)) do o.scene_floors[tonumber(k)] = to_int(v, 0) end
    for k, v in pairs(obj(sd.scene_names)) do o.scene_names[tonumber(k)] = v end
    o.blessing_items = sd.blessing_items == true
    local shop_api = cleria.shop and cleria.shop.add_filter and true or false
    o.random_start = sd.random_start == true
    -- the flag each Roo sets when fed, in the apworld's logic order (2.0: the k-th Roo needs k fruits)
    o.roo_flags = {}
    for _, f in ipairs(ints(sd.roo_flags)) do if f >= 0 and f < 0x200 then o.roo_flags[#o.roo_flags + 1] = f end end
    -- blessing_items: the purchase is the check and its effect an item of the pool, so the shop filter keeps
    -- the effect from being applied and the purchase (not the effect bit) is what detects the check [MOD
    -- ap_on_blessing_purchase]. Without the shop hooks the vanilla effect comes with the check.
    o.bless_by_purchase = o.blessing_items and shop_api
    if o.blessing_items and not shop_api then
        o.unsupported[#o.unsupported + 1] = "Blessing items (this CleriaCore has no shop hooks: the vanilla effect is also given)"
    end
    -- the statue shop's seed prices, location id -> SP (the shop filter of mod API 5 charges them), and the
    -- same prices by the vanilla one, for a blessing that is not a location of this slot [MOD g_bless_price_map]
    o.blessing_costs, o.blessing_vanilla, o.shop_hints = {}, {}, sd.shop_hints ~= false
    for k, v in pairs(obj(sd.blessing_costs)) do o.blessing_costs[tonumber(k) or -1] = to_int(v, -1) end
    for k, v in pairs(obj(sd.blessing_vanilla_price_map)) do
        local van, new = tonumber(k), to_int(v, -1)
        if van and van > 0 and new >= 0 then o.blessing_vanilla[van] = new end
    end
    if (next(o.blessing_costs) or next(o.blessing_vanilla)) and not shop_api then
        o.unsupported[#o.unsupported + 1] = "Shuffled blessing prices (this CleriaCore has no shop hooks)"
    end
    -- floor -> the active location ids on it (the warp map's counts) [MOD ap_floor_count]
    o.floor_locations = {}
    for k, v in pairs(obj(sd.floor_locations)) do
        local f = to_int(k, 0)
        if f > 0 then o.floor_locations[f] = ints(v) end
    end
    o.apworld_version = type(sd.apworld_version) == "string" and sd.apworld_version or ""
    o.shop_unlock = to_int(sd.blessing_shop_unlock, 0)   -- 1: one more shop slot per distinct floor visited
    if o.shop_unlock ~= 0 and not shop_api then
        o.unsupported[#o.unsupported + 1] = "Blessing shop pacing (one per floor)"
    end
    return o
end

-- ---- the save block --------------------------------------------------------------------------------------
-- roda: Roda Fruits received so far (-1 = a save from before 0.2, whose fruits were counted into the cell);
-- spawned: the random-start warp was done (or is not owed: a save that had already started)
function M.new_state() return {seed = "", slot_name = "", slot = 0, applied = 0, checks = {}, claimed = {}, goal_sent = false,
                               started = false, saw_gameplay = false, ore = 0, prog = {}, statues = {}, roda = 0, held = {},
                               spawned = false, floors = {}} end

local function set_list(s)
    local t = {}
    for id in pairs(s) do t[#t + 1] = id end
    table.sort(t)
    return cleria.json.array(t)
end
local function list_set(a)
    local s = {}
    for _, id in ipairs(type(a) == "table" and a or {}) do s[id] = true end
    return s
end
function M.state_to_table(st)
    if st.seed == "" then return nil end
    return {seed = st.seed, slot = st.slot, slot_name = st.slot_name, applied = st.applied, checks = set_list(st.checks),
            claimed = set_list(st.claimed), goal = st.goal_sent, started = st.started, gameplay = st.saw_gameplay, ore = st.ore,
            prog = st.prog, statues = set_list(st.statues), roda = st.roda, spawned = st.spawned,
            floors = set_list(st.floors), held = set_list(st.held)}
end
function M.state_from_table(t)
    local st = M.new_state()
    if type(t) ~= "table" or type(t.seed) ~= "string" then return st end
    st.seed, st.slot, st.slot_name = t.seed, to_int(t.slot, 0), type(t.slot_name) == "string" and t.slot_name or ""
    st.applied = math.max(0, to_int(t.applied, 0))
    st.checks, st.claimed, st.statues = list_set(t.checks), list_set(t.claimed), list_set(t.statues)
    st.floors, st.held = list_set(t.floors), list_set(t.held)
    st.goal_sent, st.started, st.saw_gameplay = t.goal == true, t.started == true, t.gameplay == true
    st.ore = math.max(0, to_int(t.ore, 0))
    st.roda = t.roda == nil and -1 or math.max(-1, to_int(t.roda, -1))
    st.spawned = t.spawned == true or (t.spawned == nil and st.started)
    for k, v in pairs(type(t.prog) == "table" and t.prog or {}) do st.prog[k] = to_int(v, 0) end
    return st
end
function M.count(set) local n = 0 for _ in pairs(set) do n = n + 1 end return n end

-- The seed's logic as the apworld exports it (slot_data.logic, rules.export_logic): region 0 is the origin,
-- entrances {from, to, rule}, locations id -> {region, rule}; a rule is true / false, {"has", item, n},
-- {"reach", region}, {"all", ...} or {"any", ...}. -> the set of location ids in logic with the items `have`.
function M.in_logic(g, have)
    local reach = {[0] = true}
    local function ev(r)
        if type(r) ~= "table" then return r == true end
        local k = r[1]
        if k == "has" then return (have[r[2]] or 0) >= r[3] end
        if k == "reach" then return reach[r[2]] == true end
        if k == "all" then for i = 2, #r do if not ev(r[i]) then return false end end return true end
        if k == "any" then for i = 2, #r do if ev(r[i]) then return true end end return false end
        return false
    end
    local grew = true
    while grew do
        grew = false
        for _, e in ipairs(g.entrances) do
            if reach[e[1]] and not reach[e[2]] and ev(e[3]) then reach[e[2]], grew = true, true end
        end
    end
    local out = {}
    for id, l in pairs(g.locations) do
        if reach[l[1]] and ev(l[2]) then out[tonumber(id)] = true end
    end
    return out
end

-- ---- the logic ---------------------------------------------------------------------------------------------
local Logic = {}
Logic.__index = Logic

function M.logic() return setmetatable({st = M.new_state(), configured = false, opt = parse_slot_options({}), regs = {},
                                         by_id = {}, by_scene = {}, by_floor = {}, scene = 0, expected_hi = 0}, Logic) end

function Logic:configure(sd, table_)
    self.tbl = table_
    self.opt = parse_slot_options(sd)
    self.regs, self.by_id, self.by_scene, self.by_floor, self.by_room = {}, {}, {}, {}, {}
    local function add(r)
        if self.by_id[r.id] then return end
        self.regs[#self.regs + 1] = r
        self.by_id[r.id] = r
        local l = table_.loc[r.id]   -- the room a location is in, for the "left in this room" list
        if l and l.scene > 0 then local t = self.by_room[l.scene] or {} t[#t + 1] = r self.by_room[l.scene] = t end
        if r.detect == "scene" then local l = self.by_scene[r.scene] or {} l[#l + 1] = r self.by_scene[r.scene] = l end
        if r.detect == "floor" then local l = self.by_floor[r.floor] or {} l[#l + 1] = r self.by_floor[r.floor] = l end
    end
    local function from_table(l, r)
        r.detect, r.flag, r.index, r.bit, r.scene, r.floor = l.detect, l.flag, l.index, l.bit, l.detect_scene, l.detect_floor
        return l.detect ~= "unmapped" and l.detect ~= nil
    end
    local sig = obj(sd.location_signals)
    if next(sig) then   -- the ACTIVE locations of this slot, their detect as the seed published it
        local names = {}
        for name in pairs(sig) do names[#names + 1] = name end
        table.sort(names)   -- the C++ registry is in name order (a std::map)
        local det = obj(sd.location_detect)
        for _, name in ipairs(names) do
            local r = {id = to_int(sig[name], 0), name = name, flag = -1, index = -1, bit = -1, scene = 0, floor = 0}
            local ok = type(det[name]) == "table" and convert_detect(det[name], r)
            if not ok and table_.loc[r.id] then ok = from_table(table_.loc[r.id], r) end
            if ok then add(r) end
        end
    else
        for _, l in ipairs(table_.locations) do
            local r = {id = l.id, name = l.name, flag = -1, index = -1, bit = -1, scene = 0, floor = 0}
            if from_table(l, r) then add(r) end
        end
    end
    if not next(self.opt.scene_floors) then self.opt.scene_floors = table_.scene_floors end
    if not next(self.opt.floor_locations) then   -- an older seed: the floor of each active location's room
        for _, r in ipairs(self.regs) do
            local l = table_.loc[r.id]
            local f = l and (tonumber(l.floor:match("^(%d+)F")) or self.opt.scene_floors[l.scene])
            if f and f > 0 then
                local t = self.opt.floor_locations[f] or {}
                t[#t + 1] = r.id
                self.opt.floor_locations[f] = t
            end
        end
    end
    if not next(self.opt.scene_names) then self.opt.scene_names = table_.scene_names end
    self.configured = true
end

function Logic:reg(id) return self.by_id[id] end

-- "Boss checks on defeat" (the mod's option): a boss room's check waits for the fight's flag instead of
-- firing at the door. Since apworld 2.2 the seed says so itself (a boss check arrives as a flag detect and
-- can hold any item), so this only acts on older seeds and on the checks the apworld left on entry. Toal's
-- S_3080 is one of those: his 232 is set in S_4021, behind Rado's Annex, which would be out of logic.
function Logic:boss_flag(r)
    if not self.boss_on_kill or r.detect ~= "scene" then return nil end
    if r.scene == 3080 and G.flag(150) == 3 then return nil end
    local l = self.tbl and self.tbl.loc[r.id]
    return (l and l.type == "boss") and kBossFlag[r.scene] or nil
end

-- the engine's boss_defeated event (mod API 4): the boss checks of that room, when they wait for the win.
-- A fight won, rolled back by a Retry and won again raises it twice: Logic:fire dedupes on the save's checks.
function Logic:boss_defeated(sc)
    local out = {}
    for _, r in ipairs(self.by_scene[sc] or {}) do
        if self:boss_flag(r) then self:fire(r.id, out) end
    end
    return out
end

-- the active locations of a room that are still to find
function Logic:left_in_room(sc)
    local out = {}
    for _, r in ipairs(self.by_room[sc] or {}) do
        if not self.st.checks[r.id] then out[#out + 1] = r end
    end
    return out
end

function Logic:fire(id, out)
    self.st.claimed[id] = nil
    if self.st.checks[id] then return end
    self.st.checks[id] = true
    out[#out + 1] = id
end

-- a blessing's bit no longer detects its check when the effects are items: the purchase does (Logic:on_blessing),
-- and the bit is set by receiving the effect, which says nothing about the shop slot
function Logic:bit_by_purchase(r) return self.opt.bless_by_purchase and r.flag == kBlessFlag end

-- per tick: flag / item_arr / bit locations set behind the VM
function Logic:sweep()
    local out = {}
    if not G.in_game() then return out end
    local st = self.st
    for _, r in ipairs(self.regs) do
        if not st.checks[r.id] and not st.claimed[r.id] then
            local on = false
            if r.detect == "flag" then on = r.flag >= 0 and r.flag < 0x200 and G.flag(r.flag) >= 1
            elseif r.detect == "item_arr" then on = r.index >= 0 and r.index < 54 and (G.upgrade(r.index) or 0) >= 1
            elseif r.detect == "bit" and not self:bit_by_purchase(r) then
                on = r.flag >= 0 and r.flag < 0x200 and ((G.flag(r.flag) & 0xFFFFFFFF) >> r.bit) & 1 ~= 0
            end
            if on then st.checks[r.id] = true out[#out + 1] = r.id end
        end
    end
    if self.boss_on_kill then
        for sc, flag in pairs(kBossFlag) do
            if G.flag(flag) >= 1 then
                for _, r in ipairs(self.by_scene[sc] or {}) do
                    if self:boss_flag(r) and not st.checks[r.id] then st.checks[r.id] = true out[#out + 1] = r.id end
                end
            end
        end
    end
    return out
end

-- a room entry: scene + floor locations
function Logic:on_scene(sc)
    local out = {}
    self.scene = sc
    -- the 7002 goal guard [MOD 461-469]. The summit (S_7000..) is 7xxx as well: it is gameplay unless it is
    -- Toal's intro, which runs there at level 1 (the retail mod's 2.0.1 fix).
    if (sc >= 1000 and sc <= 6999) or (sc >= 7000 and sc <= 7999 and (P.level() or 0) >= 2) then self.st.saw_gameplay = true end
    local lv = self.opt.scene_levels[sc]
    if lv then self.expected_hi = math.max(self.expected_hi, lv) end
    for _, r in ipairs(self.by_scene[sc] or {}) do
        if not self:boss_flag(r) then self:fire(r.id, out) end   -- else: the sweep, once the fight is won
    end
    local f = self.opt.scene_floors[sc]
    if f then
        self.st.floors[f] = true   -- the shop's one-per-floor pacing
        for _, r in ipairs(self.by_floor[f] or {}) do self:fire(r.id, out) end
    end
    return out
end

-- a VM store (the grant filter): a store of 0 is not a check (ARCHIPELAGO.md §7.3.4)
function Logic:on_store(index, old, value)
    local out = {}
    if index < 0 or index >= 0x200 then return out end
    for _, r in ipairs(self.regs) do
        if r.flag == index then
            if r.detect == "flag" and value >= 1 and old < 1 then self:fire(r.id, out) end
            if r.detect == "bit" and not self:bit_by_purchase(r) and (((value & 0xFFFFFFFF) & ~(old & 0xFFFFFFFF)) >> r.bit) & 1 ~= 0 then self:fire(r.id, out) end
        end
    end
    return out
end

function Logic:suppress_store(index, old, value)
    if not self.configured or value <= math.max(old, 0) then return false end   -- only a grant (a rise)
    if self.opt.suppress_items[index] then return true end
    if self.opt.statue_warp_locks then   -- a locked statue stays dark [MOD hook_ap.cpp:583-597]
        for sc, flag in pairs(self.opt.statue_unlock_flag) do
            if flag == index and sc ~= self.opt.start_statue_scene and not self.st.statues[sc] then return true end
        end
    end
    return false
end
function Logic:suppress_give(item) return self.opt.suppress_give_ids[item] == true end

-- the location behind a statue-shop row: the script's blessing nn is bit nn (0..6) or nn - 2 (9..25) of
-- flag 217; 7 / 8 are the armor / leggings rows, whose location is the equipped piece's upgrade cell
-- (g_flags 152 / 153 hold the equipped piece's item index [MOD kArmorSelAbs / kBootsSelAbs])
function Logic:bless_reg(index)
    if index == 7 or index == 8 then
        local sel = G.flag(index == 7 and 152 or 153)
        for _, r in ipairs(self.regs) do
            if r.detect == "item_arr" and r.index == sel then return r end
        end
        return nil
    end
    local bit = (index >= 0 and index <= 6) and index or (index >= 9 and index <= 25) and index - 2 or -1
    if bit < 0 then return nil end
    for _, r in ipairs(self.regs) do
        if r.detect == "bit" and r.flag == kBlessFlag and r.bit == bit then return r end
    end
end

-- one_per_floor: the priced slots, cheapest first; slot i is on sale once i distinct floors + 1 were visited.
-- A slot holding progression is never held back [MOD shop_item_unlocked]. -> the floors still to visit, or nil.
function Logic:shop_locked(r, progression)
    if self.opt.shop_unlock ~= 1 or progression or not self.opt.blessing_costs[r.id] then return nil end
    local cost, rank = self.opt.blessing_costs, 0
    for id, c in pairs(cost) do
        if c < cost[r.id] or (c == cost[r.id] and id < r.id) then rank = rank + 1 end
    end
    local need = rank + 1 - M.count(self.st.floors)
    return need > 0 and need or nil
end

-- The statue shop (cleria.shop.add_filter, mod API 5): the answer to one query. `scouted` is the client's
-- location id -> {item, player, flags}; `label(s)` names a scouted item for the row.
--  * price: the seed's price for the row's location (blessing_costs), else by the vanilla price
--    (blessing_vanilla_price_map); a slot one_per_floor has not put on sale yet costs more than any wallet.
--    The armor / leggings ladder keeps its own prices: the seed rolls none for them [MOD ap_menu_row].
--  * row: the price, and what the location holds when shop hints are on.
--  * owned / buy, with blessing_items: a blessing is "applied" once its location is checked, whatever was
--    received, so a bought row reads [Done] and a received effect does not take a slot off sale; the purchase
--    applies nothing (the effect is an item) [MOD ap_bless_compare_price, ap_on_blessing_purchase].
function Logic:shop_answer(q, scouted, label)
    local o = self.opt
    local gear = q.index == 7 or q.index == 8
    local r = self:bless_reg(q.index)
    local takes = o.bless_by_purchase and r ~= nil and not gear
    if q.kind == "owned" then
        if not takes then return nil end
        return {action = "replace", value = self.st.checks[r.id] and 1 or 0}
    end
    if q.kind == "buy" then return takes and "suppress" or nil end
    if q.kind ~= "price" and q.kind ~= "row" then return nil end
    if gear and not r then return nil end
    local price = not gear and r and o.blessing_costs[r.id] or nil
    if price and price < 0 then price = nil end
    if not r then   -- a blessing that is no location of this slot: only the vanilla price map applies
        if q.kind ~= "price" or gear then return nil end
        local v = o.blessing_vanilla[q.value]
        return v and {action = "replace", value = v} or nil
    end
    if not price and not gear and q.kind == "price" then price = o.blessing_vanilla[q.value] end
    local s = scouted and scouted[r.id]
    local need = price and o.blessing_costs[r.id] and self:shop_locked(r, s ~= nil and (s.flags or 0) & 1 == 1)
    if q.kind == "price" then
        if need then return {action = "replace", value = 1000000} end
        return price and {action = "replace", value = price} or nil
    end
    if q.value < 0 then return nil end                      -- a "[Done]" row
    if need then
        return {action = "replace", text = string.format("Locked: visit %d more floor%s", need, need == 1 and "" or "s")}
    end
    local what = o.shop_hints and s and not self.st.checks[r.id] and label and label(s) or nil
    if not what then
        if not price then return nil end
        return {action = "replace", text = (q.text:gsub("%d+%s*$", tostring(price)))}
    end
    return {action = "replace", text = what .. " - [SP:]" .. (price or q.value)}
end

-- blessing_bought: the purchase of a blessing is its location's check (with blessing_items the only way it is
-- detected; otherwise the effect bit says the same a tick later)
function Logic:on_blessing(index)
    local out = {}
    if index == 7 or index == 8 then return out end      -- the gear upgrades: their cell detects them
    local r = self:bless_reg(index)
    if r then self:fire(r.id, out) end
    return out
end

function Logic:claim_cell(index)
    for _, r in ipairs(self.regs) do
        if r.detect == "flag" and r.flag == index and not self.st.checks[r.id] then self.st.claimed[r.id] = true end
    end
end
function Logic:claim_bit(bit)
    for _, r in ipairs(self.regs) do
        if r.detect == "bit" and r.flag == kBlessFlag and r.bit == bit and not self.st.checks[r.id] then self.st.claimed[r.id] = true end
    end
end

-- a key item is SET to 1 (the scripts test ==), a counter adds [MOD hook_ap.cpp:952-966]
function Logic:give(index, stack, log)
    if index < 0 or index >= 0x200 then return false end
    local was = G.flag(index)
    local c
    if level_cell(index) then c = math.min(3, math.max(was, 0) + 1)
    elseif stack then c = math.max(was, 0) + 1
    else c = 1 end
    G.set_flag(index, c)
    self:claim_cell(index)
    log[#log + 1] = string.format("g_flags[0x%X] %d -> %d", index, was, c)
    return c ~= was
end

local function set_bit(bit)
    G.set_flag(kBlessFlag, G.flag(kBlessFlag) | (1 << bit))
end

-- one received item into the game (ARCHIPELAGO.md §5.2; the mod's §7.3 bugs not copied).
-- Returns {text, trap, known}.
function Logic:grant(item, flags)
    local it = self.tbl and self.tbl.item[item]
    if not it or not G.in_game() then return {known = false, text = "unknown item " .. tostring(item) .. ": skipped"} end
    local g = {known = true}
    local log = {}
    local k = it.kind
    if k == "item" then
        local i = it.flag
        local stack
        if i >= 0x40 and i <= 0x76 then stack = counted_item_cell(i) else stack = (flags & 3) == 0 end
        if i == kRodaFruit and self:roda_derived() then
            -- the cell is derived from this count (Logic:enforce), never added to
            self.st.roda = self.st.roda + 1
            log[1] = "Roda Fruit #" .. self.st.roda
        else
            self:give(i, stack, log)
        end
        if it.companion_flag >= 0 then self:give(it.companion_flag, false, log) end   -- [MOD 1855-1878]
    elseif k == "sp" then   -- the live wallet, not g_flags[0xD8] [MOD 1784-1790]
        P.add_sp(it.amount)
        log[1] = "SP +" .. it.amount
    elseif k == "trap" then
        g.trap = it.name
        log[1] = "trap " .. it.name
    elseif k == "statue_unlock" then   -- [MOD 1804-1813]
        self.st.statues[it.scene] = true
        local w = M.warp_index_of_scene(it.scene)
        if w >= 0 then G.set_warp_unlocked(w, true) end
        if it.flag >= 0 and it.flag < 0x200 then G.set_flag(it.flag, 1) self:claim_cell(it.flag) end
        log[1] = "statue S_" .. it.scene .. " unlocked (warp " .. w .. ")"
    elseif k == "progressive_gear" then   -- the first tier not owned [MOD 1791-1803]
        local ladder = self.opt.progressive_gear[it.name]
        if not ladder or #ladder == 0 then ladder = it.ladder[char_key(G.character())] or {} end
        local done = false
        for _, cell in ipairs(ladder) do
            if cell >= 0 and cell < 0x200 and G.flag(cell) < 1 then self:give(cell, false, log) done = true break end
        end
        if not done then log = {"every tier already owned"} end
    elseif k == "progressive_skill" then   -- [MOD 1199-1220]; the count is in the save (§7.3.2 fixed)
        local ps = self.opt.progressive_skills[it.name] or {artifact = it.artifact, power = it.power, level_cell = it.level_cell}
        local n = (self.st.prog[it.name] or 0) + 1
        self.st.prog[it.name] = n
        if n == 1 then
            self:give(ps.artifact, false, log)
            self:give(ps.power, false, log)
        elseif ps.level_cell >= 0 then
            self:give(ps.level_cell, true, log)
        end
    elseif k == "progressive_blessing" then   -- the next unset bit of the chain [MOD 1159-1172]
        local bits = self.opt.progressive_blessings[it.name] or it.bits
        log = {"every tier already granted"}
        for _, b in ipairs(bits) do
            if b >= 0 and b <= 31 and ((G.flag(kBlessFlag) & 0xFFFFFFFF) >> b) & 1 == 0 then
                set_bit(b)
                self:claim_bit(b)
                log = {"blessing bit " .. b}
                break
            end
        end
    elseif k == "cleria_ore" then   -- the weapon tier, not the item cell [MOD 629-641, 1814-1820]
        self.st.ore = self.st.ore + 1
        local tier = ore_tier(self.st.ore)
        if G.flag(kWeaponFlag) < tier then P.set_weapon_tier(tier) end
        log[1] = "Cleria Ore #" .. self.st.ore .. ": weapon tier " .. tier
    elseif k == "gem" then   -- the level + 1, max 3, also with progressive skills off (§7.3.1 fixed)
        self:give(it.level_cell, true, log)
    elseif k == "blessing" then
        if it.bit >= 0 and it.bit < 32 then set_bit(it.bit) self:claim_bit(it.bit) log[1] = "blessing bit " .. it.bit end
    else
        g.known = false
        log = {"no grant rule: skipped"}
    end
    if k ~= "trap" then P.refresh() end
    g.text = it.name .. ": " .. table.concat(log, ", ")
    return g
end

-- the New Game loadout of slot_data (start_items, start_weapon, start_level)
function Logic:start_loadout()
    if self.st.started or not self.configured or not G.in_game() then return nil end
    self.st.started = true
    local log = {}
    for _, i in ipairs(self.opt.start_items) do
        if i >= 0 and i < 0x200 and G.flag(i) < 1 then self:give(i, false, log) end
    end
    if self.opt.start_weapon > G.flag(kWeaponFlag) then
        P.set_weapon_tier(self.opt.start_weapon)
        log[#log + 1] = "weapon tier " .. self.opt.start_weapon
    end
    if self.opt.start_level > 1 and P.level() < self.opt.start_level then
        P.set_level(self.opt.start_level)
        log[#log + 1] = "level " .. self.opt.start_level
    end
    P.refresh()
    return #log == 0 and "nothing to give" or table.concat(log, ", ")
end

-- Random start (slot_data random_start + start_statue_scene): once per save, a New Game is warped to the
-- seed's start statue. Returns the warp index and the scene while that warp is still owed [MOD force_spawn].
function Logic:spawn_warp()
    local o, st = self.opt, self.st
    if st.spawned or not self.configured then return nil end
    local sc = o.start_statue_scene
    local w = M.warp_index_of_scene(sc)
    if not o.random_start or sc <= 0 or w < 0 or self.scene == sc then st.spawned = true return nil end
    return w, sc
end
function Logic:spawn_done(sc)
    self.st.spawned = true
    local f = self.opt.statue_unlock_flag[sc]
    if f and G.flag(f) ~= 1 then G.set_flag(f, 1) self:claim_cell(f) end   -- the statue is lit: save and warp work
end

-- Roda Fruit is derived when the seed publishes roo_flags (apworld 2.0) and the save counted its fruits
-- from the start. Logic says the k-th Roo (tower order) needs k fruits, but the Roos can be fed in any
-- order, and a fruit spent on a later one starved an earlier one that logic had promised. So with N fruits
-- received the cell holds the unfed Roos among the first N: a Roo beyond N is fed for free, one within N
-- always finds its fruit [MOD reconcile_roda_fruit].
function Logic:roda_derived() return #self.opt.roo_flags > 0 and self.st.roda >= 0 end

-- Toal and Rado's Annex. Two of his story flags, 269 (set on 20F, EV_5102_Thor_1) and 289 (set above the 21F
-- boss, EV5097_Thor), close the annex to him: its bridge (S_4104 Kadena_Stopper -> Thor_Stopper00 / 01) and
-- the room before it (S_4014 s_Stop4014_Thor) walk him back, and S_4104's room script takes that branch
-- before the one that carries on from the Shion fight (206 == 1 -> EV_4104_Thor), so the fight's chain
-- (S_4104 -> S_4017 -> S_4021, which sets 232) never ends and S_3080 starts the fight again on every entry.
-- A seed reaches those floors in any order, so both flags are held at 0 while he is in these rooms (the
-- stoppers test them when he steps on the trigger) and put back when he leaves.
local kToalAnnex = {[3080] = true, [4013] = true, [4014] = true, [4016] = true, [4017] = true, [4021] = true, [4104] = true}
function Logic:toal_annex(scene)
    local here, out = kToalAnnex[scene] == true, nil
    for _, f in ipairs({269, 289}) do
        if here and G.flag(f) == 1 then
            G.set_flag(f, 0) self.st.held[f] = true
            out = "Rado's Annex: Toal's story flag " .. f .. " held at 0 in here (the annex stays open to him)"
        elseif not here and self.st.held[f] then
            G.set_flag(f, 1) self.st.held[f] = nil
            out = "Rado's Annex: Toal's story flag " .. f .. " put back"
        end
    end
    return out
end

-- the warp map's count for a floor: the floor's active locations found (by this save or, co-op, the server)
-- and their number; nil when the floor has none [MOD ap_floor_count]
function Logic:floor_count(f, server_checked)
    local ids = self.opt.floor_locations[f]
    if not ids or #ids == 0 then return nil end
    local found = 0
    for _, id in ipairs(ids) do
        if self.st.checks[id] or (server_checked and server_checked[id]) then found = found + 1 end
    end
    return found, #ids
end

-- Hugo's Eyes of Fact are hidden by 211 == 1 while a scene runs; a scene torn down before its tail (a room
-- change in a skipped cutscene) leaves them hidden: no shots, no flames, until a later script clears it. With
-- the controls in the player's hands (185 == 1) and no fight running (156 == 0) nothing keeps them hidden:
-- after 3 s of that the flag is put back [MOD repair_hugo_eyes].
function Logic:hugo_eyes(ticks)
    if not (G.flag(211) == 1 and G.flag(185) == 1 and G.flag(156) == 0 and (P.hp() or 0) > 0) then
        self.eyes_since = nil
        return nil
    end
    self.eyes_since = self.eyes_since or ticks
    if ticks - self.eyes_since < 3 * 60 then return nil end
    self.eyes_since = nil
    G.set_flag(211, 0)
    return "Hugo's Eyes of Fact were left hidden (211 = 1) with the controls free: cleared"
end

-- per tick: repairs of story states a randomized seed can strand [MOD repair_1f_burden_softlock,
-- repair_dreaming_idol_chain, repair_hugo_eyes]. The first two are Yunica's, the third Hugo's; all follow
-- from the shipped scripts.
function Logic:repair(scene, ticks)
    if G.flag(150) == 3 then return self:toal_annex(scene) end
    if G.flag(150) == 2 then return self:hugo_eyes(ticks) end
    if G.flag(150) ~= 1 then return nil end
    -- After the Kishgal duel she is sent to 1F ("I'm just a burden", 242 == 1 and 243 == 0): the stairs and
    -- the Crystal refuse until the Roy scene, which needs the four 1F talks (278 == 4). Each talk sits under
    -- newer story lines, so a later flag (or an early Lotusblade) hides it and 278 never gets there.
    if scene == 1000 and G.flag(242) == 1 and G.flag(243) == 0 then
        if G.flag(278) ~= 4 then
            G.set_flag(278, 4)
            for f = 284, 287 do G.set_flag(f, 1) end
            self.burden_since = ticks
            return "post-Kishgal 1F state: the four 1F talks marked done (278 = 4); the south slope plays the Roy scene"
        end
        self.burden_since = self.burden_since or ticks
        if ticks - self.burden_since >= 60 * 60 then   -- still stuck a minute later: the scene's own result
            G.set_flag(243, 1) G.set_flag(185, 1) G.set_flag(180, 1)
            self.burden_since = nil
            return "post-Kishgal 1F state held for a minute: 243 / 185 / 180 set, the stairs and the Crystal work again"
        end
    else
        self.burden_since = nil
    end
    -- The Dreaming Idol: Dino's gift is skipped once a later story flag is set, and a charged idol can be
    -- left with no usable copy.
    local feena = G.flag(293) == 1 or G.flag(294) == 1
    local blocked = G.flag(225) == 1 or G.flag(235) == 1 or G.flag(237) == 1
    if feena and G.flag(296) == 0 and blocked then
        if G.flag(105) < 1 then G.set_flag(105, 1) end
        G.set_flag(296, 1)
        return "Dreaming Idol: Dino's gift was blocked by a later story flag, granted (105 = 1, 296 = 1)"
    end
    if G.flag(104) == 1 and G.flag(270) == 1 and G.flag(271) == 0 and G.flag(105) < 1 then
        G.set_flag(105, 1)
        return "Dreaming Idol: the charged idol had no usable copy, set 105 = 1"
    end
    return nil
end

-- per tick: the ore's weapon tier, the item-cell invariants, the fruit count, the statue warp locks
-- [MOD 2510-2590, enforce_item_cell_invariant]
function Logic:enforce()
    if not self.configured or not G.in_game() then return end
    local tier = ore_tier(self.st.ore)
    if tier > 0 and G.flag(kWeaponFlag) < tier then P.set_weapon_tier(tier) end
    -- ...and never above what Archipelago granted: some vanilla scripts raise the weapon with no item at
    -- all (the 4F Roo's reward calls 0x7F SetWeaponLevel), so swallowing the ore does not stop them and
    -- the player got a free tier on top of the seed's item. Every Cleria Ore source is a randomized
    -- location, so any excess is that [MOD the clamp in exp_scaling_on_frame].
    local allowed = math.max(tier, self.st.started and self.opt.start_weapon or 0)
    if self.st.started and G.flag(kWeaponFlag) > allowed then P.set_weapon_tier(allowed) end
    -- a key item above 1 fails every script that tests it with ==; a skill level above 3 breaks MP regen
    for i = 0x40, 0x76 do
        if not counted_item_cell(i) and G.flag(i) > 1 then G.set_flag(i, 1) end
    end
    for i = 0xB6, 0xB8 do if G.flag(i) > 3 then G.set_flag(i, 3) end end
    if self:roda_derived() then
        local want = 0
        for k, f in ipairs(self.opt.roo_flags) do
            if k > self.st.roda then break end
            if G.flag(f) < 1 then want = want + 1 end
        end
        local cur = G.flag(kRodaFruit)
        if cur ~= want and not (want == 0 and cur < 1) then G.set_flag(kRodaFruit, want) end   -- -1 = never held
    end
    if not self.opt.statue_warp_locks then return end
    for sc, flag in pairs(self.opt.statue_unlock_flag) do
        local w = M.warp_index_of_scene(sc)
        if w >= 0 then
            if self.st.statues[sc] then
                G.set_warp_unlocked(w, true)
                if G.flag(flag) ~= 1 then G.set_flag(flag, 1) end
            elseif sc ~= self.opt.start_statue_scene then
                G.set_warp_unlocked(w, false)
            end
        end
    end
end

-- the goal [MOD hook_ap.cpp:476-519, 1962-1977]
function Logic:goal_reached(sc)
    if not G.in_game() then return false end
    local c = G.flag(150)
    local final = ((c == 1 or c == 2) and G.flag(226) >= 1) or (c == 3 and G.flag(227) >= 2) or (sc == 7002 and self.st.saw_gameplay)
    if not final then return false end
    if self.opt.goal == 1 then
        for i = 220, 225 do if G.flag(i) < 1 then return false end end
    end
    return true
end

-- EXP scaling: level_scaling 2/3 multiply, 1/3 raise the level [MOD 2537-2560]. `base` / `catchup` (> 0) replace the
-- seed's two multipliers, as the retail client's exp_mult_base / exp_mult_catchup do.
function Logic:exp_factor(level, base, catchup)
    local o = self.opt
    if o.level_scaling ~= 2 and o.level_scaling ~= 3 then return 1 end
    local catching_up = self.expected_hi > 0 and level <= self.expected_hi + o.exp_catchup_margin
    if catching_up then return (catchup and catchup > 0) and catchup or o.exp_catchup_mult end
    return (base and base > 0) and base or o.exp_base_mult
end
function Logic:level_floor(level)
    local o = self.opt
    if o.level_scaling ~= 1 and o.level_scaling ~= 3 then return 0 end
    local lv = o.scene_levels[self.scene]
    if not lv or lv <= 0 then return 0 end
    local target = math.min(60, lv - o.level_margin)
    return (target > level and target > 1) and target or 0
end

return M
