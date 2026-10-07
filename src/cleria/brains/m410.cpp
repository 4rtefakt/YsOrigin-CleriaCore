// M410, the hand Zarue (M570) summons — brain FUN_004f4bb0 (handler FUN_004f5310 = M100's minus
// the arena clamp; AI index 34). Spec: build/ai_spec/M410.md (M440's brain FUN_004b3e70 plus DEF 999
// and the surface-0x12 clamp; it even copies M440's ENEMYINFO record 34).
//
// Invisible and unhittable, it engages at once, waits 15..59 ticks, then appears 0.5 u from the
// player at the player's heading + a +- 0.4 rad (the other way when a wall is within 1 u), at the
// player's floor + 0.5, plays section 7 (side 0) / 301 (side 1) — whose GenerateCollision at t 38 is
// its only hurt (a radius-1 sphere) — faces the player, is hittable only while the section's marker
// is 1, fades from marker 2, and is removed R%20 + 160 - 10d ticks after it appears. It dies with its
// parent's HP and vanishes when the parent is stunned (phase 3).
// [not modelled] the keep-out box restore (FUN_0042dc90); a parent that has left the pool (the
// engine keeps a dangling pointer; here the parent tests are skipped).
#include <cmath>
#include <cstring>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m410(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // 0x4f4cc2 (the TAIL too)
    Actor* A = e.actor;
    const int ee = -difficulty(e);                                 // EDI: the difficulty, negated
    if (a.i(0x854) == 0 && A) a.i(0x854) = A->instance_id;         // no parent: its own
    switch (a.phase()) {
        case 0: {                                                  // INIT (0x4f512c)
            std::uint32_t save[16];
            std::memcpy(save, &a.blk[(0x79c - EnemyAi::kBase) / 4], sizeof save);   // +0x79c..+0x7db
            const float* raw = e.world ? e.world->enemy_raw(0x30) : nullptr;      // EI[34]: M440's row
            ai::init_block(a, e.self, raw ? raw : p.raw);
            a.hittable = false;
            a.alpha = 0.0f;
            a.i(0x72c) = 0; a.i(0x730) = 0;
            if (A) { A->def = 999.0f; A->flags12c |= 0x1000000u; }
            std::memcpy(&a.blk[(0x79c - EnemyAi::kBase) / 4], save, sizeof save);
            break;
        }
        case 1:                                                    // 0x4f510c
            ai::acquire(a);
            a.i(0x72c) = 0; a.i(0x730) = 0;
            break;
        case 2:
            if (std::uint32_t(a.i(0x72c)) > 0x66u) break;
            switch (a.i(0x72c)) {
                case 0:
                    a.hittable = false;
                    a.alpha = 0.0f;
                    a.i(0x72c) = a.i(0x7a4);
                    break;
                case 100:                                          // arm the wait
                    a.action() = -1;
                    a.i(0x72c) += 1;
                    a.i(0x71c) = int(r15(e) % 45u) + 15;
                    break;
                case 0x65: {                                       // appear at the player
                    if (a.i(0x71c) != 0) break;
                    out.section = a.i(0x7a0) != 0 ? 0x12d : 7;     // gated; result ignored
                    out.attack = true;
                    const float base = e.target_heading + a.f(0x7ac);
                    float dist = a.f(0x7b0);
                    const float so = a.i(0x7a0) != 0 ? -0.4f : 0.4f;   // 0x6be008 / 0x6bd2d4
                    if (dist >= 1.0f) {
                        const double th = double(so + base);
                        const float c = float(std::cos(th)), s = float(std::sin(th));
                        do {
                            if (!e.world || !e.world->ray(e.target, {c * dist, 0.0f, s * dist}).hit) break;
                            dist -= 1.0f;
                        } while (dist >= 1.0f);
                    }
                    dist -= 0.5f;
                    const double th2 = double(so + base);
                    if (A) {
                        A->pos.x = float(std::cos(th2)) * dist + e.target.x;
                        A->pos.z = float(std::sin(th2)) * dist + e.target.z;
                        A->pos.y = (e.target_floor + 0.5f) - A->height;   // +0x54 = T.+0x318 + 0.5
                    }
                    a.action() = -1;
                    a.i(0x71c) = int(r15(e) % 20u) + 160 + 10 * ee;
                    a.alpha = 1.0f;
                    a.i(0x72c) += 1;
                    break;
                }
                case 0x66: {
                    const Vec3 to = a.v(0x734);
                    a.heading = float(std::atan2(double(to.z), double(to.x)));
                    out.snap_facing = true;                        // [approx] the sprite faces it at once
                    if (a.i(0x71c) == 0) {
                        a.i(0x72c) = a.i(0x79c);
                        if (e.world && A) e.world->remove(*A);     // FUN_004593c0: no death
                        break;
                    }
                    const int m = e.aso_marker;
                    a.hittable = m == 1;
                    if (m == 2) {
                        a.alpha -= 0.05f;                          // 0x6bd13c
                        if (0.0f > a.alpha) a.alpha = 0.0f;
                    }
                    break;
                }
                default: break;
            }
            break;
        default: break;
    }
    // TAIL (0x4f5227)
    if (e.world && A) {
        Actor* P = a.i(0x854) == A->instance_id ? A : e.world->find(a.i(0x854));
        if (P) {
            if (1.0f > P->hp) out.set_hp = 0.0f;                   // the parent is down: so is the hand
            if (P->ai.phase() == 3) e.world->remove(*A);           // the parent is stunned: gone
        }
    }
    if (A && a.surf == 0x12 && a.water_y + 0.1f > A->pos.y + A->height) {   // 0x4f525e
        A->pos.y = (a.water_y + 0.1f) - A->height;
        a.surf = 1;
    }
    return out;
}

}  // namespace cleria::brains
