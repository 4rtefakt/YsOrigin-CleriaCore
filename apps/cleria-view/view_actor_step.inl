// The actors' step after the handlers (included inside the game loop, twice): the body push FUN_0045e460 and the
// integrator FUN_0045a510 for every actor but the player — the surfaces and the water bytes, the swept sphere,
// the floor ray, the settle — then Zava's handler tail. The live tick includes it after world.step(); so does
// the dead player's branch (FIX_SLOTS.md S8): no byte of §1.1 is set by a death, the world runs on under the
// game-over menu until a choice [EXPORT 0046fba0.c:77-82; 00427820.c:41-47]. Needs view_revive.inl's lambdas.
            // FUN_0045e460, the body push (body_push.hpp): every actor's body in the list's order;
            // each ticking monster runs its pass right after its own integrator, as the handlers do
            // (FUN_0045a510 then FUN_0045e460). The player's own pass pushes nobody, so it is not run.
            // [not modelled] NPC passes (class 4 bodies: no NPC is alive() in the view).
            prof("tick:enemies");
            std::vector<bodypush::Body> bodies;
            std::vector<bodypush::Body*> body_list;
            {
                const Cast* pc = casts.get(13);
                for (Actor& a : world.actors()) {
                    bodypush::Body b;
                    if (a.is_player()) {
                        b.shape = body_shape({feet.x, feet.y + kOriginH, feet.z}, pc ? pc->hurt_radius : 0.45f,
                                             pc ? pc->hurt_half_h : 0.5f, pc ? pc->hurt_shape : 1,
                                             pc ? pc->center_offset : -0.25f);
                        // +0x60 & 0x80: cleared at death and while Godspeed's ramp runs (+0x4d8 < 1).
                        b.kind = std::uint8_t(player.hp >= 1 && gs_ramp >= 1.0f ? 0x81 : 0x01);
                        b.section = avm.section();
                        b.push = bodypush::Push::of(player_kb2);
                    } else {
                        b.shape = body_shape({a.pos.x, a.pos.y + a.height, a.pos.z}, a.hurt_radius, a.hurt_half_h,
                                             a.hurt_shape, a.hurt_off);
                        const bool live = a.alive() && a.ai.hittable && !a.destroy_requested();
                        b.kind = std::uint8_t(live ? (a.kind & 0xff) : (a.kind & 0x7f));
                        b.speed = a.ai.speed310;                     // +0x310
                        for (const EnemyRender& er : render)
                            if (er.actor == &a && er.avm.ok()) { b.section = er.avm.section(); break; }
                        b.token = 0x02000000u | std::uint32_t(a.instance_id);   // [H] +0x80 = actor+0x14
                        if (a.cast_id == 0xb8 && a.ai.phase() != 0) b.mask_clear = 8;   // M980 INIT 0x4d45f0: +0x64 &= ~8
                        // The Piache handlers (M420 0x4c00c0, M450 0x4c11d0): +0x64 bit 0 follows alpha > 0.35 — a faded
                        // one neither pushes nor is pushed by the player. [approx] the dead path keeps the last value there.
                        if ((a.cast_id == 0x2e || a.cast_id == 0x31) && !(a.ai.alpha > 0.35f)) b.mask_clear |= 1;
                        b.bank = &a.bank;
                        b.push = bodypush::Push::of(a.mover.k2);
                    }
                    bodies.push_back(b);
                }
                for (bodypush::Body& b : bodies) body_list.push_back(&b);
            }
            std::size_t body_i = 0;
            for (Actor& a : world.actors()) {
                bodypush::Body& self_body = bodies[body_i++];
                // FUN_0045c490 [BYTES 0x45d371]: a tagged actor's glow phase +1 per tick.
                // [not modelled] +4/+5 in state +0x6f0 == 2 (its meaning for enemies is unknown).
                if (a.tag.tagged()) a.glow_phase = std::uint8_t(a.glow_phase + 1);
                ++a.age_ticks;                          // +0x504, every actor every tick (0x45d32d; M900's ring reads Hugo's)
                // FUN_0045c490's foot dust [BYTES 0x45d228..0x45d2fb]: the cooldown +0x4c2 counts down; else the bit
                // anime_FootSmoke left (+0x3c0 & 1) is taken and, on the ground and out of water, five puffs rise at
                // (x, +0x318 + 0.18, z) — grey-brown, or additive grey on surface 0xa — and the cooldown is 5.
                // [approx] the water bytes +0x389 / +0x38a: the player's wading / submerged, an enemy's in_water;
                // the player's +0x318 is taken as the feet (it is grounded).
                if (a.dust_cd) --a.dust_cd;
                else {
                    AsoVm* fv = a.is_player() ? &avm : nullptr;
                    if (!fv) for (EnemyRender& er : render) if (er.actor == &a) { fv = &er.avm; break; }
                    if (fv && fv->foot_smoke_bit()) {
                        fv->clear_foot_smoke_bit();
                        const bool pl = a.is_player();
                        if ((pl ? pv.grounded : a.vert.grounded) && !(pl ? (wading || submerged) : a.ai.in_water)) {
                            a.dust_cd = 5;
                            const bool s10 = (pl ? cur_surface : a.ai.surf) == 0xa;
                            dust_at("foot", 5, s10 ? 0xa0606060u : 0xc0bda684u, s10,
                                    pl ? Vec3{feet.x, feet.y + 0.18f, feet.z} : Vec3{a.pos.x, a.floor_y + 0.18f, a.pos.z},
                                    0.35f, 0.32f, 0.012f, false);
                        }
                    }
                }
                // The hurt timer +0x238 (FUN_0045a3a0: -0.075 a tick, clamped at 0) runs on the dead
                // too (a duel puppet stays on screen).
                a.hit_flash = hitfx::step_hurt(a.hit_flash);
                if (a.is_player() || (!a.alive() && !revive_corpse(a))) continue;   // a family corpse falls on
                // The integrator FUN_0045a510 (MOVER.md §3): one swept sphere (radius +0x328,
                // centred on the ORIGIN) through D = (displacement, dy), the floor ray from the
                // pre-move origin, then the settle (land with a 0.65 lerp / coyote / fall).
                // A flyer: dy = t (0 for the AI), no settle — the hover owns its altitude.
                // Not a Chr_LocateDummy marker (CASTINFO record 1, CAST_C000: no sprite, it never moves; FUN_00570350
                // places it with FUN_00459ff0 at the script's point): [approx] the sweep's push-out lifted a marker
                // standing inside a closed door's hull onto the door's top (S_5005's (84, -8, 0.35): 6.8 above the
                // floor), out of the action probe's reach, so the Crimson Key door never opened. [H] the engine's
                // step for it has a zero displacement and leaves it where the script put it.
                if (!(a.cast_id == 1 && !a.is_enemy())) {
                    const Vec3 o0{a.pos.x, a.pos.y + a.height, a.pos.z};
                    const float hitY = floor_at(phys_mesh(room), o0.x, o0.z, o0.y);
                    // Sand2 (MOVER.md §3.1/§3.2, 0x45a63f / 0x45a6fa): in the selector-1 set over a
                    // sand2 floor dy is clamped to [-0.015, 0.03]; on sand2 (+0x4b9) xz x0.25 and a
                    // rising dy x0.25; the settle keeps such an actor grounded above its floor.
                    enemy_surfaces(a);
                    // ENEMY_WATER.md W2 (§1.3; FUN_0045a510 has no cast test in these rows): submerged (+0x38a) dy
                    // is clamped to [-0.05, +0.1] (0x45a63f), the timer steps 0.0118 and the landing margin is
                    // 0.01 (0x45beee, 0x45be61), xz x 0.8 (x 0.9 with blessing bit 1; 0x45a82a..0x45a87d), and on
                    // lava / blood (+0x4b9 0x0d / 0x11) xz and a rising dy x 0.6 (xz 0.8 blessed; 0x45a8b0).
                    // [not modelled] the foot gear's x 1 (0x76a624 & 0x1000000), as for the player.
                    const bool sub = a.ai.in_water;
                    float dy = a.flyer ? a.vert.t : vertical_dy(a.vert, sub, sub && a.ai.surf_ray == 0x12);
                    if (a.vert.grounded && a.ai.surf != kSurfaceIce) a.mover.ice_tail = 0;   // 0x45d139
                    if (a.vert.grounded && !a.flyer && a.ai.surf_ray == kSurfaceIce && !(a.flags12c & 0x20000000u)) {
                        Vec3 ihit, in_n;                                        // the ice slope slide (0x45b053)
                        if (segment_hit(phys_mesh(room), o0, {o0.x, o0.y - 510.0f, o0.z}, &ihit, &in_n)) {
                            const Vec3 ip = ice_slope_push(in_n);
                            a.mover.k1.x += ip.x; a.mover.k1.y += ip.y; a.mover.k1.z += ip.z;
                        }
                    }
                    const bool in_sand = !a.flyer && a.ai.in_water && a.ai.surf_ray == 0x12;
                    if (in_sand) { dy = std::clamp(dy, -0.015f, 0.03f); a.ai.surf = 0x12; }   // +0x4b9 = 0x12
                    float mdx = a.mover.dx, mdz = a.mover.dz;
                    {   // sand2: 0.25, xz 0.5 with blessing bit 1 (no cast test, 0x45a762); lava / blood; water
                        const SurfaceScale ss = surface_scale(a.ai.surf, sub, (game.g_flags[217] & 2) != 0);
                        mdx *= ss.xz; mdz *= ss.xz; if (dy > 0.0f) dy *= ss.up;
                    }
                    Vec3 o1 = collide_and_slide(phys_mesh(room), o0, {mdx, dy, mdz}, a.radius);
                    if (hitY > kNoFloor && o1.y > hitY) a.floor_y = hitY;     // +0x318
                    if (!a.flyer && in_sand && o1.y - a.height >= a.floor_y + 0.01f) {
                        a.vert.grounded = true;                               // 0x45bf52: +0x388 = 1 in sand
                        a.vert.t = std::max(-0.5f, a.vert.t - 0.0118f);        // the in-water decrement
                    } else if (!a.flyer) {
                        a.vert.jumping = a.mover.loco == 3;                  // the coyote keys on +0x3a0
                        const bool was_air = !a.vert.grounded;
                        settle_vertical(a.vert, o1.y, a.floor_y, a.height, sub);
                        // While knocked up (+0x4d0 != 0; LAUNCH_STATE.md §2.1 [EXPORT 0045a510.c:869-883]) the fall is
                        // damped x 0.985 after the timer's step, and a live landing ends the state (a corpse keeps it).
                        if (a.launch != 0) {
                            if (!a.vert.grounded && a.vert.t < 0.0f) a.vert.t *= 0.985f;       // 0x6bd418
                            if (was_air && a.vert.grounded && a.hp >= 1.0f) a.launch = 0;
                        }
                    }
                    a.pos = {o1.x, o1.y - a.height, o1.z};
                }
                self_body.shape.centre = {a.pos.x, a.pos.y + a.height + a.hurt_off, a.pos.z};
                bodypush::pass(self_body, body_list.data(), body_list.size(), character);
            }
            boss6_zava_tail();                           // Zava's handler tail: the ride, the clamps (view_boss06.inl)
