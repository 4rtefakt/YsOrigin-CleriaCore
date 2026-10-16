// M330 Pikkardon — brain FUN_004ea740 (handler FUN_004ebf70 = M910's with this brain and the boss gauge at
// lenScale 10000, AI index 26). Spec: build/ai_spec/M330.md (re-read: 0x6bd928 10.0, 0x6bd4e4 pi/2, 0x6bd958 13.0,
// 0x6bd494 1.25, the bullet constants 0x6bdc74 360, 0x6bdba0 180, 0x6bd0dc 0.0174533, 0x6bcfd0 0.0005,
// 0x6bd014 0.00225, 0x6bcfec 0.001, 0x6bd020 0.003, 0x6bd138 0.05, 0x6bd23c 0.23).
//
// It engages the tick after its init (no sight test) and has two long attacks, Durable and guarded from their
// first marker on:
//  - the leap (section 7; inside 10 u, 12% a tick, +/-90 deg): at Marker 1 a push of (horizontal distance / 13)
//    toward the target; at the landing (Marker 2, grounded) a cycle of three — effect 406; then twice effects
//    332 (x2) + 193 and a ring of 120 / 130 (Hard) / 140 (Nightmare) kind-2 bullets — and 6..8 M320 Pikkartons
//    while fewer than 13 actors live;
//  - the charge (section 8; 13% of the ticks the leap did not start): run x2, turn x1.25, 1 / 2 / 3 bullets a
//    tick through the Marker 1 window, then at Marker 2 it stuns itself for 99 ticks (stars, SE 184).
// [not modelled] fx+0x201 = 1 on the bullets' effects; the two mod-5 globals (no reader);
// the stars' rotation. [approx] effect 60 as the action player's stars record.
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"
#include "cleria/cos_table.hpp"

namespace cleria::brains {
namespace {

// FUN_0041c530 / FUN_0041c510: the 256-entry cosine table at i = (int)(angle x 40.7437); sin = the entry 0x40 back.
float tcos(float a) { return cos_table(int(a * 40.7437f)); }
float tsin(float a) { return cos_table(int(a * 40.7437f) - 0x40); }

// §3.7: n kind-2 bullets from the origin, all round the heading. `ring`: on Nightmare the 409 -> 515 roll is per
// bullet (sticky); a burst rolls it once before the loop.
void bullets(const EnemyAi& a, const AiEnv& e, int n, bool ring, AiOut& out) {
    int eff = 409;
    const float base = float(std::atan2(double(tsin(a.heading)), double(tcos(a.heading))));
    const bool nm = e.diff_index == 2;
    if (!ring && nm && ai::r15(e) % 3u == 0) eff = 515;
    for (int i = 0; i < n; ++i) {
        if (ring && nm && ai::r15(e) % 3u == 0) eff = 515;
        const float ang = (unit(e.rng()) * 360.0f - 180.0f) * 0.0174533f + base;
        Vec3 dir{tcos(ang), 0.0f, tsin(ang)};
        const float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
        if (len > 0.0f) { dir.x /= len; dir.z /= len; }
        float accel = unit(e.rng()) * 0.0005f - 0.00225f;
        if (nm) accel = unit(e.rng()) * 0.001f - 0.003f;
        AiBullet b;
        b.kind = 2; b.effect = eff;
        b.pos = e.self; b.dir = dir;
        b.speed = unit(e.rng()) * 0.05f + 0.23f;
        b.accel = accel;
        b.power = -10.0f; b.knock = 1.0f;
        out.bullets.push_back(b);
    }
}

}  // namespace

AiOut m330(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    out.gauge_hp = e.hp; out.gauge_len = 10000.0f;                 // the handler's FUN_0043a460 (0x4ec24a..0x4ec28f)
    if (e.launched) return out;                                    // +0x4d0
    const Vec3 to = a.v(0x734);
    const float hd = std::sqrt(to.x * to.x + 0.0f + to.z * to.z);
    const bool vok = !(e.target_grounded && std::fabs(e.target.y - e.self.y) >= 1.0f);
    Actor* A = e.actor;

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            for (int o = 0x834; o <= 0x850; o += 4) a.f(o) = 0;    // 32 bytes of the row are copied
            a.f(0x7ac) = a.f(0x820);                               // the run multiplier (2.0)
            a.f(0x7b0) = a.f(0x814);                               // the turn rate (0.06)
            return out;
        case 1:
            ai::acquire(a);
            return out;
        case 3:
            if (A) A->guard399 = false;
            return out;
        case 2:
            break;
        default:
            return out;
    }

    if (a.dist() > 20.0f) ai::disengage(a);                        // not an early out
    if (a.action() == 0) {
        if (a.i(0x728) != 100 && a.i(0x7bc) != 1) { a.action() = 0x400; a.i(0x728) = 0; }
        a.i(0x7bc) = 0;
    }
    const int sec = e.section, marker = e.aso_marker;
    bool guard = false;
    if (e.diff_index >= 0 && (sec == 7 || sec == 8)) {
        out.set_durable = 1;
        if (std::uint32_t(marker) >= 1u) guard = true;
    }
    if (A) A->guard399 = guard;

    auto in_cone = [&] {
        const float ang = float(std::atan2(double(to.z), double(to.x)));
        return 1.5707964 > std::fabs(ai::angle_diff(a.heading, ang));
    };
    switch (a.i(0x728)) {
        case 0: {
            a.f(0x820) = a.f(0x7ac);
            a.f(0x814) = a.f(0x7b0);
            auto gate = [&] { return 10.0f > a.dist() && sec != 7 && sec != 8 && a.cool() == 0 && e.grounded && vok; };
            if (gate() && r15(e) % 1000u < 120u && in_cone()) {    // the leap: the roll before the cone
                out.section = 7;
                out.attack = true;
                a.action() = 2;
                a.dur() = 105 + (e.diff_index <= -1 ? 25 : e.diff_index == 0 ? 15 : 0);
                a.cool() = a.dur() + 20 + int(r15(e) % 20u);
                a.i(0x79c) = 0; a.i(0x7a0) = 0; a.i(0x7bc) = 1; a.i(0x728) = 1;
            }
            if (gate() && r15(e) % 1000u < 130u && in_cone()) {    // the charge
                out.section = 8;
                out.attack = true;
                a.action() = 2; a.dur() = 140; a.cool() = 140; a.i(0x728) = 2;
            }
            break;
        }
        case 1:                                                    // the leap
            if (sec == 7 && marker == 1 && a.i(0x79c) == 0 && A) {
                a.i(0x79c) = 1;
                const float ang = float(std::atan2(double(to.z), double(to.x)));
                const float v = hd / 13.0f;
                A->mover.k1.x += tcos(ang) * v;                    // +0x338 / +0x340 (the table cos / sin)
                A->mover.k1.z += tsin(ang) * v;
            }
            if (marker == 2 && e.grounded && a.i(0x7a0) == 0) {    // the landing, once
                a.i(0x7a0) = 1;
                auto fx = [&](int id, float scale) {
                    AiEffect f;
                    f.id = id; f.pos = e.self; f.scale = scale; f.power = -8.0f; f.knock = 1.0f;
                    out.effects.push_back(f);
                };
                if (a.i(0x7a8) == 0) fx(406, 1.0f);
                else {
                    fx(332, 2.0f);
                    fx(193, 1.0f);
                    bullets(a, e, e.diff_index == 2 ? 140 : e.difficulty == 1 ? 130 : 120, true, out);
                }
                a.i(0x7a8) = a.i(0x7a8) + 1 == 3 ? 0 : a.i(0x7a8) + 1;
                if (std::uint32_t(e.pool_count) < 13u) {
                    const int k = int(r15(e) % 3u) + 6;
                    for (int i = 0; i < k && e.world && A; ++i)
                        if (Actor* c = e.world->spawn_child(*A, 0x24, {0, 0, 0}, A->level, true)) {
                            c->flags130 |= 1u; c->exp_reward = 0; c->sp_reward = 0;
                        }
                }
            }
            break;
        case 2:                                                    // the charge
            a.f(0x820) = a.f(0x7ac) + a.f(0x7ac);
            a.f(0x814) = a.f(0x7b0) * 1.25f;
            if (sec != 8) break;
            if (marker == 1) {
                a.action() = 0x400;
                bullets(a, e, e.diff_index == 2 ? 3 : e.difficulty == 1 ? 2 : 1, false, out);
            }
            if (marker == 2) {                                     // the self-stun (the action player's STUN_ENTER)
                out.effects.push_back({60, {e.self.x, e.self.y, e.self.z}, 0.8f, true});
                a.phase() = 3; a.dur() = 99; a.i(0x724) = 1;
                out.section = 0x13b;
                out.sounds.push_back(0xb8);
            }
            break;
        case 100:                                                  // the jump helper's (the cast has none: dead)
            if (sec != 0x13a && e.grounded) { a.action() = 0; a.i(0x728) = 0; }
            break;
        default:
            break;
    }
    return out;
}

}  // namespace cleria::brains
