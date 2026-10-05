// M600 Titamonus — brain FUN_004a8ea0 (handler FUN_004a9ec0 = M110's, its Durable overrides
// also forcing section 312; AI index 53). Spec: build/ai_spec/M600.md — M110 Genoid's brain
// (m110.cpp) with changed constants, a rewritten attack roll and a new attack, section 312.
//
// A giant. Beyond 6 u it runs; within, it walks (stands while cooling down). Off cooldown,
// grounded, facing within 45 deg: within 4.5 u 4% a tick (20% group-tagged) a slam (section 7,
// or 8 once enraged); else within 5.5 u 2% (10%) the leap (section 312): an immediate VM step,
// then the brain holds the script at each marker — Marker 1 launch (0.45 if its own jump did
// not), Marker 2 at the apex one big turn at the target, Marker 3 the dive (a run along the
// heading, the shock effect EF2 193 every third tick) until it lands, a 0.2 hop, then Marker 4
// a backwards run until grounded. Cooldowns R%60 - max(0, 15d) + 90 (60 tagged), in 312
// R%60 - max(0, 30d) + 135 (90). Below half HP it enrages once (section 313: STR x1.1, grows
// x1.30 with DEF + 20 while growing). Its base speed is restored every tick.
// [not modelled] the shock effect's particle hits (power 5, knock 10, reaction 2 —
// EFFECTHIT.md) and its rotation / scale; the keep-out box restore (FUN_0042dc90) and the 312
// block's `inside` (the box test; always false here).
#include <algorithm>
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m600(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    Actor* A = e.actor;
    const bool tag = e.group_tag;
    out.durable_rule = (a.i(0x79c) != 0 || tag) ? 1 : 2;           // 2: on in 312, else off unless 7
    if (e.launched) return out;
    const int d = difficulty(e);
    const bool inside = false;                                     // [not modelled] the keep-out box test
    a.f(0x818) = a.f(0x7d8);                                       // the base-speed restore
    a.f(0x81c) = a.f(0x7ac);
    int S = e.section;
    const int m = e.aso_marker;
    const int k = std::max(0, 15 * d);
    const int base = tag ? 60 : 90;
    auto cl0 = [&] {
        const int v1 = int(r15(e) % 60u) - k + base;
        return v1 < 0 ? 0 : int(r15(e) % 60u) - k + base;
    };

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            if (tag) {
                a.f(0x818) *= 1.1f; a.f(0x81c) *= 1.1f;
                if (A) { A->max_hp *= 1.35f; A->hp = A->max_hp; A->atk *= 1.05f; }
            }
            a.i(0x79c) = 0;
            a.f(0x7ac) = a.f(0x81c);
            if (A) {
                a.f(0x7b0) = A->scale; a.f(0x7b4) = A->radius; a.f(0x7cc) = A->height;
                a.f(0x7c8) = A->def;
            }
            a.f(0x7d8) = a.f(0x818);                               // the base-speed snapshot
            a.i(0x70c) = 0;
            break;
        case 1:
            ai::idle(a, p, e);
            break;
        case 2: {
            if (a.dist() > 20.0f) ai::disengage(a);               // the tick continues
            if (S == 7 || S == 8) {                                // ATTACKING
                const int c = cl0();
                if (std::uint32_t(a.cool()) <= std::uint32_t(c)) a.cool() = cl0();
                a.action() = 1;
                if (m == 0) {
                    a.action() = 2;
                    ai::steer(a, e);
                    ai::turn(a, a.f(0x814));
                }
                break;
            }
            if (a.dist() > 6.0f) a.action() = 0x800;
            else {
                a.action() = a.cool() != 0 ? 2 : 0x10;
                if (S == 1 && !e.grounded) a.action() = 0x800;
            }
            if (a.cool() == 0 && e.grounded) {
                const Vec3 to = a.v(0x734);
                const float ang = float(std::atan2(double(to.z), double(to.x)));
                if (float(std::fabs(ai::angle_diff(a.heading, ang))) < 0.7853982f) {
                    bool hit = false;
                    if (a.dist() < 4.5f && int(r15(e) % 1000u) < (tag ? 200 : 40)) {
                        out.section = a.i(0x79c) == 0 ? 7 : 8;     // gated; result ignored
                        out.attack = true;
                        hit = true;
                    }
                    if (!hit && a.dist() < 5.5f && int(r15(e) % 1000u) < (tag ? 100 : 20)) {
                        out.section = 0x138; out.attack = true;    // gated
                        out.vm_step = true;                        // FUN_004472e0 at once
                        out.set_marker_block = 1;                  // blk = 1: hold at Marker(1)
                        if (ai::request(S, 0x138, false, e)) S = 0x138;
                    }
                }
            }
            if (a.i(0x728) == 100 && (S != 0x13a || std::uint32_t(m) >= 2u) && e.grounded) a.i(0x728) = 0;
            break;
        }
        default:
            break;
    }

    // TAIL. The section-312 block (every phase).
    if (S == 0x138) {
        // On the request tick the immediate VM step has already run the section's first burst:
        // the live marker is 0 (the pc-0 reset), not the old section's.
        const int mm = out.vm_step ? 0 : m;
        const int k2 = std::max(0, 30 * d);
        const int base2 = tag ? 90 : 135;
        auto cl2 = [&] {
            const int v1 = int(r15(e) % 60u) - k2 + base2;
            return v1 < 0 ? 0 : int(r15(e) % 60u) - k2 + base2;
        };
        const int c = cl2();
        if (std::uint32_t(a.cool()) <= std::uint32_t(c)) a.cool() = cl2();
        a.steer() = 1;
        a.action() = 1;
        const int blk = out.set_marker_block > -1000 ? out.set_marker_block : e.aso_marker_block;
        auto set_blk = [&](int v) { out.set_marker_block = v; };
        auto retreat = [&] {
            a.f(0x818) = -a.f(0x7d8);                              // a NEGATIVE speed: backwards
            a.action() = 0x800;
            if (e.grounded) set_blk(-1);
            if (A) A->mover.loco = -1;                             // +0x3a0 = -1
        };
        if (mm == 1) {
            if (blk == 1 && A) {
                if (0.3f > A->vert.t) { A->vert.t = 0.45f; A->pos.y += 0.45f; A->vert.grounded = false; }
                out.set_own_jump = 1;
                set_blk(2);
            }
        } else if (mm == 2 && blk == 2) {
            if ((A && 0.0f > A->vert.t) || inside) {
                set_blk(3);
                a.steer() = 0; ai::steer(a, e); a.steer() = 1;     // steer mode 0's vector
                ai::turn(a, float(std::max(d, 1)) * (a.f(0x814) * 20.0f));
            }
        } else if ((mm == 2 || mm == 3) && blk == 3) {             // the dive
            a.action() = 0x800;
            if (std::uint32_t(a.i(0x87c)) % 3u == 0) {
                AiEffect fx;
                fx.id = 193;                                       // pc3_S_A_tame (the shockwave trail)
                fx.pos = {e.self.x + 0.0f, e.self.y - 1.0f, e.self.z + 0.0f};
                fx.scale = 1.5f;
                out.effects.push_back(fx);
            }
            if (e.grounded || (A && A->floor_y + 2.0f > e.self.y) || inside) {
                set_blk(4);
                if (e.grounded && A) { A->vert.t = 0.2f; A->pos.y += 0.2f; A->vert.grounded = false; out.set_own_jump = 1; }
            }
        } else if (mm == 3) {
            retreat();
        } else if (std::uint32_t(mm) > 2u) {
            retreat();
        }
    }
    // E: enrage, once.
    int S2 = S;
    if (a.i(0x79c) == 0 && e.max_hp * 0.5f > e.hp && (a.phase() == 1 || a.phase() == 2) &&
        (S == 2 || S == 0 || S == 1)) {
        out.section = 0x139; out.section_force = true; out.attack = false;
        S2 = 0x139;
        out.set_marker_block = -1;
        a.i(0x79c) = 1;
        a.f(0x818) *= 1.3f;                                        // transient: restored next tick
        if (A) A->atk *= 1.1f;
    }
    if (A) A->def = a.f(0x7c8);
    if (S2 == 0x139 && A) {                                        // F: growth
        a.action() = 1;
        A->def = a.f(0x7c8) + 20.0f;
        out.set_marker_block = -1;
        const float mf = float(double(std::uint32_t(S2 == S ? m : 0)));
        const float s = mf * 0.01f + 1.0f;
        A->scale = a.f(0x7b0) * s;
        const float old = A->radius;
        A->radius = a.f(0x7b4) * s;
        const float dl = A->radius - old;
        const float oy = A->pos.y + A->height;
        A->height = a.f(0x7cc) + dl;
        A->pos.y = oy - A->height;
        std::uint32_t col = 0xffffffffu;
        if (std::uint32_t(S2 == S ? m : 0) >= 30u) {
            const int v = std::max(0, 255 - int(mf * 3.0f));
            col = 0xff000000u | std::uint32_t(v) << 16 | std::uint32_t(v) << 8 |
                  std::uint32_t(255 - ((255 - v) >> 2));
        }
        A->tint = col;
        a.i(0x79c) = 2;
    }
    // G: section 314 (as M110: walk x2; launch K 0.1, no push).
    if (S == 0x13a) {
        a.f(0x81c) = a.f(0x7ac) + a.f(0x7ac);
        if (A && e.grounded && m == 1) launch(*A, a.f(0x7dc) * 0.1f, out);
    }
    out.durable_rule = (a.i(0x79c) != 0 || tag) ? 1 : 2;
    return out;
}

}  // namespace cleria::brains
