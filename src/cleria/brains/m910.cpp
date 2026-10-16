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
// The jump helper's section-0x13a launch (K 0.1, a fixed 0.3 push; FACTORY.md: all Noozes
// are jump-capable).
#include <algorithm>
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {
namespace {

// The constants that differ between the Nooze colour variants (one routine; their brains
// diff to identical control flow — build/ai_spec/M920.md).
struct Nooze {
    float lunge_dist;     // M910 4.0, M920 4.5 (0x6bd830), M930 5.0 (0x6bd868)
    unsigned lunge_roll;  // per mille: 100, 150, 200
    int cool_per_d;       // the cooldown term (d + cool_d0) * this: 10, 15, 15
    int cool_d0;          //   6, 4, 3 (so 60 + 10d, 60 + 15d, 45 + 15d)
    unsigned cool_mod;    // the cooldown's random part R % this: 60, 60, 45
    int split;            // per mille outside the hit reaction: 30, 30, 230
    int hit_split;        // per mille in the hit reaction: 150, 200, 50
    unsigned pool_cap;    // 24, 26, 28
    int split_cast;       // itself: 0xb1, 0xb2, 0xb3 (M940: 0xb4)
    float chunk_keep;     // HP chunk = max(0.31 max, h - h*this): 0.5, 0.525, 0.55
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
                        a.cool() = int(r15(e) % k.cool_mod) + (d + k.cool_d0) * k.cool_per_d + a.dur();
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
    int thr = k.split;
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

    // Section 314, the jump helper's jump: at Marker 1 (grounded) the launch, K 0.1; at
    // Marker 2 with +0x9c0 == 2 a push only; either way channel 1 += 0.3 along the heading.
    if (sec == 0x13a && e.actor) {
        Actor& A = *e.actor;
        bool push = false;
        if (e.grounded && marker == 1) { launch(A, a.f(0x7dc) * 0.1f, out); push = true; }
        else if (marker == 2 && e.aso_marker_block == 2) push = true;
        if (push) push1(A, a.heading + 0.0f, 0.3f);
    }
    return out;
}

}  // namespace

AiOut m910(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    return nooze(a, p, e, {4.0f, 100u, 10, 6, 60u, 30, 150, 24u, 0xb1, 0.5f});
}
// M920 Nooze (Blue), FUN_004ed3c0 (AI 85): faster, poisoning lunge (its ASO's hitbox status 1).
AiOut m920(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    return nooze(a, p, e, {4.5f, 150u, 15, 4, 60u, 30, 200, 26u, 0xb2, 0.525f});
}
// M930 Nooze (Red), FUN_004ee000 (AI 86): the same routine (build/ai_spec/M930.md); it splits
// mostly on its own (23%) rather than when hit (5%); its ASO lunge is a hop (Dash + Jump)
// with a reaction-2 (knock-down) hit.
AiOut m930(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    return nooze(a, p, e, {5.0f, 200u, 15, 3, 45u, 230, 50, 28u, 0xb3, 0.55f});
}
// M940 Nooze (Yellow), FUN_004eec20 (AI 87): the same routine (build/ai_spec/M940.md); it
// splits almost only from the hit reaction (35%; 0.1% otherwise); its ASO lunge is Durable.
// M950 Nooze (Green) — brain FUN_004ef840 (AI index 88, handler FUN_004f0180 = M910's): M910 with 15
// one-for-one constant swaps (build/ai_spec/M950.md): lunge 5.5 u / 300 per mille, cooldown R%30 +
// 10(d + 3) + 60, splits 60 / 250 per mille, pool cap 30, split cast 0xb5, HP chunk 0.4·HP.
AiOut m950(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    return nooze(a, p, e, {5.5f, 300u, 10, 3, 30u, 60, 250, 30u, 0xb5, 0.6f});
}

AiOut m940(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    return nooze(a, p, e, {4.5f, 250u, 15, 3, 30u, 1, 350, 24u, 0xb4, 0.575f});
}
// M960 Nooze (Purple), FUN_004f0470 (AI 89, handler FUN_004f0d60 = M950's): build/ai_spec/M960.md. The brain has
// no difficulty block, so the cooldown is R%60 + 60 on any difficulty (cool_per_d 0). It lunges from 6.0 u at
// 400 per mille, splits 100 / 300 per mille under a pool cap of 36 into 0xb6, and keeps a 0.375 h chunk.
AiOut m960(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    return nooze(a, p, e, {6.0f, 400u, 0, 0, 60u, 100, 300, 36u, 0xb6, 0.625f});
}

}  // namespace cleria::brains
