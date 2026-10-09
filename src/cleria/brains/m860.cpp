// M860 — Kishgal, a class-4 duel opponent (Yunica's S_3080 duel, `Battle_0`). Handler
// FUN_004e7af0, brain FUN_004e3070 (AI index 79). Spec: build/ai_spec/M860.md.
//
// The handler runs the brain only while g_flags[130] ("boss AI live") is set; otherwise Kishgal
// is a script puppet (FUN_0045f250: no AI, no action player) whose HP never drops below 1 and
// who, in S_3080, kills every live EF2 0x122 each tick. g_flags[131] freezes the AI the same
// way. The gauge is pushed on every HP change with lenScale 7500 (8500 for Yunica with
// g_flags[179]).
// The story duel is room-gated: in `s_30\s_3080\` (FUN_004358b0 against the current room) the
// handler pins his HP at half its maximum once it falls below that and writes 156 = 0 and
// 174 = 1 (Battle_0's Wait_FlagEqualInt(156, 0) then clears 130 itself); elsewhere he fights to
// the end. When his HP falls below 1 with the player alive (m860_dead) he is set back to 1 HP,
// 174 = 1 and 156 = 130 = 0, the gauge is hidden and every EF2 0x122 / 0xab is killed; in
// S_3080 the half-HP rule then lifts that 1 HP to half. Outside S_3080 (and not as Hugo's foe)
// his INIT sets g_flags[179] and attaches the EF2 0xab aura.
//
// MOVESET (body()). The sub-state +0x72c plays one move and returns to +0x79c (0x14 = decide
// again, 0x1e = continue the scripted chain +0x730):
//   100  melee: section 7 / 0x132 / 0x12f by byte +0x7be      0x6e  section 0x133 (dash, trail)
//   0x78 section 8 then 0x136: byte +0x7be volleys of the EF2 0x11c bullet fan
//   0x82 section 0x130: arms a delayed rain of EF2 0x121 bullets on the player (+0x7a0)
//   0x8c section 0x12d then a ring of EF2 0x122 shields (bytes +0x7bd / +0x7be / +0x7bf)
//   0x96 section 0x131: a lunge (channel 2) toward the player, with a trail
//   0xa0 section 0x12e: one or two homing EF2 0x124 bullets (cooldown +0x7cc)
//   0xaa section 0x134 (g_flags[179] only, cooldown +0x7d0): pinned, DEF + 40, EF2 0x1b9, a shield
//        ring, then EF2 0x195 pillars along the heading and EF2 0x196
//   0xb4 guard in section 6 (the +0x7a4 record: hits, reset, counter-sub, ticks)
//   0xbe an EF2 0x122 shield at his feet, then a side-jump      0xc8 the jump-dodge (or wait out
//        section 4)
// The decide state picks by HP band (> 80 / > 50 / > 33 / else), distance (5.0) and facing;
// after K hits (4; Hugo 2, Toal 6) it guards (near) or chain-dodges (far).
// The effect-pool scan (0x4e33d8: the live EF2 0x122 count, the player effects that steer the
// decide, ids 0x82/0x65/0x84/0x14, 0x70/0x15, 0x90) reads AiWorld::for_each_effect.
// The shield break (a player skill effect from a 13-id list overlapping an EF2 0x122 kills it: SE 424,
// EF2 0x123) and the freeze (a live unmarked EF2 0x123 with the player in section 4 / 0xb: EF2 0x11e
// at +0x88c, the player pinned in x / z for 20 ticks in section 9) follow the pool scan
// (build/mech_re/KISHGAL_SHIELD.md §3, §4).
// [not modelled] the keep-out restore (FUN_0042dc90);
// +0x64 / +0x4be / +0x3c0 writes; the camera shakes / zoom blurs (0x76a5ac.., 0x76a550..,
// FUN_005e0590, FUN_005d3500); FUN_00451b50's rotation of the 0x121 bullet's effect.
#include <cmath>
#include <cstdint>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {
namespace {

constexpr float kGone = -7777777.0f;              // DAT_006be15c
constexpr const char* kRoom = "s_30\\s_3080\\";   // the string at 0x69be54
constexpr float kPi = 3.141590118408203f;         // 0x40490fd0 (the brain's pi)
constexpr float kHalfPi = 1.5707950592041016f;    // 0x3fc90fd0
constexpr float k120 = 2.095440626144409f;        // 0x40061bb3
constexpr float k60 = 1.046149492263794f;         // 0x3f85e83a
constexpr float k30 = 0.5235983729362488f;        // 0x3f060a8b

// The effects the handler kills by EF2 id (FUN_0047a7c0 over the pool; Kishgal is the only
// spawner of both [H]): tag 0 = the EF2 0xab aura (INIT), tag 1 = every EF2 0x122. Tag 2 is the
// EF2 0x11e at +0x88c (the freeze's ice block).
constexpr int kTagAura = 0, kTag122 = 1, kTagIce = 2;
// The 13 player skill effects that break an EF2 0x122 [BYTES 0x6be3e0, 0x6be450, 0x6be4e0, the immediate
// 0x26f at 0x4e3421]: Yunica's 101 / 112, Hugo's 132 / 137, Toal's 162 / 175 (Ignis, human / demon) and
// 180 / 179 (charged Ignis), Adol's 20 / 21 / 627 / 624 / 623.
constexpr int kBreakers[13] = {101, 112, 132, 137, 162, 175, 179, 180, 20, 21, 627, 624, 623};

int gflag(const AiEnv& e, int i, int unset) { return e.world ? e.world->flag(i, unset) : unset; }
bool in_s3080(const AiEnv& e) { return e.world && e.world->in_room(kRoom); }
std::uint32_t u32(int v) { return std::uint32_t(v); }

// The byte views of the packed dwords (+0x7bc..+0x7bf move parameters, +0x7c0/+0x7c1 the ring
// counters, +0x7a0 the rain record, +0x7a4..+0x7a7 the guard record, +0x8a4 / +0x8a5 the trail
// and pin latches).
int byte_at(const EnemyAi& a, int off) {
    return int((std::uint32_t(a.i(off & ~3)) >> ((off & 3) * 8)) & 0xffu);
}
void set_byte(EnemyAi& a, int off, int v) {
    const int sh = (off & 3) * 8;
    std::uint32_t w = std::uint32_t(a.i(off & ~3));
    w = (w & ~(0xffu << sh)) | ((std::uint32_t(v) & 0xffu) << sh);
    a.i(off & ~3) = std::int32_t(w);
}

AiOut still(EnemyAi& a) {                          // puppet / frozen: see m830.cpp
    AiOut out;
    out.skip_action_player = true;
    a.mode() = 0; a.speed = 0;
    return out;
}

// 0x4e7fc6: the S_3080 story duel ends at half HP (156 = 0, 174 = 1; 130 is the script's).
void half_hp_rule(Actor& A, bool room, AiWorld* w) {
    if (!room) return;
    const float half = A.max_hp * 0.5f;                             // DAT_006bd304
    if (half > A.hp) {
        A.hp = half;
        if (w) { w->set_flag(156, 0); w->set_flag(174, 1); }
    }
}

// What the brain's effect-pool scan (0x4e33d8..0x4e3537) reads: the live EF2 0x122 count (capped
// at 0x40), a live EF2 0x123 with +0x1d5 != 0xff, and three "the player is using a skill" flags:
// fA = a live effect id 0x82/0x65/0x84/0x14, fB = 0x70/0x15, fC = 0x90 (ids 0xbc/0x7d skipped).
// [BYTES 0x4e342e..0x4e352e] over AiWorld::for_each_effect (pool order): the 0x40th 0x122 ends the
// whole scan (0x4e3475); a 0x123 with +0x1d5 == 0xff is not "live"; ids are word compares. The
// 13-id list the shield break collects is gathered by the host's break (AiWorld::break_shields).
struct PoolScan { int n122 = 0; bool live123 = false, fA = false, fB = false, fC = false; };
PoolScan pool_scan(const AiEnv& e) {
    PoolScan r;
    if (!e.world) return r;
    bool full = false;
    e.world->for_each_effect([&](const AiWorld::Fx& f) {
        if (full) return;
        const int id = f.id & 0xffff;
        if (id == 0x122) {
            if (++r.n122 >= 0x40) { full = true; return; }
        } else if (id == 0x123) {
            if ((f.attach & 0xff) != 0xff) r.live123 = true;
        }
        if (id == 0x82 || id == 0x65 || id == 0x84 || id == 0x14) r.fA = true;
        else if (id == 0xbc || id == 0x7d) {}
        else if (id == 0x70 || id == 0x15) r.fB = true;
        else if (id == 0x90) r.fC = true;
    });
    return r;
}

// FUN_0047ad30(p) with the margin in xmm1: inside the arena (g_flags[190] = DAT_0076bc14 mode 1:
// the rectangle {xmin, zmin, xmax, zmax} grown by m; mode 2: the sphere {cx, cy, cz, r} grown by
// m; any other mode: true).
bool in_arena(const AiEnv& e, const Vec3& q, float m) {
    const int mode = gflag(e, 190, 0);
    if (mode != 1 && mode != 2) return true;
    float r[4] = {0, 0, 0, 0};
    e.world->arena(r);                                              // DAT_0076a5cc..d8
    if (mode == 1) {
        if (q.x < r[0] - m || r[2] + m < q.x || q.z < r[1] - m) return false;
        return q.z <= r[3] + m;
    }
    const float dx = q.x - r[0], dy = q.y - r[1], dz = q.z - r[2];
    return std::sqrt(dy * dy + dx * dx + dz * dz) <= r[3] + m;
}

// FUN_004e3070.
void body(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e, Actor& A, AiOut& out) {
    using ai::r15;
    const int d = difficulty(e);
    const bool room = in_s3080(e);
    const int cs = e.character == 2 ? -1 : (e.character == 3 ? 1 : 0);   // [ebp-0xf4] (0x4e3381)
    const int pct = int(std::floor(A.hp * 100.0f / A.max_hp));     // FUN_004217f0 + cvttss2si
    a.set_v(0x808, e.self);                                         // [not modelled] the keep-out restore
    if (e.launched) return;
    const Vec3 to = a.v(0x734);
    const float dist = std::sqrt(to.x * to.x + 0.0f + to.z * to.z);
    bool facing = true;                                             // 0x4e3356: dot >= 0.25 (0x6bd244)
    {
        float hx = float(std::cos(double(a.heading))), hz = float(std::sin(double(a.heading)));
        float tx = to.x, tz = to.z;
        const float hl = std::sqrt(hx * hx + hz * hz), tl = std::sqrt(tx * tx + tz * tz);
        if (hl > 0) { hx /= hl; hz /= hl; }
        if (tl > 0) { tx /= tl; tz /= tl; }
        if (0.25f > tx * hx + 0.0f + tz * hz) facing = false;
    }
    if (kGone > a.f(0x880)) {                                       // defeated: stand still
        a.action() = 2; a.steer() = 0; a.hit_cut = true;
        return;
    }
    const PoolScan pool = pool_scan(e);
    int n122 = pool.n122;
    // The pillar break (0x4e3540..0x4e3879) [BYTES]: particle against particle, so the host runs it
    // over its pools — 2-D SE 424 (0x1a8), the pillar marked and killed, EF2 0x123 (marked 0xff: it
    // never freezes). No write to Kishgal: n122 keeps this tick's count.
    if (e.world) e.world->break_shields(A, 0x122, kBreakers, 13, 0x123, 0x1a8);
    // The freeze (0x4e38a9..0x4e3bae) [BYTES]. T = the player (+0x888 is never set by this brain).
    if (pool.live123 && a.f(0x7d4) == 0.0f && !e.target_durable &&
        (e.target_section == 4 || e.target_section == 0xb)) {
        AiEffect ice{0x11e, e.target, 2.0f, false};                 // power 0, knock 1; +0x88c, fx+0x1a8 = this
        ice.tag = kTagIce;
        out.effects.push_back(ice);
        a.f(0x7d4) = 20.0f;                                         // 0x41a00000
        out.target_section = 9;                                     // FUN_00446550(T+0x8f0, 9, 0)
        a.f(0x7b4) = e.target.x; a.f(0x7b8) = e.target.z;           // the pin point
    }
    if (a.f(0x7d4) > 0.0f) {                                        // 0x4e399d: the hold, the start tick included
        a.f(0x7d4) -= 1.0f;
        if (0.0f > a.f(0x7d4)) a.f(0x7d4) = 0.0f;
        float tx = e.target.x, tz = e.target.z;
        if (e.target_alive && !e.target_durable) {                  // T+0x98 >= 1.0, byte T+0x9d8 == 0
            tx = a.f(0x7b4); tz = a.f(0x7b8);
            out.hold_target = true; out.hold_xz_only = true;        // x / z only, knock channels 1 / 2 zeroed
            out.hold_origin = {tx, e.target.y, tz};
            out.target_wait_add = 1;                                // T+0x9a8 += 1
        }
        // FUN_0047a830(+0x88c, this): while the 0x11e lives, its matrix = diag(2.5) at
        // (T.x - cos(T+0x308) * 0.5, T.y, T.z) (0x4e3a40..0x4e3ba8; FUN_0066fd70 taken as cos [H]).
        AiEffect mv;
        mv.move_tag = kTagIce; mv.move_scale = 2.5f;
        mv.pos = {tx - float(std::cos(double(e.target_heading))) * 0.5f, e.target.y, tz};
        out.effects.push_back(mv);
    }
    switch (a.phase()) {
        case 0: {                                                   // ---- INIT (0x4e3c08)
            ai::init(a, p, e);                                      // FUN_004782f0 + EI[76] +0x90..
            if (!room && e.character != 2) {                        // not the story duel, not Hugo's
                if (e.world) e.world->set_flag(179, 1);             // DAT_0076bbe8
                AiEffect au{0xab, e.self, 1.0f, true};              // attached (+0x1d5 = 1), offset 0
                au.tag = kTagAura;
                out.effects.push_back(au);
            }
            // 0x4e3cd3: FUN_004202e0(CI[+0x124]+0x60 - 0xe, level = +0xc8 as read at 0x4e3c12). The duel scripts'
            // Chr_SetStatus lands before 130 = 1 lets INIT run, so only its level survives.
            if (e.world) e.world->restat(A, A.cast_id, A.level);
            if (room) {                                             // the story duel's floor on ATK
                const Actor* P = e.world ? e.world->player() : nullptr;   // T (+0x888 is 0 after INIT)
                if (P && P->def + 15.0f > A.atk) A.atk = P->def + 15.0f;  // DAT_006bd964
                if (d <= -1) { A.atk += 12.0f; A.def += 3.0f; }     // DAT_006bd948 / DAT_006bd680
                if (d == -2) { A.atk += 5.0f; A.def += 1.0f; }      // DAT_006bd868 / DAT_006bd438
            }
            if (e.character == 1) {
                A.resist[1] = 0.45f;                                // +0xb8 = 0x3ee66666
                if (gflag(e, 179, 0) != 0) A.exp_reward += 7500.0f; // DAT_006bdf50
            } else if (e.character == 2) {
                A.atk = float(double(A.atk) - 1.5);                 // the double at 0x6bd578
                A.resist[1] = 0.63f;                                // 0x3f2147ae
            } else if (e.character == 3) {
                A.atk += 2.0f;                                      // DAT_006bd580
                A.resist[1] = 0.25f;                                // 0x3e800000
                A.resist[3] = 0.2f;                                 // +0xc0 = 0x3e4ccccd
            }
            if (d == 2) A.def += 20.0f;                             // DAT_006bd994
            a.f(0x7d8) = A.def;
            a.i(0x70c) = 0;
            a.i(0x728) = 0;
            a.set_v(0x808, e.self);
            a.i(0x72c) = 0; a.i(0x730) = 0;
            a.f(0x880) = 0.0f;
            a.phase() = 4;
            a.dur() = 45; a.action() = 2; a.steer() = 0;
            a.i(0x88c) = 0;
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
    a.f(0x7cc) = 0.0f > a.f(0x7cc) - 1.0f ? 0.0f : a.f(0x7cc) - 1.0f;   // two countdowns
    a.f(0x7d0) = 0.0f > a.f(0x7d0) - 1.0f ? 0.0f : a.f(0x7d0) - 1.0f;

    // ---- MAIN (0x4e3fa6): switch (+0x72c), byte table 0x4e78b4, dwords 0x4e7828
    const bool g179 = gflag(e, 179, 0) != 0;                        // DAT_0076bbe8: the non-story fights
    int S = e.section;
    auto req = [&](int id) { if (ai::request(S, id, false, e)) { out.section = id; out.section_force = false; } };
    auto force = [&](int id) { S = id; out.section = id; out.section_force = true; };   // FUN_00446620 / FUN_00458e40
    auto play = [&](int id) {                                       // forced when byte +0x7bd or Toal
        if (byte_at(a, 0x7bd) != 0 || e.character == 3) force(id); else req(id);
    };
    auto durable = [&](int v) { out.set_durable = v; };             // +0x9d8 (FUN_00460c10 +0x1c)
    int& sub = a.i(0x72c);
    int& seq = a.i(0x730);
    const int K = 4 + (e.character == 3 ? 2 : 0) - (e.character == 2 ? 2 : 0);   // hits before it reacts
    auto back = [&] { sub = a.i(0x79c); };                          // 0x4e718b
    auto approach = [&](int t) { a.action() = 0x400; a.steer() = 0; a.dur() = t; };
    auto rand15 = [&] {                                             // byte +0x7bc ? R%15 - 5d + 35 : 0
        return byte_at(a, 0x7bc) != 0 ? int(r15(e) % 15u) - 5 * d + 35 : 0;
    };
    auto pick = [&](int s, int v7bc) {                              // a decide pick (0x4e4a5f / 0x4e4a73)
        sub = s; seq = 0; a.i(0x79c) = 0x14; a.i(0x7bc) = v7bc; a.i(0x7c0) = 0;
    };
    auto go = [&](int s, int nseq, int v7bc) {                      // a chain step
        sub = s; seq = nseq; a.i(0x7bc) = v7bc; a.i(0x7c0) = 0;
    };
    auto to_chain = [&] {                                           // 0x4e4ac6 (+0x730 kept)
        a.i(0x7c4) = 0; a.i(0x7bc) = 0; a.i(0x7c0) = 0; sub = 0x1e; a.i(0x79c) = 0x1e;
    };
    // +0x7bc for sub 0x82: byte +0x7be = rain shots ((g179 ? 7 : 5) + d), byte +0x7bc = 1.
    auto v82 = [&] { return int((u32((g179 ? 7 : 5) + d) << 16) & 0xff0000u) | 1; };
    // +0x7bc for sub 0x78 (byte +0x7be = fan volleys 2 / 3 / 4) and for chain dodges.
    auto v78 = [&](int low) { return ((d < 0 ? 2 : (d > 1 ? 4 : 3)) << 16) | low; };
    auto aim = [&] {                                                // heading = atan2(dz, dx) (FUN_0066f80a)
        a.heading = float(std::atan2(double(a.f(0x73c)), double(a.f(0x734))));
    };
    auto ray_hit = [&](const Vec3& o, const Vec3& dl) {             // FUN_005e9380 + FUN_0041e420 / 0041f0d0
        return e.world ? e.world->ray(o, dl).hit : false;
    };
    auto jump_v = [&](float v) {                                    // FUN_00449460 / FUN_004773f0; returns the origin y after
        const float y0 = A.pos.y;
        jump(A, v, e.jump_v0, out);                                 // [not modelled] +0x3c0 |= 1
        return e.self.y + (A.pos.y - y0);
    };

    // The R%4 pick (0x4e43a2 / 0x4e458c / 0x4e4923). `low` = the <= 33% band's 0x8c test.
    auto pick4 = [&](bool low) {
        const int m = int(r15(e) % 4u);
        if (m == 0) pick(0x6e, 1);
        else if (m == 1) pick(0x82, v82());
        else if (m == 2) pick(0x78, v78(1));
        else if (low) { if (u32(n122 - d + 10) < 22u) pick(0x8c, 0x2d050201); }
        else if (u32(n122) < u32(2 * d + 7)) pick(0x8c, 0x60101);
    };
    // The HP bands (0x4e4341..0x4e4ab7); true = the tick ends in an approach.
    auto bands = [&]() -> bool {
        const bool far = dist > 5.0f;                               // 0x6bd868
        auto approach_if = [&](int thr) {
            if (int(r15(e) % 100u) < thr) { approach(15); return true; }
            return false;
        };
        auto mix = [&] {                                            // 0x4e481e / 0x4e49e9
            static constexpr int t5[5] = {0x6e, 0x8c, 0xa0, 0xb4, 0xdc};
            const std::uint32_t R = r15(e);
            seq = n122 > 7 ? t5[R % 5u] : (R % 2u == 0 ? 100 : 0x78);
        };
        if (pct > 80) {
            if (far) {
                if (approach_if(80 - 6 * d)) return true;
                pick4(false);
            } else if (int(r15(e) % 100u) < 70) {
                pick(100, 1);
            } else {
                seq = r15(e) % 2u == 0 ? 0x78 : 0x96;
            }
        } else if (pct > 50) {
            if (far) {
                if (approach_if(75 - 6 * d)) return true;
                if (int(r15(e) % 100u) < 50) pick4(false);
                else {
                    static constexpr int t[5] = {0xa0, 0x8c, 0xd2, 0x82, 0xdc};   // 0x6be3d0 + 0xdc
                    seq = t[r15(e) % 5u];
                }
            } else if (int(r15(e) % 100u) < (5 - d) * 10) {
                pick(100, 1);
            } else {
                static constexpr int t[3] = {0x6e, 0x78, 0x96};
                seq = t[r15(e) % 3u];
            }
        } else {
            if (g179 && 0.0f >= a.f(0x7d0)) { seq = 0xaa; return false; }   // the 0x134 move
            if (pct > 33) {
                if (far) {
                    if (approach_if(75 - 8 * d)) return true;
                    if (int(r15(e) % 100u) < 33 - 5 * d) pick4(false);
                    else {
                        static constexpr int t[7] = {0x64, 0x6e, 0x82, 0x8c, 0xa0, 0xb4, 0xdc};   // 0x6be3f0..
                        seq = t[r15(e) % 7u];
                    }
                } else if (int(r15(e) % 100u) < 33) {
                    pick(100, 1);
                } else {
                    mix();
                }
            } else if (far) {
                if (approach_if(60 - 8 * d)) return true;
                if (int(r15(e) % 100u) < 50 - 8 * d) pick4(true);
                else mix();
            } else if (int(r15(e) % 100u) < 33) {
                pick(100, 1);
            } else {
                static constexpr int t[4] = {0x6e, 0x78, 0x96, 0xb4};   // 0x6be430
                seq = t[r15(e) % 4u];
            }
        }
        return false;
    };

    // The shield ring (0x4e5d2c): byte +0x7c1 rings left, byte +0x7c0 ticks to the next ring.
    // Ring c1 puts byte +0x7bd shields at radius (byte +0x7be - c1 + 1) * 2.6, at angles
    // +-((i >> 1) * byte +0x7bf + byte +0x7bf / 2) degrees off the heading.
    auto ring = [&] {
        const int b7be = byte_at(a, 0x7be), b7bd = byte_at(a, 0x7bd), b7bf = byte_at(a, 0x7bf);
        const int c0 = byte_at(a, 0x7c0), c1 = byte_at(a, 0x7c1);
        if (c1 == 0) {                                              // done
            if (u32(sub - 0x8c) < 10u) { a.action() = 1; a.dur() = rand15(); }
            sub += 1;
            return;
        }
        if (c0 != 0) { set_byte(a, 0x7c0, c0 - 1); return; }
        bool any = false;
        int left = c1;
        for (std::uint32_t i = 0; i < u32(b7bd); ++i) {
            if (u32(n122) >= 22u) { left = 0; break; }              // the pool cap
            const std::uint32_t q = (i >> 1) * u32(b7bf) + (u32(b7bf) >> 1);
            float ang = float(double(q)) * kPi / 180.0f;
            if (i & 1u) ang *= -1.0f;
            ang += a.heading;
            const float r = float(double(u32(b7be - c1))) * 2.6f + 2.6f;   // 0x6bd634
            const Vec3 dl{float(std::cos(double(ang))) * r, 0.0f, float(std::sin(double(ang))) * r};
            if (ray_hit({e.self.x, A.floor_y + 0.5f, e.self.z}, dl)) continue;
            const Vec3 at{e.self.x + dl.x, e.self.y + 0.0f, e.self.z + dl.z};
            if (!in_arena(e, at, 1.0f)) continue;                   // FUN_0047ad30
            AiEffect s{0x122, {at.x, at.y - 0.3f, at.z}, 1.0f, false};
            s.tag = kTag122; s.power = -2.0f; s.knock = 0.0f;       // owner +0x1a8 = this
            out.effects.push_back(s);
            ++n122;
            any = true;
        }
        if (any) out.sounds.push_back(0x180);
        set_byte(a, 0x7c0, 6);
        set_byte(a, 0x7c1, left != 0 ? left - 1 : 0);
    };
    // The bullet fan (0x4e583b): one EF2 0x11c (kind 1) ahead; with g_flags[179] 3 (5 at d == 2).
    auto fan = [&] {
        static constexpr float kFan[5] = {0.0f, 0.3926987648010254f, -0.3926987648010254f,
                                          0.7853975296020508f, -0.7853975296020508f};   // 0x6be950 + 0xbf490fd0
        const int n = d == 2 ? 5 : 3;
        for (int k = 0; k < n; ++k) {
            const double g = double(kFan[k] + a.heading);
            const float s = float(std::sin(g)), c = float(std::cos(g));
            AiBullet b;                                             // FUN_0040f700 + FUN_00583a00 + FUN_00593b90
            b.kind = 1; b.effect = 0x11c;
            b.pos = {e.self.x + c, e.self.y + 0.0f, e.self.z + s};
            b.dir = {c, 0.0f, s};
            b.speed = g179 ? 0.3f : 0.5f;                           // 0x6bd27c / 0x6bd304
            b.accel = g179 ? 0.01f : 0.0f;                          // 0x6bd084
            b.power = 0.0f; b.knock = 3.0f;
            b.trail_effect = 0x11d; b.trail_period = 0;
            b.f168 = 1.0f;                                          // 0x6be190 = {0, 1, 0, 0}
            out.bullets.push_back(b);
            if (!g179) break;
        }
    };
    // One EF2 0x124 homing bullet (kind 2, trail 0x125) from 3.0 ahead (0x4e62ea).
    auto shot124 = [&] {
        float acc = float(cs) * 0.00019999999494757503f + (d < 0 ? 0.0010999999940395355f : 0.0013000000035390258f);
        float hr = float((double(float(d)) * 0.1 + 3.0) * 3.141590118408203 / 180.0);
        float off;
        if (g179) {                                                 // +45 then -45 degrees
            const int c = a.i(0x7c0);
            if (c == 1) { acc += 0.00039999998989515007f; hr *= 0.6600000262260437f; }
            off = float(c * 90 - 135);
        } else {
            off = float(int(r15(e) % 90u) - 45);                    // FUN_00439120
        }
        off = off * kPi / 180.0f;
        const float g = off + a.heading;
        const float c = float(std::cos(double(g))), s = float(std::sin(double(g)));
        AiBullet b;                                                 // FUN_00593550(edx = 2)
        b.kind = 2; b.effect = 0x124;
        b.pos = {c * 3.0f + e.self.x, e.self.y + 0.0f, s * 3.0f + e.self.z};
        b.dir = {c, 0.0f, s};
        b.speed = 0.0010000000474974513f; b.accel = acc; b.gravity = 0.0f;
        b.power = 0.0f; b.knock = 3.0f;
        b.trail_effect = 0x125; b.trail_period = 6;
        b.home_period = 1; b.home_rate = hr;
        b.f16c = 10.0f; b.f170 = 200.0f;                            // homing from age 10 to 200
        out.bullets.push_back(b);
        // [not modelled] the zoom blur (0x76a550 = 0.1, 0x76a554 = -0.0075, 0x7b2488 = 1, FUN_005e0590)
        a.i(0x7c0) -= 1;
    };
    // The rain record +0x7a0 = [count0, timer0, count1, timer1]: sub 0x83 arms a slot with
    // (byte +0x7be, 180) (0x4e5bbf).
    auto arm_rain = [&] {
        const std::uint32_t v = u32(a.i(0x7a0));
        int slot = 0;
        if ((v & 0xffu) != 0) {
            if (((v >> 16) & 0xffu) == 0) slot = 2;
            else if (((v >> 8) & 0xffu) < (v >> 24)) slot = 2;
        }
        const int sh = slot * 8;
        std::uint32_t w = (v & ~(0xffu << sh)) | (u32(byte_at(a, 0x7be)) << sh);
        w = (w & ~(0xffu << (sh + 8))) | (0xb4u << (sh + 8));
        a.i(0x7a0) = std::int32_t(w);
    };

    switch (sub) {
        case 0:                                                     // the opening ring (0x4e3fd1)
            if (a.dur() != 0) break;
            sub = 0x8c; seq = 0; a.i(0x79c) = 0x14;
            a.i(0x7bc) = 0x5a080200; a.i(0x7c0) = 0;                // 2 shields x 8 rings at +-45
            break;
        case 0x14: {                                                // decide (0x4e401b)
            if (a.dur() != 0) break;
            durable(0);
            if (seq != 0) break;
            if (pool.fC && u32(sub) < 100u) {                       // effect 0x90 live: guard
                sub = 0xb4; seq = 0; a.i(0x79c) = 0x14; a.i(0x7bc) = 0x00c85a09; a.i(0x7c0) = 2;
            }
            bool c8 = false;
            if (u32(a.i(0x7c4)) >= u32(K) && u32(sub) < 100u && u32(seq) < 100u) {
                const int g1 = d < 0 ? 0xbe5a05 : (d < 2 ? 0xbe5a03 : 0xbe5a02);
                const int g2 = d < 0 ? 0x645a05 : (d < 2 ? 0x645a03 : 0x645a02);
                if (!(5.0f > dist)) {
                    c8 = true;                                      // far: the dodge chain
                } else {                                            // near: guard
                    const int r = int(r15(e) % 100u);
                    sub = 0xb4; seq = 0; a.i(0x79c) = 0x14;
                    if (r < 0x42) { a.i(0x7bc) = g1; a.i(0x7c0) = 0; }
                    else { a.i(0x7bc) = g2; a.i(0x7c0) = 0x20101; }
                    a.i(0x7c4) = 0;
                }
            }
            if (!c8) {
                bool fb = false;
                if (pool.fB) {
                    const int r = int(r15(e) % 100u);
                    if (r < (100 - pct) / 2 + 50 && u32(sub) < 100u) fb = u32(seq) < 100u;
                }
                if (pool.fA && u32(sub) < 100u && u32(seq) < 100u) {
                    const int r = int(r15(e) % 100u);
                    if (r < (100 - pct) / 2 + 50 || e.character == 2) c8 = true;
                }
                if (fb) c8 = true;
            }
            if (c8) {                                               // 0x4e421f
                seq = 0xc8; a.i(0x7c4) = 0; a.i(0x7bc) = 0; a.i(0x7c0) = 0; sub = 0x1e; a.i(0x79c) = 0x1e;
                break;
            }
            if (!facing) { approach(15); break; }
            if (!e.target_grounded && 6.0f > dist && u32(sub) < 100u && u32(seq) < 100u) {
                if (int(r15(e) % 100u) < 0x42) pick(0x96, 0x101);   // the player is airborne
                else seq = 0xbe;
            }
            if (u32(sub) < 100u && u32(seq) < 100u && bands()) break;
            if (u32(seq) >= 100u) { to_chain(); break; }            // 0x4e4abd
            if (u32(sub) >= 100u) break;
            approach(15);
            break;
        }
        case 0x1e: {                                                // the scripted chain (0x4e4b33)
            if (u32(a.i(0x7c4)) >= u32(K)) { sub = 0x14; seq = 0; break; }
            if (a.dur() != 0) break;
            const int v = seq;                                      // byte table 0x4e7a08, dwords 0x4e7980
            switch (v) {
                case 0: go(0xc8, 1, (5.0f > dist || pool.fB) ? 1 : 2); break;
                case 1: a.action() = 2; a.steer() = 0; seq = 2; a.dur() = 30 - 5 * d; break;
                case 2: sub = 0x14; seq = 0; break;
                case 0x64: go(0x8c, 0x65, 0x3c050201); break;
                case 0x65: case 0x6f: go(0x82, v + 1, v82()); break;
                case 0x66: go(0xa0, 0, 1); break;
                case 0x6e: go(0xc8, 0x6f, 1); break;
                case 0x70: go(0x78, 0, v78(1)); break;
                case 0x78: go(100, 0x79, 1); break;
                case 0x79: go(0x8c, 0, 0x5a040401); break;
                case 0x82: go(0x6e, 0x83, 1); break;
                case 0x83: case 0x97: if (facing) go(100, 0, 1); else approach(4); break;
                case 0x8c: go(0x78, 0x8d, v78(1)); break;
                case 0x8d: go(0x6e, 0, 1); break;
                case 0x96: go(0x96, 0x97, 1); break;
                case 0xa0: go(0xa0, 0xa1, 1); break;
                case 0xa1: go(0xc8, 0xa2, v78(2)); break;
                case 0xa2: go(0x96, 0, 1); break;
                case 0xaa: case 0xab: go(0xc8, v + 1, 0); break;
                case 0xac: go(0xaa, 0, 1); break;
                case 0xb4: go(0xa0, 0xb5, 1); break;
                case 0xb5: if (!facing) approach(4); else go(0x6e, 0xb6, 1); break;
                case 0xb6: case 0xd3: if (facing) go(0x6e, 0, 1); else approach(4); break;
                case 0xbe:                                          // hop and face the player
                    (void)jump_v(0.28f);                            // &0x3e8f5c29
                    aim();
                    seq = v + 1; a.dur() = 15; a.steer() = 0; a.action() = 1;
                    break;
                case 0xbf: go(100, 0, 0x10101); break;
                case 0xc8:
                    if (8.0f > dist) { go(0xc8, 0xc9, 1); break; }  // 0x6bd8e8
                    (void)jump_v(0.28f);
                    seq = v + 1; sub = 0xc8; a.i(0x7bc) = 2; a.i(0x7c0) = 0;
                    break;
                case 0xc9: {
                    const int r = int(r15(e) % 100u);
                    const bool ok = r < 0x42 && u32(n122 + 0x10) < 0x16u;
                    if (d > 1 && e.character == 2) {
                        if (ok) go(0x82, 0xcd, v82()); else seq = 0xce;
                    } else {
                        seq = ok ? 0xcd : 0xce;
                    }
                    break;
                }
                case 0xcd:
                    aim();
                    seq = v + 1; sub = 0x8c; a.i(0x7bc) = 0x3c080201; a.i(0x7c0) = 0;
                    break;
                case 0xce: {
                    const int r = int(r15(e) % 100u);
                    seq = 0;
                    if (pct < 50) {
                        if (r < 15 * d + 50) { sub = a.f(0x7cc) == 0.0f ? 0xa0 : 0x78; a.i(0x7bc) = 1; a.i(0x7c0) = 0; }
                        else { sub = 0x6e; a.i(0x7bc) = 1; a.i(0x7c0) = 0; }
                    } else if (r < 15 * d + 50) {
                        sub = 0x78; a.i(0x7bc) = v78(1); a.i(0x7c0) = 0;
                    } else {
                        sub = 0x6e; a.i(0x7bc) = 1; a.i(0x7c0) = 0;
                    }
                    break;
                }
                case 0xd2: go(0x82, 0xd3, v82()); break;
                case 0xdc: case 0xde: go(0xc8, v + 1, 1); break;
                case 0xdd: case 0xdf: go(0x78, 0, v78(1)); break;
                default: break;
            }
            break;
        }
        case 100: {                                                 // melee (0x4e54ad)
            const int m = byte_at(a, 0x7be);
            if (m == 0) a.i(0x7c0) = 7;
            else if (m == 1) a.i(0x7c0) = 0x132;
            else if (m == 2) a.i(0x7c0) = 0x12f;
            play(a.i(0x7c0));
            a.action() = 1; a.dur() = -1; durable(1);
            sub += 1;
            break;
        }
        case 0x65:
            if (((u32(S) ^ u32(a.i(0x7c0))) & 0xffffu) == 0) {     // still in the section
                if (e.aso_marker == 1 && (u32(a.i(0x7c0)) >> 16) == 0)
                    a.i(0x7c0) |= 0x10000;                          // [not modelled] the camera shake (once)
                break;
            }
            a.action() = 1; a.dur() = rand15(); sub = 0x66;
            [[fallthrough]];
        case 0x66: case 0x81: case 0x84: case 0x8f:                 // 0x4e55fb
            if (a.dur() != 0) break;
            durable(0); back();
            break;
        case 0x6e:                                                  // section 0x133 (0x4e5626)
            play(0x133);
            set_byte(a, 0x8a4, 1);
            a.action() = 1; a.dur() = -1; durable(1);
            sub += 1;
            break;
        case 0x6f:
            if (S == 0x133) break;
            a.action() = 1; a.dur() = rand15();
            set_byte(a, 0x8a4, 0);
            sub = 0x70;
            [[fallthrough]];
        case 0x70:
            if (a.dur() != 0) break;
            durable(0); back();
            break;
        case 0x78:                                                  // section 8 (0x4e571d)
            play(8);
            a.action() = 2; a.dur() = -1; durable(1);
            sub += 1;
            break;
        case 0x79:                                                  // into 0x136 (0x4e5769)
            if (S == 8 && e.aso_marker == 0) break;
            if (S == 4) { a.action() = 1; a.dur() = rand15(); sub = 0x81; break; }
            if (byte_at(a, 0x7bd) != 0) force(0x136); else req(0x136);   // no Toal rule here
            sub += 1;
            break;
        case 0x7a:                                                  // fire (0x4e581d)
            if (S == 0x136) {
                if (e.aso_marker == 0) break;
            } else if (S == 4) {
                a.action() = 1; a.dur() = rand15(); sub = 0x81;
                break;
            }
            fan();
            sub += 1;
            [[fallthrough]];
        case 0x7b: {                                                // 0x4e5ad2: repeat byte +0x7be times
            if (S == 0x136 && e.aso_marker == 1) break;
            const int n = byte_at(a, 0x7c0) + 1;
            set_byte(a, 0x7c0, n);
            if (n >= byte_at(a, 0x7be)) { a.action() = 1; a.dur() = rand15(); sub = 0x81; }
            else sub -= 2;
            break;
        }
        case 0x82:                                                  // section 0x130 (0x4e5b14)
            play(0x130);
            a.action() = 1; a.dur() = -1; durable(1);
            sub += 1;
            break;
        case 0x83:
            if (S == 0x130) { if (e.aso_marker == 1) set_byte(a, 0x7c0, 1); break; }
            a.action() = 1; a.dur() = rand15();
            if (byte_at(a, 0x7c0) != 0) arm_rain();
            sub = 0x84;
            if (a.dur() != 0) break;
            durable(0); back();
            break;
        case 0x8c:                                                  // section 0x12d (0x4e5c42)
            force(0x12d);
            a.action() = 2; a.steer() = 0; a.dur() = -1; durable(1);
            sub += 1;
            break;
        case 0x8d:
            if (S != 0x12d) { a.action() = 1; a.dur() = rand15(); durable(0); sub = 0x8f; break; }
            if (u32(e.aso_marker) < 2u) {
                set_byte(a, 0x8a4, 1);
                if (e.aso_marker == 1) a.action() = 1;
                break;
            }
            a.i(0x7c0) = byte_at(a, 0x7be) << 8;                    // byte +0x7c1 = rings, +0x7c0 = 0
            sub += 1;
            set_byte(a, 0x8a4, 0);
            a.action() = 1;
            [[fallthrough]];
        case 0x8e: case 0xac:
            ring();
            break;
        case 0x96:                                                  // section 0x131 (0x4e605d)
            play(0x131);
            a.action() = 2; a.dur() = -1; durable(1);
            sub += 1;
            break;
        case 0x97:
            if (S == 0x131) {
                if (e.aso_marker != 1) break;
                const float th = float(std::atan2(double(a.f(0x73c)), double(a.f(0x734))));
                const float s = float(double(float(d)) * 0.002 + 0.0860000029206276 + double(float(cs) * 0.012f));
                A.mover.k2.x += float(std::cos(double(th))) * s;    // +0x344.. (channel 2)
                A.mover.k2.y += s * 0.0f;
                A.mover.k2.z += float(std::sin(double(th))) * s;
                set_byte(a, 0x8a4, 1);
                break;
            }
            set_byte(a, 0x8a4, 0);
            a.action() = 1; a.dur() = rand15();
            sub = 0x98;
            [[fallthrough]];
        case 0x98:
            if (a.dur() != 0) break;
            durable(0); back();
            break;
        case 0xa0:                                                  // section 0x12e (0x4e6222)
            if (a.f(0x7cc) > 0.0f) { a.dur() = 0; back(); break; }
            play(0x12e);
            a.action() = 1; a.dur() = -1; durable(1);
            a.i(0x7c0) = g179 ? 2 : 1;                              // shots
            sub += 1;
            break;
        case 0xa1:
            if (S == 0x12e) {
                if (e.aso_marker == 2 && a.i(0x7c0) != 0) shot124();
                break;
            }
            a.action() = 1; a.dur() = rand15();
            sub += 1;
            [[fallthrough]];
        case 0xa2:
            if (a.dur() != 0) break;
            durable(0); back();
            a.f(0x7cc) = (e.character == 3 ? 300.0f : 600.0f) + (d < 0 ? 5.0f : 0.0f);   // 0x6bdc44 / 0x6bdd5c
            break;
        case 0xaa:                                                  // section 0x134 (0x4e663a)
            if (a.f(0x7d0) > 0.0f) { a.dur() = 0; back(); break; }
            force(0x134);
            a.action() = 2; a.steer() = 0; a.dur() = -1; durable(1);
            a.f(0x7ac) = e.self.x; a.f(0x7b0) = e.self.z;
            set_byte(a, 0x8a5, 1);                                  // pinned (the tail)
            if (u32(e.character) <= 3u) A.def = a.f(0x7d8) + 40.0f; // 0x6bda34
            out.effects.push_back({0x1b9, {e.self.x + 0.0f, e.self.y - 0.7f, e.self.z + 0.0f}, 1.0f, false});
            sub += 1;
            break;
        case 0xab:
            if (S != 0x134) { durable(0); back(); break; }          // (+0x8a5 stays set: the bytes)
            if (u32(e.aso_marker) < 2u) break;
            set_byte(a, 0x7bd, 2); set_byte(a, 0x7be, 8); set_byte(a, 0x7bf, 0x46);
            a.i(0x7c0) = 0x800;                                     // 2 shields x 8 rings at +-35
            sub += 1;
            set_byte(a, 0x8a5, 1);
            a.action() = 1;
            break;
        case 0xad:
            if (S != 0x134) { set_byte(a, 0x8a5, 0); durable(0); back(); break; }
            if (u32(e.aso_marker) < 3u) break;
            a.i(0x7c0) = 0;
            sub += 1;
            break;
        case 0xae: {                                                // the pillars (0x4e684a)
            static constexpr float kR[4] = {6.0f, 11.0f, 16.0f, 21.0f};   // 0x6becc0
            const Vec3 base{e.self.x, A.floor_y, e.self.z};
            const float ch = float(std::cos(double(a.heading))), sh = float(std::sin(double(a.heading)));
            for (int k = 0; k < 4; ++k) {
                const Vec3 dl{ch * kR[k], -0.5f, sh * kR[k]};
                if (ray_hit(base, dl)) continue;
                AiEffect f{0x195, {dl.x + e.self.x, e.self.y + dl.y, dl.z + e.self.z}, 1.0f, false};
                f.power = 18.0f; f.knock = 10.0f; f.rtype = 1;
                out.effects.push_back(f);
            }
            AiEffect g{0x196, {base.x + ch * 11.0f, A.floor_y - 1.0f, sh * 11.0f + base.z}, 1.0f, false};
            g.power = -2.0f; g.knock = 5.0f; g.rtype = 1;
            out.effects.push_back(g);
            sub += 1;
            break;
        }
        case 0xaf:
            if (S != 0x134) {
                a.action() = 1; a.dur() = rand15();
                sub += 1;
                set_byte(a, 0x8a5, 0);
                A.def = a.f(0x7d8);
                break;
            }
            a.i(0x7c0) += 1;                                        // [not modelled] the shake at 20, the blur to 50
            break;
        case 0xb0:
            if (a.dur() != 0) break;
            durable(0); back();
            a.f(0x7d0) = 1200.0f - float(d);                        // 0x6bde9c
            break;
        case 0xb4:                                                  // the guard (section 6)
            if (a.i(0x7a4) == 0) {
                a.i(0x7a4) = a.i(0x7bc);
                set_byte(a, 0x7a7, (a.i(0x7bc) >> 8) & 0xff);       // ticks left = the reset value
                a.i(0x7a8) = a.i(0x7c0);
                force(6);
                a.action() = 2; a.dur() = -1;
            }
            sub = 0xb5;
            [[fallthrough]];
        case 0xb5: {
            const std::uint32_t v = u32(a.i(0x7a4));
            if (v == 0) { a.dur() = 0; back(); break; }
            int hits = int(v & 0xff), left = int(v >> 24);
            const int reset = int((v >> 8) & 0xff), next = int((v >> 16) & 0xff);
            if (S != 6) { a.i(0x7a4) = 0; break; }
            if (e.was_hit) {
                left = reset;
                if (hits != 0 && --hits == 0) {                     // broken: counter
                    req(2);
                    sub = next; a.i(0x7bc) = a.i(0x7a8); a.i(0x7a4) = 0;
                }
            } else {
                left -= 1;
            }
            if (left == 0) {
                a.i(0x7a4) = 0; a.dur() = 0; sub = a.i(0x79c);
                req(2);
            } else if (a.i(0x7a4) != 0) {
                a.i(0x7a4) = std::int32_t((u32(left) << 24) | (u32(next) << 16) | (u32(reset) << 8) |
                                          (u32(hits) & 0xffu));
            }
            break;
        }
        case 0xbe: {                                                // shield + side-jump (0x4e6dbe)
            if (u32(n122) < 22u) {
                AiEffect s{0x122, e.self, 1.0f, false};             // no +0x1a8 owner written
                s.tag = kTag122; s.power = -2.0f; s.knock = 1.0f;
                out.effects.push_back(s);
                out.sounds.push_back(0x180);
            }
            static constexpr float kSide[5] = {kHalfPi, -kHalfPi, k30, -k30, kPi};   // 0x6bef60 + pi
            int k = 0;
            for (; k < 5; ++k) {
                const float g = kSide[k] + a.heading;
                const Vec3 dl{float(std::cos(double(g))) * 3.5f, 0.0f, float(std::sin(double(g))) * 3.5f};
                if (ray_hit(e.self, dl)) continue;
                force(3);
                const float w = 0.2857142984867096f;                // 0x6bd270
                A.mover.k1.x += dl.x * w; A.mover.k1.y += dl.y * w; A.mover.k1.z += dl.z * w;
                (void)jump_v(0.25999999046325684f);                 // 0x6bd254
                break;
            }
            a.dur() = k >= 5 ? 0 : 30;
            durable(0); back();
            break;
        }
        case 0xc8: {                                                // the jump-dodge (0x4e6fae)
            if (S == 4 && e.character != 2) {                       // wait out section 4 (60 ticks)
                a.i(0x7c0) += 1;
                if (u32(a.i(0x7c0)) >= 60u) { a.action() = 2; back(); }
                break;
            }
            const float th = float(std::atan2(double(a.f(0x73c)), double(a.f(0x734))));   // FUN_00427410
            const int m = a.i(0x7bc) & 0xff;
            float scale = 3.5f;                                     // 0x6bd720
            float T[5];
            if (m == 0) {                                           // away
                const float t0[5] = {kPi, k120, -k120, kHalfPi, -kHalfPi};
                for (int i = 0; i < 5; ++i) T[i] = t0[i];
                scale = 1.75f;                                      // 0x6bd510
                if (th > a.heading) for (int i = 1; i < 5; ++i) T[i] *= -1.0f;
            } else if (m == 1) {                                    // aside
                const float t0[5] = {kHalfPi, -kHalfPi, k120, -k120, kPi};
                for (int i = 0; i < 5; ++i) T[i] = t0[i];
                if (th > a.heading) { T[0] = -kHalfPi; T[1] = kHalfPi; T[2] = -k120; T[3] = k120; }
            } else {                                                // toward (m == 2; no other value reaches here [H])
                const float t0[5] = {0.0f, k60, -k60, kHalfPi, -kHalfPi};
                for (int i = 0; i < 5; ++i) T[i] = t0[i];
                if (a.heading > th) { T[0] = -0.0f; T[1] = -k60; T[2] = k60; T[3] = -kHalfPi; }
            }
            int k = 0;
            for (; k < 5; ++k) {
                const float g = th + T[k];
                const Vec3 dl{float(std::cos(double(g))) * scale, 0.0f, float(std::sin(double(g))) * scale};
                if (ray_hit(e.self, dl)) continue;
                force(3);
                A.mover.k1.x += dl.x * 0.5f; A.mover.k1.y += dl.y * 0.5f; A.mover.k1.z += dl.z * 0.5f;
                const float oy = jump_v(0.28999999165534973f);      // 0x6bd274
                out.effects.push_back({9, {e.self.x, oy - 0.9f, e.self.z}, 0.7f, false});
                break;
            }
            a.dur() = k >= 5 ? 0 : 30;
            a.action() = 2;
            back();
            break;
        }
        default: break;
    }

    // ---- the tail (0x4e7197)
    Vec3 org = e.self;
    if (byte_at(a, 0x8a5) == 0) {                                   // remember the spot
        a.f(0x7ac) = org.x; a.f(0x7b0) = org.z;
    } else if (e.grounded) {                                        // pinned: back to it, channels cleared
        A.pos.x = org.x = a.f(0x7ac);
        A.pos.z = org.z = a.f(0x7b0);
        A.mover.k1 = {}; A.mover.k2 = {};
    }
    if (byte_at(a, 0x8a4) != 0) {                                   // the trail (+0x8a0 ticks)
        const std::uint32_t t = u32(a.i(0x8a0));
        if (t % 15u == 0 && e.grounded) out.effects.push_back({0x11b, org, 1.0f, false});
        if (t % 5u == 0) out.effects.push_back({0x11d, org, 1.0f, false});
    }
    if (a.i(0x7a0) != 0) {                                          // the rain (0x4e74b5)
        const Actor* P = e.world ? e.world->player() : nullptr;
        for (int sh = 8; sh < 0x28; sh += 16) {
            const std::uint32_t v = u32(a.i(0x7a0));
            const std::uint32_t cnt = (v >> (sh - 8)) & 0xffu, tm = (v >> sh) & 0xffu;
            if (cnt == 0) continue;
            if (tm != 0) {
                a.i(0x7a0) = std::int32_t((v & ~(0xffu << sh)) | (((tm - 1) & 0xffu) << sh));
                continue;
            }
            const float ang = float(int(r15(e) % 360u)) * kPi / 180.0f;
            const float rr = float(int(r15(e) % 2000u)) * 0.0010000000474974513f;
            float ox = float(std::cos(double(ang))) * rr, oz = float(std::sin(double(ang))) * rr;
            if (d >= 0) {                                           // led by half a unit
                ox = float(std::cos(double(e.target_heading))) * 0.5f + ox;
                oz = oz + float(std::sin(double(e.target_heading))) * 0.5f;
            }
            const float tfloor = P ? P->floor_y : e.target_floor;   // T+0x318
            AiBullet b;                                             // FUN_00593550(edx = 1)
            b.kind = 1; b.effect = 0x121;
            b.pos = {ox + e.target.x, tfloor + 16.0f, oz + e.target.z};
            b.dir = {0.0f, -1.0f, 0.0f};
            b.speed = float(d) * 0.025f + 0.59f + float(cs) * 0.03f;
            b.power = 0.0f; b.knock = 1.5f;
            b.trail_effect = 0x11d; b.trail_period = 6;
            b.f168 = 1.0f;
            out.bullets.push_back(b);                               // [not modelled] FUN_00451b50 tilt, fx+0x1d5 = 2
            out.sounds.push_back(0x17f);
            const std::uint32_t c2 = (cnt - 1) & 0xffu;
            std::uint32_t w = (v & ~(0xffu << (sh - 8))) | (c2 << (sh - 8));
            w = (w & ~(0xffu << sh)) | ((cnt - 1 != 0 ? 15u : 0u) << sh);
            a.i(0x7a0) = std::int32_t(w);
        }
    }
    if (a.phase() == 4) {
        ai::steer(a, e);
        ai::turn(a, a.f(0x814));
        if (A.flyer) ai::hover(a, A, e, nullptr, a.i(0x720));
        ai::act(a, p, e, &out);
    }
}

}  // namespace

void m860_dead(Actor& A, AiWorld& w) {
    EnemyAi& a = A.ai;
    if (w.flag(130, 1) == 0) {                                      // the puppet path (0x4e7b16)
        A.hp = 1.0f;
        if (w.in_room(kRoom)) a.hook_kill_tags |= 1u << kTag122;
        return;
    }
    // The live path with HP < 1 (0x4e7bbd).
    if (Actor* P = w.player(); P && !P->alive()) {                  // the player lost: Kishgal stays up
        A.hp = 1.1f;
        a.hittable = false; A.def = 999.0f;                         // FUN_0047aa80(edx = 1)
        return;
    } else if (P) {
        P->i_frames = 9999;                                         // a store, not a grant
    }
    if (!(kGone > a.f(0x880))) {                                    // 0x4e7c1c: the KO resolves once
        // [not modelled] FUN_0043a040(0x1a5, 0x400): the 2D defeat SE (a death hook has no AiOut).
        a.f(0x880) = -9999999.0f;                                   // 0xcb18967f
        w.set_flag(174, 1);                                         // DAT_0076bbd4
        w.set_flag(156, 0);                                         // DAT_0076bb8c: the duel is over
        w.set_flag(130, 0);                                         // DAT_0076bb24: back to a puppet
        A.hp = 1.0f;
        w.hide_boss_gauge();                                        // DAT_006d5fd8 = 0
        a.action() = 2; a.steer() = 0;
        a.hook_kill_tags |= (1u << kTag122) | (1u << kTagAura);     // every EF2 0x122 / 0xab
        a.hit_cut = true;                                           // +0x118 = 0
    }
    half_hp_rule(A, w.in_room(kRoom), &w);                          // 0x4e7fc6, reached from here too
}

AiOut m860(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    if (!e.actor) return {};
    Actor& A = *e.actor;
    const bool room = in_s3080(e);
    // ---- the handler FUN_004e7af0
    if (gflag(e, 130, 1) == 0) {                                    // the script puppet
        if (A.hp < 1.0f) A.hp = 1.0f;
        AiOut out = still(a);
        if (room) { AiEffect k; k.kill_tag = kTag122; out.effects.push_back(k); }   // 0x4e7b31
        return out;
    }
    // (the dead branch is m860_dead; the off-screen freeze is enemy_ai_tick's)
    AiOut out;
    if (gflag(e, 131, 0) != 0) out = still(a);                      // FUN_0045f470 only
    else body(a, p, e, A, out);                                     // no FUN_0047aa80 here (0x4e7f33)
    // The gauge (0x4e7f48): while not defeated, push on every HP change.
    if (!(kGone > a.f(0x880)) && a.f(0x880) != A.hp) {
        a.f(0x880) = A.hp;
        out.gauge_hp = A.hp;
        out.gauge_len = e.character == 1 && gflag(e, 179, 0) != 0 ? 8500.0f : 7500.0f;   // 0x6bdf54 / 0x6bdf50
    }
    half_hp_rule(A, room, e.world);
    return out;
}

}  // namespace cleria::brains
