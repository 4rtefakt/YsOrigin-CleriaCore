// The bosses' GoXx parts against ONE attacker volume of the player's: FUN_004fed50's part-list pass (mode 0, after
// the actors: `for k < [0x767f38]: FUN_004faf20(attacker, *[0x767f34][k], 0, 0)` [BYTES 0x4feebd..0x4feefc]). Included
// by view_player_hit.inl twice: at the swing (the armed GenerateCollision volume) and, per sphere, for every armed
// GoLocusSword — FUN_005002e0 runs the same scan with the locus as the attacker, and FUN_004faf20 knows its type
// (0xb: the owner is +0x120 [BYTES 0x4faffd..0x4fb00d]; the ATK is the stat block the spawn copied from the owner
// [EXPORT 00464960.c:203-210], the power its +0xfc, the class its +0x109). The including scope names the attacker:
// `vol` (the volume), `player_hit_serial` (the token, +0xf0), `atk_hb` (.rehit, .power, .one_shot), `atk_cls`,
// `atk_window` (set to 0 by a one-shot volume's first hit), `part_atk` (the hit spark's attacker sphere; Darm's
// parts take none). One boss lives at a time: the order here is no rule.
// Not here: the scene-mesh ray pass (Nygtilger's pieces, Zava's heads): view_player_meshes.inl.
                // FUN_004fed50's GoMesh pass (VELAGUNDER.md §3.3): each boss part's hurt sphere,
                // its own re-hit bank, the damage with the part's DEF / resist into part +0x134.
                // The element bonus (skipped on armour) is part_element's. [not modelled] Toal's refused sections.
                if (boss && !boss->destroyed())
                    for (int pi = 0; pi < boss01::kParts; ++pi) {
                        if (!boss->part_hittable(pi)) continue;
                        const Boss01Part& bp = boss->part(pi);
                        const HitShape body = body_shape(bp.hurt_centre, bp.hurt_radius, 0.0f, 0, 0.0f);
                        if (!overlap(vol, body)) continue;
                        if (boss_banks[std::size_t(pi)].check_and_record(player_hit_serial, atk_hb.rehit)) continue;
                        if (atk_hb.one_shot) atk_window = 0;
                        AttackStats a;
                        a.atk = player_stats().atk;
                        a.bonus = atk_hb.power;
                        EnemyDefense vd; vd.def = bp.def;
                        for (int k = 0; k < 4; ++k) vd.resist[k] = bp.resist[k];
                        PlayerDamageMods mods;
                        mods.atk_debuff = ail.has(ailments::kCurse);   // FUN_004fa550: ATK - 5, floor 0
                        mods.yunica_bonus = character == 1 && game.g_flags[149] == 0;
                        const int dmg = lua_part_damage(enemy_incoming_damage(a, atk_cls, vd, mods, [&](float pct) { return float(rng_pct(rng)) < pct; }), a, vd.def, atk_cls);   // the shell's Cheats, the Lua mods' filter
                        const int dmg_hp = part_element(bp.pos, bp.armor, dmg, atk_cls);
                        if (dmg > 0) boss->add_part_damage(pi, float(dmg), float(dmg_hp));
                        boss_part_feedback(bp, dmg, atk_cls);
                        if (skillmp::boost_hit_counts(skillmp::attack_kind(avm.section()))) add_boost(300.0f);
                        if (std::getenv("CLERIA_DMG"))
                            std::printf("  hit boss part %d (DEF %.0f): %d dmg, HP %.0f stun %.0f\n",
                                        pi, bp.def, dmg, boss->hp(), boss->stun_gauge());
                    }
                // Gelaldy's five GoXx parts (GELALDY.md §2.3 / §3): the hurt sphere (a guarded part's
                // centre sits at y -1000), the part's own re-hit bank, FUN_004faf20 with the part's
                // DEF / resist into +0x134 (and the part HP: the arms are counted twice, G9).
                if (boss3 && !boss3->destroyed())
                    for (int pi = 0; pi < boss03::kParts && atk_window > 0; ++pi) {
                        if (!boss3->part_hittable(pi)) continue;
                        const Boss03Part& bp = boss3->part(pi);
                        if (!overlap(vol, body_shape(bp.hurt_centre, bp.hurt_radius, 0.0f, 0, 0.0f))) continue;
                        if (boss3_banks[std::size_t(pi)].check_and_record(player_hit_serial, atk_hb.rehit)) continue;
                        if (atk_hb.one_shot) atk_window = 0;
                        AttackStats a;
                        a.atk = player_stats().atk;
                        a.bonus = atk_hb.power;
                        EnemyDefense vd; vd.def = bp.def;
                        for (int k = 0; k < 4; ++k) vd.resist[k] = bp.resist[k];
                        PlayerDamageMods mods;
                        mods.atk_debuff = ail.has(ailments::kCurse);   // FUN_004fa550: ATK - 5, floor 0
                        mods.yunica_bonus = character == 1 && game.g_flags[149] == 0;
                        const int dmg = lua_part_damage(enemy_incoming_damage(a, atk_cls, vd, mods, [&](float pct) { return float(rng_pct(rng)) < pct; }), a, vd.def, atk_cls);   // the shell's Cheats, the Lua mods' filter
                        // +0x138 = 0 on every part (FUN_005a1a10 takes it from the ctor's {2, i, 0.75, 0, ..}, 0x5201f7):
                        // the element bonus on the part's HP, the elemental effect and the white number on each hit.
                        const int dmg_hp = part_element(bp.pos, false, dmg, atk_cls);
                        if (dmg > 0) boss3->add_part_damage(pi, float(dmg), float(dmg_hp));
                        part_hit_feedback(bp.pos, false, dmg, atk_cls);
                        if (skillmp::boost_hit_counts(skillmp::attack_kind(avm.section()))) add_boost(300.0f);
                        if (std::getenv("CLERIA_DMG"))
                            std::printf("  hit gelaldy part %d (DEF %.0f): %d dmg, part HP %.0f body %.0f\n",
                                        pi, bp.def, dmg, bp.hp, boss3->hp());
                    }
                #include "view_boss04_hit.inl"     // Khonsclard's parts (KHONSCLARD.md §A.2.3)
                #include "view_boss05_hit.inl"     // Pictimos's parts (PICTIMOS.md §P1.4.1)
                #include "view_boss07_hit.inl"     // Dalles's parts (DALLES_AB.md §B.2)
                part_atk = nullptr;                // [not modelled] Darm's parts' hit spark (that fight is ported apart)
                #include "view_boss08_hit.inl"     // Darm's parts (DARM_D.md §4.3)
                part_atk = &vol;                   // (Darm's block clears it)
                // Nygtilger's head and tail kind-9 parts (NYGTILGER.md §3.1): GoXx parts of the same list.
                if (boss2) {
                    auto b2_damage = [&](float def, const float* resist) {
                        AttackStats a;
                        a.atk = player_stats().atk;
                        a.bonus = atk_hb.power;
                        EnemyDefense vd; vd.def = def;
                        for (int k = 0; k < 4; ++k) vd.resist[k] = resist[k];
                        PlayerDamageMods mods;
                        mods.atk_debuff = ail.has(ailments::kCurse);   // FUN_004fa550: ATK - 5, floor 0
                        mods.yunica_bonus = character == 1 && game.g_flags[149] == 0;
                        return lua_part_damage(enemy_incoming_damage(a, atk_cls, vd, mods, [&](float pct) { return float(rng_pct(rng)) < pct; }), a, vd.def, atk_cls);   // the shell's Cheats, the Lua mods' filter
                    };
                    // The head / tail kind-9 parts: a sphere of r 0.85 (§3.1), their own banks.
                    for (int k = 0; k < 2 && atk_window > 0; ++k) {
                        const int o = k == 0 ? 0 : boss02::kPieces - 1;
                        if (!boss2->part_hittable(o)) continue;
                        const Boss02Part& pt = boss2->object(o).part;
                        if (!overlap(vol, body_shape(pt.pos, pt.radius, 0.0f, 0, 0.0f))) continue;
                        if (boss2_part_banks[std::size_t(k)].check_and_record(player_hit_serial, atk_hb.rehit)) continue;
                        if (atk_hb.one_shot) atk_window = 0;
                        const int dmg = b2_damage(pt.def, pt.resist);
                        // +0x138 = 0: the head's and the tail's hurt part come from FUN_005a1a10({1, idx, 0.85, 0, 0, obj}).
                        const int dmg_hp = part_element(pt.pos, false, dmg, atk_cls);
                        if (dmg > 0) boss2->add_part_damage(o, float(dmg), float(dmg_hp));
                        part_hit_feedback(pt.pos, false, dmg, atk_cls);
                        if (skillmp::boost_hit_counts(skillmp::attack_kind(avm.section()))) add_boost(300.0f);
                        if (std::getenv("CLERIA_DMG"))
                            std::printf("  hit %s part (DEF %.0f): %d dmg, part HP %.0f pool %.0f\n",
                                        boss02::object_name(o), pt.def, dmg, pt.hp, boss2->pool());
                    }
                }
