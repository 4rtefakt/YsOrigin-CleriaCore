// CLERIA_DRIVE commands for the abilities / progression checks (tools/drive_regress.py: skill_gate,
// boost_iframes, exp_*), included in render_window just before run_drive, which calls it first.
//   abil         print the ability state: section, MP + its lag, the Boost gauge / mode, i-frames,
//                the equipped bracelet, the skill charge, the combo meter, EXP / level
//   boost=n      set the Boost gauge DAT_0076c1b4 (a direct write, like FUN_0058f210 case 1)
//   boostkey     one Boost press (+0x50c & 8): the mover 0x465b1d's entry / super
//   hurt=atk     an atk-power hit on the player through the Chr_Damage path (view_xso13_damage.inl):
//                refused while the i-frames +0x734 run, priced by FUN_004fa740, halved in Boost
//   kb=x,z       add (x, z) to the player's knock channel 1 +0x338/+0x340 (PLAYTEST_1.md item 4)
auto abil_drive = [&](const std::string& cmd) -> bool {
    if (cmd == "abil") {
        std::printf("  [abil] t%llu sec 0x%x mp %.0f lag %.0f boost %.0f boosting %d iframes %d magic %d "
                    "charge %d combo %d/%d exp %.2f lv %d hp %d/%d\n",
                    (unsigned long long)tick_count, unsigned(avm.section()), player.sp_cur, player.sp_lag,
                    player.boost, int(player.boosting), player_iframes, inv.magic, skill_charge.level,
                    kill_combo.count, kill_combo.timer, double(player.exp), player.level, player.hp, player.max_hp);
        return true;
    }
    if (cmd.rfind("boost=", 0) == 0) {
        player.boost = std::clamp(float(std::atof(cmd.c_str() + 6)), 0.0f, skillmp::kBoostFull);
        return true;
    }
    if (cmd == "boostkey") { try_boost(); return true; }
    if (cmd.rfind("kb=", 0) == 0) {          // kb=x,z: add to the player's knock channel 1 (+0x338/+0x340)
        float x = 0, z = 0;
        if (std::sscanf(cmd.c_str() + 3, "%f,%f", &x, &z) == 2) { player_kb.x += x; player_kb.z += z; }
        return true;
    }
    if (cmd.rfind("hurt=", 0) == 0) {
        csHost.player_damage.push_back({float(std::atof(cmd.c_str() + 5)), false, 0});
        return true;
    }
    return false;
};
