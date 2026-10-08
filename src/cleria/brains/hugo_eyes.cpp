// Hugo's Eyes of Fact as the player's familiars: FUN_00470210 (owner = the player) and FUN_0046d8c0.
// Spec: build/ability_re/EYE_FAMILIARS.md §7..§12 (addresses there). See cleria/hugo_eyes.hpp.
#include "cleria/hugo_eyes.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "cleria/actor.hpp"

namespace cleria::hugo {
namespace {

constexpr float kPi = 3.1415927410125732f;        // DAT_006bd6ac
constexpr float k2Pi = 6.2831854820251465f;       // DAT_006bd898
constexpr float kHalfPi = 1.5707963705062866f;    // 0x6bd4e4
constexpr float kLink = 1.75f;                    // DAT_006bd510: the chain's max link
constexpr float kV = 0.8975979089736938f;         // 0x6bd3dc = 2 pi / 7: the V's half angle
constexpr float kNear = 0.1f;                     // 0x6bd19c: arrived
constexpr float kFar = 15.0f;                     // 0x6bd964: snap
constexpr float kAlphaStep = 0.03999999910593033f;   // 0x6bd12c

float fcos(float x) { return float(std::cos(double(x))); }   // FUN_0066fd70
float fsin(float x) { return float(std::sin(double(x))); }   // FUN_00670d30
float len(const Vec3& v) { return float(std::sqrt(double(v.x * v.x + v.y * v.y + v.z * v.z))); }   // FUN_0041f570
Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 mul(const Vec3& a, float k) { return {a.x * k, a.y * k, a.z * k}; }
// (float)(double)(u32) through the 0x6becd0 fix-up.
float u2f(std::uint32_t u) { return float(double(u)); }
int gflag(const AiEnv& e, int i) { return e.world ? e.world->flag(i, 0) : 0; }

}  // namespace

int eye_count(int flag211, int flag210, bool boosting) {
    if (flag211 != 0) return 0;
    if (flag210 != 0) return 6;
    return boosting ? 4 : 2;
}

int eye_effect_class(int effect) {               // FUN_0046d650 [BYTES 0x46d650..0x46d6a0]
    switch (effect) {
        case 0x82: case 0x83: return 3;
        case 0x84: return 5;
        case 0x86: case 0x96: return 0;
        case 0x87: return 4;
        case 0x88: case 0x97: return 2;
        case 0x89: case 0x95: return 1;
        case 0x93: case 0x9c: return 6;
        default: return -1;
    }
}

bool b8a4(const EnemyAi& a) { return (std::uint32_t(a.i(0x8a4)) & 0xffu) != 0; }
bool b8a5(const EnemyAi& a) { return (std::uint32_t(a.i(0x8a4)) & 0xff00u) != 0; }
void set_b8a4(EnemyAi& a, bool v) {
    a.i(0x8a4) = std::int32_t((std::uint32_t(a.i(0x8a4)) & ~0xffu) | (v ? 1u : 0u));
}
void set_b8a5(EnemyAi& a, bool v) {
    a.i(0x8a4) = std::int32_t((std::uint32_t(a.i(0x8a4)) & ~0xff00u) | (v ? 0x100u : 0u));
}

bool owned_eye(const Actor& a, int player_id) { return a.cast_id == 0xb0 && a.ai.i(0x854) == player_id; }

AiOut familiar(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    AiOut out;
    if (!e.actor || !e.familiar) return out;
    Actor& E = *e.actor;
    FamiliarHost& h = *e.familiar;
    if (e.launched) return out;                                       // +0x4d0
    if (a.i(0x854) != h.player_id) return out;                        // the owner test (0xd / 0xa7)
    const int section = e.section;
    const Vec3 Opos = h.origin;
    Vec3 O = e.self;                                                  // +0x50: the eye moves itself
    int fire_ix = -1;                                                 // the wheel's 0x89, attached at offset 0
    auto put = [&] {
        E.pos = {O.x, O.y - E.height, O.z};
        if (fire_ix >= 0) out.effects[std::size_t(fire_ix)].pos = O;  // FUN_00451c20: on the eye's +0x20
    };
    auto kill_fx = [&] {                                              // +0x88c / +0x890 -> +0x1d4 = 1
        for (int t = 0; t < 2; ++t) {
            if (a.i(t == 0 ? 0x88c : 0x890) == 0) continue;
            AiEffect k; k.kill_tag = t; out.effects.push_back(k);
            a.i(t == 0 ? 0x88c : 0x890) = 0;
        }
    };
    // Off-screen and not a mine: on the owner (FUN_0045d3c0) [BYTES 0x470272].
    if (h.culled && !b8a4(a)) O = Opos;
    // The count rule; a change restarts the wheel phase and puts the eye on the owner [0x4702d1].
    const int n = eye_count(gflag(e, 211), gflag(e, 210), h.boosting);
    if (n != a.i(0x7bc)) { a.f(0x7ac) = 0.0f; O = Opos; }
    a.i(0x7bc) = n;
    // Hugo levelled up: the eye dies (HP 0) and is respawned at his level [0x4702f7..0x4703a7].
    if (std::uint32_t(h.level) > std::uint32_t(E.level)) {
        E.hp = 0.0f; out.set_hp = 0.0f;
        if (section == 5) { put(); return out; }
        kill_fx();                                                    // [not modelled] Se_Stop(0x97 / 0x9c, 10)
        put();
        return out;
    }
    // A mine (or Hugo down): the fuse [0x4703a8..0x4705a8].
    if ((b8a4(a) || h.hp < 1.0f) && section != 5) {
        a.sub() = 2;
        bool fire = a.i(0x7c8) == 0;
        if (!fire) { a.i(0x7c8) -= 1; fire = a.i(0x7c8) == 0; }
        if (fire) {
            if (!b8a5(a)) {
                if (h.hp >= 1.0f) { out.section = 5; out.section_force = true; }   // FUN_00446620(5): explode
            } else {
                kill_fx();
                if (h.latch_7d4 == 0) h.latch_7d4 = 1;                // the player's charged blast
            }
            E.hp = 0.0f; out.set_hp = 0.0f;
        }
        // [not modelled] the +0x248 / +0x144 ramps (1 + (D - fuse) x 0.004 / 0.002, x 0.013 / 0.0065
        // charged; clamped at 1.3 / 1.15): two scale fields whose reader is not traced.
    }
    // The gather: A[s] = each eye's joint +0x7e4, B[s] = its tail +0x7f0; A[0] = the owner, B[0] = the
    // head +0x7fc. Last tick's values. [H] an empty slot reads the owner (the engine reads stale stack).
    Vec3 A[8], B[8];
    for (int k = 0; k < 8; ++k) A[k] = B[k] = Opos;
    B[0] = a.v(0x7fc);
    if (e.world)
        e.world->for_each([&](Actor& X) {
            if (!owned_eye(X, h.player_id)) return;
            const std::uint32_t s = std::uint32_t(X.ai.i(0x730));
            if (s >= 1 && s < 8) { A[s] = X.ai.v(0x7e4); B[s] = X.ai.v(0x7f0); }
        });
    // Keep or release the effects [0x470672..0x4706fd]: 0x85 is never stored, so once flag 158 is 0
    // they always go.
    {
        const int f157 = gflag(e, 157), f158 = gflag(e, 158);
        const bool keep = f157 == 1 && !b8a4(a) && (f158 == 0 ? a.i(0x7a4) == 0x85 : a.i(0x7a4) == 0x89);
        if (!keep) kill_fx();
    }
    // The phase switch [0x4706fd]: 0 INIT, 1 FUN_004758a0 (the hand-off), 2 MAIN.
    if (a.phase() == 0) {                                             // INIT [0x472cd8]
        // FUN_004782f0: memset +0x6f4..+0x8ab, keeping +0x72c, +0x730, +0x7e4..+0x804, +0x854, +0x86c.
        const std::int32_t k72c = a.i(0x72c), k730 = a.i(0x730), k854 = a.i(0x854), k86c = a.i(0x86c);
        std::int32_t kc[9];
        for (int k = 0; k < 9; ++k) kc[k] = a.i(0x7e4 + 4 * k);
        for (int off = 0x6f4; off < 0x8ac; off += 4) a.i(off) = 0;
        a.i(0x72c) = k72c; a.i(0x730) = k730; a.i(0x854) = k854; a.i(0x86c) = k86c;
        for (int k = 0; k < 9; ++k) a.i(0x7e4 + 4 * k) = kc[k];
        a.phase() = 1;
        E.flags12c |= 0x10u;
        // Join the wheel in phase: the slot-1 eye's +0x7bc / +0x7c0 / +0x7ac / +0x7b0 (last match wins).
        if (e.world)
            e.world->for_each([&](Actor& X) {
                if (!owned_eye(X, h.player_id) || X.ai.i(0x730) != 1) return;
                a.i(0x7bc) = X.ai.i(0x7bc); a.i(0x7c0) = X.ai.i(0x7c0);
                a.f(0x7ac) = X.ai.f(0x7ac); a.f(0x7b0) = X.ai.f(0x7b0);
            });
        for (int k = 0; k < 16; ++k) a.f(0x814 + 4 * k) = p.raw[k];   // ENEMYINFO[80] +0x90..+0xcc
        // +0xa4 = the player's ATK (DAT_0076a73c), + 1.0 with flag 217 & 0x200000: the host's spawn.
        set_b8a4(a, false);
        a.hittable = false;                                           // +0x60 &= ~0x80
        E.flags130 |= 0x1000u;
        put();
        return out;                                                   // mode 0: no placement for the player's
    }
    if (a.phase() == 1) {                                             // FUN_004758a0 [BYTES 0x4758a0]
        if (a.i(0x884) == 0) {
            a.i(0x6fc) = 0; a.phase() = 2;
            a.i(0x710) = 0; a.i(0x714) = 0; a.i(0x700) = 0; a.i(0x788) = 0;
        }
        put();
        return out;
    }
    if (a.phase() != 2) { put(); return out; }
    // ---- MAIN [0x47071e..0x470a78]
    if (a.i(0x710) == 0) { a.sub() = 0; a.i(0x710) = -1; }
    const std::uint32_t s = std::uint32_t(a.i(0x730));
    const std::uint32_t si = s < 8 ? s : 0;
    if (s == 1) {                                                     // the head: 1.75 from the owner
        const Vec3 d = sub(A[0], B[0]);
        const float L = len(d);
        if (L > kLink) {
            const float k = std::fabs(kLink - L) / L;
            B[0] = add(B[0], mul(d, k));
            a.set_v(0x7fc, B[0]);
        }
    }
    {
        const Vec3 d = sub(B[si], A[si]);                             // own tail vs own old joint
        const float L = len(d);
        if (L > kLink) a.set_v(0x7f0, add(A[si], mul(d, kLink / L)));
    }
    a.set_v(0x7e4, si >= 1 ? B[si - 1] : B[0]);                       // joint = the previous eye's old tail
    {
        const int f157 = gflag(e, 157);
        a.i(0x72c) = f157 == 1 ? 2 : f157 == 2 ? 3 : 1;              // [0x4709ae]
    }
    const int mode = a.i(0x72c);
    if (a.i(0x7c0) != mode && a.sub() != 2) {                         // the mode changed
        a.set_v(0x7e4, Opos); a.set_v(0x7f0, Opos); a.set_v(0x7fc, Opos);
        a.f(0x7ac) = 0.0f;
        a.f(0x7b0) = h.heading;                                       // O+0x308
        a.alpha = 0.0f;
        // [not modelled] b8a4 == 1: FUN_00446620(+0x998), +0x728 = 0 (unreachable once a mine is in sub 2)
    }
    a.i(0x7c0) = mode;
    // The facing [0x470b4d]: the owner's, or the one frozen when the eye was laid.
    float face;
    if (!b8a4(a) && !b8a5(a)) { a.heading = h.heading; face = h.facing; }
    else { a.heading = a.f(0x7b4); face = a.f(0x7b8); }
    out.snap_facing = true;                                           // +0x314 written, not turned
    // FUN_004758f0(1): the steer vector only (+0x748 / +0x750) [not used: the eye moves itself].
    // The target T per mode.
    Vec3 T = Opos;
    const int f158 = gflag(e, 158);
    if (mode == 1) {                                                  // a V of pairs behind Hugo [0x470bc3]
        const float k = f158 != 0 ? 1.25f : 1.0f;                     // 0x6bd494
        if (s >= 1 && s <= 6) {
            const float an = (s & 1u) ? face + kV : face - kV;
            const float m = s <= 2 ? k : s <= 4 ? k + k : k * 3.0f;   // 0x6bd680
            T.x = Opos.x - fcos(an) * m;
            T.z = Opos.z - fsin(an) * m;
            T.y = s <= 2 ? A[0].y : s <= 4 ? A[1].y : A[2].y;
        }
        a.f(0x7fc) -= fcos(face + kV);                                // [0x4715fa] literal
        a.f(0x804) -= fsin(face - kV);
    } else if (mode == 2) {                                           // the wheel [0x470ef8..0x4711e8]
        const float w = f158 == 0 ? 0.03999999910593033f : 0.1599999964237213f;   // 0x6bd12c / 0x6bd1dc
        a.f(0x7ac) += w;
        if (a.f(0x7ac) > k2Pi) a.f(0x7ac) = 0.0f - (a.f(0x7ac) - k2Pi);   // literal: mirrors to a small negative
        const float th = u2f(s - 1u) * (k2Pi / u2f(std::uint32_t(n))) + (a.f(0x7b0) + kHalfPi) + a.f(0x7ac);
        T.x = Opos.x - fcos(th) * 1.5f;                               // 0x6bd4d4
        T.z = Opos.z - fsin(th) * 1.5f;
        T.y = Opos.y;
        a.f(0x7fc) -= fcos(a.f(0x7ac) + kHalfPi) * 1.5f;
        a.f(0x804) -= fsin(a.f(0x7ac) + kHalfPi) * 1.5f;
        // EF2 0x89 PC2_S_A_batibati2, the wheel's fire [0x47106b..0x4711e8]: power -2, knock 1, rtype 0,
        // class 1, side 0, the eye's stat block; FUN_00451c20 mode 2 (follows the eye). Once per wheel.
        if (a.i(0x88c) == 0 && section != 5 && !b8a4(a) && f158 != 0) {
            AiEffect fx;
            fx.id = 0x89; fx.pos = O; fx.attach = true; fx.tag = 0;
            fx.power = -2.0f; fx.knock = 1.0f; fx.rtype = 0; fx.status = 0;
            fire_ix = int(out.effects.size());
            out.effects.push_back(fx);
            a.i(0x88c) = 1;
            a.i(0x7a4) = 0x89;
            a.i(0x85c) = 1;
        }
    } else if (mode == 3) {                                           // the rope [0x4711ed]
        T = A[si];
    }
    // ---- the step [0x4719b5..0x472843]: airborne (+0x388 = 0), a flyer (+0x398 = 1)
    E.vert.grounded = false;
    E.flyer = true;
    E.vert.t = 0.0f;
    const Vec3 D = sub(T, O);
    const float dl = len(D);
    auto ramp_alpha = [&] {
        if (a.alpha < 1.0f) { a.alpha += kAlphaStep; if (a.alpha > 1.0f) a.alpha = 1.0f; }
    };
    auto snap = [&] { O = T; a.alpha = 0.0f; };
    if (a.sub() == 2) {                                               // laid: FUN_004776e0(1)
        a.i(0x704) = 1; a.mode() = 0;
        if (b8a5(a)) O = h.mine_point;
        put();
        return out;
    }
    if (a.sub() == 1) {                                               // arrived: FUN_004776e0(0)
        a.i(0x704) = 0; a.mode() = 0;
        ramp_alpha();
        if (dl > kNear) a.sub() = 0;
        put();
        return out;
    }
    if (a.action() != -1) { put(); return out; }
    a.i(0x704) = 0; a.mode() = 0;
    const Vec3 nD = dl == 0.0f ? Vec3{} : mul(D, 1.0f / dl);           // FUN_0042cc50
    ramp_alpha();
    float spd, div;
    if (mode == 1) {
        if (dl < kNear) { a.sub() = 1; put(); return out; }
        spd = a.f(0x81c) * a.f(0x818);                                // 1.4 x 0.02
        div = 10.0f;
    } else if (mode == 2) {
        if (dl > kFar) { snap(); put(); return out; }
        spd = a.f(0x820) * a.f(0x818);                                // 3.2 x 0.02
        div = 10.0f;
    } else {
        if (dl < kNear) { a.sub() = 1; put(); return out; }
        if (dl > kFar) { snap(); put(); return out; }
        spd = a.f(0x81c) * a.f(0x818);
        div = s <= 2 ? 10.0f : s <= 4 ? 20.0f : s <= 6 ? 30.0f : 0.0f;   // 0x6bd994 / 0x6bd9e4
    }
    const Vec3 v = mul(nD, spd);
    const float vl = len(v);
    Vec3 step;
    if (vl > dl) step = D;
    else step = div != 0.0f ? mul(v, (dl / vl) / div + 1.0f) : v;
    // x / z through knock channel 2 (+0x344 / +0x34c), spent by the mover; y directly, unless a 1-unit
    // ray that way hits; never below the floor + 0.5.
    E.mover.k2.x += step.x; E.mover.k2.y += step.y; E.mover.k2.z += step.z;
    bool blocked = false;
    if (e.world) blocked = e.world->ray(O, {0.0f, step.y >= 0.0f ? 1.0f : -1.0f, 0.0f}).hit;
    if (!blocked) O.y += step.y;
    if (O.y < E.floor_y + 0.5f) O.y = E.floor_y + 0.5f;
    put();
    return out;
}

int deploy_mines(bool charged, FamiliarHost& h, AiWorld& w, const std::function<int(const Actor&)>& section_of,
                 std::vector<DeployEvent>& ev) {
    int deployed = 0, free_eyes = 0;
    bool saved = false;
    auto lay = [&](Actor& X, const Vec3& at, int fuse) {
        EnemyAi& a = X.ai;
        ev.push_back({DeployEvent::kEffect, 0x92, {X.pos.x, X.pos.y + X.height, X.pos.z}, 0});   // PC2_me_wapu
        X.pos = {at.x, at.y - X.height, at.z};
        a.i(0x7c8) = fuse;
        a.f(0x7b4) = h.heading;                                       // P+0x308
        a.f(0x7b8) = h.facing;                                        // P+0x314
        a.hittable = false;                                           // +0x60 &= ~0x80
        a.alpha = 1.0f;                                               // +0x4d8 = 1.0
        ev.push_back({DeployEvent::kSection, 0x10f, {}, X.instance_id});   // on the EYE (FUN_00446550)
        ++deployed;
        // [not modelled] the tint +0x264 / +0x268..+0x274 and +0x4dc / +0x4e0
    };
    // A free eye: owned, not laid (+0x8a4 != 1), not exploding (+0x994 != 5).
    auto free_eye = [&](const Actor& X) {
        return owned_eye(X, h.player_id) && !b8a4(X.ai) && section_of(X) != 5;
    };
    std::vector<Actor*> eyes;
    w.for_each([&](Actor& X) { eyes.push_back(&X); });
    // Loop 1 [BYTES 0x46d8c0..]: every free eye; the charged cast lays them all at Hugo's spot.
    for (Actor* X : eyes) {
        if (!free_eye(*X)) continue;
        if (charged) {
            set_b8a4(X->ai, true); set_b8a5(X->ai, true);
            h.latch_7d4 = 0;
            if (!saved) {
                saved = true;
                h.mine_point = h.origin;                              // P+0x7c8 = P+0x50
                ev.push_back({DeployEvent::kEffect, 0x94, h.mine_point, 0});   // PC2_S_C2_tame -> P+0x7d8
                h.fx_7d8 = true;
            }
            lay(*X, h.mine_point, 0xb4);
        }
        ++free_eyes;
    }
    // Loop 2: the normal cast lays the one free eye whose slot is the free count, at Hugo's spot now.
    if (free_eyes != 0 && !charged)
        for (Actor* X : eyes) {
            if (!free_eye(*X) || X->ai.i(0x730) != free_eyes) continue;
            set_b8a4(X->ai, true);
            lay(*X, h.origin, h.extra ? 0xb4 : 0x3c);
        }
    return deployed;
}

}  // namespace cleria::hugo
