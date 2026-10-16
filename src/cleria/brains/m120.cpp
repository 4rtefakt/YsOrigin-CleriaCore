// M120 Jestona — brain FUN_00483200 (handler FUN_00484d60 = M100's minus the arena clamp,
// AI index 5). Spec: build/ai_spec/M120.md.
//
// A swordsman. It runs at the player (forcing the run section ungated). Grounded and in
// reach, 17% a tick (43.6% above Normal) it may attack: above Normal, between 3.5 and 7.2 u,
// 64% for a leaping slash (section 7, hold 60, cooldown 110, a forward push dist/14 and a
// 0.21 hop); within 3.5 u facing within 60 deg, a slash (hold 60, 75 on Easy, cooldown hold
// + 15..44); close but not facing, a backstep request. After an attack, if within 5.5 u, it
// hops back 0.4 (+/-0.5 rad; wall within 2 behind -> forward 0.8*k instead; vy 0.17/0.25).
// When the player swings at it face to face within 8 u it sidesteps (always above Normal,
// 10% otherwise): a 0.4*k hop at heading +/-1.5 rad. k = 1.0 above Normal, else 0.8.
// The jump helper's jumps: cases 100 / 0x65 (the double jump re-forces section 314 after
// 30 ticks for a mid-air second kick) and the Marker-1 launch.
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m120(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;
    Actor* A = e.actor;
    const Vec3 to = a.v(0x734);
    const float D = std::sqrt(to.x * to.x + 0.0f + to.z * to.z);
    const int Psec = e.target_section;
    const float Thead = e.target_heading;
    const bool reach = !(A && std::fabs(e.target_floor - A->floor_y) >= 3.0f);
    const float k = e.diff_index >= 1 ? 1.0f : 0.8f;
    const bool hardp = e.diff_index >= 1;
    auto a_T = [&] { return float(std::atan2(double(to.z), double(to.x))); };
    auto adiff = [&](float h) { return float(std::fabs(ai::angle_diff(h, a_T()))); };
    auto hop = [&](float v) { if (A) jump(*A, v, e.jump_v0, out); };
    auto ray = [&](const Vec3& o, const Vec3& dl) { return e.world && e.world->ray(o, dl).hit; };

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            for (int off = 0x834; off <= 0x850; off += 4) a.f(off) = 0;   // only 32 bytes copied
            if (e.diff_index >= 2) { a.f(0x818) *= 1.1f; a.f(0x814) *= 1.05f; }
            a.f(0x7ac) = a.f(0x814);
            return out;
        case 1:
            a.f(0x814) = a.f(0x7ac);
            ai::idle(a, p, e);
            return out;
        case 2: break;
        default: return out;
    }
    const float dist = a.dist();
    // POST (0x4848a8), every phase-2 tick: the jump helper's launch at section 314's Marker 1
    // (K 0.1 through FUN_004773f0, push +0x7e0, no fallback), and the double jump's mid-air
    // second kick 30 ticks into sub-state 0x65 (+2v, effect 0x6d).
    auto post = [&]() -> AiOut {
        if (e.section != 0x13a || !A) return out;
        if (e.grounded && e.aso_marker == 1) {
            const float v = a.f(0x7dc) * 0.1f;
            A->vert.t = v;
            hop(v);                                                // FUN_004773f0(v)
            A->vert.grounded = false; out.set_own_jump = 1;
            push1(*A, a.heading, a.f(0x7e0));
            return out;
        }
        if (a.i(0x728) == 0x65 && a.i(0x730) == 30) {
            const float v = a.f(0x7dc) * 0.1f;
            A->vert.t = v; A->pos.y += v + v;
            A->vert.grounded = false; out.set_own_jump = 1;
            push1(*A, a.heading, a.f(0x7e0));
            a.i(0x730) += 1;
            out.effects.push_back({0x6d, {e.self.x, e.self.y, e.self.z}, 1.0f, false});
        }
        return out;
    };
    int S = e.section;
    if (dist > 20.0f) ai::disengage(a);
    a.i(0x79c) += 1;
    int& sub = a.i(0x728);
    if (a.action() == 0) {                                         // the pick
        if (sub != 100 && sub != 0x65) {
            const int req = a.i(0x7bc);
            if (req == 3) a.i(0x79c) = 1;
            else if (req == 2) { a.action() = 2; sub = 2; a.i(0x79c) = 1; }
            else if (req == 1 && 5.5f > D) { a.action() = 2; sub = 1; a.i(0x79c) = 1; }
            else { a.action() = 0x400; sub = 0; }
        }
        a.i(0x7bc) = 0;
    }
    a.f(0x814) = a.f(0x7ac);
    if (sub == 0) {                                                // approach / attack
        bool react = false, close_back = false;
        if (2.25f > dist) a.f(0x814) = a.f(0x7ac) * 1.25f;
        if (a.action() == 0x400) {
            if (!e.grounded) react = true;
            else if (a.mode() != 0 && S != 1) { out.section = 1; out.section_force = true; S = 1; }
        }
        if (!react && (!e.grounded || !reach || S == 7 || S == 8)) react = true;
        if (!react) {
            bool ok = false;
            if (hardp && r15(e) % 1000u < 320u) ok = true;
            else if (r15(e) % 1000u < 170u) ok = true;
            if (!ok) react = true;
            else if (a.cool() != 0) close_back = true;
            else {
                bool leapt = false;
                if (hardp && dist > 3.5f && 7.2f > dist && r15(e) % 1000u < 640u && 0.7853982f > adiff(a.heading)) {
                    ai::turn(a, a.f(0x814) * 5.5f);
                    out.section = 7; out.attack = true;
                    a.action() = 1; a.dur() = 60; a.cool() = 110; a.i(0x7bc) = 1;
                    const float s = D / 14.0f;
                    if (A) { A->mover.k1.x += std::cos(a.heading) * s; A->mover.k1.z += std::sin(a.heading) * s; }
                    hop(0.21f);
                    leapt = true;
                    close_back = true;
                }
                if (!leapt) {
                    if (3.5f > dist) {
                        const float ed = adiff(a.heading);
                        if (1.0471976f > ed || 0.5235988f > ed) {
                            out.section = 7; out.attack = true;
                            a.action() = 1;
                            a.dur() = e.diff_easy ? 75 : 60;
                            a.cool() = a.dur() + 15 + int(r15(e) % 30u);
                            a.i(0x7bc) = 1;
                            ai::turn(a, a.f(0x814) * 3.0f);
                            react = true;
                        } else close_back = true;
                    } else react = true;
                }
            }
        }
        if (close_back && 3.5f > dist) { sub = 1; a.i(0x7bc) = 3; }
        // REACT: sidestep when the player swings at it face to face.
        if (std::uint32_t(Psec - 0x100) <= 8u && adiff(Thead) > 2.0943952f && 1.0471976f > adiff(a.heading) &&
            8.0f > D && S != 7 && S != 8) {
            if (hardp || r15(e) % 1000u < 100u) { a.action() = 0; a.i(0x7bc) = 2; }
        }
        return post();
    }
    auto end12 = [&] {
        if (std::uint32_t(a.i(0x79c)) > 10u && e.grounded && S != 3 && a.cool() == 0) {
            a.i(0x79c) = 0; a.action() = 0; sub = 0;
        }
    };
    if (sub == 1) {                                                // backstep
        if (a.i(0x79c) == 1) {
            a.i(0x7a0) = 0;
            float nx = -to.x, nz = -to.z;
            const float l = std::sqrt(nx * nx + nz * nz);
            if (l > 0) { nx = nx / l * 0.4f; nz = nz / l * 0.4f; } else { nx = nz = 0; }
            const float f = unit(e.rng()) - 0.5f;                  // RAW word
            const float c = float(std::cos(double(f))), s = float(std::sin(double(f)));
            const Vec3 r{c * nx + s * nz, 0, c * nz - s * nx};
            a.set_v(0x7f0, r);
            const Vec3 O{e.self.x, e.self.y + 1.0f, e.self.z};
            if (!ray(O, {r.x * 5.0f, 0, r.z * 5.0f})) {
                if (ray({O.x + r.x * 12.0f, O.y, O.z + r.z * 12.0f}, {0, -3.5f, 0})) {
                    a.i(0x7a0) = 1;
                    if (S == 7) { out.section = 3; out.section_force = true; }   // forced over the slash
                    else if (ai::request(S, 3, false, e)) out.section = 3;
                    a.dur() = int(r15(e) % 30u) + 30;
                } else { a.i(0x79c) = 0; sub = 0; }
            } else {
                const Vec3 F{float(std::cos(double(a.heading))) * 0.8f * 7.0f, 0, float(std::sin(double(a.heading))) * 0.8f * 7.0f};
                if (ray(O, F)) { a.i(0x79c) = 0; a.action() = 0; sub = 0; }
                else if (ray({O.x + F.x, O.y, O.z + F.z}, {0, -3.5f, 0})) {
                    a.i(0x7a0) = 2;
                    if (ai::request(S, 3, false, e)) out.section = 3;
                    a.dur() = int(r15(e) % 30u) + 30;
                } else { a.i(0x79c) = 0; sub = 0; }
            }
        }
        if (e.aso_marker == 1 && e.grounded && S == 3 && A) {
            if (a.i(0x7a0) == 1) {
                const Vec3 r = a.v(0x7f0);
                A->mover.k1.x += r.x * k; A->mover.k1.y += r.y * k; A->mover.k1.z += r.z * k;
                hop(0.17f);
                a.cool() = int(r15(e) % 30u) + 30;
            } else if (a.i(0x7a0) == 2) {
                A->mover.k1.x += std::cos(a.heading) * 0.8f * k; A->mover.k1.z += std::sin(a.heading) * 0.8f * k;
                hop(0.25f);
                a.cool() = int(r15(e) % 30u) + 30;
            }
        }
        end12();
        return post();
    }
    if (sub == 0x65) {                                             // the double-jump timer
        if (a.i(0x730) == 30) { out.section = 0x13a; out.section_force = true; }   // forced restart
        else a.i(0x730) += 1;
    }
    if (sub == 100 || sub == 0x65) {                               // the jump wait
        if (!(S == 0x13a && std::uint32_t(e.aso_marker) < 2u) && e.grounded) { a.action() = 0x400; sub = 0; }
        return post();
    }
    if (sub == 2) {                                                // sidestep
        if (a.i(0x79c) == 1) {
            a.i(0x7a4) = 0;
            const Vec3 O = e.self;
            const float a1 = a.heading + 1.5f, a2 = a.heading - 1.5f;
            const Vec3 v1{float(std::cos(double(a1))) * 0.4f * k, 0, float(std::sin(double(a1))) * 0.4f * k};
            const Vec3 v2{float(std::cos(double(a2))) * 0.4f * k, 0, float(std::sin(double(a2))) * 0.4f * k};
            const bool wall1 = ray(O, {v1.x * 5, 0, v1.z * 5});
            if (!wall1 && ray({O.x + v1.x * 12, O.y, O.z + v1.z * 12}, {0, -3.5f, 0})) a.i(0x7a4) += 1;
            if (!ray(O, {v2.x * 5, 0, v2.z * 5})) {
                if (wall1) a.i(0x7a4) += 10;                       // re-queries the side-1 wall (literal)
            }
            if (a.i(0x7a4) != 0) {
                a.i(0x7a0) = int(r15(e) & 1u);
                if (ai::request(S, 3, false, e)) out.section = 3;
                a.dur() = int(r15(e) % 20u) + 20;
            } else { a.i(0x79c) = 0; a.action() = 0; sub = 0; }
        }
        if (e.aso_marker == 1 && e.grounded && S == 3 && A) {
            const int f = a.i(0x7a4);
            const float ang = f == 1 ? a.heading + 1.5f : f == 10 ? a.heading - 1.5f
                            : (a.i(0x7a0) == 0 ? a.heading - 1.5f : a.heading + 1.5f);
            A->mover.k1.x += float(std::cos(double(ang))) * 0.4f * k;
            A->mover.k1.z += float(std::sin(double(ang))) * 0.4f * k;
            hop(0.17f);
            a.cool() = int(r15(e) % 20u) + 20;
        }
        end12();
        return post();
    }
    return post();
}

}  // namespace cleria::brains
