// M770 Hubamobrun — brain FUN_0049b060 (handler FUN_0049be40 = M100's minus the arena clamp,
// AI index 70). Spec: build/ai_spec/M770.md. Hubamobre's (M300) bigger cousin.
//
// It approaches in 10-tick runs until within 7.5 u (horizontal) and facing within 45 deg;
// then (unless a grounded target is 2+ off) it picks: within 3 u the slam (section 8, stand
// 50 / 65 on Easy); beyond, the fire fan (section 7, 110 / 125) or, at or below half HP, the
// double fan (section 301, 155 / 170; on Hard+ it keeps turning). Cooldown = hold + B + R%30,
// B = 30 / 60 / 120 for d > 0 / 0 / < 0. Fans fire every 4th marker tick at 3 deg a tick:
// section 7 from heading - 50 deg up (0.75 below the origin); section 301 marker 1 the same at
// origin height, marker 2 back down from + 40 deg. Bullets: effect 302 at 0.15, accel
// -0.0015, the EF2 instance scaled x2. An exit in the hit reaction (section 4) cancels.
// Durable on in sections 7/8 and OFF otherwise.
// Literal source bug kept: section 301's stand doubles +0x814 from +0x7ac, and the shared exit
// copies +0x814 BACK into +0x7ac, so every completed 301 doubles the turn rate for good.
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m770(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;
    const Vec3 to = a.v(0x734);
    const float hd = std::sqrt(to.x * to.x + 0.0f + to.z * to.z);
    const bool can_atk = !(std::fabs(e.target.y - e.self.y) >= 2.0f && e.target_grounded);
    const float ux = hd == 0 ? 0.0f : to.x / hd, uz = hd == 0 ? 0.0f : to.z / hd;
    const bool facing = !(0.7072136f > std::cos(a.heading) * ux + std::sin(a.heading) * uz);
    int& sub = a.i(0x72c);

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            a.f(0x7ac) = a.f(0x814);
            a.i(0x728) = 0; sub = 0; a.i(0x730) = 0;
            return out;
        case 1:
            a.i(0x728) = 0; sub = 0; a.i(0x730) = 0;
            ai::idle(a, p, e);
            return out;
        case 2: break;
        default: return out;
    }
    const int S = e.section;
    if (a.dist() > 20.0f) ai::disengage(a);                       // the tick continues
    if (e.diff_index >= 0) out.set_durable = (S == 7 || S == 8) ? 1 : 0;
    const int d = difficulty(e);
    const int B = d > 0 ? 30 : d == 0 ? 60 : 120;
    auto fire = [&](float th, float yoff) {
        const float c = float(std::cos(double(th))), s = float(std::sin(double(th)));
        AiBullet b;
        b.effect = 302;                                            // enemy\m_300_fire01.eft
        b.pos = {e.self.x + c * 0.75f, e.self.y + yoff + 0.0f, e.self.z + s * 0.75f};
        b.dir = {c, 0, s};
        b.speed = 0.15f; b.accel = -0.0015f; b.gravity = 0;
        b.power = -2.0f; b.knock = 1.0f; b.f168 = 1.0f;
        b.fx_scale = 2.0f;                                         // the brain's x2 override of the instance
        out.bullets.push_back(b);
    };
    auto exit_a = [&] { a.f(0x7ac) = a.f(0x814); sub = a.i(0x79c); };
    auto start = [&](int sec, int dur) {
        out.section = sec; out.attack = true;                      // gated; result ignored
        a.dur() = dur;
        a.cool() = int(r15(e) % 30u) + B + a.dur();
    };
    switch (sub) {
        case 0: sub = 20; a.i(0x730) = 0; break;
        case 20:
            if (a.dur() != 0) break;
            if (a.i(0x730) == 0) {
                if (hd > 7.5f || !facing) { a.action() = 0x400; a.steer() = 0; a.dur() = 10; break; }
                if (can_atk) a.i(0x730) = 1;
            } else if (a.i(0x730) == 1) {
                sub = hd > 3.0f ? (e.hp / e.max_hp <= 0.5f ? 120 : 100) : 110;
                a.i(0x730) = 0; a.i(0x79c) = 0;
            }
            break;
        case 100: sub = 101; [[fallthrough]];
        case 101:
            if (a.dur() != 0) break;
            a.action() = 1;
            start(7, e.diff_easy ? 125 : 110);
            a.i(0x7c0) = 0; sub = 102;
            break;
        case 102: {
            if (a.cool() == 0 || S == 4) { exit_a(); break; }
            if (e.aso_marker != 1) break;
            const std::uint32_t n = std::uint32_t(a.i(0x7c0));
            if ((n & 3u) == 0) fire(float(n) * 3.0f * 0.0174533f + (a.heading - 0.8726646f), -0.75f);
            a.i(0x7c0) = int(n + 1);
            break;
        }
        case 110: sub = 111; [[fallthrough]];
        case 111:
            if (a.dur() != 0) break;
            a.action() = 1;
            start(8, e.diff_easy ? 65 : 50);
            sub = 112;
            break;
        case 112:
            if (a.cool() == 0) { sub = a.i(0x79c); break; }
            if (S == 4) exit_a();
            break;
        case 120: sub = 121; [[fallthrough]];
        case 121:
            if (a.dur() != 0) break;
            a.action() = e.diff_index >= 1 ? 2 : 1;
            start(0x12d, e.diff_easy ? 170 : 155);
            a.i(0x7c0) = 0; a.i(0x7c4) = 1; sub = 122;
            break;
        case 122: {
            const int c = a.cool();
            if (a.dur() == 0) { a.action() = 2; a.dur() = c; a.f(0x814) = a.f(0x7ac) + a.f(0x7ac); }
            if (c == 0 || S == 4) { exit_a(); break; }
            const int m = e.aso_marker;
            if (m != 1 && m != 2) break;
            std::uint32_t n = std::uint32_t(a.i(0x7c0));
            if ((n & 3u) == 0) {
                if (m != a.i(0x7c4)) n = 0;                        // a new marker restarts the count
                const float k = float(n) * 3.0f * 0.0174533f;
                fire(m == 1 ? (a.heading - 0.8726646f) + k : (a.heading + 0.6981317f) - k, 0.0f);
                a.i(0x7c4) = m;
            }
            a.i(0x7c0) = int(n + 1);
            break;
        }
        default: break;
    }
    return out;
}

}  // namespace cleria::brains
