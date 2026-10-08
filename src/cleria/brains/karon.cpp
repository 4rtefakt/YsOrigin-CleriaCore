// The sand skeletons: M560 Karon (brain FUN_004a0260, AI index 49) and M780 Zelkaron (brain
// FUN_004aa220, AI index 71). Specs: build/ai_spec/M560.md, M780.md (§7 = every instruction-level
// difference); their revive handlers FUN_004a12a0 / FUN_004ab280 are src/cleria/brains/revive.cpp
// (build/ai_spec/REVIVE.md).
//
// Karon sleeps buried 1.5 under the sand top (unhittable, +0x7a0 = 0) and surfaces after a delay of
// 10 x the distance once the target comes within 5 u (11 if it stands on sand); Zelkaron starts
// surfaced and is acquired by sight. Engaged they run in, hold, flee within 2 (1.5) and walk random
// legs, at x2 run speed while fleeing / walking legs and x2 again on sand. Off cooldown, 5% (10%) a
// tick inside 4.5 u and +-36 deg: section 7 (the leaping spin; the ASO spawns effect 259 twice);
// at its Marker 1 the brain leaps (y +0.3, t 0.15, a 0.3 / 0.5 push). Zelkaron chains max(d + 2, 2)
// spins, the follow-ups at Marker 3 or when the section ends, snapping the heading to the target.
// Every third tick (per actor slot) in phase 2 they look 1.5 / 2.25 (/ 3.0) ahead for a higher
// non-sand floor and climb out with section 0x13a and their own launch.
// [not modelled] the keep-out box restore (FUN_0042dc90); the init's own integrator + probe calls
// (FUN_0045a510 / FUN_0045c490, byte +0x4c3); +0x420 (written with +0x330); the +0x38c re-bury
// trigger (its probe quantity +0x390 is untraced, so it reads 0); the jump search's per-actor-slot
// stagger keys on the instance id instead of the actor's address [H].
#include <algorithm>
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {
namespace {

struct Skel {
    bool m780;
    float flee;          // sub 1 flee distance: 2.0 (0x6bd580) / 1.5 (D0)
    std::uint32_t leg;   // leg duration R % leg + leg: 15 / 30
    std::uint32_t roll;  // attack roll % 1000 < roll: 50 / 100
    int dur_hi, dur_lo;  // attack duration d > 1 / else: 45, 65 / 50, 80
    std::uint32_t xm; int xk, xb;   // the (dead) cooldown extra: R % xm - xk*max(d,0) + xb
    float push;          // the leap's push: 0.3 / 0.5 (0x6bd304)
};
constexpr Skel kM560{false, 2.0f, 15, 50, 45, 65, 60, 25, 90, 0.3f};
constexpr Skel kM780{true, 1.5f, 30, 100, 50, 80, 120, 40, 180, 0.5f};

float fcos(float x) { return float(std::cos(double(x))); }
float fsin(float x) { return float(std::sin(double(x))); }

// The leap at section 7 Marker 1 / the 0x13a launch: +0x330 = t, y += dy, airborne, own-jump,
// word +0x4b9 = 0x0101 (both surface bytes 1), the push along the heading on knock channel 1.
void leap(EnemyAi& a, Actor& A, float t, float dy, float sp, AiOut& out) {
    A.vert.t = t;
    A.pos.y += dy;
    A.vert.grounded = false;
    out.set_own_jump = 1;
    a.surf = 1; a.surf_ray = 1;
    push1(A, a.heading, sp);
}

AiOut skeleton(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e, const Skel& K) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // +0x4d0 (0x4a036d)
    Actor* A = e.actor;
    const int d = difficulty(e);
    constexpr float D0 = 1.5f;                                     // 0x6bd4d4
    a.hittable = a.i(0x7a0) != 0;                                  // 0x4a03cd..0x4a03e7
    const Vec3 to = a.v(0x734);
    const float hd = float(std::sqrt(double(to.x) * to.x + double(to.z) * to.z));   // M560 only
    a.f(0x820) = a.f(0x7ac);                                       // 0x4a0418
    const bool inmap = !K.m780 && e.world && e.world->in_room("s_40\\s_4005\\");
    int marker = e.aso_marker;
    const int S = e.section;

    auto consume_marker = [&] { marker = 2; out.set_marker = 2; };   // +0x9bc = 2

    switch (a.phase()) {
        case 0: {                                                  // INIT (0x4a04e6)
            ai::init(a, p, e);
            a.i(0x70c) = 2;
            a.i(0x72c) = 0;
            if (!K.m780 && a.surf_ray == 0x12 && !inmap) {        // BURIED, 1.5 under the sand top
                a.i(0x7a0) = 0;
                if (A) A->pos.y = (a.water_y - D0) - A->height;
            } else {
                a.i(0x7a0) = 1;
            }
            a.i(0x7bc) = 0;
            a.f(0x7ac) = a.f(0x820);
            if (A) A->flags12c |= 0x400000u;
            break;
        }
        case 4: {                                                  // reassembled, waiting (0x4a0448)
            a.hittable = false;
            if (S == 10) break;
            a.hittable = true;
            const int n = (a.i(0x79c) != 0 ? a.i(0x79c) : 60) - 1;
            a.i(0x79c) = n;
            if (n == 0) {
                for (int k = 0; k < 16; ++k) a.f(0x814 + 4 * k) = p.raw[k];
                a.f(0x7ac) = a.f(0x820);
                if (A) A->flags12c |= 0x400000u;
                ai::disengage(a);
                a.i(0x72c) = 0;
            }
            if (K.m780) a.i(0x7c8) = 0;
            break;
        }
        case 1: {
            a.f(0x820) = a.f(0x7ac);
            ai::idle(a, p, e);
            if (K.m780) { a.i(0x70c) = 0; break; }
            if (a.i(0x7a0) == 1) {                                 // surfaced
                a.i(0x70c) = 0;
                if (a.surf_ray == 0x12) { a.i(0x7a0) = 2; a.i(0x70c) = 2; }
            } else {
                a.i(0x70c) = 2;                                    // [not modelled] +0x38c re-bury
            }
            if (a.i(0x7a0) != 0 && a.i(0x7a0) != 2) break;
            const bool near_ = hd < 5.0f || inmap ||
                               ((e.target_surface == 0x12 || a.i(0x7a0) == 2) && hd < 11.0f);
            if (near_ && a.i(0x7bc) == 0) {                        // ARM (0x4a06ae)
                a.i(0x7bc) = std::max(1, int(std::floor(a.dist() * 10.0f)));
                if (inmap) a.i(0x7bc) = 1;
            }
            if (a.i(0x7bc) != 0 && --a.i(0x7bc) == 0) {            // COUNT (0x4a06f5)
                ai::acquire(a);
                a.i(0x7a0) = 1;
                a.i(0x7a8) = 0;
            }
            break;
        }
        case 2: {
            if (a.i(0x72c) != 0) {                                 // ATTACK (0x4a0d69 / 0x4aab7b)
                const int base = d > 1 ? K.dur_hi : K.dur_lo;
                a.steer() = 0;
                a.action() = std::uint32_t(a.dur()) > std::uint32_t(base - 10) ? 2 : 1;
                int dur = a.dur();
                if (marker == 1) {                                 // the LEAP at section 7 Marker 1
                    consume_marker();
                    if (A) leap(a, *A, 0.15f, 0.3f, K.push, out);
                    dur = a.dur();
                }
                bool more = false;
                const int N = std::max(d + 2, 2);
                if (S != 7) {
                    if (std::uint32_t(dur) > 1u) { dur -= 2; a.dur() = dur; }
                    if (std::uint32_t(a.cool()) > 1u) a.cool() -= 2;
                    if (K.m780 && std::uint32_t(a.i(0x7c8)) < std::uint32_t(N)) more = true;
                }
                bool re = false;
                if (dur == 0) {
                    a.i(0x72c) = 0;
                    if (K.m780 && std::uint32_t(a.i(0x7c8)) < std::uint32_t(N)) re = true;
                } else if (K.m780 && marker == 3 && std::uint32_t(a.i(0x7c8)) < std::uint32_t(N)) {
                    re = true;                                     // section 7 Marker 3 (M_780 only)
                }
                if (re || more) {                                  // REATTACK (0x4aad22)
                    a.dur() = base;
                    const int x1 = int(r15(e) % K.xm) - K.xk * std::max(d, 0) + K.xb;
                    const int extra = x1 < 0 ? int(r15(e) % K.xm) - K.xk * std::max(d, 0) + K.xb : 0;
                    a.i(0x72c) = 1;
                    a.cool() = a.dur() + extra;
                    out.section = 7; out.attack = true;            // gated; result ignored
                    a.i(0x7c8) += 1;
                    a.set_v(0x748, a.ncrumb() != 0 ? a.v(0x758) - e.self : a.v(0x734));
                    ai::turn(a, a.f(0x814) * 100.0f);              // 0x6bdafc: the snap
                }
                break;
            }
            if (K.m780) a.i(0x7c8) = 0;
            a.f(0x820) = (a.steer() == 3 || a.steer() == 5) ? a.f(0x7ac) + a.f(0x7ac) : a.f(0x7ac);
            if (a.dist() > 20.0f && !inmap) {
                a.action() = 0x10; a.i(0x728) = 1;
                ai::disengage(a);                                  // the tick continues
            }
            auto angle = [&] {
                const float an = float(r15(e) % 360u) * 0.0174533f;
                a.f(0x748) = fcos(an); a.f(0x750) = fsin(an);
            };
            auto leg = [&] { return int(r15(e) % K.leg) + int(K.leg); };
            auto flee_start = [&] { a.action() = 0x800; a.i(0x7a8) = 2; a.dur() = leg(); };
            switch (a.i(0x7a8)) {
                case 0:
                    if (a.dist() > 3.5f) a.action() = 0x800;
                    else { a.i(0x7a8) = 1; a.action() = 2; }
                    break;
                case 1:
                    if (a.dist() > 4.5f) { a.action() = 0x800; a.i(0x7a8) = 0; }
                    else if (a.dist() > K.flee) a.action() = 2;
                    else flee_start();
                    break;
                case 2:
                    a.action() = 0x800;
                    if (a.dist() > 4.5f) { a.i(0x7a8) = 0; break; }
                    if (a.dur() != 0) break;
                    angle();
                    a.dur() = leg();
                    a.i(0x7a8) = 3; a.action() = 0x800;
                    break;
                case 3:
                    a.action() = 0x800;
                    if (a.dist() > 4.5f) { a.i(0x7a8) = 0; break; }
                    if (a.dur() != 0) break;
                    if (r15(e) % 3u == 0) {
                        flee_start();
                    } else {
                        angle();
                        a.action() = 0x800;
                        a.dur() = leg();
                    }
                    break;
                default: break;
            }
            a.steer() = a.i(0x7a8) == 2 ? 3 : a.i(0x7a8) == 3 ? 5 : 0;   // STEER (0x4a0b64)
            if (a.cool() != 0) break;
            if (r15(e) % 1000u >= K.roll) break;                   // drawn before the range test
            if (!(4.5f > a.dist())) break;
            const float an = float(std::atan2(double(to.z), double(to.x)));
            if (!(float(std::fabs(ai::angle_diff(a.heading, an))) < 0.628319f)) break;   // 0x6bd344
            a.dur() = d > 1 ? K.dur_hi : K.dur_lo;
            const int x1 = int(r15(e) % K.xm) - K.xk * std::max(d, 0) + K.xb;
            const int extra = x1 < 0 ? int(r15(e) % K.xm) - K.xk * std::max(d, 0) + K.xb : 0;
            a.i(0x72c) = 1;
            a.cool() = a.dur() + extra;
            out.section = 7; out.attack = true;                    // gated; result ignored
            if (K.m780) a.i(0x7c8) += 1;                           // even if refused
            break;
        }
        default: break;                                            // 3, > 4: COMMON
    }

    // COMMON (0x4a0ea9)
    if (a.surf == 0x12) a.f(0x820) += a.f(0x820);                  // x2 run on sand
    if (a.i(0x728) == 100 && S != 0x13a && e.grounded) { a.action() = 0; a.i(0x728) = 0; }
    if (S == 0x13a) {
        a.f(0x820) = a.f(0x7ac);
        if (e.grounded && marker == 1 && A) {                      // LAUNCH (K 0.1, y += 2v)
            consume_marker();
            const float v = a.f(0x7dc) * 0.1f;
            leap(a, *A, v, v + v, a.f(0x7e0), out);
        }
    }
    if (a.phase() != 2 || !A) return out;
    // The own jump search (0x4a1000): 1 tick in 3, staggered per actor slot [H the instance id].
    if (std::uint32_t(A->instance_id) % 3u != std::uint32_t(a.i(0x87c)) % 3u) return out;
    if (S == 0x13a || !e.grounded || !e.world) return out;
    const float H = a.f(0x828);
    float dd = D0;
    if (!(H > dd)) return out;
    do {
        const float c = fcos(a.heading) * dd, s = fsin(a.heading) * dd;
        float y = H;
        if (dd > H) y = H - (dd - H);
        if (!e.world->ray(e.self, {c, y, s}).hit) {
            const Vec3 P{e.self.x + c, e.self.y + y, e.self.z + s};
            const AiWorld::Ray r = e.world->ray(P, {0.0f, -(y + H), 0.0f});
            if (r.hit && r.surface != 0x12 && r.point.y > e.self.y + 0.1f) {   // FOUND (0x4a11e9)
                a.f(0x7e0) = dd * 0.1f + 0.15f;
                a.f(0x7dc) = std::min(H, dd) * 0.5f + (std::max(0.0f, r.point.y - e.self.y) + 0.6f);
                out.section = 0x13a; out.attack = false;           // gated (type 4 beats 7's 3)
                a.action() = 1; a.i(0x728) = 100;
                a.surf = 1;
                return out;
            }
        }
        dd += 0.75f;
    } while (H > dd);
    return out;
}

}  // namespace

AiOut m560(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) { return skeleton(a, p, e, kM560); }
AiOut m780(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) { return skeleton(a, p, e, kM780); }

}  // namespace cleria::brains
