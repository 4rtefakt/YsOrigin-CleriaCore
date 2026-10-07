// M970 Vagully — handler FUN_004cc070 (AI index 90) dispatches on phase: 4 -> brain B
// FUN_004ca830 (a swarm bat owned by an M460 leader), anything else -> brain A
// FUN_004ca1a0 (a lone, room-placed bat). Spec: build/ai_spec/M970.md.
//
// Brain A: always runs (0x800) at 0.04 x 2.0. Under 2 u it steers away; from 2 to 4 u, with
// no flutter timer, it re-rolls its steer vector every tick (+/-135 deg random walk, 1 in 5
// "away"); beyond 4 it chases. Every 15th tick, off cooldown and level with the target
// (|dy| < 1, not flying high): 30% to request section 7 if within 6 u and +/-54 deg. Every
// (e+16)*15 ticks (e = -M100's d) outside section 7 it flips a coin between hover 1.0 and
// flying high at 3.0.
// Brain B (phase 4, set only by the M460 spawner): fade in, then every 10 ticks a new
// waypoint — orbiting the leader in its formation slot, or swarming the player while
// agitated (+0x7c4) — at run speed; a hand-armed bite in section 8; repelled by the
// player's skill casts; docks back into formation on the leader's recall (+0x7c0 1 -> 2)
// and dissolves on its order (3). Undamageable: DEF 999, no min-1 chip.
// H4, the phase-4 box clamp, is enemy_handler_clamp's (registry.cpp).
// [not modelled] the keep-out box restore (FUN_0042dc90); H1/H3 handler extras (the
// phase-4 room-count bit, the dust-puff clear); the Hugo-only repel
// trigger (player +0x7c4/+0x7bc, unidentified); the second impulse pair +0x344/+0x34c.
#include <algorithm>
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {
namespace {

AiOut brain_a(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // +0x4d0: nothing, not even the tail
    const int ev = -difficulty(e);
    a.i(0x7a0) += 1;
    if (a.i(0x7a8) != 0) a.i(0x7a8) -= 1;
    const bool level = std::fabs(double(e.self.y) - double(e.target.y)) < 1.0 && a.i(0x79c) == 0;

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            a.i(0x79c) = 0; a.i(0x7a0) = 0; a.i(0x7a4) = 0; a.i(0x7a8) = 0;
            a.f(0x7ac) = a.f(0x834);                               // EI hover altitude (1.0)
            a.i(0x70c) = 0;
            break;
        case 1:
            ai::idle(a, p, e);
            break;
        case 2: {
            if (a.dist() > 20.0f) { a.steer() = 0; ai::disengage(a); break; }
            a.action() = 0x800;                                    // always run
            if (e.section == 7) {
                a.steer() = 0;
                a.action() = e.turnable ? 0x800 : 1;
                break;                                             // no attack roll
            }
            if (a.steer() == 0 && a.dist() < 2.0f) {               // too close: back off
                a.steer() = 3;
                a.i(0x7a8) = int(r15(e) % 30u) + 15;
            } else if (a.dist() > 4.0f) {
                a.steer() = 0; a.i(0x7a8) = 0;
            } else if (a.i(0x7a8) == 0) {                          // flutter
                const std::uint32_t k = r15(e) % 5u;
                a.steer() = k != 0 ? 5 : 3;
                if (k != 0) {
                    const float a0 = float(std::atan2(double(a.f(0x750)), double(a.f(0x748))));
                    const float u = unit(e.rng());
                    const float ang = u * 4.712389f - 2.3561945f + a0;
                    a.f(0x748) = std::cos(ang); a.f(0x74c) = 0; a.f(0x750) = std::sin(ang);
                }
            }
            if (a.cool() == 0 && level && std::uint32_t(a.i(0x87c)) % 15u == 0) {
                if (r15(e) % 1000u < 300u && a.dist() < 6.0f) {
                    const Vec3 to = a.v(0x734);
                    const float ang = float(std::atan2(double(to.z), double(to.x)));
                    if (std::fabs(ai::angle_diff(a.heading, ang)) < 0.9424778) {
                        out.section = 7;                           // gated request
                        out.attack = true;
                    }
                }
            }
            break;
        }
        default:
            break;
    }
    // Tail, every phase.
    if (e.section != 7 && std::uint32_t(a.i(0x7a0)) > std::uint32_t((ev + 16) * 15)) {
        a.i(0x79c) = int(r15(e) & 1u);                             // fly high?
        a.i(0x7a0) = 0;
    }
    a.f(0x834) = a.i(0x79c) != 0 ? 3.0f : a.f(0x7ac);
    return out;
}

AiOut brain_b(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;
    Actor* self = e.actor;
    AiWorld* w = e.world;
    if (!self || !w) return out;
    const int d = difficulty(e);
    Actor* L = w->find(a.i(0x854));
    const Vec3 Lo = L ? Vec3{L->pos.x, L->pos.y + L->height, L->pos.z} : e.self;
    self->def = 999.0f;                                            // +0xa8
    self->no_min_damage = true;                                    // +0xce
    // The repel timer +0x79c.
    if (e.target_section == 0x10d) {
        const std::uint32_t v = std::uint32_t(40 - 2 * d);
        a.i(0x79c) = int(std::max(std::uint32_t(a.i(0x7c4)), v));   // reads +0x7c4, literally
    } else if (e.target_section == 0x110) {
        const std::uint32_t v = std::uint32_t((d <= 0 ? -2 : -10) * d + 120);
        a.i(0x79c) = int(std::max(std::uint32_t(a.i(0x79c)), v));
    }
    if (a.i(0x79c) != 0) a.i(0x79c) -= 1;
    if (L && self->pos.y < L->floor_y) self->pos.y = L->floor_y;  // feet above the leader floor
    if (a.i(0x79c) > 1 && a.dist() < 3.0f) {                       // repel from the player
        Vec3 v{-a.f(0x734), 0, -a.f(0x73c)};
        const float l = std::sqrt(v.x * v.x + v.z * v.z);
        if (l > 0) { v.x /= l; v.z /= l; } else { v = {}; }
        const float k = std::max(1.0f, 9.0f / std::max(0.75f, a.dist() * a.dist()));
        self->mover.k1.x += v.x * 0.04f * k;                       // +0x338 / +0x340
        self->mover.k1.z += v.z * 0.04f * k;
        self->mover.k2.x += 2 * (v.x * 0.04f) * k;                 // +0x344 / +0x34c
        self->mover.k2.z += 2 * (v.z * 0.04f) * k;
    }
    // The bite, armed by hand in section 8 while no attacker holds a re-hit timer on the
    // player. [STAND-IN] that bank (P+0x90) is read as "the player's i-frames are clear".
    if (e.section == 8 && e.target_rehit_clear) {
        AiHitbox h;
        h.radius = 0.55f; h.fwd = 0.5f; h.life = 2; h.rehit = 64 - std::max(0, 12 * d);
        h.rtype = 0; h.power = -12.0f; h.knock = 1.0f;
        out.hitboxes.push_back(h);
    }
    const bool hugo = e.character == 2;
    int& sub = a.i(0x728);
    auto unit_r = [&] { return unit(e.rng()); };
    auto slot = [&] {                                              // the formation slot round L
        const float rho = std::sqrt(a.f(0x7f0) * a.f(0x7f0) + a.f(0x7f8) * a.f(0x7f8));
        const float ang = float(std::atan2(double(a.f(0x7f8)), double(a.f(0x7f0)))) +
                          float(e.sprite_dir + 2) * 45.0f * 0.017453292f;
        return Vec3{std::cos(ang) * rho, 0.0f, std::sin(ang) * rho};
    };
    switch (sub) {
        case 0: {                                                  // wait / spawn stagger
            a.steer() = 0; a.action() = 1; a.alpha = 0.0f;
            if (a.i(0x7bc) == 0) sub = 1; else a.i(0x7bc) -= 1;
            const float lo = 0.4f * float(d) + 2.5f;
            const float hi = (d > 0 ? float(d) : 0.5f * float(d)) + 5.0f;
            a.f(0x7cc) = 9999.0f;
            a.f(0x7d0) = (hi - lo) * unit_r() + lo;               // the personal radius r
            break;
        }
        case 1:                                                    // fade in
            if (a.alpha < 1.0f) { a.alpha = std::min(a.alpha + 0.04f, 1.0f); break; }
            sub = 10;
            a.i(0x87c) = int(r15(e) % 10u);
            {
                const float uB = unit_r(), uC = unit_r();
                a.set_v(0x894, {Lo.x + (uC * 4.0f - 2.0f), Lo.y, Lo.z + (uB * 4.0f - 2.0f)});
            }
            break;
        case 10: {                                                 // swarm
            if (e.section != 8 && e.section != 4) out.section = 8;
            if (std::uint32_t(a.i(0x87c)) % 10u == 0) {
                const float r = a.f(0x7d0);
                if (a.i(0x7c4) != 0) {                             // agitated: round the target
                    const float u1 = unit_r(), u2 = unit_r();
                    a.set_v(0x894, {e.target.x + (2 * r * u2 - r), e.target.y, e.target.z + (2 * r * u1 - r)});
                    a.i(0x7c4) -= 1;
                    if (r15(e) % 5u == 0) {                        // spread it to the siblings
                        const int mine = a.i(0x7c4), lead = a.i(0x854);
                        w->for_each([&](Actor& s) {
                            if (s.cast_id == 0xb7 && s.ai.i(0x854) == lead)
                                s.ai.i(0x7c4) = int(std::max(std::uint32_t(s.ai.i(0x7c4)), std::uint32_t(mine)));
                        });
                    }
                } else {                                           // orbit the leader
                    const float u1 = unit_r(), u2 = unit_r();
                    const Vec3 s = slot();
                    a.set_v(0x894, {Lo.x + (2 * r * u2 - r) + s.x, Lo.y, Lo.z + (2 * r * u1 - r) + s.z});
                }
                // Altitude drift. The clamps are macro-expanded, so each re-evaluation draws.
                auto Rv = [&] { return (0.4f * r - (-0.4f * r)) * unit_r() + (-0.4f * r) + a.f(0x7f4) - a.f(0x834); };
                auto m = [&] { const float v = Rv(); return 0.5f > v ? Rv() : 0.5f; };
                auto c = [&] { const float v = m(); return -0.5f > v ? -0.5f : m(); };
                const float t = a.f(0x834) + c();
                a.f(0x834) = 1.0f > t ? 1.0f : a.f(0x834) + c();
            }
            if (a.i(0x7c4) == 0) {
                if (int(r15(e) % 1000u) < 10 * d + 30 && a.cool() == 0) {
                    const float dx = e.target.x - e.self.x, dz = e.target.z - e.self.z;
                    const float lim = 2.0f * float(d) + 8.0f;
                    if (dx * dx + dz * dz < lim * lim &&
                        std::fabs(ai::angle_diff(a.heading, float(std::atan2(double(dz), double(dx))))) < 1.5707964) {
                        const int lead = a.i(0x854);
                        w->for_each([&](Actor& s) {                 // writes SELF once per sibling
                            if (s.cast_id == 0xb7 && s.ai.i(0x854) == lead) a.i(0x7c4) = 2 * d + 12;
                        });
                    }
                }
            }
            a.steer() = 4; a.action() = 0x800;
            break;
        }
        case 0x14: {                                               // return to formation / dock
            a.f(0x834) = a.f(0x7f4);
            const Vec3 s = slot();
            a.set_v(0x894, {Lo.x + s.x, Lo.y, Lo.z + s.z});
            a.steer() = 4; a.action() = 0x800;
            const float wx = a.f(0x894) - e.self.x, wz = a.f(0x89c) - e.self.z;
            const float dist = std::sqrt(wx * wx + wz * wz);
            bool arrived = false;
            if (std::uint32_t(a.i(0x7c0)) < 2u && std::uint32_t(a.i(0x87c)) > 30u) {
                a.f(0x814) = std::min(a.f(0x814) + 0.01f, 10.0f);  // turn-rate ramp
                if (dist < 2.0f) {
                    if (e.section == 8) { out.section = 1; out.section_force = true; }
                    a.set_v(0x748, {wx, a.f(0x898) - e.self.y, wz});
                    a.heading = float(std::atan2(double(a.f(0x750)), double(a.f(0x748))));
                    out.snap_facing = true;                        // +0x308 = +0x314
                    a.action() = 0x20;                             // walk
                    a.f(0x818) = swarm_speed(d, hugo) * 0.5f;
                    const float best = a.f(0x7cc);
                    if (dist < 0.1f || (best < 0.2f && dist > best)) arrived = true;
                    float nb = std::min(dist, best);
                    a.f(0x7cc) = nb;
                    if (dist > nb) {                               // overshot: pull in
                        nb *= 0.75f;
                        a.f(0x7cc) = nb;
                        self->pos.x = a.f(0x894) - std::cos(a.heading) * nb;
                        self->pos.z = a.f(0x89c) - std::sin(a.heading) * nb;
                        self->mover.k1 = {}; self->mover.k2 = {};
                        self->mover.vx = self->mover.vz = 0;
                    }
                }
            }
            if (std::uint32_t(a.i(0x7c0)) < 2u && (arrived || dist < 0.1f)) {   // docked
                a.i(0x7c0) = 2; a.steer() = 5; a.action() = 2;
                if (L) a.set_v(0x748, L->ai.v(0x748));
            } else if (a.i(0x7c0) == 3) {                          // dissolve into the leader
                a.steer() = 5; a.action() = 2;
                if (a.alpha > 0.0f) a.alpha = std::max(0.0f, a.alpha - 0.025f);
                else w->remove(*self);
            } else if (a.i(0x7c0) == 2) {
                a.steer() = 5; a.action() = 2;
            }
            break;
        }
        default:
            break;
    }
    // Tail (0x4cbf64).
    if (a.i(0x7c0) == 1) sub = 0x14;
    else if (sub != 0 && sub != 0x14) a.hittable = a.alpha >= 0.5f;
    ai::steer(a, e);
    if (a.turn_hold() == 0)
        a.heading = rotate_toward(a.heading, float(std::atan2(double(a.f(0x750)), double(a.f(0x748)))), a.f(0x814));
    else
        a.turn_hold() -= 1;
    if (self->flyer) ai::hover(a, *self, e, L, a.i(0x720));        // the leader is the reference
    ai::act(a, p, e);
    return out;
}

}  // namespace

AiOut m970(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    if (a.phase() == 4) return brain_b(a, p, e);
    return brain_a(a, p, e);
}

}  // namespace cleria::brains
