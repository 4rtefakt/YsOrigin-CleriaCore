// M580 Sevnam — brain FUN_0049db20 (handler FUN_0049f080 = M100's minus the arena clamp,
// AI index 51). Spec: build/ai_spec/M580.md — M500 Oggnome's teleporter (m500.cpp) with the
// differences D1..D11 marked below and its own attack.
//
// It never walks: it hides, relocates while hidden (on every difficulty, within 16 / 12 / 8 of
// home by difficulty), appears within 8 u and attacks: (d + 5) x 3% a tick, facing within
// 60 deg, section 7 requested at once. At each of section 7's three markers (the marker word
// is two effect ids, m / 10000 and m % 10000: 491 m_580_awa2 both) it spits N = 1 (2 above
// Normal) bubbles almost straight up (speed 0.5), each followed by two drops falling from
// 21..25 u (speed 0.15): onto +/-3 of the target 75% of the time on the first spit when near,
// else +/-8 around itself. The attack ends when section 7 does (no re-request): cooldown
// (R%30 + 60) x 3 (x 2 above Normal).
// [not modelled] the keep-out box restore (FUN_0042dc90); the water-set up-ray of the
// relocation; +0x144 x the fade; the bullets' damage (the GoXx paths, kinds 1 and 0x29, are
// untraced) and the drops' extra block fields (+0x160..+0x174).
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m580(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    Actor* A = e.actor;
    if (e.launched) return out;
    a.hittable = true;                                             // +0x60 |= 0x8c
    a.alpha = 1.0f;
    const Vec3 T = e.target;
    const int d = difficulty(e);
    a.f(0x820) = 0; a.f(0x818) = 0;                                // NO walking
    a.mode() = 0;
    int& sub = a.i(0x728);
    int S = e.section;
    const bool tagged = e.group_tag;
    auto U = [&] { return unit(e.rng()); };

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            for (int off = 0x834; off <= 0x850; off += 4) a.f(off) = 0;   // only 32 bytes copied (D1)
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
                    if (int(r15(e) % 1000u) < (d + 5) * 30) {      // D2
                        const Vec3 to = a.v(0x734);
                        const float ang = float(std::atan2(double(to.z), double(to.x)));
                        if (1.0471976f > float(std::fabs(ai::angle_diff(a.heading, ang)))) {
                            sub = 4; a.i(0x72c) = 0; a.i(0x87c) = 0; a.i(0x7a4) = 0;
                            out.section = 7; out.attack = true;    // D3: gated, result ignored
                            if (ai::request(S, 7, false, e)) S = 7;
                            to_switch = true;
                        }
                    }
                }
                if (!to_switch && sub == 1 && a.i(0x730) != 0) {
                    const std::uint32_t r = r15(e) % 10000u;
                    const std::uint32_t n = std::uint32_t(a.i(0x730));
                    const bool hit = d >= 2 ? r < n * n * 200u : d >= 0 ? r < n * 20u : r < 10u;   // D4, D5
                    if (hit) { sub = 2; a.i(0x72c) = 0; a.i(0x7a4) = 0; }
                }
            } else if (sub == 1) {
                a.cool() = 0; sub = 2; a.i(0x72c) = 0; a.i(0x7a4) = 0;
            }
            break;
        default:
            break;
    }

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
                if (v == 7) {
                    const int r = int(r15(e) % 120u);
                    sub = 3; a.i(0x72c) = 0;
                    a.cool() = r - 20 * d + 90;                    // D6
                    a.i(0x7a4) = 0;
                }
            } else {
                if (ai::request(S, 0x12e, false, e)) out.section = 0x12e;
                v = 0;
            }
            break;
        case 3:                                                    // hidden: relocate (D7: any d)
            if (a.phase() == 2 && A && e.world) {
                if (r15(e) % 1000u < 200u && !tagged) {
                    const float ua = U(), ub = U();
                    const float rad = ub * 6.5f + 1.5f;
                    const double th = double(ua * 360.0f + 0.0f);  // 360 radians, as written
                    const float cx = float(std::cos(th)) * rad + T.x;
                    const float cy = T.y + 2.0f;
                    const float cz = float(std::sin(th)) * rad + T.z;
                    const float hx = a.f(0x7cc) - cx, hz = a.f(0x7d4) - cz;
                    const float lim = d >= 2 ? 256.0f : d >= 0 ? 144.0f : 64.0f;   // D8
                    if (hx * hx + 0.0f + hz * hz < lim) {
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
                            if (!w.ray(St, vv).hit) {
                                A->pos.x = cx; A->pos.z = cz;
                                A->pos.y = (H.y + 1.0f - 0.5f) - A->height;
                            }
                        }
                    }
                }
            }
            a.i(0x730) = 0;
            if (S != 0x12e) {                                      // D9: force the fade back
                out.section = 0x12e; out.section_force = true; S = 0x12e;
                sub = 2;
            }
            break;
        case 4: {                                                  // attack (D10, D11)
            if (S == 7) {
                const int m = e.aso_marker;
                if (m == 0) break;
                out.set_marker = 0;                                // consume
                a.i(0x72c) += 1;
                const int eff_up = int(std::uint32_t(m) / 10000u), eff_down = int(std::uint32_t(m) % 10000u);
                a.cool() = 38;
                a.steer() = 1;
                const int N = (d > 0) + 1;
                a.i(0x7a4) = 1;
                for (int i = 0; i < N; ++i) {
                    const float h = a.heading;
                    const float u1 = U(), u2 = U(), u3 = U();
                    AiBullet b;
                    b.pos = {float(std::cos(double(h))) * 0.5f + (u1 * 0.01f - 0.005f) + e.self.x,
                             (u2 * 0.01f - 0.005f) + 0.5f + e.self.y,
                             float(std::sin(double(h))) * 0.5f + (u3 * 0.01f - 0.005f) + e.self.z};
                    const float u4 = U();
                    Vec3 V{float(std::cos(double((u4 * 0.2f - 0.1f) + a.heading))), 2.0f, 0};
                    const float u5 = U();
                    V.z = float(std::sin(double((u5 * 0.2f - 0.1f) + a.heading)));
                    const float l = std::sqrt(V.x * V.x + V.y * V.y + V.z * V.z);
                    if (l > 0) V = {V.x / l, V.y / l, V.z / l};
                    b.effect = eff_up; b.dir = V; b.speed = 0.5f; b.scale = 1.0f;   // the spit (kind 1)
                    out.bullets.push_back(b);
                    for (int k = 0; k < 2; ++k) {
                        Vec3 Q;
                        if (i == 0 && 8.0f > a.dist() && r15(e) % 1000u < 750u) {
                            const float ux = U(), uy = U(), uz = U();
                            Q = {(ux * 6.0f - 3.0f) + T.x, uy * 4.0f + 0.0f + 21.0f + T.y, (uz * 6.0f - 3.0f) + T.z};
                        } else {
                            const float ux = U(), uy = U(), uz = U();
                            Q = {(ux * 16.0f - 8.0f) + e.self.x, uy * 4.0f + 0.0f + 21.0f + e.self.y,
                                 (uz * 16.0f - 8.0f) + e.self.z};
                        }
                        AiBullet dr;                               // the drop (kind 0x29)
                        dr.effect = eff_down; dr.pos = Q; dr.dir = {0, -1.0f, 0};
                        dr.speed = 0.15f; dr.scale = 1.0f;
                        out.bullets.push_back(dr);
                    }
                }
                break;
            }
            a.i(0x7a4) = 0; out.set_marker = 0; a.steer() = 0;     // refused / finished / interrupted
            const int r = int(r15(e) % 30u);
            sub = 1;
            a.cool() = (r + 60) * (e.diff_index < 1 ? 3 : 2);
            a.i(0x730) += 1;
            break;
        }
        default:
            break;
    }
    if ((S == 0x12d || S == 0x12e) && v > 3) {                     // the fade tail
        a.alpha = 1.0f - float(v - 3) * 0.25f;
        a.hittable = false;
    }
    return out;
}

}  // namespace cleria::brains
