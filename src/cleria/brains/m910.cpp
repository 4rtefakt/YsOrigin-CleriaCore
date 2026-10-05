// M910 Nooze — brain FUN_004ec790 (handler FUN_004ed0d0 = M100's minus the arena clamp,
// AI index 84). Spec: build/ai_spec/M910.md.
//
// A slime that moves in pulses: in its walk/run sections the move multipliers +0x81c/
// +0x820 ramp up 0.1/tick toward 1.0/3.0 only in the ASO's Marker(0) window and back
// down 0.15/tick otherwise. Engaged it approaches for 30..59 ticks, or (25%, only beyond
// 3.5) stands for 20..79. Within 4 u, +/-45 deg, grounded, every 10th tick, 10%: lunge
// (section 7), cooldown 120 + 10d + 0..59 with THIS brain's d = -(M100's d). It splits
// into a copy of itself (cast 0xb1) on ASO marker 9 (hit reaction 15%, run-cycle end 3%;
// needs >= 5 HP lost since init) or at a stand's start (3%; needs HP >= 10% max), while
// the actor pool holds < 24. Both halves keep HP - max(0.31 max, 0.5 HP) (floored to 1.1)
// and EXP x 0.5025.
// [not modelled] the keep-out box restore (FUN_0042dc90 over 0x76a2b4); the jump section
// 0x13a launch/push (needs the shared jump helper FUN_00476200, which the view lacks).
#include <algorithm>
#include <cmath>

#include "cleria/brains/brains.hpp"

namespace cleria::brains {
namespace {

// The constants that differ between the Nooze colour variants (one routine; their brains
// diff to identical control flow — build/ai_spec/M920.md).
struct Nooze {
    float lunge_dist;     // M910 4.0, M920 4.5 (0x6bd830)
    unsigned lunge_roll;  // per mille: 100, 150
    int cool_per_d;       // the cooldown's difficulty term: 60 + this*d (10, 15)
    int hit_split;        // per mille in the hit reaction: 150, 200
    unsigned pool_cap;    // 24, 26
    int split_cast;       // itself: 0xb1, 0xb2
    float chunk_keep;     // HP chunk = max(0.31 max, h - h*this): 0.5, 0.525
};

AiOut nooze(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e, const Nooze& k) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // +0x4d0
    const int d = -difficulty(e);                                  // sign flipped vs M100
    const int sec = e.section;
    const int marker = e.aso_marker;

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            a.f(0x7ac) = e.hp;
            a.f(0x7cc) = a.f(0x81c);                               // walk target (1.0)
            a.f(0x7d0) = a.f(0x820);                               // run target (3.0)
            a.f(0x81c) = 0; a.f(0x820) = 0; a.f(0x7d4) = 0;
            break;                                                 // falls into split + ramp
        case 1:
            a.i(0x70c) = 0;
            ai::idle(a, p, e);
            break;
        case 2: {
            const bool busy = (sec != 0 && sec != 1) || marker != 0;
            if (a.action() == 0) {
                a.action() = 0x400;                                // approach (run)
                a.dur() = int(r15(e) % 30u) + 30;
                if (r15(e) % 1000u < 250u && a.dist() > 3.5f) {    // both draws always happen
                    a.action() = 2;                                // stand, still turning
                    a.dur() = int(r15(e) % 60u) + 20;
                }
            } else if (a.action() == 0x400 && std::uint32_t(a.dur()) < 2u && !busy) {
                a.dur() = 2;                                       // keep sliding
            }
            if (a.dist() > 20.0f) {
                ai::disengage(a);
            } else if (busy && a.dist() < k.lunge_dist && sec != 7 && a.cool() == 0 && e.grounded &&
                       std::uint32_t(a.i(0x87c)) % 10u == 0) {
                if (r15(e) % 1000u < k.lunge_roll) {
                    const Vec3 to = a.v(0x734);
                    const float ang = float(std::atan2(double(to.z), double(to.x)));
                    if (0.785398185 > std::fabs(ai::angle_diff(a.heading, ang))) {
                        out.section = 7;                           // lunge; may be refused
                        out.attack = true;
                        a.dur() = 60;
                        a.action() = 1;
                        a.cool() = int(r15(e) % 60u) + 60 + k.cool_per_d * d + a.dur();
                    }
                }
            }
            if (a.i(0x728) == 100 && (sec != 0x13a || std::uint32_t(marker) >= 2u) && e.grounded)
                a.i(0x728) = 0;                                    // the jump helper's sub-state
            break;
        }
        default:
            break;
    }

    // Split (mitosis) — every phase.
    int thr = 30;
    if ((marker == 9 || (a.phase() == 2 && a.action() == 2 && a.cool() == 0)) && a.i(0x79c) == 0) {
        if (marker == 9) a.i(0x79c) = 1;
        if (a.action() == 2) a.cool() = a.dur();                   // one stand roll per stand
        const std::uint32_t r = r15(e) % 1000u;
        if (sec == 4) thr = k.hit_split;                                   // in the hit reaction
        if (r < std::uint32_t(thr) && std::uint32_t(e.pool_count) < k.pool_cap &&
            ((marker == 9 && a.f(0x7ac) - e.hp >= 5.0f) ||
             (a.action() == 2 && e.hp >= e.max_hp * 0.1f))) {
            float h = e.hp;
            const float chunk = std::max(e.max_hp * 0.31f, h - h * k.chunk_keep);
            h = h - chunk;
            AiSpawn c;
            c.cast = k.split_cast;
            c.pos = {e.self.x, e.self.y - e.height, e.self.z};
            c.yaw = a.heading;
            c.hp = h < 1.0f ? 1.1f : h;
            c.exp_copy = true;
            out.spawns.push_back(c);
            out.set_hp = h < 1.0f ? 1.1f : h;
            out.exp_scale = 0.5025f;
        }
    } else if (marker == 10) {
        a.i(0x79c) = 0;                                            // re-arm the latch
    }

    // The speed-multiplier ramp.
    float tgt = 0;
    if (sec == 0 || sec == 1 || sec == 0x13a || sec == 3) {
        tgt = sec == 0 ? a.f(0x7cc) : a.f(0x7d0);
        if (marker != 0 && sec != 0x13a && sec != 3) tgt = 0;
    }
    float cur = a.f(0x7d4);
    if (cur > tgt) { cur = std::max(tgt, cur - a.f(0x830)); a.f(0x7d4) = cur; }
    else if (tgt > cur) { cur = std::min(cur + a.f(0x82c), tgt); a.f(0x7d4) = cur; }
    a.f(0x81c) = cur;
    a.f(0x820) = cur;
    return out;
}

}  // namespace

AiOut m910(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    return nooze(a, p, e, {4.0f, 100u, 10, 150, 24u, 0xb1, 0.5f});
}
// M920 Nooze (Blue), FUN_004ed3c0 (AI 85): faster, poisoning lunge (its ASO's hitbox status 1).
AiOut m920(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    return nooze(a, p, e, {4.5f, 150u, 15, 200, 26u, 0xb2, 0.525f});
}

}  // namespace cleria::brains
