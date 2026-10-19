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
                part_atk = &vol;                                   // the boss parts' hit spark (part_hit_feedback)
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
                #include "view_player_parts.inl"   // the bosses' GoXx parts (Velagunder, Gelaldy, Khonsclard, Pictimos, Dalles, Darm)
                part_atk = &vol;                   // (Darm's block clears it) Nygtilger's pieces take the spark
                {
                    // [approx] one ray from the player's origin through the swing volume to its far side (the engine
                    // casts four or five around each sub-volume, as the locus's below).
                    const Vec3 O{feet.x, feet.y + kOriginH, feet.z};
                    Vec3 d{vol.centre.x - O.x, vol.centre.y - O.y, vol.centre.z - O.z};
                    const float dl = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                    if (dl > 1e-4f) { d.x /= dl; d.y /= dl; d.z /= dl; }
                    const std::vector<std::pair<Vec3, Vec3>> mesh_segs{
                        {O, {vol.centre.x + d.x * vol.radius, vol.centre.y + d.y * vol.radius, vol.centre.z + d.z * vol.radius}}};
                #include "view_player_meshes.inl"  // Nygtilger's pieces, Zava's heads
                }
            }
            // GoLocusSword (src/cleria/locus.hpp, build/ability_re/LOCUS.md): the player's GenerateLocus
            // sweeps. FUN_004281c0 updates the locus pool after the player and the actors; a locus with
            // hit life left is scanned once a tick by the same mode-0 path as the swing (FUN_004fed50,
            // origin NULL: its three r 0.45 spheres as placed), keyed on the swing's token. Then, while the frame at
            // the tick's start lies in its [4], [5] window, the wall clank FUN_00500b30 [BYTES 0x500b30..0x500dc4]: a
            // terrain ray from the owner's origin to sphere 0; on a hit with D.n < 0, two sparks along D reflected
            // (FUN_00589390: spread 15 deg, speed 0.06..0.205, width 0.11) and, once per locus (+0x16c), when the owner
            // faces the wall (|facing - atan2(n.z, n.x)| > 2.2619 rad, 0x6bd5e0) a bounce n x 0.09 on knock channel 1.
            // The ray (FUN_0041e420, set 0) calls back FUN_00500ac0 [BYTES 0x500ac0..0x500b2f] for each entry hit nearer than
            // the nearest so far: on a boss mesh (type byte +0x28d >= 5: the names of FUN_00501a30, as boss7_seg_nb) at
            // alpha >= 0.75 (0x6bd390), SE 0x5d at the hit point, unless a boss part was hit within 300 ms (0x74e7e4,
            // boss_part_se_tick) or the last such SE is within 72 ms (0x7ddb8c); the ms are the drive's 50 / 3 a tick.
            // [approx] "nearer than the nearest so far" is taken over the collision mesh's triangles before the object
            // (the pool order), and the object flags +0x298 & 0x4060 are not kept. Then the part-list pass with the locus as the attacker (view_player_parts.inl).
            if (!loci.empty()) {
                LocusSword::Owner lo;
                lo.origin = {feet.x, feet.y + kOriginH, feet.z};   // +0x50
                lo.floor_off = kOriginH;                             // +0x324
                lo.facing = std::atan2(face_dir.z, face_dir.x);      // +0x314
                lo.dead = player.hp < 1;                             // +0x98 < 1.0
                lo.at = feet;                                        // +0x254, the trail's translation
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
                    // ... and the part-list pass with the locus as the attacker (view_player_parts.inl): its three spheres, the
                    // swing's token on each part's own bank, its own power and class; then the scene-mesh pass (view_player_meshes.inl).
                    {
                        AsoVm::Hitbox atk_hb;                  // the locus's record, under the swing's names
                        atk_hb.rehit = rehit; atk_hb.power = bonus;
                        const int atk_cls = cls;
                        const std::uint32_t player_hit_serial = tok;
                        int atk_window = 1;
                        for (const HitShape& vol : sp) {
                            part_atk = &vol;
                #include "view_player_parts.inl"
                        }
                        // The mesh pass: for each sphere, d = normalize(centre - the locus's +0x50), which FUN_005002e0
                        // copies from the owner's +0x254 (its feet as last drawn) [EXPORT 005002e0.c:163-164]; four rays of
                        // 2r along d starting at centre - d r, offset by +-a f and +-b f with a = d x (0, 1, 0), b = d x a,
                        // f = r x 0.71 (0x6bd384), and a fifth through the centre when f > 0.5 [EXPORT 004fed50.c:198-260].
                        // [approx] +0x254 is this tick's feet.
                        std::vector<std::pair<Vec3, Vec3>> mesh_segs;
                        for (const HitShape& vol : sp) {
                            const Vec3 c = vol.centre;
                            const float r = vol.radius, f = r * 0.71f;
                            Vec3 d{c.x - feet.x, c.y - feet.y, c.z - feet.z};
                            const float dl = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                            if (dl > 0.0f) { d.x /= dl; d.y /= dl; d.z /= dl; } else d = {};
                            const Vec3 a{-d.z, 0.0f, d.x};
                            const Vec3 bb{(d.y * a.z) * f, (d.z * a.x - d.x * a.z) * f, (-d.y * a.x) * f};
                            const Vec3 S{c.x - d.x * r, c.y - d.y * r, c.z - d.z * r};
                            const Vec3 V{d.x * 2 * r, d.y * 2 * r, d.z * 2 * r};
                            auto seg = [&](Vec3 s) { mesh_segs.push_back({s, {s.x + V.x, s.y + V.y, s.z + V.z}}); };
                            seg({S.x + a.x * f, S.y, S.z + a.z * f});
                            seg({S.x - a.x * f, S.y, S.z - a.z * f});
                            seg({S.x + bb.x, S.y + bb.y, S.z + bb.z});
                            seg({S.x - bb.x, S.y - bb.y, S.z - bb.z});
                            if (f > 0.5f) seg(S);
                        }
                        const HitShape& vol = sp[0];   // the hit spark's sphere
                        part_atk = &vol;
                #include "view_player_meshes.inl"
                        part_atk = nullptr;
                    }
                    const std::uint32_t fi0 = std::uint32_t(loci[li].sword.frame_at_tick_start());
                    if (std::uint32_t(loci[li].sword.clank_lo()) <= fi0 && fi0 < std::uint32_t(loci[li].sword.clank_hi()) && wx.hit) {
                        const Vec3 O = lo.origin, S = sp[0].centre, D{S.x - O.x, S.y - O.y, S.z - O.z};
                        {
                            const RoomMesh& cm = phys_mesh(room);
                            for (const RoomMesh::ObjRange& r : cm.object_ranges) {
                                const std::string six = r.name.substr(0, 6);
                                if (!(six == "boss02" || six == "boss06" || six == "boss08" || r.name == "boss01" ||
                                      r.name == "boss03" || r.name == "boss04" || r.name == "boss05" || r.name == "boss07")) continue;
                                const MeshObj* mo = meshes.find(r.name);
                                if (mo && mo->alpha < 0.75f) continue;
                                Vec3 bp{};
                                if (!segment_hits_tris(cm, O, S, r.t0, r.t1, &bp)) continue;
                                Vec3 ep{};
                                if (r.t0 > 0 && segment_hits_tris(cm, O, bp, 0, r.t0, &ep)) continue;   // nearer before it
                                if ((tick_count - boss_part_se_tick) * 50 / 3 > 300 && (tick_count - clank_se_tick) * 50 / 3 > 72) {
                                    clank_se_tick = tick_count;
                                    sfx_at(0x5d, bp, kSfxAnimVol);
                                    if (dust_log)
                                        std::printf("  [spark] t%llu clank SE 0x5d on %s at (%.2f, %.2f, %.2f)\n",
                                                    (unsigned long long)tick_count, r.name.c_str(), bp.x, bp.y, bp.z);
                                }
                            }
                        }
                        Vec3 hp{}, n{};
                        if (wx.hit(O, S, &hp, &n)) {
                            const float dn = D.x * n.x + D.y * n.y + D.z * n.z;
                            if (dn < 0.0f) {
                                const float k = dn * -2.0f;               // 0x6be098
                                const Vec3 R{n.x * k + D.x, n.y * k + D.y, n.z * k + D.z};
                                const std::uint64_t c0 = pick_rng.calls;
                                wx.clank_sparks(hp, R, 0.2617994f, 0.06f, 0.205f, 0.11f);   // 0x3e860a92, 0x3d75c28f, 0x3e51eb85, 0x3de147ae
                                if (dust_log)
                                    std::printf("  [spark] t%llu clank: 2 sparks at (%.2f, %.2f, %.2f), %llu draws\n",
                                                (unsigned long long)tick_count, hp.x, hp.y, hp.z,
                                                (unsigned long long)(pick_rng.calls - c0));
                                if (!loci[li].clanked) {
                                    loci[li].clanked = true;
                                    float d = lo.facing - float(std::atan2(double(n.z), double(n.x)));
                                    if (d >= 0.0f) { if (d > 3.1415927f) d -= 6.2831855f; }
                                    else if (d < -3.1415927f) d += 6.2831855f;
                                    if (std::fabs(d) > 2.2619467f) { player_kb.x += n.x * 0.09f; player_kb.z += n.z * 0.09f; }
                                }
                            }
                        }
                    }
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
