// M830 — Galleon, a class-4 duel opponent (Toal's S_1080 duel, `Battle2`). Handler
// FUN_004e9870, brain FUN_004e8050 (AI index 76). Spec: build/ai_spec/M830.md.
//
// The handler runs the brain only while g_flags[130] ("boss AI live") is set; otherwise
// Galleon is a script puppet (FUN_0045f250: no AI, no action player) whose HP never drops
// below 1. g_flags[131] freezes the AI the same way. The brain is a combo sequencer: the
// sub-state +0x72c plays one move (100 = section 7 thrust, 0x6e = section 8 slam with
// repeats, 0x78 = section 300 spinning slash, 0x82 = section 301 dash, 0x8c = guard in
// section 6, 0x96 = section 305 charge-up, 0xa0 = a jump-dodge through section 3) and then
// returns to +0x79c (0x14 = decide again, 0x1e = continue the scripted chain +0x730). At
// <= 50% HP it runs the 300-chain once per 1200 ticks: dodge back, DEF + 6, the charge-up
// aura (EF2 39), three thrusts and a triple slam at double animation speed, then DEF back.
// When its HP falls below 1 with the player alive the handler (m830_dead) restores HP to
// 1, clears flags 156 and 130 (which ends the duel's Wait_FlagEqualInt(156, 0)), hides the
// gauge and asks for section 5.
// [not modelled] +0x64 / +0x4be / +0x3c0 writes; the
// slam's camera shake (0x76a5ac = 12, 0x76a598 = 0.25); the player-dead helper's forced
// section 5 -> 2.
#include <cmath>
#include <cstdint>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {
namespace {

constexpr float kGone = -7777777.0f;              // DAT_006be15c
constexpr float kPi = 3.141590118408203f;         // 0x40490fd0 (the dodge tables' pi)
constexpr float kHalfPi = 1.5707950592041016f;    // 0x3fc90fd0
constexpr float k120 = 2.095440626144409f;        // 0x6bec04
constexpr float k60 = 1.046149492263794f;         // 0x6bebf4

int gflag(const AiEnv& e, int i, int unset) { return e.world ? e.world->flag(i, unset) : unset; }

// The byte views of the packed dwords (+0x7bc..+0x7be move flags, +0x7c0 repeat count,
// +0x7a0..+0x7a3 the guard record, +0x8a4 the slam latch).
int byte_at(const EnemyAi& a, int off) {
    return int((std::uint32_t(a.i(off & ~3)) >> ((off & 3) * 8)) & 0xffu);
}
void set_byte(EnemyAi& a, int off, int v) {
    const int sh = (off & 3) * 8;
    std::uint32_t w = std::uint32_t(a.i(off & ~3));
    w = (w & ~(0xffu << sh)) | ((std::uint32_t(v) & 0xffu) << sh);
    a.i(off & ~3) = std::int32_t(w);
}

// Puppet / frozen ticks: no brain, no action player; the AI mover input stays still [H]
// (the engine skips FUN_00478080 on these paths; zeroing +0x6f8 / +0x30c is the host
// equivalent).
AiOut still(EnemyAi& a) {
    AiOut out;
    out.skip_action_player = true;
    a.mode() = 0; a.speed = 0;
    return out;
}

// FUN_004e8050, from 0x4e80a2 to the 0x4e95cf tail.
void body(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e, Actor& A, AiOut& out) {
    using ai::r15;
    const int d = difficulty(e);
    const float fd = float(d);
    const int pct = int(std::floor(A.hp * 100.0f / A.max_hp));     // FUN_004217f0 + cvttss2si
    a.set_v(0x808, e.self);                                         // (the restore: ai::keep_out, by the host)
    if (e.launched) return;
    const Vec3 to = a.v(0x734);
    const float dist = std::sqrt(to.x * to.x + 0.0f + to.z * to.z);
    bool facing = true;                                             // 0x4e8277: dot >= 0.2
    {
        float hx = std::cos(a.heading), hz = std::sin(a.heading);
        float tx = to.x, tz = to.z;
        const float hl = std::sqrt(hx * hx + hz * hz), tl = std::sqrt(tx * tx + tz * tz);
        if (hl > 0) { hx /= hl; hz /= hl; }
        if (tl > 0) { tx /= tl; tz /= tl; }
        if (0.2f > tx * hx + 0.0f + tz * hz) facing = false;
    }
    if (kGone > a.f(0x880)) {                                       // defeated: stand still
        a.action() = 2; a.steer() = 0; a.hit_cut = true;
        return;
    }
    switch (a.phase()) {
        case 0: {                                                   // ---- INIT
            ai::init(a, p, e);                                      // FUN_004782f0 + EI[73] +0x90..
            // 0x4e8335: FUN_004202e0(CI[+0x124]+0x60 - 0xe, level = +0xc8): the script's Chr_SetStatus level.
            if (e.world) e.world->restat(A, A.cast_id, A.level);
            a.i(0x70c) = 0;
            a.i(0x728) = 0;
            a.set_v(0x808, e.self);
            a.i(0x72c) = 0; a.i(0x730) = 0;
            a.f(0x880) = 0.0f;
            a.phase() = 4;
            a.f(0x7ac) = a.f(0x814);                                // the turn rate, restored after moves
            a.f(0x7b0) = A.def;                                     // DEF, restored after the 300-chain
            a.dur() = 30; a.action() = 2; a.steer() = 0;
            A.flags130 |= 1u;
            A.flags12c = (A.flags12c & ~0x40u) | 0x1090000u;
            A.kind = 0x88; a.hittable = true;                       // +0x60 = 0x88: a hittable monster
            return;
        }
        case 1: ai::acquire(a); [[fallthrough]];
        case 2: a.phase() = 4; [[fallthrough]];
        case 4: break;
        default: return;
    }

    // ---- MAIN (0x4e844d)
    int S = e.section;
    auto req = [&](int id) { if (ai::request(S, id, false, e)) { out.section = id; out.section_force = false; } };
    auto force = [&](int id) { S = id; out.section = id; out.section_force = true; };
    auto play = [&](int id) { if (byte_at(a, 0x7bd) != 0) force(id); else req(id); };
    const int K = d < 0 ? 3 : (d < 1 ? 2 : 1);                     // hits before it reacts
    int& sub = a.i(0x72c);
    int& seq = a.i(0x730);
    if (e.took_damage && (sub != 0x1e || std::uint32_t(seq) < 10u)) a.i(0x7c4) += 1;
    if (a.i(0x7a8) != 0) a.i(0x7a8) -= 1;
    auto rand_dur = [&](int base) {                                 // byte +0x7bc ? R%5 + base : 0
        return byte_at(a, 0x7bc) != 0 ? int(r15(e) % 5u) + base : 0;
    };
    auto back = [&] { sub = a.i(0x79c); };                          // 0x4e95c3
    auto approach = [&] { a.action() = 0x400; a.steer() = 0; a.dur() = 15; };
    auto to_chain = [&](int s730) {                                 // into the 0x1e chain at s730
        seq = s730; a.i(0x7c4) = 0; a.i(0x7bc) = 0; a.i(0x7c0) = 0; sub = 0x1e; a.i(0x79c) = 0x1e;
    };
    switch (sub) {
        case 0: seq = 0; sub = 0x14; break;
        case 0x14: {                                                // decide
            if (a.dur() != 0) break;
            a.f(0x814) = a.f(0x7ac);
            if (seq != 0) break;
            if (pct <= 50 && a.i(0x7a8) == 0) {                     // the 300-chain, once per 1200
                a.i(0x7a8) = 1200;
                to_chain(300);
                break;
            }
            if (std::uint32_t(a.i(0x7c4)) >= std::uint32_t(K)) {    // hit K times: guard or back off
                const int packed = d < 0 ? 0x006e5a05 : (d < 2 ? 0x006e5a03 : 0x006e5a02);
                if (int(r15(e) % 100u) >= 33) { to_chain(200); break; }
                sub = 0x8c; seq = 0;
                a.i(0x7bc) = packed;                                // [counter sub, ticks, hits]
                a.i(0x79c) = 0x14;
                set_byte(a, 0x8a4, 0);
                a.i(0x7c0) = 0x101;
                a.i(0x7c4) = 0;
            }
            if (!facing) { a.action() = 0x400; a.steer() = 0; a.dur() = 10; break; }
            bool settled = std::uint32_t(sub) >= 100u || std::uint32_t(seq) >= 100u;
            if (!settled) {
                const std::uint32_t R = e.rng();
                auto pick = [&](int s) {                            // 0x4e86a9 / 0x4e86af / 0x4e87ae
                    sub = s; seq = 0; a.i(0x79c) = 0x14; a.i(0x7bc) = 1; a.i(0x7c0) = 0; set_byte(a, 0x8a4, 0);
                };
                if (pct > 50) {
                    if (dist > 3.5f) {
                        if (int((R >> 17) % 100u) < 75) { approach(); break; }
                        int s = 0x78;
                        if (pct < 70 && int(r15(e) % 100u) < 20) s = 0x6e;
                        pick(s);
                    } else if (R & 0x20000u) {
                        pick(100);
                    } else {
                        int s = 0x82;
                        if (d > 0 && (e.rng() & 0x20000u)) s = 100;
                        pick(s);
                    }
                } else if (dist > 3.5f) {
                    if (int((R >> 17) % 100u) < 75) { approach(); break; }
                    pick(int(r15(e) % 100u) >= 33 ? 0x78 : 0x6e);
                } else {
                    const int m = int((R >> 17) % 3u);
                    if (m == 0) pick(d <= 0 ? 0x82 : 100);
                    else if (m == 1) pick(100);
                    else { pick(0xa0); a.i(0x7bc) = 2; }
                }
            }
            if (std::uint32_t(seq) >= 100u) {                       // 0x4e87bf (+0x730 kept)
                a.i(0x7c4) = 0; a.i(0x7bc) = 0; a.i(0x7c0) = 0; sub = 0x1e; a.i(0x79c) = 0x1e;
                break;
            }
            if (std::uint32_t(sub) >= 100u) break;
            approach();
            break;
        }
        case 0x1e: {                                                // the scripted chain (+0x730)
            const int v = seq;
            if (std::uint32_t(v - 300) >= 10u && std::uint32_t(a.i(0x7c4)) >= std::uint32_t(K)) {
                sub = 0x14; seq = 0;
                break;
            }
            if (a.dur() != 0) break;
            if (v > 300) {
                switch (v - 301) {
                    case 0: {                                       // the charge-up
                        out.effects.push_back({509, {e.self.x, e.self.y - 0.5f, e.self.z}, 1.0f, false});
                        AiEffect aura{39, e.self, 1.0f, true};
                        aura.tag = 0;                               // +0x88c
                        out.effects.push_back(aura);
                        a.i(0x88c) = 1;                             // [H] stands in for the effect pointer
                        seq = v + 1; sub = 0x96; a.i(0x7bc) = 0x15a; a.i(0x7c0) = 0;
                        break;
                    }
                    case 1: case 2: case 3:
                        sub = 100; seq = v + 1; a.i(0x7bc) = 0x10100; a.i(0x7c0) = 0;
                        break;
                    case 4:
                        sub = 0x6e; seq = v + 1; a.i(0x7bc) = 0x30101; a.i(0x7c0) = 0;
                        break;
                    case 5:
                        if (a.i(0x88c) != 0) {
                            AiEffect k; k.kill_tag = 0; out.effects.push_back(k);
                            a.i(0x88c) = 0;
                        }
                        a.i(0x7c4) = 0;
                        A.def = a.f(0x7b0);
                        seq = 0;
                        break;
                    default: break;
                }
                break;
            }
            if (v == 300) {                                         // dodge back, DEF + 6
                sub = 0xa0; seq = 301; a.i(0x7bc) = 0; a.i(0x7c0) = 0;
                A.def = a.f(0x7b0) + 6.0f;
                break;
            }
            if (std::uint32_t(v) > 0xc9u) break;
            switch (v) {
                case 0:
                    if (e.rng() & 0x20000u) {
                        sub = 0xa0; seq = v + 1;
                        a.i(0x7bc) = (5.0f <= dist) + 1;
                        a.i(0x7c0) = 0;
                    } else {
                        seq = v + 1;
                    }
                    break;
                case 1: a.action() = 2; a.steer() = 0; a.dur() = 30 - 5 * d; seq = v + 1; break;
                case 2: sub = 0x14; seq = 0; break;
                case 0x64: case 0x6f: sub = 0xa0; seq = v + 1; a.i(0x7bc) = 0; a.i(0x7c0) = 0; break;
                case 0x6e: sub = 100; seq = v + 1; a.i(0x7bc) = 0; a.i(0x7c0) = 0; break;
                case 0x70: sub = 0x6e; seq = v + 1; a.i(0x7bc) = 0; a.i(0x7c0) = 0; break;
                case 0x65: case 0x71: sub = 0x78; seq = 0; a.i(0x7bc) = 1; a.i(0x7c0) = 0; break;
                case 0x78: sub = 0x78; seq = v + 1; a.i(0x7bc) = 1; a.i(0x7c0) = 0; break;
                case 0x79: sub = 0xa0; seq = 2; a.i(0x7bc) = 1; a.i(0x7c0) = 0; break;
                case 0xc8: sub = 0xa0; seq = v + 1; a.i(0x7bc) = 1; a.i(0x7c0) = 0; break;
                case 0xc9: {
                    const int m = int(r15(e) % 3u);
                    if (m == 0) { sub = 0x78; seq = 2; a.i(0x7bc) = 0x101; a.i(0x7c0) = 0; }
                    else if (m == 1) { sub = 100; seq = 2; a.i(0x7bc) = 0x101; a.i(0x7c0) = 0; }
                    else { sub = 0x6e; seq = 2; a.i(0x7bc) = 0x10101; a.i(0x7c0) = 0; }
                    break;
                }
                default: break;
            }
            break;
        }
        case 100: {                                                 // section 7, the thrust
            play(7);
            a.action() = 2;
            float t = a.f(0x7ac);
            if (byte_at(a, 0x7be) == 0) t *= fd * 0.11f + 0.33f;
            a.f(0x814) = t;
            sub += 1; a.dur() = -1;
            break;
        }
        case 0x65:
            if (S == 7) break;
            a.action() = 1;
            a.dur() = byte_at(a, 0x7bc) != 0 ? int(r15(e) % 5u) + (8 - d) * 4 : 0;
            sub += 1;
            [[fallthrough]];
        case 0x66:
            if (a.dur() != 0) break;
            a.f(0x814) = a.f(0x7ac);
            back();
            break;
        case 0x6e: {                                                // section 8, the slam
            play(8);
            a.action() = 2; a.dur() = -1;
            int n = byte_at(a, 0x7be);
            if (d == 1) n += 1;
            else if (d >= 2) n += 2;
            set_byte(a, 0x7c0, n);                                  // repeats
            sub += 1;
            break;
        }
        case 0x6f:
            if (S == 8) {
                int m = e.aso_marker;
                if (m == 1) {
                    if (byte_at(a, 0x8a4) != 0) break;
                    set_byte(a, 0x8a4, 1);
                    a.action() = 1;                                 // [not modelled] the camera shake
                    push1(A, a.heading + 0.0f, fd * 0.1f + 0.22f);  // FUN_0047aa00
                }
                if (m == 2 && byte_at(a, 0x8a4) != 0 && byte_at(a, 0x7c0) != 0) {
                    set_byte(a, 0x7c0, byte_at(a, 0x7c0) - 1);
                    force(8);
                    a.action() = 2;
                    set_byte(a, 0x8a4, 0);
                }
                break;
            }
            a.action() = 1;
            a.dur() = rand_dur(42 - 4 * d);
            sub += 1;
            if (a.dur() == 0) back();
            break;
        case 0x70: case 0x7a: case 0x84:
            if (a.dur() == 0) back();
            break;
        case 0x78:                                                  // section 300
            play(300);
            a.action() = 2;
            sub += 1; a.dur() = -1;
            break;
        case 0x79:
            if (S == 300) { a.action() = int(~std::uint32_t(e.aso_marker) & 1u) + 1; break; }
            a.action() = 1;
            a.dur() = byte_at(a, 0x7bc) != 0 ? int(r15(e) % 5u) + (8 - d) * 4 : 0;
            sub += 1;
            if (a.dur() == 0) back();
            break;
        case 0x82:                                                  // section 301, the dash
            play(0x12d);
            a.action() = 1;
            sub += 1; a.dur() = -1;
            break;
        case 0x83:
            if (S == 0x12d) break;
            a.action() = 1;
            a.dur() = rand_dur(22 - 4 * d);
            sub += 1;
            if (a.dur() == 0) back();
            break;
        case 0x8c:                                                  // the guard (section 6)
            if (a.i(0x7a0) == 0) {
                a.i(0x7a0) = a.i(0x7bc);
                set_byte(a, 0x7a3, (a.i(0x7bc) >> 8) & 0xff);       // ticks left = the reset value
                a.i(0x7a4) = a.i(0x7c0);
                force(6);
                a.action() = 2; a.dur() = -1;
            }
            sub = 0x8d;
            [[fallthrough]];
        case 0x8d: {
            const std::uint32_t v = std::uint32_t(a.i(0x7a0));
            if (v == 0) { a.dur() = 0; back(); break; }
            int hits = int(v & 0xff), reset = int((v >> 8) & 0xff), next = int((v >> 16) & 0xff);
            int left = int(v >> 24);
            if (S != 6) { a.i(0x7a0) = 0; break; }
            bool live = true;
            if (e.was_hit) {
                left = reset;
                if (hits != 0 && --hits == 0) {                     // broken: counter
                    req(2);
                    sub = next; a.i(0x7bc) = a.i(0x7a4); a.i(0x7c0) = 0; a.i(0x7a0) = 0;
                    live = false;
                }
            } else {
                left -= 1;
            }
            if (left == 0) {
                a.i(0x7a0) = 0; a.dur() = 0; sub = a.i(0x79c);
                req(2);
            } else if (live) {
                a.i(0x7a0) = std::int32_t((std::uint32_t(left) << 24) | (std::uint32_t(next) << 16) |
                                          (std::uint32_t(reset) << 8) | std::uint32_t(hits & 0xff));
            }
            break;
        }
        case 0x96:                                                  // section 305, the charge-up
            play(0x131);
            a.action() = 2;
            sub += 1;
            a.dur() = byte_at(a, 0x7bc);
            break;
        case 0x97:
            if (S == 0x131 && a.dur() != 0) break;
            a.action() = 1;
            if (byte_at(a, 0x7be) != 0) force(2);
            back();
            break;
        case 0xa0: {                                                // the jump-dodge
            const float th = float(std::atan2(double(a.f(0x73c)), double(a.f(0x734))));
            const int m = a.i(0x7bc) & 0xff;
            float T[5];
            if (m == 0) {                                           // away
                const float t0[5] = {kPi, k120, -k120, kHalfPi, -kHalfPi};
                for (int k = 0; k < 5; ++k) T[k] = t0[k];
                if (th > a.heading) for (int k = 1; k < 5; ++k) T[k] *= -1.0f;
            } else if (m == 1) {                                    // aside
                const float t0[5] = {kHalfPi, -kHalfPi, k120, -k120, kPi};
                for (int k = 0; k < 5; ++k) T[k] = t0[k];
                if (th > a.heading) { T[0] = -kHalfPi; T[1] = kHalfPi; T[2] = -k120; T[3] = k120; }
            } else {                                                // toward (m == 2; the engine has no other case)
                const float t0[5] = {0.0f, k60, -k60, kHalfPi, -kHalfPi};
                for (int k = 0; k < 5; ++k) T[k] = t0[k];
                if (a.heading > th) { T[0] = -0.0f; T[1] = -k60; T[2] = k60; T[3] = -kHalfPi; }
            }
            int k = 0;
            for (; k < 5; ++k) {
                const double ang = double(T[k] + th);
                const Vec3 dl{float(std::cos(ang)) * 4.0f, 0.0f, float(std::sin(ang)) * 4.0f};
                const bool hit = e.world ? e.world->ray(e.self, dl).hit : false;
                if (hit) continue;
                force(3);
                const float s = fd * 0.1f + 0.5f;
                A.mover.k1.x += dl.x * 0.25f * s;
                A.mover.k1.y += dl.y * 0.25f * s;
                A.mover.k1.z += dl.z * 0.25f * s;
                jump(A, 0.15f, e.jump_v0, out);                    // FUN_00449460(&{0.15})
                out.effects.push_back({191, {e.self.x, e.self.y - 0.9f, e.self.z}, 1.0f, false});
                break;
            }
            a.dur() = k >= 5 ? 0 : 30;
            a.action() = 2;
            back();
            break;
        }
        default: break;
    }

    // ---- the tail (0x4e95cf)
    if (a.phase() == 4) {
        ai::steer(a, e);
        ai::turn(a, a.f(0x814));
        if (A.flyer) ai::hover(a, A, e, nullptr, a.i(0x720));
        ai::act(a, p, e, &out);
        if ((std::uint32_t(sub) >= 100u || sub == 0x1e) && std::uint32_t(seq - 300) < 10u)
            out.vm_step = true;                                     // the 300-chain at double speed
    }
}

}  // namespace

void m830_dead(Actor& A, AiWorld& w) {
    EnemyAi& a = A.ai;
    if (w.flag(130, 1) == 0) { A.hp = 1.0f; return; }               // the puppet path (0x4e987f)
    // The live path with HP < 1 (0x4e98bd).
    if (Actor* P = w.player(); P && !P->alive()) {                  // the player lost: Galleon stays up
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
    if (a.i(0x88c) != 0) { a.hook_kill_tags |= 1u; a.i(0x88c) = 0; }  // the charge-up aura
    w.hide_boss_gauge();                                            // DAT_006d5fd8 = 0
    a.f(0x814) = a.f(0x7ac);
    a.hook_section = 5;                                             // FUN_00446550(5, 0) unless current
    a.action() = 1;
    A.flags12c |= 0x40u;
    a.hit_cut = true;                                               // +0x118 = 0
}

AiOut m830(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    if (!e.actor) return {};
    Actor& A = *e.actor;
    // ---- the handler FUN_004e9870
    if (gflag(e, 130, 1) == 0) {                                    // the script puppet
        if (A.hp < 1.0f) A.hp = 1.0f;
        return still(a);
    }
    // (the dead branch is m830_dead; the off-screen freeze is enemy_ai_tick's)
    if (gflag(e, 131, 0) != 0) return still(a);                     // FUN_0045f470 only
    AiOut out;
    body(a, p, e, A, out);
    if (!e.target_alive) { a.hittable = false; A.def = 999.0f; }    // FUN_0047aa80
    // The gauge (0x4e9c54): while not defeated, push on every HP change; lenScale 3000.
    if (!(kGone > a.f(0x880)) && a.f(0x880) != A.hp) {
        a.f(0x880) = A.hp;
        out.gauge_hp = A.hp; out.gauge_len = 3000.0f;
    }
    return out;
}

}  // namespace cleria::brains
