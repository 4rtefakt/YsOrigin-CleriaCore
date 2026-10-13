// M590 Sepaswait — brain FUN_004a7030 (handler FUN_004a87f0, M100's shape; AI index 52).
// Spec: build/ai_spec/M590.md.
//
// M530's relative. Engaged it runs 30-tick legs, toward the target beyond 5 u horizontal,
// else away. At each leg start, with the cooldown spent and the target inside 7 u, it rolls a
// laser (25% - 5% per laser since the last dash; flat on very easy) or else a dash ((d+2) x 5%).
// Laser: at frame 0 it stands for 360..389 ticks; while 60 <= dur <= the window it fires three
// lasers every P ticks on a 0.5 ring at y - 0.6 (id, period and frame length from the section-2
// marker 9 | 25 | 446), re-placed every tick so the ring turns with the animation. Dash: turn
// to the target until frame 0, section 7 (rooted 46 ticks), then the move primitive's ramp
// (target factor x5, +0.15 a call) at a tenth of the turn rate, sparkles (the section-7 marker
// effect) at a rate rising with the ramp; at frame 7 after 180 ticks, section 8's 118-tick
// wind-down. Every tick: M530's bounce against cast 63 and (engaged) walls, with a direct
// 0.0625 nudge and the spark + SE on any push.
// The handler's water keep-out / dry-point trail is m590_keep_out below; the swim block's surface
// snap is in ai::move.
// [not modelled] the handler's section-change revert (M590.md §2.3); its kill of the owned lasers
// on the first dead tick (§2.2); the specular glow; the neighbour filter's byte +0x11 bit 0.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m590(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    AiOut out;
    if (e.launched) return out;
    Actor* A = e.actor;
    const Vec3 to = a.v(0x734);
    const float dxz = std::sqrt(to.x * to.x + 0.0f + to.z * to.z);
    const int d = difficulty(e);
    float mult = 1.0f;
    bool beam = false;
    bool dur8 = false;
    auto tv = [&]() -> Vec3 {                                      // the target vector (steer 0)
        if (a.ncrumb() != 0) return a.v(0x758) - e.self;
        return to;
    };
    auto turn_tv = [&] { a.set_v(0x748, tv()); ai::turn(a, a.f(0x814)); };

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            a.i(0x7a4) = 0;
            a.f(0x7ac) = a.f(0x814);                               // the base turn rate
            break;
        case 1:
            a.i(0x70c) = 2;                                        // stand: never wanders
            ai::idle(a, p, e);
            break;
        case 2:
            if (a.dist() > 20.0f) ai::disengage(a);                // the tick continues
            beam = true;
            break;
        default: break;
    }

    // The state switch runs in every phase.
    int& sub = a.i(0x72c);
    switch (sub) {
        case 0:                                                    // settle
            a.action() = 1;
            beam = false;
            if (e.aso_frame == 0) { a.i(0x730) = 0; sub = 20; }
            break;
        case 20: {                                                 // legs and the attack roll
            if (a.dur() != 0 || a.phase() != 2 || a.i(0x730) != 0) break;
            a.action() = 0x400; a.dur() = 30;
            a.steer() = dxz > 5.0f ? 0 : 3;
            if (a.cool() == 0 && 7.0f > a.dist()) {
                const int pen = d < -1 ? 0 : a.i(0x7c4) * -50;
                const std::uint32_t w1 = e.rng();
                if ((w1 >> 17) % 1000u < std::uint32_t(pen + 250)) { sub = 100; a.i(0x79c) = 0; break; }
                const std::uint32_t w2 = e.rng();
                if (int((w2 >> 17) % 1000u) < (d + 2) * 50) { sub = 200; a.i(0x79c) = 0; }
            }
            break;
        }
        case 100: {                                                // laser start
            a.action() = 0x400;
            if (e.aso_frame != 0) break;
            const int r = int((e.rng() >> 17) % 30u);
            a.dur() = 360 + r;
            a.i(0x7a8) = 330 + r;
            sub = 101; a.i(0x7c4) += 1; a.i(0x7c8) = 0;
            break;
        }
        case 101:                                                  // laser hold
            a.action() = 1;
            a.cool() = 150 - 20 * d;
            if (std::uint32_t(a.dur()) < 60u || std::uint32_t(a.i(0x7a8)) < std::uint32_t(a.dur())) beam = false;
            if (a.dur() == 0) sub = 0;
            break;
        case 200:                                                  // dash wind-up
            a.i(0x7c4) = 0;
            a.action() = 2; a.steer() = 0;
            beam = false;
            turn_tv();
            if (e.aso_frame != 0) break;
            a.action() = 0x800;
            out.section = 7;                                       // gated; result ignored
            sub = 201; a.dur() = 180;
            a.set_v(0x748, tv());
            // sic (0x4a7744): turns toward the integrator's drift vector +0x3ec, normally zero.
            if (a.turn_hold() == 0) a.heading = rotate_toward(a.heading, 0.0f, a.f(0x814));
            else --a.turn_hold();
            dur8 = true;
            break;
        case 201:                                                  // dash
            a.steer() = 0; dur8 = true;
            if (e.movable >= 0.5f || std::isnan(e.movable)) {
                a.f(0x78c) = a.f(0x820) * 5.0f;
                a.f(0x82c) = a.f(0x830) = 0.15f;
                mult = 0.1f;
            } else {
                a.f(0x78c) = 0.0f;
            }
            turn_tv();
            if (e.aso_frame == 7 && (a.dur() == 0 || e.section != 7)) {
                sub = 202; a.action() = 0x800; a.dur() = 118;
                out.section = 8;                                   // gated; result ignored
                a.f(0x78c) = a.f(0x820);
                a.f(0x82c) = a.f(0x830) = 0.05f;
            }
            break;
        case 202:                                                  // wind-down
            a.steer() = 0; dur8 = true;
            turn_tv();
            if (e.section == 8) break;
            a.action() = 1; a.f(0x78c) = 0.0f;
            a.cool() = 120 - 20 * d;
            sub = 0;
            break;
        default: break;
    }

    // §3.2 dash sparkles: the section-7 marker IS the effect id (72).
    if (a.f(0x78c) > 0.0f && std::uint32_t(e.aso_marker) > 10u && e.section == 7) {
        const int n = int(std::max(4.0f, (1.0f - a.f(0x824) / (a.f(0x820) * 4.0f)) * 12.0f));
        if (std::uint32_t(a.i(0x7a0)) % std::uint32_t(n) == 0) {
            const int c = ++a.i(0x7c0);
            const int ang = ((c % 3) * 8 - e.aso_frame) * 15;
            const float s = unit(e.rng()) * 2.0f + 0.5f;           // RAW
            const float an = float(ang) * 0.0174533f;
            AiEffect fx;
            fx.id = e.aso_marker;
            fx.pos = {e.self.x + float(std::sin(double(an))) * 0.5f, e.self.y - 0.6f,
                      e.self.z - float(std::cos(double(an))) * 0.5f};
            fx.scale = s;
            out.effects.push_back(fx);
        }
    } else {
        a.i(0x7c0) = 0;
    }

    // §3.3 the turn rate and the bounce.
    a.f(0x814) = a.f(0x7ac) * mult;
    a.i(0x7bc) = 0;
    const float r = e.radius;
    if (e.world && A) {
        e.world->for_each([&](Actor& o) {
            if (o.cast_id != 63 || o.hp < 1.0f || &o == A || o.ai.phase() == 0) return;
            const Vec3 v{o.pos.x - A->pos.x, (o.pos.y + o.height) - (A->pos.y + A->height), o.pos.z - A->pos.z};
            const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            if (l > r * 2.0f + 0.2f) return;
            const Vec3 an = l == 0 ? Vec3{} : Vec3{v.x / l, v.y / l, v.z / l};
            a.f(0x7cc) += -an.x; a.f(0x7d0) += -an.z;
            if (o.ai.i(0x7bc) == 0) { o.ai.f(0x7cc) += an.x; o.ai.f(0x7d0) += an.z; }
        });
    }
    if (a.phase() == 2 && e.world) {                               // walls: one ray per probe
        for (int deg = 0; deg < 360; deg += 30) {
            const float R = r + 0.2f;
            const float t = float(deg) * 3.14159f / 180.0f;
            if (e.world->ray(e.self, {std::cos(t) * R, 0, std::sin(t) * R}).hit) {
                a.f(0x7cc) -= std::cos(t + 0.261799f);
                a.f(0x7d0) -= std::sin(t + 0.261799f);
            }
        }
    }
    Vec3 at = e.self;                                              // x, z after the nudge
    const float px = a.f(0x7cc), pz = a.f(0x7d0);
    if (px != 0.0f || pz != 0.0f) {
        const float len = float(std::sqrt(double(pz * pz + px * px)));
        float kx = 0, kz = 0;
        if (!(1e-5f > len) && !std::isnan(len)) { kx = px / len * 0.25f; kz = pz / len * 0.25f; }
        if (A) {
            A->mover.k1.x += kx; A->mover.k1.z += kz;              // channel 1: inert (+0x12c & 0x40)
            A->pos.x += kx * 0.25f; A->pos.z += kz * 0.25f;
        }
        at.x += kx * 0.25f; at.z += kz * 0.25f;
        const float ux = px / len * r, uz = pz / len * r;
        AiEffect sp{65, {at.x - ux, at.y - 0.3f, at.z - uz}, 1.0f, false};   // z_yarare01
        sp.rot = {0.0f, -1.5708f - e.facing, 0.0f};
        out.effects.push_back(sp);
        out.sounds.push_back(472);                                 // zako\z_210_atk
        a.heading = float(std::atan2(double(uz), double(ux)));
        a.i(0x7bc) = 1; a.f(0x7cc) = 0; a.f(0x7d0) = 0;
    }

    // §3.4 the frame clock: +0x7a4 = frame x 10^6 + D x 10^3 + ticks in this frame.
    a.i(0x7a0) += 1;
    std::uint32_t& clk = reinterpret_cast<std::uint32_t&>(a.i(0x7a4));
    const std::uint32_t q = ++clk / 1000u;
    const std::uint32_t frame = std::uint32_t(e.aso_frame);
    if (frame != q / 1000u) clk = (frame * 1000u + q % 1000u) * 1000u;
    auto ring = [&](int k) {
        const float n = float(clk % 1000u), D = float((clk / 1000u) % 1000u);
        return (float((8 * k - e.aso_frame) * 15) - (n / D) * 15.0f) * 0.0174533f;
    };
    auto ring_pos = [&](float an) {
        return Vec3{at.x + float(std::sin(double(an))) * 0.5f, at.y - 0.6f, at.z - float(std::cos(double(an))) * 0.5f};
    };
    if (e.section == 2 && a.phase() == 2 && beam && sub == 101) {
        out.set_durable = 1;                                       // Durable while firing
        const int m = e.aso_marker;
        const int L = m % 1000, P = (m / 1000) % 1000;
        clk = ((clk / 1000000u) * 1000u + std::uint32_t(m / 1000000)) * 1000u + clk % 1000u;
        int c = a.i(0x7c8);
        if (c != 0) { c += 1; a.i(0x7c8) = c; }
        if (L > 0 && P > 0 && std::uint32_t(a.i(0x7a0)) % std::uint32_t(P) == 0) {
            a.i(0x7c8) = c = c + 1;
            for (int i = 0; i < 3; ++i) {
                const float an = ring(i);
                AiEffect fx;
                fx.id = L;                                         // enemy\m_590_laser.eft
                fx.pos = ring_pos(an);
                fx.rot = {0.0f, -an, 0.0f};
                fx.rtype = 1;
                fx.tag = 0xff - i;                                 // fx+0x1a4 = self, +0x1d5 = ~i
                out.effects.push_back(fx);
            }
        }
        if (c % 60 == 1) out.sounds.push_back(523);                // zako\z_590_atk
    } else {
        out.set_durable = dur8 ? 1 : 0;
    }

    // §3.5 every owned laser re-placed on the ring.
    for (int k = 0; k < 3; ++k) {
        const float an = ring(k);
        AiEffect mv;
        mv.pos = ring_pos(an);
        mv.rot = {0.0f, -an, 0.0f};
        mv.move_tag = 0xff - k;
        out.effects.push_back(mv);
    }
    return out;
}

// The handler's top, before its HP test (M590.md §2.1 [BYTES 0x4a881b..0x4a8ade]). Over water — the origin under
// +0x320 + 0.6, or a set-1 surface within 100 below it (`water_below`) — the actor is drawn halfway back to its
// last dry sample C (+0x7e4) on x / z and takes C's y; under +0x320 + 0.7 it is grounded with the timer at 0. Dry:
// the three samples A (+0x7f0), B (+0x7fc), C follow the origin in steps of over 0.2 (dy weighted x 0.05).
// +0x320 is the placement's set-1 hit or the constructor's 0, as are the samples of an M590 that was never dry
// [EXPORT 004590b0.c:24: the actor is zeroed from +0x120 for 2000 bytes]; the per-tick probe is off for this cast
// (+0x12c & 0x8000). [approx] the port has no placement probe: 0 always.
bool m590_keep_out(EnemyAi& a, Vec3& o, bool water_below, bool& grounded, float& timer) {
    const float w = a.w320();
    if (w + 0.6f > o.y || water_below) {
        const float y = w + 0.6f > o.y ? w + 0.6f : o.y;
        if (w + 0.7f > y) { grounded = true; timer = 0.0f; }
        o = {(a.f(0x7e4) - o.x) * 0.5f + o.x, a.f(0x7e8), (a.f(0x7ec) - o.z) * 0.5f + o.z};
        return true;
    }
    auto far = [](const Vec3& p, const Vec3& q) {
        const double dx = std::fabs(double(p.x - q.x)), dy = std::fabs(double(p.y - q.y)), dz = std::fabs(double(p.z - q.z));
        const float ey = float(dy) * 0.05f;
        return float(dx) * float(dx) + ey * ey + float(dz) * float(dz) > 0.04f;
    };
    if (far(o, a.v(0x7f0))) a.set_v(0x7f0, o);
    if (far(a.v(0x7f0), a.v(0x7fc))) a.set_v(0x7fc, a.v(0x7f0));
    if (far(a.v(0x7fc), a.v(0x7e4))) a.set_v(0x7e4, a.v(0x7fc));
    return false;
}

}  // namespace cleria::brains
