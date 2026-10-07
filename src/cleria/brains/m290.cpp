// M290 Bicmobre — brain FUN_004a2120 and its handler's death helper FUN_004a2f90 (handler
// FUN_004a31c0 = M100's minus the arena clamp, plus the helper on the dead path; AI index 22).
// Spec: build/ai_spec/M290.md; the bomb itself: build/ai_spec/BOMB.md (bullet.cpp, kind 0xf).
//
// A ground bomber. Engaged it runs at the target; attack-ready (cooldown 0, grounded, the target's
// floor within 6 of its origin, not in 7 / 8) inside 8.5 and within +-36 deg it plays section 7 for
// 205 - 20d ticks (stop; Easy locks the heading): at Marker 1 a bomb 5 deg left of its facing, at
// Marker 2 one 5 deg right, each with a random extra on d >= 2. Ready inside 5.5 it retreats (steer
// 3, x3 turn, x1.3 run) for up to 150 ticks / until 10 u, throwing anyway when still inside 5.5
// after 100. It owns its section-0x13a launch (K 0.1, push +0x7e0). Dying, it throws bombs up in
// random directions on dead ticks 15, 30 (Normal), 20, 30, 40 (Hard), 16, 20 .. 44 (Nightmare).
// [not modelled] the keep-out box restore (FUN_0042dc90).
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {
namespace {

constexpr float kDeg = 0.0174533f;   // 0x6bd0dc

Vec3 normalize3(float x, float y, float z) {                   // FUN_0042cc50: zero -> 0
    const float l = std::sqrt(x * x + y * y + z * z);
    if (l == 0.0f) return {};
    const float k = 1.0f / l;
    return {x * k, y * k, z * k};
}

AiBullet bomb_at(const Vec3& self, const Vec3& v, float fwd, float yoff, float speed) {
    AiBullet b;
    b.kind = 0xf; b.effect = 300;                                // enemy\m_290_bakudann.eft
    b.pos = {self.x + fwd * v.x, (self.y + yoff) + fwd * v.y, self.z + fwd * v.z};
    b.dir = v;
    b.speed = speed;                                             // args 5.. are never read for 0xf
    b.gravity = 0.005f;
    return b;
}

}  // namespace

AiOut m290(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // 0x4a222b
    Actor* A = e.actor;
    const int d = difficulty(e);
    bool can = true;
    if (std::fabs(e.target_floor - e.self.y) >= 6.0f) can = false;   // T+0x318 vs the origin (0x6bd880)
    switch (a.phase()) {
        case 0:                                                    // INIT (0x4a2eb2)
            ai::init(a, p, e);
            for (int off = 0x834; off <= 0x850; off += 4) a.f(off) = 0;   // only 32 bytes copied
            a.f(0x7cc) = a.f(0x814);
            a.f(0x7d0) = a.f(0x820);
            return out;
        case 1:
            ai::idle(a, p, e);
            return out;
        case 2: break;
        default: return out;
    }
    if (a.dist() > 20.0f) { ai::disengage(a); a.i(0x7c0) = 0; }   // A. the tick continues
    a.f(0x814) = a.f(0x7cc);                                       // B
    a.f(0x820) = a.f(0x7d0);
    if (a.action() == 0) {                                         // C
        if (a.i(0x728) != 100) {
            if (a.i(0x7c0) == 1) { a.i(0x728) = 1; a.action() = 0x400; }
            else if (a.i(0x7bc) == 1) {}
            else { a.i(0x728) = 0; a.action() = 0x400; }
        }
        a.i(0x7bc) = 0; a.i(0x7c0) = 0;
    }
    const int S = e.section;
    auto attack = [&](int act) {
        out.section = 7; out.attack = true;                        // gated; result not checked
        a.action() = act;
        a.dur() = 205 - 20 * d;
        a.cool() = int(r15(e) % 60u) + a.dur();
    };
    switch (a.i(0x728)) {
        case 0: {                                                  // approach
            a.steer() = 0;
            const bool ready = S != 7 && S != 8 && a.cool() == 0 && e.grounded && can;
            if (a.dist() < 5.5f && ready) { a.action() = 0; a.i(0x79c) = 0; a.i(0x7c0) = 1; }
            if (a.dist() < 8.5f && ready) {
                const Vec3 to = a.v(0x734);
                const float an = float(std::atan2(double(to.z), double(to.x)));
                if (float(std::fabs(ai::angle_diff(a.heading, an))) < 0.628319f) {   // 0x6bd344
                    attack(e.diff_easy ? 1 : 2);                   // DAT_0076bb58 == 1: heading locked
                    a.i(0x7bc) = 1;
                    a.i(0x728) = 3;
                    a.i(0x7c4) = 0; a.i(0x7c8) = 0;
                }
            }
            break;
        }
        case 1: {                                                  // retreat
            a.steer() = 3;
            const std::uint32_t n = std::uint32_t(++a.i(0x79c));
            a.i(0x7c0) = 1;
            a.f(0x814) = a.f(0x7cc) * 3.0f;                        // 0x6bd680
            a.f(0x820) = a.f(0x7d0) * 1.3f;                        // 0x6bd4a4
            bool end = false;
            if (a.dist() < 5.5f && n > 100u) {                     // cornered: attack anyway
                out.section = 7; out.attack = true;
                a.action() = 2; a.i(0x728) = 3;
                a.i(0x7c4) = 0; a.i(0x7c8) = 0;
                a.dur() = 205 - 20 * d;
                a.cool() = int(r15(e) % 60u) + a.dur();
                end = true;
            } else if (a.dist() > 10.0f || n > 150u) {
                a.action() = 0; a.i(0x728) = 0;
                end = true;
            }
            if (end) {
                a.steer() = 0;
                a.f(0x814) = a.f(0x7cc); a.f(0x820) = a.f(0x7d0);
                a.i(0x79c) = 0; a.i(0x7c0) = 0;
            }
            break;
        }
        case 2:
            a.steer() = 0;
            break;
        case 3: {                                                  // the volleys (§3.5)
            a.steer() = 0;
            const float f = e.facing;                              // +0x314
            if (e.aso_marker == 1 && S == 7 && a.i(0x7c4) == 0) {   // volley A
                a.i(0x7c4) = 1;
                const float th = f + 0.0872665f;                   // 0x6bd18c
                out.bullets.push_back(bomb_at(e.self, normalize3(float(std::cos(double(th))), 0.6981317f,
                                                                 float(std::sin(double(th)))), 0.75f, -0.5f, 0.125f));
                if (d > 1) {
                    const float al = f + float(int(r15(e) % 20u) - 5) * kDeg;
                    const float be = f + float(int(r15(e) % 20u) - 5) * kDeg;
                    out.bullets.push_back(bomb_at(e.self, normalize3(float(std::cos(double(al))), 0.8203047f,
                                                                     float(std::sin(double(be)))), 0.75f, 0.0f, 0.125f));
                }
            }
            if (e.aso_marker == 2 && S == 7 && a.i(0x7c8) == 0) {   // volley B
                a.i(0x7c8) = 1;
                const float th = f - 0.0872665f;
                out.bullets.push_back(bomb_at(e.self, normalize3(float(std::cos(double(th))), 0.6981317f,
                                                                 float(std::sin(double(th)))), 0.75f, -0.5f, 0.125f));
                if (d > 1) {
                    const float al = f - float(int(r15(e) % 30u) - 10) * kDeg;
                    const float be = f - float(int(r15(e) % 30u) - 10) * kDeg;
                    out.bullets.push_back(bomb_at(e.self, normalize3(float(std::cos(double(al))), 0.9075712f,
                                                                     float(std::sin(double(be)))), 0.75f, -0.1f, 0.125f));
                }
            }
            break;
        }
        case 100:                                                  // the obstacle jump (form b)
            if (S == 0x13a) break;
            if (e.grounded) { a.action() = 0; a.i(0x728) = 0; }
            break;
        default: break;
    }
    // TAIL: the obstacle-jump launch, K 0.1, the +0x7e0 push, no surface factor.
    if (S == 0x13a && e.grounded && e.aso_marker == 1 && A) {
        launch(*A, a.f(0x7dc) * 0.1f, out);
        push1(*A, a.heading, a.f(0x7e0));
    }
    return out;
}

std::vector<AiBullet> m290_dead(Actor& A, int bb5c, const std::function<std::uint32_t()>& rng) {
    // FUN_004a2f90 (M290.md §2.1), once per dead tick after FUN_004780e0 and the VM step.
    std::vector<AiBullet> out;
    const Vec3 pos{A.pos.x, A.pos.y + A.height, A.pos.z};
    const std::uint32_t n = std::uint32_t(++A.ai.i(0x72c));
    if (n < 15u) return out;
    const int k = bb5c < 0 ? 0 : bb5c >= 2 ? 2 : bb5c;
    static constexpr std::uint32_t kP[3] = {15, 10, 4};
    if (n % kP[k] != 0 || n > 65u) return out;
    const float an = float((rng() >> 17) % 360u) * kDeg;
    const float c = float(std::cos(double(an)));
    const float el = float((rng() >> 17) % 20u + 60u) * kDeg;      // used raw as y
    const float s = float(std::sin(double(an)));
    out.push_back(bomb_at({pos.x, pos.y - 0.1f, pos.z}, normalize3(c, el, s), 0.5f, 0.0f, 0.1f));
    return out;
}

}  // namespace cleria::brains
