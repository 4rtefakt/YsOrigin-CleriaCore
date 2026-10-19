// FUN_004fed50's scene-mesh pass for one attacker of the player's (NYGTILGER.md §3.1 / §3.2, ZAVA_D.md §7.1): each ray
// of `mesh_segs` against Nygtilger's struck-able pieces and Zava's heads, through FUN_0041e420 (set 0) with the callback
// FUN_004fecb0; a struck object is taken once per scan (the callback's 32-entry list) at the first ray that meets it.
// Included by view_player_hit.inl at the swing and once per armed locus, after the part list (view_player_parts.inl).
// The including scope names the attacker as there, plus `mesh_segs` (its rays, start -> end).
// [approx] the first ray that meets the object gives the hit point (retail: the callback's nearest entry).
{
    auto mesh_seg_hit = [&](const RoomMesh& m, std::uint32_t t0, std::uint32_t t1, Vec3* at) {
        for (const auto& sg : mesh_segs)
            if (segment_hits_tris(m, sg.first, sg.second, t0, t1, at)) return true;
        return false;
    };
                // Nygtilger (NYGTILGER.md §3.1 / §3.2). The mesh path FUN_004fecb0 against each struck-able piece's
                // collision hull;
                // a struck piece asks its accept-hit callback, then its re-hit bank, then takes
                // FUN_004faf20(1, 0) with its own DEF / resist. A segment's one-way entries (+0x8c)
                // are skipped [approx] per object, while both are off (MeshObj::no_collide).
                if (boss2) {
                    const RoomMesh& b2m = phys_mesh(room);
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
                    for (int o = 0; o < boss02::kObjects && atk_window > 0; ++o) {
                        if (!boss2->mesh_hittable(o)) continue;
                        const char* nm = boss02::object_name(o);
                        const MeshObj* mo = meshes.find(nm);
                        const RoomMesh::ObjRange* rr = b2m.object_range(nm);
                        if (!mo || !rr || mo->no_collide) continue;
                        Vec3 hit{};
                        if (!mesh_seg_hit(b2m, rr->t0, rr->t1, &hit)) continue;
                        Boss02Output fb;
                        // The throttle's clock is real time; a (lockstep) drive run reads the tick clock so
                        // the run does not depend on the machine's speed.
                        const std::uint32_t now_ms = lockstep ? std::uint32_t(tick_count * 1000 / 60)
                                                              : std::uint32_t(SDL_GetTicks());
                        // ... and the accept-hit's own splat (0x513364: 1 of 0xffc000c0, size 1.95..2.7) on a cracked
                        // piece, with the option byte.
                        const bool ok = boss2->accept_hit(o, hit, now_ms, fb, [&](const Boss02Cosmetic& c) {
                            if (c.kind == Boss02Cosmetic::kScreenDebris)
                                blood_at("boss02 hit", 1, 0xffc000c0u, 2, 0x18, 1.95f, 2.7f, c.pos, 0.65f, false, false);
                        }, settings().s.game.bloody_effect_level > 0);
                        boss2_apply(fb);
                        if (!ok) continue;
                        if (boss2_banks[std::size_t(o)].check_and_record(player_hit_serial, atk_hb.rehit)) continue;
                        if (atk_hb.one_shot) atk_window = 0;
                        const Boss02Piece& bp = boss2->object(o);
                        const int dmg = b2_damage(bp.def, bp.resist);
                        // [H] FUN_004faf20 skips a victim already below 1 HP: the dead tail stays
                        // struck-able (its entries are re-armed every frame, §3.5), and without
                        // this gate each further hit would "kill" it again (pool -350 a time).
                        if (dmg > 0 && bp.hp >= 1.0f) boss2->add_damage(o, float(dmg));
                        if (skillmp::boost_hit_counts(skillmp::attack_kind(avm.section()))) add_boost(300.0f);
                        if (std::getenv("CLERIA_DMG"))
                            std::printf("  hit %s (DEF %.0f): %d dmg, HP %.0f pool %.0f\n", nm, bp.def, dmg,
                                        bp.hp, boss2->pool());
                    }
                }
    #include "view_boss06_hit.inl"     // Zava's heads: the accept-hit callback (ZAVA_D.md §7.1)
}
