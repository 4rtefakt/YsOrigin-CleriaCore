// M890 "Velagunder Spawn" (cast 0xaf) — handler FUN_004f7690 (AI index 82), brain FUN_004f6520.
// Spec: build/boss_re/VELAGUNDER.md §7 (re-read against the export and the bytes, 2026-10-07).
//
// Velagunder's run (state 1) drops these at phase >= 2 (the boss host writes +0x330, the knock
// channel and +0x72c after the spawn). Init (phase 0) goes straight to phase 2 with the ENEMYINFO
// mover row (8 floats) and the character / difficulty mods; for Yunica / Toal it zeroes +0x72c /
// +0x730, so the first engaged tick picks a waypoint and sets +0x72c = 1. Engaged:
//   - the BOSS STUN: +0x72c != 0 and +0x730 == 100 (the boss's stun, Boss01Output::minion_730):
//     the dizzy stars (EF2 60, scale 0.8, attached), phase 3, hold 99, section 0x13b, SE 184. The
//     tick then CONTINUES into the walk below (no return, 0x4f68e5 falls through).
//   - not Hugo (+0x728 == 0): a wanderer. Walk (0x20) to the waypoint W (steer 4); re-pick W when
//     within 1.25 of it and grounded, after 200 ticks, or within 4.0 (planar) of the player while
//     heading at them (+/-45 deg). PICK: up to 10 tries of 2 draws, L = R(11) + 5, a = R(360) deg,
//     v = (0, 0, L) x RotY(a); accept when the terrain ray self -> self + v is clear and
//     16.5 < self.z + v.z < 35.0 (the arena), else W = the player.
//   - Hugo (+0x728 == 1): +0x72c 1 chases (W = player); 2 flanks at +45 deg x (3 + R(30)/10) from
//     the player's side, and switches to the chase once +0x79c > R(350) + 150; 3 / 4 (-45 x 3.5 /
//     +45 x 4.0, switch at +0x730 x 100 - 100) are never set by the boss.
//   - phase 3 (the boss-stun hold): +0x730 == 1 (the boss's stun ended) zeroes the hold, which the
//     action player turns into the exit; otherwise the hold counts UP, so it never runs out.
// Harmless for Yunica / Toal: M_890.ASO's walk section arms its contact sphere for Hugo only.
// [not modelled] FUN_0058a5f0's slime puddle every 6 action ticks (Weather::blood exists now, this site is not
// wired; its 6 draws a puddle are not taken); phase 4 (the parent-spawned drop, +0x854: the boss spawns
// with mode 0, so it never runs); the shared-RNG identity (the host's brain RNG is its own).
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {
namespace {

constexpr float kDeg = 0.0174532924f;       // 0x6bd0dc
constexpr float kQuarterPi = 0.785398185f;  // 0x6bd3a0 / 0x3f490fdb

// p * RotationY(a), D3DX row vectors: (x cos + z sin, y, -x sin + z cos).
Vec3 rot_y(const Vec3& p, float a) {
    const float c = std::cos(a), s = std::sin(a);
    return {p.x * c + p.z * s, p.y, -p.x * s + p.z * c};
}

bool ray_clear(const AiEnv& e, const Vec3& o, const Vec3& d) {
    return !(e.world && e.world->ray(o, d).hit);           // FUN_005e9380 + FUN_0041e420(0, ...)
}

// The waypoint pick (0x4f6a18 / 0x4f6d63): 10 tries, else the target.
void pick(EnemyAi& a, const AiEnv& e) {
    for (int k = 0; k < 10; ++k) {
        const float L = float((e.rng() >> 17) % 11u + 5u);
        const float ang = float((e.rng() >> 17) % 360u) * kDeg;
        const Vec3 v = rot_y({0.0f, 0.0f, L}, ang);
        const float z = e.self.z + v.z;
        if (ray_clear(e, e.self, v) && z < 35.0f && 16.5f < z) {   // 0x6bda10 / 0x6bd978
            a.set_v(0x894, {e.self.x + v.x, e.self.y + v.y, z});
            return;
        }
    }
    a.set_v(0x894, e.target);
}

}  // namespace

AiOut m890(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    AiOut out;
    if (e.launched) return out;                              // +0x4d0 != 0
    Actor* A = e.actor;
    const Vec3 to = a.v(0x734);                              // the vector to the target (last tick's)
    const float dpl = std::sqrt(to.x * to.x + 0.0f + to.z * to.z);
    switch (a.phase()) {
        case 0: {
            ai::init_block(a, e.self, nullptr);              // FUN_004782f0
            for (int k = 0; k < 8; ++k) a.f(0x814 + 4 * k) = p.raw[k];   // EI+0x90..+0xac
            a.phase() = 2;
            a.action() = 0;
            a.i(0x728) = 0;
            float spd = a.f(0x818), turn = a.f(0x814);
            const float hp = A ? A->hp : 0.0f, def = A ? A->def : 0.0f;
            switch (e.character) {
                case 0: case 1:
                    if (A) A->max_hp = hp;
                    a.i(0x72c) = 0; a.i(0x730) = 0;
                    break;
                case 2:                                      // Hugo: x1.25 (0x6bd494), x1.4 (0x6bd4b4)
                    if (A) { A->hp = hp * 1.25f; A->max_hp = hp * 1.25f; A->def = def * 1.25f; }
                    a.i(0x728) = 1;
                    a.f(0x818) = spd * 1.4f;
                    a.f(0x814) = turn * 1.4f;
                    break;
                case 3:
                    if (A) { A->max_hp = hp; A->resist[0] = -1.0f; }   // +0xb4 = -1.0
                    a.i(0x72c) = 0; a.i(0x730) = 0;
                    break;
                default: break;
            }
            const bool hugo = e.character == 2;
            if (e.diff_index == 0) {
                if (e.diff_easy && A) {                      // HP, DEF x0.75 of the base (0x6bd390)
                    A->hp = hp * 0.75f; A->max_hp = hp * 0.75f; A->def = def * 0.75f;
                }
                return out;
            }
            if (e.diff_index == 2) {
                if (hugo) {
                    spd *= 1.5f; turn *= 1.5f;               // 0x6bd4d4
                    if (A) { A->hp *= 1.75f; A->max_hp *= 1.75f; }   // 0x6bd510
                } else {
                    spd *= 1.3f; turn *= 1.3f;               // 0x6bd4a4
                    if (A) { A->hp *= 1.5f; A->max_hp *= 1.5f; A->def = def * 1.25f; A->resist[0] = -0.2f; }
                }
            } else if (e.diff_index == 1) {
                if (hugo) {
                    spd *= 1.5f; turn *= 1.5f;
                    if (A) { A->hp *= 1.5f; A->max_hp *= 1.5f; }
                } else {
                    spd *= 1.3f; turn *= 1.3f;
                    if (A) { A->hp *= 1.25f; A->max_hp *= 1.25f; A->def = def * 1.25f; }
                }
            } else {
                return out;
            }
            a.f(0x814) = turn;
            a.f(0x818) = spd;
            return out;
        }
        case 1:
            ai::acquire(a);                                  // FUN_004758a0
            return out;
        case 2: break;
        case 3:
            if (a.i(0x730) == 1) a.dur() = 0;               // the boss's stun is over: the hold ends
            else a.dur() += 1;                               // held: counts up, never runs out
            return out;
        default:
            return out;                                      // [not modelled] phase 4 (parent drop)
    }

    // ---- phase 2
    if (a.i(0x72c) != 0 && a.i(0x730) == 100) {              // stunned with the boss
        AiEffect st{60, e.self, 0.8f, true};                 // the dizzy stars, attached (fx+0x1d5 = 1)
        st.rot = {0.0f, -1.5707964f - e.facing, 0.0f};       // yaw 0x6be078 - +0x314
        out.effects.push_back(st);
        a.phase() = 3;
        a.dur() = 99;
        a.i(0x724) = 1;
        int S = e.section;
        if (ai::request(S, 0x13b, false, e)) out.section = 0x13b;
        out.sounds.push_back(0xb8);                          // SE 184 i_piyopiyo
    }
    if (a.i(0x728) == 0) {                                   // the wanderer
        if (a.i(0x72c) == 0) {
            pick(a, e);
            a.i(0x72c) = 1;
            return out;
        }
        if (a.i(0x72c) != 1) return out;
        a.action() = 0x20;
        a.steer() = 4;
        const Vec3 W = a.v(0x894);
        const Vec3 dw{W.x - e.self.x, W.y - e.self.y, W.z - e.self.z};
        const float dW = std::sqrt(dw.x * dw.x + dw.y * dw.y + dw.z * dw.z);
        std::uint32_t n = std::uint32_t(a.i(0x7a0));
        bool repick = (dW < 1.25f && e.grounded) || n > 200u;          // 0x6bd494
        if (!repick && dpl < 4.0f) {                                   // 0x6bd7c0
            const float at = std::atan2(to.z, to.x);                   // toward the player
            const Vec3 s = a.v(0x748);                                 // the steer vector (to W)
            const float as = std::atan2(s.z, s.x);
            repick = std::fabs(float(ai::angle_diff(at, as))) < kQuarterPi;
        }
        if (repick) {
            pick(a, e);
            n = 0;
        }
        a.i(0x7a0) = int(n + 1);
    } else {                                                 // Hugo
        if (a.i(0x728) != 1) return out;
        Vec3 dir{-to.x, -0.0f, -to.z};                       // from the player toward self
        {
            const float l = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);   // FUN_0042cc50
            if (l != 0.0f) { const float k = 1.0f / l; dir = {dir.x * k, dir.y * k, dir.z * k}; }
        }
        a.action() = 0x20;
        auto flank = [&](float rot, float scale, std::uint32_t threshold) {
            const Vec3 v0 = rot_y(dir, rot);
            const Vec3 v{v0.x * scale, v0.y * scale, v0.z * scale};
            if (ray_clear(e, e.self, v)) a.set_v(0x894, {e.target.x + v.x, e.target.y + v.y, e.target.z + v.z});
            else a.set_v(0x894, e.target);
            if (std::uint32_t(a.i(0x79c)) > threshold) a.set_v(0x894, e.target);
        };
        switch (a.i(0x72c)) {
            case 1:
                a.set_v(0x894, e.target);
                break;
            case 2: {
                const float k = float((e.rng() >> 17) % 30u) / 10.0f + 3.0f;   // 0x6bd928 / 0x6bd680
                const Vec3 v0 = rot_y(dir, kQuarterPi);
                const Vec3 v{v0.x * k, v0.y * k, v0.z * k};
                if (ray_clear(e, e.self, v)) a.set_v(0x894, {e.target.x + v.x, e.target.y + v.y, e.target.z + v.z});
                else a.set_v(0x894, e.target);
                const std::uint32_t thr = (e.rng() >> 17) % 350u + 150u;
                if (std::uint32_t(a.i(0x79c)) > thr) a.set_v(0x894, e.target);
                break;
            }
            case 3: flank(-kQuarterPi, 3.5f, std::uint32_t(a.i(0x730) * 100 - 100)); break;   // 0x6bd720
            case 4: flank(kQuarterPi, 4.0f, std::uint32_t(a.i(0x730) * 100 - 100)); break;
            default: break;
        }
        a.steer() = 4;
        a.i(0x79c) += 1;
    }
    // [not modelled] every 6th +0x8a0 tick: FUN_0058a5f0(2, 10, 0.4, 0.8, origin, 0.2, 0, 1).
    return out;
}

}  // namespace cleria::brains
