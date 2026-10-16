// The water rings and wakes' host side (src/cleria/water_rings.hpp, build/effect_re/WATER_RINGS.md); included in
// render_window before view_extra.inl. The pool `wrings` ages once per sim tick after the effects (view_game.inl)
// and is drawn after the room's water surfaces (draw_water_rings, view_sprites.inl).
//
//   water_down()             the water ray down a vertical segment (FUN_0041e420 / FUN_0041f130, set 1)
//   water_followers_tick()   the kind-0x19 followers (FUN_005914b0), once per sim tick with the runners
//   player_wade_step()       the player's wading rings (FUN_0045c490), after his water test
//   enemy_wade_step(a)       an enemy's, after its water test (enemy_surfaces)
//
// RNG: the followers and the wading draw from the shared xorshift (pick_rng) — FUN_0045e390's two words a ring,
// the follower's third for the wake's heading. Not drawn before 2026-10-16: every drive case that wades (any actor,
// the player included) or casts Napishtim Adol's water special in water changed its course (WATER_RINGS.md §6).

// [approx] a vertical segment only, against the room's water triangles: the lowest surface within it (the ray from
// above takes the highest; the rooms have one surface over a point). A flooded room (mode 2) has none.
auto water_down = [&](const Vec3& from, float len, float* y) {
    const RoomMesh& pm = phys_mesh(room);
    if (!pm.has_water()) return false;
    const float s = water_above(pm, from.x, from.y - len, from.z, len);
    if (s <= kNoFloor) return false;
    *y = s;
    return true;
};
// One pair for the drive logs.
auto ring_log = [&](const char* who, const Vec3& at) {
    if (std::getenv("CLERIA_DMG"))
        std::printf("  [rings] t%llu %s: a ring at (%.2f, %.2f, %.2f), %zu live\n", (unsigned long long)tick_count, who,
                    at.x, at.y, at.z, wrings.size());
};
// The kind-0x19 followers [GOXX_17_19.md §4]: the owner is the player (only C_990.ASO calls anime_Special), his
// in-water byte +0x38a = `submerged`, his origin +0x50 = the feet + kOriginH.
auto water_followers_tick = [&] {
    const Vec3 org{feet.x, feet.y + kOriginH, feet.z};
    auto surface = [&](const Vec3& p, float* y) { return water_down({p.x, p.y + 10.0f, p.z}, 10.0f, y); };
    auto rnd = [&] { return pick_rng.next(); };
    for (water::Follower& f : xfollowers) {
        Vec3 at{};
        if (f.tick(org, submerged, surface, rnd, wrings, &at)) ring_log("kind 0x19", at);
    }
    std::erase_if(xfollowers, [](const water::Follower& f) { return f.dead; });
};
// The player [BYTES 0x45cbb1..0x45cec6]: the counter +0x39b, the move since the last tick (+0x3d4 = the origin
// before this tick's move: here the feet after the last one), the surface +0x320, the floor bytes.
// [not modelled] the attached ripple effect 0x47 (0x50 lava / blood, 0x78 sand2) a moving player gets when +0x4c4
// is 0 (then 25; its countdown was not read), the cull byte +0x250 and +0x12c & 0x400.
water::Wade player_wade;
Vec3 player_wade_prev{};
bool player_wade_seen = false;
auto player_wade_step = [&] {
    water::WadeIn in;
    in.player = true;
    in.alive = player.hp >= 1;
    in.wading = wading; in.submerged = submerged;
    in.sand2 = psand.on_sand2();
    in.lava = cur_surface == 0x0d || cur_surface == 0x11;
    in.pos = feet; in.prev = player_wade_seen ? player_wade_prev : feet;
    in.surface = psand.water_y;
    const std::size_t n0 = wrings.size();
    water::wade_step(player_wade, in, [&] { return pick_rng.next(); }, wrings);
    if (wrings.size() != n0) ring_log("the player wades", wrings.live()[n0].pos);
    player_wade_prev = feet; player_wade_seen = true;
};
// An enemy: its water test runs before its move here (view_actor_step.inl), so `moved` is the last tick's move
// [approx: a tick late]. Its wading byte +0x389 and +0x12c & 0x400 are kept; [approx] no cull byte.
auto enemy_wade_step = [&](Actor& a) {
    water::WadeIn in;
    in.alive = a.hp >= 1.0f;
    in.wading = a.ai.wading; in.submerged = a.ai.in_water;
    in.hidden = (a.flags12c & 0x400u) != 0;
    in.sand2 = a.ai.surf_ray == 0x12;
    in.lava = a.ai.surf == 0x0d || a.ai.surf == 0x11;
    in.pos = a.pos; in.prev = a.ai.wade_seen ? a.ai.wade_prev : a.pos;
    in.surface = a.ai.water_y;
    water::Wade w{a.ai.wade_ticks};
    const std::size_t n0 = wrings.size();
    water::wade_step(w, in, [&] { return pick_rng.next(); }, wrings);
    a.ai.wade_ticks = w.ticks;
    if (wrings.size() != n0) ring_log("an enemy wades", wrings.live()[n0].pos);
    a.ai.wade_prev = a.pos; a.ai.wade_seen = true;
};
