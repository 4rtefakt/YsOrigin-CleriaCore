// M210 Kelemare — brain FUN_004865b0 (handler FUN_004887a0 = M100's minus the arena clamp,
// AI index 14). Spec: build/ai_spec/M210.md.
//
// A scorpion. Re-deciding each tick: beyond 5.75 u it approaches; 3.75..5.75 it backs off
// facing the target (an impulse into knock channel 2); under 3.75 (5.25 on Nightmare) it
// rolls a 21% sting (section 7, two strikes; on Hard+ it re-aims and creeps between them;
// on Nightmare from range a 0.4 lunge). Within 9.5 u with the spit lockout clear it stands
// and spits: section 8 if the target is not above, section 0x139 (a hop-spit, 0.25 at
// Marker 1) if 0.9..2.4 above; the lockout is then R%60 + (3 - max(d,0))*60. After a sting it
// flees (steer away, run x2.5, turn x1.5) until beyond 10.5, 150 ticks, or inside 3.25 after
// 60. On Hard and above an evade (sub 4): 7% (87% right after an attack) when the target is
// outside +/-60 deg or swinging at it face to face — a 0.17 hop back 0.4 (+/-0.5 rad, floor
// 4.8 behind) or, walled in, forward 0.7.
// [not modelled] "spawned mode" (+0x730 != 0, set only by the gimmick spawner FUN_00511340:
// the fade/scale intro, waypoint backing and bubble bullets); water (+0x389/+0x38a/+0x320);
// the keep-out box restore (FUN_0042dc90).
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m210(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (a.i(0x7a8) != 0) a.i(0x7a8) -= 1;                         // before everything
    if (e.launched) return out;
    Actor* A = e.actor;
    const int d = difficulty(e);
    const Vec3 to = a.v(0x734);
    const Vec3 T = e.target;
    const float dyT = T.y - e.self.y;
    const bool near_y = std::fabs(dyT) < 3.5f;
    const int P_sect = e.target_section;
    const float T_head = e.target_heading;
    const float a_T = float(std::atan2(double(to.z), double(to.x)));
    auto adiff = [&](float h) { return float(std::fabs(ai::angle_diff(h, a_T))); };
    const bool grounded = e.grounded;
    // The mid-air boost toward a higher target (not below Normal).
    if (A && !grounded && A->vert.t <= 0.1f && a.f(0x7ac) > 0.15f && d >= 0) {
        const float t = dyT - 1.4f;
        float v = dyT > 1.2f ? (1.0f <= t ? t : 1.0f) : (0.6f <= t ? t : 0.6f);
        v *= 0.25f;
        A->vert.t = v;
        A->pos.y += v * 1.2f;
    }
    a.f(0x7ac) = A ? A->vert.t : 0.0f;
    if (grounded) a.f(0x7ac) = 0;
    int& sub = a.i(0x728);
    auto jump = [&](float v) { if (A) brains::jump(*A, v, e.jump_v0, out); };

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            for (int off = 0x834; off <= 0x850; off += 4) a.f(off) = 0;   // only 32 bytes copied
            if (d >= 2) { a.f(0x814) *= 1.1f; a.f(0x820) *= 1.2f; a.f(0x81c) *= 1.6f; }
            a.f(0x7cc) = a.f(0x814); a.f(0x7d0) = a.f(0x820); a.f(0x7d4) = a.f(0x81c);
            a.i(0x7a8) = 0;
            return out;
        case 1:
            ai::idle(a, p, e);
            return out;
        case 3:
            return out;
        case 2: break;
        default: return out;
    }
    const float dist = a.dist();
    int S = e.section;
    if (dist > 20.0f) ai::disengage(a);
    if (S == 7) { a.i(0x79c) = 0; a.i(0x7c0) = 1; }
    a.f(0x814) = a.f(0x7cc); a.f(0x820) = a.f(0x7d0);
    auto evade_ok = [&] {
        return adiff(a.heading) > 1.0471976f ||
               (std::uint32_t(P_sect - 0x100) <= 8u && adiff(T_head) > 2.0943952f);
    };
    // B: the pick, only when the last action is over.
    if (a.action() == 0) {
        if (a.i(0x7c0) == 1) { sub = 2; a.action() = 0x400; }
        else if (dist < 3.75f || (d >= 2 && dist < 5.25f)) {
            bool evade = false;
            if (std::uint32_t(r15(e) % 1000u) < std::uint32_t(a.i(0x7bc) * 800 + 70) && evade_ok()) evade = true;
            if (evade && e.diff_index >= 1) { a.action() = -1; sub = 4; a.i(0x72c) = 1; }
            else { sub = 1; a.action() = 0x400; }
        } else if (dist < 5.75f) { a.action() = -1; sub = 3; }
        else { sub = 0; a.action() = 0x400; }
        a.i(0x7bc) = 0; a.i(0x7c0) = 0;
    }
    auto spit_common = [&] {
        a.action() = 1;
        a.dur() = 50 + (e.diff_easy ? 20 : e.diff_index == 0 ? 10 : 0);
        a.cool() = a.dur() + int(r15(e) % 30u);
        a.i(0x7bc) = 1;
        const int base = (3 - std::max(d, 0)) * 60;
        const int r2 = int(r15(e) % 60u) + base;
        a.i(0x7a8) = r2 > 0 ? int(r15(e) % 60u) + base : 0;
    };
    switch (sub) {
        case 0: case 3: {                                          // stand-and-spit / retreat
            bool attacked = false;
            if (dist < 9.5f && a.i(0x7a8) == 0) {
                const bool ready = S != 8 && S != 0x139 && a.cool() == 0 && near_y &&
                                   adiff(a.heading) < 0.62831855f;
                if (dyT < 0.9f) {
                    a.action() = 2;
                    if (ready && grounded) {
                        if (ai::request(S, 8, false, e)) out.section = 8;
                        out.attack = true; spit_common(); attacked = true;
                    }
                } else if (dyT < 2.4f) {
                    a.action() = 2;
                    if (ready && grounded) {
                        if (ai::request(S, 0x139, false, e)) out.section = 0x139;
                        out.attack = true; spit_common(); attacked = true;
                    }
                }
            }
            if (S == 0x139 && grounded && e.aso_marker == 1) jump(0.25f);   // the hop-spit lift
            if (sub == 3 && !attacked && S != 0x139 && S != 8) {
                if (dist < 3.75f || dist > 8.75f) { a.action() = 0; sub = 0; }
                else {
                    ai::stop(a, 0);
                    ai::steer(a, e);
                    ai::turn(a, a.f(0x814));
                    a.mode() = 1;
                    const float k = a.f(0x81c) * a.f(0x818) * 0.9f;
                    const float ba = a.heading + 3.1415927f;
                    if (A) { A->mover.k2.x += std::cos(ba) * k; A->mover.k2.z += std::sin(ba) * k; }
                    if (r15(e) % 1000u < 70u && evade_ok() && e.diff_index >= 1) {
                        a.action() = -1; sub = 4; a.i(0x72c) = 1;
                    }
                }
            }
            break;
        }
        case 1:                                                    // close in and sting
            if ((dist < 3.75f || (d >= 2 && dist < 5.25f)) && S != 7 && a.cool() == 0 && grounded &&
                near_y && adiff(a.heading) < 0.62831855f) {
                if (r15(e) % 1000u < 210u) {
                    if (d >= 2 && dist >= 3.75f && A) {            // Nightmare long lunge
                        const float l = std::sqrt(to.x * to.x + to.y * to.y + to.z * to.z);
                        if (l > 0) { A->mover.k1.x += to.x / l * 0.4f; A->mover.k1.z += to.z / l * 0.4f; }
                    }
                    out.section = 7; out.attack = true;
                    a.action() = 1;
                    a.dur() = 50 + (e.diff_easy ? 20 : e.diff_index == 0 ? 10 : 0);
                    a.cool() = a.dur() + int(r15(e) % 20u);
                    a.i(0x7bc) = 1;
                }
            }
            break;
        case 2:                                                    // flee after the sting
            a.i(0x79c) += 1; a.i(0x7c0) = 1;
            a.steer() = 3;
            a.f(0x814) = a.f(0x7cc) * 1.5f; a.f(0x820) = a.f(0x7d0) * 2.5f;
            if (dist > 10.5f || std::uint32_t(a.i(0x79c)) > 150u ||
                (dist < 3.25f && std::uint32_t(a.i(0x79c)) > 60u)) {
                a.action() = 0; sub = 0; a.i(0x7c0) = 0; a.i(0x79c) = 0; a.steer() = 0;
                a.f(0x814) = a.f(0x7cc); a.f(0x820) = a.f(0x7d0);
            }
            break;
        case 4: {                                                  // the evade hop
            bool go_hop = true;
            if (a.i(0x72c) == 1) {
                a.i(0x72c) = 0;
                float vx = -to.x, vz = -to.z;
                const float l = std::sqrt(vx * vx + vz * vz);
                if (l > 0) { vx = vx / l * 0.4f; vz = vz / l * 0.4f; } else { vx = vz = 0; }
                const float ang = unit(e.rng()) - 0.5f;            // RAW word
                const float s = float(std::sin(double(ang))), c = float(std::cos(double(ang)));
                const Vec3 D{c * vx + s * vz, 0, c * vz - s * vx};
                a.set_v(0x7f0, D);
                const Vec3 O{e.self.x, e.self.y + 1.0f, e.self.z};
                int kind = 0;
                if (e.world) {
                    if (!e.world->ray(O, {D.x * 5.0f, 0, D.z * 5.0f}).hit) {
                        if (e.world->ray({O.x + D.x * 12.0f, O.y, O.z + D.z * 12.0f}, {0, -3.5f, 0}).hit) kind = 1;
                    } else {
                        const Vec3 F{float(std::cos(double(a.heading))) * 0.7f, 0, float(std::sin(double(a.heading))) * 0.7f};
                        if (!e.world->ray(O, {F.x * 8.0f, 0, F.z * 8.0f}).hit &&
                            e.world->ray({O.x + F.x * 8.0f, O.y, O.z + F.z * 8.0f}, {0, -3.5f, 0}).hit) kind = 2;
                    }
                }
                a.i(0x7a4) = kind;
                if (kind == 0) { a.action() = 0; sub = 0; go_hop = false; }
                else {
                    a.action() = 2;
                    if (ai::request(S, 3, false, e)) out.section = 3;
                    a.dur() = int(r15(e) % 10u) + 30;
                }
            }
            if (go_hop && e.aso_marker == 1 && grounded && S == 3 && A) {
                if (a.i(0x7a4) == 1) {
                    const Vec3 D = a.v(0x7f0);
                    A->mover.k1.x += D.x; A->mover.k1.y += D.y; A->mover.k1.z += D.z;
                    jump(0.17f);
                    a.cool() = int(r15(e) % 10u) + 30;
                } else if (a.i(0x7a4) == 2) {
                    A->mover.k1.x += std::cos(a.heading) * 0.7f; A->mover.k1.z += std::sin(a.heading) * 0.7f;
                    jump(0.17f);
                    a.cool() = int(r15(e) % 10u) + 30;
                }
            }
            if (grounded && S != 3 && a.cool() == 0) { a.action() = 0; sub = 0; }
            break;
        }
        case 100:
            if (S != 0x13a && grounded) { a.action() = 0; sub = 0; }
            break;
        default:
            break;
    }
    // POST: between the two stings.
    if (S == 7) {
        a.action() = 1;
        if (e.aso_marker == 1) {
            a.action() = 2;
            if (d >= 1 && dist >= 1.875f) {
                ai::turn(a, a.f(0x814));
                const float l = std::sqrt(to.x * to.x + to.y * to.y + to.z * to.z);
                if (A && l > 0) { A->mover.k1.x += to.x / l * 0.05f; A->mover.k1.z += to.z / l * 0.05f; }
            }
        }
    }
    // The jump helper's section-0x13a launch: K 0.1, push +0x7e0 (no fallback). The engine
    // also launches from deep water (+0x38a with t < 0) — [not modelled] (no water set).
    if (A && S == 0x13a && grounded && e.aso_marker == 1) {
        launch(*A, a.f(0x7dc) * 0.1f, out);
        push1(*A, a.heading, a.f(0x7e0));
    }
    return out;
}

}  // namespace cleria::brains
