// M980, Jenocres' crystals — brain FUN_004d4370, handler FUN_004d6ea0 (AI index 91).
// Spec: build/ai_spec/M980.md (the boss side: M360.md, brains/m360.cpp).
//
// Eight invulnerable turrets (HP refilled and DEF 999 every tick). They obey the boss's formation
// orders: on an order (sub-state 50) a crystal walks to its slot at 0.1 a tick (a displacement the
// handler adds before the integrator), snaps onto it and turns at 5 deg/tick to the slot's heading;
// after an "advance" (11/21/31/41) they slide or rotate by direct position writes clocked by the
// boss's +0x87c. While the boss holds a formation, the morph has caught up (g_flags[179] ==
// g_flags[180], written by the crystal ASO) and the facing has settled, each crystal attacks in the
// boss's mode: 0 a fire line (EF2 302 pieces), 1 a homing tornado (kind-4 bullet 502), 2 a lightning
// ball (kind-4 bullet 503, explodes into 231 at its end), 3 straight orbs (kind-1 bullet 310); the
// effect ids come from section 7's marker. An aim offset +0x7cc turns toward the player within a
// difficulty cap (unbounded in mode 1). The handler snaps the drawn yaw to 45-degree steps compensated
// for the pattern frame.
// [not modelled] byte +0x399. [approx] the yaw snap runs at the end of the brain with this tick's pre-step pattern
// frame (the engine's runs after the integrator); the root-life draws of the ball are made before
// the bullet's EF2 spawn draws (the engine draws them after); the host's bullet scan
// (view_crystal.inl) runs just before the bullet pass, and kills an ended crystal's bullets at once
// (the engine after its 43-tick fade).
#include <algorithm>
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {
namespace {

constexpr float kDeg = 0.0174533f, kRad2Deg = 57.2958f, k2Pi = 6.28319f;

constexpr float kSX[4][8] = {
    {9, -9, 9, -9, 9, -9, 9, -9},
    {3.3333333f, -3.3333333f, 10, -10, 10, -10, 3.3333333f, -3.3333333f},
    {0, -2.5f, 2.5f, -2.5f, 2.5f, -2.5f, 2.5f, 0},
    {0, -10, 0, -7.5f, 7.5f, 0, 10, 0}};
constexpr float kSZ[4][8] = {
    {4, 4, 10, 10, 16, 16, 22, 22},
    {3, 3, 9.6666667f, 9.6666667f, 16.333333f, 16.333333f, 23, 23},
    {10.5f, 10.5f, 10.5f, 13, 13, 15.5f, 15.5f, 15.5f},
    {3, 13, 5.5f, 13, 13, 20.5f, 13, 23}};
constexpr float kHead[4][8] = {                                  // 0x4d59c3..0x4d5b0a, degrees
    {180, 0, 180, 0, 180, 0, 180, 0},
    {90, 90, 180, 0, 180, 0, 270, 270},
    {270, 225, 315, 180, 0, 135, 45, 90},
    {90, 0, 90, 0, 180, 270, 180, 270}};

int row_of(int f) {                                              // fa/10: 1..3, else the plus row
    const unsigned q = unsigned(f) / 10u;
    return (q >= 1 && q <= 3) ? int(q) - 1 : 3;
}
float wrap360(float v) { while (v >= 360.0f) v -= 360.0f; while (v < 0.0f) v += 360.0f; return v; }
float wrap2pi(float v) { while (v >= k2Pi) v -= k2Pi; while (v < 0.0f) v += k2Pi; return v; }
float fcos(float x) { return float(std::cos(double(x))); }
float fsin(float x) { return float(std::sin(double(x))); }
bool outer(int s) { return s == 0 || s == 1 || s == 6 || s == 7; }

}  // namespace

AiOut m980(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    Actor* Ap = e.actor;
    AiWorld* w = e.world;
    if (!Ap || !w) return out;
    Actor& A = *Ap;
    if (e.launched) return out;                                    // +0x4d0: +0x7f0 keeps last tick's
    const int d = difficulty(e);
    A.hp = A.max_hp;                                               // 0x4d44ea: cannot be worn down
    A.def = 999.0f;
    a.f(0x7f0) = a.f(0x7f4) = a.f(0x7f8) = 0.0f;
    Actor* O = w->find(a.i(0x854));
    auto force = [&](int s) { out.section = s; out.section_force = true; };
    const int slot = a.i(0x7a0) & 7;
    const int o87c = O ? O->ai.i(0x87c) : 0;
    const Vec3 me = e.self;                                        // the origin

    bool scan_only = false;
    switch (a.phase()) {
        case 0: {                                                  // INIT (0x4d452e)
            const float f7ac = a.f(0x7ac);
            const int s7a0 = a.i(0x7a0), s79c = a.i(0x79c);
            ai::init(a, p, e);
            if (a.i(0x854) == 0) { a.phase() = 0; return out; }    // no owner: inert
            if (O) { A.level = O->level; A.atk = O->atk; }
            A.flags12c |= 0x2080040u;
            a.alpha = 0.0f;
            a.i(0x7a0) = s7a0; a.i(0x79c) = s79c;
            a.i(0x7a4) = a.i(0x7a8) = a.i(0x7bc) = 0;
            a.f(0x7ac) = f7ac; a.f(0x7b8) = f7ac;
            a.f(0x7b0) = a.f(0x7b4) = a.f(0x7cc) = 0.0f;
            a.i(0x88c) = 0; a.i(0x72c) = 0; a.i(0x730) = 0;
            a.cool() = 60; a.steer() = 5; a.action() = 2;
            ai::turn(a, a.f(0x814));
            a.phase() = 4;
            force(300);
            scan_only = true;
            break;
        }
        case 1: case 2: case 4: break;
        default: scan_only = true; break;
    }

    if (!scan_only) {
        a.phase() = 4; a.steer() = 5; a.action() = 2;              // MAIN (0x4d46c3)
        const int fa = a.i(0x79c), row = row_of(fa);
        auto tri = [&](float L) {                                  // the triangle wave (SUB_11 / SUB_21)
            const float k = (d > 0 ? float(d) * 0.05f : 0.0f) + 0.1f;
            const int Pn = int(L / k);
            if (Pn <= 0) return 0.0f;
            const int r = int(std::uint32_t(o87c) % std::uint32_t(Pn));
            const int h = Pn >> 1, q = Pn >> 2;
            if (r > h) return -(k * float(q - std::abs(q - (r - h))));
            return k * float(q - std::abs(q - r));
        };
        auto polar = [&](float& rad, float& a0) {
            const float dx = kSX[row][slot] - 0.0f, dz = kSZ[row][slot] - 13.0f;
            rad = float(std::sqrt(double(dz * dz + dx * dx)));
            a0 = float(std::atan2(double(dz), double(dx)));
        };
        auto place = [&](float ang, float rad) {
            A.pos.x = fcos(ang) * rad + 0.0f;
            A.pos.z = fsin(ang) * rad + 13.0f;
        };
        switch (a.i(0x72c)) {
            case 0:                                                // fade-in
                if (1.0f > a.alpha) a.alpha += 0.03f;
                else { a.alpha = 1.0f; a.i(0x72c) = 10; }
                break;
            case 11: {                                             // the columns slide in z
                float off = tri(12.0f);
                if (slot & 1) off = -off;
                A.pos.z = kSZ[row][slot] + off;
                break;
            }
            case 21: {                                             // the ring swings
                float off = tri(18.0f);
                if (slot == 0 || (slot != 7 && (slot & 1))) off = -off;
                if (outer(slot)) A.pos.x = kSX[row][slot] + off;
                else A.pos.z = kSZ[row][slot] + off;
                break;
            }
            case 31: {                                             // the tight ring rotates
                float rad, a0; polar(rad, a0);
                const float wv = (d > -2 ? float(d) * 0.007f : -0.007f) + 0.015f;
                const float an = wrap2pi(float(std::uint32_t(o87c)) * wv + a0);
                place(an, rad);
                a.f(0x7b8) = wrap360(an * kRad2Deg); a.f(0x7ac) = a.f(0x7b8);
                break;
            }
            case 40: case 41: {                                    // the plus rotates (41: the pulse)
                const bool adv = a.i(0x72c) == 41;
                if (!adv && O && O->ai.i(0x7a4) == -1) break;      // still travelling: hold still
                float rad, a0; polar(rad, a0);
                const std::uint32_t N = std::uint32_t(720 - 90 * d);
                const float ph = float(std::uint32_t(o87c) % N) * 360.0f / float((8 - d) * 90) * kDeg;
                const float an = wrap2pi(outer(slot) ? a0 + ph : a0 - ph);
                float yawoff = 180.0f;
                if (adv && !outer(slot)) {
                    yawoff = 0.0f;
                    const float k2 = (d > 0 ? float(d) * 0.005f : 0.0f) + 0.01f;
                    const int P2 = int(8.0f / k2);
                    const int r2 = int(std::uint32_t(a.i(0x87c)) % std::uint32_t(P2));
                    rad -= k2 * float((P2 >> 1) - std::abs((P2 >> 1) - r2));
                }
                place(an, rad);
                a.f(0x7b8) = wrap360(an * kRad2Deg + yawoff);
                break;
            }
            case 50: {                                             // travel to the slot
                const float vx = a.f(0x894) - me.x, vz = a.f(0x89c) - me.z;
                if (std::fabs(vx) + std::fabs(vz) < 0.1f) {
                    A.pos.x = a.f(0x894); A.pos.z = a.f(0x89c);
                    const int of = O ? O->ai.i(0x79c) : a.i(0x79c);
                    a.i(0x79c) = of;
                    const float H = kHead[row_of(of)][slot];
                    a.f(0x7b8) = H;
                    if (std::fabs(a.f(0x7ac) - H) < 1.0f) { a.f(0x7ac) = H; a.i(0x72c) = of; }
                } else {
                    const float h = float(std::atan2(double(vz), double(vx))) * kRad2Deg;
                    a.f(0x7b8) = a.f(0x7ac) = h;
                    a.f(0x7f0) = fcos(h * kDeg) * 0.1f; a.f(0x7f8) = fsin(h * kDeg) * 0.1f;
                }
                break;
            }
            default: break;                                        // 10/20/30, 12/22/32/42: still
        }

        // COMMON (0x4d5bac): the attack gate and the attack states.
        float eg = std::fabs(a.f(0x7ac) - a.f(0x7b8));
        while (eg > 180.0f) eg -= 180.0f;                          // sic: steps of 180
        a.i(0x7a4) += 1;
        const int f180 = w->flag(180, 0), f179 = w->flag(179, 0);
        const bool gate = O && O->ai.i(0x7a4) != -1 && f180 == f179 && 5.0f > eg;
        if (!gate) {                                               // RESET (0x4d683b)
            a.i(0x730) = (f180 * 5 + 5) * 2;
            a.cool() = int(r15(e) % 45u) + 45;
            if (e.section != 2) force(2);
        } else {
            const int m = e.aso_marker;
            const float ang = (a.f(0x7cc) + a.f(0x7ac)) * kDeg;
            auto arm = [&](int next) { a.i(0x730) = next; a.i(0x7a4) = 0; force(7); };
            auto bullet = [&](int kind, const Vec3& pos, const Vec3& dir) {
                AiBullet b; b.kind = kind; b.effect = m; b.pos = pos; b.dir = dir; b.knock = 5.0f;
                return b;
            };
            switch (std::uint32_t(a.i(0x730) - 10)) {
                case 0:                                            // ST10: mode 0 arm
                    if (a.cool() != 0) break;
                    a.cool() = int(r15(e) % 20u) * 4 + 160;
                    arm(11);
                    break;
                case 1: {                                          // ST11: the fire line
                    const std::uint32_t t = std::uint32_t(a.i(0x7a4));
                    if ((t & 7u) == 1u) {
                        const int n = int(std::min(t, std::uint32_t(a.cool())) >> 3);
                        const float c = fcos(ang), s = fsin(ang);
                        const float flip = (e.rng() & 0x20000u) ? 0.0f : 3.14159f;   // RAW
                        const Vec3 B{me.x + c, me.y + 0.3f, me.z + s};
                        if (m > 0) {
                            const float a2 = ang + 1.5708f + flip;
                            for (int i = 0; i <= n; ++i) {
                                if ((i & 1) != (n & 1)) continue;
                                const float u = unit(e.rng()) - 0.5f;
                                Vec3 pp{B.x + c * float(i) * 0.75f, B.y - float(std::min(i, 2)) * 0.35f,
                                        B.z + s * float(i) * 0.75f};
                                if (i != 0) {
                                    const float k = float(std::min(i, 12)), lo = -0.04f * k, span = 0.08f * k;
                                    pp.x += fcos(a2) * u + (unit(e.rng()) * span + lo);
                                    pp.z += fsin(a2) * u + (unit(e.rng()) * span + lo);
                                }
                                AiEffect fx; fx.id = m; fx.pos = pp; fx.power = 0.0f; fx.knock = 5.0f; fx.rtype = 1;
                                out.effects.push_back(fx);
                            }
                        }
                    }
                    if (O && O->ai.i(0x7c8) == 0) {
                        w->sound_at(536, {0.0f, -0.19f, 13.0f});   // zako\z_360_02
                        O->ai.i(0x7c8) = 90;
                    }
                    if (a.cool() == 0) { a.i(0x730) = 10; a.cool() = int(r15(e) % 45u) + 45; }
                    break;
                }
                case 10: case 20: case 30:                         // ST20 / ST30 / ST40: arm
                    if (a.cool() != 0) break;
                    arm(a.i(0x730) + 1);
                    a.cool() = 10;
                    break;
                case 11: {                                         // ST21: the tornado
                    if (a.cool() != 0) break;
                    const Vec3 dir{fcos(ang), 0.0f, fsin(ang)};
                    if (m > 0) {
                        a.i(0x7bc) = m;
                        AiBullet b = bullet(4, {me.x + dir.x, me.y + 0.3f, me.z + dir.z}, dir);
                        b.speed = 0.0001f; b.accel = float(d) * 0.00125f + 0.003f;
                        b.power = 1.5f; b.home_period = 1;
                        b.home_rate = std::max(0.0f, float(d) * 0.15f + 0.2f) * kDeg;
                        b.f168 = 0.0f; b.f16c = 20.0f; b.f170 = 360.0f; b.f174 = 0.0f;
                        b.root_remaining = 301; b.root_life = 301;
                        out.bullets.push_back(b);
                        out.sounds.push_back(540);                 // zako\z_360_06
                    }
                    a.i(0x730) = 20;
                    a.cool() = int(r15(e) % 50u) - 20 * d + 180;
                    break;
                }
                case 21: {                                         // ST31: the lightning ball
                    if (a.cool() != 0) break;
                    const Vec3 dir{fcos(ang), 0.0f, fsin(ang)};
                    if (m > 0) {
                        const std::uint32_t w1 = e.rng(), w2 = e.rng();   // RAW
                        AiBullet b = bullet(4, {me.x + dir.x, me.y - 0.2f, me.z + dir.z}, dir);
                        b.speed = 0.08f + unit(w2) * 0.04f;
                        b.accel = -0.0001f - unit(w1) * 0.001f;
                        b.power = 2.2f; b.home_period = 1;
                        b.home_rate = (float(d) * 0.2f + 0.9f) * kDeg;
                        b.f168 = 0.0f; b.f16c = 10.0f; b.f170 = 900.0f; b.f174 = 0.0f;
                        b.root_remaining = int(r15(e) % 240u) + 61;
                        b.root_life = int(r15(e) % 240u) + 61;
                        out.bullets.push_back(b);
                        out.sounds.push_back(538);                 // zako\z_360_04
                    }
                    a.i(0x730) = 30;
                    a.cool() = int(r15(e) % 90u) - 30 * d + 240;
                    break;
                }
                case 31: {                                         // ST41: the orb
                    if (a.cool() != 0) break;
                    a.f(0x7cc) *= 0.5f;
                    const float an = (a.f(0x7ac) + a.f(0x7cc)) * kDeg;
                    const Vec3 dir{fcos(an), 0.0f, fsin(an)};
                    if (m > 0) {
                        a.i(0x7a8) = m;
                        AiBullet b = bullet(1, {me.x + dir.x, me.y + 0.3f, me.z + dir.z}, dir);
                        b.speed = 0.1f; b.power = -0.5f;
                        out.bullets.push_back(b);
                        out.sounds.push_back(537);                 // zako\z_360_03
                    }
                    a.i(0x730) = 40;
                    a.cool() = int(r15(e) % 30u) - 11 * d + 45;
                    break;
                }
                default: break;
            }
        }
    }

    // SCAN (0x4d687c): (a) the bullet scan is the host's (view_crystal.inl); (b) facing.
    {
        float ef = a.f(0x7b8) - a.f(0x7ac);
        while (ef > 180.0f) ef -= 360.0f;
        while (ef <= -180.0f) ef += 360.0f;
        a.f(0x7ac) = wrap360(a.f(0x7ac) + std::clamp(ef, -5.0f, 5.0f));
    }
    {   // (c) the spin
        float v = a.f(0x7b0);
        if (v > 0.0f) { a.f(0x7b4) += std::min(5.0f, v); v -= 5.0f; a.f(0x7b0) = v < 0.0f ? 0.0f : v; }
        else if (v < 0.0f) { a.f(0x7b4) += std::max(v, -5.0f); v += 5.0f; a.f(0x7b0) = v > 0.0f ? 0.0f : v; }
        a.f(0x7b4) = wrap360(a.f(0x7b4));
    }
    {   // (d) the aim
        const Vec3 to = a.v(0x734);
        const float t = wrap360(float(std::atan2(double(to.z), double(to.x))) * kRad2Deg);
        float ea = t - a.f(0x7ac) - a.f(0x7cc);
        while (ea >= 180.0f) ea -= 360.0f;
        while (ea < -180.0f) ea += 360.0f;
        const float lim = (d > 0 ? float(d) : 1.5f) * 1.5f;
        a.f(0x7cc) += std::clamp(ea, -lim, lim);
        if (a.i(0x730) != 20 && a.i(0x730) != 21) {
            const float cap = 5.0f + (d > 0 ? 3.0f * float(d) : 2.0f * float(d));
            a.f(0x7cc) = std::clamp(a.f(0x7cc), -cap, cap);
        }
    }
    a.dur() = -1;                                                  // (e) the tail
    ai::act(a, p, e, &out);
    out.add_dx = a.f(0x7f0); out.add_dz = a.f(0x7f8);              // handler 0x4d71c6
    {   // the handler's yaw snap (0x4d7243): a multiple of 45 deg, compensated for the pattern frame
        const int Y = int(a.f(0x7b4) + a.f(0x7ac) + a.f(0x7cc) + 0.5f);
        auto m120 = [](int v) { v %= 120; return v < 0 ? v + 120 : v; };
        const int F = m120(e.aso_frame * 15);
        const int Y15 = m120((Y / 15) * 15);
        int diff = Y15 - F;
        while (diff > 60) diff -= 120;
        while (diff <= -60) diff += 120;
        int k = diff;
        while (k < diff + 360 && k % 45 != 0) k += 120;
        a.heading = float(k) * kDeg;
        out.snap_facing = true;
    }
    return out;
}

}  // namespace cleria::brains
