// M340 Rezalior — brain FUN_00493240 (AI 27) and M380 Chelasken — brain FUN_004953e0 (AI 31):
// one routine (build/ai_spec/M340.md, M380.md; their pseudocode matches line for line). Both
// handlers are M100's minus the arena clamp (M340's dead path also sets +0x12c |= 1).
//
// Flying breeders. They hover at 2.5, or 3.5 when the target is near (< 7 u horizontal, not
// over 1 above, and the breeder not over 6.6 above the target's floor), and home on the
// PLAYER plus a 1.5 offset re-picked 8% a tick (40..59 lockout; the atan2 takes (x, z) —
// swapped vs the engine basis — ported literally). Within 5.5 u, off cooldown, with fewer
// live children than the cap (1 Easy / 2 Normal / 3 Hard+, +2 group-tagged) and 1.5 u clear
// ahead, (6 - children)% a tick: section 7, hold 50, cooldown 150 (80 tagged); at its Marker 1
// one child (M340: Perrie 0x3a, M380: Celny 0x29) 0.8 ahead and 0.25 down. Drip bursts
// (10% roll, 120..199 ticks, every (60..99)th tick) drop a bullet 170..189 deg behind, falling
// at 0.09, scaled 1.1..2.3. From the 61st tick without taking damage it arms a 0.3 sting
// sphere 0.5 behind itself every tick (FUN_00477f00: fresh serial, life 1, re-hit 200).
// [not modelled] the keep-out box restore (FUN_0042dc90).
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {
namespace {

AiOut breeder(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e, int child_cast, int drip_effect) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;
    Actor* A = e.actor;
    const Vec3 to = a.v(0x734);
    const float bias = (e.self.y - a.f(0x754) > 6.6f || to.y > 1.0f) ? 10000.0f : 0.0f;
    const float deff = float(std::sqrt(double(to.x * to.x + to.z * to.z))) + bias;
    const bool tag = e.group_tag;

    // The sting (every phase).
    if (e.took_damage) a.i(0x7a4) = 0;
    if (std::uint32_t(a.i(0x7a4)) > 60u) {
        AiHitbox h;
        h.shape = 0; h.radius = 0.3f; h.fwd = -0.5f; h.angle_deg = 0; h.voff = 0; h.half_h = 0;
        h.life = 1; h.rehit = 200; h.one_shot = 0; h.rtype = 0; h.status = 0; h.power = 0; h.knock = 0;
        h.new_serial = true;
        out.hitboxes.push_back(h);
    }
    a.i(0x7a4) += 1;

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            break;
        case 1:
            ai::idle(a, p, e);
            if (a.i(0x79c) != 0) a.i(0x79c) -= 1;
            else if (r15(e) % 1000u < 100u) {
                a.i(0x7a0) = int(r15(e) & 1u);
                a.i(0x79c) = int(r15(e) % 60u) + 120;
            }
            a.f(0x834) = a.i(0x7a0) != 0 ? 3.5f : 2.5f;
            break;
        case 3:
            for (int off : {0x7a0, 0x79c, 0x7c8, 0x7c4, 0x7c0, 0x7bc}) a.i(off) = 0;
            break;
        case 2: {
            if (a.dist() > 20.0f) ai::disengage(a);               // the tick continues
            a.f(0x834) = 7.0f > deff ? 3.5f : 2.5f;
            if (a.i(0x7c4) != 0) a.i(0x7c4) -= 1;                  // B: the drip scheduler
            if (a.i(0x7c4) == 0 && r15(e) % 1000u < 100u) {
                a.i(0x7c0) = int(r15(e) % 80u) + 120;
                a.i(0x7c4) = a.i(0x7c0) + 20 + int(r15(e) % 20u);
            }
            if (a.action() == 0) { a.action() = 0x400; a.steer() = 4; a.i(0x728) = 0; }   // C
            const int sub = a.i(0x728);
            if (sub == 0) {                                        // D: the orbit point + breed roll
                if (a.i(0x7c8) != 0) a.i(0x7c8) -= 1;
                if (r15(e) % 1000u < 80u && a.i(0x7c8) == 0) {
                    const std::uint32_t j = e.rng();
                    const float ox = (a.f(0x7cc) + e.target.x) - e.target.x;
                    const float oz = (a.f(0x7d0) + e.target.z) - e.target.z;
                    float th = float(std::atan2(double(ox), double(oz)));   // swapped (literal)
                    th += float(int((j >> 17) % 120u) - 60) * 3.14159f / 180.0f;
                    a.f(0x7cc) = float(std::cos(double(th))) * 1.5f;
                    a.f(0x7d0) = float(std::sin(double(th))) * 1.5f;
                    a.i(0x7c8) = int(r15(e) % 20u) + 40;
                }
                if (a.dist() > 8.0f) { a.f(0x7cc) = 0; a.f(0x7d0) = 0; }
                // the steer-4 point: the PLAYER (the env's target here) + the offset
                a.set_v(0x894, {e.target.x + a.f(0x7cc), e.target.y, e.target.z + a.f(0x7d0)});
                if (5.5f > deff && e.section != 7) {
                    int n = 0;                                     // FUN_00476ef0: live children
                    if (e.world && A)
                        e.world->for_each([&](Actor& c) {
                            if (c.hp != 0.0f && c.ai.i(0x854) == A->instance_id) ++n;
                        });
                    if (a.cool() == 0) {
                        int cap = e.diff_easy ? 1 : (e.diff_index != 0 ? 3 : 2);
                        cap += tag ? 2 : 0;
                        if (n < cap && int(r15(e) % 1000u) < (6 - n) * 10) {
                            const Vec3 d{float(std::cos(double(a.heading))) * 1.5f, 0,
                                         float(std::sin(double(a.heading))) * 1.5f};
                            if (!(e.world && e.world->ray(e.self, d).hit)) {
                                out.section = 7; out.attack = true;   // gated; unchecked
                                a.action() = 1; a.dur() = 50; a.i(0x728) = 1;
                                a.cool() = tag ? 80 : 150;
                                a.i(0x7a8) = 0;
                            }
                        }
                    }
                }
            } else if (sub == 1) {                                 // E: the child at Marker 1
                if (e.section == 7 && e.aso_marker == 1 && a.i(0x7a8) == 0) {
                    a.i(0x7a8) = 1;
                    const Vec3 off{float(std::cos(double(a.heading))) * 0.8f + 0.0f, -0.25f,
                                   float(std::sin(double(a.heading))) * 0.8f + 0.0f};
                    if (e.world && A)
                        if (Actor* c = e.world->spawn_child(*A, child_cast, off, A->level, true)) {
                            c->ai.i(0x854) = A->instance_id;
                            c->ai.f(0x7f0) = a.heading;
                        }
                }
            }
            break;
        }
        default: break;
    }

    // TAIL: the drip (every phase).
    if (a.i(0x7c0) == 0) return out;
    a.i(0x7c0) -= 1;
    if (std::uint32_t(a.i(0x8a0)) % std::uint32_t(a.i(0x7bc) + 60) != 0) return out;
    a.i(0x7bc) = int(r15(e) % 40u);
    const std::uint32_t w2 = e.rng(), w3 = e.rng();                // RAW
    const float ang = float(int(unit(w2) * 20.0f + 170.0f)) * 0.0174533f;
    const float rr = unit(w3) * 0.25f + 0.25f;
    AiBullet b;
    b.pos.x = std::cos(ang + a.heading) * rr + e.self.x;
    const std::uint32_t w4 = e.rng();                              // RAW
    b.pos.y = (unit(w4) * 0.5f - 0.5f) + e.self.y;
    b.pos.z = std::sin(ang + a.heading) * rr + e.self.z;
    b.kind = 0x10; b.effect = drip_effect;
    b.dir = {0, -1.0f, 0};
    b.speed = 0.09f; b.knock = 1.0f; b.f168 = 1.0f;
    // The proxy is released at once (no batting); then FUN_00451a40 re-seats the effect at scale
    // k and A's stat block is copied in (only later-armed particles would see it).
    b.fx_scale = unit(e.rng()) * 1.2f + 1.1f;                      // RAW; the bullet is always found here
    out.bullets.push_back(b);
    return out;
}

}  // namespace

AiOut m340(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    return breeder(a, p, e, 0x3a, 500);                            // Perrie; enemy\m_380_02.eft
}
AiOut m380(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    return breeder(a, p, e, 0x29, 264);                            // Celny; enemy\m_370_02.eft
}

}  // namespace cleria::brains
