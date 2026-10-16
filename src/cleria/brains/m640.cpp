// M640 Samachryce — brain FUN_004b74c0 (handler FUN_004b8e30 = M100's minus the arena clamp, AI index
// 57). Spec: build/ai_spec/M640.md (§7: its kind-0x2a boomerang, src/cleria/bullet.cpp).
//
// Every 5..19 ticks it re-plans by distance: run at the target beyond 4.5, back off (knock channel 2 at
// walk speed) inside 3.5, else strafe. Grounded, off cooldown, inside 6 u and 45 deg, (15 + 10d)% a
// re-plan: section 7 — a hop at Marker 1 (0.175, 0.3 to a target 1.5 above) and a 0.6 lunge at Marker
// 2, skipping to Marker 4 when it lands early. Inside 6.5 u and 40 deg: section 8 — it jumps back and at
// Marker(260) throws 2 / 3 / 4 boomerangs (kind 0x2a, effect 260, 0.35 slowing 0.0035 a tick, then home)
// pitched down toward a lower target. Cooldowns 135 - 30d + R%60 (melee) / 360 - 90d + R%60 (throw), a
// coin zeroing them. When the player attacks while facing it, now and then it hops aside, back, or
// forward (x2 on a coin); a blocked back-off / strafe can trigger the same hops.
// [approx] FUN_00446680 (skip to Marker 4)
// runs the host's fast_forward_to_marker.
#include <algorithm>
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m640(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;
    Actor* A = e.actor;
    const int d = difficulty(e);
    const int q = d * d;
    const int PS = e.target_section;                               // the PLAYER's section
    const bool tag = e.group_tag;
    int sel = 0;
    if (a.i(0x79c) != 0) a.i(0x79c) -= 1;
    float hop_vy = 0.3f;                                           // 0x6bd27c
    const bool gnd = e.grounded;
    const float h = a.heading;
    int S = e.section;
    int marker = e.aso_marker, mblock = e.aso_marker_block;
    const int T1 = d < 0 ? 200 : 200 * q + 500;
    auto adiff_to_target = [&] {
        const float an = float(std::atan2(double(a.f(0x73c)), double(a.f(0x734))));
        return float(std::fabs(ai::angle_diff(h, an)));
    };
    auto ray_hit = [&](const Vec3& o, const Vec3& dl) { return e.world && e.world->ray(o, dl).hit; };

    switch (a.phase()) {
        case 0:                                                    // INIT (0x4b8978)
            ai::init(a, p, e);
            a.i(0x70c) = 0;
            a.i(0x79c) = a.i(0x7a0) = a.i(0x7a4) = a.i(0x7a8) = 0;
            a.i(0x72c) = 0;
            break;
        case 1:                                                    // UNAWARE (0x4b894e)
            ai::idle(a, p, e);
            a.i(0x72c) = 0; a.cool() = 0; a.i(0x79c) = 0;
            break;
        case 2: {                                                  // ENGAGED (0x4b76bb)
            if (a.dist() > 20.0f) ai::disengage(a);                // the tick continues
            const int old708 = a.steer(), old710 = a.action();
            a.steer() = 0; a.action() = 0x800;                     // default: run at T
            if (a.i(0x728) == 100 && (S != 0x13a || std::uint32_t(marker) >= 2u) && gnd) a.i(0x728) = 0;
            if (S == 7) {                                          // MELEE (0x4b7733)
                a.action() = e.turnable ? 2 : 1;
                if (mblock != 4 && marker == 1) {
                    if (1.5f > e.target.y - e.self.y) hop_vy = 0.175f;
                    if (gnd && A) { A->pos.y += hop_vy; A->vert.t = hop_vy; A->vert.grounded = false; out.set_own_jump = 1; }
                    if (A) push1(*A, h, 0.1f);
                    mblock = 4; out.set_marker_block = 4;
                } else if (marker == 2) {                          // the lunge
                    if (A) push1(*A, h, 0.6f);
                    marker = 3; out.set_marker = 3;
                    mblock = 4; out.set_marker_block = 4;
                } else if (mblock == 4 && gnd) {                   // landed before Marker 4
                    out.fast_forward = 4;                          // FUN_00446680(marker 4)
                    mblock = 0; out.set_marker_block = 0;
                    marker = 4;                                    // [approx] the skip lands on Marker 4
                }
                if (marker == 4 && gnd) out.set_marker_block = 0;
                a.cool() = a.i(0x7a8) == 0 ? int(r15(e) % 60u) - 30 * d + 135 : 0;   // every tick
                break;
            }
            if (S == 8) {                                          // THROW (0x4b7970)
                a.action() = 2;
                const int m = marker;
                if (m != 0) {
                    out.set_marker = 0;
                    const float c = float(std::cos(double(h))), s = float(std::sin(double(h)));
                    const float v = a.f(0x738) > 0.0f ? 0.0f : -a.f(0x738);
                    const float hd = float(std::sqrt(double(a.f(0x734)) * a.f(0x734) + double(a.f(0x73c)) * a.f(0x73c)));
                    const double ang = double(float(std::atan2(double(hd), double(v))));
                    const float sp = float(std::sin(ang));
                    const int N = std::max(std::min(d, 2), 0) + 2;
                    static const Vec3 POS[2] = {{-0.75f, 0.75f, 0.5f}, {0.75f, 0.75f, 0.5f}};
                    static const Vec3 DIR[4] = {{0.1f, 0, 1}, {-0.1f, 0, 1}, {-0.1f, 0, 1}, {0.1f, 0, 1}};
                    auto norm = [](Vec3 x) {                       // FUN_0042cc50
                        const float l = std::sqrt(x.x * x.x + x.y * x.y + x.z * x.z);
                        return l == 0.0f ? Vec3{} : Vec3{x.x * (1.0f / l), x.y * (1.0f / l), x.z * (1.0f / l)};
                    };
                    for (int i = 0; i < N; ++i) {
                        const Vec3& Pt = POS[i & 1];
                        const Vec3& Dt = DIR[i];
                        AiBullet b;
                        b.kind = 0x2a; b.effect = m;
                        b.pos = {(Pt.z * c + Pt.x * s) + e.self.x, Pt.y + e.self.y, (Pt.z * s + Pt.x * c) + e.self.z};   // (s, c): literal
                        const float uA = unit(e.rng()), uB = unit(e.rng());
                        Vec3 dir{(Dt.z * c + s * Dt.x) + (uA * 0.2f - 0.1f), Dt.y, (Dt.z * s + c * Dt.x) + (uB * 0.6f - 0.2f)};
                        dir = norm(dir);
                        dir.x *= sp; dir.y = -float(std::cos(ang)); dir.z *= sp;
                        b.dir = norm(dir);
                        b.speed = 0.35f; b.accel = -0.0035f; b.gravity = 0; b.power = 0; b.knock = 1.0f;
                        b.f168 = 0.3f; b.f16c = 0.1f; b.f170 = 15.0f;
                        out.bullets.push_back(b);
                    }
                }
                a.i(0x79c) = a.i(0x7a8) == 0 ? int(r15(e) % 60u) - 90 * d + 360 : 0;   // every tick
                break;
            }
            if (a.dur() != 0) {                                    // MOVE (0x4b829f)
                const int plan = a.i(0x72c);
                if (plan != 1 && plan != 2) break;
                auto walk_or_block = [&](const Vec3& bdir, int cancel, std::uint32_t mod) {
                    const bool wall = ray_hit(e.self, bdir);
                    const bool floor = !wall && ray_hit({e.self.x + bdir.x, e.self.y + bdir.y, e.self.z + bdir.z}, {0, -1.5f, 0});
                    if (!wall && floor) {
                        const float k = a.f(0x81c) * a.f(0x818);
                        if (A) { A->mover.k2.x += bdir.x * k; A->mover.k2.z += bdir.z * k; }
                    } else {
                        a.mode() = 0;
                        if (int(r15(e) % std::uint32_t(30000 - 4000 * d)) < T1) {
                            sel = int(r15(e) % mod);
                            if (mod == 4u && sel == 3) sel = 4;
                            if (mod == 5u && (sel == 1 || sel == 2) && sel == cancel) sel = 0;
                        }
                    }
                };
                if (plan == 1) {                                   // BACK
                    a.action() = -1;
                    ai::stop(a, 0); ai::steer(a, e); ai::turn(a, a.f(0x814));
                    a.mode() = 1;
                    walk_or_block({-float(std::cos(double(a.heading))), 0, -float(std::sin(double(a.heading)))}, 0, 4u);
                } else {                                           // STRAFE
                    a.action() = -1;
                    if (++a.i(0x7a4) > 30 && r15(e) % 1000u < 30u) { a.i(0x7a4) = 0; a.i(0x7a0) = int(r15(e) % 3u); }
                    a.steer() = 0;
                    if (a.i(0x7a0) == 0) { a.action() = 2; break; }
                    const float off = a.i(0x7a0) == 1 ? -1.5707964f : 1.5707964f;
                    ai::stop(a, 0); ai::steer(a, e); ai::turn(a, a.f(0x814));
                    a.mode() = 1;
                    const double th = double(a.heading + off);
                    walk_or_block({-float(std::cos(th)), 0, -float(std::sin(th))}, a.i(0x7a0), 5u);
                }
                break;
            }
            // DECIDE (0x4b7dde)
            a.steer() = old708; a.action() = old710;
            const float ad = adiff_to_target();
            bool requested = false;
            if (gnd && a.cool() == 0 && 6.0f > a.dist()) {
                const int r = int(r15(e) % 1000u);
                if (r < 150 + (d < 0 ? 30 * d : 100 * d) && 0.7853982f > ad) {
                    out.section = 7; out.attack = true;            // gated; result ignored
                    out.set_marker_block = 1;                      // erased by the section start (literal)
                    if (tag) a.i(0x7a8) = int(r15(e) % 2u);
                    requested = true;
                }
            }
            if (!requested && gnd && a.i(0x79c) == 0 && 6.5f > a.dist()) {
                const int r = int(r15(e) % 1000u);
                if (r < 150 + (d < 0 ? -30 : 150 * d) && 0.6981317f > ad) {
                    out.section = 8; out.attack = true;            // gated; result ignored
                    a.i(0x7a8) = int(r15(e) % 2u);
                    requested = true;
                }
            }
            if (!requested) {
                const float D = a.dist();
                a.i(0x72c) = D > 4.5f ? 0 : 3.5f > D ? 1 : 2;
                a.dur() = int(r15(e) % 15u) + 5;
            }
            if (a.dur() == 0) break;                               // CHECK: after an attack request
            if (int(r15(e) % 6000u) < T1) sel = 4;                 // the forward pounce (the other picks are dead)
            break;
        }
        default: break;
    }

    // TAIL (0x4b8a0d)
    if (S == 0x13a && gnd && marker == 1 && A) {                   // the jump helper's launch (K 0.1, no push)
        const float v = a.f(0x7dc) * 0.1f;
        A->vert.t = v; A->pos.y += v; A->vert.grounded = false; out.set_own_jump = 1;
    }
    if (std::uint32_t(a.i(0x87c)) % 10u == 0 && gnd && (S == 0 || S == 1 || S == 2) &&
        ((0x100 <= PS && PS <= 0x109) || (0x10b <= PS && PS <= 0x112))) {
        const float an = float(std::atan2(double(-a.f(0x73c)), double(-a.f(0x734))));
        if (0.7853982f > float(std::fabs(ai::angle_diff(e.target_heading, an)))) {   // T faces it
            const int r = int(r15(e) % std::uint32_t(30000 - 4000 * d));
            const int Ao = d < 0 ? 150 : 130 * q + 350;
            if (r < Ao) {
                const int r2 = int(r15(e) % std::uint32_t(Ao));
                sel = r2 < (d < 0 ? 50 : 70 * q + 150) ? 3 : int(r15(e) % 2u) + 1;
            }
        }
    }
    if (sel != 0) {                                                // the hop
        static const float HOP[4] = {-1.5707964f, 1.5707964f, 3.1415927f, 0.0f};   // 0x6be220
        const double th = double(HOP[sel - 1] + h);
        float vx = float(std::cos(th)) * 0.4f, vz = float(std::sin(th)) * 0.4f;
        bool big = false;
        if (sel == 4 && (e.rng() & 0x20000u) != 0) { big = true; vx += vx; vz += vz; }
        if (gnd && A) {
            const float vy = big ? 0.3f : 0.25f;
            A->vert.t = vy; A->pos.y += vy; A->vert.grounded = false; out.set_own_jump = 1;
            A->mover.k1.x += vx; A->mover.k1.z += vz;
            if (out.section < 0) out.section = 3;                  // gated: the air section
        }
    }
    if (a.action() == -1 && a.dur() != 0) a.dur() -= 1;            // the brain-driven countdown
    return out;
}

}  // namespace cleria::brains
