// M630 Kelmarel — brain FUN_004b0d60 (handler FUN_004b24f0 = M100's minus the arena clamp, AI index 56).
// Spec: build/ai_spec/M630.md.
//
// A skirmisher. Far (beyond 6.5 u or not facing within 60 deg) it runs at the target in 15-tick steps, hopping a
// 1-unit wall when the target is above. Near it picks: the double bite (section 7) when the target is 1 above
// (50%), else past 2 u 30% the hover-and-dive (section 311: hop at Marker 1, hang at Marker 2, rise at Marker 3)
// / 45% the triple shot (section 8, chained into 311 on Hard+) / else back off (flee 60..89 ticks), a back-hop
// (section 312) or the hop-and-shoot (section 313). Cooldown 45 - 15d + R%20 past the hold. Standing on
// surface 0x11 (blood water) it runs back to the target's last safe position; unaware, a target on 0x11 wakes it.
// The set-1 ("water") rays are AiWorld::water_under (straight down, as both of the brain's are).
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m630(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;
    Actor* A = e.actor;
    const int d = difficulty(e);
    const Vec3 v{a.f(0x734), 0.0f, a.f(0x73c)};
    const float pd = std::sqrt(v.x * v.x + 0.0f + v.z * v.z);
    const bool can_atk = !(std::fabs(e.target.y - e.self.y) >= 3.0f && e.target_grounded);   // 0x6bd680
    const bool tag = e.group_tag;
    bool F;
    {
        float fx = std::cos(a.heading), fz = std::sin(a.heading);
        const float fl = std::sqrt(fx * fx + 0.0f + fz * fz);
        fx = fl == 0.0f ? 0.0f : fx / fl; fz = fl == 0.0f ? 0.0f : fz / fl;
        const float ux = pd == 0.0f ? 0.0f : v.x / pd, uz = pd == 0.0f ? 0.0f : v.z / pd;
        F = !(0.5f > fx * ux + fz * uz);
    }
    const bool gnd = e.grounded;
    const int S = e.section;
    const float h = a.heading;
    auto ray_hit = [&](const Vec3& o, const Vec3& dl) { return e.world && e.world->ray(o, dl).hit; };
    auto water1 = [&](const Vec3& o, float dy) { return e.world && e.world->water_under(o, dy) > -1.0e29f; };   // ray1
    auto hop = [&](float vy) { if (A) jump(*A, vy, e.jump_v0, out); };
    auto dirh = [&](float ang, float k) { if (A) push1(*A, ang, k); };
    int& sub = a.i(0x72c);
    auto cool = [&] { a.cool() = int(r15(e) % 20u) + 15 * (-d + 3) + a.dur(); };
    // HOLD: stand out the cooldown; true when the sub ended.
    auto hold = [&] {
        if (a.dur() == 0) { a.action() = 1 + (d >= 1 ? 1 : 0); a.dur() = a.cool(); }
        if (a.cool() == 0 || (S == 4 && tag)) { sub = a.i(0x79c); return true; }
        return false;
    };

    switch (a.phase()) {
        case 0:                                                    // INIT (0x4b211b)
            ai::init(a, p, e);
            a.i(0x7bc) = a.i(0x7c0) = 0; sub = 0; a.i(0x730) = 0;
            a.set_v(0x7b0, e.target);
            a.f(0x7cc) = a.f(0x818);
            break;
        case 1:                                                    // UNAWARE (0x4b20c0)
            ai::idle(a, p, e);
            sub = 0; a.i(0x730) = 0; a.i(0x7bc) = 0;
            a.f(0x818) = a.f(0x7cc);
            if (A) A->flags12c &= ~0x8000u;
            reinterpret_cast<std::uint8_t*>(a.blk.data())[0x8a4 - EnemyAi::kBase] = 0;
            if (e.target_surface == 0x11) ai::acquire(a);
            break;
        case 2: {                                                  // ENGAGED (0x4b10ba)
            if (a.dist() > 20.0f) ai::disengage(a);
            if (e.target_grounded && e.target_surface != 0x11) a.set_v(0x7b0, e.target);
            if (std::uint32_t(sub) > 0xc9u) break;
            switch (sub) {
                case 0:
                    a.i(0x730) = 0; sub = a.surf == 0x11 ? 10 : 0x14;
                    break;
                case 10:                                           // ESCAPE (0x4b114b)
                    if (a.surf != 0x11) { sub = 0; a.f(0x7cc) = a.f(0x818); break; }
                    if (a.dur() != 0) break;
                    a.f(0x7cc) = a.f(0x818) + a.f(0x818);
                    a.action() = 0x800; a.steer() = 4; a.dur() = 10;
                    a.set_v(0x894, a.v(0x7b0));
                    if (ray_hit({e.self.x, e.self.y - 0.4f, e.self.z}, {2 * std::cos(h), 0, 2 * std::sin(h)}) &&
                        a.f(0x7b4) > e.self.y - 1.5f && A) {
                        A->vert.t += 0.4f; A->pos.y += 0.4f;        // no grounded / own-jump writes
                    }
                    break;
                case 0x14: {                                       // DECIDE (0x4b12c7)
                    if (a.dur() != 0 || !gnd) break;
                    if (a.surf == 0x11) { sub = 10; break; }
                    if (a.i(0x730) != 0) break;
                    const bool climb_ok = e.target.y > e.self.y + 1.0f && e.target_grounded &&
                                          ray_hit(e.self, {std::cos(h), 0, std::sin(h)});
                    if (pd > 6.5f || !F) {                         // FAR
                        a.action() = 0x400; a.steer() = 0; a.dur() = 15;
                        if (climb_ok) { hop(0.28f); dirh(h, 0.1f); }
                        break;
                    }
                    sub = 0x14; a.i(0x730) = 0; a.i(0x79c) = 0;    // NEAR
                    if (e.target.y > e.self.y + 1.0f && r15(e) % 100u < 50u) sub = 0x78;   // N1
                    if (sub == 0x14 && pd > 2.0f) {
                        a.action() = 0x400; a.steer() = 0; a.dur() = 30;
                        int r = int(r15(e) % 100u);                // N2
                        bool climb = false;
                        if (e.diff_easy && d != 3) {               // Easy / Very easy (not bc40)
                            if (!tag) climb = true;
                            else r = r >= 30 ? 100 : 0;
                        } else if (tag) r = int(r15(e) % 75u);     // N3
                        if (!climb && r < 30) sub = 0x82;
                        else if (!climb && r < 75) sub = 0x6e;
                        else if (climb_ok) { hop(0.28f); dirh(h, 0.1f); }
                    }
                    if (sub == 0x14) {                             // POST (0x4b15ff)
                        if (r15(e) % 100u >= 50u) sub = 100;       // N4
                        else {
                            if ((e.rng() & 0x20000u) != 0) {       // N5 (BIT17)
                                const double hb = double(h + 3.14159f);   // 0x6bd6a8 (truncated pi)
                                const Vec3 O{e.self.x + 3 * float(std::cos(hb)), e.self.y, e.self.z + 3 * float(std::sin(hb))};
                                if (ray_hit(O, {0, -1.5f, 0}) && !water1(O, -1.5f)) sub = 0xc8;   // floor 3 behind, not "water"
                            }
                            if (sub != 0xc8) { a.action() = 0x400; a.steer() = 3; a.dur() = int(r15(e) % 30u) + 60; }   // N6
                        }
                    }
                    if (!can_atk) sub = 0x14;
                    break;
                }
                case 100: a.action() = 2; a.steer() = 0; a.dur() = 10; sub = 0x65; break;
                case 0x65:
                    if (a.dur() != 0) break;
                    out.section = 7; out.attack = true;
                    a.dur() = 79; a.action() = 1; cool(); sub = 0x66;
                    break;
                case 0x66: hold(); break;
                case 0x6e: a.action() = 2; a.steer() = 0; a.dur() = 10; sub = 0x6f; break;
                case 0x6f:
                    if (a.dur() != 0) break;
                    out.section = 8; out.attack = true;
                    a.dur() = 43; a.action() = 1; cool(); sub = 0x70;
                    break;
                case 0x70:                                         // 0x4b1b96
                    if (a.dur() == 0) {
                        a.action() = 1 + (d >= 1 ? 1 : 0); a.dur() = a.cool();
                        if (d >= 2) { sub = 0x82; break; }
                    }
                    if (a.cool() != 0) { if (S == 4 && tag) sub = a.i(0x79c); }
                    else sub = d >= 1 ? 0x82 : a.i(0x79c);
                    break;
                case 0x78: a.action() = 2; a.steer() = 0; a.dur() = 0; sub = 0x79; break;
                case 0x79:
                    if (a.dur() != 0) break;
                    out.section = 0x139; out.attack = true;
                    a.action() = 2; a.dur() = 43; cool(); sub = 0x7a;
                    break;
                case 0x7a:
                    if (!hold() && e.aso_marker == 1 && gnd) hop(0.28f);
                    break;
                case 0x82:
                    a.action() = 2; a.steer() = 0; a.dur() = 10; a.i(0x7a0) = 0; sub = 0x83;
                    break;
                case 0x83:
                    if (a.dur() != 0) break;
                    out.section = 0x137; out.attack = true;
                    a.action() = 1 + (d >= 2 ? 1 : 0); a.dur() = 114; cool(); sub = 0x84;
                    break;
                case 0x84: {
                    if (hold()) break;
                    const int m = e.aso_marker;
                    if (m == 1 && gnd) hop(0.28f);
                    else if (m == 2 && A) {
                        if (a.i(0x7a0) == 0) { a.i(0x7a0) = 1; a.f(0x7ac) = A->pos.y; }
                        A->pos.y = a.f(0x7ac);                     // hang in the air
                    } else if (m == 3 && A) {
                        a.action() = 1;
                        A->vert.t += 0.018f; A->pos.y += 0.018f;   // 0x6bd0e4
                        A->vert.grounded = false; out.set_own_jump = 1;
                    }
                    break;
                }
                case 0xc8:
                    a.action() = 2; a.steer() = 1; a.dur() = 36; a.i(0x7a0) = 0;
                    out.section = 0x138;                           // gated (absent in M_630.ASO: a stand)
                    sub = 0xc9;
                    break;
                case 0xc9:
                    if (a.dur() == 0) { sub = a.i(0x79c); break; }
                    if (e.aso_marker == 1 && a.i(0x7a0) == 0) {
                        a.i(0x7a0) = 1; hop(0.28f); dirh(e.facing + 3.14159f, 0.35f);   // behind the visible facing
                    }
                    break;
                default: break;
            }
            break;
        }
        default: break;
    }
    // TAIL (0x4b21ce)
    if (S == 0x13a && gnd && e.aso_marker == 1 && A) {             // the launch (K 0.11, fallback push x3.5)
        // ... skipped only for a set-1 surface within 2 under the point L ahead, the target nearer than 6.5 and
        // not faced (M630.md §3.6): L = +0x328 + 0.5 past a radius of 0.5, else 1.0.
        const float L = e.radius > 0.5f ? e.radius + 0.5f : 1.0f;
        const Vec3 Q{e.self.x + float(std::cos(double(h))) * L, e.self.y, e.self.z + float(std::sin(double(h))) * L};
        if (!(water1(Q, -2.0f) && 6.5f > pd && !F)) {
            const float vv = a.f(0x7dc) * 0.11f;
            launch(*A, vv, out);
            push1(*A, h, a.f(0x7e0) != 0.0f ? a.f(0x7e0) : a.f(0x818) * 3.5f);
        }
    }
    if (A && a.surf_ray == 0x11 && A->pos.y + A->height > a.water_y + 5.0f && a.water_y > -1.0e29f)
        A->pos.y = a.water_y + 5.0f - A->height;                   // the height clamp over +0x320
    return out;
}

}  // namespace cleria::brains
