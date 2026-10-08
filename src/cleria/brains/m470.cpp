// M470 Levenueba — brain FUN_0048c300 (AI index 40; handler FUN_0048de00 = M100's minus the arena clamp, plus
// the grab release on death and the section-8 lifesteal: the view). Spec: build/ai_spec/M470.md.
//
// A big slime that always runs at its target. Every 10..29 ticks, off cooldown: within 5 u 30 % (target within 90
// deg) requests section 7, the glob fan — at Marker(315) 7 globs of that effect from a fixed 7-slot pattern (3 on
// Easy, 11 on Nightmare+), each a kind-0x29 lob on a coin flip, else a kind-1 shot (homing 1 deg a tick on d >= 2),
// gravity 0.004; cooldown R%30 + 30. Else within 7.5 u, when no other live Levenueba is mid-grab, 15 % (45 deg)
// requests section 8, the grab: while the script's marker is 1..13 a target within 1.4 + 0.15d of the mouth (0.75
// ahead, 0.5 down) is held there every tick (AiOut::hold_target), Levenueba gaining 25 DEF and knock channel 2
// only; struggling escapes past (d + 5) x 10 points. At the bite (marker = 2 mod 3) it spawns the poison-hit
// effect 255 and fast-forwards the script to Marker(14), where it lets go and sprays 3 more a tick. A hit it
// takes splashes effect 255 between the two and never alerts it (the was-hit bit is consumed).
// [not modelled] the keep-out box restore (FUN_0042dc90); the green particle bursts (FUN_0058a5f0: the trail, the
// bites, the splash) and their hidden RNG draws, the particle budget FUN_0047a8e0 (taken as free); FUN_00459ff0's
// floor / facing writes on the held player. [H] the camera nudge reads AiEnv::cam_back; the player's input byte
// +0x514 is AiEnv::target_moving (0 / 1).
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

namespace {
struct V3 { float x, y, z; };
const V3 kSlotPos[7] = {{-0.75f, 0.8f, 0.4f}, {0.75f, 0.8f, 0.4f}, {0.0f, 0.5f, 0.5f}, {-1.0f, 1.0f, 0.8f},
                        {1.0f, 1.0f, 0.8f}, {-1.0f, -0.2f, 0.7f}, {1.0f, -0.2f, 0.7f}};          // [ebp-0x108]
const V3 kSlotDir[7] = {{-0.2f, 1.2f, 0.8f}, {0.2f, 1.2f, 0.8f}, {0.0f, 0.4f, 0.6f}, {-0.5f, 1.5f, 0.8f},
                        {0.5f, 1.5f, 0.8f}, {-0.4f, 0.75f, 1.0f}, {0.4f, 0.75f, 1.0f}};          // [ebp-0x160]
const float kSpeed[7] = {0.15f, 0.15f, 0.2f, 0.1f, 0.1f, 0.1f, 0.1f};                            // [ebp-0xac]
Vec3 norm(const Vec3& v) {                                         // FUN_0042cc50
    const float l = std::sqrt((v.x * v.x + v.y * v.y) + v.z * v.z);
    return l == 0.0f ? Vec3{} : Vec3{v.x * (1.0f / l), v.y * (1.0f / l), v.z * (1.0f / l)};
}
}  // namespace

AiOut m470(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // 0x48c431: skips the tail too
    Actor* A = e.actor;
    const int d = difficulty(e);
    const int pS = e.target_section;
    const int S = e.section;
    bool other8 = false;                                           // another live Levenueba is in its grab
    if (e.world && A)
        e.world->for_each([&](Actor& c) {
            if (!other8 && c.cast_id == 0x33 && &c != A && c.hp >= 1.0f && c.live_section == 8) other8 = true;
        });
    if (A) A->flags12c &= ~0x40u;                                  // 0x48c509
    bool grabbed = false;
    auto adiff_target = [&] {
        const float an = float(std::atan2(double(a.f(0x73c)), double(a.f(0x734))));
        return float(std::fabs(ai::angle_diff(a.heading, an)));
    };
    auto puff = [&](const Vec3& pos, float yaw, float sc) {        // EF2 255 (m_poizonhit01: no hit emitter)
        AiEffect fx; fx.id = 0xff; fx.pos = pos; fx.scale = sc; fx.rot = {0.0f, yaw, 0.0f};
        out.effects.push_back(fx);
    };

    switch (a.phase()) {
        case 0:                                                    // INIT (0x48d7d9)
            ai::init(a, p, e);
            a.set_v(0x7e4, e.self);
            a.i(0x79c) = 0; a.i(0x7a0) = 0; a.i(0x7a4) = 0; a.i(0x7a8) = 0;
            a.f(0x7ac) = A ? A->def : 0.0f;
            a.i(0x70c) = 0;
            break;
        case 1:                                                    // UNAWARE (0x48d763)
            if (S == 7 || S == 8) { out.section = 2; out.section_force = true; }
            ai::idle(a, p, e);
            break;
        case 2: {                                                  // ENGAGED (0x48c53b)
            if (a.dist() > 20.0f) ai::disengage(a);                // inlined; the tick continues
            a.action() = 0x800;
            a.steer() = 0;
            if (S == 7) {                                          // VOLLEY_STATE (0x48c597)
                a.action() = 2;
                const int m = e.aso_marker;
                if (m != 0) {
                    out.set_marker = 0;
                    const float c = float(std::cos(double(a.heading))), s = float(std::sin(double(a.heading)));
                    const int nslot = d < 0 ? 3 : 7;
                    for (int i = 0; i < nslot; ++i) {
                        const V3& P = kSlotPos[i];
                        const Vec3 pos{(P.z * c + P.x * s) + e.self.x, P.y + e.self.y, (P.z * s + P.x * c) + e.self.z};
                        const int nshot = (d > 1 && i > 2) ? 2 : 1;
                        for (int j = 0; j < nshot; ++j) {
                            const std::uint32_t w1 = e.rng(), w2 = e.rng(), w3 = e.rng(), w4 = e.rng();
                            const V3& D = kSlotDir[i];
                            const Vec3 v{(c * D.z + s * D.x) + (unit(w1) * 0.6f - 0.3f), (unit(w2) * 0.6f - 0.1f) + D.y,
                                         (s * D.z + c * D.x) + (unit(w3) * 0.6f - 0.3f)};
                            AiBullet b;
                            b.effect = m; b.pos = pos; b.dir = norm(v);
                            b.speed = kSpeed[i]; b.gravity = 0.004f; b.knock = 1.0f;
                            if ((w4 & 0x20000u) != 0 && j == 0) {
                                b.kind = 0x29;
                                b.f168 = 65636.0f; b.f16c = 1.2f; b.f170 = 2.5f; b.f174 = 4.0f;   // raw 0x47803200: the burst
                            } else {
                                b.kind = 1;
                                b.home_period = 1; b.home_rate = d > 1 ? 0.0174533f : 0.0f; b.f170 = 360.0f;
                            }
                            out.bullets.push_back(b);
                        }
                    }
                }
                a.cool() = int(r15(e) % 30u) + 30;                 // every tick in section 7
            } else if (S == 8) {                                   // GRAB_STATE (0x48cab7)
                const bool dodge = (e.character == 1 && pS == 0x139) || (e.character == 2 && pS == 0x12c) ||
                                   (e.character == 3 && pS == 0x13d);
                a.action() = 2;
                const float sf = float(std::sin(double(e.facing))), cf = float(std::cos(double(e.facing)));
                const Vec3 M{e.self.x + cf * 0.75f, e.self.y - 0.5f, e.self.z + sf * 0.75f};   // the mouth
                const int m = e.aso_marker;                        // not consumed
                if (!dodge && a.i(0x7a8) == 0 && std::uint32_t(m - 1) <= 12u) {   // the hold (0x48cb93)
                    const Vec3 v = e.target - M;
                    const float d2 = (v.y * v.y + v.x * v.x) + v.z * v.z;
                    const float rr = float(d) * 0.15f + 1.4f;
                    if (rr * rr > d2 && ((v.x * cf + v.y * 0.0f) + v.z * sf >= 0.0f || 0.5f > d2)) {
                        a.action() = 1;
                        const Vec3 n = norm({e.cam_back.x, 0.0f, e.cam_back.z});
                        const Vec3 Q{M.x + n.x * 0.05f, (M.y + n.y * 0.05f) + 0.5f, M.z + n.z * 0.05f};
                        const Vec3 D = Q - e.target;
                        const Vec3 u = norm(D);
                        const Vec3 ex{D.x + u.x * 0.5f, D.y + u.y * 0.5f, D.z + u.z * 0.5f};
                        Vec3 F = Q;
                        if (e.world) {
                            const AiWorld::Ray r = e.world->ray(e.target, ex);
                            if (r.hit) {                           // stop 0.5 short of the wall
                                const Vec3 u2 = norm(ex);
                                F = {r.point.x - u2.x * 0.5f, r.point.y - u2.y * 0.5f, r.point.z - u2.z * 0.5f};
                            }
                        }
                        if (e.target_alive) {                      // the escape meter (0x48ce00)
                            const int b = e.target_moving ? 1 : 0;
                            if (b != 0 && b != a.i(0x7a0)) { a.i(0x7a4) += 1; a.i(0x7a0) = b; }
                            if (pS != 3 && pS != 0xb) a.i(0x7a4) += 3;
                            if (std::uint32_t(a.i(0x7a4)) > std::uint32_t((d + 5) * 10)) a.i(0x7a8) = 1;
                        }
                        a.i(0x79c) = 1;
                        if (A) A->flags12c |= 0x40u;
                        grabbed = true;
                        out.hold_target = true; out.hold_origin = F;
                    }
                }
                if (m % 3 == 2) {                                  // the BITE (0x48cedf): 2, 5, 8, 11, 14
                    const float an = float(std::atan2(double(a.f(0x73c)), double(a.f(0x734))));
                    puff(M, -1.5707964f - an, 1.3f);
                    out.fast_forward = 14;                         // FUN_00446680(14)
                }
                if (m == 14)                                       // the SPIT (0x48d177)
                    for (int k = 0; k < 3; ++k) {
                        const float sc = unit(e.rng()) * 0.5f + 1.0f;
                        const float an = float(std::atan2(double(a.f(0x73c)), double(a.f(0x734))));
                        puff(M, (unit(e.rng()) * 0.2f - 0.1f) - (an + 1.5707964f), sc);
                    }
            } else {                                               // DECIDE (0x48d4f9)
                a.i(0x7a0) = 0; a.i(0x7a4) = 0; a.i(0x7a8) = 0;
                if (a.dur() != 0) break;
                a.dur() = int(r15(e) % 20u) + 10;                  // D1
                if (a.cool() != 0) break;
                if (5.0f > a.dist()) {
                    if (r15(e) % 1000u < 300u && 1.5707964f > adiff_target()) {   // D2
                        out.section = 7; out.attack = true;        // gated; the 8 roll is skipped
                        break;
                    }
                }
                if (!other8 && 7.5f > a.dist()) {
                    if (r15(e) % 1000u < 150u && 0.7853982f > adiff_target()) {   // D3
                        out.section = 8; out.attack = true;        // gated
                    }
                }
            }
            break;
        }
        default: break;
    }

    // TAIL (0x48d887)
    {   // A. the mucus trail: one draw per 0.5 u moved [not modelled: the burst]
        const float dz = a.f(0x7ec) - e.self.z, dx = a.f(0x7e4) - e.self.x;
        if (dz * dz + dx * dx > 0.25f) {
            if (a.phase() != 2) (void)((e.rng() >> 17) & 0xfu);
            else (void)(r15(e) % 3u);
            a.set_v(0x7e4, e.self);
        }
    }
    if (e.was_hit) {                                               // B. the hit splash (0x48d9e0)
        out.consume_hit = true;                                    // +0x3bc &= ~4 before the action player
        const std::uint32_t w1 = e.rng(), w2 = e.rng(), w3 = e.rng();
        const Vec3 Q{(e.self.x + e.target.x) * 0.5f + (unit(w3) - 0.5f), (e.target.y + e.self.y) * 0.5f + (unit(w2) - 0.5f),
                     (e.target.z + e.self.z) * 0.5f + (unit(w1) - 0.5f)};
        const float an = float(std::atan2(double(a.f(0x73c)), double(a.f(0x734))));
        puff(Q, -1.5707964f - an, 1.3f);
    }
    if (grabbed) {                                                 // C. DEF and the release
        if (A) A->def = a.f(0x7ac) + 25.0f;
        return out;
    }
    if (A) A->def = a.f(0x7ac);
    if (a.i(0x79c) != 0) a.i(0x79c) = 0;                           // let go (no hold_target this tick)
    return out;
}

}  // namespace cleria::brains
