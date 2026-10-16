// M180 Wilewarer — brain FUN_0047cfd0 (handler FUN_0047d8a0 = M100's minus the arena
// clamp, AI index 11). Spec: build/ai_spec/M180.md.
//
// A shield-bearer. Engaged it re-issues an approach (0x400) every tick. It GUARDS
// (section 6, prio 2, action 2 for 60 ticks) reactively — when the player is in an
// attack section 0x100..0x112 (minus 0x109..0x10c, 0x10f) within 5 u (9.5 for 0x10e /
// 0x111), it faces the player within +/-90 deg and the player faces it within +/-45 deg —
// or at random (3% a tick within 5 u, +/-45 deg). Blocked hits count up; at 2/3/4/999
// (Easy / bb5c 0 / 1 / 2) blocks, or when the guard's 60 ticks run out, it breaks the guard
// (section 300) and is locked out of guarding for {60, 40, 5}[bb5c] ticks. Within 4 u,
// +/-45 deg, grounded, not 2+ below/above a grounded player: 22% a tick to strike
// (section 7, hold 132 / 142 on Easy, cooldown hold + 20..39).
// The block itself is the hit pipeline's (FUN_004fbb40: victim section 6, rtype <= 1,
// +/-75 deg) — see the view.
#include <cmath>

#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m180(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // +0x4d0
    static constexpr int kGuardLock[3] = {60, 40, 5};              // indexed by bb5c
    const int bb5c = e.diff_index;
    const bool bb58 = e.diff_easy;
    const int psec = e.target_section;
    bool can_atk = true;
    if (e.target_grounded && std::fabs(e.target.y - e.self.y) >= 2.0f) can_atk = false;

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            for (int off = 0x834; off <= 0x850; off += 4) a.f(off) = 0;   // only 32 bytes copied
            a.i(0x728) = 0; a.i(0x72c) = 0; a.i(0x730) = 0; a.i(0x878) = 0;
            return out;
        case 1:
            ai::idle(a, p, e);
            return out;
        case 3:
            if (a.action() == -1) {
                a.i(0x728) = 0;
                a.cool() = int(r15(e) % 20u) + 20;
            }
            return out;
        case 2: break;
        default: return out;
    }

    const float dist = a.dist();
    // A. leash + approach
    if (dist > 20.0f) ai::disengage(a);                            // the tick continues
    if (a.action() == 0) {
        if (a.i(0x7bc) != 1) { a.action() = 0x400; a.i(0x728) = 0; }
        a.i(0x7bc) = 0;
    }
    if (a.i(0x728) != 0) return out;

    int S = e.section;
    const Vec3 to = a.v(0x734);
    const float ang = float(std::atan2(double(to.z), double(to.x)));
    auto adiff = [&](float h) { return float(std::fabs(ai::angle_diff(h, ang))); };
    auto guard = [&] {
        if (ai::request(S, 6, /*no_restart*/ true, e)) out.section = 6;
        a.action() = 2; a.dur() = 60; a.cool() = 3;
    };
    const bool can_guard = (a.i(0x878) == 0 || S == 6) && S != 300 && S != 7 && a.i(0x72c) == 0;

    // B. reactive guard
    bool guarded = false;
    const std::uint32_t ups = std::uint32_t(psec);
    const bool p_attacking = ups >= 0x100u && ups <= 0x112u && psec != 0x109 && psec != 0x10b &&
                             psec != 0x10a && psec != 0x10c && psec != 0x10f;
    if (can_guard && p_attacking &&
        (dist < 5.0f || ((psec == 0x10e || psec == 0x111) && dist < 9.5f))) {
        if (adiff(a.heading) < 1.5707964f && adiff(e.target_heading) > 2.3561945f) {
            guard();
            guarded = true;
        }
    }
    // C. random guard
    if (!guarded && can_guard && dist < 5.0f) {
        const std::uint32_t r = r15(e) % 1000u;
        if (r < 30u && adiff(a.heading) < 0.7853982f && adiff(e.target_heading) > 2.3561945f) guard();
    }
    // D. attack
    if (dist < 4.0f && S != 7 && S != 6 && a.cool() == 0 && e.grounded && can_atk) {
        const std::uint32_t r = r15(e) % 1000u;
        if (r < 220u && adiff(a.heading) < 0.7853982f) {
            if (ai::request(S, 7, false, e)) { out.section = 7; out.attack = true; }
            a.action() = 1;
            a.dur() = bb58 ? 142 : 132;
            a.cool() = a.dur() + 20 + int(r15(e) % 20u);
            a.i(0x7bc) = 1;
        }
    }
    // E. guard bookkeeping
    if (S == 6) {
        if (e.was_hit && !e.took_damage) a.i(0x878) += 1;          // (+0x3bc & 6) == 4: blocked
        const int thr = bb58 ? 2 : bb5c == 0 ? 3 : bb5c == 1 ? 4 : 999;
        if (std::uint32_t(a.i(0x878)) >= std::uint32_t(thr) || a.dur() == 0) {
            if (ai::request(S, 300, false, e)) out.section = 300;  // guard break
            a.dur() = 6;
            a.cool() = 3;
            a.i(0x72c) = kGuardLock[bb5c >= 0 && bb5c <= 2 ? bb5c : 0];
            a.i(0x878) = bb58 ? 1 : 0;
        }
    } else if (e.took_damage && a.i(0x878) != 0) {
        a.i(0x878) -= 1;
    }
    // F.
    if (a.i(0x72c) != 0) a.i(0x72c) -= 1;
    return out;
}

}  // namespace cleria::brains
