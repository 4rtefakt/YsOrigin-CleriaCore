// The Piache pair — M450 Maka-Piache, brain FUN_004c0140 (AI index 38, handler FUN_004c0ee0) and M420
// Sinn-Piache, brain FUN_004be3f0 (AI index 35, handler FUN_004bfdd0). Both handlers are M100's minus the arena
// clamp, plus the 25-u far freeze (enemy_handler_far_freeze), the +0x64 alpha bit and (M450) the Mask of Eyes
// tint: the view. Specs: build/ai_spec/M450.md, build/ai_spec/M420.md.
//
// One design in two casts. Shared: the visibility cycle (a show flag +0x79c rebuilt each tick from the counter
// +0x7a0: shown 240 ticks after INIT, then hidden 210 + 30d / shown max(15 (3 - d), 1), frozen in sub-states >= 2;
// forced shown under the Mask of Eyes, in sub-states >= 2 and outside sections 0 / 1 / 2; alpha +-0.01, +0.03
// acting), the stand (15..59 ticks facing T) / walk (toward T +-45 deg, away inside 4 u) loop, and the summon
// (section 0x139: at its Marker(269) each spawns THE OTHER cast 1..5 u away, invisible, engaged, 10 % EXP).
// M450 the shooter: standing, off cooldown, inside 11 u 60 % requests section 8 (the scripted ring of slow orbs
// tagged +0x168 = +0x16c = +0x170 = 1000), else 40 % the summon unless crowded; cooldown 90 - 15d, then a walk.
// M420 the batter: 75 % the summon, or when Maka-Piaches stand near it follows the nearest (+0x888); every 10th
// tick it claims a tagged orb not heading at the player, runs (0x800) to a lead point ahead of it, holds section 7
// there and, the orb within 0.32, bats it flat at the player (speed 0.35 + 0.3d, effect 65, section 0x14).
// [not modelled] the keep-out box restore (FUN_0042dc90). [approx] the child is spawned at self + (dx, 0, dz)
// (the engine hands the factory the absolute point as an offset, then moves the child); its section-2 request
// rides EnemyAi::hook_section (one tick late); the orb scan's `owner == 1` test (never true) is dropped; a
// released orb that is already gone is not written to; the GoParticle pool always has room for effect 65.
#include <algorithm>
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

namespace {

Vec3 norm(const Vec3& v) {                                         // FUN_0042cc50
    const float l = std::sqrt((v.x * v.x + v.y * v.y) + v.z * v.z);
    return l == 0.0f ? Vec3{} : Vec3{v.x * (1.0f / l), v.y * (1.0f / l), v.z * (1.0f / l)};
}

// WALK_SETUP (M450 0x4c0af8, M420 0x4bf5db): a fixed direction toward T +-45 deg, away inside 4 u.
void walk_setup(EnemyAi& a, const AiEnv& e) {
    using ai::r15;
    a.dur() = int(r15(e) % 45u) + 15;
    a.steer() = 5;
    float an = 4.0f > a.dist() ? float(std::atan2(double(e.self.z - e.target.z), double(e.self.x - e.target.x)))
                               : float(std::atan2(double(e.target.z - e.self.z), double(e.target.x - e.self.x)));
    an += float(int(r15(e) % 91u) - 45) * 0.0174533f;
    const float c = float(std::cos(double(an))), s = float(std::sin(double(an)));
    a.set_v(0x748, {c, 0.0f, s});
    if (a.turn_hold() == 0) a.heading = rotate_toward(a.heading, std::atan2(s, c), a.f(0x814));
    else a.turn_hold() -= 1;
}

// FLAG's body (M450 0x4c04d1, M420 0x4bf4f3): a walk that ended rolls 1-in-5 for another, else a stand; a stand
// (or a sub-state set this tick) becomes a walk.
void flag_body(EnemyAi& a, const AiEnv& e) {
    using ai::r15;
    a.i(0x7a8) = 0;
    if (a.i(0x7a4) == 1) {
        a.i(0x7a4) = r15(e) % 5u == 0 ? 1 : 0;
        a.dur() = int(r15(e) % 45u) + 15;
        if (a.i(0x7a4) != 1) return;
    } else {
        a.i(0x7a4) = 1;
    }
    walk_setup(a, e);
}

// The visibility tail (M450 0x4c0d66, M420 0x4bfc34).
void visibility(EnemyAi& a, const AiEnv& e, int d) {
    const bool acting = std::uint32_t(a.i(0x7a4)) > 1u;
    int c = a.i(0x7a0);
    if (c > 0) {
        a.i(0x79c) = 1;
        if (!acting) a.i(0x7a0) = --c;
        if (c == 0) a.i(0x7a0) = d >= 0 ? -(30 * d + 210) : -std::max(30 * d + 60, 1);
    } else if (c < 0) {
        a.i(0x79c) = 0;
        if (!acting) a.i(0x7a0) = ++c;
        if (c == 0) a.i(0x7a0) = std::max(15 * (3 - d), 1);
    } else {
        a.i(0x7a0) = std::max(15 * (3 - d), 1);
    }
    const int S = e.section;
    if (e.mask_of_eyes || acting || !(S == 0 || S == 1 || S == 2)) a.i(0x79c) = 1;
    if (a.i(0x79c) != 0) {
        const float step = acting ? 0.03f : 0.01f;
        a.alpha = 1.0f > a.alpha + step ? a.alpha + step : 1.0f;
    } else {
        a.alpha = std::max(0.0f, a.alpha - 0.01f);
    }
}

// The INIT both share (M450 0x4c0cba, M420 0x4bf1d8).
void piache_init(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    ai::init(a, p, e);                                             // keeps +0x854
    a.i(0x79c) = 0; a.i(0x7a0) = 240; a.i(0x7a4) = 0; a.i(0x7a8) = 1;
    a.dur() = 240;
    if (a.i(0x854) != 0) { a.alpha = 0.0f; a.phase() = 2; }        // summoned: invisible, engaged at once
}

// The summon at Marker(m) (M450 0x4c0723, M420 0x4be99f): up to 10 probes a tick for a spot 1..5 u away.
void summon(const AiEnv& e, int child_cast, int m, AiOut& out) {
    Actor* A = e.actor;
    if (!e.world || !A) return;
    for (int k = 0; k < 10; ++k) {
        const std::uint32_t w1 = e.rng(), w2 = e.rng();
        const float r = unit(w1) * 4.0f + 1.0f, ang = unit(w2) * 6.2831855f + 0.0f;
        const float dx = float(std::cos(double(ang))) * r, dz = float(std::sin(double(ang))) * r;
        const Vec3 n = norm({dx, 0.0f, dz});
        const Vec3 P{e.self.x + dx, e.self.y + 0.0f, e.self.z + dz};
        const Vec3 S{P.x - n.x * 0.2f, P.y - n.y * 0.2f, P.z - n.z * 0.2f};
        const Vec3 E{n.x * 0.2f + n.x * 0.2f + dx, n.y * 0.2f + n.y * 0.2f + 0.0f, n.z * 0.2f + n.z * 0.2f + dz};
        if (e.world->ray(S, E).hit) continue;                      // the space beyond P (literal)
        if (!e.world->ray(P, {0.0f, -2.0f, 0.0f}).hit) continue;   // a floor within 2 below P
        if (Actor* c = e.world->spawn_child(*A, child_cast, {dx, 0.0f, dz}, A->level, true)) {
            c->pos = {P.x, P.y - c->height, P.z};
            c->exp_reward *= 0.1f;
            c->ai.alpha = 0.0f;
            c->ai.i(0x854) = A->instance_id;
            c->ai.hook_section = 2;                                // FUN_00446550(c, 2, 0)
        }
        AiEffect fx; fx.id = m; fx.pos = P;
        out.effects.push_back(fx);
        out.set_marker = 0;
        return;
    }
}

Vec3 origin(const Actor& o) { return {o.pos.x, o.pos.y + o.height, o.pos.z}; }

}  // namespace

AiOut m450(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // 0x4c024b: no tail this tick
    Actor* A = e.actor;
    const int d = difficulty(e);
    const int S = e.section;
    a.i(0x79c) = 0;
    bool engaged = false;
    switch (a.phase()) {
        case 0: piache_init(a, p, e); break;                       // 0x4c0cba
        case 1:                                                    // UNAWARE (0x4c02e2)
            if (std::uint32_t(a.i(0x7a4)) >= 2u) { engaged = true; break; }
            a.i(0x70c) = 0;
            if (a.i(0x7a8) == 0) a.dur() = 0; else a.i(0x70c) = 1;
            ai::idle(a, p, e);
            a.i(0x7a4) = 0;
            break;
        case 2: engaged = true; break;
        default: break;
    }
    if (engaged) {                                                 // ENGAGED (0x4c032a)
        if (a.dist() > 20.0f && std::uint32_t(a.i(0x7a4)) < 2u) ai::disengage(a);
        auto end = [&] {                                           // END (0x4c0700): cooldown, then always a walk
            a.cool() = 90 - 15 * d;
            a.i(0x7a8) = 0; a.i(0x7a4) = 1;
            walk_setup(a, e);
        };
        switch (a.i(0x7a4)) {
            case 0: {                                              // STAND (0x4c0361)
                a.steer() = 0; a.action() = 2;
                const bool flag = a.dur() == 0;
                if (a.cool() == 0 && std::uint32_t(a.dur()) % 10u == 0) {
                    const std::uint32_t r = r15(e) % 45u + 15u;    // D1
                    if (std::uint32_t(a.dur()) < r) {
                        bool done = false;
                        if (11.0f > a.dist() && r15(e) % 1000u < 600u) {   // D2
                            out.section = 8; out.attack = true;    // gated; the state is set either way
                            a.i(0x7a4) = 2; done = true;
                        }
                        if (!done && r15(e) % 1000u < 400u) {      // D3
                            int near = 0, mid = 0;
                            if (e.world)
                                e.world->for_each([&](Actor& o) {
                                    if (&o == A || o.is_player() || !(o.hp >= 1.0f)) return;
                                    if (!(o.cast_id == 0x2e || (o.cast_id == 0x31 && o.live_section == 0x139))) return;
                                    const float h2 = (o.pos.z - e.self.z) * (o.pos.z - e.self.z) +
                                                     (o.pos.x - e.self.x) * (o.pos.x - e.self.x);
                                    if (225.0f > h2) ++near;
                                    if (625.0f > h2) ++mid;
                                });
                            if (near < 2 && mid < 10) { out.section = 0x139; a.i(0x7a4) = 3; }
                        }
                    }
                }
                if (flag) flag_body(a, e);
                break;
            }
            case 1:                                                // WALK (0x4c06bb)
                a.steer() = 5; a.action() = 0x20;
                if (a.dur() == 0) flag_body(a, e);
                break;
            case 2:                                                // RING (0x4c06df)
                a.steer() = 5; a.action() = 1;
                if (S != 8) end();
                break;
            case 3:                                                // SUMMON (0x4c0723)
                a.steer() = 5; a.action() = 1;
                if (S != 0x139) { end(); break; }
                if (e.aso_marker != 0) summon(e, 0x2e, e.aso_marker, out);
                break;
            default: break;
        }
    }
    visibility(a, e, d);
    return out;
}

AiOut m420(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // 0x4be521: no tail this tick
    Actor* A = e.actor;
    const int d = difficulty(e);
    const int S = e.section;
    const int self_id = A ? A->instance_id : 0;
    const Actor* PLa = e.world ? e.world->player() : nullptr;
    const Vec3 PL = PLa ? origin(*PLa) : e.target;                 // the player, never +0x888
    bool walkflag = false, scanflag = false, decide = false;
    if (a.i(0x888) != 0) {                                         // validate the ally target (0x4be59a)
        const Actor* t = e.world ? e.world->find(a.i(0x888)) : nullptr;
        if (!t || !(t->hp >= 1.0f)) a.i(0x888) = 0;
    }
    a.i(0x79c) = 0;
    // The claimed orb (+0x7bc: its handle), still a kind-1 bullet this actor owns.
    auto with_orb = [&](const std::function<void(const AiWorld::BulletRef&)>& f) {
        bool found = false;
        if (e.world && a.i(0x7bc) != 0)
            e.world->for_each_bullet([&](const AiWorld::BulletRef& b) {
                if (found || b.handle != a.i(0x7bc) || b.kind != 1 || *b.owner != self_id) return;
                found = true;
                f(b);
            });
        return found;
    };
    auto lead_point = [&](const AiWorld::BulletRef& b) {           // pos + 2 dir + dir max(L / 3 - 1, 0)
        Vec3 wp{(b.dir->x + b.dir->x) + b.pos.x, (b.dir->y + b.dir->y) + b.pos.y, (b.dir->z + b.dir->z) + b.pos.z};
        const float L = std::sqrt((e.self.y - wp.y) * (e.self.y - wp.y) + (e.self.x - wp.x) * (e.self.x - wp.x) +
                                  (e.self.z - wp.z) * (e.self.z - wp.z));
        float k = L / 3.0f - 1.0f;
        if (0.0f > k) k = 0.0f;
        wp = {wp.x + b.dir->x * k, wp.y + b.dir->y * k, wp.z + b.dir->z * k};
        a.set_v(0x894, wp);
    };
    auto back_to_loop = [&] { a.i(0x7a4) = 1; a.i(0x7bc) = 0; walkflag = true; };

    bool engaged = false;
    switch (a.phase()) {
        case 0:                                                    // INIT (0x4bf1d8)
            piache_init(a, p, e);
            a.i(0x7bc) = 0;
            a.i(0x7c0) = A ? std::int32_t(A->flags12c) : 0;
            break;
        case 1:                                                    // UNAWARE (0x4be667)
            if (std::uint32_t(a.i(0x7a4)) >= 2u) { engaged = true; break; }
            if (a.i(0x888) != 0 && !(a.dist() > 15.0f)) { engaged = true; break; }
            a.i(0x70c) = 0;
            if (a.i(0x7a8) == 0) a.dur() = 0; else a.i(0x70c) = 1;
            ai::idle(a, p, e);
            a.i(0x7a4) = 0;
            scanflag = true;
            break;
        case 2: engaged = true; break;
        default: break;
    }
    if (engaged) {                                                 // ENGAGED (0x4be622)
        if (a.dist() > 20.0f && std::uint32_t(a.i(0x7a4)) < 2u && (a.i(0x888) == 0 || a.dist() > 15.0f)) ai::disengage(a);
        switch (a.i(0x7a4)) {
            case 0:                                                // STAND (0x4be6cc)
                a.steer() = 0; a.action() = 2;
                walkflag = a.dur() == 0; scanflag = true; decide = true;
                break;
            case 1:                                                // WALK (0x4be6fb)
                a.steer() = 5; a.action() = 0x20;
                walkflag = a.dur() == 0; scanflag = true;
                break;
            case 2: {                                              // RUN (0x4be729): to the orb's lead point
                a.steer() = 4; a.action() = 0x800;
                const Vec3 old = a.v(0x894);
                const float d2 = (e.self.x - old.x) * (e.self.x - old.x) + (e.self.z - old.z) * (e.self.z - old.z);
                const bool found = with_orb(lead_point);
                if (0.05f > d2) {                                  // arrived (at last tick's point)
                    a.i(0x7a4) = 5; a.steer() = 1; a.action() = 1;
                    out.section = 7;                               // the stance (gated)
                    if (!found) back_to_loop();
                    break;
                }
                const Vec3 wp = a.v(0x894);
                if (!found || (e.world && e.world->ray(e.self, wp - e.self).hit)) {   // gone, or the line is blocked
                    with_orb([&](const AiWorld::BulletRef& b) { *b.owner = 0; });      // RELEASE
                    back_to_loop();
                }
                break;
            }
            case 3:                                                // SUMMON (0x4be99f)
                a.steer() = 5; a.action() = 1;
                if (S != 0x139) { a.cool() = 90 - 15 * d; a.i(0x7a4) = 1; walkflag = true; break; }
                if (e.aso_marker != 0) summon(e, 0x31, e.aso_marker, out);
                break;
            case 5: {                                              // BAT (0x4bed7d)
                if (S != 7) {
                    if (S != 0x14) back_to_loop();                 // the stance / swing is over (or was cut)
                    break;
                }
                const bool found = with_orb([&](const AiWorld::BulletRef& b) {
                    a.set_v(0x894, b.pos);
                    const float vx = b.pos.x - e.self.x, vz = b.pos.z - e.self.z;
                    const Vec3 u = norm({e.self.x - b.pos.x, e.self.y - b.pos.y, e.self.z - b.pos.z});
                    if (A) { A->mover.k2.x -= u.x * 0.01f; A->mover.k2.z -= u.z * 0.01f; }   // tugged toward the orb
                    if (0.1f > vx * vx + vz * vz) {                // within ~0.316: the hit
                        *b.dir = norm({PL.x - b.pos.x, 0.0f, PL.z - b.pos.z});
                        *b.speed = float(d) * 0.3f + 0.35f;
                        AiEffect fx; fx.id = 0x41;                 // z_yarare01 at the midpoint (no hit emitter)
                        fx.pos = {(b.pos.x + e.self.x) * 0.5f, (e.self.y + b.pos.y) * 0.5f, (e.self.z + b.pos.z) * 0.5f};
                        out.effects.push_back(fx);
                        out.section = 0x14;                        // the swing
                    } else if (0.0f > (b.dir->y * u.y + b.dir->x * u.x) + b.dir->z * u.z) {
                        out.section = 0x14;                        // the orb moves away: swing and miss
                    }
                });
                if (!found) out.section = 0x14;                    // orb gone: swing anyway
                break;
            }
            default: break;                                        // 4 and > 5: nothing
        }
    }

    // COMMON (0x4bf29b): the flag word
    if (A) {
        if (S == 7) A->flags12c &= 0xfdf7ffbfu;
        else A->flags12c = std::uint32_t(a.i(0x7c0));
    }
    // DECIDE (0x4bf2bc): only from STAND
    if (decide && a.cool() == 0 && a.i(0x7a4) == 0 && std::uint32_t(a.dur()) % 10u == 0) {
        const std::uint32_t r = r15(e) % 45u + 15u;                // D1
        if (std::uint32_t(a.dur()) < r && r15(e) % 1000u < 750u) { // D2
            int near = 0, mid = 0, best = -1;
            float m2 = 225.0f;
            if (e.world)
                e.world->for_each([&](Actor& o) {
                    if (&o == A || o.is_player() || !(o.hp >= 1.0f)) return;
                    if (!(o.cast_id == 0x31 || (o.cast_id == 0x2e && o.live_section == 0x139))) return;
                    const float h2 = (o.pos.z - e.self.z) * (o.pos.z - e.self.z) + (o.pos.x - e.self.x) * (o.pos.x - e.self.x);
                    const float old = m2;
                    if (m2 > h2) m2 = h2;
                    if (old > h2) { best = o.instance_id; ++near; }   // new minima below 225
                    if (625.0f > h2) ++mid;
                });
            if (near >= 1 || mid >= 5) a.i(0x888) = best >= 0 ? best : 0;   // follow the nearest ally
            else { out.section = 0x139; a.i(0x7a4) = 3; }
        }
    }
    if (walkflag) flag_body(a, e);                                 // FLAG (0x4bf4f3)
    // The orb scan (0x4bf809)
    if (scanflag && std::uint32_t(a.dur()) % 10u == 0 && e.world) {
        int claimed[32]; int n = 0;                                // the orbs other Sinn-Piaches hold
        e.world->for_each([&](Actor& o) {
            if (n < 32 && &o != A && o.cast_id == 0x2e && o.hp >= 1.0f && o.ai.i(0x7bc) != 0) claimed[n++] = o.ai.i(0x7bc);
        });
        int best = 0;
        float m2 = 144.0f;
        e.world->for_each_bullet([&](const AiWorld::BulletRef& b) {
            if (b.kind != 1) return;
            if (!(*b.f168 > 995.0f && *b.f16c > 995.0f && *b.f170 > 995.0f)) return;
            if (!(1.0f > float(std::fabs(double(e.self.y - b.pos.y))))) return;
            for (int i = 0; i < n; ++i) if (claimed[i] == b.handle) return;
            const Vec3 t = PL - b.pos;
            const float t2 = (t.x * t.x + t.y * t.y) + t.z * t.z;
            const Vec3 tn = norm(t);
            if (!((b.dir->x * tn.x + b.dir->y * tn.y) + b.dir->z * tn.z < 0.5f)) return;   // already heading at the player
            if (4.0f > t2) return;                                 // within 2 of the player
            const float h2 = (b.pos.z - e.self.z) * (b.pos.z - e.self.z) + (b.pos.x - e.self.x) * (b.pos.x - e.self.x);
            if (m2 > h2) { m2 = h2; best = b.handle; }
        });
        if (best != 0)
            e.world->for_each_bullet([&](const AiWorld::BulletRef& b) {   // CLAIM
                if (b.handle != best) return;
                *b.owner = self_id;
                lead_point(b);
                a.i(0x7a4) = 2;
                a.i(0x7bc) = b.handle;
                *b.f168 = *b.f16c = *b.f170 = 0.0f;
            });
    }
    visibility(a, e, d);
    return out;
}

}  // namespace cleria::brains
