// The player's status ailments in the view (src/cleria/ailments.hpp, build/mech_re/STATUS_AILMENTS.md,
// issue #60), included in render_window after view_hudstate.inl:
//   ail_apply(bit)        FUN_004630a0: the timer, and for a bit that was not active the attached effect
//                         (EF2 58 / 56 / 59 / 57) with its SE (117..120) and the popup(s)
//   ail_on_hit(st, dmg)   FUN_004fd350's status loop, after a hit's damage left the player's HP
//   ail_tick()            FUN_00462550, once a logic tick: the countdown, the poison damage, the endings;
//                         a Fix kills the effects and freezes it, the thaw / a room load re-creates them
//                         (FUN_004612e0: no SE, no popup)
//   ail_drive()           CLERIA_DRIVE: ail=<bit> (apply), ailhit=<status> (a 1-damage hit's roll), ailstate
// The readers are where the engine has them: the mover (heavy: no run stage, the lift-off x 2/3; confuse:
// the heading + pi), the two damage formulas (curse), the HUD lines (view_hud.inl).
// [not modelled] the state in the save record (g_flags 528..532 lie past the 0x200 flags kept here: a
// load starts clean); path B (FUN_004fe1a0, the hit flag +0x11c & 8: no hit record carries the flag
// here); the sound environment 0x19 (§6.5); the stick-tap offset (§6.3 item 2); the popups' other
// languages. [H] the effect's attach offset (0, 0, 0).
ef2sim::Effect* ail_fx[4] = {nullptr, nullptr, nullptr, nullptr};   // P+0x73c / +0x740 / +0x744 / +0x748
bool ail_was_frozen = false;
std::string ail_room;
auto ail_attach = [&](int i) {
    return attach_to_player(spawn_sim_fx(ailments::kEffect[i], {}, {}, 1.0f, {}, 0, {}), {0.0f, 0.0f, 0.0f},
                            camera_yaw());
};
auto ail_apply = [&](std::uint32_t bit) {
    const ailments::Applied r =
        ailments::apply(ail, bit, std::uint32_t(game.g_flags[217]), game.g_flags[191] != 0);
    if (r.index < 0) return;
    const int i = r.index;
    std::printf("  [ail] t%llu %s for %d ticks%s\n", (unsigned long long)tick_count, ailments::kHudName[i], ail.t[i],
                r.announce ? "" : " (refreshed)");
    if (!r.announce) return;
    if (!fx_live(ail_fx[i])) {                                       // P.fx[bit] == 0: the effect and the SE
        ail_fx[i] = ail_attach(i);
        sfx_at(ailments::kSound[i], feet, kSfxAnimVol);
    }
    const Vec3 o{feet.x, feet.y + kOriginH, feet.z};
    if (popups.size() < 32)
        popups.push_back(make_popup(ailments::kName[i], ailments::kColour[i], 0.75f, {o.x, o.y + 1.0f, o.z}));
    if (bit == ailments::kCurse && popups.size() < 32)
        popups.push_back(make_popup(ailments::kCurseLine, ailments::kColour[i], 0.75f, {o.x, o.y + 1.5f, o.z}));
};
auto ail_on_hit = [&](int status, int dmg) {
    if (status == 0) return;
    ailments::on_hit(std::uint32_t(status), dmg, float(player.hp), game.g_flags[156] != 0, game.g_flags[221] != 0,
                     game.g_flags[144], std::uint32_t(game.g_flags[217]),
                     [&] { return int((pick_rng.next() >> 17) % 100u); }, ail_apply);
};
auto ail_tick = [&]() {
    const bool frozen = csMgr.world_frozen();
    if (frozen) {                                                    // FUN_0056e8d0: a Fix kills the four effects
        for (ef2sim::Effect*& f : ail_fx) { if (fx_live(f)) f->kill = true; f = nullptr; }
        ail_was_frozen = true;
        return;                                                      // 0x462550: nothing counts under a Fix
    }
    const bool recreate = ail_was_frozen || ail_room != room.name;   // FUN_004612e0: the Unfix, a room load
    ail_was_frozen = false;
    ail_room = room.name;
    const ailments::Ticked r = ailments::tick(ail, float(player.max_hp), float(player.hp),
                                              (inv.equip_flags & 0x20048u) != 0);   // the Blue Necklace's 0x20000
    if (r.poison > 0) {
        sfx_at(ailments::kSound[0], feet, kSfxAnimVol);
        player.hp -= r.poison;
        damage_popup(r.poison, 0xff00ff, {feet.x, feet.y + kOriginH, feet.z});
    }
    for (int i = 0; i < 4; ++i) {
        if (r.cleared & ailments::kBits[i]) {
            std::printf("  [ail] t%llu %s ends\n", (unsigned long long)tick_count, ailments::kHudName[i]);
            if (fx_live(ail_fx[i])) ail_fx[i]->kill = true;
            ail_fx[i] = nullptr;
        } else if (recreate && ail.has(ailments::kBits[i]) && !fx_live(ail_fx[i])) {
            ail_fx[i] = ail_attach(i);
        }
    }
};
auto ail_drive = [&](const std::string& cmd) -> bool {
    if (cmd.rfind("ail=", 0) == 0) { ail_apply(std::uint32_t(std::strtoul(cmd.c_str() + 4, nullptr, 0))); return true; }
    if (cmd.rfind("ailhit=", 0) == 0) { ail_on_hit(int(std::strtoul(cmd.c_str() + 7, nullptr, 0)), 1); return true; }
    if (cmd == "ailstate") {
        std::printf("  [ail] mask 0x%02x timers %d %d %d %d fx %d%d%d%d hp %d\n", unsigned(ail.mask), ail.t[0], ail.t[1],
                    ail.t[2], ail.t[3], int(fx_live(ail_fx[0])), int(fx_live(ail_fx[1])), int(fx_live(ail_fx[2])),
                    int(fx_live(ail_fx[3])), player.hp);
        return true;
    }
    return false;
};
