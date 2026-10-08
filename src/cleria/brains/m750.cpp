// M750 Liliborad — brain FUN_004bae90 (AI index 68; handler FUN_004bbc30 = M100's minus the arena clamp, plus a
// dead path that kills every effect it owns: the view, tag kOrb). Spec: build/ai_spec/M750.md.
//
// It never walks: it stands facing its target and, engaged, rolls 15 % a tick to request (gated) section 7 when
// off cooldown, the target is within 8 u and its floor between 0.5 below and 1.5 above its own, and section 2
// shows. At the section's Marker(319) it spawns EF2 319 (the orb, knock 1.5, rtype 1) 0.5 above its origin and
// flies it by hand: N-tick legs of 8.5 u (N = 25(Q + 4) / (Q + 3), Q = d < 0 ? -1 : d²) — straight ahead, then Q + 1
// at the target, then back — turning at most 0.09 (x (d + 1) 0.45 from d = 2) a tick toward the leg's end; on the
// way back it homes in within 1 u and is caught within 0.1: section 20 (gated), the orb's particles get 15 ticks,
// cooldown R%30 + 30. While the orb is out in section 7 the thrower faces it (steer 4).
// [approx] the orb object's "destroyed" finish (code 2, state 0) folds into the "gone" one (code 1): the port's
// effect scan lists only live effects; the re-find's kill of extra owned effects is not modelled (one orb at most).
// [not modelled] the keep-out box restore (FUN_0042dc90).
#include <algorithm>
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

namespace {
constexpr int kOrb = 1;                                            // the owned orb's tag (fx+0x1a8 = self)
float sq(float v) { return float(double(v) * double(v)); }         // FUN_006701e0(v, 2.0) [H pow]
float wrap1(float x) {                                             // a single step (0x6be0ac / 0x6bd6ac / 0x6bd898)
    if (-3.1415927f > x) x += 6.2831855f;
    else if (x > 3.1415927f) x -= 6.2831855f;
    return x;
}
}  // namespace

AiOut m750(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // 0x4baf9f
    Actor* A = e.actor;
    const int d = difficulty(e);
    const int Q = d < 0 ? -1 : d * d;                              // 0x4bb437
    const int N = 25 * (Q + 4) / (Q + 3);
    const int S = e.section;
    switch (a.phase()) {
        case 0:                                                    // INIT (0x4bb1bd)
            ai::init(a, p, e);
            a.i(0x79c) = 0; a.i(0x7a0) = 0; a.f(0x7ac) = 0; a.f(0x7b0) = 0; a.f(0x7b4) = 0;
            a.set_v(0x7e4, e.self);
            break;
        case 1:                                                    // UNAWARE (0x4bb19a)
            if (a.i(0x79c) == 0) { a.i(0x70c) = 1; ai::idle(a, p, e); }
            break;
        case 2: {                                                  // ENGAGED (0x4bb026)
            if (a.dist() > 20.0f && a.i(0x79c) == 0) ai::disengage(a);
            a.steer() = 0;
            const std::uint32_t r = r15(e) % 1000u;                // D1
            const float self_floor = A ? A->floor_y : e.self.y;
            if (r < 150u && e.target_floor >= self_floor - 0.5f && self_floor + 1.5f >= e.target_floor &&
                a.cool() == 0 && 8.0f > a.dist() && S == 2) {
                a.dur() = 25 * (Q + 4);
                a.cool() = a.dur() + 30 + int(r15(e) % 30u);       // D2
                out.section = 7; out.section_force = false; out.attack = true;   // gated
            }
            break;
        }
        default: break;
    }
    // COMMON (0x4bb267)
    if (S == 7) {
        if (a.i(0x79c) != 0) {                                     // face the orb
            a.set_v(0x894, a.v(0x7e4));
            a.steer() = 4;
        } else if (e.aso_marker != 0) {                            // THROW at Marker(319)
            const int m = e.aso_marker;
            out.set_marker = 0;
            a.i(0x79c) = 1; a.i(0x7a0) = 0;
            const Vec3 O{e.self.x + 0.0f, e.self.y + 0.5f, e.self.z + 0.0f};
            a.set_v(0x7e4, O);
            AiEffect fx;
            fx.id = m; fx.pos = O; fx.power = 0.0f; fx.knock = 1.5f; fx.rtype = 1; fx.tag = kOrb;
            out.effects.push_back(fx);
        }
    }
    // The re-find (0x4bb3d6): is our orb alive? (spawned this tick: it will be — the host spawns it after.)
    bool orb = !out.effects.empty();
    if (!orb && e.world && A)
        e.world->for_each_effect([&](const AiWorld::Fx& f) {
            if (f.owner == A->instance_id && f.tag == kOrb && !f.kill) orb = true;
        });
    if (a.i(0x79c) == 2) {                                         // END (0x4bbbd2)
        AiEffect r; r.remain_tag = kOrb; r.remaining = 15;         // p+0x1c4 = 15 on its particles; +0x88c = 0
        out.effects.push_back(r);
        a.i(0x79c) = 0;
        return out;
    }
    if (a.i(0x79c) != 1) return out;
    // FLY (0x4bb437)
    const std::uint32_t c = std::uint32_t(a.i(0x7a0)), q = c / std::uint32_t(N), rem = c % std::uint32_t(N);
    const bool ret = q >= std::uint32_t(Q + 2);
    int fin = 0;
    if (rem == 0) {
        if (ret) {                                                 // RETURN leg
            a.set_v(0x7fc, {e.self.x, e.self.y + 0.5f, e.self.z});
        } else if (c == 0) {                                       // LEG 0: 8.5 straight ahead
            const float h = a.heading;
            Vec3 v{float(std::cos(double(h))) * 8.5f, 0.0f, float(std::sin(double(h))) * 8.5f};
            const Vec3 O{e.self.x, e.self.y + 0.5f, e.self.z};
            a.set_v(0x7e4, O);
            a.f(0x7ac) = h;
            const float L = float(std::sqrt(double((v.x * v.x + v.y * v.y) + v.z * v.z)));   // FUN_0041f570
            a.f(0x7b0) = a.f(0x7b4) = L / float(N);
            a.set_v(0x7fc, {O.x + v.x, O.y + v.y, O.z + v.z});
        } else {                                                   // HOMING LEG
            const Vec3 P0 = a.v(0x7e4);
            Vec3 v{e.target.x - P0.x, 0.0f, e.target.z - P0.z};
            if ((v.z * v.z + v.x * v.x) > 0.001f) {               // FUN_0042cc50
                const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
                v = l == 0.0f ? Vec3{} : Vec3{v.x * (1.0f / l), 0.0f, v.z * (1.0f / l)};
            } else {                                               // the 8-way fallback (0x6bd374: 0x3f350481)
                const float k = 0.70710421f;
                if (v.x < 0) v = v.z < 0 ? Vec3{-k, 0, -k} : v.z > 0 ? Vec3{-k, 0, k} : Vec3{-1, 0, v.z};
                else if (v.x > 0) v = v.z < 0 ? Vec3{k, 0, -k} : v.z > 0 ? Vec3{k, 0, k} : Vec3{1, 0, v.z};
                else v = v.z < 0 ? Vec3{v.x, 0, -1} : v.z > 0 ? Vec3{v.x, 0, 1} : Vec3{1, 0, v.z};
            }
            a.set_v(0x7fc, {P0.x + v.x * 8.5f, P0.y + v.y * 8.5f, P0.z + v.z * 8.5f});
        }
    }
    if (c > 0) {                                                   // MOVE (0x4bb74d)
        const Vec3 P0 = a.v(0x7e4), E = a.v(0x7fc);
        const float dx = E.x - P0.x, dz = E.z - P0.z;
        float hd = a.f(0x7ac);
        float da = float(std::atan2(double(dz), double(dx))) - hd;
        const float dist = float(std::sqrt(double(sq(dx) + sq(dz))));
        const float k = 8.5f / std::max(dist, 1.0f);
        da = wrap1(da);
        const float lim = std::max(float(d + 1) * 0.45f, 1.0f) * 0.09f;   // 0x6bd2f0, 0x6bd190
        float step = da;
        if (da < 0.0f) {
            const float t = std::min(da / k, -0.002f);
            const float m = std::max(-lim, t);
            step = da > m ? da : m;
        } else if (da > 0.0f) {
            const float t = std::max(da / k, 0.002f);
            const float m = std::min(lim, t);
            step = da <= m ? da : m;
        }
        hd = hd + step; a.f(0x7ac) = hd;
        const float sp = a.f(0x7b0);
        a.f(0x7e4) += float(std::cos(double(hd))) * sp;
        a.f(0x7e8) += 0.0f;
        a.f(0x7ec) += float(std::sin(double(hd))) * sp;
        if (!orb) fin = 1;                                         // the orb is gone
        else { AiEffect mv; mv.move_tag = kOrb; mv.pos = a.v(0x7e4); out.effects.push_back(mv); }   // FUN_00451a40
    }
    if (ret) {                                                     // the return leg's catch (0x4bb9ed)
        const Vec3 P0 = a.v(0x7e4);
        const float r2 = sq(e.self.z - P0.z) + sq(e.self.x - P0.x);
        if (0.01f > r2) fin = 1;                                   // caught
        else if (1.0f > r2) {
            float s = (a.f(0x7b4) - a.f(0x7b0)) / 3.0f + a.f(0x7b0);
            a.f(0x7b0) = s;
            if (a.f(0x7b4) > s) { a.f(0x7b0) = a.f(0x7b4); s = a.f(0x7b4); }
            if (s > r2) a.f(0x7b0) = r2;
            const float da = wrap1(float(std::atan2(double(e.self.z - P0.z), double(e.self.x - P0.x))) - a.f(0x7ac));
            a.f(0x7ac) = da * 0.5f + a.f(0x7ac);
        }
    }
    a.i(0x7a0) = int(c + 1);
    if (fin != 0) {                                                // FINISH (0x4bbb4f)
        a.i(0x7a0) = 0;
        a.i(0x79c) = fin == 2 ? 0 : 2;
        out.section = 20; out.section_force = false;               // the catch (gated)
        a.cool() = int(r15(e) % 30u) + 30;                         // D3
    }
    return out;
}

}  // namespace cleria::brains
