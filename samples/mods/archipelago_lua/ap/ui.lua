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
        if not cfg.show_feed or #A.feed == 0 then return end
        local now, S = cleria.time(), d.scale
        local px, bw, pad = 16 * S, 430 * S, 10 * S
        local shown = 0
        for i = #A.feed, 1, -1 do
            if shown >= 6 or now - A.feed[i].t > 10 then break end
            shown = shown + 1
        end
        local y = d.h * 0.24
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

    -- the connection line, top left
    cleria.hud.layer("status", function(d)
        local s = A.session.status
        if not cfg.show_status or (s == "idle" and A.logic.st.seed == "") then return end
        local S = d.scale
        local px = 14.5 * S
        local t = "AP  " .. (A.mismatch == "" and A.session:status_text(cleria.time()) or A.mismatch)
        if s == "connected" and A.logic.configured then
            t = t .. "  \u{2022}  " .. ys.count(A.logic.st.checks) .. "/" .. #A.logic.regs .. " checks"
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
        if not cfg.show_tracker or not A.logic.configured then return end
        local zones, lowest, order = {}, {}, {}
        for _, r in ipairs(A.logic.regs) do
            local l = A.table.loc[r.id]
            local z = (l and l.zone ~= "") and l.zone or "Other"
            local fl = l and tonumber(l.floor:match("^(%d+)")) or 0
            if not zones[z] then zones[z] = {0, 0} lowest[z] = 999 order[#order + 1] = z end
            if fl > 0 then lowest[z] = math.min(lowest[z], fl) end
            zones[z][2] = zones[z][2] + 1
            if A.logic.st.checks[r.id] then zones[z][1] = zones[z][1] + 1 end
        end
        table.sort(order, function(x, y) if lowest[x] ~= lowest[y] then return lowest[x] < lowest[y] end return x < y end)
        local S = d.scale
        local px, lh = 14 * S, 19 * S
        local x0, y0, w = 14 * S, d.h * 0.30, 250 * S
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

    -- the Blinding Fog trap: a grey haze fading in and out ("0" sorts it under this mod's other layers)
    cleria.hud.layer("0fog", function(d)
        local t = A.fog_t
        if t <= 0 then return end
        local k = math.min(1, t / 60, (30 * 60 - t) / 60)
        d:rect(0, 0, d.w, d.h, {color = 0xC4C8CE, alpha = 205 / 255 * k})
    end)

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
        for _, u in ipairs(A.logic.opt.unsupported) do p:info("Not supported", u, {color = "warn"}) end
        p:section("Overlays")
        cfg.show_status, changed = p:toggle("Connection line", cfg.show_status, "The connection status, top left.")
        if changed then persist("show_status") end
        cfg.show_feed, changed = p:toggle("Item feed", cfg.show_feed, "Items sent and received, chat and DeathLinks, top right.")
        if changed then persist("show_feed") end
        cfg.show_tracker, changed = p:toggle("Tracker", cfg.show_tracker, "Checked / total locations per area, left.")
        if changed then persist("show_tracker") end
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
