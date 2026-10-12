// The Extra versions of Yunica, Hugo and Toal (Type Select's second answer: 0x74c0dc / 0x74c0d8 / 0x74c0d4),
// build/mode_re/EXTRA_VERSIONS.md. An Extra version is the normal character plus code branches in the player
// tick FUN_00465170 (and, for Hugo, in his eyes' code): no asset, stat or cost changes. This is the pure part:
// the per-tick counters (player +0x814 / +0x818 / +0x81c) and what they ask the host to spawn.
#pragma once
#include <cmath>
#include <cstdint>

#include "cleria/mathx.hpp"

namespace cleria::extra {

// Player +0x814 / +0x818 / +0x81c: ticks spent in the Ventus / Terra / Ignis skill sections.
struct Counters {
    std::uint32_t c814 = 0, c818 = 0, c81c = 0;
};

// FUN_005916e0's descriptor as the two Terra branches fill it (EXTRA_VERSIONS.md §2.2, §4.3, §5): a kind-0x17
// pulse emitter `ahead` in front of the player. The other fields are the same for every caller here:
// step +0x0c = 0.3, power +0x14 = 0, scale +0x1c = 1.0, +0x24 = a fresh hit token | 0x1000000.
struct Emitter {
    bool on = false;
    float ahead = 0;          // the offset along the facing
    int c = 0;                // D+0x28: C, the life factor (life = C * P + 1)
    int effect = 0;           // D+0x2c
    int p = 0;                // D+0x30: P, the pulse reload (a pulse every P + 1 ticks)
};

// ---- Yunica [BYTES 0x466e0a..0x46737b] ----
struct YunicaOut {
    // Ventus (0x10d / 0x110), the section's 12th tick: `bullets` kind-6 bullets of effect 0x66 from her origin,
    // bullet i heading facing + a0 + i * step; f0 = speed, f1 = accel, f4 = 1.0, f9 = f168, f10 = 1.9.
    int bullets = 0;
    float a0 = 0, step = 0, speed = 0, accel = 0, f168 = 0;
    // Terra (0x10c / 0x10f), tick 24 / 30.
    Emitter em;
};
inline YunicaOut yunica_tick(Counters& c, int sec) {
    YunicaOut o;
    if (sec == 0x10d || sec == 0x110) {
        if (++c.c814 == 12) {
            const bool ch = sec == 0x110;
            o.bullets = ch ? 4 : 3;
            o.a0 = ch ? 0.7853982f : 1.0471976f;         // 0x6bd3a0 / 0x6bd454
            o.step = ch ? 1.5707964f : 2.0943952f;       // 0x6bd4e4 / 0x6bd5a8
            o.speed = ch ? 0.2f : 0.12f;                 // 0x6bd210 / 0x6bd1b0
            o.accel = ch ? 0.004f : 0.002f;              // 0x6bd03c / 0x6bd010
            o.f168 = ch ? 5.0f : 6.0f;                   // 0x6bd868 / 0x6bd880
        }
    } else c.c814 = 0;
    if (sec == 0x10c || sec == 0x10f) {
        const bool ch = sec == 0x10f;
        if (++c.c818 == (ch ? 30u : 24u)) {
            o.em.on = true;
            o.em.ahead = ch ? 3.5f : 1.5f;               // 0x6bd720 / 0x6bd4d4
            o.em.c = 6; o.em.effect = 0x76; o.em.p = ch ? 5 : 3;
        }
    } else c.c818 = 0;
    return o;
}

// ---- Toal [BYTES 0x4673d4..0x467aff]; demon = flag 141 (0x76bb50) != 0 ----
struct ToalOut {
    // FUN_00450410(id, at his origin, rot (0, camera yaw, 0), scale s; side 0, power 0, knock 1, rtype 0,
    // status 0, cls -1): Ventus's kick effect (human form only) and Ignis's repeated fire.
    int fx[2] = {0, 0};
    float fx_scale[2] = {1.0f, 1.0f};
    // Terra, tick 18.
    Emitter em;
    // Terra, from tick 12 every 3rd / 2nd tick: a kind-2 bullet of effect 0xa7 from 2.0 ahead, each axis off by
    // rnd(-r, r), a random direction, f1 = rnd(0.0001, 0.0002), f0 = rnd(0.01, 0.015), f4 = 1.0.
    bool bullet = false;
    float r = 0;
};
inline ToalOut toal_tick(Counters& c, int sec, bool demon) {
    ToalOut o;
    if (sec == 0x10d || sec == 0x110) {
        if (!demon && c.c814 % 9u == 0 && c.c814 < (sec == 0x110 ? 18u : 9u)) o.fx[0] = 0xae;
        ++c.c814;
    } else c.c814 = 0;
    if (sec == 0x10e || sec == 0x111) {
        if (c.c81c % (demon ? 10u : 12u) == 0) {
            o.fx[1] = sec == 0x111 ? (demon ? 0xb3 : 0xb4) : (demon ? 0xaf : 0xa2);
            o.fx_scale[1] = demon ? 1.2f : 1.1f;         // 0x6bd484 / 0x6bd464
        }
        ++c.c81c;
    } else c.c81c = 0;
    if (sec == 0x10c || sec == 0x10f) {
        const bool ch = sec == 0x10f;
        ++c.c818;
        if (c.c818 == 18) {
            o.em.on = true;
            o.em.ahead = 0.5f;                           // 0x6bd304
            o.em.c = (ch ? 5 : 4) + (demon ? 1 : 0); o.em.effect = 0xa7; o.em.p = demon ? 5 : 4;
        }
        if (c.c818 >= 12 && c.c818 % (ch ? 2u : 3u) == 0) {
            o.bullet = true;
            o.r = ch ? 2.5f : 1.5f;                      // 0x6bd624 / 0x6bd4d4
        }
    } else c.c818 = 0;
    return o;
}

// ---- Hugo ----
// The Force Shield's life +0x7bc at the cast [BYTES 0x46f4c8..0x46f53e] (Boost's 480 overrides both versions).
constexpr int hugo_shield_life(bool charged, bool extra) {
    return charged ? (extra ? 0x1b8 : 0x17c) : (extra ? 0x1a4 : 0x168);
}
// FUN_00593b90 [BYTES 0x593dbd..0x593dda]: every bullet of effect 0x82 flies x 1.35 under Hugo Extra.
constexpr float kHugoShotSpeed = 1.35f;                  // 0x6bd4ac
// The player tick [BYTES 0x46737b..0x4673ce]: flag 157 (the eyes' mode source) from the bracelet slot
// (FUN_0046d560: 0 Ventus, 1 Terra, 2 Ignis, 3 none): the V, the rope, the wheel; none leaves it.
constexpr int hugo_flag157(int slot, int cur) {
    return slot == 0 ? 0 : slot == 1 ? 2 : slot == 2 ? 1 : cur;
}

// ---- The kind-0x17 GoXx (FUN_005916e0 / FUN_00591850; EXTRA_VERSIONS.md §5, GOXX_17_19.md §1-§2) ----
// A ground runner: it moves `step` a tick along a fixed heading on the floor and, every P + 1 ticks, the host
// spawns one EF2 (FUN_00450410: rot (0, -pi/2 - heading, 0), scale s, power, knock 0, rtype 0, status 0, cls 1)
// at its position. It hits nothing itself. Also Napishtim Adol's Ignis (anime_Special a0 = 1).
// [not modelled] the water probe (life -= 2 a tick in water, vy >= -0.125, the ring and the wake), the floor
// decals (2 RNG draws each), the hit token it writes to each effect, the 0x74c0f0 == 0 move-only state.
struct Runner {
    Vec3 pos{}, last{};
    float heading = 0, cs = 1, sn = 0;        // +0x130, +0x158, +0x160
    float step = 0, power = 0, scale = 1;     // +0x12c, +0xfc, +0x13c
    float vy = 0;                             // +0x15c
    int effect = 0, p = 0;                    // +0x14c, +0x150
    int life = 0, cnt = 0;                    // +0x154, +0x168
    float atk = 0;                            // the owner's stat block at creation (+0x94..)
    bool dead = false;

    // FUN_005916e0 [BYTES 0x591720..0x591847]: `at` = the descriptor's position.
    static Runner make(const Vec3& at, float heading, float step, float power, float scale, int c, int p, int effect,
                       float atk) {
        Runner r;
        r.heading = heading; r.cs = std::cos(heading); r.sn = std::sin(heading);
        r.step = step; r.power = power; r.scale = scale; r.effect = effect; r.p = p; r.atk = atk;
        r.life = c * p + 1;
        const float k = float(unsigned(p) >> 1) * step;
        r.pos = {at.x + r.cs * k, at.y - 0.75f, at.z + r.sn * k};         // 0x6bd390
        r.last = r.pos;
        return r;
    }
    // One sim tick [BYTES 0x591890.., pulse 0x591baa..0x591d96, move 0x591d9f..0x591de4]. `ray(from, d, &hit)`:
    // the terrain ray (FUN_0041e420 set 2) over the segment from..from + d. Returns true when it pulses: the
    // host spawns the effect at `*at` (the position before this tick's move).
    template <class Ray>
    bool tick(Ray&& ray, Vec3* at) {
        if (dead) return false;
        if (--life <= 0) { dead = true; return false; }
        Vec3 hit{};
        if (ray(Vec3{pos.x, pos.y + 1.1f, pos.z}, Vec3{0.0f, -2.6f, 0.0f}, &hit)) {   // 0x6bd464; 0xc0266666
            if (hit.y - pos.y > 0.0f) pos.y = hit.y;                                  // snaps up only
            vy = 0.0f;
        } else vy = -0.75f;
        bool pulse = false;
        if (cnt != 0) --cnt;
        else {
            cnt = p;
            pulse = true; *at = pos;
            const Vec3 d{pos.x - last.x, pos.y - last.y, pos.z - last.z};
            if (ray(Vec3{last.x, last.y + 0.75f, last.z}, d, &hit)) { dead = true; return true; }   // a wall: this pulse, then gone
            last = pos;
        }
        pos = {pos.x + cs * step, pos.y + vy * step, pos.z + sn * step};
        return pulse;
    }
};

}  // namespace cleria::extra
