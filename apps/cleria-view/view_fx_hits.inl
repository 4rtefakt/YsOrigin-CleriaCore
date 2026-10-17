// The EF2 step with its hit tick (included inside the game loop, twice): every effect and particle steps, and each
// armed particle is tested — the player's own on the monsters, Velagunder's parts and the rest of the mode-0 scan,
// a monster's on the player (FUN_004fed50(0) from FUN_005645b0). The live tick includes it; so does the frozen
// branch under a Fix that holds slot 0 only (FIX_SLOTS.md row 20, S7): the hit test needs the actor-stop byte
// 0x74ab8b == 0 — slot 1 alone — [EXPORT 005645b0.c:109-110], so particles hit under a Fix(0) and not under a
// Fix(1), where the plain step runs. [LIKELY, not retail-proven] as S5c (build/audit/RETAIL_ANSWERS.md A1).
// Needs hurt_player and p_untouchable (view_enemy_hits.inl) in scope.
            {
                const Cast* pc = casts.get(13);
                const HitShape pbody = body_shape({feet.x, feet.y + kOriginH, feet.z},
                                                  pc ? pc->hurt_radius : 0.45f, pc ? pc->hurt_half_h : 0.5f,
                                                  pc ? pc->hurt_shape : 1, pc ? pc->center_offset : -0.25f);
                int fx_broke = 0;                        // @Break_* objects the player's particles smashed
                fxsim.step([&](ef2sim::Particle& p) {
                    if (p.hit.side == 0) {
                        // The player's own effect particles on the monsters (FUN_004fbb40 through the
                        // mode-0 scan): the victim's bank keys on the particle token, 0 in practice
                        // (EFFECTHIT.md §5.4). [H] every overlapping monster is hit in the one tick,
                        // and the knockback heading is the particle -> victim direction.
                        bool landed = false;
                        const HitShape vol{p.hit.radius, p.hit.centre, p.hit.half_h, p.hit.half_h > 0.0f ? 1 : 0};
                        part_atk = &vol;                                   // the boss parts' hit spark
                        for (Actor& en : world.actors()) {
                            if (en.is_player() || !en.alive() || en.destroy_requested()) continue;
                            const EnemyRender* vr = nullptr;
                            for (const EnemyRender& er : render) if (er.actor == &en) { vr = &er; break; }
                            if (vr && !vr->actor->ai.hittable) continue;            // +0x60 & 0x80 clear
                            const HitShape body = body_shape({en.pos.x, en.pos.y + en.height, en.pos.z},
                                                             en.hurt_radius, en.hurt_half_h, en.hurt_shape, en.hurt_off);
                            if (!overlap(vol, body)) continue;
                            if (en.bank.check_and_record(0u, p.hit.rehit)) continue;
                            const float hd = std::atan2(en.pos.z - p.hit.centre.z, en.pos.x - p.hit.centre.x);
                            player_hit_enemy(en, vr, p.hit.power, p.hit.knock, p.hit.rtype,
                                             p.hit.cls >= 0 ? p.hit.cls : kDamageClassNone, hd, /*actor_owned*/ false);
                            landed = true;
                        }
                        // The same mode-0 scan's GoMesh pass (FUN_004fed50 [EXPORT 004fed50.c:296-345],
                        // VELAGUNDER.md §3.3): Velagunder's parts, so magic / skill effects (Hugo's
                        // shots) reach the stun part and the core as the swing does. Token 0, the
                        // particle's re-hit; the damage with the part's DEF / resist and the particle's
                        // power / class.
                        if (boss && !boss->destroyed())
                            for (int pi = 0; pi < boss01::kParts; ++pi) {
                                if (!boss->part_hittable(pi)) continue;
                                const Boss01Part& bp = boss->part(pi);
                                if (!overlap(vol, body_shape(bp.hurt_centre, bp.hurt_radius, 0.0f, 0, 0.0f))) continue;
                                if (boss_banks[std::size_t(pi)].check_and_record(0u, p.hit.rehit)) continue;
                                AttackStats a;
                                a.atk = player_stats().atk;
                                a.bonus = p.hit.power;
                                EnemyDefense vd; vd.def = bp.def;
                                for (int k = 0; k < 4; ++k) vd.resist[k] = bp.resist[k];
                                PlayerDamageMods mods;
                                mods.atk_debuff = ail.has(ailments::kCurse);   // FUN_004fa550: ATK - 5, floor 0
                                mods.yunica_bonus = character == 1 && game.g_flags[149] == 0;
                                const int dmg = lua_part_damage(enemy_incoming_damage(a, p.hit.cls >= 0 ? p.hit.cls : kDamageClassNone, vd, mods, [&](float pct) { return float(rng_pct(rng)) < pct; }), a, vd.def, p.hit.cls >= 0 ? p.hit.cls : kDamageClassNone);
                                const int pcls = p.hit.cls >= 0 ? p.hit.cls : kDamageClassNone;
                                const int dmg_hp = part_element(bp.pos, bp.armor, dmg, pcls);
                                if (dmg > 0) boss->add_part_damage(pi, float(dmg), float(dmg_hp));
                                boss_part_feedback(bp, dmg, pcls);
                                if (std::getenv("CLERIA_DMG"))
                                    std::printf("  fx %d hits boss part %d (DEF %.0f): %d dmg, HP %.0f stun %.0f part HP %.0f\n",
                                                p.fx ? p.fx->id : -1, pi, bp.def, dmg, boss->hp(), boss->stun_gauge(),
                                                boss->part(pi).hp);
                                landed = true;
                            }
                        #include "view_fx_mode0.inl"     // the rest of the mode-0 scan for a particle (Hugo's shots)
                        part_atk = nullptr;
                        return landed;
                    }
                    if (p.hit.side == 1 && !g_lua_targets.empty()) {   // a monster's shot on the mods' other players (API 7)
                        const HitShape tv{p.hit.radius, p.hit.centre, p.hit.half_h, p.hit.half_h > 0.0f ? 1 : 0};
                        const Cast* tc = casts.get(13);
                        for (int pid : g_lua_targets) {
                            Actor* pu = world.find(pid);
                            if (!pu) continue;
                            const HitShape tb = body_shape({pu->pos.x, pu->pos.y + kOriginH, pu->pos.z}, tc ? tc->hurt_radius : 0.45f,
                                                           tc ? tc->hurt_half_h : 0.5f, tc ? tc->hurt_shape : 1, tc ? tc->center_offset : -0.25f);
                            if (!overlap(tv, tb) || pu->bank.check_and_record(0u, std::max(p.hit.rehit, 20))) continue;
                            content_emit(events::Event::named("puppet_struck").with("puppet", pid).with("actor", 0).with("cast", -1).with("level", 0)
                                             .with("atk", p.hit.atk).with("power", p.hit.power));
                        }
                    }
                    if (p.hit.side != 1 || player_iframes > 0 || p_untouchable || player.hp < 1) return false;
                    const HitShape vol{p.hit.radius, p.hit.centre, p.hit.half_h, p.hit.half_h > 0.0f ? 1 : 0};
                    float ang = 0;
                    if (!overlap(vol, pbody, &ang)) return false;
                    if (player_bank.check_and_record(0u, p.hit.rehit)) return false;
                    const int hp0 = player.hp;
                    hurt_player(p.hit.atk, p.hit.power, p.hit.rtype, ang, p.hit.status);
                    if (std::getenv("CLERIA_DMG"))
                        std::printf("  [hurt] t%llu fx %d (atk %.0f power %.1f knock %.2f rtype %d r %.2f) player hp %d -> %d\n",
                                    (unsigned long long)tick_count, p.fx ? p.fx->id : -1, p.hit.atk, p.hit.power,
                                    p.hit.knock, p.hit.rtype, p.hit.radius, hp0, player.hp);
                    return true;
                });
                if (fx_broke) reload_room(/*rerun_events*/false);   // drop a mesh a particle shattered
                fxsim.sweep();
                if (ai_debug && (tick_count % 60) == 0 && !fxsim.effects().empty())
                {
                    std::printf("[fx] effects %zu particles %zu:", fxsim.effects().size(), fxsim.particles().size());
                    for (const auto& f : fxsim.effects()) std::printf(" %d(owner %d live %d)", f->id, f->owner, f->live);
                    std::puts("");
                }
            }
