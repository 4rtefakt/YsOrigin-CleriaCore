// M660 Urobass — brain FUN_004af610 and its handler's post-mover block (handler FUN_004afb80, AI index 59).
// Spec: build/ai_spec/M660.md.
//
// It never walks: it stands turning toward the target and decides every 15..44 / 30..59 ticks — off cooldown and
// inside 10 u, (35 + 10·max(d, 0))% the cast (section 7 forced); 45% the teleport (section 300, replacing a cast).
// The cast: at section 7's Marker 1 it forces section 8 and drops strikes at a point 50 ticks ahead of the target
// (an exponentially smoothed lead, wall-clamped, on the floor + 1): 1 below Normal, 4 (a ring of 3 at 3 u) on
// Normal / Hard, 7 (and a ring at 6 u) on Nightmare — near-stationary kind-1 carriers of EF2 494 whose trail at age
// 45 is EF2 495 (the pillar, then the ring), SE 437 at the centre the same tick. Cooldown 240 - 30d. The teleport
// fades out from section 300's Marker 1 (0.1 a tick), tries up to 5 spots a tick 1.5..6 u from the target (in
// sight of its chest, a floor within 10, room for a radius-1 body), fades back in with section 301; it is
// unhittable below alpha 0.5 and stun-immune only in section 7.
// [approx] the handler's post-mover block (FUN_004afb80 0x4afee6, run after the action player and the mover's
// ASO step, and on the culled / g_flags[131] freeze paths too) runs here at the top of the NEXT brain tick, from
// the section / marker the VM left: one tick late, and not while frozen. The strike SE countdown (0x4afc19, before
// the HP test) likewise. [not modelled] the carrier's rtype 2 (fx+0x1ec;
// effect 494 has no hit emitter).
#include <algorithm>
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m660(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    Actor* A = e.actor;
    AiWorld* w = e.world;
    const int d = difficulty(e);
    auto force = [&](int s) { out.section = s; out.section_force = true; };
    auto ray = [&](const Vec3& o, const Vec3& dl) { return w ? w->ray(o, dl) : AiWorld::Ray{}; };
    const Vec3 T = e.target;

    // ---- the handler: the strike SE countdown (0x4afc19)
    if (a.phase() != 0 && a.i(0x79c) != 0 && --a.i(0x79c) == 0 && w) w->sound_at(437, a.v(0x7fc));   // zako\z_bomb

    // ---- the handler's post-mover block (0x4afee6) [approx: a tick late]
    if (a.phase() != 0 && A) {
        A->flags12c &= ~0x80000u;
        int S = e.section;
        if (S == 7) {                                              // A. the cast
            a.i(0x72c) = 0x14; a.action() = 2;
            A->flags12c |= 0x80000u;
            if (e.aso_marker != 0) {
                force(8); S = 8;
                // B. STRIKE (0x4aff89)
                const Vec3 v = a.v(0x7f0);
                const Vec3 L{v.x, 0.0f, v.z};
                const Vec3 P{T.x + v.x, T.y, T.z + v.z};
                Vec3 Q = P;
                const AiWorld::Ray r1 = ray(T, L);
                if (r1.hit) {
                    const float ll = std::sqrt(L.x * L.x + L.z * L.z);
                    const Vec3 n = ll == 0.0f ? Vec3{} : Vec3{L.x / ll, 0.0f, L.z / ll};
                    Q = {r1.point.x - n.x, T.y, r1.point.z - n.z};
                    const AiWorld::Ray r2 = ray({P.x, T.y + 20.0f, P.z}, {0, -20.0f, 0});
                    if (r2.hit && !ray({r2.point.x, r2.point.y + 1.0f, r2.point.z}, {-v.x, 0, -v.z}).hit)
                        Q = {r2.point.x, r2.point.y + 1.0f, r2.point.z};
                }
                const AiWorld::Ray rf = ray(Q, {0, -15.0f, 0});   // 0x6be0e4
                Q.y = rf.hit ? rf.point.y + 1.0f : e.target_floor + 1.0f;
                const int n_shots = d < 0 ? 1 : d < 2 ? 4 : 7;
                int ad = 0;
                for (int i = 0; i < n_shots; ++i, ad += 120) {
                    Vec3 pos = Q;
                    if (i == 0) { a.set_v(0x7fc, Q); a.i(0x79c) = 45; }
                    else {
                        const int k = i > 3 ? 60 : 0;
                        const float R = i > 3 ? 6.0f : 3.0f;
                        const float ang = float(k + ad) * 0.0174533f;
                        pos = {Q.x + float(std::cos(double(ang))) * R, Q.y + 0.0f, Q.z + float(std::sin(double(ang))) * R};
                    }
                    AiBullet b;
                    b.kind = 1; b.effect = 494;
                    b.pos = pos; b.dir = {0, 0, -1e-05f};
                    b.speed = 1e-05f; b.accel = 0; b.gravity = 0; b.power = 0; b.knock = 1.0f;
                    b.trail_effect = 495; b.trail_period = 45; b.home_period = 0;
                    out.bullets.push_back(b);
                }
            }
        } else if (S != 8 && a.i(0x72c) == 0x14) {                 // the cast is over
            a.i(0x72c) = 0;
            a.cool() = 240 - 30 * d;
            const int b = int((e.rng() >> 17) & 1u);
            a.i(0x730) = b;
            a.dur() = b ? int(r15(e) % 30u) + 30 : int(r15(e) % 30u) + 15;
        }
        if (S == 300) {                                            // C. fade out + relocate
            if (e.aso_marker != 0) {
                a.alpha = std::max(0.0f, a.alpha - 0.1f);
                if (a.alpha == 0.0f) {                             // D. RELOCATE (0x4b066b)
                    bool moved = false;
                    for (int k = 1; k <= 5 && !moved; ++k) {
                        const std::uint32_t w1 = e.rng(), w2 = e.rng();
                        const float r = unit(w1) * 4.5f + 1.5f;
                        const float ang = k <= 2 ? (unit(w2) * 360.0f + 0.0f) * 0.0174533f
                                                 : (unit(w2) * 2.51327f - 1.25664f) + e.target_heading;
                        const Vec3 C{T.x + float(std::cos(double(ang))) * r, T.y + 0.0f, T.z + float(std::sin(double(ang))) * r};
                        const Vec3 Dv{T.x - C.x, (T.y + 0.75f) - C.y, T.z - C.z};
                        const float dl = std::sqrt(Dv.y * Dv.y + Dv.x * Dv.x + Dv.z * Dv.z);
                        const Vec3 n = dl == 0.0f ? Vec3{} : Vec3{Dv.x / dl, Dv.y / dl, Dv.z / dl};
                        if (ray({C.x + 0.5f * n.x, C.y + 0.5f * n.y, C.z + 0.5f * n.z},
                                {Dv.x + 0.5f * n.x, Dv.y + 0.5f * n.y, Dv.z + 0.5f * n.z}).hit) continue;
                        const AiWorld::Ray fl = ray(C, {0, -10.0f, 0});
                        if (!fl.hit) continue;
                        const Vec3 B{fl.point.x, fl.point.y + 1.1f, fl.point.z};   // 0x6bd464
                        if (w && w->sphere_hits(B, 1.0f)) continue;
                        A->pos = {B.x, B.y - A->height, B.z};      // the origin at floor + 1.1
                        a.i(0x72c) = 3;
                        force(0x12d);
                        moved = true;
                    }
                    if (!moved && std::uint32_t(a.i(0x87c)) > 90u) force(0x12d);   // give up in place
                }
            }
        } else if (S == 0x12d) {                                   // E. fade in
            if (e.aso_marker != 0) {
                a.alpha = std::min(a.alpha + 0.1f, 1.0f);
                if (a.alpha == 1.0f) {
                    a.i(0x72c) = 0;
                    const int b = int((e.rng() >> 17) & 1u);
                    a.i(0x730) = b;
                    a.dur() = b ? int(r15(e) % 30u) + 30 : int(r15(e) % 30u) + 15;
                    force(2);
                    A->mover.loco = -1;                            // +0x3a0
                }
            }
        } else {
            a.alpha = 1.0f;
        }
        // F. the target lead estimate (every live tick)
        const Vec3 prev = a.v(0x7e4);
        Vec3 v = a.v(0x7f0);
        v.x = (T.x - prev.x) * 50.0f * 0.05f + v.x * 0.95f;
        v.y = (T.y - prev.y) * 50.0f * 0.05f + v.y * 0.95f;
        v.z = (T.z - prev.z) * 50.0f * 0.05f + v.z * 0.95f;
        a.set_v(0x7f0, v);
        a.set_v(0x7e4, T);
        a.hittable = !(0.5f > a.alpha);                            // G
    }

    // ---- the brain FUN_004af610
    if (e.launched) return out;                                    // 0x4af718
    switch (a.phase()) {
        case 0:                                                    // INIT (0x4afaa8)
            ai::init(a, p, e);
            a.set_v(0x7f0, {}); a.set_v(0x7e4, {}); a.set_v(0x7fc, {});
            a.i(0x79c) = 0;
            return out;
        case 1:                                                    // 0x4afa73
            a.i(0x70c) = 1;
            a.i(0x72c) = 0; a.dur() = 0; a.cool() = 0;
            ai::idle(a, p, e);
            return out;
        case 2: break;
        default: return out;
    }
    if (a.dist() > 20.0f) ai::disengage(a);
    a.action() = 2; a.steer() = 0;
    if (a.i(0x72c) != 0) return out;                               // casting / teleporting
    if (a.dur() == 0) {
        const int b = int((e.rng() >> 17) & 1u);                   // DRAW 1
        a.i(0x730) = b;
        a.dur() = b ? int(r15(e) % 30u) + 30 : int(r15(e) % 30u) + 15;   // DRAW 2
        if (a.cool() == 0) {
            const int dd = std::max(d, 0);
            const int r = int(r15(e) % 1000u);                     // DRAW 3
            if (r < dd * 100 + 350 && 10.0f > a.dist()) {
                a.i(0x72c) = 0x14;
                force(7); out.attack = true;                       // the cast (ungated)
            }
            a.i(0x87c) = 0;
        }
        if (r15(e) % 1000u < 450u) {                               // DRAW 4: the teleport
            a.i(0x72c) = 0xa;
            force(300);
            a.i(0x87c) = 0;
        }
    }
    a.action() = 2;
    return out;
}

}  // namespace cleria::brains
