// The player's hit scan (included inside the game loop, twice): the enemy's reaction, one landed hit, the
// breakables pass, then the swing volume against the monsters, the breakables and the boss parts, and the
// GoLocusSword pool's step and scan. The live tick includes it after the actors' integrator pass; the frozen
// branch includes it too (FIX_SLOTS.md S4): her handler calls FUN_004fed50(self + 0x50) on every logic step
// under either fix slot [EXPORT 0046fba0.c:113-116], the hit path reads neither counter (§1.4), and the locus
// pool is gated on the menu byte only [EXPORT 005002e0.c:77, 86]. Defines react_enemy, player_hit_enemy,
// smash_breakables, mesh_struck, killed and broke in the including scope.
            #include "view_player_hit_defs.inl"  // react_enemy, player_hit_enemy, smash_breakables
            prof("tick:hits");
            int killed = 0, broke = 0;
            if (!g_lua.hits.empty()) {                   // cleria.combat.damage (API 7): the mods' hits, as hits of the player's
                const std::vector<LuaRt::Hit> hits = std::move(g_lua.hits);
                g_lua.hits.clear();
                for (const LuaRt::Hit& h : hits) {
                    Actor* en = world.find(h.id);
                    if (!en || en->is_player() || !en->alive()) continue;
                    g_lua.forced_dmg = h.dmg;
                    if (player_hit_enemy(*en, nullptr, 0.0f, h.knock, 0, h.cls, std::atan2(en->pos.z - feet.z, en->pos.x - feet.x), false)) ++killed;
                    g_lua.forced_dmg = -1;
                }
            }
            if (atk_window > 0) {
                --atk_window;
                // FUN_004fed50 mode 0 (build/ai_spec/HITVOLUME.md §2): the volume is placed
                // from the player's ORIGIN and current facing every tick, then tested against
                // each monster's hurt body; the victim's bank keys on this attack's serial.
                const float pface = std::atan2(face_dir.z, face_dir.x);
                const HitShape vol = volume_shape(atk_hb, {feet.x, feet.y + kOriginH, feet.z}, pface);
                for (Actor& en : world.actors()) {
                    if (en.is_player() || !en.alive() || en.destroy_requested()) continue;
                    const EnemyRender* vr = nullptr;
                    for (const EnemyRender& er : render) if (er.actor == &en) { vr = &er; break; }
                    if (vr && !vr->actor->ai.hittable) continue;            // +0x60 & 0x80 clear
                    const HitShape body = body_shape({en.pos.x, en.pos.y + en.height, en.pos.z},
                                                     en.hurt_radius, en.hurt_half_h, en.hurt_shape, en.hurt_off);
                    if (!overlap(vol, body)) continue;
                    if (en.bank.check_and_record(player_hit_serial, atk_hb.rehit)) continue;   // FUN_004faac0
                    if (atk_hb.one_shot) atk_window = 0;                     // 0x4fd251
                    if (player_hit_enemy(en, vr, atk_hb.power, atk_hb.knock, atk_hb.rtype,
                                         atk_cls, std::atan2(atk_dir.z, atk_dir.x)))
                        ++killed;
                }
                broke += smash_breakables(vol, player_hit_serial, atk_hb.rehit, atk_hb.one_shot, atk_hb.power, atk_cls);
                // FUN_004fed50's GoMesh pass (VELAGUNDER.md §3.3): each boss part's hurt sphere,
                // its own re-hit bank, the damage with the part's DEF / resist into part +0x134.
                // [not modelled] the armour's skipped elemental bonus, Toal's refused sections.
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
                        if (dmg > 0) boss->add_part_damage(pi, float(dmg));
                        boss_part_feedback(bp, dmg, atk_cls);
                        if (skillmp::boost_hit_counts(skillmp::attack_kind(avm.section()))) add_boost(300.0f);
                        if (std::getenv("CLERIA_DMG"))
                            std::printf("  hit boss part %d (DEF %.0f): %d dmg, HP %.0f stun %.0f\n",
                                        pi, bp.def, dmg, boss->hp(), boss->stun_gauge());
                    }
                // Nygtilger (NYGTILGER.md §3.1 / §3.2). The mesh path FUN_004fecb0: [approx] one
                // ray from the player's origin through the swing volume to its far side (the engine
                // casts one per attack sub-volume) against each struck-able piece's collision hull;
                // a struck piece asks its accept-hit callback, then its re-hit bank, then takes
                // FUN_004faf20(1, 0) with its own DEF / resist. A segment's one-way entries (+0x8c)
                // are skipped [approx] per object, while both are off (MeshObj::no_collide).
                if (boss2) {
                    const RoomMesh& b2m = phys_mesh(room);
                    const Vec3 O{feet.x, feet.y + kOriginH, feet.z};
                    Vec3 d{vol.centre.x - O.x, vol.centre.y - O.y, vol.centre.z - O.z};
                    const float dl = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                    if (dl > 1e-4f) { d.x /= dl; d.y /= dl; d.z /= dl; }
                    const Vec3 E{vol.centre.x + d.x * vol.radius, vol.centre.y + d.y * vol.radius,
                                 vol.centre.z + d.z * vol.radius};
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
                        if (!segment_hits_tris(b2m, O, E, rr->t0, rr->t1, &hit)) continue;
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
                    // The head / tail kind-9 parts: a sphere of r 0.85 (§3.1), their own banks.
                    for (int k = 0; k < 2 && atk_window > 0; ++k) {
                        const int o = k == 0 ? 0 : boss02::kPieces - 1;
                        if (!boss2->part_hittable(o)) continue;
                        const Boss02Part& pt = boss2->object(o).part;
                        if (!overlap(vol, body_shape(pt.pos, pt.radius, 0.0f, 0, 0.0f))) continue;
                        if (boss2_part_banks[std::size_t(k)].check_and_record(player_hit_serial, atk_hb.rehit)) continue;
                        if (atk_hb.one_shot) atk_window = 0;
                        const int dmg = b2_damage(pt.def, pt.resist);
                        if (dmg > 0) boss2->add_part_damage(o, float(dmg));
                        part_hit_feedback(pt.pos, true, dmg, atk_cls);
                        if (skillmp::boost_hit_counts(skillmp::attack_kind(avm.section()))) add_boost(300.0f);
                        if (std::getenv("CLERIA_DMG"))
                            std::printf("  hit %s part (DEF %.0f): %d dmg, part HP %.0f pool %.0f\n",
                                        boss02::object_name(o), pt.def, dmg, pt.hp, boss2->pool());
                    }
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
                        if (dmg > 0) boss3->add_part_damage(pi, float(dmg));   // [not modelled] the element bonus on HP
                        part_hit_feedback(bp.pos, true, dmg, atk_cls);
                        if (skillmp::boost_hit_counts(skillmp::attack_kind(avm.section()))) add_boost(300.0f);
                        if (std::getenv("CLERIA_DMG"))
                            std::printf("  hit gelaldy part %d (DEF %.0f): %d dmg, part HP %.0f body %.0f\n",
                                        pi, bp.def, dmg, bp.hp, boss3->hp());
                    }
                #include "view_boss04_hit.inl"     // Khonsclard's parts (KHONSCLARD.md §A.2.3)
                #include "view_boss05_hit.inl"     // Pictimos's parts (PICTIMOS.md §P1.4.1)
                #include "view_boss06_hit.inl"     // Zava's heads: the accept-hit callback (ZAVA_D.md §7.1)
                #include "view_boss07_hit.inl"     // Dalles's parts (DALLES_AB.md §B.2)
                #include "view_boss08_hit.inl"     // Darm's parts (DARM_D.md §4.3)
            }
            // GoLocusSword (src/cleria/locus.hpp, build/ability_re/LOCUS.md): the player's GenerateLocus
            // sweeps. FUN_004281c0 updates the locus pool after the player and the actors; a locus with
            // hit life left is scanned once a tick by the same mode-0 path as the swing (FUN_004fed50,
            // origin NULL: its three r 0.45 spheres as placed), keyed on the swing's token.
            // [not modelled] the boss parts, the wall clank (args [4],[5]: FUN_00500b30).
            if (!loci.empty()) {
                LocusSword::Owner lo;
                lo.origin = {feet.x, feet.y + kOriginH, feet.z};   // +0x50
                lo.floor_off = kOriginH;                             // +0x324
                lo.facing = std::atan2(face_dir.z, face_dir.x);      // +0x314
                lo.dead = player.hp < 1;                             // +0x98 < 1.0
                for (std::size_t li = 0; li < loci.size(); ++li) {
                    if (!loci[li].sword.tick(lo) || !loci[li].sword.armed()) continue;
                    const std::array<HitShape, 3> sp = loci[li].sword.spheres();
                    const std::uint32_t tok = loci[li].sword.token();
                    const int rehit = loci[li].sword.rehit(), cls = loci[li].cls;
                    const float bonus = loci[li].sword.bonus(), knock = loci[li].sword.knock();
                    for (Actor& en : world.actors()) {
                        if (en.is_player() || !en.alive() || en.destroy_requested()) continue;
                        const EnemyRender* vr = nullptr;
                        for (const EnemyRender& er : render) if (er.actor == &en) { vr = &er; break; }
                        if (vr && !vr->actor->ai.hittable) continue;
                        const HitShape body = body_shape({en.pos.x, en.pos.y + en.height, en.pos.z},
                                                         en.hurt_radius, en.hurt_half_h, en.hurt_shape, en.hurt_off);
                        if (!overlap(sp[0], body) && !overlap(sp[1], body) && !overlap(sp[2], body)) continue;
                        if (en.bank.check_and_record(tok, rehit)) continue;          // FUN_004faac0
                        if (std::getenv("CLERIA_DMG"))
                            std::printf("  [locus] frame %.2f tip (%.2f, %.2f) reach %.2f\n", loci[li].sword.frame(),
                                        loci[li].sword.tip().x, loci[li].sword.tip().z,
                                        std::sqrt((en.pos.x - feet.x) * (en.pos.x - feet.x) + (en.pos.z - feet.z) * (en.pos.z - feet.z)));
                        if (player_hit_enemy(en, vr, bonus, knock, /*rtype +0x108*/ 0, cls, lo.facing)) ++killed;
                    }
                    for (const HitShape& h : sp) broke += smash_breakables(h, tok, rehit, false, bonus, cls);
                }
                loci.erase(std::remove_if(loci.begin(), loci.end(),
                                          [](const PlayerLocus& l) { return !l.sword.alive(); }),
                           loci.end());
            }
            if (broke) reload_room(/*rerun_events*/false);   // drop the shattered mesh
            if (killed) {
                int left = 0;
                for (const Actor& en : world.actors())
                    if (!en.is_player() && en.alive()) ++left;
                std::printf("  defeated %d (%d left)\n", killed, left);
            }
