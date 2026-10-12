// The kind-0x18 chain lightnings' tick (FUN_00592d00 / FUN_00592af0; src/cleria/extra_versions.hpp Chain,
// build/mode_re/GOXX_17_19.md §3), included in the game loop before the effects' hit tick: needs player_hit_enemy
// (view_player_hit_defs.inl) in scope. An arrival is one direct hit (FUN_004fba30 flag 1: the victim's hittable
// bit and HP only; no overlap test, no re-hit bank — the chain's token and timer are 0), power +0xfc, class 2,
// knock 0, rtype 0. The heading handed to FUN_004fbb40 is the victim's hurt centre seen from the world origin: the
// chain's own volume centre +0xdc is never set [BYTES 0x4fba72..0x4fbac4].
// [H] not stepped under a Fix. [approx] the damage reads the player's ATK now, not the copy taken at creation.
            for (extra::Chain& c : xchains) {
                const bool clog = std::getenv("CLERIA_DMG") != nullptr;
                auto render_of = [&](const Actor& en) -> const EnemyRender* {
                    for (const EnemyRender& er : render) if (er.actor == &en) return &er;
                    return nullptr;
                };
                const bool scan = c.tick(
                    [&](int id, Vec3* p) {                               // the slot's id unchanged, HP >= 1
                        const Actor* a = world.find(id);
                        if (!a || a->is_player() || a->destroy_requested() || a->hp < 1.0f) return false;
                        *p = {a->pos.x, a->pos.y + a->height, a->pos.z};
                        return true;
                    },
                    [&] { return pick_rng.next(); },
                    [&](int id) {
                        Actor* en = world.find(id);
                        if (!en || !en->ai.hittable) return;             // +0x60 & 0x80
                        const float hp0 = en->hp;
                        player_hit_enemy(*en, render_of(*en), c.power, 0.0f, 0, 2, std::atan2(en->pos.z, en->pos.x),
                                         /*actor_owned*/ false);
                        if (clog)
                            std::printf("  [extra] t%llu chain hop %d/%d hits #%d (cast %d): HP %.0f -> %.0f\n",
                                        (unsigned long long)tick_count, c.cur + 1, c.n, id, en->cast_id, hp0, en->hp);
                    });
                if (!scan) continue;
                // The ray (no monster at the cast) [BYTES 0x592af0..0x592cf7]: every hittable monster whose body
                // (+0x68, + 0.4) the 5-unit ray meets, through the re-hit bank on the chain's token (12 ticks).
                for (Actor& en : world.actors()) {
                    if (en.is_player() || en.destroy_requested() || en.hp < 1.0f) continue;
                    if (!(en.kind & 0x8u) || !en.ai.hittable) continue;  // (+0x60 & 0x88) == 0x88
                    if (!c.ray_reaches({en.pos.x, en.pos.y + en.height, en.pos.z}, en.hurt_radius)) continue;
                    if (en.bank.check_and_record(c.token, 12)) continue;
                    const float hp0 = en.hp;
                    player_hit_enemy(en, render_of(en), c.power, 0.0f, 0, 2, c.heading, /*actor_owned*/ false);
                    if (clog)
                        std::printf("  [extra] t%llu chain ray hits #%d (cast %d): HP %.0f -> %.0f\n",
                                    (unsigned long long)tick_count, en.instance_id, en.cast_id, hp0, en.hp);
                }
            }
            std::erase_if(xchains, [](const extra::Chain& c) { return c.dead; });
