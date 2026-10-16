// The Extra versions' host side (src/cleria/extra_versions.hpp, build/mode_re/EXTRA_VERSIONS.md); included in
// render_window after view_hugo_eyes.inl. `skin == 1` is the Type Select answer of the character in play
// (0x74c0dc / 0x74c0d8 / 0x74c0d4; for character 0 it is 0x74c0cc: Adol's Napishtim version, ADOL.md §4).
//
//   extra_tick()           the three blocks of the player tick FUN_00465170 [BYTES 0x466e0a..0x467aff], and
//                          Napishtim's Terra block just before them [BYTES 0x466cf4..0x466e02]
//   extra_runners_tick()   the kind-0x17 runners (FUN_00591850), once per sim tick with the bullets
//   napishtim_input()      the Terra counter's press and the Ignis release, before the action input
//   extra_special()        anime_Special of the player's script (FUN_00449ed0): the runner, the chain lightning
//                          (its tick: view_extra_chain.inl)
//
// [H] the counters' phase: the blocks read the section +0x994 inside the player tick, after the action input; here
// extra_tick() runs once per player tick after the attack request's set_section, before the skill cast of the same
// tick (skill_tick) — so a skill's section is first seen on the tick after its cast. One call site (view_game.inl,
// beside hugo_eyes_tick): move it to flip the reading.
extra::Counters xcnt;                                  // (xrunners: beside `bullets`, for the scene change's clear)
extra::Napishtim nap;                                  // Adol's +0x6fc / +0x700 / +0x704 / +0x708
bool nap_skip = false;                                 // this tick's skill block is jumped over (jmp 0x466b0d)
// Napishtim Adol, before the action input: an attack press steps the Terra counter [BYTES 0x465e68..0x465f17] —
// the 11th plays section 0x124 in place of the swing; the button up in an Ignis wind-up starts its swing [BYTES
// 0x465fa5..0x466047]. Either skips the skill block of the tick. [not modelled] +0x3b8 = 0 at the 0x124 start.
auto napishtim_input = [&](PlayerActions::In& pi) {
    nap_skip = false;
    const int k = pi.adol_slot;
    if (pi.attack_pressed && !pi.busy && nap.press(k)) {
        avm.set_section(0x124);
        if (pi.stick_active) face_dir = {std::cos(pi.stick_angle), 0.0f, std::sin(pi.stick_angle)};
        pi.attack_pressed = false;
        pact.request = false;                                      // +0x6f4 / +0x6f5 = 0
        nap_skip = true;
        if (std::getenv("CLERIA_DMG"))
            std::printf("  [extra] t%llu Adol: the 11th press, section 0x124 (lockout 60)\n", (unsigned long long)tick_count);
        return;
    }
    // (a press edge counts as held: the drive's `atk` is an edge without the held bit)
    if (const int s = extra::Napishtim::ignis_release(k, pi.attack_held || pi.attack_pressed, avm.section()); s >= 0) {
        avm.set_section(s);
        player_spec.a = player_spec.b = 0; player_spec.t = 0.0f; player_spec.step = 0.0f; player_spec.count = 0;
        nap_skip = true;
        if (std::getenv("CLERIA_DMG"))
            std::printf("  [extra] t%llu Adol: the button is up, section 0x%x\n", (unsigned long long)tick_count, s);
    }
};
// FUN_005902d0 [BYTES 0x5902d0..]: is there a water surface between 0.25 under the point and 10.0 over it.
// [approx] the room's water triangles only (a flooded room's mode 2 has none).
auto bolt_water = [&](const Vec3& p) {
    const RoomMesh& pm = phys_mesh(room);
    return pm.has_water() && water_above(pm, p.x, p.y - 0.25f, p.z, 10.25f) > kNoFloor;
};
// The creator's scan of the bosses' parts (view_extra_parts.inl sets it: the bosses' hosts come later).
std::function<void(const Vec3&, std::vector<extra::Chain::Target>&)> chain_parts;
// anime_Special of the player's script (FUN_00449ed0 [BYTES 0x449ed0..0x44a1c1]; only C_990.ASO calls it).
auto extra_special = [&](const AsoVm::SpecialEvent& ev) {
    const float h = std::atan2(face_dir.z, face_dir.x);            // +0x308 - the camera yaw
    const Vec3 org{feet.x, feet.y + kOriginH, feet.z};             // +0x50
    const bool log = std::getenv("CLERIA_DMG") != nullptr;
    if (ev.a[0] == 1) {                                            // the kind-0x17 runner: the Ignis sword's fire
        xrunners.push_back(extra::special_runner(ev.a, org, h, float(player_stats().atk)));
        const extra::Runner& r = xrunners.back();
        if (log)
            std::printf("  [extra] t%llu runner: effect %d C %d P %d, power %.1f scale %.2f step %.2f (section 0x%x)\n",
                        (unsigned long long)tick_count, r.effect, ev.a[4], r.p, r.power, r.scale, r.step, avm.section());
    } else if (ev.a[0] == 2) {                                     // the kind-0x18 chain lightning: the Terra skill
        // The handler takes +0x308 as stored for a world heading here (case 1 subtracts the camera yaw) and
        // writes a world atan2 back into it [BYTES 0x44a015, 0x44a071, 0x44a0d4]: kept as the code has it.
        const float h308 = h + camera_yaw();
        float power;
        std::memcpy(&power, &ev.a[1], 4);
        const float ahead = ev.a[4] != 0 ? 1.5f : 0.0f;            // 0x6bd4d4
        const Vec3 at{org.x + std::cos(h308) * ahead, org.y, org.z + std::sin(h308) * ahead};
        std::vector<extra::Chain::Target> found;
        const Vec3 ctr = extra::Chain::centre(at, h308);
        if (ev.a[4] == 0) {                                        // FUN_00591f60's actor scan [BYTES 0x5920cd..0x5921d7]
            for (const Actor& en : world.actors()) {
                if (en.is_player() || en.destroy_requested() || !(en.kind & 0x18u) || !en.ai.hittable || en.hp < 1.0f) continue;
                const Vec3 p{en.pos.x, en.pos.y + en.height, en.pos.z};
                if (extra::Chain::in_range(ctr, p)) found.push_back({en.instance_id, p});
            }
        }
        const std::size_t actors_found = found.size();
        if (chain_parts) chain_parts(ctr, found);                  // ... then the bosses' parts [BYTES 0x5921d7..0x5923d0]
        extra::Chain c = extra::Chain::make(at, h308, power, ev.a[3], ev.a[5], found, float(player_stats().atk),
                                            next_hit_serial(world.hit_serial_counter_));
        auto rnd = [&] { return pick_rng.next(); };
        if (!c.ray) {                                              // FUN_00591ef0 -> +0x308: he turns to the first hop
            const float nh = c.first_heading() - camera_yaw();
            face_dir = {std::cos(nh), 0.0f, std::sin(nh)};
            // The two driven lines [BYTES 0x5928c5..0x592ab4]: 25 nodes toward a random point 12.0 away and 1.0 up
            // (they start hidden), widths 0.75 -> 0.15, the uncharged skill's (section 0x10c) 0.25 -> 0.05.
            for (extra::Bolt& b : c.bolt) {
                const int v = int(pick_rng.next() >> 24);
                extra::Bolt::Desc d;
                d.a = at;
                d.b = {at.x + cos_table(v) * 12.0f, at.y + 1.0f, at.z + cos_table((v - 0x40) & 0xff) * 12.0f};
                d.w0 = 0.75f; d.w1 = 0.15f;
                if (avm.section() == 0x10c) { d.w0 = 0.25f; d.w1 = 0.05f; }
                d.segs = 25; d.life = 30; d.reach = 1.0f; d.fork_segs = 7; d.driven = true;
                b = extra::Bolt::make(d, rnd);
            }
            c.bolted = true;
        } else {
            // The ray's two free lines [BYTES 0x5923ea..0x5926cb]: 12 nodes to 6.0 ahead (+- 0.5, y +- 0.2), 30
            // updates, widths 0.25 -> 0.05, one or two side lines of 5 nodes. Draws: x, z, y, the count.
            const bool w = bolt_water(at);
            for (int k = 0; k < 2; ++k) {
                extra::Bolt::Desc d;
                d.a = at;
                const float jx = extra::Bolt::u01(pick_rng.next()) - 0.5f, jz = extra::Bolt::u01(pick_rng.next()) - 0.5f;
                const float jy = extra::Bolt::u01(pick_rng.next()) * 0.4f - 0.2f;
                d.b = {at.x + (std::cos(h308) * 6.0f + jx), at.y + jy, at.z + (std::sin(h308) * 6.0f + jz)};
                d.w0 = 0.25f; d.w1 = 0.05f; d.segs = 12; d.life = 30;
                d.forks = ((pick_rng.next() >> 17) & 0x10u) ? 2 : 1;
                d.reach = 0.75f; d.fork_segs = 5; d.water = w;
                xbolts.push_back(extra::Bolt::make(d, rnd));
            }
        }
        if (log) {
            std::printf("  [extra] t%llu chain: power %.1f, %d hop(s) over %zu monster(s), delay %d%s (section 0x%x):",
                        (unsigned long long)tick_count, c.power, c.n, actors_found, c.delay, c.ray ? ", the ray" : "", avm.section());
            if (found.size() != actors_found) std::printf(" [%zu boss part(s)]", found.size() - actors_found);
            for (const extra::Chain::Hop& hp : c.hops)
                if (hp.id >= (1 << 20)) std::printf(" boss%02d.%d", (hp.id - (1 << 20)) / 100, (hp.id - (1 << 20)) % 100);
                else std::printf(" #%d", hp.id);
            std::puts("");
        }
        xchains.push_back(c);
    } else if (ev.a[0] == 3) {
        // The kind-0x19 follower (GOXX_17_19.md §4, water_rings.hpp): surface rings and a wake every 8th tick while
        // he is in water (FUN_005914b0, view_water_rings.inl); it hits nothing. a1 is not read.
        water::Follower f;
        f.pos = org;
        xfollowers.push_back(f);
        if (log) std::printf("  [extra] t%llu water rings: a kind-0x19 follower (%zu live)\n", (unsigned long long)tick_count,
                             xfollowers.size());
    }
};
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
    if (character == 0) {                                          // Napishtim Adol's Terra block [BYTES 0x466cf4..0x466e02]
        const extra::Napishtim::Tint t = nap.frame(skillmp::bracelet_slot(inv.magic), sec);
        if (t.op != 0) {                                           // FUN_00445b30(colour, colour, 0), step 0.001
            player_spec.a = player_spec.b = t.colour; player_spec.t = 0.0f; player_spec.step = 0.001f; player_spec.count = 0;
        }
        return;
    }
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
    // The water probe (set 1): a vertical segment, as water_down (view_water_rings.inl) reads it.
    auto wray = [&](const Vec3& o, const Vec3& d, Vec3* hit) {
        float y = 0.0f;
        if (d.y >= 0.0f || !water_down(o, -d.y, &y)) return false;
        *hit = {o.x, y, o.z};
        return true;
    };
    for (extra::Runner& r : xrunners) {
        Vec3 at{}, ring{};
        const bool pulse = r.tick(ray, wray, &at, &ring);
        if (r.ring_due) {                                          // FUN_00454f40 / FUN_00454ff0 (GOXX_17_19.md §2)
            wrings.spawn_ring(ring, 0, r.scale * 0.66667f);        // 0x6bd360
            wrings.spawn_wake(ring, 0, r.scale * 1.75f, r.heading);   // 0x6bd510
            ring_log("a kind-0x17 runner", ring);
        }
        if (!pulse) continue;
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
