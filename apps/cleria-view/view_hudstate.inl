// The HUD state of build/audit/PLAYTEST_1_HUD.md (src/cleria/hudstate.hpp), included in
// render_window next to view_abilities.inl:
//   hud_state_tick()   once a logic tick: the buff expiry (FUN_00462c40) and the pulse counters
//                      0x76c184 / 0x76c188 (FUN_0043a540: +3 / +6 for widget 0)
//   hud_state_fill()   before the HUD draws: builder B's ready test, the buffs, the combo meter
//   damage_popup()     FUN_004fae00: a damage number (GoPopup) at an origin + 0.5 up
//   hud_drive()        CLERIA_DRIVE: hudstate (print it), drop=<item id> (an item at the feet)
std::int32_t hud_pulse_ticks = 0;
// The Defense / MP buffs' auras, pc\adol\adol_defup (EF2 0x25) and adol_mpup (0x26). FUN_00463f40 spawns one
// when its buff starts (the bit clear in 0x76c198, the handle player+0x750 / +0x754 null) [EXPORT 00463f40.c:
// 98-180, 198-277]; the scene entry FUN_00461d30 spawns it again for the new player actor while the bit is set
// [EXPORT 00461d30.c:60-270]; FUN_00462c40 kills it when the timer runs out [EXPORT 00462c40.c:76-98]. Placed
// at 0 and attached to the player with the camera's yaw, mode 2 [BYTES 0x464222..0x464285]; power 0, knock 1,
// rtype 0, class 0xff. The Strength buff has no aura in either routine. Here the three are one rule a tick:
// "the buff is on and it has no live aura" spawns, "off" kills.
auto buff_aura = [&](float value, int id, const char* name) {
    bool live = false;
    for (auto& f : fxsim.effects()) if (!f->dead && !f->kill && f->name == name) live = true;
    if (value == 0.0f) { if (live) kill_named_fx(name); return; }
    if (live) return;
    ef2sim::SpawnParams sp;
    sp.power = 0.0f; sp.knock = 1.0f; sp.rtype = 0; sp.cls = -1;
    if (ef2sim::Effect* a = attach_to_player(spawn_sim_fx(id, {}, {}, 1.0f, sp, 0, {}), {}, camera_yaw())) {
        a->name = name;
        std::printf("  [buff] aura %d on\n", id);
    }
};
auto hud_state_tick = [&]() {
    ++hud_pulse_ticks;                              // FUN_0043a540: not gated on a Fix
    // 0x462c5b: no buff count-down while a Fix holds
    if (!csMgr.world_frozen() && hudstate::tick_buffs(inv)) (void)player_stats();   // FUN_00420c40 | 0x75: the buffs left the stats
    buff_aura(inv.def_buff, 0x25, "#buff_def");
    buff_aura(inv.mp_buff, 0x26, "#buff_mp");
    // The Boost aura (39, Toal's 171; player+0x758) the same way: the scene entry spawns it again for the new player
    // actor while the mode bit 0x20 is set [EXPORT 00461d30.c:280-388] -- a door crossed while boosting, a load or a
    // Retry of a snapshot taken inside a Boost. try_boost spawns the first one, end_boost kills it. Only then: an
    // aura that ran out by itself leaves its handle set and is not spawned again.
    const bool aura_entry = boost_aura_entry;
    boost_aura_entry = false;
    if (aura_entry && player.boosting) {
        bool live = false;
        for (auto& f : fxsim.effects()) if (!f->dead && !f->kill && f->name == "#boost_aura") live = true;
        if (!live)
            if (ef2sim::Effect* a = attach_to_player(spawn_sim_fx(character == 3 ? 171 : 39, {}, {}, 1.0f, {}, 0, {}), {}, camera_yaw())) {
                a->name = "#boost_aura";
                std::printf("  [boost] aura %d on again\n", character == 3 ? 171 : 39);
            }
    }
};
auto hud_state_fill = [&]() {
    skillmp::CostIn ci;
    ci.chr = character;
    ci.slot = skillmp::bracelet_slot(inv.magic);
    ci.difficulty = difficulty >= 2 ? difficulty - 2 : 0;   // DAT_0076bb5c
    ci.bc80 = std::uint32_t(game.g_flags[217]);
    ci.acc_mp = (inv.equip_flags & 0x200000u) != 0;
    ci.boosting = player.boosting;
    ci.adol_alt = character == 0 && skin == 1;              // Napishtim: 0x68a408 = 1500 / 1800 / 1200 (ADOL.md §3)
    player.skill_ready = hudstate::skill_ready(ci, player.sp_cur);
    player.charge_level = skill_charge.level;
    player.pulse = hud_pulse_ticks * (skill_charge.level == 2 ? 6 : 3);
    player.buff_val[0] = inv.atk_buff; player.buff_val[1] = inv.def_buff; player.buff_val[2] = inv.mp_buff;
    player.buff_timer[0] = inv.timer_atk; player.buff_timer[1] = inv.timer_def; player.buff_timer[2] = inv.timer_mp;
    player.buff_full = hudstate::buff_full(std::uint32_t(game.g_flags[217]));
    player.combo = kill_combo.count;
    player.combo_timer = kill_combo.timer;
    player.hud_fix = csMgr.world_frozen();
    player.ail_mask = ail.mask;
};
auto damage_popup = [&](int value, std::uint32_t colour, const Vec3& origin) {
    static const bool log = std::getenv("CLERIA_DRIVE") != nullptr;
    const std::string t = std::to_string(std::max(0, value));   // _ltoa(max(v, 0)) 0x4fae54
    if (log) std::printf("  [popup] %s %06x\n", t.c_str(), unsigned(colour));
    if (popups.size() < 32) popups.push_back(make_popup(t, colour, 1.0f, {origin.x, origin.y + 0.5f, origin.z}));
};
auto hud_drive = [&](const std::string& cmd) -> bool {
    if (cmd == "hudstate") {
        hud_state_fill();
        std::printf("  [hud] ready %d charge %d alpha %d mp %.0f magic %d | str %.1f/%d def %.1f/%d mp %.1f/%d "
                    "full %.0f | combo %d/%d fix %d\n",
                    int(player.skill_ready), player.charge_level, int(hudstate::pulse_alpha(player.pulse)),
                    double(player.sp_cur), inv.magic, double(inv.atk_buff), inv.timer_atk, double(inv.def_buff),
                    inv.timer_def, double(inv.mp_buff), inv.timer_mp, double(player.buff_full), kill_combo.count,
                    kill_combo.timer, int(player.hud_fix));
        return true;
    }
    if (cmd.rfind("drop=", 0) == 0) {                // a world pickup 0.5 above the feet (an enemy drop's args)
        const int item = std::atoi(cmd.c_str() + 5);
        spawn_pickups(pick_rng, &item, 1, {feet.x, feet.y + 0.5f, feet.z}, SpawnArgs{}, water_mode == 2, pickups);
        return true;
    }
    if (cmd.rfind("mp=", 0) == 0) {                  // the MP DAT_0076a738 (and its lag)
        player.sp_cur = player.sp_lag = std::clamp(float(std::atof(cmd.c_str() + 3)), 0.0f, player.sp_max);
        return true;
    }
    return false;
};
