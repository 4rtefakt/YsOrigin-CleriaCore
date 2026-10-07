-- PrintJSON -> a line of coloured spans (the reference client's JSONtoTextParser colours; a port of
-- CleriaCore's src/cleria/ap/text.cpp, ARCHIPELAGO.md §3.5).
--   span = {text, kind ("text" | "player" | "item" | "location" | "entrance" | "hint"), rgb (0xRRGGBB) or nil, bold}

local M = {}

local colors = {
    black = 0x000000, red = 0xEE0000, green = 0x00FF7F, yellow = 0xFAFAD2, blue = 0x6495ED, magenta = 0xEE00EE,
    cyan = 0x00EEEE, slateblue = 0x6D8BE8, plum = 0xAF99EF, salmon = 0xFA8072, white = 0xFFFFFF, orange = 0xFF7700,
}
M.colors = colors

local function apply(sp, codes)
    for c in string.gmatch(codes, "[^;]+") do
        if c == "bold" then sp.bold = true
        elseif c == "underline" then sp.underline = true
        elseif c:sub(-3) == "_bg" then sp.bg = colors[c:sub(1, -4)]
        elseif colors[c] then sp.rgb = colors[c] end
    end
end

local function text_int(part)
    local t = part.text
    if math.type(t) == "integer" then return t end
    if type(t) == "string" and t:match("^%-?%d+$") then return math.tointeger(tonumber(t)) end
    return nil
end

local hint_colors = {[40] = "green", [0] = "white", [10] = "slateblue", [20] = "salmon", [30] = "plum"}

local function item_color(sp)
    local f = sp.flags or 0
    if f == 0 then apply(sp, "cyan")
    elseif f & 1 ~= 0 then apply(sp, "plum")
    elseif f & 2 ~= 0 then apply(sp, "slateblue")
    elseif f & 4 ~= 0 then apply(sp, "salmon")
    else apply(sp, "cyan") end
end

-- names: an object with player_name(slot), is_self(slot), item_name(id, slot), location_name(id, slot)
function M.render_parts(parts, names)
    local line = {spans = {}}
    for _, part in ipairs(type(parts) == "table" and parts or {}) do
        local t = part.text
        local sp = {kind = "text", text = type(t) == "string" and t or ((t == nil or t == cleria.json.null) and "" or cleria.json.encode(t))}
        local ty = type(part.type) == "string" and part.type or ""
        local n = text_int(part)
        if ty == "player_id" and n then
            sp.kind, sp.id = "player", n
            sp.text = names:player_name(n)
            apply(sp, names:is_self(n) and "magenta" or "yellow")
        elseif ty == "player_name" then
            sp.kind = "player"
            apply(sp, "yellow")
        elseif ty == "item_id" and n then
            sp.kind, sp.id = "item", n
            sp.player, sp.flags = math.tointeger(part.player) or 0, math.tointeger(part.flags) or 0
            sp.text = names:item_name(n, sp.player)
            item_color(sp)
        elseif ty == "item_name" then
            sp.kind = "item"
            sp.player, sp.flags = math.tointeger(part.player) or 0, math.tointeger(part.flags) or 0
            item_color(sp)
        elseif ty == "location_id" and n then
            sp.kind, sp.id = "location", n
            sp.player = math.tointeger(part.player) or 0
            sp.text = names:location_name(n, sp.player)
            apply(sp, "green")
        elseif ty == "location_name" then
            sp.kind = "location"
            apply(sp, "green")
        elseif ty == "entrance_name" then
            sp.kind = "entrance"
            apply(sp, "blue")
        elseif ty == "hint_status" then
            sp.kind = "hint"
            apply(sp, hint_colors[math.tointeger(part.hint_status) or -1] or "red")
        elseif ty == "color" then
            apply(sp, type(part.color) == "string" and part.color or "")
        end
        line.spans[#line.spans + 1] = sp
    end
    return line
end

function M.render_print_json(packet, names)
    local line = M.render_parts(packet.data, names)
    line.type = type(packet.type) == "string" and packet.type or ""
    return line
end

function M.plain(line)
    local t = {}
    for i, sp in ipairs(line.spans) do t[i] = sp.text end
    return table.concat(t)
end

-- A local line of one span.
function M.line(s, rgb)
    return {type = "", spans = {{kind = "text", text = s, rgb = rgb}}}
end

return M
