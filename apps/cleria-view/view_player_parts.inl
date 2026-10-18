// The bosses' GoXx parts against ONE attacker volume of the player's: FUN_004fed50's part-list pass (mode 0, after
// the actors: `for k < [0x767f38]: FUN_004faf20(attacker, *[0x767f34][k], 0, 0)` [BYTES 0x4feebd..0x4feefc]). Included
// by view_player_hit.inl twice: at the swing (the armed GenerateCollision volume) and, per sphere, for every armed
// GoLocusSword — FUN_005002e0 runs the same scan with the locus as the attacker, and FUN_004faf20 knows its type
// (0xb: the owner is +0x120 [BYTES 0x4faffd..0x4fb00d]; the ATK is the stat block the spawn copied from the owner
// [EXPORT 00464960.c:203-210], the power its +0xfc, the class its +0x109). The including scope names the attacker:
// `vol` (the volume), `player_hit_serial` (the token, +0xf0), `atk_hb` (.rehit, .power, .one_shot), `atk_cls`,
// `atk_window` (set to 0 by a one-shot volume's first hit), `part_atk` (the hit spark's attacker sphere; Darm's
// parts take none). One boss lives at a time: the order here is no rule.
// Not here: the scene-mesh ray pass (Nygtilger's pieces, Zava's heads) — the swing's only (view_player_hit.inl).
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
