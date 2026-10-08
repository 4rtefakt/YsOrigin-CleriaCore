// M570 Zarue — brain FUN_004f3730 (AI index 50; its revive handler FUN_004f4690 is
// src/cleria/brains/revive.cpp). Spec: build/ai_spec/M570.md, REVIVE.md; its hands: M410.md.
// M430 Zeruena (brain FUN_004b27e0, AI index 36, handler FUN_004b3970) is the same template:
// build/ai_spec/M430.md (inside 7 u it raises a barrier — section 6 for 120 ticks, the turn rate x
// (0.1d + 0.3), a cooldown of 165 - 15d + R%30 — and flees only when high with room behind it; its
// hands are cast 0x30, M440).
//
// A hovering summoner (hover 1.0, "high" 3.2). Engaged it decides when its action runs out: beyond
// 9 u (horizontal) it runs in for 15 ticks; inside 4 it flees for 60 (or, already high over a
// grounded target with a wall 1 u behind it, summons), rolling (d + 2) x 15% (always when
// XSO-tagged) to go high; between 4 and 9, level enough, it summons: 10 ticks facing, then section
// 7 held 111 ticks; at Marker 2 it opens the hole (EF2 277 at its floor) and calls 2 hands (cast 45,
// 4 on Hard and up) 2.5 u out at +-54 deg (and 180 +- 54). While a hand of each side lives it holds
// section 300; a damaging hit sends it through section 4 and a 23-tick hit-free wait; when a side
// is gone it plays 0x12d and rests 75 + R%30 - 10d. A hit reaction drops it to the ground for 24
// ticks; a stun grounds it while engaged.
// [not modelled] the keep-out box restore (FUN_0042dc90); the handler's +0x3c0 &= ~1 (the landing
// puff request); the hole effect's actor-matrix placement (placed at the floor + 0.1, rotated by
// the facing [approx]).
// M670 Bazeraena (brain FUN_004f1050, AI index 60, handler FUN_004f24c0 = M430's + the +0x7b4 base-speed moves,
// revive.cpp) is M430 with other numbers (near 5 u, high +1.7, MID +0x7a0 = 0x6e), cast-0x1e hands (M260) 8.5 u
// out in 1 or 2 pairs, and a dash from the guard hold (section 302, sub 0x78..0x7a): build/ai_spec/M670.md.
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

namespace {

// M570 and M430 Zeruena (brain FUN_004b27e0, build/ai_spec/M430.md: the same template with the near
// branch inverted at 7 u, a barrier (section 6, sub 0x6e..0x70), cast-0x30 children 2/4/6 at
// heading - (a + 0.3)·pi).
enum class Kind { Z570, Z430, Z670 };

AiOut summoner(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e, Kind kind) {
    using ai::r15;
    const bool z670 = kind == Kind::Z670;
    const bool z430 = kind != Kind::Z570;                          // M670 runs M430's branches (M670.md)
    const int kChild = z670 ? 0x1e : z430 ? 0x30 : 0x2d;
    const float kHigh = z670 ? 1.7f : 2.2f;                        // 0x6bd500 / 0x6bd5c8
    AiOut out;
    if (e.launched) return out;                                    // 0x4f385b
    Actor* A = e.actor;
    const Vec3 to = a.v(0x734);
    const float pd = std::sqrt(to.x * to.x + 0.0f + to.z * to.z);  // the horizontal distance
    const int d = difficulty(e);
    bool can_atk = true;
    if (std::fabs(e.target.y - e.self.y) >= 6.0f && e.target_grounded) can_atk = false;   // 0x6bd880
    const bool tagged = e.group_tag;                               // +0x130 & 0x700
    bool child0 = false, child1 = false;                           // the last match of each side
    if (e.world && A) {
        e.world->for_each([&](Actor& c) {
            if (c.cast_id != kChild || c.ai.i(0x854) != A->instance_id) return;
            if (z670 && c.destroy_requested()) return;             // M670 0x4f12c4: byte +0x11 & 2
            (c.ai.i(0x7a0) == 0 ? child0 : child1) = true;
        });
    }
    const int S = e.section;
    auto force = [&](int s) { out.section = s; out.section_force = true; };   // inlined FUN_00446620
    auto gated = [&](int s) { out.section = s; out.section_force = false; };  // FUN_00446550(s, 0)

    switch (a.phase()) {
        case 0:                                                    // INIT (0x4f39d7)
            ai::init(a, p, e);
            a.i(0x7a8) = 0;
            a.f(0x7ac) = a.f(0x834);                               // normal hover height (1.0)
            a.f(0x7b0) = a.f(0x814);                               // turn rate (0.1)
            a.i(0x72c) = 0; a.i(0x730) = 0;
            if (z670) a.f(0x7b4) = a.f(0x818);                     // 0x4f1386: the base-speed backup
            return out;
        case 4: {                                                  // rebirth (0x4f3a7a)
            a.hittable = false;
            if (S == 10) return out;
            a.hittable = true;
            const int n = (a.i(0x7a8) != 0 ? a.i(0x7a8) : 60) - 1;
            a.i(0x7a8) = n;
            if (n != 0) return out;
            ai::disengage(a);
            a.f(0x814) = a.f(0x7b0);
            if (A) A->flyer = true;
            a.i(0x72c) = 0; a.i(0x730) = 0;
            return out;
        }
        case 3:                                                    // stun hold (0x4f3bb1)
            a.i(0x7a8) = 60;
            a.i(0x72c) = 0;
            if (A) A->flyer = false;
            return out;
        case 1:                                                    // unaware (0x4f3afa)
            ai::idle(a, p, e);
            a.i(0x72c) = 0; a.i(0x730) = 0;
            a.f(0x834) = a.f(0x7ac);
            a.f(0x814) = a.f(0x7b0);
            if (z670) a.f(0x818) = a.f(0x7b4);                     // 0x4f146c
            if (A) A->flyer = true;
            if (S == 300) force(0x12d);
            return out;
        case 2: break;
        default: return out;
    }

    // A. top (0x4f3bdf)
    if (a.dist() > 19.0f) ai::disengage(a);                        // the tick continues
    if (a.i(0x7a8) != 0) { a.i(0x7a8) -= 1; return out; }          // the post-stun lockout
    if (S == 4) {                                                  // a hit reaction: down for a while
        a.f(0x834) = a.f(0x7ac);
        a.i(0x7bc) = 24;
        if (A) A->flyer = false;
    }
    if (a.i(0x7bc) != 0 && --a.i(0x7bc) == 0 && A) A->flyer = true;
    if (std::uint32_t(a.i(0x72c)) > (z670 ? 0x7au : z430 ? 0x70u : 0x6du)) return out;
    std::uint8_t& latch = reinterpret_cast<std::uint8_t*>(a.blk.data())[0x8a4 - EnemyAi::kBase];   // byte +0x8a4
    switch (a.i(0x72c)) {
        case 0:
            a.f(0x814) = a.f(0x7b0);
            if (z670) {                                            // 0x4f15c5: never below the normal height
                a.f(0x818) = a.f(0x7b4);
                if (a.f(0x7ac) > a.f(0x834)) a.f(0x834) = a.f(0x7ac);
            }
            a.i(0x730) = 0;
            a.i(0x72c) = 0x14;
            return out;
        case 0x14: {                                               // decide
            if (a.dur() != 0 || a.i(0x730) != 0) return out;
            if (pd > 9.0f) {                                       // FAR: approach
                a.action() = 0x400; a.steer() = 0; a.dur() = 15;
                a.f(0x834) = a.f(0x7ac);
                return out;
            }
            if (z430 && (z670 ? 5.0f : 7.0f) > pd) {               // M430 NEAR (0x4b2d45; M670 5.0 0x6bd868): the barrier
                bool flee = false;                                 // 0x6bd8b8
                if (a.f(0x834) > a.f(0x7ac) + 0.1f && e.target_grounded && e.world) {
                    Vec3 v{e.self.x - e.target.x, 0.0f, e.self.z - e.target.z};
                    const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
                    v = l == 0.0f ? Vec3{} : Vec3{v.x * (1.0f / l), 0.0f, v.z * (1.0f / l)};
                    flee = !e.world->ray(e.self, v).hit;           // room behind it: flee
                }
                if (flee) { a.action() = 0x400; a.steer() = 3; a.dur() = 60; }
                else { a.i(0x72c) = 0x6e; a.i(0x730) = 0; a.i(0x79c) = 0; }   // +0x7a0 not written
                const int r = int(r15(e) % 100u);                  // ROLL (0x4b2e2b)
                if (r < (d + 2) * 15 || tagged) a.f(0x834) = a.f(0x7ac) + kHigh;
                return out;
            }
            if (!z430 && 4.0f > pd) {                              // NEAR (NaN goes to MID)
                bool cornered = false;
                if (a.f(0x834) > a.f(0x7ac) + 0.1f && e.target_grounded && e.world) {
                    Vec3 v{e.self.x - e.target.x, 0.0f, e.self.z - e.target.z};   // FUN_0042cc50
                    const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
                    v = l == 0.0f ? Vec3{} : Vec3{v.x * (1.0f / l), 0.0f, v.z * (1.0f / l)};
                    if (e.world->ray(e.self, v).hit) {             // a wall within 1 u behind it
                        a.i(0x72c) = 100; a.i(0x730) = 0; a.i(0x7a0) = 100; a.i(0x79c) = 0;
                        cornered = true;
                    }
                }
                if (!cornered) { a.action() = 0x400; a.steer() = 3; a.dur() = 60; }   // FLEE
                const int r = int(r15(e) % 100u);                  // ROLL
                if (r < (d + 2) * 15 || tagged) a.f(0x834) = a.f(0x7ac) + 2.2f;   // go HIGH (0x6bd5c8)
                return out;
            }
            if (!can_atk) return out;                              // MID
            a.f(0x834) = tagged ? a.f(0x7ac) + kHigh : a.f(0x7ac);
            a.i(0x72c) = 100; a.i(0x730) = 0; a.i(0x7a0) = z670 ? 0x6e : 100; a.i(0x79c) = 0;
            return out;
        }
        case 100:                                                  // wind-up
            a.action() = 2; a.steer() = 0; a.dur() = 10;
            a.i(0x72c) = 0x65;
            return out;
        case 0x65:
            if (a.dur() != 0) return out;
            gated(7); out.attack = true;                           // result ignored
            a.i(0x72c) = 0x66;
            a.action() = 1; a.dur() = 111;
            latch = 0;                                             // summon latch
            return out;
        case 0x66: {                                               // the summon section
            if (a.dur() == 0) {
                a.action() = 2; a.dur() = 60;
                gated(300);
                a.i(0x72c) = 0x67;
                return out;
            }
            if (e.aso_marker == 0) { a.action() = 2; a.steer() = 0; }
            else a.action() = 1;
            if (e.aso_marker == 2 && latch == 0 && A) {            // SUMMON (0x4f4014)
                AiEffect fx;                                       // enemy\m_430_ana.eft: the hole, no hit
                fx.id = 277;
                fx.pos = {e.self.x, A->floor_y + 0.1f, e.self.z};
                fx.rot = {0.0f, -1.5708f - e.facing, 0.0f};
                fx.power = 10.0f; fx.knock = 1.0f;
                out.effects.push_back(fx);
                if (e.world) {
                    // M670 0x4f1bc7: +0x7a0 == 0x6e (always, from MID) -> 1 + (d >= 1) pairs, 7.0 + 1.5 out, child
                    // +0x7b0 = 7.0 (0x6bd8b8, 0x6bd4d4); otherwise M430's count, 1.0 + 1.5.
                    const bool far6e = z670 && a.i(0x7a0) == 0x6e;
                    const float cdist = far6e ? 7.0f : 1.0f;
                    const float rad = z670 ? cdist + 1.5f : 2.5f;
                    const int n = far6e ? 1 + (d >= 1 ? 1 : 0)
                                : z430 ? (d + 2) / 2 + 1 : 1 + (d >= 1 ? 1 : 0);   // M430 0x4b32ad
                    const float step = 6.28318f / float(n);        // 0x6bd894
                    float an = 0.0f;
                    for (int k = 0; k < n; ++k) {
                        for (int side = 0; side < 2; ++side) {
                            // M430: both of a pair at heading - (a + 0.3)·pi (0x6bd27c, 0x6bd6a8), literal.
                            const float th = z430 ? a.heading - (an + 0.3f) * 3.14159f
                                           : side == 0 ? (an + 0.942477f) + a.heading : (an - 0.942477f) + a.heading;
                            const Vec3 dd{float(std::cos(double(th))) * rad + 0.0f, 0.0f,
                                          float(std::sin(double(th))) * rad + 0.0f};
                            Actor* c = e.world->spawn_child(*A, kChild, dd, A->level, /*keep_phase0*/true);
                            if (!c) continue;                      // (the engine does not check)
                            c->ai.i(0x854) = A->instance_id;
                            c->ai.i(0x7a0) = side;
                            c->ai.f(0x7ac) = an;
                            c->ai.i(0x7a4) = a.i(0x7a0);           // 100
                            c->ai.f(0x7b0) = z670 ? cdist : 1.0f;
                        }
                        an += step;
                    }
                }
                latch = 1;
            }
            return out;
        }
        case 0x67:                                                 // hold while both sides live
            a.action() = 2; a.dur() = 60;
            if (e.took_damage) {                                   // +0x3bc & 2
                gated(4);
                a.i(0x7a4) = 0;
                a.i(0x72c) = 0x6d;
                return out;
            }
            if (child0 && child1) {
                // M670 0x4f1e1d: the dash, inside 5 + 1.5d of the target while the guard loop (300) shows.
                if (z670 && float(d) * 1.5f + 5.0f > pd && S == 300) { a.i(0x72c) = 0x78; a.i(0x79c) = 0x67; }
                return out;
            }
            gated(0x12d);
            a.action() = 1;
            {
                const int r = int(r15(e) % 30u);
                a.i(0x72c) = 0x68;
                a.dur() = r - 10 * d + 75;
            }
            return out;
        case 0x68:                                                 // rest
            if (a.dur() == 0) a.i(0x72c) = z670 ? 0 : a.i(0x79c);  // M670 0x4f1f06: a literal 0
            return out;
        case 0x6d: {                                               // hit-free wait
            a.action() = 2; a.dur() = 60;
            const int n = e.took_damage ? 1 : a.i(0x7a4) + 1;
            a.i(0x7a4) = n;
            if (std::uint32_t(n) >= 23u) { force(300); a.i(0x72c) = 0x67; }
            return out;
        }
        case 0x6e:                                                 // the barrier (M430 0x4b36f5)
            a.action() = 2; a.steer() = 0; a.dur() = 0;
            a.i(0x72c) = 0x6f;
            [[fallthrough]];
        case 0x6f:                                                 // 0x4b371d
            if (a.dur() != 0) return out;
            gated(6);                                              // result ignored
            a.action() = 2; a.dur() = 120;
            {
                const int r = int(r15(e) % 30u);
                a.cool() = r + (-d + 3) * 15 + a.dur();            // r + 165 - 15d
            }
            a.f(0x814) = (float(d) * 0.1f + 0.3f) * a.f(0x7b0);    // 0x6bd19c, 0x6bd27c
            a.i(0x72c) += 1;
            [[fallthrough]];
        case 0x70:                                                 // 0x4b3860
            if (a.dur() == 0) { a.action() = 1; a.dur() = a.cool(); }
            if (a.cool() != 0) return out;
            a.i(0x72c) = a.i(0x79c);
            a.f(0x814) = a.f(0x7b0);
            return out;
        case 0x78:                                                 // M670 0x4f2126: the dash wind-up
            if (!z670) return out;
            gated(0x12e);                                          // result ignored
            a.i(0x72c) += 1;
            latch = 0;                                             // the push latch
            a.action() = 2;
            a.f(0x834) = a.f(0x7ac) * 0.5f;                        // 0x6bd304: half hover height
            a.dur() = 166;
            return out;
        case 0x79:                                                 // M670 0x4f2184
            if (!z670) return out;
            if (S == 0x12e && a.dur() != 0) {
                const int m = e.aso_marker;
                const float k = float(d) * 0.25f + 3.0f;           // 0x6bd244, 0x6bd680
                if (m == 0) {
                    a.action() = 2;
                    a.f(0x814) = a.f(0x7b0);
                } else if (m == 1) {                               // the charge
                    a.action() = 0x800; a.steer() = 0;
                    a.f(0x814) = a.f(0x7b0) * (d > 0 ? float(d) * 0.15f : 0.01f);   // 0x6bd1d8, 0x6bd084
                } else {
                    a.action() = 1;
                    a.f(0x814) = a.f(0x7b0);
                    if (latch == 0 && A) { push1(*A, a.heading + 0.0f, k * a.f(0x7b4)); latch = 1; }   // FUN_0047aa00
                }
                a.f(0x818) = m == 1 ? k * a.f(0x7b4) : a.f(0x7b4);
                return out;
            }
            a.action() = 1;                                        // EXIT (0x4f22d3)
            a.dur() = int(r15(e) % 30u) - 10 * d + 60;
            a.f(0x834) = a.f(0x7ac); a.f(0x818) = a.f(0x7b4); a.f(0x814) = a.f(0x7b0);
            force(300);
            a.i(0x72c) += 1;
            [[fallthrough]];
        case 0x7a:                                                 // M670 0x4f23be
            if (!z670) return out;
            if (a.dur() == 0) a.i(0x72c) = a.i(0x79c);
            return out;
        default: return out;
    }
}

}  // namespace

AiOut m570(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) { return summoner(a, p, e, Kind::Z570); }
AiOut m430(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) { return summoner(a, p, e, Kind::Z430); }
AiOut m670(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) { return summoner(a, p, e, Kind::Z670); }

}  // namespace cleria::brains
