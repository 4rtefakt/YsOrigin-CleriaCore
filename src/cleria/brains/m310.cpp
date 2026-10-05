// M310 Jiamobre — brain FUN_00491c00 (handler FUN_00492f50 = M100's, WITH the arena clamp;
// AI index 24). Spec: build/ai_spec/M310.md.
//
// A shuriken ninja that moves by hopping. Grounded and re-deciding (dur 0) it searches for a
// landing spot: r = 1, 2, ... < 7, each try a fresh random angle within +/-30 deg of the
// target (of the opposite direction after a damaging hit), accepted when a slanted ray up to
// H/2 = 1.75 above the spot is clear and a floor lies within 7 below it (edge rays at r +/- 0.5
// nudge the distance); the farthest accepted spot wins and section 0x13a is requested with a
// 32-tick hold. No spot: run at the target for 15 ticks. At section 0x13a / 3 Marker 1 the
// brain launches the hop itself (at most once per 17 ticks): channel 1 += dist*0.1 along the
// chosen angle; vertically mode 1 lifts y by 0.7 (and 0.35 into both channels' y), mode 2 sets
// t = 0.35 (and, literally, adds channel 1's y to y). Within 5 u, level and unhurt: a back-hop,
// then section 7 (hold 67) and N - 1 section 8s (hold 47), N = {1,1,2,2,3,3}[d+2] (+1 tagged),
// cooldown hold + 10*(3 - d) + R%20; it tracks the target x2/x3/x4 during each Marker 1.
// [not modelled] the keep-out box restore (FUN_0042dc90). Launch mode 0 reads the shared jump
// helper FUN_00476200's +0x7dc/+0x7e0, which stay 0 until the helper is run.
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m310(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;
    Actor* A = e.actor;
    static constexpr int tbl[6] = {1, 1, 2, 2, 3, 3};
    const Vec3 to = a.v(0x734);
    const float hdist = std::sqrt(to.x * to.x + 0.0f + to.z * to.z);
    const bool can = !(std::fabs(e.target.y - e.self.y) >= 2.0f && e.target_grounded);
    const bool tagged = e.group_tag;
    const int d = difficulty(e);

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            a.f(0x7ac) = a.f(0x814);
            a.i(0x7c8) = 0; a.i(0x7a0) = 0;
            a.i(0x8a0) += 16;
            a.i(0x7c4) = 0; a.i(0x730) = 0; a.i(0x72c) = 0; a.i(0x728) = 0;
            return out;
        case 1:
            ai::idle(a, p, e);
            a.f(0x814) = a.f(0x7ac);
            a.i(0x7c8) = 0;
            a.i(0x7c4) = 0; a.i(0x730) = 0; a.i(0x72c) = 0; a.i(0x728) = 0;
            return out;
        case 2: break;
        default: return out;
    }
    if (a.dist() > 20.0f) ai::disengage(a);                       // the tick continues
    if (e.took_damage && std::uint32_t(a.i(0x7c4)) < 1u) a.i(0x7c4) += 1;
    auto ray = [&](const Vec3& o, const Vec3& dl) { return e.world && e.world->ray(o, dl).hit; };
    auto hop_search = [&](float base, bool retreat_mode) {
        const float H = a.f(0x828);
        float r = 1.0f;
        a.action() = 2; a.steer() = 0; a.i(0x7c8) = 0;
        const float aT = float(std::atan2(double(to.z), double(to.x)));
        do {
            const int j = int(r15(e) % 60u) - 30;
            const float ang = float(j) * 3.14159265f / 180.0f + (aT + base);
            const float c = float(std::cos(double(ang))), s = float(std::sin(double(ang)));
            const Vec3 P{e.self.x + c * r, e.self.y + H * 0.5f, e.self.z + s * r};
            if (!ray(e.self, {c * r, H * 0.5f, s * r}) && ray(P, {0, -7.0f, 0})) {
                if (a.i(0x7c8) == 0) a.i(0x7c8) = 1;
                if (retreat_mode && base > 0.0f) a.i(0x7c8) = 2;
                const Vec3 C{(e.self.x + c * r) + c * 0.5f, e.self.y + H * 0.5f, (e.self.z + s * r) + s * 0.5f};
                const Vec3 Dp{(e.self.x + c * r) - c * 0.5f, e.self.y + H * 0.5f, (e.self.z + s * r) - s * 0.5f};
                float dist;
                if (!ray(C, {0, -7.0f, 0})) { dist = r - 0.5f; a.i(0x7c8) = 2; }
                else if (!ray(Dp, {0, -7.0f, 0})) { dist = r + 0.5f; a.i(0x7c8) = 2; }
                else dist = r;
                a.f(0x7b0) = H; a.f(0x7b4) = ang; a.f(0x7b8) = dist;
                out.section = 0x13a;                               // gated; result ignored
                a.dur() = 32;
                r = dist;
            }
            r = r + 1.0f;
        } while (7.0f > r);
    };
    auto track = [&] {
        float k;
        if (d == 3) k = 4.0f;
        else if (d < 0) k = 2.0f;
        else k = d >= 1 ? 4.0f : 3.0f;
        if (e.aso_marker == 1) { a.f(0x814) = a.f(0x7ac) * k; a.action() = 2; }
        else { a.f(0x814) = a.f(0x7ac); a.action() = 1; }
    };
    const int N = tbl[d + 2] + (tagged ? 1 : 0);
    int& sub = a.i(0x72c);
    switch (sub) {
        case 0:
            a.f(0x814) = a.f(0x7ac); sub = 20; a.i(0x730) = 0; a.i(0x7c8) = 0;
            break;
        case 20:
            if (a.dur() != 0) break;
            if (a.i(0x730) == 1) { if (can) { sub = 100; a.i(0x79c) = 0; } else sub = 0; break; }
            if (a.i(0x730) == 0) {
                const float off = a.i(0x7c4) != 0 ? 3.14159265f : 0.0f;
                if (!(hdist > 5.0f) && can && !(off > 0.0f)) { a.i(0x730) = 1; break; }
                if (!e.grounded) break;
                hop_search(off, true);
                if (a.i(0x7c8) == 0) { a.action() = 0x400; a.steer() = 0; a.dur() = 15; }
                else if (a.i(0x7c4) != 0) a.i(0x7c4) -= 1;
            }
            break;
        case 100:                                                  // the back-hop (no grounded test)
            hop_search(3.14159265f, false);
            if (a.i(0x7c8) == 0) a.dur() = 0;
            sub = 110;
            break;
        case 110:
            if (a.dur() != 0 || e.section == 0x13a) break;
            out.section = 7; out.attack = true;
            a.action() = 1; a.dur() = 67;
            a.cool() = int(r15(e) % 20u) + 10 * (3 - d) + a.dur();
            a.i(0x7bc) = 1;
            sub = 111;
            break;
        case 111:
            if (a.cool() == 0) { sub = a.i(0x79c); break; }
            if (e.section != 7) break;
            track();
            if (N <= 1 || e.aso_marker != 4) break;
            sub = 112;
            break;
        case 112:
            out.section = 8; out.attack = true;
            a.action() = 1; a.dur() = 47;
            a.cool() = int(r15(e) % 20u) + 10 * (3 - d) + a.dur();
            a.i(0x7c0) = 0;
            sub = 113;
            break;
        case 113:
            if (a.cool() == 0) { sub = a.i(0x79c); break; }
            if (e.section != 8) break;
            track();
            if (e.aso_marker != 4) break;
            if (a.i(0x7c0) == 0) { a.i(0x7bc) += 1; a.i(0x7c0) = 1; }
            if (std::uint32_t(N) > std::uint32_t(a.i(0x7bc))) sub = 112;
            break;
        default: break;
    }

    // END: the hop launch.
    if (std::uint32_t(a.i(0x8a0) - a.i(0x7a0)) <= 16u) return out;
    const int S = e.section;
    if (S != 0x13a && S != 3) return out;
    if (e.grounded && e.aso_marker == 1 && A) {
        switch (a.i(0x7c8)) {
            case 1: {
                const float v = a.f(0x7b0) * 0.1f;
                A->mover.k1.y += v; A->mover.k2.y += v;
                A->pos.y += v + v;                                 // t not written
                break;
            }
            case 2:
                A->vert.t = a.f(0x7b0) * 0.1f;
                A->pos.y += A->mover.k1.y;                         // literal: channel 1's y, not v
                break;
            default: {                                             // mode 0: the jump helper's launch
                const float v = a.f(0x7dc) * 0.105f;
                A->vert.t = v; A->pos.y += v;
                break;
            }
        }
        A->vert.grounded = false;
        out.set_own_jump = 1;
        float sp, h;
        if (a.i(0x7c8) != 0) { sp = a.f(0x7b8) * 0.1f; h = a.f(0x7b4); }
        else { sp = a.f(0x7e0); if (sp == 0.0f) sp = a.f(0x818) * 3.5f; h = a.heading; }
        A->mover.k1.x += float(std::cos(double(h))) * sp;
        A->mover.k1.z += float(std::sin(double(h))) * sp;
        a.i(0x7a0) = a.i(0x8a0);
    }
    if (e.aso_marker == 2) a.i(0x7c8) = 0;
    return out;
}

}  // namespace cleria::brains
