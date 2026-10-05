// The two first-floor turrets — the same brain routine with different constants:
//   M150 Normous  FUN_0047c5d0 (handler FUN_0047cce0, AI index 8):  range d+10, volley 130
//   M480 Nustarl  FUN_00480f50 (handler FUN_00481650, AI index 41): range d+11, volley 100
// Specs: build/ai_spec/M150.md, M480.md. Base speed 0 — they never move. Engaged, the
// heading steers (mode 4) at one of two FIXED world waypoints 30 u out, (30,0,30) or
// (30,0,0), i.e. heading pi/4 or 0, selected by +0x79c; each rest flips it, so the
// four-way volleys of ASO section 7 (FireBullet x4 at facing + 0/90/180/270, effect 310)
// alternate between an "x" and a "+". A damaging hit before the volley fires can cut the
// stand short and re-fire, up to max(d+1, 0) times. Handlers: as M100's minus the arena
// clamp FUN_0047abf0. [not modelled] the keep-out box revert (FUN_0042dc90 over 0x76a2b4).
#include <cmath>

#include "cleria/brains/brains.hpp"

namespace cleria::brains {
namespace {

AiOut turret(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e, float range_add, int volley) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // +0x4d0
    const int d = difficulty(e);
    const Vec3 to = a.v(0x734);
    const float pd = std::sqrt(to.x * to.x + 0.0f + to.z * to.z);  // planar, last tick's vector
    const bool tag = e.group_tag;                                  // +0x130 & 0x700
    const Vec3 wpA{e.self.x + 30.0f, e.self.y + 0.0f, e.self.z + 30.0f};
    const Vec3 wpB{e.self.x + 30.0f, e.self.y + 0.0f, e.self.z + 0.0f};

    switch (a.phase()) {
        case 0:                                                    // init
            ai::init(a, p, e);
            a.i(0x728) = 0; a.i(0x72c) = 0; a.i(0x730) = 0;
            a.i(0x7a4) = int(r15(e) % std::uint32_t((4 - d) * 15));   // first-rest jitter
            return out;
        case 1:                                                    // unaware
            ai::idle(a, p, e);
            a.i(0x70c) = 2;                                        // from now on: stand, heading held
            a.i(0x728) = 0; a.i(0x72c) = 0; a.i(0x730) = 0;
            a.i(0x79c) = int(r15(e) & 1u);                         // first orientation
            return out;
        case 2: break;
        default: return out;
    }

    if (a.dist() > 20.0f) ai::disengage(a);                       // continues this tick
    a.set_v(0x894, a.i(0x79c) != 0 ? wpB : wpA);
    a.steer() = 4;
    int& sub = a.i(0x728);
    const int K = (3 - d) * 40;
    const int next = K != 0 ? 10 : 0;
    switch (sub) {
        case 0:
            if (float(d) + range_add < pd && !tag) { sub = 10; return out; }
            out.section = 7;                                       // the volley
            out.attack = true;
            sub += 1;
            a.action() = 2;
            a.dur() = volley;
            a.cool() = volley;
            return out;
        case 1: {
            if (a.action() == 0) {                                 // volley ran its length
                sub = next; a.i(0x7a0) = 0;
                if (K == 0) a.i(0x79c) ^= 1;
                return out;
            }
            if (!e.took_damage) return out;                        // +0x3bc & 2
            if (e.aso_marker != 0) return out;                     // already fired
            const int limit = d >= 0 ? d + 1 : 0;
            const int c = a.i(0x7a0)++;
            if (std::uint32_t(c) < std::uint32_t(limit)) {         // cut short, re-fire after 30
                a.action() = 2; a.steer() = 1; a.dur() = 30; a.i(0x7a4) = 0; sub = 11;
                return out;
            }
            sub = next; a.i(0x7a0) = 0;
            if (K == 0) a.i(0x79c) ^= 1;
            return out;
        }
        case 10:                                                   // rest, flip orientation
            a.action() = 2; a.steer() = 1;
            a.dur() = a.i(0x7a4) + (3 - d) * (tag ? 30 : 40);
            a.i(0x79c) ^= 1;
            a.i(0x7a0) = 0; a.i(0x7a4) = 0;
            sub += 1;
            return out;
        case 11:
            if (a.action() == 0) sub = 0;
            return out;
        default:
            return out;
    }
}

}  // namespace

AiOut m150(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) { return turret(a, p, e, 10.0f, 130); }
AiOut m480(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) { return turret(a, p, e, 11.0f, 100); }

}  // namespace cleria::brains
