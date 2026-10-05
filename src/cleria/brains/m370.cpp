// M370 Celny — brain FUN_00494430 (handler FUN_004950f0 = M100's minus the arena clamp,
// AI index 30). Spec: build/ai_spec/M370.md. Chelasken's (M380) brood.
//
// Birth (phase 4): a placed Celny engages at once. A spawned one (parent +0x854) requests
// section 8 and stands 100 ticks, Durable, unhittable until age 64, growing alpha and scale
// 0.5 -> 1.0 over 60 ticks while sliding along the parent's heading (+0x7f0) through knock
// channel 2 (0.075 fading to 0) and hopping 0.18, 0.09, 0.045... at each grounded Marker 1.
// Engaged it reads where the target faces: faced and within 5 u it flees (walk x1.5, turn
// x1.5, 20..34 ticks) — or, walled in 1 u ahead, charges; back turned within 4 u (charge
// lockout clear) it charges; back turned beyond 2 u it sneaks up (walk, 15 ticks); otherwise
// 40% it stands 60 ticks, 60% it walks 2 u to a ray-clear point (3 tries 120 deg apart).
// The charge is action 0x400 for 112 / 164 / 216 ticks (Easy / Normal / Hard+) at half turn
// rate: the run section 1 is its hop loop and hit volume. Lockout 120 / 90 / 60 after.
// The jump helper's section-0x13a launch (K 0.1, push +0x7e0; FACTORY.md: jump-capable).
// [not modelled] the keep-out box restore (FUN_0042dc90).
#include <algorithm>
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m370(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;
    Actor* A = e.actor;
    const Vec3 to = a.v(0x734);
    const float pd = std::sqrt(to.x * to.x + 0.0f + to.z * to.z);
    const int self_id = A ? A->instance_id : -1;
    if (a.i(0x854) == 0) a.i(0x854) = self_id;                    // no parent -> parent = self
    const bool E = e.diff_easy, N = !E && e.diff_index == 0;
    auto ray = [&](const Vec3& d) { return e.world && e.world->ray(e.self, d).hit; };
    auto set_scale = [&](float s) { a.alpha = s; if (A) A->scale = s; };

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            a.phase() = 4;
            set_scale(0.5f);
            a.f(0x7ac) = a.f(0x81c);
            a.f(0x7b0) = a.f(0x814);
            a.i(0x7bc) = 0; a.i(0x7a0) = 0; a.i(0x728) = 0; a.i(0x72c) = 0; a.i(0x730) = 0;
            return out;
        case 4: {
            if (a.i(0x854) == self_id) {                           // map-placed
                a.hittable = true;
                set_scale(1.0f);
                a.phase() = 2;
                a.action() = 0; a.i(0x728) = 0; a.i(0x72c) = 0; a.i(0x730) = 0;
                return out;
            }
            out.set_durable = 1;
            if (a.action() == 0 && std::uint32_t(a.i(0x7bc)) >= 1u) {
                a.phase() = 2; a.action() = 0; a.i(0x728) = 0; a.i(0x72c) = 0; a.i(0x730) = 0;
            }
            std::uint32_t n;
            if (e.section != 8 && a.i(0x7bc) == 0) {
                out.section = 8;                                   // gated; result ignored
                a.action() = 1; a.dur() = 100; a.cool() = 100;
                a.i(0x7bc) = 1;
                a.f(0x7b4) = 0.18f;
                a.i(0x87c) = 0;
                n = 0;
            } else {
                n = std::uint32_t(a.i(0x87c));
                set_scale(std::min(60.0f, float(n)) / 120.0f + 0.5f);
            }
            const float k = std::max(0.0f, (60.0f - float(n)) / 60.0f) * 0.075f;
            if (e.aso_marker == 1 && e.grounded && A) {
                if (a.i(0x7bc) == 2) {
                    A->vert.t += a.f(0x7b4);
                    A->pos.y += A->vert.t;
                    a.f(0x7b4) *= 0.5f;
                } else {
                    a.i(0x7bc) += 1;                               // this tick only goes airborne
                }
                A->vert.grounded = false;
                out.set_own_jump = 1;
            }
            if (A) {
                const float h = a.f(0x7f0);                        // the parent's heading
                A->mover.k2.x += std::cos(h) * k;
                A->mover.k2.z += std::sin(h) * k;
            }
            a.hittable = std::uint32_t(a.i(0x87c)) >= 64u;
            return out;
        }
        case 1:
            a.f(0x81c) = a.f(0x7ac); a.f(0x814) = a.f(0x7b0);
            a.i(0x728) = 0; a.i(0x72c) = 0; a.i(0x730) = 0;
            ai::idle(a, p, e);
            return out;
        case 2: break;
        default: return out;
    }
    if (a.dist() > 20.0f) ai::disengage(a);                       // the tick continues
    int& sub = a.i(0x730);
    switch (a.i(0x72c) != 0 ? -1 : sub) {                          // +0x72c != 0: dead in practice
        case 1:                                                    // start the charge
            a.action() = 0x400; a.steer() = 0;
            a.dur() = E ? 112 : N ? 164 : 216;
            a.cool() = a.dur() + (E ? 250 : N ? 200 : 150);
            a.f(0x814) = a.f(0x7b0) * 0.5f;
            sub = 2;
            break;
        case 2:
            if (a.dur() == 0) {
                sub = 0;
                a.f(0x814) = a.f(0x7b0);
                a.i(0x7a4) = E ? 120 : N ? 90 : 60;
            }
            break;
        case 0: {
            if (a.i(0x7a0) != 0) a.i(0x7a0) -= 1;
            if (a.i(0x7a4) != 0) a.i(0x7a4) -= 1;
            const int dur = a.dur();
            if (dur != 0 && a.steer() != 4) break;
            a.f(0x81c) = a.f(0x7ac); a.f(0x814) = a.f(0x7b0);
            auto norm = [](float x, float z) {
                const float l = std::sqrt(x * x + z * z);
                return l == 0 ? Vec3{} : Vec3{x * (1.0f / l), 0, z * (1.0f / l)};
            };
            const Vec3 f = norm(std::cos(e.target_heading), std::sin(e.target_heading));
            const Vec3 v = norm(e.self.x - e.target.x, e.self.z - e.target.z);
            const float dot = v.x * f.x + 0.0f + v.z * f.z;
            const bool fA = dot >= 0.25f && 5.0f > pd;
            const bool fC = 0.25f > dot && pd > 2.0f;
            const bool fD = fA || a.i(0x7a0) != 0;
            const bool fB = a.i(0x7a4) == 0 && 0.25f > dot && 4.0f > pd;
            if (fD) {
                if (ray({std::cos(a.heading), 0, std::sin(a.heading)})) sub += 1;   // cornered: charge
                else {                                             // flee
                    a.action() = 0x10; a.steer() = 3;
                    const int r = int(r15(e) % 15u);
                    a.dur() = r + 20; a.cool() = r + 20;
                    a.f(0x81c) = a.f(0x7ac) * 1.5f;
                    a.f(0x814) = a.f(0x7b0) * 1.5f;
                }
            } else if (fB) {
                sub += 1;
            } else if (fC) {                                       // sneak up
                a.action() = 0x10; a.steer() = 0; a.dur() = 15; a.cool() = 15;
            } else if (dur == 0) {                                 // idle choice
                if (r15(e) % 10u >= 6u) { a.action() = 1; a.steer() = 1; a.dur() = 60; }
                else {
                    for (int i = 0, off = 0; i < 3; ++i, off += 120) {
                        const int deg = int(r15(e) % 120u) - 60 + off;
                        const float an = float(deg) * 3.14159f / 180.0f + a.heading;
                        const float c = std::cos(an), s = std::sin(an);
                        const Vec3 d2{c + c, 0, s + s};
                        if (!ray(d2)) {
                            a.action() = 0x10; a.steer() = 4;
                            a.set_v(0x894, {e.self.x + d2.x, e.self.y + 0.0f, e.self.z + d2.z});
                            const int r2 = int(r15(e) % 15u);
                            a.cool() = 0; a.dur() = r2 + 85;
                            a.f(0x814) = a.f(0x7b0);
                            a.f(0x81c) = a.f(0x7ac) * 0.8f;
                            break;
                        }
                    }
                }
            }
            break;
        }
        default: break;
    }
    // TAIL: the jump helper's launch, K 0.1, push +0x7e0.
    if (A && launch_gate(e)) {
        launch(*A, a.f(0x7dc) * 0.1f, out);
        push1(*A, a.heading, a.f(0x7e0));
    }
    return out;
}

}  // namespace cleria::brains
