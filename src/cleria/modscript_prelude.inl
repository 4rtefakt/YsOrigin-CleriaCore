// The parts of the mod API that are written in Lua (Mod API 7, LUA_API.md §12.20): run once per mod when the API
// opens, with the cleria table as its argument. Everything here is built from the calls a mod could make itself:
// it is in the engine so that every mod gets the same shop, bag, skills, rooms and characters, tested once.
// Each literal stays under the compilers' limit for one string (MSVC: 16 KiB); they are joined before loading.
static const char* const kPrelude[] = {
R"LUA(
local cleria = ...
local C, W, G, P, UI, E, S = cleria.content, cleria.world, cleria.game, cleria.player, cleria.ui, cleria.events, cleria.save

-- ---- cleria.save.card: a line of the mod's on this save's card (the Save / Load pages) ------------------------
function S.card(text)
    if not S.ready() then return false end
    if text == nil or text == "" then S.set("cleria.card", nil) else S.set("cleria.card", tostring(text):sub(1, 40)) end
    return true
end

-- ---- cleria.ui.console: type a command of any mod, read what the mods logged --------------------------------
function UI.console()
    local line, note = "", nil
    UI.window("cleria_console", {title = "Console", width = 0.6, pause = false}, function(p)
        local text, kept = p:text_field("Command", line, {empty = "name arguments", desc = "A cleria.command of any mod. Enter runs it."})
        if kept then
            local name, args = text:match("^%s*(%S+)%s*(.-)%s*$")
            if name then
                note = (not cleria.mods.command(name, args)) and ("no command '" .. name .. "'") or nil
                line = ""
            end
        else
            line = text
        end
        if note then p:text(note, {color = 0xE06050}) end
        p:info("Commands", table.concat(cleria.mods.commands(), "  "))
        p:section("Log")
        for _, l in ipairs(cleria.mods.log_lines(12)) do p:text(l) end
        if p:button("Close") then p:close() end
    end)
    UI.open("cleria_console")
end

-- ---- cleria.ui.shop: a window that sells for SP ---------------------------------------------------------------
function UI.shop(def)
    assert(type(def) == "table" and type(def.items) == "table", "ui.shop: {items = {...}} is required")
    local id = def.id or "shop"
    UI.window(id, {title = def.title or "Shop", width = def.width or 0.42}, function(p)
        if def.text then p:text(def.text) end
        p:info(def.currency or "SP", tostring(P.sp()))
        for i, it in ipairs(def.items) do
            local price, stock = it.price or 0, it.stock
            local can = P.sp() >= price and (stock == nil or stock > 0)
            if can and it.enabled ~= nil then
                if type(it.enabled) == "function" then can = it.enabled(it) and true or false else can = it.enabled and true or false end
            end
            local label = tostring(it.name) .. "  -  " .. price
            if stock ~= nil then label = label .. "  (" .. stock .. ")" end
            if p:button(label, {desc = it.desc, enabled = can}) then
                P.add_sp(-price)
                if stock then it.stock = stock - 1 end
                if it.buy then it.buy(it, i) end
                if def.on_buy then def.on_buy(it, i) end
            end
        end
        if p:button(def.leave or "Leave") then
            p:close()
            if def.on_close then def.on_close() end
        end
    end)
    UI.open(id)
    return id
end

-- ---- cleria.content.define_item: items of the mod's own, kept in the save, with a bag window ------------------
local items, item_order, item_hook, items_changed = {}, {}, false, nil
local function bag()
    local b = S.get("cleria.items")
    if type(b) ~= "table" then b = {} end
    return b
end
function C.define_item(id, def)
    assert(type(id) == "string" and type(def) == "table", "define_item(id, {name =, ...})")
    if not items[id] then item_order[#item_order + 1] = id end
    def.id, def.name = id, def.name or id
    items[id] = def
    if item_hook then return end
    item_hook = true
    E.on("load", items_changed)
    E.on("new_game", items_changed)
end
function C.item_count(id) return bag()[id] or 0 end
function C.add_item(id, n)
    local def = items[id]
    if not def then error("no item of that name was defined", 2) end
    if not S.ready() then return false end
    local b = bag()
    local have = b[id] or 0
    local now = math.max(0, math.min(def.max or 99, have + (n or 1)))
    if now == have then return have end
    b[id] = now > 0 and now or nil
    S.set("cleria.items", b)
    if now > have and def.notice ~= false then
        cleria.notice(def.name .. (now - have > 1 and ("  x" .. (now - have)) or ""), def.notice_title or "Obtained")
    end
    if def.on_change then def.on_change(now, def) end
    return now
end
function C.use_item(id)
    local def = items[id]
    if not def or not def.use or C.item_count(id) < 1 then return false end
    if def.use(def) == false then return false end        -- the item refused: nothing is spent
    if def.keep ~= true then C.add_item(id, -1) end
    return true
end
function C.items()
    local out = {}
    for _, id in ipairs(item_order) do
        local n = C.item_count(id)
        if n > 0 then out[#out + 1] = {id = id, name = items[id].name, desc = items[id].desc, count = n} end
    end
    return out
end
function C.open_bag(opts)
    opts = opts or {}
    UI.window("cleria_bag", {title = opts.title or "Items"}, function(p)
        local any = false
        for _, id in ipairs(item_order) do
            local def, n = items[id], C.item_count(id)
            if n > 0 then
                any = true
                if def.use then
                    if p:button(def.name .. "  x" .. n, {desc = def.desc}) then C.use_item(id) end
                else
                    p:info(def.name, "x" .. n, {desc = def.desc})
                end
            end
        end
        if not any then p:text(opts.empty or "Nothing yet.") end
        if p:button(opts.close or "Close") then p:close() end
    end)
    UI.open("cleria_bag")
end
items_changed = function()                                 -- a load or a new game: what each item's holder must apply again
    for _, id in ipairs(item_order) do
        local def = items[id]
        if def.on_change then def.on_change(C.item_count(id), def) end
    end
end

-- ---- cleria.content.define_skill: a cast of the mod's own on a bracelet's slot --------------------------------
local skills, skill_ready, skill_hook = {}, {}, false
function C.define_skill(slot, def)
    assert(slot == 1 or slot == 2 or slot == 3, "define_skill: the slot is 1, 2 or 3")
    assert(type(def) == "table" and type(def.cast) == "function", "define_skill(slot, {cast = fn, ...})")
    skills[slot] = def
    if skill_hook then return end
    skill_hook = true
    W.add_filter("skill", function(q)
        local d = skills[q.slot]
        if not d or (d.character and d.character ~= G.character()) then return end
        local now = G.play_time()
        if (skill_ready[q.slot] or 0) > now then return "suppress" end
        local cost = d.keep and 0 or (d.mp or q.cost)
        if q.mp < cost then return "suppress" end
        if d.cast({slot = q.slot, level = q.level, charged = q.level == 2, air = q.air == 1, cost = cost}) == false then return "suppress" end
        if cost > 0 then P.set_mp(q.mp - cost) end
        skill_ready[q.slot] = now + (d.cooldown or 0)
        if d.keep then return end                            -- the game's own cast follows, at its own cost
        return "suppress"
    end)
end
function C.undefine_skill(slot) skills[slot] = nil end
)LUA",
R"LUA(
-- ---- cleria.content.define_room: a room of the game under another identity ------------------------------------
local rooms, room_now, room_next, room_hook = {}, nil, nil, false
local function populate(d)
    if room_now ~= d.id then return end
    d._cleared = false
    for _, m in ipairs(d.monsters or {}) do
        W.spawn(m.id or m.cast or m[1], m.x or m[2], m.y or m[3], m.z or m[4], {level = m.level, yaw = m.yaw})
    end
    if d.music then cleria.audio.music(d.music) end
    if d.on_enter then d.on_enter(d) end
end
local function room_hooks()
    if room_hook then return end
    room_hook = true
    W.add_filter("spawn", function(q)
        local d = rooms[room_next or room_now]
        if d and q.by == "" and not d.keep_monsters then return "suppress" end   -- the room's own; a mod's spawns pass
    end)
    W.add_filter("exit", function(q)
        if room_next and q.index == -1 then return end       -- our own enter_room
        local d = room_now and rooms[room_now]
        if not d or not d.exits then return end
        local to = d.exits[q.index]
        if to == nil then to = d.exits[q.door] end
        if to == nil then to = d.exits.default end
        if to == nil then return end                         -- the base room's own exit
        if to == false then return "suppress" end            -- closed
        if rooms[to] then
            room_next = to
            return {to = rooms[to].base}
        end
        return {to = to}
    end)
    E.on("room_enter", function(e)
        local d = room_next and rooms[room_next]
        room_next = nil
        if d and e.room == d.base then
            room_now = d.id
            cleria.after(0.05, function() populate(d) end)
        else
            room_now = nil
        end
    end)
    E.on("enemy_killed", function()
        local d = room_now and rooms[room_now]
        if not d or not d.on_clear or d._cleared then return end
        cleria.after(0.1, function()
            if room_now == d.id and not d._cleared and #W.actors() == 0 then
                d._cleared = true
                d.on_clear(d)
            end
        end)
    end)
end
function C.define_room(id, def)
    assert(type(id) == "string" and type(def) == "table" and type(def.base) == "string", "define_room(id, {base = \"S_xxxx\", ...})")
    def.id = id
    rooms[id] = def
    room_hooks()
end
function C.enter_room(id)
    local d = rooms[id]
    if not d then error("no room of that name was defined", 2) end
    room_next = id
    if G.goto_room(d.base) then return true end
    room_next = nil
    return false
end
function C.room() return room_now end

-- ---- cleria.content.define_character: a character of the game with other numbers and another look -------------
local chars, char_now, char_hook = {}, nil, false
local function apply_char(d)
    P.set_modifier("max_hp", d.hp_add or 0, d.hp or 1)
    P.set_modifier("atk", d.atk_add or 0, d.atk or 1)
    P.set_modifier("def", d.def_add or 0, d.def or 1)
    P.tune({speed = d.speed or 1, jump = d.jump or 1, air_jumps = d.air_jumps or 0, mp_cost = d.mp_cost or 1,
            mp_regen = d.mp_regen or 1, boost_gain = d.boost_gain or 1})
    P.set_look({scale = d.scale or 1, glow = d.glow or false})
end
local function restore_char()                                -- a load or a new game: the save says who is played
    local id = S.get("cleria.character")
    local d = id and chars[id]
    if d and (not d.base or d.base == G.character()) then
        char_now = id
        apply_char(d)
        if d.on_apply then d.on_apply(d) end
    elseif char_now then
        char_now = nil
        apply_char({})
    end
end
function C.define_character(id, def)
    assert(type(id) == "string" and type(def) == "table", "define_character(id, {name =, ...})")
    def.id, def.name = id, def.name or id
    chars[id] = def
    if char_hook then return end
    char_hook = true
    E.on("load", restore_char)
    E.on("new_game", restore_char)
end
function C.set_character(id)
    if id == nil then
        if char_now then apply_char({}) end
        char_now = nil
        if S.ready() then S.set("cleria.character", nil) end
        return true
    end
    local d = chars[id]
    if not d then error("no character of that name was defined", 2) end
    if d.base and d.base ~= G.character() then return false end
    char_now = id
    apply_char(d)
    if S.ready() then S.set("cleria.character", id) end
    if d.on_apply then d.on_apply(d) end
    return true
end
function C.character() return char_now end
)LUA",
};
