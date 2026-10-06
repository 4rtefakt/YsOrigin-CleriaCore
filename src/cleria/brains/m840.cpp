// M840 — Shion, a class-4 duel opponent (Toal's S_3080 duel, `Battle2`). Handler FUN_004df410,
// brain FUN_004dce30 (AI index 77). Spec: build/ai_spec/M840.md.
//
// The handler runs the brain only while g_flags[130] ("boss AI live") is set; otherwise Shion
// is a script puppet (FUN_0045f250: no AI, no action player) whose HP never drops below 1.
// g_flags[131] freezes the AI the same way. The gauge is pushed on every HP change with
// lenScale 5000.
// When his HP falls below 1 with the player alive the handler (m840_dead) restores HP to 1,
// writes 174 = 1 and clears 156 and 130 (ending `Battle2`'s Wait_FlagEqualInt(156, 0)), hides
// the gauge, kills the +0x88c effect and every effect he owns with byte +0x1d5 >= 200, zeroes
// the hover bob (+0x840 / +0x844) and drops the flyer byte +0x398, so he falls.
// MOVESET (FUN_004dce30): a caster. The sub-state +0x72c plays one move and returns to +0x79c
// (always 0x14, "decide"); 0x14 picks +0x730 by HP% and distance: 0xa = a bullet volley
// (section 303 then EF2 0x134 shots: pct > 70 a one-by-one 3-fan, 40..70 homing 3-fans, <= 40
// 7/9/11-shot fans), 0x14 = glide to a clear point 8 u from the player, 0x1e = the strike
// (section 301, EF2 0x133 on the player's spot, EF2 0x1f2 there when it lands, d+6 times),
// 0x28 = the close cast (section 303 with EF2 0x1f1 / 0x130 on him, d+3 times, pct <= 40 only).
// Each pick is followed by a hold of +0x7c8 ticks. The power-up (sub 100, section 7) opens the
// fight and returns when its 240-tick rest (+0x7cc) and the 2140-tick charge (+0x7d0) are both
// over: Marker 1 lifts him (+0x7a0 flight) and lays an orb (EF2 0x131), Marker 2 sets DEF 999
// and a 4-hit barrier (+0x7a4) that burns on contact (a 1.25 sphere hit), adds an orb every 530
// ticks and sparks (EF2 0x15a) on each hit; the 4th hit breaks it (EF2 0x15a, power 10) and
// starts the rest; else DEF comes back when the charge drops under 20 and he lands.
// [not modelled] the keep-out box restore (FUN_0042dc90); FUN_004202e0 at INIT; +0x64 / +0x4be /
// +0x3c0 writes; the barrier hit's tint block (+0x264..+0x274); the orb disown at Marker 1
// (+0x1d5 = 0, +0x1a8 = 0 on every owned orb: AiEffect cannot disown, so a stale orb that is
// still alive keeps its tag); the copied rotation of the orb re-placement (only the position is
// re-placed); the target's +0x31c (AiEnv has +0x318 only: target_floor stands in [H]).
#include <cmath>
#include <cstdint>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {
namespace {

constexpr float kGone = -7777777.0f;              // DAT_006be15c
constexpr float kPi = 3.141590118408203f;         // 0x40490fd0 (DAT_006bd6a8)

// Effect tags. The orbs (EF2 0x131) carry byte +0x1d5 = 200 + k with k = +0x7a8 at their spawn
// (the Marker-1 orb k = 0, then one per 530 barrier ticks while the charge is > 80: k <= 3
// [BYTES 0x4defd1..0x4df0e0]): tag 1 + k. They are the ones the defeat sweeps (+0x1d5 >= 200 &&
// +0x1a8 == A). The close cast's EF2 0x1f1 (+0x1d5 = 1, +0x7d8) is tag 15, outside the sweep.
// Tag 0 (the skeleton's "+0x88c effect") is unused: here +0x88c is the latest orb, in the sweep.
constexpr int kTagCast = 15;
constexpr std::uint32_t kOwnedTags = 0x1eu;       // tags 1..4
int orb_tag(int k) { return 1 + k; }

int gflag(const AiEnv& e, int i, int unset) { return e.world ? e.world->flag(i, unset) : unset; }

// The byte views of the packed dwords (+0x7bc / +0x7c0 the volley record, +0x8a4 / +0x8a5 latches).
int byte_at(const EnemyAi& a, int off) {
    return int((std::uint32_t(a.i(off & ~3)) >> ((off & 3) * 8)) & 0xffu);
}
void set_byte(EnemyAi& a, int off, int v) {
    const int sh = (off & 3) * 8;
    std::uint32_t w = std::uint32_t(a.i(off & ~3));
    w = (w & ~(0xffu << sh)) | ((std::uint32_t(v) & 0xffu) << sh);
    a.i(off & ~3) = std::int32_t(w);
}
std::int32_t pack4(int b0, int b1, int b2, int b3) {
    return std::int32_t((std::uint32_t(b3 & 0xff) << 24) | (std::uint32_t(b2 & 0xff) << 16) |
                        (std::uint32_t(b1 & 0xff) << 8) | std::uint32_t(b0 & 0xff));
}

// FUN_00450410 with rot 0, the given uniform scale, power, knock 1.0, rtype 0, status 0.
AiEffect ef2(int id, const Vec3& pos, float scale, float power) {
    AiEffect f{id, pos, scale, false};
    f.power = power; f.knock = 1.0f;
    return f;
}

AiOut still(EnemyAi& a) {                          // puppet / frozen: see m830.cpp
    AiOut out;
    out.skip_action_player = true;
    a.mode() = 0; a.speed = 0;
    return out;
}

// FUN_004dce30.
void body(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e, Actor& A, AiOut& out) {
    using ai::r15;
    const int d = difficulty(e);
    const int pct = int(std::floor(A.hp * 100.0f / A.max_hp));     // FUN_004217f0 + cvttss2si
    a.set_v(0x808, e.self);                                         // [not modelled] the keep-out restore
    if (e.launched) return;
    const float dist = std::sqrt(a.f(0x734) * a.f(0x734) + 0.0f + a.f(0x73c) * a.f(0x73c));
    switch (a.phase()) {
        case 0: {                                                   // ---- INIT (0x4dd043)
            ai::init(a, p, e);                                      // FUN_004782f0 + EI[74] +0x90..
            // [not modelled] FUN_004202e0(level): the spawn already computed this cast's stats.
            a.i(0x70c) = 0;
            a.i(0x728) = 0;
            a.set_v(0x808, e.self);
            a.i(0x72c) = 0; a.i(0x730) = 0;
            a.f(0x880) = 0.0f;
            a.phase() = 4;
            a.i(0x88c) = 0;
            a.f(0x7ac) = A.def;                                     // DEF
            if (d < 0) { A.atk -= 2.0f; A.def -= 2.0f; }            // DAT_006bd580
            A.flags130 |= 1u;
            A.kind = 0x88; a.hittable = true;                       // +0x60 = 0x88; [not modelled] +0x64 = 0x2f, +0x4be
            A.flags12c = 0x1090000u;                                // a store, not an or
            return;
        }
        case 1: ai::acquire(a); [[fallthrough]];
        case 2: a.phase() = 4; [[fallthrough]];
        case 4: break;
        default: return;
    }
    if (e.took_damage) a.i(0x7c4) += 1;                             // +0x3bc & 2

    // ---- MAIN (0x4dd1a9): switch (+0x72c), byte table 0x4df37c, dword table 0x4df32c
    int S = e.section;
    auto req = [&](int id) { if (ai::request(S, id, false, e)) { out.section = id; out.section_force = false; } };
    auto force = [&](int id) { S = id; out.section = id; out.section_force = true; };
    auto R100 = [&] { return int(r15(e) % 100u); };
    const float pw = d < 0 ? 0.0f : 2.0f;                           // the effects' power (DAT_006bd580)
    const float t31c = e.target_floor;                              // [H] T+0x31c, see the header
    int& sub = a.i(0x72c);
    int& seq = a.i(0x730);
    auto ray_hit = [&](const Vec3& o, const Vec3& dl) { return e.world ? e.world->ray(o, dl).hit : false; };

    // 0x730 == 0: decide (0x4dd5b3).
    auto decide = [&] {
        const float V = a.f(0x7d0), C = a.f(0x7cc);
        if (std::uint32_t(a.i(0x7c4)) >= std::uint32_t(d < 0 ? 10 : 8) && (V == 0.0f || !(2140.0f >= V))) {
            if (5.0f > dist) {                                      // hit too often up close: 301 away
                force(0x12d);
                seq = 0x14;
                if (a.i(0x7a0) != 2) a.i(0x7a0) = 1;
            } else {
                a.i(0x7c4) = 0;
                sub = 100;
            }
            set_byte(a, 0x8a4, 0);
            a.i(0x79c) = 0x14;
            return;
        }
        if ((C != 0.0f && V == 0.0f) || (V != 0.0f && !(2140.0f >= V))) {   // resting (0x4dd9ec)
            a.action() = 2; a.steer() = 0;
            set_byte(a, 0x8a4, 0);
            return;
        }
        if (C == 0.0f && V == 0.0f) {                               // rested, uncharged: power up
            sub = 100; a.i(0x79c) = 0x14;
            set_byte(a, 0x8a4, 0);
            return;
        }
        if (byte_at(a, 0x8a4) != 0) { seq = 0x3e7; return; }        // after a move: hold
        if (pct > 80) {
            if (7.0f > dist && R100() < 50) seq = 0x14;
            if (seq == 0) {
                if (R100() < 70) { seq = 0xa; if (dist > 20.0f) seq = R100() < 50 ? 0x14 : 0x1e; }
                else seq = 0x1e;
            }
        } else if (pct > 60) {
            if (7.0f > dist && R100() < 60) seq = 0x14;
            if (seq == 0) {
                if (R100() < 60) { seq = 0xa; if (dist > 20.0f) seq = R100() < 50 ? 0x14 : 0x1e; }
                else seq = 0x1e;
            }
        } else if (pct > 40) {
            if (7.0f > dist && R100() < 70) seq = 0x14;
            if (seq == 0) {
                seq = R100() < 50 ? 0xa : 0x1e;
                if (dist > 20.0f && R100() < 40) seq = 0x14;
            }
        } else {
            if (7.0f > dist && R100() < 75) seq = 0x14;
            if (R100() < 60) {
                seq = 0x14;
            } else if (seq == 0) {
                const int r = R100();
                seq = r < 50 ? 0xa : (r < 75 ? 0x1e : 0x28);
                if (dist > 20.0f && R100() < 30) seq = 0x14;
            }
        }
        set_byte(a, 0x8a4, 1);
        a.i(0x7c8) = seq == 0x14 ? 30 - 4 * d : 120 - 10 * d;       // the hold after the move
    };

    // 0x730 == 0xa: load the volley record (0x4dd483). +0x7bc = [type, interval, shots, volleys
    // left]; +0x7c0 = [speed flag, accel flag, burst index, tick toggle].
    auto load_volley = [&] {
        int type, every, n = 3, more, c0 = 1, c1 = 0;
        if (pct > 70) {                                             // one by one, 3 to a burst
            type = 3; c0 = 0; c1 = 1;
            if (d < 0) { every = 20; more = 4; } else if (d < 2) { every = 10; more = 8; } else { every = 5; more = 16; }
        } else if (pct > 40) {                                      // homing 3-fans
            type = 2;
            if (d < 0) { every = 30; more = 4; c0 = 0; c1 = 1; } else if (d < 2) { every = 30; more = 6; } else { every = 15; more = 12; }
        } else {                                                    // wide fans
            type = 0;
            if (d < 0) { every = 40; n = 7; more = 6; } else if (d < 2) { every = 30; n = 9; more = 10; } else { every = 25; n = 11; more = 14; }
        }
        seq = 0;
        a.i(0x7bc) = pack4(type, every, n, more);
        a.i(0x7c0) = pack4(c0, c1, 0, 0);
        sub = 0x6e; a.i(0x79c) = 0x14;
    };

    // 0x730 == 0x14: pick a clear point 8 u from the target, rotating from the side Shion is on
    // (0x4dd225).
    auto pick_spot = [&] {
        const float th = float(std::atan2(double(-a.f(0x73c)), double(-a.f(0x734))));   // FUN_0066f80a
        const int deg = int(th * 180.0f / kPi);
        const bool down = (r15(e) & 1u) != 0;
        for (int k = 0; k < 360; k += 30) {
            const int ang = down ? deg - k : deg + k;
            const float rad = float(ang) * kPi / 180.0f;
            const Vec3 off{float(std::cos(double(rad))) * 8.0f, 0.0f, float(std::sin(double(rad))) * 8.0f};
            if (ray_hit({e.target.x, e.target_floor + 0.5f, e.target.z}, off)) continue;
            const Vec3 dl{e.target.x + off.x - e.self.x, 0.0f, off.z + e.target.z - e.self.z};
            if (ray_hit({e.self.x, A.floor_y + 1.0f, e.self.z}, dl)) continue;
            const float L = std::sqrt(dl.x * dl.x + 0.0f * 0.0f + dl.z * dl.z);
            a.f(0x7b8) = L;
            a.f(0x7b0) = dl.x / L;
            a.f(0x7b4) = dl.z / L;
            sub = 0x78; a.i(0x79c) = 0x14;
            break;
        }
        seq = 0;
    };

    // sub 0x71: one volley step (0x4ddd6e).
    auto volley = [&] {
        const std::uint32_t pk = std::uint32_t(a.i(0x7bc)), pc = std::uint32_t(a.i(0x7c0));
        const int type = int(pk & 0xffu), n = int((pk >> 16) & 0xffu), more = int(pk >> 24);
        int every = int((pk >> 8) & 0xffu);
        const int c0 = int(pc & 0xffu), c1 = int((pc >> 8) & 0xffu);
        int idx = int((pc >> 16) & 0xffu), tog = int(pc >> 24);
        const std::uint32_t t = std::uint32_t(a.i(0x87c));        // ticks since the last shot
        const bool burst = type == 3 || type == 4 || type == 5;
        if (!burst) {
            if (t < std::uint32_t(every)) return;
        } else {
            if (every < 3) every = 3;
            tog ^= 1;                                               // a shot every other tick
            if (tog & 1) { a.i(0x7c0) = pack4(c0, c1, idx, tog); return; }
            if (idx >= n && t < std::uint32_t(every)) return;
            if (!(t < std::uint32_t(every))) idx = 0;               // the next burst
        }
        a.i(0x87c) = 0;
        const bool wide = type == 2 || type == 5;
        const float sq = float(std::sqrt(double(float(n))));       // 0x670f00
        const float step = (wide ? 160.0f : 45.0f) / sq * kPi / 180.0f;
        int hp = 0;
        float hr = 0.0f;
        if (type == 1 || type == 4) { hr = 0.2617991864681244f; hp = 15; }   // 0x6bd258
        if (type == 2 || type == 5) { hr = 0.7853975296020508f; hp = 15; }   // 0x6bd39c
        const int kind = (type == 1 || type == 2 || type == 4 || type == 5) ? 4 : 1;
        const float base = d < 0 ? 0.55f : float(d) * 0.03f + 0.75f;
        const float speed = c0 == 0 ? 0.03f : base * 0.505f;
        const float accel = c1 == 0 ? 0.0f : base * 0.011f;
        int from = 0, to = n, nidx = idx;
        if (type == 3) {                                            // the next of the fan
            from = idx < n ? idx : 0;
            to = idx < n ? idx + 1 : 0;
            nidx = idx + 1;
        } else if (type == 4 || type == 5) {                        // one of the fan at random
            if (idx < n) { from = int(r15(e) % std::uint32_t(n)); to = from + 1; } else { from = 0; to = 0; }
            nidx = idx + 1;
        }
        if (from == 0) out.sounds.push_back(0x19e);
        for (int k = from; k < to; ++k) {
            float off;
            if (n & 1) {
                if (k == 0) off = 0.0f;
                else if (k & 1) off = float((k + 1) >> 1) * step;
                else off = -(float(k >> 1) * step);
            } else if (k & 1) {
                off = float((k + 1) >> 1) * step - step * 0.5f;
            } else {
                off = step * 0.5f - float((k + 2) >> 1) * step;
            }
            const float g = off + a.heading;
            const float s = float(std::sin(double(g))), c = float(std::cos(double(g)));
            AiBullet b;                                             // FUN_0040f700 + FUN_00583a00 + FUN_00593b90
            b.kind = kind; b.effect = 0x134;
            b.pos = {c * 0.85f + e.self.x, t31c + 0.7f + 0.0f, e.self.z + s * 0.85f};
            b.dir = {c, 0.0f, s};
            b.speed = speed; b.accel = accel; b.gravity = 0.0f;
            b.power = -1.0f; b.knock = 1.0f;
            b.home_period = hp; b.home_rate = hr;
            b.f168 = 15.0f; b.f16c = 0.0f; b.f170 = 31.0f; b.f174 = 0.0f;   // 0x6be250
            out.bullets.push_back(b);
        }
        int left = more;
        if (!burst || nidx >= n) {
            if (left != 0) {
                left -= 1;
            } else {                                                // the last volley
                const int w = int(r15(e) % 15u) + (3 - d) * 10;
                a.cool() = w;
                a.dur() = type == 0 ? w + 90 : w;
                sub += 1;
            }
        }
        a.i(0x7bc) = pack4(type, every, n, left);
        a.i(0x7c0) = pack4(c0, c1, burst ? nidx : idx, tog);
    };

    switch (sub) {
        case 0: seq = 0; sub = 0x14; break;
        case 0x14: {
            if (a.dur() != 0) break;
            const std::uint32_t v = std::uint32_t(seq);
            if (v > 0x1eu) {
                if (v == 0x28u) { sub = 0x8c; seq = 0; a.i(0x79c) = 0x14; }
                else if (v == 0x3e7u) {                             // the hold after a move
                    a.action() = 2; a.steer() = 0; a.dur() = a.i(0x7c8);
                    set_byte(a, 0x8a4, 0);
                    seq = 0;
                }
            } else if (v == 0x1eu) {
                sub = 0x82; seq = 0; a.i(0x79c) = 0x14;
            } else if (v == 0) {
                decide();
            } else if (v == 0xau) {
                load_volley();
            } else if (v == 0x14u) {
                pick_spot();
            }
            break;
        }
        case 100:                                                   // the power-up (section 7)
            force(7);
            a.action() = 2; a.steer() = 0; a.dur() = 0x54; a.cool() = 0x54;
            a.f(0x7d0) = 0.0f;
            sub += 1;
            set_byte(a, 0x8a5, 0);
            break;
        case 0x65: {
            const int c = a.cool();
            if (a.dur() == 0) { a.action() = 2; a.dur() = c; }
            if (c == 0) { sub = a.i(0x79c); break; }
            if (e.aso_marker == 1) {
                if (byte_at(a, 0x8a5) != 0) break;
                set_byte(a, 0x8a5, 1);
                if (a.i(0x7a0) != 2) a.i(0x7a0) = 1;                // take off
                // [not modelled] the disown of the owned orbs (+0x1d5 = 0, +0x1a8 = 0).
                AiEffect o = ef2(0x131, {e.self.x, e.self.y - 0.9f, e.self.z}, 1.0f, -2.0f);
                o.tag = orb_tag(0);                                 // +0x1a8 = A, +0x1d5 = 200
                out.effects.push_back(o);
                a.i(0x88c) = 0;
                out.sounds.push_back(0x19b);
            }
            if (e.aso_marker == 2 && byte_at(a, 0x8a5) != 0) {      // the barrier
                A.def = 999.0f;
                a.f(0x7d0) = 2140.0f;                               // the charge
                a.f(0x7d4) = 530.0f;                                // ticks to the next orb
                a.i(0x7a4) = 4;                                     // hits it takes
                set_byte(a, 0x8a5, 0);
            }
            break;
        }
        case 0x6e:                                                  // the volley (section 303)
            force(0x12f);
            a.action() = 2; a.steer() = 0; a.dur() = -1; a.cool() = 0;
            a.i(0x87c) = 9999;
            out.set_durable = 1;
            sub += 1;
            out.sounds.push_back(0x19d);
            break;
        case 0x6f:
            if (e.aso_marker == 1) { a.action() = 2; a.steer() = 0; a.dur() = 20; sub = 0x70; }
            break;
        case 0x70:
            if (a.dur() == 0) { a.action() = 2; a.steer() = 0; a.dur() = -1; sub = 0x71; }
            break;
        case 0x71: volley(); break;
        case 0x72:
            if (a.cool() != 0) break;
            out.set_durable = 0;
            sub = a.i(0x79c);
            req(0x130);
            break;
        case 0x78:                                                  // the glide
            a.action() = 2; a.steer() = 2; a.dur() = 30;            // [not modelled] +0x3c0 &= ~1
            sub += 1;
            break;
        case 0x79: {
            const float s0 = float(std::sin(double(dist / a.f(0x7b8) * kPi)));
            const float s = 0.01f > s0 ? 0.01f : s0;                // maxss
            A.mover.k1.x += a.f(0x7b0) * s;
            A.mover.k1.z += a.f(0x7b4) * s;
            if (dist >= 8.0f || a.dur() == 0) {
                a.dur() = 0;
                sub = a.i(0x79c);                                   // [not modelled] +0x3c0 |= 1
            }
            break;
        }
        case 0x82:                                                  // the strike (section 301)
            force(0x12d);
            a.action() = 2; a.steer() = 4; a.dur() = -1;
            a.i(0x7c0) = d + 6;                                     // strikes
            out.effects.push_back(ef2(0x174, {e.self.x + 0.0f, e.self.y + 1.0f, e.self.z + 0.0f}, 0.5f, pw));
            out.sounds.push_back(0x19f);
            sub += 1;
            [[fallthrough]];
        case 0x83: {
            if (e.aso_marker != 1) break;
            a.action() = 2; a.steer() = 4; a.dur() = 26 - d; a.cool() = 0;
            a.f(0x7b0) = e.target.x;
            a.f(0x7b4) = e.target.z;
            const Vec3 mark{e.target.x, e.target_floor + 0.05f, e.target.z};
            a.set_v(0x894, mark);                                   // steer 4 heads here
            out.effects.push_back(ef2(0x133, mark, 3.0f, pw));
            out.sounds.push_back(0x1a0);
            out.set_durable = 1;
            sub += 1;
            break;
        }
        case 0x84:
            if (a.dur() != 0) break;
            out.effects.push_back(ef2(0x1f2, {a.f(0x7b0), e.target_floor, a.f(0x7b4)}, 1.0f, pw));
            a.action() = 1;
            a.dur() = 20 - 5 * d;
            a.cool() = int(r15(e) % 15u) + 30 - 10 * d + a.dur();
            sub += 1;
            break;
        case 0x85:
            if (a.dur() == 0) {
                if (std::uint32_t(a.i(0x7c0)) > 1u) { a.i(0x7c0) -= 1; sub -= 2; break; }
                a.action() = 1; a.dur() = a.cool();
            }
            if (a.cool() != 0) break;
            req(0x12e);
            out.set_durable = 0;
            sub = a.i(0x79c);
            break;
        case 0x8c:                                                  // the close cast
            a.i(0x7c0) = d < 0 ? 3 : d + 3;
            sub += 1;
            [[fallthrough]];
        case 0x8d: {
            AiEffect c = ef2(0x1f1, e.self, 1.0f, pw);
            c.attach = true; c.tag = kTagCast;                      // +0x1a0 = A+0x20, +0x1d5 = 1, +0x1a8 = A
            out.effects.push_back(c);
            a.i(0x7d8) = kTagCast;                                  // [H] stands in for the effect pointer
            force(0x12f);
            a.action() = 2; a.steer() = 0;
            a.dur() = d < 0 ? 0x34 : 8 * d + 0x40;
            a.cool() = 0;
            out.set_durable = 1;
            // FUN_00450410 gets (x + cos h * 0.8, T+0x31c + 0.7, z + sin h * 0.8), but +0x1a0 = A+0x20
            // then draws it on Shion's matrix [H]: attached at offset 0.
            AiEffect s = ef2(0x130, e.self, 1.0f, 3.0f);
            s.attach = true;                                        // +0x1d5 = 1, not owned
            out.effects.push_back(s);
            out.sounds.push_back(0x1a1);
            sub += 1;
            break;
        }
        case 0x8e:
            if (a.dur() != 0) break;
            a.action() = 1;
            a.dur() = d < 0 ? 0x37 : 0x2f - 8 * d;
            a.cool() = int(r15(e) % 15u) + 30 - 10 * d + a.dur();
            if (a.i(0x7d8) != 0) {                                  // FUN_0047a830 + FUN_0047a7c0
                AiEffect k; k.kill_tag = kTagCast; out.effects.push_back(k);
            }
            sub += 1;
            break;
        case 0x8f:
            if (a.dur() == 0) {
                if (std::uint32_t(a.i(0x7c0)) > 1u) { a.i(0x7c0) -= 1; sub -= 2; break; }
                a.action() = 2; a.steer() = 0; a.dur() = a.cool();
            }
            if (a.cool() != 0) {
                const float V = a.f(0x7d0);
                if (V != 0.0f && 2140.0f >= V) break;               // charged: sit the cooldown out
            }
            req(0x130);
            out.set_durable = 0;
            sub = a.i(0x79c);
            break;
        default: break;
    }

    // ---- 0x4debdd: the flight (+0x7a0: 1 take off, 2 up, 3 landing)
    if (a.i(0x7a0) == 1) {
        a.f(0x834) = A.height + 0.4f;
        a.f(0x83c) = 0.015f; a.f(0x838) = 0.015f;                   // sink / rise
        a.f(0x840) = 0.15f; a.f(0x844) = 3.0f;                      // the bob
        A.flyer = true;
        a.i(0x7a0) = 2;
    } else if (a.i(0x7a0) == 3) {
        a.f(0x834) = A.height + 0.0001f;
        a.f(0x840) = 0.0f; a.f(0x844) = 0.0f;
        if (0.01f > std::fabs(A.height - e.self.y)) {               // FUN_0041f170
            A.flyer = false;
            a.i(0x7a0) = 0;
        }
    }
    // ---- 0x4dec9b: the charge and the barrier
    {
        const float V0 = a.f(0x7d0);
        if ((V0 != 0.0f && 2140.0f >= V0) || byte_at(a, 0x8a5) != 0) {
            const Vec3 orb_at{e.self.x, e.self.y - 0.9f, e.self.z};
            float V = V0 - 1.0f;
            a.f(0x7d0) = V;
            a.f(0x7cc) = 240.0f;                                    // the rest after it
            // The two newest owned orbs (+0x1d5 == +0x7a8 + 200 / + 199) [H: assumed alive].
            const int n = a.i(0x7a8);
            int f1 = orb_tag(n), f2 = n >= 1 ? orb_tag(n - 1) : 0;
            if (byte_at(a, 0x8a5) == 0) {
                if (1.0f > V) {                                     // the charge is over
                    a.f(0x7d0) = 0.0f; a.i(0x7a8) = 0;
                    f1 = f2 = 0;
                } else if (!(20.0f > V && A.def > 990.0f) && a.i(0x7a4) != 0) {
                    if (!(V < 20.0f)) {                             // 0x4deef7: the barrier is up
                        AiHitbox h;                                 // FUN_004494e0
                        h.shape = 0; h.radius = 1.25f; h.fwd = 0; h.angle_deg = 0; h.voff = 0; h.half_h = 0;
                        h.life = 2; h.rehit = 15; h.one_shot = 0; h.rtype = 2; h.status = 0;
                        h.power = -2.0f; h.knock = 1.0f;
                        out.hitboxes.push_back(h);
                        a.f(0x7d4) -= 1.0f;
                        if (1.0f > a.f(0x7d4)) {
                            if (V - 20.0f > 60.0f) {                // one more orb
                                const int k = ++a.i(0x7a8);
                                AiEffect o = ef2(0x131, orb_at, 1.0f, -2.0f);
                                o.tag = orb_tag(k);                 // +0x1d5 = +0x7a8 + 200
                                out.effects.push_back(o);
                                const int old = a.i(0x88c);
                                if (f2 == 0) f2 = o.tag;            // re-placed this tick: see the spec
                                else if (f1 != old) f1 = old;
                                else f2 = old;
                                a.i(0x88c) = o.tag;                 // [H] stands in for the effect pointer
                                a.f(0x7d4) = 530.0f;
                            } else {
                                a.f(0x7d4) = 0.0f;
                            }
                        }
                    }
                } else {                                            // 0x4deddd: ends
                    if (!(V < 20.0f) && a.i(0x7a4) == 0 && f1 != 0) {   // broken
                        AiEffect bk = ef2(0x15a, e.self, 1.33f, 10.0f);
                        bk.rot = {0.0f, a.heading, 0.0f};
                        out.effects.push_back(bk);
                        for (int t : {f1, f2 != f1 ? f2 : 0}) {
                            if (t == 0) continue;
                            AiEffect kill; kill.kill_tag = t; out.effects.push_back(kill);
                        }
                        out.sounds.push_back(0x19c);
                        f1 = f2 = 0;
                        a.i(0x7a8) = 0;
                        a.f(0x7d0) = 0.0f;
                        a.f(0x7cc) = 240.0f;
                    }
                    A.def = a.f(0x7ac);
                    a.i(0x7a4) = 0;
                }
                // 0x4df0f6
                if (20.0f > a.f(0x7d0) && a.i(0x7a0) != 0) a.i(0x7a0) = 3;   // land
                if (e.was_hit && a.i(0x7a4) != 0 && std::uint32_t(sub - 100) >= 10u) {
                    a.i(0x7a4) -= 1;                                // [not modelled] the tint +0x264..+0x274
                    AiEffect sp = ef2(0x15a, e.self, 1.0f, -2.0f);
                    sp.rot = {0.0f, a.heading, 0.0f};
                    out.effects.push_back(sp);
                }
            }
            // 0x4df1fc: the two newest orbs ride with him (his matrix, y - 0.9)
            for (int t : {f1, f2 != f1 ? f2 : 0}) {
                if (t == 0) continue;
                AiEffect mv; mv.move_tag = t; mv.pos = orb_at; out.effects.push_back(mv);
            }
        }
    }
    // ---- 0x4df27e: the rest counts down outside the moves
    if (std::uint32_t(sub) < 100u) a.f(0x7cc) = a.f(0x7cc) > 1.0f ? a.f(0x7cc) - 1.0f : 0.0f;

    // ---- the tail (0x4df2b4)
    if (a.phase() == 4) {
        ai::steer(a, e);
        ai::turn(a, a.f(0x814));                                    // FUN_00476900(+0x814, &+0x748)
        if (A.flyer) ai::hover(a, A, e, nullptr, a.i(0x720));
        ai::act(a, p, e, &out);
    }
}

}  // namespace

void m840_dead(Actor& A, AiWorld& w) {
    EnemyAi& a = A.ai;
    if (w.flag(130, 1) == 0) { A.hp = 1.0f; return; }               // the puppet path (0x4df41f)
    // The live path with HP < 1 (0x4df46e).
    if (Actor* P = w.player(); P && !P->alive()) {                  // the player lost: Shion stays up
        A.hp = 1.1f;
        a.hittable = false; A.def = 999.0f;                         // FUN_0047aa80(edx = 1)
        return;
    } else if (P) {
        P->i_frames = 9999;                                         // a store, not a grant
    }
    if (kGone > a.f(0x880)) return;                                 // already resolved
    a.f(0x880) = -9999999.0f;                                       // 0xcb18967f
    w.set_flag(174, 1);                                             // DAT_0076bbd4
    w.set_flag(156, 0);                                             // DAT_0076bb8c: the duel is over
    w.set_flag(130, 0);                                             // DAT_0076bb24: back to a puppet
    A.hp = 1.0f;
    if (a.i(0x88c) != 0) { a.hook_kill_tags |= 1u; a.i(0x88c) = 0; }   // +0x1d4 = 1
    w.hide_boss_gauge();                                            // DAT_006d5fd8 = 0
    a.action() = 2; a.steer() = 0;
    a.f(0x840) = 0.0f; a.f(0x844) = 0.0f;                           // the hover bob
    A.flyer = false;                                                // byte +0x398 = 0
    a.hook_kill_tags |= kOwnedTags;                                 // +0x1d5 >= 200 && +0x1a8 == A
    a.hit_cut = true;                                               // +0x118 = 0
}

AiOut m840(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    if (!e.actor) return {};
    Actor& A = *e.actor;
    // ---- the handler FUN_004df410
    if (gflag(e, 130, 1) == 0) {                                    // the script puppet
        if (A.hp < 1.0f) A.hp = 1.0f;
        return still(a);
    }
    // (the dead branch is m840_dead; the off-screen freeze is enemy_ai_tick's)
    AiOut out;
    if (gflag(e, 131, 0) != 0) {
        out = still(a);                                             // FUN_0045f470 only
    } else {
        body(a, p, e, A, out);
        if (!e.target_alive) { a.hittable = false; A.def = 999.0f; }   // FUN_0047aa80
    }
    // The gauge (0x4df81d): while not defeated, push on every HP change; lenScale 5000.
    if (!(kGone > a.f(0x880)) && a.f(0x880) != A.hp) {
        a.f(0x880) = A.hp;
        out.gauge_hp = A.hp; out.gauge_len = 5000.0f;               // 0x459c4000
    }
    return out;
}

}  // namespace cleria::brains
