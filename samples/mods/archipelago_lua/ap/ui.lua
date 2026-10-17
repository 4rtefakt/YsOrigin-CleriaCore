-- The Archipelago mod's UI: its page (F1 > Mods > Archipelago) and its overlays (the connection line, the item
-- feed, the tracker, the Blinding Fog trap), drawn with cleria.ui / cleria.hud in the shell's look. The layout
-- and the [H] values (10 s feed, 6 lines, the sizes) follow CleriaCore's former C++ client.

local text = require("ap.text")
local ys = require("ap.ys")

local M = {}

-- the logo of tools/make_ap_icon.py, live: a gold-rimmed night-blue disc, five coloured islands linked to a pale centre
local function logo(d, cx, cy, u, a)
    d:circle(cx, cy, 0.47 * u, {color = 0xD6B03E, alpha = a})
    d:circle(cx, cy, (0.47 - 0.045) * u, {color = 0x1B2240, alpha = a})
    local isl = {0xAF99EF, 0x6D8BE8, 0xFA8072, 0x00EEEE, 0x00FF7F}
    for k = 0, 4 do
        local ang = math.rad(-90 + 72 * k)
        local px, py = cx + 0.265 * u * math.cos(ang), cy + 0.265 * u * math.sin(ang)
        d:line(cx, cy, px, py, {color = 0xC4CCE8, alpha = a, thickness = math.max(1, 0.035 * u)})
        d:circle(px, py, 0.1 * u, {color = 0x14182C, alpha = a})
        d:circle(px, py, 0.08 * u, {color = isl[k + 1], alpha = a})
    end
    d:circle(cx, cy, 0.075 * u, {color = 0xF4F0E6, alpha = a})
end

-- a line's spans word-wrapped in `maxw` from (x, y); returns the height used
local function draw_line(d, line, x, y, maxw, px, a, draw)
    local cx, cy, lh = 0, 0, px * 1.25
    for _, sp in ipairs(line.spans) do
        local font = (sp.kind == "player" or sp.kind == "item" or sp.bold) and "medium" or "body"
        local opts = {size = px, font = font, color = sp.rgb or 0xEAE6DA, alpha = a}
        for word in string.gmatch(sp.text, "[^ ]* ?") do
            if word ~= "" then
                local ww = d:text_size(word, opts)
                if cx > 0 and cx + ww > maxw then cx, cy = 0, cy + lh end
                if draw then d:text(x + cx, y + cy, word, opts) end
                cx = cx + ww
            end
        end
    end
    return cy + lh
end

function M.install(A, persist)
    local cfg, c = A.cfg, nil

    -- the item feed, top right: the last 6 lines of 10 s [H], fading
    cleria.hud.layer("feed", function(d)
        if A.hud_hidden or not cfg.show_feed or #A.feed == 0 then return end
        local now, S = cleria.time(), d.scale
        local px, bw, pad = 16 * S, 430 * S, 10 * S
        local shown = 0
        for i = #A.feed, 1, -1 do
            if shown >= 6 or now - A.feed[i].t > 10 then break end
            shown = shown + 1
        end
        local y = d.h * 0.05
        for k = #A.feed - shown + 1, #A.feed do
            local f = A.feed[k]
            local a = math.max(0, math.min(1, 10 - (now - f.t)))
            local th = draw_line(d, f, 0, 0, bw - 2 * pad - 28 * S, px, a, false)
            local x0, x1 = d.w - bw - 18 * S, d.w - 18 * S
            local hh = th + 2 * pad * 0.7
            d:rect(x0, y, x1 - x0, hh, {color = 0x100F0B, alpha = a * 200 / 255, rounding = 5 * S})
            d:rect(x0, y, x1 - x0, hh, {color = 0x9C8442, alpha = a * 150 / 255, rounding = 5 * S, fill = false, thickness = S})
            logo(d, x0 + pad + 10 * S, y + pad * 0.7 + px * 0.6, 20 * S, a)
            draw_line(d, f, x0 + pad + 26 * S, y + pad * 0.7, bw - 2 * pad - 28 * S, px, a, true)
            y = y + hh + 6 * S
        end
    end)

    -- an autosave was just written: the logo and "Saving..." for 2 s, bottom right
    cleria.hud.layer("saving", function(d)
        local age = A.saved_at and cleria.time() - A.saved_at
        if not age or age > 2 then return end
        local S = d.scale
        local px, a = 15 * S, math.min(1, age * 6, (2 - age) * 2)
        local tw = d:text_size("Saving...", {size = px})
        local x0, y0, h = d.w - tw - 58 * S, d.h - 44 * S, 28 * S
        d:rect(x0, y0, tw + 40 * S, h, {color = 0x100F0B, alpha = a * 170 / 255, rounding = 4 * S})
        logo(d, x0 + 15 * S, y0 + h / 2, 18 * S, a)
        d:text(x0 + 30 * S, y0 + h / 2 - px * 0.58, "Saving...", {size = px, color = 0xEAE6DA, alpha = a * 230 / 255})
    end)

    -- the connection line, top left
    cleria.hud.layer("status", function(d)
        local s = A.session.status
        if A.hud_hidden or not cfg.show_status or (s == "idle" and A.logic.st.seed == "") then return end
        local S = d.scale
        local px = 14.5 * S
        local t = "AP  " .. (A.mismatch == "" and A.session:status_text(cleria.time()) or A.mismatch)
        if s == "connected" and A.logic.configured then
            local done = ys.count(A.logic.st.checks)
            t = t .. "  \u{2022}  " .. done .. "/" .. #A.logic.regs .. " checks"
            if A.in_logic then t = t .. "  \u{2022}  " .. done .. "/" .. A.in_logic .. " in logic (" .. A.logic_left .. " left)" end
        end
        local dot = s == "connected" and (A.mismatch == "" and "good" or "warn") or ((s == "refused" or s == "disconnected") and 0xE66050 or "warn")
        local tw = d:text_size(t, {size = px})
        local x0, y0, h = 14 * S, 12 * S, 26 * S
        d:rect(x0, y0, tw + 34 * S, h, {color = 0x100F0B, alpha = 170 / 255, rounding = 4 * S})
        d:circle(x0 + 12 * S, y0 + h / 2, 4.5 * S, {color = dot})
        d:text(x0 + 24 * S, y0 + h / 2 - px * 0.58, t, {size = px, color = 0xEAE6DA, alpha = 230 / 255})
    end)

    -- the tracker, left: checked / total per zone of the active locations, in tower order
    cleria.hud.layer("tracker", function(d)
        if A.hud_hidden or not cfg.show_tracker or not A.logic.configured then return end
        local zones, lowest, order = {}, {}, {}
        local floors = cfg.tracker_mode == 2
        for _, r in ipairs(A.logic.regs) do
            local l = A.table.loc[r.id]
            local z = (l and l.zone ~= "") and l.zone or "Other"
            local fl = l and tonumber(l.floor:match("^(%d+)")) or 0
            if floors then z = (l and l.floor ~= "") and l.floor or (z == "Other" and "Shop" or z) end
            if not zones[z] then zones[z] = {0, 0} lowest[z] = 999 order[#order + 1] = z end
            if fl > 0 then lowest[z] = math.min(lowest[z], fl) end
            zones[z][2] = zones[z][2] + 1
            if A.logic.st.checks[r.id] then zones[z][1] = zones[z][1] + 1 end
        end
        table.sort(order, function(x, y) if lowest[x] ~= lowest[y] then return lowest[x] < lowest[y] end return x < y end)
        local S = d.scale
        local px, lh = (floors and 12.5 or 14) * S, (floors and 16 or 19) * S
        local x0, y0, w = 14 * S, d.h * (floors and 0.22 or 0.30), 250 * S
        local h = 34 * S + lh * #order
        d:rect(x0, y0, w, h, {color = 0x100F0B, alpha = 185 / 255, rounding = 5 * S})
        d:rect(x0, y0, w, h, {color = 0x9C8442, alpha = 150 / 255, rounding = 5 * S, fill = false, thickness = S})
        d:text(x0 + 12 * S, y0 + 8 * S, "ARCHIPELAGO CHECKS", {size = 13.5 * S, font = "cond", color = "section"})
        local y = y0 + 30 * S
        for _, z in ipairs(order) do
            local got, all = zones[z][1], zones[z][2]
            local col = got == all and "good" or "value"
            d:text(x0 + 12 * S, y, z, {size = px, color = col})
            local n = got .. " / " .. all
            d:text(x0 + w - 12 * S - d:text_size(n, {size = px}), y, n, {size = px, color = col})
            y = y + lh
        end
    end)

    -- "left in this room", bottom left: the room's locations still to find; with spoilers, what each holds
    cleria.hud.layer("room", function(d)
        if A.hud_hidden or not cfg.show_room or not A.active() then return end
        local left = A.logic:left_in_room(A.logic.scene)
        if #left == 0 then return end
        local cl = A.session.client
        local hinted = {}
        for _, h in ipairs(A.hints) do if not h.found and h.finder == cl.slot then hinted[h.location] = h end end
        local S = d.scale
        local px, lh, w = 13.5 * S, 18 * S, 360 * S
        local n = math.min(#left, 8)
        local h = 30 * S + lh * n + (#left > n and lh or 0)
        local x0, y0 = 14 * S, d.h - h - 150 * S
        d:rect(x0, y0, w, h, {color = 0x100F0B, alpha = 185 / 255, rounding = 5 * S})
        d:rect(x0, y0, w, h, {color = 0x9C8442, alpha = 150 / 255, rounding = 5 * S, fill = false, thickness = S})
        d:text(x0 + 12 * S, y0 + 7 * S, "LEFT IN THIS ROOM  " .. #left, {size = 13 * S, font = "cond", color = "section"})
        local y = y0 + 27 * S
        for i = 1, n do
            local r = left[i]
            local name = r.name:gsub("^[^:]+: ", ""):gsub(" %(S_%d+%)$", "")
            local col = "value"
            local s = cl.scouted[r.id]
            if hinted[r.id] then
                name, col = name .. "  >  " .. hinted[r.id].item_name, "gold"
            elseif cfg.room_spoil and s then
                name = name .. "  >  " .. cl:item_name(s.item, s.player) .. (s.player ~= cl.slot and " (" .. cl:player_name(s.player) .. ")" or "")
            end
            while #name > 4 and d:text_size(name, {size = px}) > w - 24 * S do name = name:sub(1, #name - 4) .. "..." end
            d:text(x0 + 12 * S, y, name, {size = px, color = col})
            y = y + lh
        end
        if #left > n then d:text(x0 + 12 * S, y, "and " .. (#left - n) .. " more", {size = px, color = "muted"}) end
    end)

    -- The warp map's counts: found / total checks of each floor on its bar, while the map is open (game.state()
    -- "map", mod API 7 with the warp-map state). The bars are the map's 22 plates, in the warp registry's order,
    -- at their places of the 1024 x 768 layout (retail 0x701730, CleriaCore's kMapRecs), drawn 4:3 and centred.
    -- The count sits on the plate's right end, as the retail mod's [MOD warpmap.cpp]; the floors with no plate
    -- (no statue: 2F, 3F, 15F, 19F) are listed bottom left. Lime while checks are left, gold when all are found.
    local kPlates = {{560, 680}, {576, 628}, {594, 580}, {608, 532}, {624, 484}, {640, 436}, {656, 388}, {672, 340},
                     {688, 292}, {704, 244}, {720, 196}, {736, 148}, {752, 100}, {56, 432}, {100, 384}, {142, 336},
                     {182, 288}, {226, 240}, {268, 192}, {310, 144}, {352, 96}, {392, 48}}
    -- the floor each plate counts: the statue's floor; Rado's Annex counts 16F (no statue of its own), the summit nothing
    local kPlateFloor = {1, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 18, 20, 21, 22, 23, 24, 25, 0}
    local on_plate = {}
    for _, f in ipairs(kPlateFloor) do if f > 0 then on_plate[f] = true end end
    local kLeft, kDone = 0xAAFF33, 0xFFC83C
    local G = cleria.game
    local function map_open() return G.state ~= nil and G.state() == "map" end
    A.map_open = map_open
    cleria.hud.layer("warpmap", function(d)
        if A.hud_hidden or not cfg.show_warpmap or not A.active() or not map_open() then return end
        local s = math.min(d.w / 1024, d.h / 768)
        local ox, oy = (d.w - 1024 * s) / 2, (d.h - 768 * s) / 2
        local px = 30 * d.scale
        local checked = A.session.client.checked
        local function put(x, y, t, col, centre)
            local opts = {size = px, font = "bold", color = col}
            local tw, th = d:text_size(t, opts)
            if centre then x = x - tw / 2 end
            y = y - (th or px) / 2
            for _, o in ipairs({{-2, 0}, {2, 0}, {0, -2}, {0, 2}}) do
                d:text(x + o[1] * d.scale, y + o[2] * d.scale, t, {size = px, font = "bold", color = 0x000000, alpha = 0.92})
            end
            d:text(x, y, t, opts)
        end
        for i, p in ipairs(kPlates) do
            local f = kPlateFloor[i]
            local found, total = A.logic:floor_count(f, checked)
            if found then
                put(ox + (p[1] + 205) * s, oy + (p[2] + 20) * s, found .. "/" .. total, found >= total and kDone or kLeft, true)
            end
        end
        local y = oy + 630 * s
        for f = 1, 30 do
            if not on_plate[f] then
                local found, total = A.logic:floor_count(f, checked)
                if found then
                    put(ox + 130 * s, y, f .. "F  " .. found .. "/" .. total, found >= total and kDone or kLeft, false)
                    y = y + px * 1.15
                end
            end
        end
    end)

    -- the Blinding Fog trap: a grey haze fading in and out ("0" sorts it under this mod's other layers)
    cleria.hud.layer("0fog", function(d)
        local t = A.fog_t
        if t <= 0 then return end
        local k = math.min(1, t / 60, (30 * 60 - t) / 60)
        d:rect(0, 0, d.w, d.h, {color = 0xC4C8CE, alpha = 205 / 255 * k})
    end)

    -- The chat window (mod API 7: a window and a key of the mod's own): the room's lines, the newest at the bottom,
    -- and a line to type a message or a server command into (!hint <item>, !help ...). F6 by default, the player's
    -- key in the page's Keys section. The game runs on underneath; while the window is open it has the keyboard
    -- and the pad, so typing never moves the player [MOD chat.cpp: F6, 60 lines kept, Enter sends].
    if cleria.ui.window and cleria.input and cleria.input.action then
        local line = ""
        cleria.ui.window("chat", {title = "Archipelago", width = 0.6, pause = false}, function(p)
            local first = math.max(1, #A.chat - 13)
            if #A.chat == 0 then p:text("Nothing yet: items found and sent, hints, joins and chat show here.", {color = "muted"}) end
            for i = first, #A.chat do p:text(text.plain(A.chat[i]), {color = A.chat[i].spans[1] and A.chat[i].spans[1].rgb or nil}) end
            local typed, kept = p:text_field("Say", line, {empty = A.connected() and "(type, then Enter)" or "(not connected)",
                                             desc = "A chat line, or a server command: !hint <item>, !help ... Enter sends it."})
            if kept then
                typed = typed:gsub("^%s+", ""):gsub("%s+$", "")
                if typed ~= "" and A.connected() then A.session.client:say(typed) end
                line = ""
            else
                line = typed
            end
            if p:button("Close", "Back to the game (or F6, Esc).") then p:close() end
        end)
        cleria.input.action("chat", {label = "Archipelago chat", key = "F6"}, function()
            if cleria.ui.is_open("chat") then cleria.ui.close()
            elseif A.in_game or A.session.status ~= "idle" then cleria.ui.open("chat") end
        end)
    end

    if cleria.input and cleria.input.action then
        cleria.input.action("skip", {label = "Skip cutscenes (hold)", key = "Right Ctrl"})
        -- Insert hides the feed, the status line, the tracker and the counts for a clean screen, as the
        -- retail client's overlay toggle did (the Blinding Fog trap stays)
        cleria.input.action("overlay", {label = "Hide / show the Archipelago overlay", key = "Insert"}, function()
            A.hud_hidden = not A.hud_hidden
        end)
    end

    -- the page: F1 > Mods > Archipelago
    cleria.ui.page("Archipelago", function(p)
        local s = A.session.status
        local cl = A.session.client
        p:section("Connection")
        local st_text = A.mismatch == "" and A.session:status_text(cleria.time()) or A.mismatch
        p:info("Status", st_text, {color = (s == "connected" and A.mismatch == "") and "good" or s == "idle" and "muted" or "warn",
            desc = "The connection to the Archipelago server, made by this Lua mod: the client for the Ys Origin apworld, " ..
                   "written on CleriaCore's mod API. Not a retail feature."})
        local changed
        cfg.server, changed = p:text_field("Server", cfg.server, "The room's address, e.g. archipelago.gg:38281. ws:// or wss:// " ..
                                           "may be given; without one the secure connection is tried first.")
        if changed then persist("server") end
        cfg.slot, changed = p:text_field("Slot name", cfg.slot, "Your player (slot) name in the multiworld, as in your YAML.")
        if changed then persist("slot") end
        cfg.password, changed = p:text_field("Password", cfg.password, {desc = "The room password, if it has one. Stored in " ..
                                             "cleria.ini; never shown or logged.", secret = true, empty = "(none)"})
        if changed then persist("password") end
        if s == "idle" or s == "refused" or s == "disconnected" then
            if p:button("Connect", {desc = "Connect to the room with the server, slot name and password above.", enabled = cfg.slot ~= ""}) then
                A.connect()
            end
        elseif p:button("Disconnect", "Close the connection. Checks made meanwhile are kept in the save and sent on the next connect.") then
            A.disconnect()
        end
        cfg.auto_connect, changed = p:toggle("Connect at start", cfg.auto_connect, "Connect to this room as soon as the game starts.")
        if changed then persist("auto_connect") end
        cfg.death_link, changed = p:toggle("DeathLink", cfg.death_link, "Your deaths kill the other DeathLink players, and theirs " ..
                                           "kill you (not in a story duel). A seed made with DeathLink turns it on too. Not a retail option.")
        if changed then
            persist("death_link")
            if A.connected() then cl:set_tags(A.death_link_on() and {"DeathLink"} or {}) end
        end
        p:section("This game")
        local st = A.logic.st
        p:info("Seed", st.seed ~= "" and st.seed or (A.connected() and cl:seed_name() or "-"),
               "The seed this save belongs to. A save is stamped the first time it is played connected.")
        p:info("Items received", st.applied .. " / " .. #cl.received,
               "Items granted into this save / items the server has sent. Saved with the game: a reload never grants twice.")
        p:info("Locations checked", ys.count(st.checks) .. " / " .. #A.logic.regs, "Locations this save has found, of the seed's active ones.")
        p:info("Goal", st.goal_sent and "Complete" or "Not yet", {color = st.goal_sent and "good" or "muted",
               desc = A.logic.opt.goal == 1 and "Defeat every floor boss and the final boss." or "Defeat the final boss."})
        if A.active() then
            local G = cleria.game
            local names, have, n = {"Wind", "Thunder", "Fire"}, {}, 0
            for i = 1, 3 do
                local on = (G.flag(0x73 + i) or 0) >= 1
                have[i] = names[i] .. (on and " yes" or " no")
                if on then n = n + 1 end
            end
            p:info("Elemental skills", table.concat(have, "   "), {color = n == 3 and "good" or "warn",
                   desc = "The final fight's barriers each fall to one element only: all three skills are needed to finish."})
            if A.logic.opt.goal == 1 then
                local b = 0
                for f = 220, 225 do if (G.flag(f) or 0) >= 1 then b = b + 1 end end
                p:info("Floor bosses", b .. " / 6", {color = b == 6 and "good" or "value",
                       desc = "This seed's goal needs every floor boss beaten before the final boss."})
            end
            p:info("Left in this room", tostring(#A.logic:left_in_room(A.logic.scene)), "Locations of the current room still to find.")
        end
        for _, u in ipairs(A.logic.opt.unsupported) do p:info("Not supported", u, {color = "warn"}) end
        -- hints
        p:section("Hints")
        p:info("Hint points", tostring(cl.hint_points or 0), "What the server says you can spend on hints.")
        A.hint_text, changed = p:text_field("Hint for", A.hint_text, {desc = "An item name (or part of one) to ask the server about.",
                                            empty = "(an item name)"})
        if p:button("Ask for a hint", {desc = "Sends !hint <the name above> to the server. The answer shows in the feed and below.",
                                       enabled = A.connected() and A.hint_text ~= ""}) then
            cl:say("!hint " .. A.hint_text)
        end
        local shown = 0
        for _, h in ipairs(A.hints) do
            if not h.found and shown < 30 then
                shown = shown + 1
                local mine = h.finder == cl.slot
                p:info(h.item_name .. (h.receiver ~= cl.slot and " (" .. h.receiver_name .. ")" or ""),
                       h.location_name .. (mine and "" or "  -  " .. h.finder_name .. "'s world"),
                       {color = mine and "gold" or "value", desc = mine and "In your world: go and find it." or
                        "In " .. h.finder_name .. "'s world: they have to find it."})
            end
        end
        if shown == 0 then p:info("", "No open hints.", {color = "muted"}) end
        -- chat and commands
        p:section("Chat and commands")
        A.chat_text, changed = p:text_field("Message", A.chat_text, {desc = "A chat line or a server command (!help lists them).",
                                            empty = "(type here)"})
        if p:button("Send", {desc = "Sends the message above to the room.", enabled = A.connected() and A.chat_text ~= ""}) then
            cl:say(A.chat_text)
            A.chat_text = ""
        end
        if p:button("Release my items", {desc = "Sends !release: everything still in your world goes out to its owners. " ..
                                         "Rooms usually allow it once your goal is complete.", enabled = A.connected() and st.goal_sent}) then
            cl:say("!release")
        end
        if p:button("Collect my items", {desc = "Sends !collect: your items still in other worlds come to you. " ..
                                         "Rooms usually allow it once your goal is complete.", enabled = A.connected() and st.goal_sent}) then
            cl:say("!collect")
        end
        -- quality of life
        p:section("Quality of life")
        if A.can_save then
            cfg.autosave, changed = p:toggle("Autosave", cfg.autosave, "Save the game by itself after a check, a received item, " ..
                "a door opened with a key or medallion and a Panacea used, at the next safe moment. Not a retail option.")
            if changed then persist("autosave") end
            cfg.autosave_slot, changed = p:slider("Autosave slot", cfg.autosave_slot, 1, 64, 1, "No.%02.0f", {desc = "The save slot " ..
                "the autosave writes, as numbered in the book. It is overwritten without asking.", enabled = cfg.autosave})
            if changed then cfg.autosave_slot = math.tointeger(math.floor(cfg.autosave_slot + 0.5)) or 8 persist("autosave_slot") end
        else
            p:info("Autosave", "needs a newer CleriaCore", {color = "muted", desc = "The autosave uses cleria.game.save, which came with mod API 4."})
        end
        cfg.seed_saves, changed = p:toggle("Seed save folder", cfg.seed_saves, {desc = "Keep this seed's saves in a " ..
            "folder of their own (profiles/AP_<seed>_<slot>), away from your other saves. Applied on the title, at the " ..
            "next connect. Off: the normal saves.", enabled = cleria.save.set_profile ~= nil})
        if changed then persist("seed_saves") end
        cfg.boss_on_kill, changed = p:toggle("Boss checks on defeat", cfg.boss_on_kill, "A boss room's check is sent when the fight " ..
            "is won, not when you walk in (the floor bosses, the duels and the 20F ward; the 17F room has no fight and stays on entry). " ..
            "Not a retail option.")
        if changed then persist("boss_on_kill") end
        local can_skip = G.skip_cutscenes ~= nil
        cfg.cutscene_skip, changed = p:choice("Cutscene skip", cfg.cutscene_skip, {"Off", "Hold Right Ctrl", "Always"},
            {desc = "Hold: cutscenes go by while the key is held (rebind it under Controls). Always: every cutscene " ..
            "goes by on its own; what you start yourself (a talk, a statue, a shop) still plays. CleriaCore skips each " ..
            "one whole; CleriaOre fast-forwards it. Needs mod API 7.", enabled = can_skip})
        if changed then persist("cutscene_skip") end
        cfg.skip_intro, changed = p:toggle("Skip the intro on a random start", cfg.skip_intro, {desc = "On a seed with a " ..
            "random start, a New Game skips its intro until you stand at the start statue. Needs mod API 7.", enabled = can_skip})
        if changed then persist("skip_intro") end
        cfg.hint_alerts, changed = p:toggle("Hint alerts", cfg.hint_alerts, "A feed line when you enter a room that holds a hinted item.")
        if changed then persist("hint_alerts") end
        cfg.exp_mult, changed = p:slider("EXP multiplier", cfg.exp_mult, 0, 100, 1, "%.0f", "Your own EXP multiplier for every kill. " ..
            "0 = the seed's setting. Not a retail option.")
        if changed then cfg.exp_mult = math.tointeger(math.floor(cfg.exp_mult + 0.5)) or 0 persist("exp_mult") end
        local scaled = A.logic.opt.level_scaling == 2 or A.logic.opt.level_scaling == 3
        cfg.exp_base, changed = p:slider("...base", cfg.exp_base, 0, 100, 1, "%.0f", {desc = "Replaces the seed's base EXP " ..
            "multiplier (above the catch-up range). 0 = the seed's. Only on a seed whose level scaling multiplies EXP, and " ..
            "only while the EXP multiplier above is 0.", enabled = cfg.exp_mult == 0 and scaled})
        if changed then cfg.exp_base = math.tointeger(math.floor(cfg.exp_base + 0.5)) or 0 persist("exp_base") end
        cfg.exp_catchup, changed = p:slider("...catch-up", cfg.exp_catchup, 0, 100, 1, "%.0f", {desc = "Replaces the seed's " ..
            "catch-up multiplier (while your level is at or under the deepest floor's expected level plus the margin). 0 = the " ..
            "seed's.", enabled = cfg.exp_mult == 0 and scaled})
        if changed then cfg.exp_catchup = math.tointeger(math.floor(cfg.exp_catchup + 0.5)) or 0 persist("exp_catchup") end
        p:section("Overlays")
        cfg.show_status, changed = p:toggle("Connection line", cfg.show_status, "The connection status, top left.")
        if changed then persist("show_status") end
        cfg.show_feed, changed = p:toggle("Item feed", cfg.show_feed, "Items sent and received, chat and DeathLinks, top right.")
        if changed then persist("show_feed") end
        cfg.show_tracker, changed = p:toggle("Tracker", cfg.show_tracker, "Checked / total locations, left.")
        if changed then persist("show_tracker") end
        cfg.tracker_mode, changed = p:choice("Tracker detail", cfg.tracker_mode, {"Areas", "Floors"}, "Count per area of the tower, or per floor.")
        if changed then persist("tracker_mode") end
        cfg.show_warpmap, changed = p:toggle("Warp map counts", cfg.show_warpmap, {desc = "On the statues' warp map, the " ..
            "checks found / total of each floor on its bar. Needs a CleriaCore or CleriaOre that reports the warp map.",
            enabled = G.state ~= nil})
        if changed then persist("show_warpmap") end
        cfg.show_room, changed = p:toggle("Left in this room", cfg.show_room, "The current room's locations still to find, bottom left. " ..
                                         "A hinted one shows its item.")
        if changed then persist("show_room") end
        cfg.room_spoil, changed = p:toggle("...with what they hold", cfg.room_spoil, {desc = "Also show the item at each location " ..
                                          "(a spoiler: the server tells the client what every location holds).", enabled = cfg.show_room})
        if changed then persist("room_spoil") end
        cfg.export_state, changed = p:toggle("Tracker file", cfg.export_state,
            "Keeps state.json in this mod's data folder (modsdata/ysorigin.archipelago beside cleria.ini) up to date.")
        if changed then persist("export_state") end
        if #A.log > 0 then
            p:section("Recent events")
            for i = #A.log, math.max(1, #A.log - 5), -1 do p:info("", A.log[i], {color = "muted", desc = A.log[i]}) end
        end
    end)
end

return M
