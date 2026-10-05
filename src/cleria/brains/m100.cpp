// M100 Riffligan — brain FUN_0047adf0 (handler FUN_0047b7f0, AI index 3).
// Runs BEFORE the action player, so it reads the previous tick's +0x734/+0x740.
// Fields: +0x728 sub-state, +0x79c hold/cooldown timer, +0x7a0 action timer,
// +0x7a4 "patience" (non-attack actions since the last approach), +0x7a8 ticks the
// target has been in front, +0x894 waypoint.
#include <cmath>

#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m100(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    const int d = difficulty(e);
    const Vec3 to = a.v(0x734);
    const float d2 = to.x * to.x + to.z * to.z;                     // planar
    bool can_attack = true;
    if (std::fabs(e.target.y - e.self.y) >= 2.0f && e.target_grounded) can_attack = false;  // 0x47af98
    const std::uint32_t R = r15(e);
    int pmax = int(R % 3u) - d + 4;                                 // the patience budget
    if (d < 0) pmax -= d;
    if (a.phase() == 0) {                                           // init
        ai::init(a, p, e);
        a.i(0x7a4) = 1;
        return out;
    }
    if (a.phase() == 1) { ai::idle(a, p, e); a.i(0x728) = 0; return out; }
    if (a.phase() != 2) return out;

    if (a.dist() > 20.0f) ai::disengage(a);                        // DAT_006bd994
    const float l = std::sqrt(dot(to, to));
    const Vec3 dir = l > 0 ? Vec3{to.x / l, to.y / l, to.z / l} : Vec3{};
    const bool not_front = std::cos(a.heading) * dir.x + std::sin(a.heading) * dir.z < 0.001f;
    if (d2 > 2.5f || not_front) can_attack = false;                // DAT_006bd624, DAT_006bcff0

    int& sub = a.i(0x728);
    int& t79c = a.i(0x79c);
    int& t7a0 = a.i(0x7a0);
    int& patience = a.i(0x7a4);
    int& facing = a.i(0x7a8);
    if (sub == 0) {
        if (!can_attack) {
            if (t7a0 == 0 && t79c == 0) {
                if (!not_front) {                                   // in front, out of reach
                    a.action() = 0x400; a.steer() = 0; a.dur() = 60; t7a0 = 60; patience = 0;
                } else {
                    facing = 0;
                    const std::uint32_t R2 = r15(e);
                    if ((d == -2 || int(R2) % (d + 4) == 0) && patience < pmax / 2) {
                        a.action() = 1; a.steer() = 1;              // hold, heading locked
                        a.dur() = t7a0 = int(r15(e) % 15u) + 30;
                        ++patience;
                    } else {
                        a.action() = 2;                             // stand, turning
                        if (patience < pmax) {                      // ... to a +/-45 deg waypoint
                            a.steer() = 4;
                            Vec3 v = a.v(0x748);
                            if (dot(v, v) < 1e-6f) v = {std::cos(a.heading), 0, std::sin(a.heading)};
                            const float ang = float(int(r15(e) % 90u) - 45) * 3.14159f / 180.0f;
                            const float ca = std::cos(ang), sa = std::sin(ang);
                            const Vec3 r{v.x * ca + sa * v.z, v.y, ca * v.z - v.x * sa};
                            a.set_v(0x894, {e.self.x + r.x * 30.0f, e.self.y + r.y * 30.0f,
                                            e.self.z + r.z * 30.0f});
                            a.dur() = t7a0 = int(r15(e) % 15u) + 30;
                            ++patience;
                        } else {
                            a.steer() = 0;
                        }
                    }
                }
            }
        } else {
            if (t79c == 0) {                                        // pre-attack hold
                a.action() = 2; a.steer() = 0; t79c = 10; a.dur() = 10; sub = 10;
            }
            t7a0 = 0;
            patience = 0;
        }
    } else if (sub == 10) {
        if (t79c == 0) {
            if (facing < 30) {                                      // aim: 30 ticks in front
                const int w = 30 - facing;
                t79c = w; a.action() = 1; a.steer() = 1; a.dur() = w;
            }
            sub = 11;
        }
    }
    if (sub == 11 && t79c == 0) {                                   // strike: section 7
        out.section = 7;
        out.attack = true;
        a.action() = 1;
        const int cd = int(r15(e) % 40u) + 100 - 10 * d;
        a.dur() = a.cool() = t79c = cd;
        sub = 0;
    }
    if (t79c != 0) --t79c;
    if (t7a0 != 0) --t7a0;
    if (!not_front) ++facing; else facing = 0;
    return out;
}

}  // namespace cleria::brains
