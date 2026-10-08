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
// M260, Bazeraena's (M670's) hand — brain FUN_004f29e0 (AI 19, handler FUN_004f3440 = M410's): build/ai_spec/M260.md.
// M440's brain with M410's DEF 999, its own ENEMYINFO record 16 and a far branch 0x6e -> 0x6f -> 0x70 (the only
// one M670 starts): it appears 6.5 u from the player (k - 0.5 for the first clear ray of 7, 6, .. 1), plays
// section 8 / 302, keeps its heading while the marker is 1 (the beam of EF2 52 flies along it) and is removed
// R%20 + 175 - 10d ticks after it appears. The near branch's removal (0x66) would clear +0x854 and then read
// [0 + 0x98] (an access violation, unreachable): not reproduced.
#include <cmath>
#include <cstring>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

namespace {

// M410 and M440 (Zeruena's hand, brain FUN_004b3e70, AI index 37; build/ai_spec/M440.md): the same
// brain minus M410's DEF 999 and its surface-0x12 clamp; M440's ASO adds a FireBullet at t 109.
enum class Hand { M410, M440, M260 };

AiOut hand(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e, Hand kind) {
    using ai::r15;
    const bool m410 = kind == Hand::M410, m260 = kind == Hand::M260;
    AiOut out;
    if (e.launched) return out;                                    // 0x4f4cc2 (the TAIL too)
    Actor* A = e.actor;
    const int ee = -difficulty(e);                                 // EDI: the difficulty, negated
    if (a.i(0x854) == 0 && A) a.i(0x854) = A->instance_id;         // no parent: its own
    switch (a.phase()) {
        case 0: {                                                  // INIT (0x4f512c)
            std::uint32_t save[16];
            std::memcpy(save, &a.blk[(0x79c - EnemyAi::kBase) / 4], sizeof save);   // +0x79c..+0x7db
            const float* raw = e.world ? e.world->enemy_raw(m260 ? 0x1e : 0x30) : nullptr;   // EI[34] M440's row; M260 EI[16]
            ai::init_block(a, e.self, raw ? raw : p.raw);
            a.hittable = false;
            a.alpha = 0.0f;
            a.i(0x72c) = 0; a.i(0x730) = 0;
            if (A) { if (m410 || m260) A->def = 999.0f; A->flags12c |= 0x1000000u; }   // M260 0x4f3326
            std::memcpy(&a.blk[(0x79c - EnemyAi::kBase) / 4], save, sizeof save);
            break;
        }
        case 1:                                                    // 0x4f510c
            ai::acquire(a);
            a.i(0x72c) = 0; a.i(0x730) = 0;
            break;
        case 2:
            if (std::uint32_t(a.i(0x72c)) > (m260 ? 0x70u : 0x66u)) break;
            switch (a.i(0x72c)) {
                case 0:
                    a.hittable = false;
                    a.alpha = 0.0f;
                    a.i(0x72c) = a.i(0x7a4);
                    break;
                case 0x6e:                                         // M260: the same entry as 100 (0x4f2b83)
                    if (!m260) break;
                    [[fallthrough]];
                case 100:                                          // arm the wait
                    a.action() = -1;
                    a.i(0x72c) += 1;
                    a.i(0x71c) = int(r15(e) % 45u) + 15;
                    break;
                case 0x6f:                                         // M260 0x4f2f42: 0x65's twin
                    if (!m260) break;
                    [[fallthrough]];
                case 0x65: {                                       // appear at the player
                    if (a.i(0x71c) != 0) break;
                    const bool far = a.i(0x72c) == 0x6f;
                    out.section = far ? (a.i(0x7a0) != 0 ? 0x12e : 8)   // gated; result ignored
                                      : (a.i(0x7a0) != 0 ? 0x12d : 7);
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
                    a.i(0x71c) = int(r15(e) % 20u) + (far ? 175 : 160) + 10 * ee;   // 0x4f31b1: 0xaf
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
                case 0x70: {                                       // M260 0x4f31cb
                    if (!m260) break;
                    const int m = e.aso_marker;
                    if (m != 1) {                                  // 0x4f31d4: the heading held while marker 1
                        const Vec3 to = a.v(0x734);
                        a.heading = float(std::atan2(double(to.z), double(to.x)));
                        out.snap_facing = true;                    // [approx] as 0x66
                    }
                    if (a.i(0x71c) == 0) {
                        a.i(0x72c) = a.i(0x79c);
                        if (e.world && A) e.world->remove(*A);     // FUN_004593c0 (+0x854 kept)
                        break;
                    }
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
    if (m410 && A && a.surf == 0x12 && a.water_y + 0.1f > A->pos.y + A->height) {   // 0x4f525e
        A->pos.y = (a.water_y + 0.1f) - A->height;
        a.surf = 1;
    }
    return out;
}

}  // namespace

AiOut m410(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) { return hand(a, p, e, Hand::M410); }
AiOut m440(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) { return hand(a, p, e, Hand::M440); }
AiOut m260(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) { return hand(a, p, e, Hand::M260); }

}  // namespace cleria::brains
