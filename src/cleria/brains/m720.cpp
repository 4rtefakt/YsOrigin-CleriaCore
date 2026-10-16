// M720 Heketie — brain FUN_004bd150 (AI index 65; handler FUN_004be0a0 = M100's minus the arena clamp, plus the
// 25-u far freeze: enemy_handler_far_freeze). Spec: build/ai_spec/M720.md.
//
// A winged healer that also shoots; its own distance and angle always read the PLAYER, while the action player's
// target is the ally +0x888 once one is picked. Sub-states (+0x79c): 0 idle — it stands facing the player or walks
// a few steps within 90 deg of its heading (a coin per 10..19-tick period), flees (1) 40 % a tick inside 4 u, and
// at each period, off its cooldown +0x7a4, inside 12 u and 60 deg, (48 + 10 max(d, 0)) % requests section 8 (5,
// the scripted fan). Every idle tick (2 % of unaware ones) it scans the wounded monsters within 15 u, the 5
// nearest, and runs (2) to the nearest with a clear line; within 5 u it requests section 7 (3) and at
// Marker(5080314) fires one shot OWNED BY THE ALLY from 1 u ahead: a kind-0xb heal (effect 314; 150 x (d + 1), 150
// / |d| on Easy) or, (20 + 15d) % of the time, a kind-1 attack (effect 508, power 5) homing on the player.
#include <algorithm>
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m720(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // 0x4bd264
    Actor* A = e.actor;
    const int d = difficulty(e);
    const Actor* P = e.world ? e.world->player() : nullptr;        // the player, never +0x888
    const Vec3 Pp = P ? Vec3{P->pos.x, P->pos.y + P->height, P->pos.z} : e.target;
    const float dx = Pp.x - e.self.x, dz = Pp.z - e.self.z;
    const float dist = float(std::sqrt(double(dz * dz + dx * dx)));
    const float h = a.heading;
    int s0 = a.i(0x79c);
    bool scan = false;
    auto parity = [&] { return int((e.rng() >> 17) & 1u); };
    auto rand_dir = [&](std::uint32_t span, int half) {            // (cos, sin) of heading +- half degrees
        const float an = float(int(r15(e) % span) - half) * 0.0174533f + h;
        a.f(0x748) = float(std::cos(double(an))); a.f(0x750) = float(std::sin(double(an)));
    };

    bool body = false;
    switch (a.phase()) {
        case 0:                                                    // INIT (0x4bdd71)
            ai::init(a, p, e);
            a.i(0x70c) = 0;
            a.i(0x79c) = 0; a.i(0x7a0) = 0; a.i(0x7a4) = 0;
            a.set_v(0x7e4, {});
            break;
        case 1:
            if (std::uint32_t(a.i(0x79c)) >= 2u) { body = true; break; }   // busy healing: as if engaged
            ai::idle(a, p, e);                                     // UNAWARE (0x4bd360)
            a.i(0x79c) = 0;
            scan = r15(e) % 1000u < 20u;
            s0 = -1;
            break;
        case 2: body = true; break;
        default: break;
    }
    if (body) {                                                    // ENGAGED_BODY (0x4bd32e)
        if (dist > 20.0f && std::uint32_t(a.i(0x79c)) < 2u) ai::disengage(a);   // the tick continues
        switch (a.i(0x79c)) {
            case 0: {                                              // IDLE (0x4bd3d4)
                scan = true;
                a.action() = 2; a.steer() = 0;
                if (4.0f > dist && r15(e) % 1000u < 400u) { a.i(0x79c) = 1; a.i(0x7a0) = 0; scan = false; }
                if (a.dur() == 0) {
                    const int b = parity();
                    a.i(0x7a0) = b;
                    if (b) rand_dir(180u, 90);
                    a.dur() = int(r15(e) % 10u) + 10;
                    if (a.i(0x7a4) == 0) {
                        const int r = int(r15(e) % 1000u);
                        if (r < std::max(d, 0) * 100 + 480 && 12.0f > dist) {
                            const float an = float(std::atan2(double(dz), double(dx)));
                            if (1.0471976f > float(std::fabs(ai::angle_diff(h, an)))) {
                                a.f(0x748) = dx; a.f(0x750) = dz;
                                a.i(0x79c) = 5;
                                out.section = 8; out.attack = true;   // gated
                            }
                        }
                    }
                }
                if (a.i(0x7a0) != 0) { a.action() = 0x20; a.steer() = 5; }
                break;
            }
            case 1:                                                // FLEE (0x4bd731)
                a.action() = 0x800; a.steer() = 3;
                if (a.dur() == 0) {
                    a.dur() = int(r15(e) % 7u) + 7;
                    const int b = parity();
                    a.i(0x7a0) = b;
                    if (b) rand_dir(241u, 120);
                }
                if (a.i(0x7a0) == 1) a.steer() = 5;
                if (dist > 7.0f) a.i(0x79c) = 0;
                break;
            case 2: {                                              // GOTO_ALLY (0x4bd8de)
                const Actor* A2 = a.i(0x888) != 0 && e.world ? e.world->find(a.i(0x888)) : nullptr;
                a.action() = 0x800; a.steer() = 4;
                if (a.i(0x888) == 0 || !A2) { a.i(0x79c) = 0; a.i(0x888) = 0; break; }
                if (1.0f > A2->hp) { a.i(0x888) = 0; break; }      // the ally died (+0x79c stays 2 this tick)
                a.set_v(0x894, {A2->pos.x, A2->pos.y + A2->height, A2->pos.z});
                if (4.0f > dist) { a.i(0x79c) = 1; a.i(0x888) = 0; break; }
                const float ey = a.f(0x898) - e.self.y, ex = a.f(0x894) - e.self.x, ez = a.f(0x89c) - e.self.z;
                if (25.0f > (ey * ey + ex * ex) + ez * ez) {
                    a.i(0x79c) = 3;
                    out.section = 7;                               // gated
                }
                break;
            }
            case 3:                                                // CAST (0x4bd9da)
                a.action() = 2; a.steer() = 4;
                if (e.section == 7) {
                    const int m = e.aso_marker;
                    if (m == 0) break;
                    out.set_marker = 0;
                    const int q = m / 10000, r = m - 10000 * q;    // 5080314 -> 508, 314
                    if (a.i(0x888) == 0 || std::uint32_t(r - 1) > 998u || std::uint32_t(q - 1) > 998u) break;
                    // SHOT (0x4bda64)
                    const int heal = d < 0 ? 150 / -d : 150 * d + 150;
                    const Vec3 dir{float(std::cos(double(e.facing))), 0.0f, float(std::sin(double(e.facing)))};
                    const float l = std::sqrt((dir.x * dir.x + dir.y * dir.y) + dir.z * dir.z);
                    const Vec3 u = l == 0.0f ? Vec3{} : Vec3{dir.x * (1.0f / l), dir.y * (1.0f / l), dir.z * (1.0f / l)};
                    const int rr = int(r15(e) % 1000u);
                    AiBullet b;
                    b.owner = a.i(0x888);                          // the ALLY owns the bullet
                    b.pos = {u.x + e.self.x, (u.y + 0.5f) + e.self.y, e.self.z + u.z};
                    b.dir = dir; b.speed = 0.075f; b.accel = 0.0001f;
                    b.home_period = 2; b.home_rate = 0.15707964f;
                    if (rr < std::max(0, 150 * d + 200)) {         // the disguised attack
                        b.kind = 1; b.effect = q; b.power = 5.0f; b.knock = 2.0f;
                    } else {                                       // the heal shot
                        b.kind = 0xb; b.effect = r; b.power = 20.0f; b.knock = 5.0f;
                        b.f168 = 1.0f; b.f16c = float(heal); b.f170 = 30.0f;
                    }
                    out.bullets.push_back(b);
                } else {
                    a.cool() = int(r15(e) % 60u) + 60;
                    a.i(0x79c) = 0; a.i(0x888) = 0;
                }
                break;
            case 5:                                                // ATTACK (0x4bdce2)
                a.action() = 2; a.steer() = 0;
                if (e.section != 8) {
                    a.i(0x79c) = 0;
                    a.i(0x7a4) = int(r15(e) % 60u) - 15 * d + 90;
                }
                break;
            default: break;                                        // 4 and > 5: nothing
        }
    }

    // END (0x4bde12)
    if (a.i(0x7a4) != 0 && a.i(0x79c) != 3) a.i(0x7a4) -= 1;
    if (scan && e.world && A) {                                    // ALLY_SCAN (0x4bde3e)
        int best_id[5] = {-1, -1, -1, -1, -1};
        float best_d2[5] = {225.0f, 225.0f, 225.0f, 225.0f, 225.0f};
        Vec3 best_o[5]{};
        e.world->for_each([&](Actor& o) {
            if (&o == A || o.is_player()) return;
            if (!(o.hp >= 1.0f)) return;
            if (!(o.max_hp - 1.0f > o.hp)) return;                 // at least 1 HP missing
            if (!(o.kind & 8u)) return;                            // monster side
            const Vec3 oo{o.pos.x, o.pos.y + o.height, o.pos.z};
            const float d2 = ((oo.y - e.self.y) * (oo.y - e.self.y) + (oo.x - e.self.x) * (oo.x - e.self.x)) +
                             (oo.z - e.self.z) * (oo.z - e.self.z);
            int k = 0;
            while (k < 5 && !(best_d2[k] > d2)) ++k;
            if (k == 5) return;
            for (int j = 4; j > k; --j) { best_d2[j] = best_d2[j - 1]; best_id[j] = best_id[j - 1]; best_o[j] = best_o[j - 1]; }
            best_d2[k] = d2; best_id[k] = o.instance_id; best_o[k] = oo;
        });
        a.i(0x888) = 0;
        for (int k = 0; k < 5 && best_id[k] >= 0; ++k)
            if (!e.world->ray(e.self, best_o[k] - e.self).hit) {   // a clear line
                a.i(0x888) = best_id[k]; a.i(0x79c) = 2;
                break;
            }
    } else if (scan) {
        a.i(0x888) = 0;
    }
    if (s0 != a.i(0x79c)) { a.dur() = 0; a.i(0x7a0) = 0; }         // a sub-state change restarts the timers
    return out;
}

}  // namespace cleria::brains
