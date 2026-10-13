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
    if (op == "to_title") { return_to_title = true; return true; }
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
        Actor* a = world.spawn(SpawnRequest{0, cast, Vec3{float(n[1]), float(n[2]), float(n[3])}, float(arg(5)), int(arg(6)), int(arg(4))});
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
    return false;
};
