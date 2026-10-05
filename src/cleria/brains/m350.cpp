// M350 Diphtheloyd — brain FUN_004a56c0 (handler FUN_004a6d40 = M100's minus the arena
// clamp, AI index 28). Spec: build/ai_spec/M350.md.
//
// A flying fire-breather (hover 5.2). Close (< 8 u, the target within 90 deg) it stands,
// faces it and strafes round a 6-unit ring through knock channel 2 (a random side, flipping
// 1.5% a tick after 200 ticks); else it runs at the target. Between 4.5 and 6.5 u, off
// cooldown, facing within 36 deg: 7% a tick the breath (section 7, 142 ticks: between its
// Marker(302) and Marker(2) a fire bullet every third tick sweeping from heading + 45 deg,
// pitched at the target's depth), or 2.5% the swoop (section 8, 100 ticks: the hover drops
// 0.2 a tick to 2.2 during marker 3 and climbs back from marker 5, fire puffs at
// marker-dependent rates). Cooldown hold + (6 - d) x 15 + R%45. Durable in 8 on Hard+.
// [not modelled] the puffs' and bullets' damage (EF2 particle hits; the kind-2 update); the
// attached second puff while marker 1; the s_4013 particle flag; the keep-out box restore.
#include <algorithm>
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m350(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // nothing, not even the tail
    const int d = difficulty(e);
    const int S = e.section;
    const Vec3 to = a.v(0x734);
    const float dist = a.dist();
    auto adiff = [&] {
        const float an = float(std::atan2(double(to.z), double(to.x)));
        return float(std::fabs(ai::angle_diff(a.heading, an)));
    };

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            a.i(0x7a0) = int(r15(e) & 1u);                         // the strafe side
            a.f(0x7ac) = a.f(0x834); a.f(0x7b0) = a.f(0x814);
            a.f(0x7b4) = a.f(0x838); a.f(0x7b8) = a.f(0x83c);
            a.i(0x7c0) = 0; a.i(0x7c4) = 0;
            break;
        case 1:
            ai::idle(a, p, e);
            break;
        case 2: {
            if (dist > 20.0f) ai::disengage(a);                   // the tick continues
            if (a.action() == 0) {
                if (a.i(0x728) != 100 && a.i(0x7bc) != 1) {
                    if (8.0f > dist && 1.5707964f > adiff()) { a.action() = 2; a.i(0x728) = 0; }
                    else { a.action() = 0x400; a.i(0x728) = 1; }
                }
                a.i(0x7bc) = 0;
            }
            if (e.diff_index >= 1 && S == 8) out.set_durable = 1;
            auto restore = [&] { a.f(0x834) = a.f(0x7ac); a.f(0x838) = a.f(0x7b4); a.f(0x83c) = a.f(0x7b8); };
            switch (a.i(0x728)) {
                case 0: {                                          // circle at ~6 u, attack rolls
                    restore();
                    a.steer() = 0;
                    ai::stop(a, 0);
                    ai::steer(a, e);
                    ai::turn(a, a.f(0x814));
                    a.mode() = 0;
                    const float sp = a.speed310 * a.f(0x818);
                    const float side = a.i(0x7a0) != 0 ? a.heading + 1.5f : a.heading - 1.5f;
                    const Vec3 s{std::cos(side) * sp, 0, std::sin(side) * sp};
                    Vec3 v{}; float k = 0;
                    auto nrm = [](float x, float z) {
                        const float l = std::sqrt(x * x + z * z);
                        return l == 0 ? Vec3{} : Vec3{x / l, 0, z / l};
                    };
                    if (6.0f > dist) { v = nrm(-to.x, -to.z); k = 6.0f - dist; }
                    else if (dist > 6.0f) { v = nrm(to.x, to.z); k = dist - 6.0f; }
                    const Vec3 r{v.x * k * 0.01f, 0, v.z * k * 0.01f};
                    const Vec3 m{r.x + s.x, r.y + 0.0f, r.z + s.z};
                    const float L = std::sqrt(m.x * m.x + m.y * m.y + m.z * m.z);
                    const float q = 1.0f / (L * 60.0f);
                    if (e.actor) {
                        e.actor->mover.k2.x += q * m.x * 2.5f;
                        e.actor->mover.k2.y += q * m.y * 2.5f;
                        e.actor->mover.k2.z += q * m.z * 2.5f;
                    }
                    a.speed310 = 0;                                // +0x30c = 0
                    if (r15(e) % 1000u < 15u && std::uint32_t(a.i(0x7a4)) > 200u) { a.i(0x7a4) = 0; a.i(0x7a0) = a.i(0x7a0) == 0; }
                    else a.i(0x7a4) += 1;
                    if (dist > 8.0f || !(1.5707964f > adiff())) { a.action() = 0x400; a.i(0x728) = 1; }
                    int s_live = S;
                    if (dist > 4.5f && 6.5f > dist && s_live != 7 && s_live != 8 && a.cool() == 0) {
                        if (r15(e) % 1000u < 70u && 0.6283185f > adiff()) {
                            out.section = 7; out.attack = true;    // gated; unchecked
                            if (ai::request(s_live, 7, false, e)) s_live = 7;
                            a.dur() = 142; a.action() = 1;
                            const int r3 = int(r15(e) % 45u);
                            a.i(0x728) = 2;
                            a.cool() = r3 + (6 - d) * 15 + a.dur();
                            a.i(0x7bc) = 1; a.i(0x7a8) = 0; a.i(0x7c4) = 0;
                        }
                    }
                    if (dist > 4.5f && 6.5f > dist && s_live != 7 && s_live != 8 && a.cool() == 0) {
                        if (r15(e) % 1000u < 25u && 0.6283185f > adiff()) {
                            out.section = 8; out.attack = true;
                            a.dur() = 100; a.action() = 1;
                            const int r5 = int(r15(e) % 45u);
                            a.i(0x728) = 3;
                            a.cool() = r5 + (6 - d) * 15 + a.dur();
                            a.i(0x7bc) = 1; a.i(0x7c4) = 0;
                        }
                    }
                    break;
                }
                case 1:
                    restore();
                    if (7.0f > dist && 1.5707964f > adiff()) { a.action() = 2; a.i(0x728) = 0; }
                    break;
                case 2: {                                          // the breath: the bullet window
                    const int m = e.aso_marker;
                    if (std::uint32_t(m) > 2u) { a.i(0x7c0) = m; out.set_marker = 0; a.i(0x7c4) = 0; }
                    else if (m == 2) a.i(0x7c0) = 0;
                    break;
                }
                case 3: {                                          // the swoop
                    a.f(0x838) = 1.0f; a.f(0x83c) = 1.0f;
                    const std::uint32_t n = std::uint32_t(++a.i(0x7c4));
                    const int m = e.aso_marker;
                    const bool fire = (m == 1 || m == 2) ? (n & 3u) == 0 : m == 3 ? (n & 1u) == 0
                                    : m == 4 ? n % 3u == 0 : m == 5 ? n % 5u == 0
                                    : m == 6 ? (n & 7u) == 0 : m == 7 ? n % 12u == 0 : false;
                    if (fire) {                                    // PUFF: four draws, then effect 302
                        const std::uint32_t w1 = e.rng();
                        const std::uint32_t w2 = e.rng(), w3 = e.rng();
                        e.rng();                                   // w4: stored, unused
                        const float rr = unit(w1) * 1.5f + 0.0f;
                        const float pitch = float((w2 >> 17) % 90u) * 0.0174533f;
                        const float yaw = float((w3 >> 17) % 360u) * 0.0174533f;
                        AiEffect fx;
                        fx.id = 302;
                        fx.pos = {e.self.x + std::cos(yaw) * std::cos(pitch) * rr,
                                  e.self.y + std::sin(pitch) * rr - 2.0f,
                                  e.self.z + std::sin(yaw) * std::cos(pitch) * rr};
                        fx.power = -5.0f; fx.rtype = 1;
                        out.effects.push_back(fx);
                    }
                    a.action() = m == 1 ? 2 : 1;
                    if (m == 3) {
                        const float lo = a.f(0x7ac) - 3.0f;
                        if (a.f(0x834) > lo) a.f(0x834) -= 0.2f; else a.f(0x834) = lo;
                    }
                    if (std::uint32_t(m) > 4u) {
                        if (a.f(0x7ac) > a.f(0x834)) a.f(0x834) += 0.1f; else a.f(0x834) = a.f(0x7ac);
                    }
                    break;
                }
                default: break;
            }
            break;
        }
        default: break;
    }

    // TAIL (every phase): the breath's bullets, every third tick of the window.
    if (a.i(0x7c0) != 0) {
        if (S == 7) {
            const std::uint32_t n = std::uint32_t(a.i(0x7c4));
            if (n % 3u == 0) {
                const float nf = float(n);
                const float t = (nf + nf) * 0.0174533f;
                const float th = (a.heading + 0.7853982f) - t;
                const float hd = float(std::sqrt(double(to.x * to.x + to.z * to.z)));
                const float dd = std::fabs(std::min(0.0f, to.y));
                const float pp = float(std::atan2(double(hd), double(dd)));
                const float sp = float(std::sin(double(pp)));
                const float ya = (1.1780972f - t) + a.heading;
                AiBullet b;
                b.kind = 2; b.effect = 302;                        // enemy\m_300_fire01.eft
                b.dir = {std::cos(ya) * sp, -float(std::cos(double(pp))), std::sin(ya) * sp};
                b.pos = {e.self.x + std::cos(th) * 0.55f,
                         e.self.y - (std::fabs(nf - 22.5f) * 0.005f + 1.635f),
                         e.self.z + std::sin(th) * 0.55f};
                b.speed = 0.16f; b.accel = -0.0012f; b.scale = 1.0f;
                b.home_period = 1; b.home_rate = 0.0174533f; b.probe = true; b.f16c = 30.0f; b.f170 = 120.0f;
                out.bullets.push_back(b);
            }
            a.i(0x7c4) = int(n + 1);
            return out;
        }
        a.i(0x7c0) = 0;
    }
    return out;
}

}  // namespace cleria::brains
