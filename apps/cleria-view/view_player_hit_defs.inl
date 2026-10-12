// What a hit of the player's lands with (included inside the game loop): the monster's reaction, one landed hit on a
// monster (FUN_004fbb40), the mode-0 scan's breakables pass. Definitions only — view_player_hit.inl (the swing and
// the loci) and view_fx_hits.inl (her effects' particles) use them; the frozen branch's slot-0 block includes this
// file by itself for the latter (FIX_SLOTS.md S7).
            // The enemy's reaction to the player's hit (FUN_004fbb40, src/cleria/hit.hpp).
            // No i-frames and no hit-stop exist for enemies. The knockback impulse `k` is
            // ADDED to +0x338/+0x340 whatever the damage; the flinch (section 4; a re-hit
            // while flinching restarts it) needs dmg >= 1 and is skipped for rtype 3 or a
            // durable victim (anime_Durable ~ +0x9d8).
            auto react_enemy = [&](Actor& en, int dmg, bool died, int rtype, const Impulse& k) {
                en.mover.k1.x += k.x; en.mover.k1.z += k.z;   // knock channel 1 (+0x338/+0x340)
                if (died) return;                       // death is handled by the caller
                for (EnemyRender& er : render) {
                    if (er.actor != &en || !er.avm.ok()) continue;
                    er.hit = true;                      // +0x3bc & 4: wakes an unaware enemy
                    if (dmg >= 1) er.dmg_hit = true;    // +0x3bc & 2: a damaging hit
                    // 0x4fd1d7.. [EXPORT 004fbb40.c:675-683]: a damaging hit while the attacker's kind +0x404 is 2
                    // (the thrust 0x10b) sets 0x2000, and 0x4000 too when the attacker's +0x77c != 0; the action
                    // player rolls the stun on them next tick (30 % / 60 %; enemy_ai.cpp). The attacker is the
                    // player for a bullet or an effect too (LAUNCH_STATE.md §1).
                    // [H] +0x77c is the double-jump count (player_actions dj_count): its meaning here is the spec's
                    // unknown U1.
                    if (dmg >= 1 && skillmp::attack_kind(avm.section()) == 2)
                        er.events |= 0x2000u | (pact.dj_count != 0 ? 0x4000u : 0u);
                    if (enemy_flinch_state(dmg, false, rtype, er.avm.durable(), er.avm.section()) == 4) {
                        er.avm.set_section(4, /*force*/ er.avm.section() == 4);
                        er.atk_win = 0; er.atk_armed = false; er.atk_age = 0;
                    }
                    break;
                }
            };

            // The player's melee swing (build/attack_re). While the hit-volume is live, an
            // enemy inside the sphere is hit, then shielded for the swing's re-hit timer
            // (arg[7]; the engine's 10-slot victim bank) — so a box whose timer is shorter
            // than its lifetime multi-hits. Damage is FUN_004fa550 (hit.hpp), NOT
            // damage_real: elemental resist multiplier, a min-1 chip, Yunica's x1.1.
            // One landed hit by an attacker the player owns — the swing's box or a GoLocusSword —
            // on a monster (FUN_004fbb40). `power` / `knock` / `rtype` / `cls` are the attacker
            // record's +0xfc / +0x104 / +0x108 / +0x109; `heading` is the attack direction the guard
            // and the knockback read. Returns true on a kill.
            // `actor_owned`: the hitter is the player or his locus (cl = 1), not a bullet or an effect (the launch rule).
            auto player_hit_enemy = [&](Actor& en, const EnemyRender* vr, float power, float knock, int rtype,
                                        int cls, float heading, bool actor_owned = true) {
                    // 0x4fbcb2..0x4fbcd1: the first player-owned locus in the pool holds its frame one
                    // tick (+0x180 += 1). [H] the victim's +0x12c bit 28 is taken as clear; the owner's
                    // own +0x118 += 1 at the same site is [not modelled].
                    if (!loci.empty()) loci.front().sword.freeze();
                    AttackStats a;
                    a.atk = player_stats().atk;
                    a.bonus = power;                 // per-attack power, from this section's script
                    EnemyDefense vd; vd.def = en.def;
                    for (int k = 0; k < 4; ++k) vd.resist[k] = en.resist[k];
                    PlayerDamageMods mods;
                    mods.atk_debuff = ail.has(ailments::kCurse);   // FUN_004fa550: ATK - 5, floor 0
                    mods.yunica_bonus = character == 1 && game.g_flags[149] == 0;
                    int dmg = enemy_incoming_damage(a, cls, vd, mods,
                        [&](float pct) { return float(rng_pct(rng)) < pct; });
                    const int formula = dmg;
                    // The guard (FUN_004fbb40): a victim in section 6 facing the swing blocks it.
                    if (vr && vr->avm.ok() &&
                        guard_blocks(en.cast_id, vr->avm.section() == 6 || en.guard399, vr->avm.durable(), rtype,
                                     vr->facing, heading)) {
                        dmg = 0;
                        // 0x4fbfaa..0x4fc0da: the guard's three sparks at the victim's origin (FUN_00589130: delay
                        // step 1, life 35 + 0..25, width 0.11, speed 0.05..0.125, vy 0.05..0.075, any heading).
                        // [not modelled] its yellow tint (+0x268 = 0xffff00, +0x270 = 0.05, +0x274 = 1).
                        sparks_at("guard", 3, 1, 0x19, 0.11f, {en.pos.x, en.pos.y + en.height, en.pos.z}, 0.05f, 0.125f,
                                  0.05f, 0.075f, 0.0f, 6.2831855f);
                    }
                    dmg = access::damage_dealt(dmg, settings().s.access.damage_dealt);       // the shell's Cheats
                    // The knock-up / juggle (0x4fc137..0x4fc7d4, after the guard and before the HP subtraction; hit.hpp
                    // launch_on_hit, LAUNCH_STATE.md §1): a stunned, grounded, launchable victim hit by anything of
                    // the player's but the thrust is popped up (+0x4d0 = 1); every hit while +0x4d0 != 0 juggles it;
                    // a stunned flyer's stun grows instead. Each multiplies the integer damage by 1.2 / 1.15 / 1.1 and
                    // spawns effect 1 at the origin (taken before the lift). [approx] the shell's Damage-dealt cheat
                    // is applied before the factor.
                    LaunchKind lk = LaunchKind::None;
                    {
                        LaunchVictim lv;
                        lv.launch = en.launch; lv.t = en.vert.t; lv.y = en.pos.y; lv.grounded = en.vert.grounded;
                        lv.stun_dur = en.ai.dur(); lv.flyer = en.flyer; lv.guard399 = en.guard399;
                        lv.stunned = en.ai.i(0x724) & 0xff; lv.flags12c = en.flags12c;
                        lk = launch_on_hit(lv, actor_owned, skillmp::attack_kind(avm.section()));
                        if (lk != LaunchKind::None) {
                            const int d0 = dmg;
                            dmg = launch_damage(dmg, difficulty >= 2 ? difficulty - 2 : 0);
                            ef2sim::SpawnParams lsp; lsp.side = 0;
                            spawn_sim_fx(1, {en.pos.x, en.pos.y + en.height, en.pos.z}, {}, 1.0f, lsp, 0, {});
                            en.launch = lv.launch; en.vert.t = lv.t; en.pos.y = lv.y; en.vert.grounded = lv.grounded;
                            en.ai.dur() = lv.stun_dur;
                            if (std::getenv("CLERIA_DMG"))
                                std::printf("  [launch] t%llu #%d %s: count %d, t %.4f, stun %d, dmg %d -> %d\n",
                                            (unsigned long long)tick_count, en.instance_id,
                                            lk == LaunchKind::Launch ? "launched" : lk == LaunchKind::Juggle ? "juggled" : "flyer",
                                            en.launch, en.vert.t, en.ai.dur(), d0, dmg);
                        }
                    }
                    if (cheat_ohko && dmg > 0) dmg = std::max(dmg, int(std::ceil(en.hp)));   // the shell's Cheats
                    dmg = lua_damage(true, dmg, formula, a.atk, power, en.def, cls, en.cast_id, en.level);   // Lua mods
                    HitResult r = apply_damage(en.hp, en.flags, dmg);
                    if (r.died && boss6_zava_undying(en)) { en.flags &= ~kActorDead; r.died = false; }   // her handler floors it (view_boss06.inl)
                    if (dmg > 0) kill_combo.hit();   // 0x4fc835: the combo meter, before the kill's EXP
                    // ... and in the same block, Toal in 0x10c / 0x10f: FUN_0046cc10(500.0) (refused while
                    // boosting, like every add) and the effect 0x9d at the victim, side 0, power 0, knock 1
                    // [BYTES 0x4fc873..0x4fc8a0; EXPORT 004fbb40.c:508-552].
                    if (dmg > 0 && character == 3 && (avm.section() == 0x10c || avm.section() == 0x10f)) {
                        add_boost(500.0f);
                        ef2sim::SpawnParams sp;
                        sp.side = 0; sp.power = 0.0f; sp.knock = 1.0f; sp.cls = -1; sp.owner = 13;
                        spawn_sim_fx(0x9d, {en.pos.x, en.pos.y + en.height, en.pos.z}, {}, 1.0f, sp, 0, {});
                    }
                    {   // FUN_004fbb40's own feedback (hit_feedback.hpp), at the victim's origin.
                        const Vec3 org{en.pos.x, en.pos.y + en.height, en.pos.z};
                        // The tail 0x4fd241: FUN_004fae00(0xffffffff), the white damage number (a GoPopup,
                        // scale 1.0, origin + 0.5 up; "0" for a guarded hit), every path included.
                        // PLAYTEST_1_HUD.md item 3.
                        damage_popup(hudstate::dealt_popup_value(dmg, cls, game.g_flags), 0xffffff, org);
                        if (dmg < 1) {
                            // 0x4fd1b5: a guarded / zero hit: the SE, then four sparks thrown back within a quarter
                            // turn of the hit angle (FUN_00589130: delay step 2, life 35 + 0..10, width 0.15, speed
                            // 0.15..0.2, vy -0.05..0.1) [BYTES 0x4fd1cf..0x4fd239].
                            sfx_at(hitfx::no_damage_se(en.cast_id), org, kSfxAnimVol);
                            sparks_at("zero hit", 4, 2, 0xa, 0.15f, org, 0.15f, 0.2f, -0.05f, 0.1f,
                                      heading - 1.5707964f, heading + 1.5707964f);
                        } else if (const int fx = hitfx::skill_hit_effect(cls, character); fx >= 0) {
                            // 0x4fd020: the damage class's elemental hit effect (FUN_00450410).
                            ef2sim::SpawnParams sp;
                            sp.side = 0; sp.power = 0.0f; sp.knock = 1.0f; sp.cls = cls; sp.owner = 13;
                            const float vf = vr ? vr->facing : heading_of_yaw(en.yaw);
                            spawn_sim_fx(fx, org, {0.0f, hitfx::skill_hit_yaw(vf), 0.0f}, 1.0f, sp, 0, {});
                            if (sfx_log) std::printf("  [hitfx] t%llu class %d hit effect 0x%x\n", (unsigned long long)tick_count, cls, fx);
                        }
                        // 0x4fd10e..0x4fd167 [BYTES], every hit for 1 or more: FUN_004fab40(victim, colour) = five dust
                        // puffs (20 draws) at the origin - (cos, sin)(the hit angle) x the hurt radius +0x68, in the
                        // cast's ENEMYINFO +0xe0 colour (0 = invisible, the draws are still taken; 0xff7c0000 for a
                        // cast outside the monster ranges), layer 9 when the victim is in water.
                        if (dmg >= 1) {
                            const Enemy* hr = etab.get(en.cast_id);
                            const bool mon = (en.cast_id >= 0x0e && en.cast_id <= 0x53) || (en.cast_id >= 0xa6 && en.cast_id <= 0xb0);
                            std::uint32_t col = mon && hr ? hr->blood : 0xff7c0000u;
                            if (col && !(col & 0xff000000u)) col |= 0xff000000u;
                            const int hi = int(heading * 40.743664f);
                            dust_at("hit", 5, col, false,
                                    {org.x - cos_table(hi & 0xff) * en.hurt_radius, org.y, org.z - cos_table((hi - 0x40) & 0xff) * en.hurt_radius},
                                    0.3f, 0.45f, 0.025f, en.ai.in_water);
                        }
                    }
                    // +300 per hit dealt (0x4fd0bf, DAT_006bdc44) when the kind is not 0 / 5 (skills).
                    if (skillmp::boost_hit_counts(skillmp::attack_kind(avm.section()))) add_boost(300.0f);
                    // Knockback: L = walk speed (CASTINFO+0x48) x1.7 [x run mult while running —
                    // the view has no run state yet] x the swing's arg[12], driven along the
                    // player's facing (hit.hpp player_knock_angle).
                    const float kang = player_knock_angle(heading,
                                                          feet.x, feet.z, feet.y, en.pos.x, en.pos.z, en.pos.y);
                    react_enemy(en, dmg, r.died, rtype,
                                knock_impulse(kang, player_knock_scale(pm.speed_base, false, 2.2f) * knock));
                    if (lk == LaunchKind::Juggle) { en.mover.k1.x *= 0.875f; en.mover.k1.z *= 0.875f; }   // 0x6bd3cc, this hit's knock included
                    const std::string nm = casts.get(en.cast_id) && !casts.get(en.cast_id)->display.empty()
                                           ? casts.get(en.cast_id)->display : ("cast " + std::to_string(en.cast_id));
                    hud.debug_message(r.died ? (nm + " defeated!")
                                       : (nm + "  -" + std::to_string(dmg) + " (" +
                                          std::to_string(int(en.hp)) + "/" +
                                          std::to_string(int(en.max_hp)) + ")"));
                    if (std::getenv("CLERIA_DMG"))
                        std::printf("  hit %s (lv%d DEF %.0f): %d dmg, %.0f/%.0f left\n",
                                    nm.c_str(), en.level, en.def, r.damage, en.hp, en.max_hp);
                    if (r.died) {
                        // The kill credit (0x4fcaf5..0x4fcd42): nothing once +0x12c & 0x4000000 is set,
                        // which this kill sets (a revived actor keeps it: REVIVE.md §3.10c).
                        const float kexp = award_exp(en.exp_reward, en.level, /*boss*/false, (en.flags12c & 0x4000000u) != 0, -1,
                                                     en.cast_id);
                        // The kill SP in the same block [BYTES 0x4fcd1d..0x4fcd3d]: the victim's +0xc4 through
                        // FUN_004fa390 into the wallet (FUN_004fa290), when it is not 0.
                        const int ksp = !(en.flags12c & 0x4000000u) ? credit_sp(en.sp_reward, "kill", en.cast_id) : 0;
                        en.flags12c |= 0x4000000u;
                        {   // mods: enemy_killed (LUA_API.md §3)
                            events::Event ke;
                            ke.kind = events::Kind::EnemyKilled;
                            ke.cast = en.cast_id; ke.level = en.level; ke.amount = kexp; ke.amount2 = ksp;
                            ke.pos[0] = en.pos.x; ke.pos[1] = en.pos.y + en.height; ke.pos[2] = en.pos.z;
                            content_emit(ke);
                        }
                        // The revive family flips +0x130 ^= 9 on the first dead tick when the Chimes are
                        // on, before FUN_004780e0's death effects and drops (REVIVE.md §3.1).
                        const std::uint32_t f130 = en.flags130 ^
                            ((revive::row(en.ai_index) && (inv.equip_flags & 0x8000u)) ? 9u : 0u);
                        {   // FUN_004780e0 -> FUN_00478510's death path (hit_feedback.hpp): its entry
                            // draw (the scale), the puff (+ SE 862 with +0x130 & 0x2000) and the
                            // character's spark, from the victim's last vector to the player.
                            const std::uint32_t d0 = pick_rng.next();
                            en.hit_flash = hitfx::kHurtTimer;            // +0x238 = 1.65 (whatever +0x130)
                            const hitfx::DeathPuff dp = hitfx::death_puff(f130);
                            if (dp.spark) {
                                const Vec3 tp = en.ai.v(0x734);
                                const hitfx::Spark sk = hitfx::place_spark({en.pos.x, en.pos.y + en.height, en.pos.z},
                                    cam.forward(), tp.x, tp.z, hitfx::spark_scale(d0, hitfx::kDeathLo, hitfx::kDeathHi));
                                ef2sim::SpawnParams sp; sp.side = 0;
                                if (dp.effect >= 0) spawn_sim_fx(dp.effect, sk.pos, sk.rot, sk.scale, sp, 0, {});
                                if (dp.se >= 0) sfx_at(dp.se, sk.pos, kSfxAnimVol);
                                const int sid = hitfx::hit_spark_effect(character, game.g_flags[149], false, 0);
                                if (sid >= 0) spawn_sim_fx(sid, sk.pos, sk.rot, sk.scale, sp, 0, {});
                                if (sfx_log) std::printf("  [hitfx] t%llu death puff 0x%x spark 0x%x x%.2f\n",
                                                         (unsigned long long)tick_count, dp.effect, sid, sk.scale);
                            }
                        }
                        // FUN_00478510's drop loop is skipped while +0x130 & 1 (0x478b5c; factory 0x2009 / 0x6081 / 0x1);
                        // casts 0xaf (M890) / 0xb0 (M900) branch away before it (0x478b39..0x478b49 -> 0x479348: no drops).
                        if (const Enemy* er = etab.get(en.cast_id); er && !(f130 & 1u) && en.cast_id != 0xaf && en.cast_id != 0xb0) {
                            DropContext dc;                 // [not modelled] luck
                            dc.launch = en.launch;          // +0x4d0: a corpse keeps its count (LAUNCH_STATE.md §2.3)
                            dc.combo = kill_combo.count;    // 0x76c1a8 (FUN_00478510 0x479062)
                            dc.flag108 = game.g_flags[108] == 1;
                            dc.bless8 = (game.g_flags[217] & 8) != 0;
                            dc.no_drops = game.g_flags[146] != 0;
                            dc.boss_lock = game.g_flags[156] != 0;
                            dc.rate = settings().s.access.drop_rate;   // the shell's Cheats
                            if (lua_combat_on(script::CombatQuery::Kind::Drop))   // Lua mods: the "drop" filter
                                dc.decide = [&](int item, int slot, float chance, float* value) {
                                    script::CombatQuery q;
                                    q.kind = script::CombatQuery::Kind::Drop;
                                    q.item = item; q.value = -1.0;
                                    q.num = {{"slot", double(slot)}, {"chance", double(chance)}, {"cast", double(en.cast_id)},
                                             {"level", double(en.level)}, {"x", double(en.pos.x)},
                                             {"y", double(en.pos.y + en.height)}, {"z", double(en.pos.z)}};
                                    q.str = {{"name", g_content.items.name(item)}};
                                    const script::CombatDecision d = lua_combat(q);
                                    if (d.action == script::CombatDecision::Action::Suppress) return -1;
                                    if (d.action != script::CombatDecision::Action::Replace) return item;
                                    *value = d.value >= 0.0 ? float(std::min(d.value, 999999.0)) : -1.0f;
                                    const bool known = (d.item >= 0 && d.item < content::kRetailItems) ||
                                                       (d.item >= content::kNewItemFirst && d.item <= content::kNewItemLast);
                                    return known ? d.item : item;
                                };
                            // The drop position is enemy+0x50..+0x58 exactly, its ORIGIN (0x4785b5), not its feet.
                            roll_enemy_drops(pick_rng, *er, en.cast_id, {en.pos.x, en.pos.y + en.height, en.pos.z}, dc,
                                             pickups, /*entry_draw*/ false);
                        }
                        // FUN_00478510's death path, after the drops (0x479348..0x479390; reached by the casts that
                        // skip the drops too): three blood splats at the origin (18 draws) when the cast has a colour.
                        if (const Enemy* br = etab.get(en.cast_id);
                            br && br->blood != 0 && !(en.flags12c & 0x80000000u) && settings().s.game.bloody_effect_level > 0 &&
                            !en.ai.in_water)                 // not while +0x38a [EXPORT 00478510.c:747-759]
                            blood_at("death", 3, br->blood | 0xff000000u, 2, 0x18, 1.3f, 1.8f,
                                     {en.pos.x, en.pos.y + en.height, en.pos.z}, 0.65f, false, false);
                        // The death section's GenerateDebris (spawn_debris). M360's corpse and a familiar run their
                        // section 5 live and issue it there.
                        if (en.ai_index != 29 && !hugo_eye(en))
                            if (const DeathDebris* dd = enemy_death_debris(asset_root, casts.sprite_stem(en.cast_id))) {
                                const Vec3 org{en.pos.x, en.pos.y + en.height, en.pos.z};
                                if (dd->tick == 0) spawn_debris(en.cast_id, org, dd->ev);
                                else pending_debris.push_back({dd->tick, en.cast_id, org, &dd->ev});
                            }
                        if (en.cast_id >= 0 && en.cast_id < 256) {   // FUN_004fbb40: the Topic record
                            topic_seen[en.cast_id] = 1;
                            topic_kills[en.cast_id] = std::min(topic_kills[en.cast_id] + 1, 50);
                        }
                        // Death entry plays p_hit0 from C for any actor, and the enemy's
                        // own death animation adds its z_dead* pair (SFX_TRIGGERS.md §4.3).
                        // Positional: panned/attenuated from the enemy's spot.
                        sfx_at(kSfxDeathEntry, en.pos, kSfxAnimVol);
                        for (int id : enemy_death_sounds(asset_root, casts.sprite_stem(en.cast_id)))
                            sfx_at(id, en.pos, kSfxAnimVol);
                    }
                    return r.died;
            };
            // The mode-0 scan's breakables pass for one attacker volume `vol` (keyed on `tok`, re-hit
            // `rehit`); returns the number of @Break_* objects smashed.
            int mesh_struck = 0;                 // 0x186 child spheres struck (the particle pass reads it)
            auto smash_breakables = [&](const HitShape& vol, std::uint32_t tok, int rehit, bool one_shot,
                                        float power, int cls) {
                const Vec3 c = vol.centre;
                int broke = 0;
                // BREAKABLES (SOB trigger class 1 with a script): the same swing volume also
                // tests the room's attackable scene objects. @Break_* is a one-hit break —
                // no SOB field holds HP, and Mesh_SetHp is called by none of the shipped
                // scripts — then it plays SE 114, rolls each of its two $I drop slots and
                // fades the mesh out. The roll rate is the script's own: @Break_5 20%,
                // @Break_30 50%, @Break_100 always.
                for (const SceneTrigger& t : room.mesh.triggers) {
                    if (!(t.trigger & 1) || broken_objects.count(room.name + "/" + t.name)) continue;
                    std::string lname = t.name;
                    for (char& ch : lname) ch = char(std::tolower((unsigned char)ch));
                    if (auto hm = hit_meshes.find(lname); hm != hit_meshes.end()) {
                        // FUN_004fed50's GoMesh pass [EXPORT 004fed50 l.297-340]: the swing vs the 0x186
                        // child sphere (at the mesh + offset); FUN_004faf20(0, 0) on the child: its re-hit
                        // bank (FUN_004faac0), then FUN_004fa550 with the attacker's class +0x109 against the
                        // child's copied block (the SOB record's DEF, resist bits and no-floor byte) into
                        // child +0x134 (0x4fb489); the mesh's +0x98 -= it (0x4ff6b1), and below 1.0 (0x4ff6c9) the
                        // mesh's script runs (FUN_00434970); the object STAYS (FLOOR3 B1). The torches
                        // (flags 0x6b0) take only class 1 / 5 (the fire skill), the walls and S_3103's pole
                        // (0x670) only class 2 / 6 (thunder); 0x400 drops the min-1 chip, so a melee or
                        // wind hit deals 0 (#53).
                        HitMesh& h = hm->second;
                        if (h.ran) continue;
                        const MeshObj* mo = meshes.find(lname);
                        const Vec3 at = mo ? mo->pos : t.position;
                        const HitShape body = body_shape({at.x + h.off.x, at.y + h.off.y, at.z + h.off.z}, h.r, 0.0f, 0, 0.0f);
                        if (!overlap(vol, body)) continue;
                        if (h.bank.check_and_record(tok, rehit)) continue;
                        if (one_shot) atk_window = 0;
                        ++mesh_struck;                                 // a landed hit (FUN_004fed50's return)
                        {
                            AttackStats a;
                            a.atk = player_stats().atk;
                            a.bonus = power;
                            EnemyDefense vd;                             // the child: the SOB battle block
                            vd.def = t.def;
                            for (int k = 0; k < 4; ++k) vd.resist[k] = t.resist[k];
                            vd.no_floor = t.no_floor;
                            if (!h.hp_set) { h.hp = t.hp; h.hp_set = true; }   // the mesh's +0x98
                            PlayerDamageMods mods;
                            mods.atk_debuff = ail.has(ailments::kCurse);   // FUN_004fa550: ATK - 5, floor 0
                            mods.yunica_bonus = character == 1 && game.g_flags[149] == 0;   // FUN_004faf20 uVar9
                            h.hp -= float(enemy_incoming_damage(a, cls, vd, mods,
                                [&](float pct) { return float(rng_pct(rng)) < pct; }));
                        }
                        if (!(h.hp < 1.0f)) continue;                    // +0x98 <= 1.0 && != 1.0
                        h.ran = true;
                        std::string stem = t.script_stem;
                        for (char& ch : stem) ch = char(std::tolower((unsigned char)ch));
                        XsoVm::Vars ov;
                        ov.s[0] = t.name; ov.i[0] = t.i0; ov.i[1] = t.i1; ov.f[0] = t.f0;
                        if (csMgr.start(stem, ov, /*owner: the player*/ 13))
                            std::printf("  [hit] %s -> %s\n", t.name.c_str(), stem.c_str());
                        continue;
                    }
                    // Only the @Break_* family is the smash; any other trigger-1 script waits for
                    // its 0x186 (the root sets it on every visit).
                    if (t.script_stem.empty() || t.script_stem[0] != '@') continue;
                    // Sphere vs the object's world AABB.
                    const float qx = std::max(t.aabb_min.x, std::min(c.x, t.aabb_max.x));
                    const float qy = std::max(t.aabb_min.y, std::min(c.y, t.aabb_max.y));
                    const float qz = std::max(t.aabb_min.z, std::min(c.z, t.aabb_max.z));
                    const float ddx = c.x - qx, ddy = c.y - qy, ddz = c.z - qz;
                    if (ddx * ddx + ddy * ddy + ddz * ddz > vol.radius * vol.radius) continue;
                    broken_objects.insert(room.name + "/" + t.name);
                    {   // The object's own @Break_* script (FUN_00434970 on the mesh; FLOOR13_OPS.md):
                        // $S0 = its name, $I0/$I1 = its SOB drop ids. It poses OPEN, plays SE 114 (under
                        // water SE 779 + three effects 61 on the mesh), loads each drop id with Reg_SetInt
                        // and rolls it (0x81, then 0x17e Mesh_DropItem), then turns the collision off
                        // (the room rebuild drops the mesh), fades and hides it. AllowMultiInstance lets
                        // several pots break at once. The stand-in below runs only when it cannot start.
                        std::string bstem = t.script_stem;
                        for (char& ch : bstem) ch = char(std::tolower((unsigned char)ch));
                        XsoVm::Vars bv;
                        bv.s[0] = t.name; bv.i[0] = t.i0; bv.i[1] = t.i1; bv.f[0] = t.f0;
                        if (events_enabled && csMgr.start(bstem, bv, /*owner: the player*/ 13)) {
                            std::printf("  [hit] %s -> %s\n", t.name.c_str(), bstem.c_str());
                            continue;
                        }
                    }
                    sfx_at(114, t.position, kSfxAnimVol);      // the shatter SE
                    int pct = 50;                               // @Break_30
                    if (t.script_stem.find("_5") != std::string::npos) pct = 20;
                    if (t.script_stem.find("_100") != std::string::npos) pct = 100;
                    // The script's guard + 0x81 roll (one game-RNG draw: (xs() >> 17) % 100 < p),
                    // then 0x17e Mesh_DropItem($S0, $Ix, 1): a kept world pickup (PICKUPS.md §13.7).
                    // [STAND-IN] only when the script did not start (CLERIA_EVENTS=0 / missing).
                    for (int slot : {t.i0, t.i1}) {
                        if (slot == 0) continue;
                        if (pct < 100 && int(pick_rng.next() >> 17) % 100 >= pct) continue;
                        drop_at(t.position, slot, true);
                    }
                    ++broke;
                }
                return broke;
            };
