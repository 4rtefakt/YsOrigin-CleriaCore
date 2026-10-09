// The HUD state of build/audit/PLAYTEST_1_HUD.md (src/cleria/hudstate.hpp), included in
// render_window next to view_abilities.inl:
//   hud_state_tick()   once a logic tick: the buff expiry (FUN_00462c40) and the pulse counters
//                      0x76c184 / 0x76c188 (FUN_0043a540: +3 / +6 for widget 0)
//   hud_state_fill()   before the HUD draws: builder B's ready test, the buffs, the combo meter
//   damage_popup()     FUN_004fae00: a damage number (GoPopup) at an origin + 0.5 up
//   hud_drive()        CLERIA_DRIVE: hudstate (print it), drop=<item id> (an item at the feet)
std::int32_t hud_pulse_ticks = 0;
auto hud_state_tick = [&]() {
    ++hud_pulse_ticks;                              // FUN_0043a540: not gated on a Fix
    if (csMgr.world_frozen()) return;               // 0x462c5b: no buff count-down while a Fix holds
    if (hudstate::tick_buffs(inv)) (void)player_stats();   // FUN_00420c40 | 0x75: the buffs left the stats
};
auto hud_state_fill = [&]() {
    skillmp::CostIn ci;
    ci.chr = character;
    ci.slot = skillmp::bracelet_slot(inv.magic);
    ci.difficulty = difficulty >= 2 ? difficulty - 2 : 0;   // DAT_0076bb5c
    ci.bc80 = std::uint32_t(game.g_flags[217]);
    ci.acc_mp = (inv.equip_flags & 0x200000u) != 0;
    ci.boosting = player.boosting;
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
