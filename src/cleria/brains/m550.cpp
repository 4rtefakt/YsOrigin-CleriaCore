// M550 Arte-Baocar — brain FUN_0049c130 (handler FUN_0049d830 = M100's minus the arena
// clamp, as M390's; AI index 48). Spec: build/ai_spec/M550.md (its own program, not a
// Baocar diff; it shares M390's warp effects).
//
// A flying caster. Engaged it runs at the player beyond 8 u, walks within, stands within 4,
// pushing itself off inside 3 u (knock channel 2). Off cooldown: 2% a tick the warp-attack
// chain — three teleports to 1.5..8 u around the player (each a 40-tick fade out, a probed
// relocation, a 40-tick fade in, then a 40 - 10d wait) and the shot cast (section 8, 150
// ticks): at each marker a homing shot (kind 2, effect = the marker value, 310); above Normal
// a second, slower one on the first marker. Else 5% a tick, when another actor is below 30%
// HP, the heal cast (section 7): a heal shot (kind 0xb, effect 314) owned by the most damaged
// other actor, healing it 25..200 by difficulty on contact. Unaware it may warp around itself.
// While warping it stands, is Durable and, below alpha 0.5, unhittable; every other tick
// Durable is cleared. A coin flip on each relocation raises its hover height for good.
// [not modelled] the warp effects' attachment.
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m550(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;
    Actor* A = e.actor;
    const Vec3 T = e.target;
    const int d = difficulty(e);
    if (a.i(0x7a8) != 0) a.i(0x7a8) -= 1;
    auto norm = [](Vec3 v) {
        const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        return l == 0 ? Vec3{} : Vec3{v.x * (1.0f / l), v.y * (1.0f / l), v.z * (1.0f / l)};
    };

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            a.i(0x72c) = 0;
            a.set_v(0x7ac, e.self);
            a.f(0x7b8) = a.f(0x834);                               // the saved hover height
            a.i(0x79c) = 0; a.i(0x7a0) = 0; a.i(0x7a4) = 0; a.i(0x7a8) = 0; a.i(0x7bc) = 0;
            a.i(0x7c0) = 1;
            a.i(0x730) = 0;
            break;
        case 1:
            if (a.i(0x72c) != 0) break;
            a.action() = 0x20;
            ai::idle(a, p, e);
            if (a.i(0x7a8) == 0 && r15(e) % 1000u < 50u) { a.i(0x7a0) = 0; a.i(0x72c) = 1; }
            break;
        case 2: {
            if (a.dist() > 20.0f && a.i(0x72c) == 0) ai::disengage(a);   // the tick continues
            const float D = a.dist();
            a.steer() = 0;
            a.action() = 8.0f > D ? 0x20 : 0x800;
            if (4.0f > D) a.action() = 2;
            if (a.i(0x72c) != 0) break;
            int& st = a.i(0x730);
            switch (st) {
                case 0:
                    if (a.cool() == 0) {
                        if (r15(e) % 1000u < 20u) st = 1;
                        else if (r15(e) % 1000u < 50u && e.world && A) {
                            bool found = false;
                            e.world->for_each([&](Actor& o) {
                                if (found || o.cast_id == 0xd || &o == A) return;
                                if (o.max_hp * 0.3f > o.hp && o.hp >= 1.0f) { st = 20; found = true; }
                            });
                        }
                    }
                    if (3.0f > a.dist() && A) {                    // the close-range push
                        const Vec3 v = norm({e.self.x - T.x, e.self.y - T.y, e.self.z - T.z});
                        A->mover.k2.x += ((v.x * a.f(0x818)) * a.f(0x820)) * 0.3f;
                        A->mover.k2.y += ((v.y * a.f(0x818)) * a.f(0x820)) * 0.3f;
                        A->mover.k2.z += ((v.z * a.f(0x818)) * a.f(0x820)) * 0.3f;
                    }
                    break;
                case 1: case 2: case 3:                            // wait, then warp near the target
                    a.action() = 2;
                    if (a.i(0x7a8) == 0) { a.i(0x7a0) = 1; st += 1; a.i(0x72c) = 1; }
                    break;
                case 4:
                    a.action() = 2;
                    if (a.i(0x7a8) == 0) st = 10;
                    break;
                case 10:
                    st = 11;
                    out.section = 8; out.attack = true;            // gated; result ignored
                    a.action() = 2; a.dur() = 150;
                    a.cool() = 330 - 45 * d;
                    a.i(0x7c0) = 0;
                    break;
                case 11: {
                    a.action() = 2;
                    const int M = e.aso_marker;
                    if (e.section == 8 && M != 0) {
                        out.set_marker = 0;
                        Vec3 v = norm({T.x - e.self.x, T.y - e.self.y, T.z - e.self.z});
                        const float s = float(std::sin(double(a.heading))), c = float(std::cos(double(a.heading)));
                        if (0.1f > c * v.x + v.y * 0.0f + s * v.z) {   // > ~84 deg off: clamp to +/-81
                            const float an = float(std::atan2(double(v.z), double(v.x)));
                            const float dd = float(ai::angle_diff(a.heading, an));
                            const float ang = 0 > dd ? a.heading - 1.4137167f : a.heading + 1.4137167f;
                            const float L = float(std::sqrt(double(v.x * v.x + v.z + v.z)));   // [sic]
                            v.x = float(std::cos(double(ang))) * L; v.z = float(std::sin(double(ang))) * L;
                        }
                        AiBullet b;
                        b.kind = 2; b.effect = M;
                        b.pos = {e.self.x + c * 0.5f, e.self.y + 0.75f, e.self.z + s * 0.5f};
                        b.dir = v; b.speed = 0.2f; b.knock = 1.15f;
                        b.home_period = 1; b.home_rate = d >= 2 ? 0.0191986f : d >= 0 ? 0.01309f : 0.0f;
                        b.f168 = 1.0f; b.f16c = 0; b.f170 = 300.0f;
                        out.bullets.push_back(b);
                        if (d >= 1 && a.i(0x7c0) == 0) {
                            a.i(0x7c0) = 1;
                            b.speed = 0.1f; b.home_rate = d >= 2 ? 0.015708f : 0.0095993f;
                            out.bullets.push_back(b);
                        }
                    }
                    if (e.section != 8 || a.dur() == 0) {
                        st = 0;
                        const std::uint32_t cap = std::uint32_t(180 - 45 * d);
                        if (std::uint32_t(a.cool()) > cap) a.cool() = int(cap);
                    }
                    break;
                }
                case 20:
                    st = 21;
                    out.section = 7; out.attack = true;
                    a.action() = 1; a.dur() = 270;
                    a.cool() = 420 - 15 * d;
                    a.i(0x7bc) = 0;
                    break;
                case 21: {
                    a.action() = 1;
                    if (e.section == 7) {
                        const int M = e.aso_marker;
                        if (std::uint32_t(M) >= 5u) {
                            out.set_marker = 4;
                            float best = 1.0f; int owner = A ? A->instance_id : -1;
                            if (e.world)
                                e.world->for_each([&](Actor& o) {
                                    if (o.cast_id == 0xd || &o == A) return;
                                    if (o.max_hp > o.hp && o.hp >= 1.0f) {
                                        const float r = o.hp / o.max_hp;
                                        if (best > r) { best = r; owner = o.instance_id; }
                                    }
                                });
                            const int n = d < 0 ? 50 / -d : (d + 1) * 50;
                            const Vec3 dir{float(std::cos(double(e.facing))), 0, float(std::sin(double(e.facing)))};
                            const Vec3 u = norm(dir);
                            AiBullet b;
                            b.kind = 0xb; b.effect = M; b.owner = owner;
                            b.pos = {e.self.x + u.x, (u.y + 0.5f) + e.self.y, e.self.z + u.z};
                            b.dir = dir; b.speed = 0.05f; b.accel = 0.0001f; b.knock = 1.0f;
                            b.home_period = 2; b.home_rate = 0.15707964f; b.f168 = 1.0f;
                            b.f16c = float(n); b.f170 = 30.0f;
                            out.bullets.push_back(b);
                        }
                        if (a.dur() != 0) break;
                    }
                    st = 0;
                    const std::uint32_t cap = std::uint32_t(150 - 15 * d);
                    if (std::uint32_t(a.cool()) > cap) a.cool() = int(cap);
                    break;
                }
                default: break;
            }
            break;
        }
        default: break;
    }

    // TAIL: the warp machine (every phase).
    a.hittable = true;
    out.set_durable = 0;
    a.alpha = 1.0f;
    bool fx = false;
    switch (a.i(0x72c)) {
        case 1:                                                    // fade out
            if (a.i(0x79c) == 0) { fx = true; a.i(0x7a4) = 0; a.i(0x79c) = 1; }
            else if (a.i(0x79c) == 1) {
                const int k = ++a.i(0x7a4);
                a.alpha = 1.0f - float(k) * 0.025f;
                if (!(0.0f < a.alpha)) { a.alpha = 0.0f; a.i(0x72c) = 2; }
            }
            break;
        case 2: {                                                  // relocate (one attempt a tick)
            a.alpha = 0.0f;
            const float rmin = a.i(0x7a0) != 0 ? 1.5f : 0.0f;
            const float u1 = unit(e.rng()), u2 = unit(e.rng());
            const float r = (8.0f - rmin) * u1 + rmin;
            const float ang = (u2 * 360.0f + 0.0f) * 0.0174533f;
            const float s = float(std::sin(double(ang))), c = float(std::cos(double(ang)));
            const Vec3 C = a.i(0x7a0) != 0 ? T : e.self;
            const Vec3 P{C.x + c * r, C.y + 0.0f, C.z + r * s};
            const Vec3 Dv{C.x - P.x, (C.y + 0.25f) - P.y, C.z - P.z};
            const Vec3 n = norm(Dv);
            if (!e.world || !A) break;
            AiWorld& w = *e.world;
            if (w.ray({P.x + 0.5f * n.x, P.y + 0.5f * n.y, P.z + 0.5f * n.z},
                      {Dv.x + 0.5f * n.x, Dv.y + 0.5f * n.y, Dv.z + 0.5f * n.z}).hit) break;
            const AiWorld::Ray g = w.ray(P, {0, -10.0f, 0});
            if (!g.hit) break;
            Vec3 Q{g.point.x, g.point.y + a.f(0x7b8), g.point.z};
            float X = 2.0f;
            const AiWorld::Ray h = w.ray(Q, {0, 2.0f, 0});
            if (h.hit) {
                X = ((2.0f - h.point.y) - 0.5f) - Q.y;              // [sic] (M550.md §5.1)
                if (!(X > 1.0f)) break;
            }
            const std::uint32_t wv = e.rng();
            const float H = a.f(0x7b8) + ((wv & 0x20000u) ? X : 0.0f);
            a.f(0x834) = H;                                        // the hover height, for good
            Q.y = (H - a.f(0x7b8)) + Q.y;
            if (w.sphere_hits(Q, 0.5f)) break;
            A->pos = {Q.x, Q.y - A->height, Q.z};                  // the origin = Q
            a.i(0x72c) = 3;
            break;
        }
        case 3:                                                    // fade in
            a.alpha = 0.0f;
            if (a.i(0x79c) == 1) {
                a.i(0x79c) = 2;
                ai::steer(a, e);
                ai::turn(a, a.f(0x814) * 20.0f);
                fx = true; a.i(0x7a4) = 0;
            } else if (a.i(0x79c) == 2) {
                const int k = ++a.i(0x7a4);
                a.alpha = float(k) * 0.025f;
                if (!(a.alpha < 1.0f)) {
                    a.alpha = 1.0f; a.i(0x72c) = 0; a.i(0x79c) = 0;
                    a.i(0x7a8) = 40 - 10 * d;
                }
            }
            break;
        default: break;
    }
    if (std::uint32_t(a.i(0x72c) - 1) <= 2u) {
        out.set_durable = 1;
        a.action() = 1;
        if (0.5f > a.alpha) a.hittable = false;
    }
    if (fx) {
        const Vec3 at{e.self.x, e.self.y - e.height, e.self.z};
        out.effects.push_back({0x10e, at, 1.0f, true});            // enemy\m_230_taiki
        out.effects.push_back({0x10d, at, 1.3f, true});            // enemy\z_warp
        out.sounds.push_back(0x1d0);                               // zako\z_390_run (warp)
    }
    return out;
}

}  // namespace cleria::brains
