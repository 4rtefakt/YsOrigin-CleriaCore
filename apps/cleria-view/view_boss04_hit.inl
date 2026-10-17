// Khonsclard's eight GoXx parts (KHONSCLARD.md §A.2.3 / §A.3): the core sphere, the six pod spheres, the sunk
// body's cylinder; each with its own re-hit bank; FUN_004faf20 with the part's DEF (999 = no damage) / resist.
// Included at the player's swing next to Gelaldy's block.
if (boss4 && !boss4->destroyed())
    for (int pi = 0; pi < boss04::kParts && atk_window > 0; ++pi) {
        if (!boss4->part_hittable(pi)) continue;
        const Boss04Part& bp = boss4->part(pi);
        if (!overlap(vol, body_shape(bp.centre, bp.hurt_radius, bp.half_h, bp.shape, 0.0f))) continue;
        if (boss4_banks[std::size_t(pi)].check_and_record(player_hit_serial, atk_hb.rehit)) continue;
        if (atk_hb.one_shot) atk_window = 0;
        AttackStats a;
        a.atk = player_stats().atk;
        a.bonus = atk_hb.power;
        EnemyDefense vd; vd.def = bp.def;
        for (int k = 0; k < 4; ++k) vd.resist[k] = bp.resist[k];
        PlayerDamageMods mods;
        mods.atk_debuff = ail.has(ailments::kCurse);   // FUN_004fa550: ATK - 5, floor 0
        mods.yunica_bonus = character == 1 && game.g_flags[149] == 0;
        const int dmg = lua_part_damage(enemy_incoming_damage(a, atk_cls, vd, mods, [&](float pct) { return float(rng_pct(rng)) < pct; }), a, vd.def, atk_cls);
        const int dmg_hp = part_element(bp.centre, bp.broken, dmg, atk_cls);
        if (dmg > 0) boss4->add_part_damage(pi, float(dmg), float(dmg_hp));
        part_hit_feedback(bp.centre, bp.broken, dmg, atk_cls);   // FUN_004faf20's tail (view_game.inl)
        if (skillmp::boost_hit_counts(skillmp::attack_kind(avm.section()))) add_boost(300.0f);
        if (std::getenv("CLERIA_DMG"))
            std::printf("  hit khonsclard part %d (DEF %.0f): %d dmg, part HP %.0f body %.0f\n", pi, bp.def, dmg, bp.hp,
                        boss4->hp());
    }
