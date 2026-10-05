// M520 Menoak — brain FUN_0048b460 (handler FUN_0048c010 = M100's minus the arena clamp,
// AI index 45). Spec: build/ai_spec/M520.md.
//
// An electric turtle. Its run multiplier is zeroed at init, so "running" (0x800) is the hop
// section 1 at mover speed 0, and the brain moves it by writing the velocity itself: every
// third hop marker re-arms a leap strength k = base*run*20 (decaying x0.99 a tick); while
// hopping the velocity blends toward k along its HEADING (0.15, then 0.05) and the vertical
// value toward 1.5*k*u.y with its own -0.01/tick. Near the floor, close to the target, it
// walks (x1.75). Within 4.5 u, off cooldown: max(0, 20d) + 20 per mille a tick to spin
// (section 7, 150..209 ticks, cooldown + 240; above Normal it homes through the air), then a
// 28-tick spin-down (section 8, ungated) and a retreat for the rest of the cooldown. A hit
// reaction cancels the spin. Section 7's pulse is a sphere centred on it (hit_volume.hpp).
// [not modelled] the keep-out box restore (FUN_0042dc90); byte +0x9d1.
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m520(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;
    Actor* A = e.actor;
    const int d = difficulty(e);
    a.f(0x7ac) *= 0.99f;                                           // leap strength decay
    const bool tag = e.group_tag;
    const Vec3 T = e.target;
    auto normalize = [](Vec3 v) {
        const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        return l == 0 ? Vec3{} : Vec3{v.x / l, v.y / l, v.z / l};
    };
    float t_now = A ? A->vert.t : 0.0f;
    auto set_t = [&](float t) { if (A) A->vert.t = t; t_now = t; };
    auto airborne = [&] { if (A) A->vert.grounded = false; out.set_own_jump = 1; };

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            for (int off = 0x834; off <= 0x850; off += 4) a.f(off) = 0;   // only 32 bytes copied
            if (d >= 2) { a.f(0x820) *= 1.05f; a.f(0x81c) *= 1.2f; a.f(0x814) *= 1.3f; }
            if (tag) { a.f(0x820) *= 1.3f; a.f(0x81c) *= 1.3f; }
            a.i(0x70c) = 0; a.steer() = 0;
            a.f(0x7ac) = 0; a.f(0x7b0) = 0;
            a.f(0x7cc) = a.f(0x820); a.f(0x7d0) = a.f(0x81c);
            a.i(0x79c) = 0; a.i(0x7a0) = 0;
            a.f(0x820) = 0;                                        // run multiplier zeroed for good
            return out;
        case 1:
            a.action() = e.grounded ? 0x20 : 2;
            a.steer() = 0;
            ai::idle(a, p, e);
            a.i(0x728) = 0; a.i(0x79c) = 2; a.cool() = 0;
            a.f(0x81c) = a.f(0x7d0);
            a.f(0x7b0) = t_now;
            if (e.section == 7) out.section = 8;                   // disengaged mid-spin
            return out;
        case 2: break;
        default: return out;
    }
    a.f(0x81c) = a.f(0x7d0);
    a.steer() = 0;
    const int sub = a.i(0x728);
    const float dist = a.dist();
    if (sub == 0) {
        if (a.cool() == 0) {
            if (4.5f > dist) {
                const int thr = std::max(0, 20 * d) + 20;
                if (int(r15(e) % 1000u) < thr) a.i(0x728) = 1;
            }
            const int S = e.section;
            if (S == 0) {                                          // walking
                a.action() = 0x20;
                a.f(0x81c) = a.f(0x7d0) * 1.75f;
                a.dur() = 10;
                a.i(0x79c) = 0;
                if (T.y - e.self.y > 4.0f || dist > 8.0f) { a.action() = 0x800; a.i(0x79c) = 2; }
                if (!e.grounded) a.action() = 0x800;
                a.i(0x7a0) = 0;
                a.f(0x7b0) = t_now;
            } else if (S == 1 || S == 3) {                         // hopping / falling
                a.action() = 0x800; a.dur() = 10;
                if (S == 1) {
                    if (e.aso_marker == 1) {
                        out.set_marker = 2;
                        a.i(0x79c) += 1;
                        if (std::uint32_t(a.i(0x79c)) > 2u) {
                            a.i(0x79c) = 0; a.i(0x7a0) = 0;
                            a.f(0x7ac) = a.f(0x818) * a.f(0x7cc) * 20.0f;
                        }
                    }
                } else {                                           // 3: force 1 ungated, skip to Marker(1)
                    out.section = 1; out.section_force = true; out.fast_forward = 1;
                }
                // HOP
                const Vec3 u = normalize({T.x - e.self.x, (T.y - e.self.y) + 0.5f, T.z - e.self.z});
                const float k = a.f(0x7ac);
                const float h = float(std::sqrt(double((k * u.z) * (k * u.z) + (k * u.x) * (k * u.x))));
                const float hx = float(std::cos(double(a.heading))) * h;
                const float hz = float(std::sin(double(a.heading))) * h;
                const float hy = k * u.y * 1.5f;
                airborne();
                set_t(a.f(0x7b0));
                float ka, kb;
                if (a.i(0x7a0) == 0) { ka = 0.15f; kb = 0.85f; a.i(0x7a0) = 1; }
                else { ka = 0.05f; kb = 0.95f; }
                if (A) {
                    A->mover.vx = A->mover.vx * kb + hx * ka;
                    A->mover.vz = A->mover.vz * kb + hz * ka;
                }
                set_t(t_now * kb + hy * ka);
                a.speed = 0;
                const float t2 = t_now - 0.01f;
                set_t(t2); a.f(0x7b0) = t2;
                if (A && A->floor_y > e.self.y - 1.0f && 4.0f > T.y - e.self.y && 3.0f > dist)
                    a.action() = 0x20;                             // landing near the target: walk
            }
        } else {                                                   // cooling down: retreat
            a.action() = 0x20; a.steer() = 3;
            a.f(0x81c) = a.f(0x7d0);
        }
    } else if (sub == 1 || sub == 2) {                             // spin / spin-down
        a.steer() = 1;
        if (!e.grounded || d >= 2) {
            airborne();
            Vec3 v;
            if (d >= 2) {
                a.steer() = 0;
                v = normalize({T.x - e.self.x, (T.y - e.self.y) + 0.5f, T.z - e.self.z});
            } else {
                v = {A ? A->mover.vx : 0.0f, 0, A ? A->mover.vz : 0.0f};
                if (v.x * v.x + v.z * v.z > 0.001f) v = normalize(v);
            }
            const float k = a.f(0x7ac);
            set_t(a.f(0x7b0));
            if (A) {
                A->mover.vx = A->mover.vx * 0.95f + k * v.x * 0.05f;
                A->mover.vz = A->mover.vz * 0.95f + k * v.z * 0.05f;
            }
            set_t(t_now * 0.95f + k * v.y * 0.05f);
            a.speed = 0;
            const float t2 = t_now - 0.01f;
            set_t(t2); a.f(0x7b0) = t2;
            a.action() = 0x800;
        }
        if (sub == 1) {
            if (a.cool() == 0) {                                   // start the spin
                a.action() = 0x800;
                const int r = int(r15(e) % 60u);
                a.dur() = r + 150;
                a.cool() = r + 150 + 240;
                out.section = 7; out.attack = true;                // gated; result ignored
                if (d >= 2) {
                    const float m = a.f(0x818) * a.f(0x7cc) * 10.0f;
                    if (m > a.f(0x7ac)) a.f(0x7ac) = m;
                }
            } else if (a.dur() == 0) {                             // spin-down, section 8 ungated
                a.action() = 0x800;
                a.dur() = 28;
                a.i(0x728) = 2;
                out.section = 8; out.section_force = true;
            }
        } else if (sub == 2 && a.dur() == 0) {
            a.i(0x728) = 0;
        }
    }
    // Tail: a hit reaction cancels the spin.
    if (e.section == 4 && (a.i(0x728) == 1 || a.i(0x728) == 2)) {
        const std::uint32_t c = std::uint32_t(a.cool());
        if (c > 240u) a.cool() = 239;
        else if (c != 0) a.cool() = int(c) - 1;
        a.i(0x728) = 0;
    }
    return out;
}

}  // namespace cleria::brains
