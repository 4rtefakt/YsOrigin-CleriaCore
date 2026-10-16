// M530 Sepast — brain FUN_004965d0 (handler FUN_004978a0 = M100's, its arena clamp replaced
// by z >= -2.75 while DAT_0076bb8c is set: enemy_handler_clamp; AI index 46).
// Spec: build/ai_spec/M530.md.
//
// A spinning top. No randomness and no section requests. Engaged it moves in 60-tick legs
// (run; toward the target beyond 5 u horizontal, else away) at a speed that ramps 0.0425 x
// n / cap (cap 60 on Hard+, else 90). After L - 2 legs (L = 8, 6 group-tagged; a damaging hit
// on d >= 0 skips one ahead) a glowing leg, then the attack: it waits for frame 0, stands
// steering away for 112 / 224 / 336 ticks and, once the ASO marker is up, fires a volley of
// three EF2 235 beams (SE 477) every 28 ticks on a radius-1 ring at y - 0.7, re-placing every
// beam it owns each tick so the ring turns with the animation; then it faces the target for
// 30 - 10d ticks. Every tick it bounces off other Sepasts within 2r + 0.2 (pushing their
// accumulators too) and, engaged, off walls / ledges found by 12 probes at r + 0.2 (the push
// skewed +15 deg): knock channel 1 += 0.25 (0.4 tagged) along the push, the heading turned to
// it, and on contact a spark (EF2 65) and SE 472.
// [not modelled] the beams' particle-borne hits (EFFECTHIT.md: em6/em7, radius 0.25, re-hit
// 20 — no EF2 particle simulation yet); the glow leg's specular (+0x264..+0x274); the beams'
// rotation and the tagged x1.5 z scale; the
// neighbour filter's byte +0x11 bit 0.
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m530(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    AiOut out;
    if (e.launched) return out;
    Actor* A = e.actor;
    const Vec3 to = a.v(0x734);
    const float dxz = std::sqrt(to.x * to.x + 0.0f + to.z * to.z);
    const bool tagged = e.group_tag;
    const std::uint32_t L = tagged ? 6u : 8u;
    const int d = difficulty(e);
    const float r = e.radius;

    auto common_reset = [&] {
        a.i(0x7a0) = d >= 1 ? 30 : 45;
        a.i(0x730) = 0; a.i(0x72c) = 0; a.i(0x728) = 0;
    };
    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            a.f(0x7ac) = a.f(0x818);                               // the base speed
            a.i(0x7c8) = 0;
            common_reset();
            break;
        case 1:
            ai::idle(a, p, e);
            a.i(0x7c8) = int(L) - 3;
            a.f(0x818) = a.f(0x7ac);
            common_reset();
            break;
        case 2: {
            if (a.dist() > 20.0f) ai::disengage(a);               // the tick continues
            int& sub = a.i(0x72c);
            if (sub == 0) {                                        // the legs
                if (d != -2 && d != -1 && e.took_damage && std::uint32_t(a.i(0x7c8) + 2) < L) a.i(0x7c8) += 1;
                const std::uint32_t cap = d >= 1 ? 60u : 90u;
                std::uint32_t n = std::uint32_t(a.i(0x7a0));
                if (n < cap) { n += 1; a.i(0x7a0) = int(n); }
                if (std::uint32_t(a.i(0x7c8) + 1) == L && n > 4u) { n -= 4; a.i(0x7a0) = int(n); }
                a.f(0x818) = (float(n) * a.f(0x7ac)) / float(cap);
                if (a.dur() == 0) {
                    const std::uint32_t c = std::uint32_t(++a.i(0x7c8));
                    if (c + 1 == L) {                              // the glow leg (specular not drawn)
                        a.action() = 0x400; a.dur() = 60;
                    } else if (c == L) {                           // the attack
                        a.steer() = 3; a.action() = 1;
                        sub = 10; a.i(0x7c8) = 0; a.i(0x7a0) = 0;
                    } else {
                        a.action() = 0x400; a.dur() = 60;
                        a.steer() = dxz > 5.0f ? 0 : 3;
                    }
                }
            } else if (sub == 10 || sub == 11) {
                if (sub == 10) {                                   // the attack start
                    if (e.aso_frame != 0) break;
                    a.action() = 2; a.steer() = 3;
                    const int k = (d == -2 || d == -1) ? 4 : d == 0 ? 8 : 12;
                    a.dur() = 28 * k;
                    a.cool() = 28 * k + 2 * (5 * (-d) + 15);
                    a.i(0x7c0) = 0; a.i(0x7c4) = 0;
                    sub += 1;
                    a.i(0x87c) = 0;
                }
                if (a.dur() == 0) {                                // the beams are over
                    if (a.cool() != 0) { a.action() = 2; sub = 12; a.steer() = 0; a.dur() = a.cool(); }
                    else sub = a.i(0x79c);
                    break;
                }
                if (a.i(0x7c0) != 0) a.i(0x7c0) -= 1;              // the volley
                else if (e.aso_marker != 0) {
                    for (int i = 0; i < 3; ++i) {
                        const float an = ((30.0f - float(std::uint32_t(e.aso_frame)) * 15.0f) + float(i) * 120.0f) *
                                         3.14159f / 180.0f;
                        const float s = float(std::sin(double(an))), c = float(std::cos(double(an)));
                        AiEffect fx;
                        fx.id = 235;                               // enemy\m_530_bi-mu01.eft
                        fx.pos = {e.self.x + c, e.self.y - 0.7f, e.self.z + s};
                        fx.tag = 0xff - i;                         // fx+0x1d5 = ~i, fx+0x1a4 = self
                        fx.rot = {0.0f, -1.5708f - an, 0.0f};
                        out.effects.push_back(fx);
                    }
                    out.sounds.push_back(477);                     // zako\z_370_atk
                    a.i(0x7c0) = 27;
                }
                if (a.i(0x7c4) != e.aso_frame) { a.i(0x87c) = 0; a.i(0x7c4) = e.aso_frame; }
                for (int kk = 0; kk < 3; ++kk) {                   // re-place every owned beam
                    const float rr = (float(kk * 120) - (float(std::uint32_t(a.i(0x87c))) / 14.0f +
                                      float(std::uint32_t(e.aso_frame))) * 15.0f) * 0.0174533f;
                    const float c = float(std::cos(double(rr))), s = float(std::sin(double(rr)));
                    AiEffect mv;
                    mv.pos = {e.self.x + s, e.self.y - 0.7f, e.self.z + c * -1.0f};
                    mv.move_tag = 0xff - kk;
                    mv.rot = {0.0f, -rr, 0.0f};
                    out.effects.push_back(mv);
                }
            } else if (sub == 12) {                                // recovery
                if (a.dur() == 0 && e.aso_frame == 0) sub = a.i(0x79c);
            }
            break;
        }
        default: break;
    }

    // TAIL: the bounce (every phase).
    a.i(0x7bc) = 0;
    bool hit = false;
    if (e.world && A) {
        e.world->for_each([&](Actor& o) {
            if (o.cast_id != 57 || o.hp < 1.0f || &o == A || o.ai.phase() == 0) return;
            const Vec3 v{o.pos.x - A->pos.x, (o.pos.y + o.height) - (A->pos.y + A->height), o.pos.z - A->pos.z};
            const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            if (l > r * 2.0f + 0.2f) return;
            const Vec3 an = l == 0 ? Vec3{} : Vec3{v.x / l, v.y / l, v.z / l};
            a.f(0x7cc) += -an.x; a.f(0x7d0) += -an.z; hit = true;
            if (o.ai.i(0x7bc) == 0) { o.ai.f(0x7cc) += an.x; o.ai.f(0x7d0) += an.z; }
        });
    }
    if (a.phase() == 2 && e.world && A) {                          // the wall / ledge ring
        for (int deg = 0; deg < 360; deg += 30) {
            const float R = r + 0.2f;
            const float t = float(deg) * 3.14159f / 180.0f;
            const float s = std::sin(t), c = std::cos(t);
            const bool h1 = e.world->ray(e.self, {c * R, 0, s * R}).hit;
            if (h1) hit = true;
            const bool h2 = e.world->ray({e.self.x + c * R, A->floor_y + 1.0f + 0.0f, e.self.z + s * R}, {0, -2.0f, 0}).hit;
            if (!h2 || h1) {
                a.f(0x7cc) -= std::cos(t + 0.261799f);
                a.f(0x7d0) -= std::sin(t + 0.261799f);
            }
        }
    }
    const float px = a.f(0x7cc), pz = a.f(0x7d0);
    if (px == 0.0f && pz == 0.0f) return out;
    const float len = float(std::sqrt(double(pz * pz + px * px)));
    float kx = 0, kz = 0;
    if (!(1e-5f > len) && !std::isnan(len)) {
        const float k = tagged ? 0.4f : 0.25f;
        kx = px / len * k; kz = pz / len * k;
    }
    if (A) { A->mover.k1.z += kz; A->mover.k1.x += kx; }
    const float ux = px / len * r, uz = pz / len * r;
    if (hit) {
        AiEffect sp{65, {e.self.x - ux, e.self.y - 0.3f, e.self.z - uz}, 1.0f, false};   // z_yarare01
        sp.rot = {0.0f, -1.5708f - e.facing, 0.0f};
        out.effects.push_back(sp);
        out.sounds.push_back(472);                                 // zako\z_210_atk
    }
    a.heading = float(std::atan2(double(uz), double(ux)));
    a.i(0x7bc) = 1; a.f(0x7cc) = 0; a.f(0x7d0) = 0;
    return out;
}

}  // namespace cleria::brains
