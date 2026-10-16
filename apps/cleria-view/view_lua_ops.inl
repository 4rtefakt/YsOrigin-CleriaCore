// cleria-view — the named writes of the mod API (Host::game_op; API 7, build/shell_re/API7.md C). Textual part of
// view_game.inl: #included inside render_window, after the game-over flow, where everything it touches is in scope.
// Each op is what a script or a drive command already does to that state; nothing here invents a new path.
g_lua.b.op = [&](const std::string& mod, const std::string& op, const std::vector<double>& n, const std::string& text,
                 std::vector<double>* out) -> bool {
    auto arg = [&](std::size_t i, double d = 0.0) { return i < n.size() ? n[i] : d; };
    const bool alive = player.hp >= 1 && death_t == 0;
    if (op == "set_position") {                          // the drive's at3: the floor under (x, y + 1, z), else y itself
        if (!alive || n.size() < 3) return false;
        const float x = float(n[0]), y = float(n[1]), z = float(n[2]);
        const float fl = floor_at(phys_mesh(room), x, z, y + 1.0f);
        feet = {x, fl > kNoFloor ? fl : y, z};
        if (fl > kNoFloor) ledge_serial = -1;
        pv = VerticalState{}; pvel = {}; last_floor = feet.y;
        if (n.size() >= 4) {
            const float yaw = float(n[3]) * 3.14159265358979323846f / 180.0f;
            face_dir = {std::sin(yaw), 0.0f, std::cos(yaw)};
            facing = heading_to_facing(face_dir.x, face_dir.z, cam.yaw);
        }
        return true;
    }
    if (op == "set_mp") { player.sp_cur = player.sp_lag = std::clamp(float(arg(0)), 0.0f, player.sp_max); return true; }
    if (op == "set_boost") { player.boost = std::clamp(float(arg(0)), 0.0f, skillmp::kBoostFull); return true; }
    if (op == "set_exp") {                               // the total; levels gained follow (the game never takes a level back)
        player.exp = float(arg(0));
        level_ups();
        return true;
    }
    if (op == "invulnerable") { player_iframes = std::max(player_iframes, int(arg(0))); return true; }
    if (op == "equipment") {
        if (!out) return false;
        *out = {double(game.g_flags[151]), double(game.g_flags[152]), double(game.g_flags[153]), double(inv.accessory),
                double(inv.magic), double(inv.held)};
        return true;
    }
    if (op == "equip") {                                 // an owned item of the slot's own range, or -1 (none) where the game allows it
        const int slot = int(arg(0)), item = int(arg(1, -1));
        const bool owned = item >= 0 && item < 0x80 && game.g_flags[item] >= 1;
        auto in = [&](int lo, int hi) { return item >= lo && item <= hi && owned; };
        switch (slot) {
            case 0: if (!in(0, 5)) return false; game.g_flags[151] = item; break;
            case 1: if (!in(6, 29)) return false; game.g_flags[152] = item; break;
            case 2: if (!in(30, 53)) return false; game.g_flags[153] = item; break;
            case 3: if (item != -1 && !in(54, 65)) return false; inv.accessory = item; break;
            case 4: if (item != -1 && !in(116, 119)) return false; inv.magic = item; break;
            case 5: if (item != -1 && !in(66, 115)) return false; inv.held = item; break;
            default: return false;
        }
        player.max_hp = int(player_stats().max_hp);
        player.hp = std::min(player.hp, player.max_hp);
        return true;
    }
    if (op == "status") {
        if (!out) return false;
        out->clear();
        for (int i = 0; i < 4; ++i) out->push_back(ail.has(ailments::kBits[i]) ? double(std::max(ail.t[i], 1)) : 0.0);
        return true;
    }
    if (op == "set_status") {                            // on: as a hit inflicts it; off: its timer runs out at the next tick
        const int i = int(arg(0));
        if (i < 0 || i > 3) return false;
        if (arg(1) != 0.0) ail_apply(ailments::kBits[i]);
        else if (ail.has(ailments::kBits[i])) ail.t[i] = 1;
        return true;
    }
    if (op == "exits") {
        if (!out) return false;
        out->clear();
        for (const SenTrigger& t : room.sen.triggers) {
            int sc = 0;
            for (std::size_t k = 2; k < t.dest_scene.size() && std::isdigit(static_cast<unsigned char>(t.dest_scene[k])); ++k) sc = sc * 10 + (t.dest_scene[k] - '0');
            out->push_back(double(sc));
        }
        return true;
    }
    if (op == "take_exit") {                             // the drive's exit=i: a committed door of this room
        const std::size_t i = std::size_t(arg(0));
        if (!alive || csMgr.world_frozen() || arg(0) < 0 || i >= room.sen.triggers.size()) return false;
        const SenTrigger t = room.sen.triggers[i];
        transition(t.dest_scene, &t);
        return true;
    }
    if (op == "goto_room") {                             // a script's ChangeScene: the room's own arrival point
        if (!alive || csMgr.world_frozen() || !scenes.find_room(text)) return false;
        transition(text);
        return true;
    }
    if (op == "speed") {
        if (arg(0, 1.0) == 1.0) g_lua.speed.erase(mod); else g_lua.speed[mod] = arg(0, 1.0);
        return true;
    }
    if (op == "retry") {                                 // only on the game-over screen, as its Retry does
        if (player.hp >= 1) return false;
        retry();
        return true;
    }
    if (op == "to_title") { return_to_title = true; running = false; accum = 0; return true; }   // as the book's own return
    if (op == "load") {                                  // a slot that exists, as the book's Load does
        const int s = int(arg(0, -1));
        std::error_code ec;
        if (s < 0 || s > 63 || content_session_pending() || !fs::exists(slot_path(s), ec)) return false;
        g_content.load_slot(s);
        return true;
    }
    if (op == "seed_random") {                           // the game's one generator (xorshift128, 0x74622c)
        const std::uint32_t s = std::uint32_t(arg(0));
        game.rs[0] = s ? s : 1u; game.rs[1] = 362436069u; game.rs[2] = 521288629u; game.rs[3] = 88675123u;
        return true;
    }
    if (op == "spawn_actor") {                           // a script's spawn op: an enemy cast at a point (id 0 = a fresh one)
        if (n.size() < 4 || !out) return false;
        const int cast = int(n[0]);
        if (!etab.get(cast) || !casts.get(cast)) return false;        // enemies only
        g_lua.spawn_by = mod;
        Actor* a = world.spawn(SpawnRequest{0, cast, Vec3{float(n[1]), float(n[2]), float(n[3])}, float(arg(5)), int(arg(6)), int(arg(4))});
        g_lua.spawn_by.clear();
        if (!a) return false;
        csHost.world_changed = true;
        *out = {double(a->instance_id)};
        return true;
    }
    if (op == "remove_actor") {                          // gone at once: no EXP, no drops (the drive's killall)
        Actor* a = world.find(int(arg(0, -1)));
        if (!a || a->is_player() || !a->is_enemy()) return false;
        world.despawn(a->instance_id);
        csHost.world_changed = true;
        return true;
    }
    if (op == "set_actor") {                             // hp (at least 1: a mod does not kill this way), position, heading
        Actor* a = world.find(int(arg(0, -1)));
        if (!a || a->is_player() || !a->is_enemy() || !a->alive()) return false;
        if (n.size() > 1 && std::isfinite(n[1])) a->hp = std::clamp(float(n[1]), 1.0f, a->max_hp);
        if (n.size() > 2 && std::isfinite(n[2])) a->pos.x = float(n[2]);
        if (n.size() > 3 && std::isfinite(n[3])) a->pos.y = float(n[3]);
        if (n.size() > 4 && std::isfinite(n[4])) a->pos.z = float(n[4]);
        if (n.size() > 5 && std::isfinite(n[5])) a->yaw = float(n[5]) * 3.14159265358979323846f / 180.0f;
        return true;
    }
    if (op == "se") {                                    // a sound of EFFECTDEF, as a script's Se_Play
        audio.play(int(arg(0)), kSfxAnimVol * float(arg(1, 1.0)));
        std::printf("  [lua] se %d\n", int(arg(0)));
        return true;
    }
    if (op == "bgm") { if (!csHost.bgm_play_fn) return false; csHost.bgm_play_fn(int(arg(0))); return true; }   // a script's Bgm_Play
    if (op == "bgm_stop") { if (!csHost.bgm_stop_fn) return false; csHost.bgm_stop_fn(int(arg(0, 1000))); return true; }
    if (op == "fade") {                                  // the fader a script drives (Fader_Color / Current / Target / Time)
        csHost.fade_rgb = std::uint32_t(arg(0)) & 0xffffffu;
        csHost.fade_cur = std::clamp(float(arg(1)), 0.0f, 1.0f);
        csHost.fade_tgt = std::clamp(float(arg(2)), 0.0f, 1.0f);
        csHost.fade_rate = 1.0f / float(std::max(1.0, arg(3, 20.0)));
        return true;
    }
    // A puppet: the event cast of a character (C500 Yunica 116, C510 Hugo 117, C520 Toal 118: class 4, no brain, no
    // stats) spawned as a script spawns an NPC. The mod owns its place, heading and motion from then on.
    if (op == "puppet_spawn") {
        if (n.size() < 4 || !out) return false;
        const int cast = 115 + std::clamp(int(n[0]), 1, 3);
        Actor* a = world.spawn(SpawnRequest{0, cast, Vec3{float(n[1]), float(n[2]), float(n[3])}, float(arg(4)), 0, 0});
        if (!a) return false;
        static const char* const kHero[] = {"C_111", "C_112", "C_113"};      // the three players' gameplay sheets
        a->sprite_override = kHero[std::clamp(int(n[0]), 1, 3) - 1];
        a->aia_file = 14 + std::clamp(int(n[0]), 1, 3);                     // the same sheets by their file ids (aso_aia_name 15..17)
        a->pattern.tag = 305; a->pattern.frame = 0;                         // standing, weapon in hand (the players' idle tag)
        a->height = kOriginH;                                               // its origin where a player's is (its effects hang from it)
        g_lua_puppets.insert(a->instance_id);
        csHost.world_changed = true;
        *out = {double(a->instance_id)};
        return true;
    }
    if (op == "puppet_set" || op == "puppet_remove") {
        const int id = int(arg(0, -1));
        Actor* a = g_lua_puppets.count(id) ? world.find(id) : nullptr;
        if (!a) { g_lua_puppets.erase(id); g_lua_targets.erase(id); return false; }
        if (op == "puppet_remove") {
            world.despawn(id);
            g_lua_puppets.erase(id); g_lua_targets.erase(id);
            csHost.world_changed = true;
            return true;
        }
        if (n.size() > 1 && std::isfinite(n[1])) a->pos.x = float(n[1]);
        if (n.size() > 2 && std::isfinite(n[2])) a->pos.y = float(n[2]);
        if (n.size() > 3 && std::isfinite(n[3])) a->pos.z = float(n[3]);
        if (n.size() > 4 && std::isfinite(n[4])) {
            a->yaw = float(n[4]) * 3.14159265358979323846f / 180.0f;
            a->look.cmd = a->look.dir = heading_of_yaw(a->yaw);
        }
        if (n.size() > 5 && std::isfinite(n[5])) a->pattern.tag = int(n[5]);      // a tag of its sheet
        if (n.size() > 7 && std::isfinite(n[7]) && *aso_aia_name(std::int32_t(n[7]))) a->aia_file = std::int32_t(n[7]);   // the sheet
        if (n.size() > 9 && std::isfinite(n[9])) { if (n[9] != 0) g_lua_targets.insert(id); else g_lua_targets.erase(id); }   // target: the monsters chase and hit it
        if (n.size() > 10 && std::isfinite(n[10])) a->mod_spec = std::uint32_t(std::max(0.0, n[10])) & 0xffffffu;   // glow: added to its sprite
        if (n.size() > 11 && std::isfinite(n[11])) {         // aura: an effect that follows it (the Boost's 39 / 171), 0 = none
            static std::map<int, std::pair<int, ef2sim::Effect*>> auras;   // puppet -> the effect id and the effect
            if (auras.size() > 32) auras.clear();
            auto& au = auras[id];
            const int want = int(n[11]);
            if (au.first != want || (want && !fx_live(au.second))) {
                if (fx_live(au.second)) au.second->kill = true;
                au = {want, nullptr};
                if (want > 0) {
                    ef2sim::SpawnParams sp;
                    sp.side = 2; sp.power = 0.0f; sp.owner = id;
                    if (ef2sim::Effect* f = spawn_sim_fx(want, {}, {}, 1.0f, sp, 0, {})) {
                        f->local = ef2sim::translation(0.0f, 0.0f, 0.0f);
                        f->attach_parent = &actor_mats[id];
                        f->attach_mode = 2;
                        au.second = f;
                    }
                }
            }
        }
        if (n.size() > 8 && std::isfinite(n[8])) {           // hittable: a body the player's hits find (they raise puppet_hit)
            const bool on = n[8] != 0;
            a->hp = a->max_hp = on ? 1.0f : 0.0f;
            a->hurt_radius = 0.6f; a->hurt_half_h = 0.6f; a->hurt_shape = 0; a->hurt_off = 0.0f;
            a->ai.hittable = on;
        }
        if (n.size() > 6 && std::isfinite(n[6])) a->pattern.frame = std::max(0, int(n[6]));
        return true;
    }
    if (op == "pose") {                                  // the player's place, heading and the frame the script shows now
        if (!out) return false;
        *out = {double(feet.x), double(feet.y), double(feet.z), double(std::atan2(face_dir.x, face_dir.z) * 57.29577951f),
                double(avm.anim_tag()), double(avm.frame()), -1.0,
                // the colour added to the sprite now, as the draw computes it: the charged skill's pulse, a mod's glow
                double(skill_charge.level == 2 && csMgr.fix_counts[0] + csMgr.fix_counts[1] == 0
                           ? hitfx::charged_glow(player_spec.colour(), character, skillmp::bracelet_slot(inv.magic), player_glow_phase)
                           : (g_lua.look_on && g_lua.look_tint != 0xffffffffu ? (g_lua.look_tint & 0xffffffu) : player_spec.colour())),
                player.boosting ? (character == 3 ? 171.0 : 39.0) : 0.0};   // the Boost aura's effect while it is on
        for (std::int32_t i = 0; i < 269; ++i)           // the sheet drawn now, as its file id (the script swaps it: anime_File)
            if (cur_aia == aso_aia_name(i)) { (*out)[6] = double(i); break; }
        return true;
    }
    if (op == "script_stop") return csMgr.stop_current();          // the script whose op a filter answers ends after it
    if (op == "script_run") return !text.empty() && csMgr.start(text, {}, 13);   // a script by name, the player its owner (the drive's run=)
    if (op == "tune") {                                  // (key, value): this mod's factor; the motion constants follow
        static const CastMotion base = pm;               // CASTINFO's own, before any mod
        const int k = int(arg(0, -1));
        if (k < 0 || k > 5) return false;
        auto& a = g_lua.tune.try_emplace(mod, std::array<double, 6>{1, 1, 1, 1, 1, 0}).first->second;
        a[std::size_t(k)] = k == 5 ? std::clamp(std::floor(arg(1)), 0.0, 8.0) : std::clamp(arg(1, 1.0), 0.0, 10.0);
        pm.speed_base = base.speed_base * float(std::clamp(lua_tune(0), 0.1, 5.0));
        pm.jump_v0 = base.jump_v0 * float(std::clamp(lua_tune(1), 0.1, 4.0));
        return true;
    }
    if (op == "heal" || op == "hurt") {                  // HP given or taken as it is: no formula, no i-frames; 0 is the death
        if (!alive || arg(0) < 0) return false;
        const int d = int(arg(0)) * (op == "heal" ? 1 : -1);
        player.hp = std::clamp(player.hp + d, 0, player.max_hp);
        return true;
    }
    if (op == "hit") {                                   // (actor, damage, knock, class): lands at the next hit scan
        const int id = int(arg(0, -1));
        const Actor* a = world.find(id);
        if (!alive || !a || a->is_player() || !a->is_enemy() || !a->alive() || g_lua_puppets.count(id)) return false;
        g_lua.hits.push_back({id, std::max(0, int(arg(1))), float(arg(2)), std::clamp(int(arg(3, 3)), 0, 3)});
        return true;
    }
    // ---- cleria.camera: the mods' own layer over the game's camera (view_camera.inl ModCam) ----
    if (op == "cam_get") {
        if (!out) return false;
        *out = {double(g_cam_out.eye.x), double(g_cam_out.eye.y), double(g_cam_out.eye.z), double(g_cam_out.target.x),
                double(g_cam_out.target.y), double(g_cam_out.target.z), double(g_cam_out.fov), double(g_mod_cam.dist), double(g_mod_cam.fov)};
        return true;
    }
    if (op == "cam_set") {                               // (dist, fov, ox, oy, oz): a NaN keeps the value
        if (std::isfinite(arg(0, NAN))) g_mod_cam.dist = std::clamp(float(arg(0)), 0.2f, 5.0f);
        if (std::isfinite(arg(1, NAN))) g_mod_cam.fov = std::clamp(float(arg(1)), 0.3f, 2.5f);
        if (std::isfinite(arg(2, NAN))) g_mod_cam.off.x = std::clamp(float(arg(2)), -50.0f, 50.0f);
        if (std::isfinite(arg(3, NAN))) g_mod_cam.off.y = std::clamp(float(arg(3)), -50.0f, 50.0f);
        if (std::isfinite(arg(4, NAN))) g_mod_cam.off.z = std::clamp(float(arg(4)), -50.0f, 50.0f);
        return true;
    }
    if (op == "cam_reset") { g_mod_cam = ModCam{}; return true; }
    if (op == "cam_shake") {                             // a script's Camera_Shake: amplitudes, frames
        shake(float(arg(0)), float(arg(1)), float(arg(2)), std::clamp(int(arg(3, 20)), 1, 600));
        return true;
    }
    if (op == "cam_look") {                              // a script's Scroll_MoveTo / Scroll_Return: the camera's subject
        if (n.size() >= 3 && std::isfinite(n[0])) scroll_moveto(float(n[0]), float(n[1]), float(n[2]), std::clamp(int(arg(3, 30)), 0, 600));
        else scroll_return(std::clamp(int(arg(3, 30)), 0, 600));
        return true;
    }
    if (op == "hud_show") {                              // (part bit, on): this mod's hidden parts
        const unsigned bit = unsigned(arg(0));
        unsigned& v = g_lua.hud_hidden[mod];
        v = arg(1) != 0 ? (v & ~bit) : (v | bit);
        if (!v) g_lua.hud_hidden.erase(mod);
        return true;
    }
    if (op == "skip_cutscenes") {                        // this mod asks every cutscene and movie to skip ("Always")
        if (arg(0) != 0) g_skip_mods.insert(mod); else g_skip_mods.erase(mod);
        return true;
    }
    if (op == "pause") {                                 // this mod holds the game (as a window of its own would)
        if (arg(0) != 0) g_lua.pause.insert(mod); else g_lua.pause.erase(mod);
        return true;
    }
    if (op == "boss") {                                  // the boss gauge: who owns it and what it shows
        if (!out || hud.boss_frac < 0.0f) return false;
        *out = {double(hud.boss_id), double(hud.boss_cur), double(hud.boss_max)};
        return true;
    }
    if (op == "actor_form") {                            // a mod's variant: multipliers of what the spawn computed, the look, the handler
        Actor* a = world.find(int(arg(0, -1)));
        if (!a || a->is_player() || !a->is_enemy() || !a->alive() || g_lua_puppets.count(a->instance_id)) return false;
        auto mul = [&](std::size_t i) { const double v = arg(i, NAN); return std::isfinite(v) ? float(std::clamp(v, 0.01, 1000.0)) : 1.0f; };
        if (mul(1) != 1.0f) { a->max_hp = std::max(1.0f, a->max_hp * mul(1)); a->hp = a->max_hp; }
        a->atk *= mul(2); a->def *= mul(3); a->exp_reward *= mul(4);
        a->sp_reward = int(float(a->sp_reward) * mul(5));
        if (arg(6, -1) > 0) a->scale = float(std::clamp(arg(6), 0.2, 5.0));
        if (arg(7, -1) >= 0) a->tint = 0xff000000u | (std::uint32_t(arg(7)) & 0xffffffu);
        if (std::isfinite(arg(8, NAN))) {
            a->mod_ai_off = arg(8) == 0;
            if (!a->mod_ai_off) a->pattern = PatternPin{};   // world.apply's picture goes with the hold
        }
        if (std::isfinite(arg(9, NAN))) {
            a->mod_boss = arg(9) != 0;
            if (!a->mod_boss && gauge_owner == a->instance_id) { hud.hide_boss_gauge(); gauge_owner = 0; }
        }
        const std::size_t tab = text.find('\t');
        if (tab != std::string::npos) {                  // "kind \t name [\t sheet file]"
            const std::size_t tab2 = text.find('\t', tab + 1);
            if (tab > 0) a->mod_kind = text.substr(0, tab);
            const std::string name = text.substr(tab + 1, tab2 == std::string::npos ? std::string::npos : tab2 - tab - 1);
            if (!name.empty()) a->mod_name = name;
            if (tab2 != std::string::npos && tab2 + 1 < text.size()) {   // a sheet of the mod's own, drawn in place of the cast's
                a->sprite_override = text.substr(tab2 + 1);
                a->aia_file = -1;
                csHost.world_changed = true;
            }
        }
        return true;
    }
    if (op == "strike") {                                // an atk-power hit on the player, the game's own way (a script's Chr_Damage)
        if (!alive || arg(0) <= 0) return false;
        csHost.player_damage.push_back({float(arg(0)), false, 0, float(arg(1))});
        return true;
    }
    // ---- cleria.world.snapshot / apply: the room's monsters as numbers, and a room made to match them ----
    if (op == "world_snapshot") {                        // 12 numbers a monster: id cast level x y z yaw hp max_hp tag frame sheet
        if (!out) return false;
        out->clear();
        for (const Actor& a : world.actors()) {
            if (a.is_player() || !a.is_enemy() || !a.alive() || g_lua_puppets.count(a.instance_id)) continue;
            int tag = a.pattern.pinned() ? a.pattern.tag : -1, frame = a.pattern.frame;
            if (tag < 0)
                for (const EnemyRender& er : render)
                    if (er.actor == &a && er.avm.ok()) { tag = int(er.avm.anim_tag()); frame = int(er.avm.frame()); break; }
            out->insert(out->end(), {double(a.instance_id), double(a.cast_id), double(a.level), double(a.pos.x), double(a.pos.y), double(a.pos.z),
                                     double(a.yaw * 57.29577951f), double(a.hp), double(a.max_hp), double(tag), double(frame), double(a.aia_file)});
        }
        return true;
    }
    if (op == "world_apply") {                           // (flags: 1 hold the handlers, 2 remove the others; then 12 a monster) -> how many matched
        if (!alive || n.empty()) return false;
        const bool hold = (int(n[0]) & 1) != 0, sweep = (int(n[0]) & 2) != 0;
        // A monster that died over there: its death section and sound here, no EXP, no SP (the kill is not this
        // player's: +0x12c & 0x4000000 is what the kill credit sets and tests).
        auto fall = [&](Actor& a) {
            a.mod_ai_off = false;
            a.pattern = PatternPin{};
            a.flags12c |= 0x4000000u;
            apply_damage(a.hp, a.flags, 99999999);
            bool scripted = false;
            for (EnemyRender& er : render) {
                if (er.actor != &a || !er.avm.ok()) continue;
                if (er.avm.section() != 5) { er.avm.set_section(5); er.avm.tick(); }
                er.atk_win = 0; er.atk_armed = false; er.atk_age = 0;
                scripted = true;
                break;
            }
            if (!scripted) { world.despawn(a.instance_id); csHost.world_changed = true; return; }   // nothing to play it with
            sfx_at(kSfxDeathEntry, a.pos, kSfxAnimVol);
            g_lua_fallen[a.instance_id] = (unsigned long long)tick_count + 150;
        };
        std::set<int> seen;
        int done = 0;
        for (std::size_t i = 1; i + 12 <= n.size(); i += 12) {
            const int id = int(n[i]), cast = int(n[i + 1]);
            if (id <= 0 || !etab.get(cast) || !casts.get(cast) || g_lua_puppets.count(id)) continue;
            seen.insert(id);
            const float hp = float(n[i + 7]);
            Actor* a = world.find(id);
            if (a && (a->is_player() || !a->is_enemy())) continue;         // the id is somebody else's here
            if (a && a->cast_id != cast) { world.despawn(id); csHost.world_changed = true; continue; }   // rebuilt by the next apply
            if (a && !a->alive()) continue;
            if (!a) {
                if (hp < 1.0f) continue;
                g_lua.spawn_by = mod;
                a = world.spawn(SpawnRequest{id, cast, Vec3{float(n[i + 3]), float(n[i + 4]), float(n[i + 5])}, float(n[i + 6]), 0, int(n[i + 2])});
                g_lua.spawn_by.clear();
                if (!a) continue;
                csHost.world_changed = true;
            }
            if (hp < 1.0f) { fall(*a); continue; }           // it died over there
            a->pos = {float(n[i + 3]), float(n[i + 4]), float(n[i + 5])};
            a->yaw = float(n[i + 6]) / 57.29577951f;
            a->max_hp = std::max(1.0f, float(n[i + 8]));
            a->hp = std::min(hp, a->max_hp);
            if (hold) {
                a->mod_ai_off = true;
                if (n[i + 9] >= 0) { a->pattern.tag = int(n[i + 9]); a->pattern.frame = std::max(0, int(n[i + 10])); }
                if (n[i + 11] >= 0 && *aso_aia_name(std::int32_t(n[i + 11]))) a->aia_file = std::int32_t(n[i + 11]);
            }
            ++done;
        }
        if (sweep) {
            std::vector<int> gone;
            for (const Actor& a : world.actors())
                if (!a.is_player() && a.is_enemy() && a.alive() && !seen.count(a.instance_id) && !g_lua_puppets.count(a.instance_id)) gone.push_back(a.instance_id);
            for (int id : gone) if (Actor* a = world.find(id)) fall(*a);
        }
        if (out) *out = {double(done)};
        return true;
    }
    if (op == "player_look") {                           // (scale, glow 0xRRGGBB or -1): the player's own sprite
        g_lua.look_scale = std::isfinite(arg(0, NAN)) ? float(std::clamp(arg(0), 0.3, 3.0)) : g_lua.look_scale;
        if (std::isfinite(arg(1, NAN))) g_lua.look_tint = arg(1) < 0 ? 0xffffffffu : (0xff000000u | (std::uint32_t(arg(1)) & 0xffffffu));
        g_lua.look_on = g_lua.look_scale != 1.0f || g_lua.look_tint != 0xffffffffu;
        if (Actor* wp = world.player()) wp->scale = g_lua.look_scale;
        return true;
    }
    if (op == "se_file") return audio.play_file(fs::path(text), kSfxAnimVol * float(std::clamp(arg(0, 1.0), 0.0, 1.0)));   // a WAV of the mod's folder
    if (op == "bgm_file") return audio.play_bgm(fs::path(text));        // an Ogg Vorbis file of the mod's folder (LOOPSTART / LOOPLENGTH honoured)
    if (op == "new_game") {                              // back to the title, which starts the game at once
        g_lua.newgame_char = std::clamp(int(arg(0, character)), 1, 3);
        g_lua.newgame_diff = std::clamp(int(arg(1, difficulty)), 0, 4);
        return_to_title = true; running = false; accum = 0;
        return true;
    }
    if (op == "boss_bonus") {                            // (n): added to the pool; 0 empties it. -> the pool
        if (arg(0) >= 0) g_lua.boss_bonus = arg(0) > 0 ? std::min(g_lua.boss_bonus + arg(0), 1.0e7) : 0.0;   // < 0: only read
        if (out) *out = {g_lua.boss_bonus};
        return true;
    }
    if (op == "puppet_effect") {                         // (puppet, id, fwd, angle, voff, scale, attach): the effect a player's script makes, for show
        const int id = int(arg(0, -1));
        const Actor* pu = g_lua_puppets.count(id) ? world.find(id) : nullptr;
        if (!pu || n.size() < 7) return false;
        AsoVm::EffectEvent fe;
        fe.id = std::int32_t(n[1]); fe.fwd = float(n[2]); fe.angle_deg = float(n[3]); fe.voff = float(n[4]);
        fe.scale = std::clamp(float(n[5]), 0.05f, 20.0f); fe.attach = std::clamp(std::int32_t(n[6]), 0, 3);
        ef2sim::SpawnParams sp;
        sp.side = 2; sp.power = 0.0f; sp.owner = id;      // side 2: neither the player's nor the monsters': it hits nobody
        return spawn_generate_effect(fe, {pu->pos.x, pu->pos.y + kOriginH, pu->pos.z}, heading_of_yaw(pu->yaw), sp, id) != nullptr;
    }
    if (op == "show_bullet") {                           // a player_bullet event's numbers: the same bullet, hitting nobody
        if (n.size() < 19) return false;
        bullet::Spawn b;
        b.kind = int(n[0]); b.effect = int(n[1]);
        if (b.kind == 0xf) return false;                 // (a bomb is not a bullet)
        b.pos = {float(n[2]), float(n[3]), float(n[4])}; b.dir = {float(n[5]), float(n[6]), float(n[7])};
        b.speed = float(n[8]); b.accel = float(n[9]); b.gravity = float(n[10]);
        b.f158 = std::uint32_t(n[11]); b.f15c = std::uint32_t(n[12]); b.f160 = std::uint32_t(n[13]);
        b.f164 = float(n[14]); b.f168 = float(n[15]); b.f16c = float(n[16]); b.f170 = float(n[17]); b.f174 = float(n[18]);
        b.owner = 0; b.owner_monster = false; b.owner_atk = 0.0f; b.power = 0.0f; b.show_only = true;
        return bullets.spawn(b) != nullptr;
    }
    if (op == "familiars") {                             // the player's own helpers (Hugo's eyes): 8 numbers each
        if (!out) return false;
        out->clear();
        const Actor* pa = world.player();
        if (!pa) return true;
        for (const Actor& a : world.actors()) {
            if (!hugo::owned_eye(a, pa->instance_id) || !a.alive()) continue;
            int tag = 2, frame = 0;
            for (const EnemyRender& er : render)
                if (er.actor == &a && er.avm.ok()) { tag = int(er.avm.anim_tag()); frame = int(er.avm.frame()); break; }
            out->insert(out->end(), {double(a.cast_id), double(a.pos.x), double(a.pos.y), double(a.pos.z), double(a.yaw * 57.29577951f),
                                     double(tag), double(frame), double(a.aia_file)});
        }
        return true;
    }
    if (op == "prop_spawn") {                            // (cast, x, y, z, degrees): that cast's sprite as a puppet (no brain, no body)
        if (n.size() < 4 || !out) return false;
        const std::string stem = casts.sprite_stem(int(n[0]));
        if (stem.empty()) return false;
        Actor* a = world.spawn(SpawnRequest{0, 116, Vec3{float(n[1]), float(n[2]), float(n[3])}, float(arg(4)), 0, 0});
        if (!a) return false;
        a->sprite_override = stem;
        a->pattern.tag = 2; a->pattern.frame = 0;
        a->height = 0.0f;
        a->flags12c |= 1u;                               // no shadow of its own (Chr_Shadow's bit)
        g_lua_puppets.insert(a->instance_id);
        csHost.world_changed = true;
        *out = {double(a->instance_id)};
        return true;
    }
    return false;
};
