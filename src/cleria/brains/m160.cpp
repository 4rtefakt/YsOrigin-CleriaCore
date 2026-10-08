// M160 Xomack — brain FUN_004b9120 (handler FUN_004baba0 = M100's minus the arena clamp, AI index 9).
// Spec: build/ai_spec/M160.md.
//
// A runner that re-issues a 1-tick run at the target. Off cooldown, grounded, level (within 2 u of the
// target's origin or floor) and facing it within 45 deg: 2% a tick inside 7.75 u the drill shot (section 7),
// 11% inside 4.75 the charge (section 8: Dash 1.4 + the hit volume); at each section's Marker 1 it may replay
// it, up to max(d + 2, 1) times at 60..90%, snapping its heading to the target and consuming the marker. It
// dodges: a backstep hop when the target is not turned away inside 3.5 (12% + 6%d a tick), a sidestep hop while
// the player is in sections 0x100..0x108 facing it, at most 3 hops between attacks. On a hit reaction 1 in
// (10 - 2d) it shrugs it off (section 2 forced, 10 ticks of Durable); on Hard+ the charge is Durable. A charge
// sliding toward no floor is braked to 20% a tick. The brain clears +0x3bc & 4 before the action player, so a
// hit never wakes an unaware Xomack (AiOut::consume_hit).
// [not modelled] the keep-out box restore (FUN_0042dc90). [approx] the sidestep's unreachable "stale" side
// (+0x7a4 outside 1/10/11) takes the left side.
#include <algorithm>
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m160(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // 0x4b9231: the tail does not run
    Actor* A = e.actor;
    const Vec3 v{a.f(0x734), 0.0f, a.f(0x73c)};
    const int Psec = e.target_section;
    const float pd = std::sqrt(v.x * v.x + 0.0f + v.z * v.z);
    const float Th = e.target_heading;
    bool can_atk = true;
    if (std::fabs(double(e.target.y - e.self.y)) >= 2.0 && A && std::fabs(double(e.target_floor - A->floor_y)) >= 2.0)
        can_atk = false;                                           // 0x6bd580
    const int d = difficulty(e);
    const int bb5c = e.diff_index;                                 // DAT_0076bb5c (the host's 0 / 1 / 2)
    int S = e.section;
    int marker = e.aso_marker;
    auto ray_hit = [&](const Vec3& o, const Vec3& dl) { return e.world && e.world->ray(o, dl).hit; };
    // LEDGE BRAKE (0x4b9380), every phase
    if (S == 8 && A && (std::fabs(A->mover.k1.x) > 0.02f || std::fabs(A->mover.k1.z) > 0.02f)) {
        const float l = std::sqrt(A->mover.k1.x * A->mover.k1.x + A->mover.k1.z * A->mover.k1.z);
        const float nx = l == 0.0f ? 0.0f : A->mover.k1.x / l, nz = l == 0.0f ? 0.0f : A->mover.k1.z / l;
        const Vec3 O{e.self.x + 1.5f * nx, e.self.y + 1.5f * 0.0f, e.self.z + 1.5f * nz};
        if (!ray_hit(O, {0.0f, -a.f(0x828), 0.0f})) { A->mover.k1.x *= 0.2f; A->mover.k1.y *= 0.2f; A->mover.k1.z *= 0.2f; }
    }
    const int dur_k = 88 + (bb5c <= -1 ? 240 : bb5c == 0 ? 120 : 0);
    const float a_to = float(std::atan2(double(v.z), double(v.x)));

    switch (a.phase()) {
        case 0:                                                    // INIT (0x4ba99c): only 8 floats copied
            ai::init(a, p, e);
            for (int off = 0x834; off <= 0x850; off += 4) a.f(off) = 0.0f;
            a.f(0x7b0) = a.f(0x818); a.f(0x7b4) = a.heading; a.f(0x7b8) = a.f(0x814);
            break;
        case 1:
            ai::idle(a, p, e);
            break;
        case 2: {
            // A. top
            if (a.dist() > 20.0f) { a.i(0x7a8) = 0; ai::disengage(a); }
            if (bb5c >= 1 && S == 8) out.set_durable = 1;
            if (a.action() == 0) {
                if (a.i(0x728) != 100) {
                    switch (a.i(0x7bc)) {
                        case 1: a.action() = 2; a.i(0x728) = 1; a.i(0x72c) = 1; break;
                        case 2: a.action() = 2; a.i(0x728) = 2; a.i(0x72c) = 1; break;
                        default: a.action() = 0x400; a.i(0x728) = 0; break;
                    }
                }
                a.i(0x7bc) = 0;
            }
            // B. combo
            if (S != 7 && S != 8) a.i(0x7c0) = 0;
            else if (marker == 1) {
                a.i(0x7c0) += 1;
                marker = 0; out.set_marker = 0;                    // consumed
                const int lim = std::max(d + 2, 1);
                if (std::uint32_t(a.i(0x7c0)) < std::uint32_t(lim)) {
                    const std::uint32_t r = r15(e) % 1000u;
                    if (r < std::uint32_t((d - (a.i(0x7c0) - 1) + 6) * 100)) {
                        out.section = S; out.attack = true;        // gated: the same section again
                        a.action() = (S == 7 && bb5c >= 1) ? 2 : 1;
                        a.dur() = dur_k;
                        const int r2 = int(r15(e) % 20u);
                        a.cool() = (120 - 35 * d) * a.i(0x7c0) + r2 + (S == 7 ? 100 : 0) + a.dur();
                        a.i(0x7a8) = 0;
                        a.set_v(0x748, a.ncrumb() != 0 ? a.v(0x758) - e.self : a.v(0x734));
                        a.heading = rotate_toward(a.heading, float(std::atan2(double(a.f(0x750)), double(a.f(0x748)))),
                                                  a.f(0x814) * 20.0f);   // a snap
                    }
                }
            }
            bool tail13a_in = false;
            switch (a.i(0x728)) {
                case 0: {                                          // DECIDE (0x4b98a2)
                    int c = a.cool();
                    if (2.75f > a.dist()) { c = c > 1 ? c - 2 : 0; a.cool() = c; }   // 0x6bd654
                    if (c == 0 && e.grounded && can_atk &&
                        float(std::fabs(ai::angle_diff(a.heading, a_to))) < 0.785398f) {
                        if (7.75f > a.dist() && S != 7 && r15(e) % 1000u < 20u) {
                            int s7 = S;
                            if (ai::request(s7, 7, false, e)) S = 7;   // an accepted 7 is current at once
                            out.section = 7; out.attack = true;
                            a.action() = 1 + (bb5c >= 1 ? 1 : 0);
                            a.dur() = dur_k;
                            a.cool() = a.dur() + 100 + int(r15(e) % 20u);
                            a.i(0x7a8) = 0;
                        }
                        if (4.75f > a.dist() && S != 8 && r15(e) % 1000u < 110u) {
                            out.section = 8; out.attack = true;
                            a.action() = 1;
                            a.dur() = dur_k;
                            a.cool() = a.dur() + int(r15(e) % 20u);
                            a.i(0x7a8) = 0;
                        }
                    }
                    bool pending = false;
                    if (float(std::fabs(ai::angle_diff(Th, a_to))) > 0.785398f && 3.5f > pd) {   // backstep
                        const std::uint32_t r = r15(e) % 1000u;
                        if ((float(d) * 0.5f + 1.0f) * 120.0f > float(r) && S != 7 && S != 8 &&
                            std::uint32_t(a.i(0x7a8)) <= 2u) {
                            a.action() = 0; a.i(0x7bc) = 1; pending = true;
                        }
                    }
                    if (!pending && std::uint32_t(Psec - 0x100) <= 8u &&                         // sidestep
                        float(std::fabs(ai::angle_diff(Th, a_to))) > 2.0944f &&
                        float(std::fabs(ai::angle_diff(a.heading, a_to))) < 1.0472f &&
                        8.0f > pd && pd > 2.5f && std::uint32_t(a.i(0x7a8)) <= 2u) {
                        a.action() = 0; a.i(0x7bc) = 2;
                    }
                    break;
                }
                case 1: {                                          // BACKSTEP (0x4b9dad)
                    if (a.i(0x72c) == 1) {
                        a.i(0x72c) = 0;
                        Vec3 B{-v.x, -0.0f, -v.z};
                        const float bl = std::sqrt(B.x * B.x + B.y * B.y + B.z * B.z);
                        B = bl == 0.0f ? Vec3{} : Vec3{B.x / bl, B.y / bl, B.z / bl};
                        const float ang = unit(e.rng()) - 0.5f;    // RAW: [-0.5, 0.5) rad
                        const float bx = B.x * 0.6f, by = B.y * 0.6f, bz = B.z * 0.6f;   // 0x6bd334
                        const float c = float(std::cos(double(ang))), s = float(std::sin(double(ang)));
                        const Vec3 V{s * bz + c * bx, by, c * bz - s * bx};
                        a.set_v(0x7f0, V);
                        const Vec3 O{e.self.x, e.self.y + 1.0f, e.self.z};
                        if (!ray_hit(O, {3.0f * V.x, 3.0f * V.y, 3.0f * V.z}) &&
                            ray_hit({e.self.x + 9 * V.x, e.self.y + 1 + 9 * V.y, e.self.z + 9 * V.z}, {0, -3.5f, 0})) {
                            a.i(0x728) = 1; a.action() = 2;
                            if (out.section < 0) out.section = 3;  // gated hop section
                            a.dur() = int(r15(e) % 30u) + 30;
                            a.i(0x7a8) += 1;
                        } else { a.i(0x79c) = 0; a.action() = 0; a.i(0x728) = 0; }
                    }
                    if (marker == 1) {
                        if (!e.grounded) break;
                        if (S == 3 && A) {                         // launch at the hop's Marker 1
                            A->mover.k1.x += a.f(0x7f0); A->mover.k1.y += a.f(0x7f4); A->mover.k1.z += a.f(0x7f8);
                            jump(*A, 0.17f, e.jump_v0, out);       // 0x6bd1ec
                            a.cool() = int(r15(e) % 30u) + 30;
                        }
                    }
                    if (e.grounded && S != 3 && a.cool() == 0) { a.i(0x79c) = 0; a.action() = 0; a.i(0x728) = 0; }
                    break;
                }
                case 2: {                                          // SIDESTEP (0x4ba21c)
                    const float h = a.heading;
                    if (a.i(0x72c) == 1) {
                        a.i(0x72c) = 0; a.i(0x7a4) = 0;
                        const Vec3 L{0.4f * float(std::cos(double(h + 1.5f))), 0, 0.4f * float(std::sin(double(h + 1.5f)))};
                        const Vec3 R{0.4f * float(std::cos(double(h - 1.5f))), 0, 0.4f * float(std::sin(double(h - 1.5f)))};
                        const Vec3 Lf{e.self.x + 12 * L.x, e.self.y, e.self.z + 12 * L.z};
                        if (!ray_hit(e.self, {5 * L.x, 0, 5 * L.z}) && ray_hit(Lf, {0, -3.5f, 0})) a.i(0x7a4) += 1;
                        if (!ray_hit(e.self, {5 * R.x, 0, 5 * R.z}) && ray_hit(Lf, {0, -3.5f, 0})) a.i(0x7a4) += 10;   // LEFT floor (literal)
                        if (a.i(0x7a4) != 0) {
                            a.i(0x7a0) = int((e.rng() >> 17) & 1u);
                            if (out.section < 0) out.section = 3;  // gated
                            a.dur() = int(r15(e) % 30u) + 30;
                            a.i(0x7a8) += 1;
                        } else { a.i(0x79c) = 0; a.action() = 0; a.i(0x728) = 0; }
                    }
                    if (marker == 1 && e.grounded && S == 3 && A) {
                        const int m = a.i(0x7a4);
                        const float side = m == 1 ? h + 1.5f : m == 10 ? h - 1.5f
                                         : m == 11 ? (a.i(0x7a0) == 0 ? h - 1.5f : h + 1.5f) : h + 1.5f;   // [approx] stale
                        A->mover.k1.x += 0.4f * float(std::cos(double(side)));
                        A->mover.k1.z += 0.4f * float(std::sin(double(side)));
                        jump(*A, 0.17f, e.jump_v0, out);
                        a.cool() = int(r15(e) % 30u) + 30;
                    }
                    break;
                }
                case 100:                                          // the jump helper's state
                    if (S == 0x13a) { tail13a_in = true; break; }
                    if (e.grounded) { a.action() = 0x400; a.i(0x728) = 1; }   // 1, literal (§3.6)
                    break;
                default: break;
            }
            // TAIL_13A (0x4ba843)
            if (S == 0x13a || tail13a_in) {
                if (marker == 0) { a.f(0x818) = 0.0f; a.f(0x814) = a.f(0x7b8); a.f(0x7b4) = a.heading; }
                else { a.f(0x818) = a.f(0x7b0); a.f(0x814) = 0.0f; a.heading = a.f(0x7b4); }
                if (e.grounded && marker == 1 && A) {              // K 0.1, the push +0x7e0
                    launch(*A, a.f(0x7dc) * 0.1f, out);
                    push1(*A, a.heading, a.f(0x7e0));
                }
            } else {
                a.f(0x818) = a.f(0x7b0); a.f(0x7b4) = a.heading; a.f(0x814) = a.f(0x7b8);
            }
            break;
        }
        default: break;
    }

    // TAIL (0x4ba9fc), every phase
    if (e.was_hit) {
        out.consume_hit = true;                                    // +0x3bc &= ~4 before the action player
        if (S == 4 && r15(e) % std::uint32_t((5 - d) * 2) == 0u) {
            out.section = 2; out.section_force = true;             // inlined FUN_00446620(2)
            a.i(0x7c4) = 10;
        }
    }
    if (a.i(0x7c4) != 0) { a.i(0x7c4) -= 1; out.set_durable = 1; return out; }
    if ((S == 2 || S == 0 || S == 1) && e.durable) out.set_durable = 0;
    return out;
}

}  // namespace cleria::brains
