-- Hello Lua: the smallest CleriaCore Lua mod (build/shell_re/LUA_API.md).
-- mod.ini says Script=main.lua and ApiVersion=3; this file runs once when the game starts.

local show = cleria.settings.get("ShowCorner", true)    -- cleria.ini [cleria.mod.sample.hello]
local rooms = 0                                          -- rooms entered in this save

-- The count lives in the save (cleria.save is per save: a new game starts empty, a load brings the
-- save's own). The first room of a session is entered BEFORE new_game / load arrive (they come at the
-- first tick), so the count is read back from the save on every room, not kept across sessions.
cleria.events.on("room_enter", function(e)
    rooms = cleria.save.get("rooms", 0) + 1
    cleria.save.set("rooms", rooms)                      -- written with the game's next save
    cleria.log("entered " .. e.room .. " (room #" .. rooms .. ")")
end)
cleria.events.on("new_game", function()
    rooms = cleria.save.get("rooms", 0)
    cleria.notice("Hello from Lua! Rooms you enter are counted.")
end)
cleria.events.on("load", function() rooms = cleria.save.get("rooms", 0) end)

-- A corner overlay, drawn over the game every frame (sizes scale with the window: d.scale).
cleria.hud.layer("corner", function(d)
    if not show or not cleria.game.in_game() then return end
    local s = d.scale
    local text = string.format("%s  -  HP %d/%d  -  %d rooms", cleria.game.room(), cleria.player.hp(),
                               cleria.player.max_hp(), rooms)
    local w = d:text_size(text, {size = 15 * s})
    local x, y = d.w - w - 38 * s, d.h - 46 * s           -- bottom right, clear of the game's own HUD
    d:rect(x, y, w + 24 * s, 30 * s, {color = 0x100F0B, alpha = 0.7, rounding = 4 * s})
    d:text(x + 12 * s, y + 6 * s, text, {size = 15 * s, color = "label"})
end)

-- Its own tab in the F1 panel, in the shell's look.
cleria.ui.page("Hello", function(p)
    p:section("Hello Lua")
    p:info("Rooms entered", tostring(rooms), "Rooms entered in this save (kept in the save's .cleria file).")
    local changed
    show, changed = p:toggle("Corner text", show, "The room, your HP and the room count in the bottom-right corner.")
    if changed then cleria.settings.set("ShowCorner", show) end
    if p:button("Say hello", "Shows a notice card.") then cleria.notice("Hello!") end
end)

cleria.command("hello", function(args) cleria.log("hello " .. args .. ", " .. rooms .. " rooms") end)
cleria.log("Hello Lua loaded (API " .. cleria.api_version .. ")")
