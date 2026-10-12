// The Extra versions' host side (src/cleria/extra_versions.hpp, build/mode_re/EXTRA_VERSIONS.md); included in
// render_window after view_hugo_eyes.inl. `skin == 1` is the Type Select answer of the character in play
// (0x74c0dc / 0x74c0d8 / 0x74c0d4).
//
//   extra_tick()           the three blocks of the player tick FUN_00465170 [BYTES 0x466e0a..0x467aff]
//   extra_runners_tick()   the kind-0x17 runners (FUN_00591850), once per sim tick with the bullets
//
// [H] the counters' phase: the blocks read the section +0x994 inside the player tick, after the action input; here
// extra_tick() runs once per player tick after the attack request's set_section, before the skill cast of the same
// tick (skill_tick) — so a skill's section is first seen on the tick after its cast. One call site (view_game.inl,
// beside hugo_eyes_tick): move it to flip the reading.
extra::Counters xcnt;                                  // (xrunners: beside `bullets`, for the scene change's clear)
// FUN_00429e40(lo, hi) [BYTES 0x429e40]: one step of the game's xorshift, lo + U * (hi - lo), U in [0, 1) from
// the word's low 23 bits.
auto extra_rnd = [&](float lo, float hi) {
    const std::uint32_t bits = (pick_rng.next() & 0x7fffffu) | 0x3f800000u;
    float u;
    std::memcpy(&u, &bits, 4);
    return lo + (u - 1.0f) * (hi - lo);
};
// FUN_005916e0 from the two Terra branches: step 0.3, power 0, scale 1.0, owner = the player.
auto extra_runner = [&](const extra::Emitter& em, const Vec3& org, float h) {
    if (!em.on) return;
    xrunners.push_back(extra::Runner::make({org.x + std::cos(h) * em.ahead, org.y, org.z + std::sin(h) * em.ahead}, h,
                                           0.3f, 0.0f, 1.0f, em.c, em.p, em.effect, float(player_stats().atk)));
    if (std::getenv("CLERIA_DMG"))
        std::printf("  [extra] t%llu runner: effect %d C %d P %d, %.2f ahead\n", (unsigned long long)tick_count, em.effect,
                    em.c, em.p, em.ahead);
};
bool xlogged = false;
auto extra_tick = [&] {
    bullets.set_hugo_extra(skin == 1 && character == 2);           // FUN_00593b90's test of 0x74c0d8 (bullet.cpp)
    if (skin != 1) { xlogged = false; return; }
    if (!xlogged && std::getenv("CLERIA_DMG")) {
        std::printf("  [extra] t%llu the Extra version is in play (character %d)\n", (unsigned long long)tick_count, character);
        xlogged = true;
    }
    const int sec = avm.section();
    const float h = std::atan2(face_dir.z, face_dir.x);            // +0x308 - the camera yaw = the facing +0x314
    const Vec3 org{feet.x, feet.y + kOriginH, feet.z};             // +0x50
    auto shot = [&](int kind, int effect, const Vec3& pos, const Vec3& dir, const float* f13) {
        bullet::Spawn s = bullet::block_spawn(kind, effect, pos, dir, f13);
        s.owner = 13; s.owner_monster = false;
        s.owner_atk = float(player_stats().atk);
        s.cls = player_damage_class(sec);                          // the 0x594174 table
        bullets.spawn(s);
    };
    if (character == 1) {                                          // Yunica [BYTES 0x466e0a..0x46737b]
        const extra::YunicaOut o = extra::yunica_tick(xcnt, sec);
        for (int i = 0; i < o.bullets; ++i) {
            const float a = h + float(i) * o.step + o.a0;
            const float f[13] = {o.speed, o.accel, 0, 0, 1.0f, 0, 0, 0, 0, o.f168, 1.9f, 0, 0};
            shot(6, 0x66, org, {std::cos(a), 0.0f, std::sin(a)}, f);
        }
        if (o.bullets && std::getenv("CLERIA_DMG"))
            std::printf("  [extra] t%llu Yunica: %d shots of 0x66 (section 0x%x)\n", (unsigned long long)tick_count, o.bullets, sec);
        extra_runner(o.em, org, h);
    } else if (character == 2) {                                   // Hugo [BYTES 0x46737b..0x4673ce]
        // Flag 157, the eyes' mode source, follows the bracelet: Ventus the V, Terra the rope, Ignis the wheel.
        game.g_flags[157] = extra::hugo_flag157(skillmp::bracelet_slot(inv.magic), game.g_flags[157]);
    } else if (character == 3) {                                   // Toal [BYTES 0x4673d4..0x467aff]
        const extra::ToalOut o = extra::toal_tick(xcnt, sec, game.g_flags[141] != 0);
        for (int k = 0; k < 2; ++k) {
            if (!o.fx[k]) continue;
            ef2sim::SpawnParams sp;                                // side 0, power 0, knock 1, rtype 0, status 0, cls -1
            sp.power = 0.0f; sp.knock = 1.0f; sp.rtype = 0; sp.status = 0; sp.cls = -1;
            sp.atk = player_stats().atk; sp.side = 0; sp.owner = 13;
            spawn_sim_fx(o.fx[k], org, {0.0f, camera_yaw(), 0.0f}, o.fx_scale[k], sp, 0, {});
            if (std::getenv("CLERIA_DMG"))
                std::printf("  [extra] t%llu Toal: effect %d x %.1f (section 0x%x)\n", (unsigned long long)tick_count, o.fx[k],
                            o.fx_scale[k], sec);
        }
        extra_runner(o.em, org, h);
        if (o.bullet) {                                            // eight draws, in this order
            Vec3 p{org.x + std::cos(h) * 2.0f, org.y, org.z + std::sin(h) * 2.0f};
            p.x += extra_rnd(-o.r, o.r); p.y += extra_rnd(-o.r, o.r); p.z += extra_rnd(-o.r, o.r);
            Vec3 d{};
            d.x = extra_rnd(-1.0f, 1.0f); d.y = extra_rnd(-1.0f, 1.0f); d.z = extra_rnd(-1.0f, 1.0f);
            const float l = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            d = l == 0.0f ? Vec3{} : Vec3{d.x / l, d.y / l, d.z / l};
            const float acc = extra_rnd(0.0001f, 0.0002f), spd = extra_rnd(0.01f, 0.015f);
            const float f[13] = {spd, acc, 0, 0, 1.0f, 0, 0, 0, 0, 0, 0, 0, 0};
            shot(2, 0xa7, p, d, f);
        }
    }
};
auto extra_runners_tick = [&] {
    auto ray = [&](const Vec3& o, const Vec3& d, Vec3* hit) {
        Vec3 n{};
        return segment_hit(phys_mesh(room), o, {o.x + d.x, o.y + d.y, o.z + d.z}, hit, &n);
    };
    for (extra::Runner& r : xrunners) {
        Vec3 at{};
        if (!r.tick(ray, &at)) continue;
        ef2sim::SpawnParams sp;                                    // side 0 (the owner is the player), cls 1, knock 0
        sp.power = r.power; sp.knock = 0.0f; sp.rtype = 0; sp.status = 0; sp.cls = 1;
        sp.atk = r.atk; sp.side = 0; sp.owner = 13;
        spawn_sim_fx(r.effect, at, {0.0f, -1.5707964f - r.heading, 0.0f}, r.scale, sp, 0, {});
        if (std::getenv("CLERIA_DMG"))
            std::printf("  [extra] t%llu runner pulse: effect %d at (%.2f, %.2f, %.2f)\n", (unsigned long long)tick_count,
                        r.effect, at.x, at.y, at.z);
    }
    if (std::getenv("CLERIA_DMG"))
        for (const extra::Runner& r : xrunners)
            if (r.dead)
                std::printf("  [extra] t%llu runner gone at (%.2f, %.2f, %.2f): %s\n", (unsigned long long)tick_count, r.pos.x, r.pos.y,
                            r.pos.z, r.life <= 0 ? "its life" : "a wall");
    std::erase_if(xrunners, [](const extra::Runner& r) { return r.dead; });
};
