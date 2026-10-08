// Pictimos's 13 GoXx parts (PICTIMOS.md §P1.4.1): spheres on their node slots, each with its own re-hit bank;
// FUN_004faf20 with the part's DEF (999 for parts 2..12) and its character resists. Included at the swing.
if (boss5 && !boss5->destroyed())
    for (int pi = 0; pi < boss05::kParts && atk_window > 0; ++pi) {
        if (!boss5->part_hittable(pi)) continue;
        const Boss05Part& bp = boss5->part(pi);
        if (!overlap(vol, body_shape(bp.centre, bp.radius, 0.0f, 0, 0.0f))) continue;
        if (boss5_banks[std::size_t(pi)].check_and_record(player_hit_serial, atk_hb.rehit)) continue;
        if (atk_hb.one_shot) atk_window = 0;
        AttackStats a;
        a.atk = player_stats().atk;
        a.bonus = atk_hb.power;
        EnemyDefense vd; vd.def = bp.def;
        for (int k = 0; k < 4; ++k) vd.resist[k] = bp.resist[k];
        PlayerDamageMods mods;
        mods.yunica_bonus = character == 1 && game.g_flags[149] == 0;
        const int dmg = access::damage_dealt(enemy_incoming_damage(a, atk_cls, vd, mods,
            [&](float pct) { return float(rng_pct(rng)) < pct; }), settings().s.access.damage_dealt);
        if (dmg > 0) boss5->add_part_damage(pi, float(dmg));
        if (skillmp::boost_hit_counts(skillmp::attack_kind(avm.section()))) add_boost(300.0f);
        if (std::getenv("CLERIA_DMG"))
            std::printf("  hit pictimos part %d (DEF %.0f): %d dmg, body %.0f\n", pi, bp.def, dmg, boss5->hp());
    }
