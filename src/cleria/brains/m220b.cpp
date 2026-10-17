// M220 Chryolos — the boss-fight brain FUN_004ab840 (0x4ab840..0x4ac451), the one the handler FUN_004ac470 runs
// while DAT_0076bb8c (g_flags[156]) != 0 (0x4ac8cc): Pictimos's summons. Spec: build/ai_spec/M220B.md.
//
// A puppet: Pictimos (src/cleria/boss05) writes its sub +0x728 = 0x1e, mode +0x79c (0 orbit Pictimos, 1 hunt
// the player), target +0x888 (an instance id here; -2 = Pictimos; 0 = the player) and DEF (999 = held). INIT
// re-bases it on ENEMYINFO record 23, engages at once, pops it 0.2 up at a random scale 0.9..1.4 and alpha 0.1
// (+0.05 a tick). In sub 0x1e it never issues a move: it re-rolls the mover's raw speed +0x30c and turn rate
// +0x814 (1/20 a tick) and forces run mode while the idle section shows; mode 0 hops (5%) near a swinging
// player; mode 1 lunges (10%, within 3 u of the player after 100 ticks): section 7, and at its Marker 1 a leap
// at the target on knock channel 1 (pd / 13 x 2.5).
// The handler's death burst (SE 11 + EF2 67 when +0x130 & 0x2000) is the host's (view_boss05.inl).
#include <algorithm>
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m220_boss(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // +0x4d0 (0x4ab948)
    Actor* A = e.actor;
    const int Psec = e.target_section;
    const Vec3 v{a.f(0x734), 0.0f, a.f(0x73c)};
    const float pd = std::sqrt(v.x * v.x + 0.0f + v.z * v.z);
    const int S = e.section;
    a.i(0x7c0) += 1;
    const bool held = A && A->def == 999.0f;                       // +0xa8 == 999.0
    if (held) {
        a.speed = 0.16f; a.f(0x814) = 0.05f;
    } else if (a.i(0x728) == 0x1e) {
        if (a.i(0x79c) == 0) {
            if (r15(e) % 20u == 0) {
                a.speed = unit(e.rng()) * 0.07f + 0.12f;
                a.f(0x814) = unit(e.rng()) * 0.02f + 0.04f;
            }
        } else if (S != 7) {
            if (r15(e) % 20u == 0) {
                a.speed = unit(e.rng()) * 0.08f + 0.06f;
                a.f(0x814) = unit(e.rng()) * 0.02f + 0.02f;
            }
        }
    }
    if (A) {                                                       // +0x4d8: the fade-in
        if (A->hp >= 1.0f && 1.0f > a.alpha) { a.alpha += 0.05f; if (a.alpha > 1.0f) a.alpha = 1.0f; }
        if (1.0f > A->hp) a.alpha = 0;
    }
    switch (a.phase()) {
        case 0: {                                                  // INIT (0x4abd3e)
            const int keep = a.i(0x888);
            ai::init_block(a, e.self, nullptr);
            a.i(0x888) = keep;
            a.i(0x7c0) = 0;
            const float* ei = e.world ? e.world->enemy_raw(37) : nullptr;   // ENEMYINFO record 23 (cast 37)
            for (int k = 0; k < 8; ++k) a.f(0x814 + 4 * k) = ei ? ei[k] : 0.0f;
            a.phase() = 2;
            a.i(0x728) = 0;
            a.speed = 0.08f; a.f(0x818) = 0.02f; a.f(0x814) = 0.01f;
            a.i(0x79c) = 0;
            a.alpha = 0.1f;
            if (A) {
                A->flags12c |= 0x80200000u;
                A->flags130 |= 0xc009u;
                launch(*A, 0.2f, out);                             // +0x330 = 0.2, +0x54 += 0.2, airborne, own-jump
                const float s = unit(e.rng()) * 0.5f + 0.9f;
                A->scale = s;
                A->hurt_radius = s * 0.6f;
            }
            return out;
        }
        case 1:
            ai::idle(a, p, e);
            return out;
        case 3:
            if (A) A->guard399 = true;                             // byte +0x399 = 1, never cleared
            return out;
        case 2: break;
        default: return out;
    }
    const int sub = a.i(0x728);
    if (sub != 0x1e && sub != 0x1f && sub != 0x21 && a.action() == 0) {
        a.i(0x728) = 0; a.action() = 0x400; a.i(0x7bc) = 0;
    }
    if (a.i(0x728) == 0) {                                         // SUB 0 (0x4ac37d)
        if (r15(e) % 50u == 0 && e.grounded && A) launch(*A, unit(e.rng()) * 0.3f + 0.1f, out);
        return out;
    }
    if (a.i(0x728) != 0x1e) return out;
    // SUB 0x1e: the swarm
    if (15.0f > pd && a.i(0x79c) == 0 && std::uint32_t(Psec - 0x100) <= 8u && e.grounded) {
        if (r15(e) % 20u == 0 && A) A->vert.t = unit(e.rng()) * 0.15f + 0.25f;   // the hop: +0x330 only
    }
    if (a.i(0x7bc) == 0 && !held && S != 7 && std::uint32_t(a.i(0x7c0)) > 100u) {
        bool lunge = false;
        if (a.i(0x79c) == 1) {
            if (r15(e) % 10u == 0) {
                const Actor* P = e.world ? e.world->player() : nullptr;
                const Vec3 Po = P ? Vec3{P->pos.x, P->pos.y + P->height, P->pos.z} : e.target;
                const float dx = e.self.x - Po.x, dy = e.self.y - Po.y, dz = e.self.z - Po.z;
                lunge = 3.0f > std::sqrt(dy * dy + dx * dx + dz * dz);
            }
        } else if (a.i(0x79c) == 2) {
            lunge = r15(e) % 20u == 0;
        }
        if (lunge) {                                               // LUNGE (0x4ac187)
            if (A) {
                A->vert.t = 0.3f;
                push1(*A, a.heading, a.f(0x7e0) * 0.1f);           // +0x7e0 is 0 in practice
            }
            a.speed = 0.17f;
            a.f(0x814) = 0;
            out.section = 7;                                       // gated
            a.action() = 2; a.i(0x7bc) = 1; a.i(0x888) = 0;
        }
    }
    if (S == 2) a.mode() = 2;                                      // idle showing -> run mode
    if (S == 7 && e.aso_marker == 1 && a.i(0x7bc) == 1 && A) {      // the leap at Marker 1
        a.action() = 0x400; a.i(0x7bc) = 0;
        const float k = pd / 13.0f;
        const float an = float(std::atan2(double(v.z), double(v.x)));
        A->mover.k1.x += float(std::cos(double(an))) * k * 2.5f;
        A->mover.k1.z += float(std::sin(double(an))) * k * 2.5f;
    }
    return out;
}

}  // namespace cleria::brains
