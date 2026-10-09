// M900 — the Eyes of Fact, Hugo's familiars in Toal's S_2080 duel (cast 0xb0, AI index 83).
// Handler FUN_004731d0, duel brain FUN_004dab30. Spec: build/ai_spec/M900.md.
//
// The handler runs FUN_004dab30 while g_flags[130] is set and the player is not Hugo; else
// FUN_00470210 (the player-Hugo familiar chain, [not modelled]). The brain also hands over to
// FUN_00470210 when the owner (+0x854) is missing, itself or not an M810. An Eye is not
// hittable (INIT: +0x60 = (+0x60 & ~0x81) | 8) and moves itself: each tick it computes an
// anchor (+0x894) for its formation mode +0x72c (written by M810's eye commands) and its slot
// +0x730, and steps half a unit toward it (snapping when close; with 6+ eyes wanted, modes
// other than 1 bend the path round the player on a Hermite curve and smooth the step):
//   0 pairs behind Hugo, 1 a ring turning with Hugo's tick counter, 2 the dash (section 271,
//   EF2 0x92, fade out, reappear in front of Hugo, die 120 ticks later or near the player),
//   3 scattered round the player, firing one 0x1b shot 40 ticks after arriving, 4 a vertical
//   ring in front of Hugo, 5 a fan round Hugo's facing, 6 two columns ahead.
// M810's shot orders set the timers: +0x7a4 shots, +0x7a8 ticks to the next (each a 0xc6
// kind-0x1b shot along its heading, turned toward Hugo's target by at most 72 deg a tick);
// +0x7a0 holds the facing after a shot and +0x7cc is the recoil. M810's recall sets +0x7c0
// = 30(d+7): the Eye carries EF2 0x95 (power 5, knock 15) and turns toward the player. An Eye
// whose slot is >= the wanted count, or that is 200 u from Hugo, or with g_flags[130] clear,
// kills itself.
// [not modelled] FUN_00470210;
// FUN_004202e0(level) (the spawn computed it); the handler's dead path (FUN_004780e0, the VM
// step) and its alpha ramp there; the sound positions (played at the actor).
#include <cmath>
#include <cstdint>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"
#include "cleria/hugo_eyes.hpp"

namespace cleria::brains {
namespace {

constexpr float kPi = 3.1415927410125732f;        // DAT_006bd6ac
constexpr float k2Pi = 6.2831854820251465f;       // DAT_006bd898
constexpr float kDeg = 0.01745329238474369f;      // DAT_006bd0dc

int gflag(const AiEnv& e, int i, int unset) { return e.world ? e.world->flag(i, unset) : unset; }

float fcos(float x) { return float(std::cos(double(x))); }   // FUN_0066fd70
float fsin(float x) { return float(std::sin(double(x))); }   // FUN_00670d30
float fatan2(float y, float x) { return float(std::atan2(double(y), double(x))); }   // FUN_0066f80a
// (float)(double)(u32) through the 0x6becd0 fix-up.
float u2f(std::uint32_t u) { return float(double(u)); }

// FUN_0042cc50: v / |v| (the sqrt through FUN_0041f570); zero stays zero.
Vec3 nrm(const Vec3& v) {
    const float l = float(std::sqrt(double(v.x * v.x + v.y * v.y + v.z * v.z)));
    if (l == 0.0f) return {};
    const float k = 1.0f / l;
    return {v.x * k, v.y * k, v.z * k};
}

// (-pi, pi]: the engine's wrap loops.
float wrap(float x) {
    while (x > kPi) x -= k2Pi;
    while (-kPi >= x) x += k2Pi;
    return x;
}

// D3DXVec3Hermite (0x67b50c) [H: the D3DX evaluation order is not read; this is the textbook form].
Vec3 hermite(const Vec3& p1, const Vec3& t1, const Vec3& p2, const Vec3& t2, float s) {
    const float h1 = 2.0f * s * s * s - 3.0f * s * s + 1.0f;
    const float h2 = s * s * s - 2.0f * s * s + s;
    const float h3 = -2.0f * s * s * s + 3.0f * s * s;
    const float h4 = s * s * s - s * s;
    return {h1 * p1.x + h2 * t1.x + h3 * p2.x + h4 * t2.x, h1 * p1.y + h2 * t1.y + h3 * p2.y + h4 * t2.y,
            h1 * p1.z + h2 * t1.z + h3 * p2.z + h4 * t2.z};
}

Vec3 origin(const Actor& o) { return {o.pos.x, o.pos.y + o.height, o.pos.z}; }
// An actor's visible facing +0x314 in the engine basis: the host keeps Actor::yaw in its view
// basis, yaw = pi/2 - facing (view_game.inl) [H].
float facing314(const Actor& o) { return 1.5707963f - o.yaw; }

}  // namespace

AiOut m900(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (!e.actor) return out;
    Actor& A = *e.actor;
    AiWorld* w = e.world;
    // The handler's alpha ramp (0x47327d), after the tick: up 0.025 a step to 1, or down to 0
    // while g_flags[211] (DAT_0076bc68) is set.
    auto ramp = [&] {
        if (gflag(e, 211, 0) == 0) {
            a.alpha += 0.02500000037252903f;
            if (a.alpha > 1.0f) a.alpha = 1.0f;
        } else {
            a.alpha -= 0.02500000037252903f;
            if (0.0f > a.alpha) a.alpha = 0.0f;
        }
    };
    // ---- the handler FUN_004731d0: the duel brain only while 130 is set and the player is not Hugo
    // 3c: FUN_00470210. The player's own eyes (owner +0x854 = the player) run hugo_eyes.cpp; an
    // M810-owned eye outside the duel stays [not modelled].
    if (gflag(e, 130, 1) == 0 || e.character == 2) {
        if (e.familiar) out = hugo::familiar(a, p, e);
        ramp();
        return out;
    }
    // ---- the brain FUN_004dab30
    if (e.launched) { ramp(); return out; }                         // +0x4d0
    Actor* Ow = (w && a.i(0x854) != 0) ? w->find(a.i(0x854)) : nullptr;
    if (!Ow || Ow == &A || Ow->cast_id != 0xa7) { ramp(); return out; }   // FUN_00470210 [not modelled]
    Actor& H = *Ow;
    A.flags12c |= 0x1002u;
    A.flags130 |= 0x100000u;
    const int d = difficulty(e);
    const float fd = float(d);
    const bool f231 = gflag(e, 231, 0) != 0;                        // DAT_0076bcb8
    const bool half = A.cast_id == 0xa7 ? A.max_hp * 0.5f > A.hp : H.max_hp * 0.5f > H.hp;
    const int want = (f231 ? 8 : 4) - (half ? 0 : 2);               // local_ac
    const float face0 = e.facing;                                   // local_bc: +0x314 on entry
    const Vec3 HO = origin(H);
    const Vec3 T = e.target;                                        // +0x888 = 0: the player
    Vec3 O = e.self;                                                // +0x50: the Eye moves itself
    float face = e.facing;                                          // +0x314
    bool faced = false;
    auto set_face = [&](float v) { face = v; a.heading = v; faced = true; };   // +0x314 = +0x308 = v
    std::uint32_t& slot = reinterpret_cast<std::uint32_t&>(a.i(0x730));
    int& mode = a.i(0x72c);
    auto kill_fx = [&] {                                            // FUN_0047a830 / FUN_0047a7c0
        if (a.i(0x88c) != 0) { AiEffect k; k.kill_tag = 0; out.effects.push_back(k); }
        a.i(0x88c) = 0;
    };
    auto die = [&] {                                                // HP 0, the "remove" bits
        A.flags12c |= 0x80000000u;
        A.flags130 |= 0x1008u;
        A.hp = 0.0f;
        out.set_hp = 0.0f;
        kill_fx();
    };
    auto shot = [&](const Vec3& dir, float speed) {                 // 0xc6, kind 0x1b, Hugo's
        AiBullet b;
        b.kind = 0x1b; b.effect = 0xc6; b.owner = H.instance_id;
        b.pos = O; b.dir = dir; b.speed = speed;
        b.power = 0.0f; b.knock = 1.0f;
        out.bullets.push_back(b);
    };

    if (a.phase() == 0) {                                           // ---- INIT (0x4dc464)
        ai::init(a, p, e);                                          // FUN_004782f0 + EI[80] +0x90..
        a.set_v(0x808, O);
        a.i(0x720) = 2;
        // FUN_004202e0(self, cast - 14, self+0xc8) [BYTES 0x4dc4dd..0x4dc4ec]; the cast operand is read through
        // [0x74c11c]+0x16060 (0x4dc4e3), 176 for the Eye [DOC DIFFICULTY.md G4, M900.md] = its own cast.
        if (w) w->restat(A, A.cast_id, A.level);
        A.atk = H.atk;                                              // +0xa4 = the owner's
        a.i(0x79c) = 0; a.i(0x7a4) = 0; a.i(0x7a8) = 0; a.i(0x7bc) = 0; a.i(0x7c0) = 0;
        a.steer() = 1; a.action() = 2;
        A.kind = (A.kind & ~0x81u) | 8u;                            // an enemy, not hittable
        a.hittable = false;
    } else {
        a.steer() = 1; a.action() = 2;
        if (a.i(0x7a0) == 0) {                                      // the facing (0x4dace9)
            if (mode == 0 || mode == 4 || mode == 1) {
                set_face(facing314(H));
            } else if (mode == 3) {
                set_face(fatan2(a.f(0x73c), a.f(0x734)));
            } else if (mode == 5) {
                set_face(fatan2(O.z - HO.z, O.x - HO.x));
            } else if (mode == 6) {
                float v = u2f((slot >> 1) + 1) * 10.0f * kDeg;
                if (slot & 1) v *= -1.0f;
                set_face(facing314(H) + v);
            } else {
                set_face(face);                                     // +0x308 = +0x314
            }
        } else {
            a.i(0x7a0) -= 1;
        }

        // ---- the anchor +0x894 per mode (jump table 0x4dce0c)
        auto anchor = [&](float x, float y, float z) { a.set_v(0x894, {HO.x + x, HO.y + y, HO.z + z}); };
        switch (mode) {
            case 0: {                                               // pairs behind (0x4dae5d)
                const float g = (slot & 1) ? face + 0.8975979089736938f : face - 0.8975979089736938f;
                const float m = u2f((slot >> 1) + 1);
                anchor(fcos(g) * -1.75f * m, 0.0f, fsin(g) * -1.75f * m);
                break;
            }
            case 1: {                                               // the turning ring (0x4daf1c)
                float r = 2.25f;
                float g = u2f(H.age_ticks) * 0.11999999731779099f * (fd * 0.20000000298023224f + 0.5f);
                int j = 0;
                for (int v = want; v > 2;) { v >>= 1; ++j; }
                for (std::uint32_t k = 1u << j; j > 0; --j, k >>= 1) {
                    if ((slot >> j) & 1u) g += kPi / float(int(k));
                    if (j != 1 && int(float(j) * 0.5f + 0.5f) == (j >> 1)) r *= 1.2999999523162842f;
                }
                if (slot & 1) g += kPi;
                g = wrap(g);
                anchor(fcos(g) * r, 0.0f, fsin(g) * r);
                break;
            }
            case 2: {                                               // the dash (0x4db0c5)
                if (a.i(0x7bc) == 0) {
                    out.section = 0x10f; out.section_force = true;  // FUN_00446620(271)
                    a.i(0x7bc) = 0x78;
                    a.set_v(0x7fc, {0, 0, 0});
                    AiEffect fx{0x92, O, 1.0f, false};
                    fx.power = 12.0f; fx.knock = 5.0f; fx.rtype = 2; fx.status = f231 ? 0x20 : 0;
                    out.effects.push_back(fx);
                    break;
                }
                if (f231 && a.i(0x79c) != 0) {                      // the rematch: it homes
                    const Vec3 n = nrm(a.v(0x734));
                    const Vec3 s{a.f(0x7fc) * 0.949999988079071f + n.x * 0.05000000074505806f,
                                 a.f(0x800) * 0.949999988079071f + n.y * 0.05000000074505806f,
                                 a.f(0x804) * 0.949999988079071f + n.z * 0.05000000074505806f};
                    a.set_v(0x7fc, s);
                    const float k = fd * 0.05000000074505806f + 0.10000000149011612f;
                    O = {s.x * k + O.x, s.y * k + O.y, s.z * k + O.z};
                }
                a.i(0x7bc) -= 1;
                if (a.i(0x79c) == 0 && a.alpha > 0.0f) {            // fade out, then in front of Hugo
                    a.alpha -= 0.25f;
                    if (!(0.0f < a.alpha)) {
                        a.alpha = 0.0f;
                        a.i(0x79c) = 1;
                        O = {fcos(H.ai.heading) + HO.x, HO.y + 0.0f, fsin(H.ai.heading) + HO.z};
                    }
                } else if (1.0f > a.alpha) {
                    a.alpha = std::fmin(1.0f, a.alpha + 0.15000000596046448f);
                }
                if (std::uint32_t(a.i(0x7bc)) < 0x5au && 1.0f > a.dist()) a.i(0x7bc) = 0;
                else if (a.i(0x7bc) != 0) break;
                die();                                              // ATK - 12.5, section 5
                A.atk -= 12.5f;
                out.section = 5; out.section_force = true;
                break;
            }
            case 3: {                                               // round the player (0x4db3f8)
                if (a.i(0x79c) == 0) {
                    a.f(0x7ac) = unit(e.rng()) * k2Pi - kPi;
                    a.i(0x79c) += 1;
                    const float r = unit(e.rng()) * 1.5f + 2.5f;
                    a.f(0x7b0) = r;
                    a.f(0x894) = fcos(a.f(0x7ac)) * r + T.x;
                    a.f(0x89c) = fsin(a.f(0x7ac)) * r + T.z;
                    const float u = unit(e.rng());
                    a.f(0x898) = u + u + 1.0f + e.target_floor;
                } else if (a.i(0x79c) == 1) {
                    const float r = a.f(0x7b0);
                    a.f(0x894) = fcos(a.f(0x7ac)) * r + T.x;
                    a.f(0x89c) = fsin(a.f(0x7ac)) * r + T.z;
                    a.f(0x898) = unit(e.rng()) * 2.5f + 0.5f + e.target_floor;
                    const float dy = a.f(0x898) - O.y, dx = a.f(0x894) - O.x, dz = a.f(0x89c) - O.z;
                    if (0.10000000149011612f > dy * dy + dx * dx + dz * dz) { a.i(0x87c) = 0; a.i(0x79c) = 2; }
                } else {
                    const std::uint32_t thr = r15(e) % 90u + 60u;
                    if (std::uint32_t(a.i(0x87c)) > thr && a.i(0x7c0) == 0) a.i(0x79c) = 0;
                    if (a.i(0x87c) == 40 && a.i(0x7c0) == 0) {      // one shot, pitched at the player
                        const float L = float(std::sqrt(double(a.f(0x734) * a.f(0x734) + a.f(0x73c) * a.f(0x73c))));
                        const Vec3 dir = nrm({fcos(a.heading) * L, a.f(0x738), fsin(a.heading) * L});
                        shot(dir, fd * 0.019999999552965164f + (f231 ? 0.30000001192092896f : 0.22499999403953552f));
                        out.sounds.push_back(0x94);
                    }
                }
                break;
            }
            case 4: {                                               // a ring in front (0x4db8da)
                const float hf = float(want >> 1);
                float b = 0.5f / hf * kPi;
                if (want & 1) b = b * 4.0f / 3.0f;
                float g = u2f(slot >> 1) / hf * kPi + b;
                if (slot & 1) g *= -1.0f;
                const float s = fsin(g) * 0.6000000238418579f;
                const float c3 = fcos(face), s3 = fsin(face);
                anchor(c3 * 0.800000011920929f - s * s3, fcos(g) * 0.6000000238418579f + 0.0f,
                       s * c3 + s3 * 0.800000011920929f);
                break;
            }
            case 5: {                                               // a fan round Hugo (0x4dba41)
                float v = u2f((slot >> 1) + 1) * 360.0f / float(want + 1) * kDeg;
                if (slot & 1) v *= -1.0f;
                const float g = facing314(H) + v;
                anchor(fcos(g), 0.0f, fsin(g));
                break;
            }
            case 6: {                                               // two columns (0x4dbb0e)
                float v = u2f((slot >> 1) + 1) * 0.4000000059604645f;
                if (slot & 1) v *= -1.0f;
                const float c3 = fcos(face), s3 = fsin(face);
                anchor(c3 * 0.4000000059604645f - v * s3, 0.0f, v * c3 + s3 * 0.4000000059604645f);
                break;
            }
            default: break;
        }

        // ---- the step toward the anchor (0x4dbc12), every mode but the dash
        if (mode != 2) {
            const Vec3 P = a.v(0x894);
            const Vec3 D{P.x - O.x, P.y - O.y, P.z - O.z};
            Vec3 V = D;
            bool smooth = false;
            if (mode != 1 && want >= 6 && std::fabs(D.x) + std::fabs(D.z) > 1.0f) {   // round the player
                smooth = true;
                const float th = fatan2(a.f(0x73c), a.f(0x734));
                const float pitch = float(std::atan2(double(a.f(0x738)),
                    double(float(std::sqrt(double(a.f(0x734) * a.f(0x734) + a.f(0x73c) * a.f(0x73c)))))));
                const float Rx = P.x - T.x, Rz = P.z - T.z;
                float g = fatan2(Rz, Rx) - th;
                float q = float(std::atan2(double(P.y - T.y), double(float(std::sqrt(double(Rz * Rz + Rx * Rx)))))) * pitch;
                while (g > kPi) g -= k2Pi;                          // the lower bound is +pi in
                while (kPi >= g) g += k2Pi;                         // the bytes: into (pi, 3pi]
                while (q > kPi) q -= k2Pi;
                while (kPi >= q) q += k2Pi;
                q += pitch;
                const float cp = fcos(q);
                g += th;
                const Vec3 Hd{fcos(g) * cp, fsin(q), fsin(g) * cp};
                const Vec3 U = nrm({Hd.x + O.x - T.x, Hd.y + O.y - T.y, Hd.z + O.z - T.z});
                const Vec3 dA{P.x - T.x, P.y - T.y, P.z - T.z};
                float m = dA.y * dA.y + dA.x * dA.x + dA.z * dA.z;
                const float mv = a.f(0x734) * a.f(0x734) + a.f(0x738) * a.f(0x738) + a.f(0x73c) * a.f(0x73c);
                if (!(m > mv)) m = mv;
                const float L = float(std::sqrt(double(m)));
                const Vec3 Q{L * U.x + T.x, L * U.y + T.y, L * U.z + T.z};
                const Vec3 t1 = nrm({Q.x - O.x, Q.y - O.y, Q.z - O.z});
                const Vec3 t2 = nrm({Q.x - P.x, Q.y - P.y, Q.z - P.z});
                const float s = D.y * D.y + D.x * D.x + D.z * D.z;
                float t = 1.0f;
                if (!(0.0000999999974737875f > s)) t = std::fmin(0.5f / s, 1.0f);
                const Vec3 hp = hermite(O, t1, P, t2, t);
                const Vec3 st{hp.x - O.x, hp.y - O.y, hp.z - O.z};
                const Vec3 sn = nrm(st), dn = nrm(D);
                bool reject = (P.y > O.y && 0.0f > st.y) || (O.y > P.y && st.y > 0.0f);
                if (!reject) reject = 0.25f > sn.y * dn.y + sn.x * dn.x + sn.z * dn.z || s > 120.0f;
                if (!reject) V = st;
            }
            const Vec3 n = nrm(V);
            const Vec3 h{n.x * 0.5f, n.y * 0.5f, n.z * 0.5f};
            if (std::fabs(h.x * 1.25f) + 0.05000000074505806f >= std::fabs(D.x) &&
                std::fabs(h.y * 1.25f) + 0.05000000074505806f >= std::fabs(D.y) &&
                std::fabs(h.z * 1.25f) + 0.05000000074505806f >= std::fabs(D.z)) {
                O = P;                                              // close enough: snap
                a.set_v(0x7f0, {0, 0, 0});
            } else {
                Vec3 st = h;
                if (smooth) {
                    if (a.f(0x7f0) == 0.0f && a.f(0x7f4) == 0.0f && a.f(0x7f8) == 0.0f) a.set_v(0x7f0, h);
                    st = {a.f(0x7f0) * 0.8999999761581421f + h.x * 0.10000000149011612f,
                          a.f(0x7f4) * 0.8999999761581421f + h.y * 0.10000000149011612f,
                          a.f(0x7f8) * 0.8999999761581421f + h.z * 0.10000000149011612f};
                }
                O = {st.x + O.x, st.y + O.y, st.z + O.z};
                a.set_v(0x7f0, st);
            }
            O.x -= fcos(a.heading) * a.f(0x7cc);                    // the recoil
            O.y -= 0.0f;
            O.z -= fsin(a.heading) * a.f(0x7cc);
            a.f(0x7cc) *= 0.8999999761581421f;
        }
    }

    // ---- 0x4dc56b (also after INIT): the timed shots M810 ordered
    if (a.i(0x7a8) != 0 && a.i(0x7c0) == 0) {
        if (mode != 5) {                                            // toward Hugo's target, 72 deg a tick
            float g = wrap(fatan2(H.ai.f(0x73c), H.ai.f(0x734)) - face);
            if (std::fabs(g) > 1.2566370964050293f) g = (0.0f > g ? -1.0f : 1.0f) * 1.2566370964050293f;
            set_face(face + g);
        }
        if (--a.i(0x7a8) == 0 && a.i(0x7a4) != 0) {
            a.i(0x7a4) -= 1;
            const float f = f231 ? 1.0f : 1.5f;
            a.i(0x7a0) = 3;
            a.f(0x7cc) = 0.30000001192092896f;
            a.i(0x7a8) = int(15.0f / f);
            const float g = a.heading + 0.0f;
            shot({fcos(g), 0.0f, fsin(g)}, (f231 ? 0.3700000047683716f : 0.30000001192092896f) +
                                               fd * 0.03500000014901161f);   // FUN_00593550
            out.sounds.push_back(0x94);
        }
    }
    // ---- the recall (+0x7c0, set by M810): EF2 0x95 on, turn toward the player
    if (a.i(0x7c0) != 0) {
        // The zoom blur [BYTES 0x4dc816..0x4dc833]: DAT_0076a550 = 0.1 every tick of the recall (the rate is left
        // as it is), DAT_007b2488 = 1, centred (FUN_005e0590) on Hugo's origin (+0x854 -> +0x50).
        out.zoom_s = 0.1f;
        out.zoom_centre = HO;
        if (std::uint32_t(a.i(0x7c0)) < std::uint32_t(30 * d + 165)) {
            if (mode != 5) {
                float g = wrap(fatan2(a.f(0x73c), a.f(0x734)) - face);
                if (g > 2.0420351028442383f) g = 2.0420351028442383f;   // fabs of the BOOL: one side only
                set_face(face + g);
            }
            const float g2 = wrap(face - face0);
            const float lim = fd * 0.006000000052154064f + 0.014999999664723873f;
            const float ad = std::fabs(g2);
            const float m = lim < ad ? lim : ad;
            set_face((0.0f > g2 ? -1.0f : 1.0f) * m + face0);
        }
        auto fx = [&](int id, int tag) {                            // FUN_00450410 + FUN_00451c20(mode 1)
            AiEffect f{id, O, 1.0f, true};
            f.tag = tag; f.power = 5.0f; f.knock = 15.0f; f.rtype = 1; f.status = 0;
            out.effects.push_back(f);
        };
        if (a.i(0x88c) == 0) {
            fx(0x95, 0);
            a.i(0x88c) = 1;                                         // [H] stands in for the effect pointer
            fx(0x96, -1);
        } else if (std::uint32_t(a.i(0x7c0)) > 100u && std::uint32_t(a.i(0x7c0)) % 50u == 15u) {
            fx(0x96, -1);
        }
        a.i(0x7c0) -= 1;
        if (mode == 5) mode = 0;
    } else {
        if (a.i(0x88c) == 0) out.zoom_r = -0.025f;                  // 0x4dcc8c: DAT_0076a554 = 0xbccccccd
        kill_fx();
    }
    // ---- the self-kill (0x4dcc96)
    {
        const float dy = O.y - HO.y, dx = O.x - HO.x, dz = O.z - HO.z;
        if (slot >= std::uint32_t(want) || dy * dy + dx * dx + dz * dz > 40000.0f || gflag(e, 130, 1) == 0) die();
    }
    // ---- the floor under it (+0x318): a ray from 5 above, 20 down
    if (w) {
        const AiWorld::Ray r = w->ray({O.x, O.y + 5.0f, O.z}, {0.0f, -20.0f, 0.0f});
        if (r.hit) A.floor_y = r.point.y;
    }
    A.pos = {O.x, O.y - A.height, O.z};
    if (faced) out.snap_facing = true;
    ramp();
    return out;
}

}  // namespace cleria::brains
