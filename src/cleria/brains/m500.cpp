// M500 Oggnome — brain FUN_00485050 (handler FUN_004862c0 = M100's minus the arena clamp,
// AI index 43). Spec: build/ai_spec/M500.md.
//
// It never walks (base speed and run multiplier zeroed every tick, no action issued); the
// action player only turns it toward the target at 1 rad/tick. It moves by teleporting:
// vanish (section 302, fading frames 4..7) -> hidden (0..119 ticks; while engaged, 20% a tick
// to relocate 1.5..8 u from the target, within 16 of home, onto floor with a clear line to
// the target) -> appear (section 301) -> ready: within 8 u, off cooldown, 5% a tick facing
// within 60 deg -> attack (section 7): at its Marker(1), 1/2/3 poison balls (d < 0 / 0 / >= 1,
// effect 448 m_dokutama, speed 0.1, gravity 0.004, lobbed by distance), 1 or 2 volleys
// (+1 when group-tagged), then a cooldown of (R%30 + 60) x 3 (x 2 above Normal).
// [not modelled] the keep-out box restore (FUN_0042dc90); the water-set up-ray of the
// relocation (the room collision has no water set, so it never blocks); +0x144 (CASTINFO
// +0x3c) scaled by the fade; the bullet's damage (the GoXx path is untraced).
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m500(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    Actor* A = e.actor;
    a.hittable = true;                                             // +0x60 |= 0x8c
    a.alpha = 1.0f;
    if (e.launched) return out;
    const Vec3 T = e.target;
    const int d = difficulty(e);
    a.f(0x820) = 0; a.f(0x818) = 0;                                // NO walking
    a.mode() = 0;                                                  // +0x6f8
    int& sub = a.i(0x728);
    int S = e.section;
    const bool tagged = e.group_tag;
    auto U = [&] { return unit(e.rng()); };

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            for (int off = 0x834; off <= 0x850; off += 4) a.f(off) = 0;   // only 32 bytes copied
            sub = 2; a.i(0x72c) = 0; a.i(0x70c) = 2;
            a.action() = 0; a.steer() = 0;
            a.i(0x7a4) = 0;
            a.set_v(0x7cc, e.self);                                // home
            out.section = 0x12e; out.section_force = true;         // ungated fade-out
            S = 0x12e;
            break;
        case 1:
            ai::idle(a, p, e);
            if (8.0f > a.dist()) {
                if (sub == 3 && a.cool() == 0) { sub = 0; a.i(0x72c) = 0; a.i(0x7a4) = 0; }
            } else if (sub == 1) {
                sub = 2; a.i(0x72c) = 0; a.i(0x7a4) = 0; a.cool() = 0;
            }
            break;
        case 2:
            if (a.dist() > 20.0f) ai::disengage(a);
            if (8.0f > a.dist()) {
                bool to_switch = false;
                if (sub == 3) {
                    if (a.cool() != 0) to_switch = true;
                    else { sub = 0; a.i(0x72c) = 0; a.i(0x7a4) = 0; }
                }
                if (!to_switch && sub == 1 && a.cool() == 0 && std::uint32_t(a.i(0x87c)) > 30u) {
                    if (r15(e) % 1000u < 50u) {
                        const Vec3 to = a.v(0x734);
                        const float ang = float(std::atan2(double(to.z), double(to.x)));
                        if (1.0471976f > float(std::fabs(ai::angle_diff(a.heading, ang)))) {
                            sub = 4; a.i(0x87c) = 0; a.i(0x72c) = 0; a.i(0x7a4) = 0;
                            to_switch = true;
                        }
                    }
                }
                if (!to_switch && sub == 1 && a.i(0x730) != 0) {
                    const std::uint32_t r = r15(e) % 10000u;
                    const std::uint32_t n = std::uint32_t(a.i(0x730));
                    const bool hit = d >= 2 ? r < n * n * 200u : d >= 0 ? r < n * 10u : r < 5u;
                    if (hit) { sub = 2; a.i(0x72c) = 0; a.i(0x7a4) = 0; }
                }
            } else if (sub == 1) {
                a.cool() = 0; sub = 2; a.i(0x72c) = 0; a.i(0x7a4) = 0;
            }
            break;
        default:
            break;
    }

    // The sub-state switch. v = the anime_Pattern frame, latched before any request.
    int v = e.aso_frame;
    switch (sub) {
        case 0:                                                    // appear
            a.i(0x730) = 0;
            if (S == 0x12d) { if (v != 0) break; }
            else if (S == 0x12e) { if (ai::request(S, 0x12d, false, e)) out.section = 0x12d; break; }
            sub = 1; a.i(0x72c) = 0; a.i(0x87c) = 0; a.cool() = 0; a.i(0x7a4) = 0;
            break;
        case 2:                                                    // vanish
            a.i(0x730) = 0;
            if (S == 0x12e) {
                if (v == 7) { sub = 3; a.cool() = int(r15(e) % 120u); a.i(0x72c) = 0; a.i(0x7a4) = 0; }
            } else {
                if (ai::request(S, 0x12e, false, e)) out.section = 0x12e;
                v = 0;                                             // no fade this tick
            }
            break;
        case 3:                                                    // hidden: relocate
            if (d >= 0 && a.phase() == 2 && A && e.world) {
                if (r15(e) % 1000u < 200u && !tagged) {
                    const float ua = U(), ub = U();
                    const float rad = ub * 6.5f + 1.5f;
                    const double th = double(ua * 360.0f + 0.0f);  // 360 radians, as written
                    const float cx = float(std::cos(th)) * rad + T.x;
                    const float cy = T.y + 2.0f;
                    const float cz = float(std::sin(th)) * rad + T.z;
                    const float hx = a.f(0x7cc) - cx, hz = a.f(0x7d4) - cz;
                    if (hx * hx + 0.0f + hz * hz < 256.0f) {
                        AiWorld& w = *e.world;
                        const Vec3 D{0, -7.0f, 0};
                        const float x2 = ((cx + 0.5f) - 1.0f) + 0.5f;
                        const AiWorld::Ray R1 = w.ray({cx, cy, cz}, D);
                        const bool all = R1.hit && w.ray({cx + 0.5f, cy, cz}, D).hit &&
                                         w.ray({(cx + 0.5f) - 1.0f, cy, cz}, D).hit &&
                                         w.ray({x2, cy, cz + 0.5f}, D).hit &&
                                         w.ray({x2, cy, (cz + 0.5f) - 1.0f}, D).hit;
                        if (all) {
                            const Vec3 H = R1.point;
                            Vec3 vv{T.x - cx, T.y - H.y, T.z - cz};
                            const float l = std::sqrt(vv.x * vv.x + vv.y * vv.y + vv.z * vv.z);
                            const Vec3 n = l > 0 ? Vec3{vv.x / l, vv.y / l, vv.z / l} : Vec3{};
                            const Vec3 St{cx - 0.5f * n.x, H.y + 1.0f, cz - 0.5f * n.z};
                            if (!w.ray(St, vv).hit) {              // a clear line to the target
                                A->pos.x = cx; A->pos.z = cz;
                                A->pos.y = (H.y + 1.0f - 0.5f) - A->height;   // origin = floor + 0.5
                            }
                        }
                    }
                }
            }
            a.i(0x730) = 0;
            break;
        case 4: {                                                  // attack
            const int N = d >= 1 ? 3 : d >= 0 ? 2 : 1;
            if (S == 7) {
                if (e.aso_marker != 1) break;
                out.set_marker = 2;
                float f = a.dist() * 0.125f;
                a.i(0x72c) += 1;
                a.cool() = 38;
                a.steer() = 1;
                if (f > 1.0f) f = 0.7854f;
                a.i(0x7a4) = 1;
                const float sf = float(std::sin(double(f)));
                float c2f = float(std::cos(double(f))); c2f = c2f + c2f;
                for (int k = 1; k <= N; ++k) {
                    const float s = float(k) * 0.1f;
                    const float u1 = U(), u2 = U(), u3 = U(), u4 = U();
                    const float yaw = ((s * 100.0f - s * -100.0f) * u1 + s * -100.0f) * 0.017453292f + a.heading;
                    const float vy = ((s - (-s)) * u2 + (-s)) + c2f;
                    const float vx = float(std::cos(double(yaw))) * sf + ((s * 0.75f - s * -0.75f) * u3 + s * -0.75f);
                    const float vz = float(std::sin(double(yaw))) * sf + ((s * 0.75f - s * -0.75f) * u4 + s * -0.75f);
                    AiBullet b;
                    b.effect = 0x1c0;                              // enemy\m_dokutama
                    b.pos = {float(std::cos(double(a.heading))) * 0.5f + e.self.x, e.self.y + 0.3f,
                             float(std::sin(double(a.heading))) * 0.5f + e.self.z};
                    b.dir = {vx, vy, vz};
                    b.speed = 0.1f; b.gravity = 0.004f; b.scale = 1.0f;
                    out.bullets.push_back(b);
                }
                break;
            }
            if (a.i(0x7a4) != 0) for (int k = 0; k < 4 * N; ++k) e.rng();   // discarded draws
            a.i(0x7a4) = 0; out.set_marker = 0; a.steer() = 0;
            const int cnt = a.i(0x72c);
            const bool done = tagged ? ((cnt == 4 && d == 3) || (cnt == 3 && d == 2) || (cnt == 2 && d <= 1))
                                     : ((cnt == 2 && d >= 2) || (cnt == 1 && d <= 1));
            if (done) {
                const int r = int(r15(e) % 30u);
                sub = 1;
                a.cool() = (r + 60) * (e.diff_index < 1 ? 3 : 2);
                a.i(0x730) += 1;
            } else if (a.cool() == 0) {
                if (ai::request(S, 7, false, e)) out.section = 7;
                out.attack = true;
            }
            break;
        }
        default:
            break;
    }
    // The fade tail.
    if ((S == 0x12d || S == 0x12e) && v > 3) {
        a.alpha = 1.0f - float(v - 3) * 0.25f;
        a.hittable = false;
    }
    return out;
}

}  // namespace cleria::brains
