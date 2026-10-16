// M620 Ogacha — brain FUN_004ad980 (handler FUN_004ae5a0 = M100's minus the arena clamp, plus the bulb kill on
// the first dead tick; AI index 55). Spec: build/ai_spec/M620.md.
//
// A walker that carries a glowing bulb (EF2 447, a hit emitter at power -3) 0.85 ahead and 0.75 below its origin,
// re-spawned every 60 engaged ticks. Beyond 7 u or not facing the target (+-60 deg) it walks at it in 15-tick
// steps; inside, level with a grounded target, it runs one tick with its turn slowed to (0.2 + 0.15(d + 2)) x,
// then plays section 7 (117 ticks: the ASO's bite and, at t 72, the bulb fired as a bullet) standing with its
// heading locked — at Marker 2 it pounces (a 0.28 jump, a 0.1 + 0.04(d + 2) kick) and runs at the target while
// airborne. Cooldown 117 + 15(3 - d) + R%40. Grounded on surface 0x11 (blood water) it springs out (+1.6, 0.3).
// [not modelled] the handler's bulb kill on the first dead tick
// (FUN_0047a7c0: the bulb fades within its own 60-tick life anyway). [approx] the bulb is re-placed by
// position with the visible facing's yaw (the engine copies the actor's world matrix); each re-spawn takes the
// other of two tags so the orphaned bulb stays where it was, as in the engine.
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m620(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // 0x4adaa8
    Actor* A = e.actor;
    const int d = difficulty(e);
    const Vec3 V = a.v(0x734);
    const float dist = std::sqrt((V.x * V.x + 0.0f) + V.z * V.z);
    bool can_atk = true;
    if (!(std::fabs(e.target.y - e.self.y) < 2.5f) && e.target_grounded) can_atk = false;
    bool facing;
    {
        float hx = std::cos(a.heading), hz = std::sin(a.heading);
        const float hl = std::sqrt(hx * hx + 0.0f + hz * hz);
        hx = hl == 0.0f ? 0.0f : hx / hl; hz = hl == 0.0f ? 0.0f : hz / hl;
        const float ux = dist == 0.0f ? 0.0f : V.x / dist, uz = dist == 0.0f ? 0.0f : V.z / dist;
        facing = !(0.5f > (hx * ux + 0.0f) + hz * uz);
    }
    int& sub = a.i(0x72c);
    int& step = a.i(0x730);
    std::uint8_t& latch = reinterpret_cast<std::uint8_t*>(a.blk.data())[0x8a4 - EnemyAi::kBase];

    switch (a.phase()) {
        case 0:                                                    // INIT (0x4ae488)
            ai::init(a, p, e);
            a.f(0x7ac) = a.f(0x814);
            sub = 0; step = 0; a.i(0x88c) = 0;
            return out;
        case 1:                                                    // 0x4ae44e
            ai::idle(a, p, e);
            sub = 0; step = 0;
            a.f(0x814) = a.f(0x7ac);
            return out;
        case 2: break;
        default: return out;
    }
    if (a.dist() > 20.0f) ai::disengage(a);
    if (std::uint32_t(sub) <= 0x66u) {
        switch (sub) {
            case 0:
                step = 0; sub = 0x14; a.f(0x814) = a.f(0x7ac);
                break;
            case 0x14:
                if (a.dur() != 0) break;
                if (step == 0) {
                    if (dist > 7.0f || !facing) { a.action() = 0x10; a.steer() = 0; a.dur() = 15; break; }
                    if (!can_atk) break;
                    step = 1;
                } else if (step == 1) {
                    sub = 0x64; step = 0; a.i(0x79c) = 0;
                }
                break;
            case 0x64:                                             // one run tick, slow the turn
                a.action() = 0x400; a.steer() = 0; a.dur() = 0;
                a.f(0x814) = (float(d + 2) * 0.15f + 0.2f) * a.f(0x7ac);   // 0x6bd1d8, 0x6bd210
                latch = 1;
                sub = 0x65;
                break;
            case 0x65: {
                if (a.dur() != 0) break;
                out.section = 7; out.attack = true;                // gated; result ignored
                a.action() = 0x400; a.steer() = 0; a.dur() = 117;
                const int r = int(r15(e) % 40u);
                sub += 1;
                a.cool() = r + 15 * (-d + 3) + a.dur();
                break;
            }
            case 0x66: {                                           // the attack
                a.action() = e.grounded ? 1 : 0x400;
                const int c = a.cool();
                if (a.dur() == 0) { a.action() = 1; a.steer() = 0; a.dur() = c; }
                if (c == 0) { sub = a.i(0x79c); a.action() = 0x400; break; }
                if (latch != 0 && e.aso_marker == 2 && A) {        // the pounce, once
                    jump(*A, 0.28f, e.jump_v0, out);               // 0x6bd26c
                    push1(*A, a.heading, float(d + 2) * 0.04f + 0.1f);   // 0x6bd12c, 0x6bd19c
                    latch = 0;
                }
                break;
            }
            default: break;
        }
    }
    // TAIL (0x4ae0da), phase 2 only: the bulb, then the blood-water escape
    bool skip = false;
    int n = a.i(0x7a0);
    if (n == 0) {
        if (e.section == 7 && std::uint32_t(e.aso_marker) >= 3u) skip = true;
        else {
            const int tag = a.i(0x88c) == 1 ? 2 : 1;               // [approx] the next of two tags
            AiEffect fx;
            fx.id = 447;                                           // enemy\m_620_attack02.eft
            fx.pos = e.self;
            fx.rot = {0.0f, -1.5707964f - e.facing, 0.0f};
            fx.power = -3.0f; fx.knock = 1.0f;
            fx.tag = tag;
            out.effects.push_back(fx);
            a.i(0x88c) = tag;
            n = 60;
        }
    }
    if (!skip) {
        a.i(0x7a0) = n - 1;
        if (a.i(0x88c) != 0) {                                     // re-placed every tick
            AiEffect mv;
            mv.move_tag = a.i(0x88c);
            mv.pos = {e.self.x + float(std::cos(double(a.heading))) * 0.85f, e.self.y - 0.75f,
                      e.self.z + float(std::sin(double(a.heading))) * 0.85f};   // 0x6bd3bc, 0x6bd390
            mv.rot = {0.0f, -1.5707964f - e.facing, 0.0f};
            out.effects.push_back(mv);
        }
    }
    if (e.grounded && a.surf == 0x11 && A) {                       // WATER (0x4ae36f)
        a.surf = 1;
        A->vert.t = 0.3f;
        A->pos.y += 1.6f;                                          // 0x6bd4ec
        A->vert.grounded = false; out.set_own_jump = 1;
        push1(*A, a.heading, a.f(0x818) * 3.5f);                   // 0x6bd720
    }
    return out;
}

}  // namespace cleria::brains
