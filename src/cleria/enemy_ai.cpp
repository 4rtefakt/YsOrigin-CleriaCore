#include "cleria/enemy_ai.hpp"

#include "cleria/actor.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cleria {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

float wrap(float a) {
    while (a > kPi) a -= kTwoPi;
    while (a < -kPi) a += kTwoPi;
    return a;
}
Vec3 add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }

}  // namespace

float rotate_toward(float cur, float target, float rate) {
    if (rate >= kPi) return target;
    const float d = wrap(target - cur);
    if (std::fabs(d) <= rate) return target;
    return wrap(cur + (d > 0 ? rate : -rate));
}

float enemy_turn_facing(float facing, float heading, bool running) {
    return rotate_toward(facing, heading, running ? 0.35f : 0.18f);
}

namespace ai {

void stop(EnemyAi& a, int hold) {
    a.turn_hold() = hold;
    a.mode() = 0;
    a.speed = 0;
}

// FUN_00477450(P): a wall or a ledge ahead of P along the heading (JUMPHELPER.md §4.2).
// Off for +0x12c & 0x100000 (most casts: FACTORY.md), flyers and airborne actors. The
// probe reaches radius + 0.5 (min 1.0), x1.5 while unaware and again while stopped. A wall
// at origin height counts only for a non-engaged actor; else a floor must lie within
// `depth` below the far point (-7.5 with +0x130 & 0x20; min(-(H + 2), -2.5) for jump-capable
// casts; else -(2 + radius)). Then, in a room with a water set (DAT_0076a544 == 1): a set-1 surface under
// the far point more than 1.0 above that floor is deep water ahead = blocked (MOVEPRIM.md §2.1; W3).
bool probe_ahead(const EnemyAi& a, const AiEnv& e, const Vec3& P) {
    const Actor* A = e.actor;
    const std::uint32_t f12c = A ? A->flags12c : 0u, f130 = A ? A->flags130 : 0u;
    if (f12c & 0x100000u) return false;
    if (!e.grounded || (A && A->flyer)) return false;
    float L = e.radius > 0.5f ? e.radius + 0.5f : 1.0f;
    if (a.phase() == 1) L *= 1.5f;
    if (a.mode() == 0) L *= 1.5f;
    const float depth = (f130 & 0x20u) ? -7.5f
                      : (f12c & 0x400000u) ? std::min(-(a.f(0x828) + 2.0f), -2.5f)
                      : -2.0f - e.radius;
    const float c = std::cos(a.heading), s = std::sin(a.heading);
    const Vec3 D{L * c, 0, L * s};
    const Vec3 Q{P.x + L * c, P.y + (L - e.height), P.z + L * s};
    if (e.world) {
        if (e.world->ray(P, D).hit) return a.phase() != 2;
        const AiWorld::Ray fl = e.world->ray(Q, {0, depth, 0});
        if (!fl.hit) return true;
        const float wy = e.world->water_under(Q, depth);
        return wy > -1.0e29f && wy > fl.point.y + 1.0f;
    }
    if (e.blocked && e.blocked(P, add(P, D))) return a.phase() != 2;
    if (e.has_floor && !e.has_floor(Q.x, Q.z, Q.y, -depth)) return true;
    return false;
}

bool blocked_ahead(const EnemyAi& a, const AiEnv& e) { return probe_ahead(a, e, e.self); }

namespace {

// A solid-set segment test (FUN_005e9380 + FUN_0041e420, sel 0) that also reports the hit y.
bool solid(const AiEnv& e, const Vec3& o, const Vec3& d, float* hit_y = nullptr) {
    if (!e.world) return false;
    const AiWorld::Ray r = e.world->ray(o, d);
    if (r.hit && hit_y) *hit_y = r.point.y;
    return r.hit;
}

// FUN_00475fd0(lo, up): the wall-top measure. A wall at floor + lo within L ahead (0.2
// behind the origin), then an up-ray of `up` from the far end of that probe; h = the hit y
// above the feet, kept only above lo and capped at up - 1.
bool wall_top(const EnemyAi& a, const AiEnv& e, float lo, float up, float* h) {
    const Actor* A = e.actor;
    const float L = std::max(1.0f, e.radius + 0.5f);
    const float foot = e.height > 0.8f ? e.height + 0.2f : 1.0f;
    const float c = float(std::cos(double(a.heading))), s = float(std::sin(double(a.heading)));
    const Vec3 O1{e.self.x - c * 0.2f, A->floor_y + lo, e.self.z - s * 0.2f};
    const Vec3 D1{L * c, 0, L * s};
    if (!solid(e, O1, D1)) return false;
    float hy = 0;
    if (!solid(e, add(O1, D1), {0, up, 0}, &hy)) return false;
    const float t = hy - (e.self.y - foot);
    if (!(t > lo)) return false;
    *h = std::min(up - 1.0f, t);
    return true;
}

// FUN_00476200 (lo = 0.5, H = +0x828): the step-up hop, then the gap jump, the double
// jump (+0x130 & 0x20) and the climb, each arming section 0x13a with +0x7dc / +0x7e0 for
// the brain's launch tail. build/ai_spec/JUMPHELPER.md §3.
void jump_helper(EnemyAi& a, const AiEnv& e, AiOut& out) {
    Actor* A = e.actor;
    if (!A || !e.world) return;
    const float lo = 0.5f, H = a.f(0x828);
    const float L = std::max(1.0f, e.radius + 0.5f);
    float c = float(std::cos(double(a.heading))), s = float(std::sin(double(a.heading)));
    // 1. the step-up hop
    Vec3 O{e.self.x - c * 0.1f, A->floor_y + lo, e.self.z - s * 0.1f};
    const Vec3 D{L * c, 0, L * s};
    if (solid(e, O, D)) {
        O.y = A->floor_y + 1.0f;
        if (!solid(e, O, D)) {
            if (!A->vert.grounded) return;
            A->vert.grounded = false;
            A->vert.t = 0.1f;                                      // +0x420 not written
            A->pos.y += 0.1f;
            out.set_own_jump = 1;
            return;
        }
    }
    // 2. gates
    if (a.phase() == 3 || e.section == 0x13a) return;
    if (!A->vert.grounded) return;                                 // (the +0x710 rewrite is dead)
    auto request = [&](int sub) {
        out.section = 0x13a;                                       // gated; result ignored
        a.action() = 1;
        a.i(0x728) = sub;
    };
    // 3. the gap jump
    const bool blocked = probe_ahead(a, e, e.self);
    if (a.phase() == 2 && blocked && !(A->flags12c & 0x40u)) {
        float d = 1.5f;
        const float dmax = H * 1.5f;
        if (dmax > 1.5f) {
            do {
                c = float(std::cos(double(a.heading))); s = float(std::sin(double(a.heading)));
                float y = H;
                if (d > H) y = H - (d - H);
                if (!solid(e, e.self, {d * c, y, d * s})) {
                    const Vec3 P{e.self.x + d * c, e.self.y + y, e.self.z + d * s};
                    float hy = 0;
                    if (solid(e, P, {0, -(y + H), 0}, &hy)) {
                        a.f(0x7e0) = d * 0.1f + 0.15f;
                        a.f(0x7dc) = std::min(H, d) * 0.5f + (std::max(0.0f, hy - e.self.y) + 0.6f);
                        request(100);
                        return;
                    }
                }
                d = d + 0.5f;
            } while (dmax > d);
        }
    }
    // 4. the double jump
    float h = 0;
    if (A->flags130 & 0x20u) {
        if (wall_top(a, e, H, H * 2.0f, &h)) {
            if (h > H * 2.0f) return;
            a.f(0x7e0) = 0;
            a.f(0x7dc) = (H - 0.4f > h - H) ? H - 0.4f : h - H;
            request(0x65);
            a.i(0x730) = 0;
            return;
        }
    }
    // 5. the climb
    if (!wall_top(a, e, lo, H, &h)) return;
    if (h > H) return;
    a.f(0x7e0) = 0;
    a.f(0x7dc) = h + 0.6f;
    request(100);
}

}  // namespace

namespace {

// The stuck-escape fan (0x47777b..0x477b84, MOVEPRIM.md §2.2): 18 points 30 ticks of travel
// ahead at facing + 0, -0, +0.3, -0.3 .. +-2.4, each needing a floor within h + 0.5 below it
// and a clear probe from it (along the current heading). A side turns once it has three clear
// decisive probes in a row; "floor but blocked" leaves that side's flags alone. Only the
// heading is written.
bool fan(EnemyAi& a, const AiEnv& e) {
    const Vec3 down{0.0f, -(e.height + 0.5f), 0.0f};
    bool even_cur = false, even_prev = false, odd_cur = false, odd_prev = false;
    float ang = 0.0f;
    for (int k = 2;; ++k) {
        const bool odd = (k & 1) != 0;
        if (odd) ang = -ang;
        const float fac = (a.mode() == 2 || a.mode() == 0) ? a.f(0x820) : a.f(0x81c);
        const float spd = (fac * a.f(0x818)) * 30.0f;
        const float d = e.facing + ang;
        const float dx = float(std::cos(double(d))) * spd, dz = float(std::sin(double(d))) * spd;
        const Vec3 P{e.self.x + dx, e.self.y + 0.0f, e.self.z + dz};
        bool& cur = odd ? odd_cur : even_cur;
        bool& prev = odd ? odd_prev : even_prev;
        const bool ok = prev && cur;
        if (solid(e, P, down)) {
            if (!probe_ahead(a, e, P)) {
                prev = cur; cur = true;
                if (ok) { a.heading = e.facing + ang; return true; }
            }
        } else {
            prev = cur; cur = false;
        }
        if (odd) ang = -ang + 0.3f;
        if (!(2.6179938f > ang)) return false;
    }
}

}  // namespace

// FUN_00477720(approach, walk) (MOVEPRIM.md). approach=0: always move — walk (walk=1) or
// run. approach=1: move only while the target is >= 0.75 away; runs unless the heading is
// locked (+0x704) or walk is asked. speed = +0x818 x (+0x81c walk | +0x820 run): the actor's
// copy of EI+0x94/+0x98/+0x9c, which a brain may rewrite (M910's speed ramp). Blocked (the
// probe from the origin): the fan for +0x130 & 0x800 while grounded and engaged, else a stop.
// The ramp (+0x78c a target factor, +0x824 the accumulated step) is live only for M590.
// [not modelled] the displacement +0xa3c/+0xa44 (read only by the mover's ice blend).
void move(EnemyAi& a, const EnemyAiParams&, const AiEnv& e, int approach, int walk) {
    if (blocked_ahead(a, e)) {
        const Actor* A = e.actor;
        if (e.grounded && a.phase() == 2 && A && (A->flags130 & 0x800u) && fan(a, e)) return;
        stop(a, 0);
        return;
    }
    int m;
    if (!approach) {
        a.speed = a.f(0x818);
        m = walk ? 1 : 2;
    } else {
        a.speed = 0.75f > a.dist() ? 0.0f : a.f(0x818);   // DAT_006bd390; NaN moves
        m = a.speed == 0.0f ? 0 : (a.turn_hold() == 0 && !walk) ? 2 : 1;
    }
    a.mode() = m;
    float f = m == 2 ? a.f(0x820) : m == 1 ? a.f(0x81c) : 0.0f;
    const float R = a.f(0x78c);
    if (R != 0.0f) {                                   // 0x477c8c: the ramp
        const float r = a.f(0x824);
        if (f > R) {
            f = f - r;
            if (f > R) a.f(0x824) = r - a.f(0x830);    // as written: diverges (unreached)
        } else {
            f = f + r;
            if (R > f) a.f(0x824) = a.f(0x82c) + r;
        }
    }
    a.speed *= f;
    a.speed310 = a.speed;                              // 0x477cfc
    // 0x477d40..0x477ef8, the swim block [BYTES]: +0x130 & 0x80 and not a flyer (+0x398), on every move that was
    // not blocked. P = the origin 0.5 ahead with y x 1.5 as compiled (0x477d8c..0x477dde: y + y x 0.5). On sand2
    // (+0x4ba == 0x12): a solid ray from P + 4 down 8 — a hit off sand2 puts the whole origin on the hit point,
    // one on it rides at +0x320 - +0x7ac with +0x7ac + 0.02 a call, back to 0 past 0.5. Else: from P + 2 down 4,
    // y = the hit + 1.0 (+ 0 on sand2). Either hit writes +0x4ba. Only M590 gets here: the Hands (M260 / M410 /
    // M440) never run an action that moves (their specs). +0x7ac is also M590's base turn rate, as written.
    const Actor* A = e.actor;
    if (A && (A->flags130 & 0x80u) && !A->flyer && e.world) {
        Vec3 P{e.self.x + float(std::cos(double(a.heading))) * 0.5f, e.self.y + e.self.y * 0.5f,
               e.self.z + float(std::sin(double(a.heading))) * 0.5f};
        const bool sand = a.surf_ray == 0x12;
        P.y += sand ? 4.0f : 2.0f;
        const AiWorld::Ray r = e.world->ray(P, {0.0f, sand ? -8.0f : -4.0f, 0.0f});
        if (!r.hit) return;
        a.surf_ray = r.surface;
        a.swim_set = true;
        a.swim_origin = e.self;
        if (!sand) {
            a.swim_origin.y = r.point.y + (r.surface != 0x12 ? 1.0f : 0.0f);
        } else if (r.surface != 0x12) {
            a.swim_origin = r.point;
        } else {
            a.swim_origin.y = a.w320() - a.f(0x7ac);
            a.f(0x7ac) = a.f(0x7ac) + 0.02f;
            if (a.f(0x7ac) > 0.5f) a.f(0x7ac) = 0.0f;
        }
    }
}

// FUN_004758f0(+0x708): what the heading steers toward (+0x748).
void steer(EnemyAi& a, const AiEnv& e) {
    const Vec3 to = a.v(0x734);
    switch (a.steer()) {
        case 0:
            if (a.ncrumb() != 0) { a.set_v(0x748, a.v(0x758) - e.self); break; }
            [[fallthrough]];
        case 2: a.set_v(0x748, to); break;
        case 1: a.set_v(0x748, {std::cos(a.heading), 0, std::sin(a.heading)}); break;
        case 3: a.set_v(0x748, {-to.x, -to.y, -to.z}); break;
        case 4: a.set_v(0x748, a.v(0x894) - e.self); break;
        case 6: a.set_v(0x748, {e.target_vel.x * 30.0f + to.x, e.target_vel.y * 30.0f + to.y,
                                e.target_vel.z * 30.0f + to.z}); break;
        default: break;
    }
}

// FUN_00476900: turn the heading toward +0x748 at `rate` (XMM2) per tick, unless locked.
void turn(EnemyAi& a, float rate) {
    if (a.turn_hold() == 0) {
        const Vec3 s = a.v(0x748);
        a.heading = rotate_toward(a.heading, std::atan2(s.z, s.x), rate);
    } else {
        --a.turn_hold();
    }
}

// FUN_00479a70(+0x710). 0 / unknown ids leave the mover as it was.
void act(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e, AiOut* out) {
    switch (a.action()) {
        case 1: stop(a, 1); return;
        case 2: stop(a, 0); return;
        case 0x10: move(a, p, e, 1, 1); break;
        case 0x20: move(a, p, e, 0, 1); break;
        case 0x400: move(a, p, e, 1, 0); break;
        case 0x800: move(a, p, e, 0, 0); break;
        default: return;
    }
    // 0x479aca: the jump helper for +0x12c & 0x400000 (JUMPHELPER.md §1).
    if (out && e.actor && (e.actor->flags12c & 0x400000u)) jump_helper(a, e, *out);
}

void acquire(EnemyAi& a) {   // FUN_004758a0
    if (a.leash() != 0) return;
    a.i(0x6fc) = 0; a.phase() = 2; a.action() = 0; a.dur() = 0; a.cool() = 0; a.ncrumb() = 0;
    a.hit_cut = true;        // +0x118 = 0, as FUN_00475860 [EXPORT 004758a0.c]
}
void disengage(EnemyAi& a) {   // FUN_00475860 (it also zeroes +0x118, the hit record's life)
    a.phase() = 1; a.i(0x6fc) = 0; a.dur() = 0; a.cool() = 0; a.ncrumb() = 0;
    a.hit_cut = true;
}

// FUN_00475a80: sight. Vertical gap <= max_dy, distance^2 <= d2max (XMM2), inside
// +/-cone (XMM3) of the heading, and a clear segment.
bool sees(const EnemyAi& a, const AiEnv& e, float max_dy, float d2max, float cone) {
    const float dy = e.target.y - e.self.y;
    if (std::fabs(dy) > max_dy) return false;
    const float dx = e.target.x - e.self.x, dz = e.target.z - e.self.z;
    if (dx * dx + dy * dy + dz * dz > d2max) return false;
    if (std::fabs(wrap(std::atan2(dz, dx) - a.heading)) > cone) return false;
    return !(e.blocked && e.blocked(e.self, e.target));
}

// FUN_00475c00: keep line of sight, leaving up to 4 breadcrumbs (+0x758, count +0x788)
// of the target's last-seen positions to follow round corners. False = lost.
bool los_keep(EnemyAi& a, const AiEnv& e, float max_dy, float sight) {
    if (e.actor && (e.actor->flags12c & 0x200000u)) return true;   // 0x475c0b
    if (std::fabs(e.target.y - e.self.y) > max_dy) return false;
    auto clear = [&](const Vec3& from) { return !(e.blocked && e.blocked(from, e.target)); };
    auto crumb = [&](int k) { return 0x758 + k * 12; };
    Vec3 from = e.self;
    if (a.ncrumb() != 0) {
        if (clear(e.self)) { a.ncrumb() = 0; return true; }
        from = a.v(crumb(a.ncrumb() - 1));
    }
    if (clear(from)) {
        if (a.ncrumb() < 4) a.set_v(crumb(a.ncrumb()), e.target);   // last seen here
    } else {
        if (a.ncrumb() > 3) { a.ncrumb() = 0; return false; }
        ++a.ncrumb();
    }
    if (a.ncrumb() != 0) {
        const Vec3 d = a.v(crumb(0)) - Vec3{e.self.x, e.self.y - sight, e.self.z};
        if (d.x * d.x + d.y * d.y + d.z * d.z < 1.5625f) {           // DAT_006bd4e0: reached
            --a.ncrumb();
            for (int k = 0; k < 3; ++k) a.set_v(crumb(k), a.v(crumb(k + 1)));
        }
    }
    return true;
}

// FUN_00476f40: idle wander. Phases of (r15>>8)%240 + 90 ticks; a "go" phase picks a
// point within +/-3 of home (+0x790) with floor under it (up to 10 tries) and re-aims
// every 8 ticks until within 1 unit. The walk step runs every tick.
void wander(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    if (a.w_type == 1) { stop(a, 1); return; }
    if (a.w_timer == 0) {
        a.w_phase ^= 1;
        a.w_timer = int((r15(e) >> 8) % 240u) + 90;
        if (a.w_phase == 1 && a.w_type == 0) {
            const Vec3 home = a.v(0x790);
            for (int k = 0; k < 10; ++k) {
                auto unit = [&] {   // (float)(w & 0x7fffff | 0x3f800000) - 1.0f
                    const std::uint32_t bits = (e.rng() & 0x7fffffu) | 0x3f800000u;
                    float x; std::memcpy(&x, &bits, 4); return x - 1.0f;
                };
                a.w_target = {home.x + 6.0f * unit() - 3.0f, home.y, home.z + 6.0f * unit() - 3.0f};
                const Vec3 d = a.w_target - e.self;
                const float l = std::sqrt(dot(d, d));
                const Vec3 probe = l > 0 ? add(a.w_target, {d.x / l, d.y / l, d.z / l}) : a.w_target;
                if (!e.has_floor || e.has_floor(probe.x, probe.z, probe.y, 7.5f)) break;
            }
        }
    } else {
        --a.w_timer;
        if (a.w_phase == 1 && a.w_type == 0 && (a.w_timer & 7) == 0) {
            const float dx = a.w_target.x - e.self.x, dz = a.w_target.z - e.self.z;
            if (dx * dx + dz * dz < 1.0f) a.w_timer = 0;
            else a.heading = std::atan2(dz, dx);
        }
    }
    if (!blocked_ahead(a, e)) { move(a, p, e, 1, 1); return; }
    stop(a, 1);
}

// FUN_00478470 by +0x70c: 0 wander, 1 stand (turning), 2 stand (heading locked).
// FUN_00478470. Every case first runs the flyer hover (0x4784fe / 0x4784de / 0x478496).
void idle(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    const int c = a.i(0x70c);
    if (c >= 0 && c <= 2 && e.actor && e.actor->flyer) hover(a, *e.actor, e, nullptr, a.i(0x720));
    switch (c) {
        case 0: wander(a, p, e); return;
        case 1: stop(a, 0); return;
        case 2: stop(a, 1); return;
        default: return;
    }
}

void hover(EnemyAi& a, Actor& self, const AiEnv& e, const Actor* ref, int mode) {
    if ((a.i(0x724) & 0xff) != 0) return;
    const bool ref_grounded = ref ? true : e.target_grounded;      // +0x388 (actors keep 1)
    if (ref_grounded) a.f(0x754) = ref ? ref->floor_y : e.target_floor;
    const float floor = self.floor_y;
    float base;
    if (std::uint32_t(a.phase()) <= 1u) {
        const float p = self.spawn_y;
        base = p > floor ? (4.0f > p - floor ? p : floor + 4.0f) : floor;
    } else {
        base = a.f(0x754) > floor ? a.f(0x754) : floor;
    }
    const float H = a.f(0x834), rise = a.f(0x838), sink = a.f(0x83c);
    float y = self.pos.y + self.height;                            // the origin, +0x54
    const Vec3 o{self.pos.x, y, self.pos.z};
    // The up-probe is the solid set (sel 0); the down-probe is the water set (sel 1), which
    // the room collision here does not have — so it is never blocked.
    auto up_blocked = [&](float step) {
        return e.blocked && e.blocked(o, {o.x, o.y + e.radius * 1.05f + step, o.z});
    };
    auto store = [&] { self.pos.y = y - self.height; };
    if (mode == 1) {                                               // terrain follow, no bob
        const float T = floor + H;
        if (T > y) {
            if (up_blocked(rise)) return;
            y += rise;
            float s = rise;
            if (floor + H - 0.5f > y) s *= 20.0f;
            y += s;
            if (y > T) y = T;
        } else if (y > T) {
            float s = sink;
            if (y > floor + H + 0.5f) s *= 20.0f;
            y -= s;
            if (T > y) y = T;
        } else {
            y = T;
        }
        store();
        return;
    }
    if (mode == 2) return;
    if (mode == 5) base = floor;
    const float bob = float(std::sin(double(a.f(0x848) * 0.017453292f)));
    y -= bob * a.f(0x840);
    const float G = base + H;
    if (G > y) {
        if (!up_blocked(rise)) { y += rise; if (y > G) y = G; }
    } else if (y > G) {
        y -= sink; if (G > y) y = G;
    } else {
        y = G;
    }
    y += float(std::sin(double(a.f(0x848) * 0.017453292f))) * a.f(0x840);
    a.f(0x848) += a.f(0x844);
    store();
}

// FUN_00479b00: the action player. build/ai_spec/REACT.md §2.
void action_player(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e, AiOut& out) {
    // 0x479b36..0x479b5c: a damaging hit since the last tick -> FUN_00478510 (the spark, from the
    // previous +0x734) then FUN_0043a080(92 player\p_hit0, origin). build/combat_re/HIT_FEEDBACK.md
    if (e.took_damage) {
        const Vec3 last = a.v(0x734);
        out.hit_spark = true;
        out.hit_spark_dx = last.x; out.hit_spark_dz = last.z;
        out.sounds.push_back(92);
    }
    a.set_v(0x734, e.target - e.self);
    const Vec3 to = a.v(0x734);
    a.dist() = std::sqrt(dot(to, to));
    // +0x744: the 3-D distance to the anchor +0x51c, taken before the anchor moves [EXPORT 00479b00.c:98-104].
    const Vec3 from_anchor = e.self - a.anchor;
    a.f(0x744) = std::sqrt(dot(from_anchor, from_anchor));
    if (a.seen_phase() != a.phase()) { a.anchor = e.self; a.set_v(0x790, e.self); a.leash() >>= 2; }
    a.seen_phase() = a.phase();
    const float max_dy = a.f(0x834) * 0.5f + 10.0f;
    if (a.phase() == 1) {
        // The "every 16 / 32 ticks" gates test +0x6fc & 0xf / 0x1f, and nothing
        // increments +0x6fc for an enemy — so in practice they run every tick.
        if (e.bloody || e.target_alive) {                                   // 0x47a472: [0x6d5d92] > 1 || T HP >= 1
            if (sees(a, e, max_dy, 225.0f, 0.785398f)) acquire(a);          // 15 u, +/-45 deg
            if (e.was_hit) acquire(a);
            if (a.dist() < 8.0f && sees(a, e, max_dy, 64.0f, kPi)) acquire(a); // 8 u, all round
        }
    } else if (a.phase() == 3) {
        // The stun hold (0x479ca1): re-request 0x13B unless hurt (4), hard stop; exit after 99.
        int S = e.section;
        if ((a.i(0x724) & 0xff) != 0 && S != 0x13b && S != 4 && request(S, 0x13b, false, e)) out.section = 0x13b;
        a.turn_hold() = 1; a.mode() = 0; a.speed = 0;
        if (a.dur() == 0) {
            a.i(0x724) = 0;                                 // FUN_004522b0 releases the stars
            a.phase() = 2; a.dur() = 0; a.cool() = 0;
            out.launch_clear = true;                        // +0x4d0 = 0
            out.section = 4; out.section_force = true;      // inlined FUN_00446620(4)
        }
    } else if (a.phase() == 2) {
        if (a.action() != -1) {
            // The stun check (0x479dce): +0x3bc & 0x6480, unless stun-immune (+0x12c & 0x80000).
            if (!e.stun_immune && (e.events & 0x6480u) != 0) {
                bool hit = false;
                if (e.events & 0x4000u) hit = (e.rng() >> 17) % 100u <= (e.bless_stun ? 70u : 60u);
                else if (e.events & 0x2000u) hit = (e.rng() >> 17) % 100u <= (e.bless_stun ? 40u : 30u);
                if (e.character == 2) hit = false;          // Hugo never stuns (after the draw)
                if (hit) {                                  // STUN_ENTER (0x479f06)
                    out.effects.push_back({0, {e.self.x, e.self.y, e.self.z}, 1.0f, false});
                    out.effects.push_back({60, {e.self.x, e.self.y, e.self.z}, 0.8f, true});   // the stars
                    a.phase() = 3; a.dur() = 99; a.i(0x724) = 1;
                    int S = e.section;
                    if (request(S, 0x13b, false, e)) out.section = 0x13b;
                    out.sounds.push_back(0xb8);
                }
            }
            // The body below ALSO runs on the stun tick (literal). 0x47a353: a cast with +0x12c & 0x2000, or
            // BloodyEffectLevel > 1, keeps hunting a target whose HP is below 1 [EXPORT 00479b00.c:374-376].
            const std::uint32_t f12c = e.actor ? e.actor->flags12c : 0u;
            if ((f12c & 0x2000u) || e.bloody || e.target_alive) {
                // 0x47a39e, the home leash (+0x12c & 0x800000, M100 only): more than 12.5 [0x6bd950] from the
                // anchor and more than 4.0 [0x6bd7c0] from the target -> disengage, +0x884 = 20.
                if ((f12c & 0x800000u) && a.f(0x744) > 12.5f && a.dist() > 4.0f) { disengage(a); a.leash() = 20; }
                if (a.steer() == 0 && !los_keep(a, e, max_dy, a.f(0x834))) disengage(a);
                steer(a, e);
                turn(a, a.f(0x814));
                if (e.actor && e.actor->flyer) hover(a, *e.actor, e, nullptr, a.i(0x720));   // 0x47a45b
                act(a, p, e, &out);
            } else if (a.cool() == 0) {
                a.phase() = 1;
            }
        }
    }
    if (a.cool() != 0 && a.cool() != -1) --a.cool();
    a.i(0x87c) += 1;                                   // per-tick counter (M910's lunge gate)
    a.i(0x8a0) += 1;                                   // another per-tick counter (0x47a580; M200's tail)
    if (a.action() == -1) {
        if (a.i(0x71c) != 0 && a.i(0x71c) != -1) --a.i(0x71c);   // the brain's own timer (0x47a5e3)
    } else {
        if (a.dur() == 0) { a.action() = 0; a.steer() = 0; a.i(0x824) = 0; }
        else if (a.dur() != -1) --a.dur();
    }
    if (a.leash() != 0) --a.leash();
    // The floor's damage [BYTES 0x47a60a..0x47a6d6]: on blood 0x11 / lava 0x0d (+0x4b9) a cast with +0x130 & 0x40
    // loses max HP x 0.05 [0x6bd13c] on every 20th count of +0x4c8, which counts on those floors only. Under
    // 1 HP it dies there: +0x3bc |= 0x10, HP 0, the death section unless it plays (the gated request), the
    // notes' seen byte. The number is drawn in 0xa000a0 either way.
    if ((a.surf == 0x11 || a.surf == 0x0d) && e.actor && (e.actor->flags130 & 0x40u) != 0) {
        if (a.floor_ticks % 20u == 0) {
            const float dmg = e.max_hp * 0.05f;
            e.actor->hp -= dmg;
            if (1.0f > e.actor->hp) {
                e.actor->hp = 0.0f;
                out.floor_death = true;
                if (e.section != 5) out.section = 5;
            }
            out.popup = int(dmg);
            out.popup_rgb = 0xa000a0;
            a.floor_ticks = 0;
        }
        ++a.floor_ticks;
    }
}

// FUN_004782f0: memset +0x6f4..+0x8ac, keeping the fields it saves around the memset
// (+0x72c, +0x730, +0x7e4..+0x7f8, +0x7fc..+0x804, +0x854, the +0x86c byte); phase = 1;
// home (+0x790) = the anchor. The heading (+0x308) lies outside and survives. The
// ENEMYINFO row +0x90..+0xcc is copied to +0x814..+0x850 by the brains' init branch.
void init_block(EnemyAi& a, const Vec3& home, const float raw[16]) {
    const EnemyAi keep = a;
    for (int off = 0x6f4; off < 0x8ac; off += 4) a.i(off) = 0;
    for (int off : {0x72c, 0x730, 0x7e4, 0x7e8, 0x7ec, 0x7f0, 0x7f4, 0x7f8, 0x7fc, 0x800, 0x804, 0x854})
        a.i(off) = keep.i(off);
    a.blk[(0x86c - EnemyAi::kBase) / 4] = keep.blk[(0x86c - EnemyAi::kBase) / 4];
    a.phase() = 1;
    a.set_v(0x790, home);
    if (raw) for (int k = 0; k < 16; ++k) a.f(0x814 + 4 * k) = raw[k];
    a.initialised = true;
}

void init(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) { init_block(a, e.self, p.raw); }

double angle_diff(float h, float a) {
    constexpr double k2Pi = 6.2831854820251465, kPiD = 3.1415927410125732;
    auto norm = [&](double x) {
        if (x > k2Pi) return std::fmod(x, k2Pi);
        if (x < 0) return std::fmod(x, k2Pi) + k2Pi;
        return x;
    };
    const double d = norm(a) - norm(h);
    if (d < 0) return d >= -kPiD ? d : d + k2Pi;
    return d <= kPiD ? d : d - k2Pi;
}

bool request(int& cur, int id, bool no_restart, const AiEnv& e) {
    const int t = e.section_type ? e.section_type(id) : 0;
    if (t < 0) return false;
    if (no_restart && cur == id) return false;
    const int ct = e.section_type ? e.section_type(cur) : 0;
    if (ct >= 0 && std::uint32_t(ct) > std::uint32_t(t)) return false;
    cur = id;
    return true;
}

bool keep_out(EnemyAi& a, Vec3& origin, const std::function<bool(const Vec3&)>& inside) {
    bool moved = false;
    if (a.phase() != 0 && inside && inside(origin)) {
        const Vec3 prev = a.v(0x808);
        const Vec3 c1{origin.x, prev.y, prev.z};                   // keep only the x move
        const Vec3 c2{prev.x, prev.y, origin.z};                   // keep only the z move
        origin = !inside(c1) ? c1 : !inside(c2) ? c2 : prev;
        moved = true;
    }
    a.set_v(0x808, origin);
    return moved;
}

}  // namespace ai

void arena_clamp(Vec3& o, int mode, const float r[4]) {
    if (mode == 1) {
        if (o.x < r[0]) o.x = r[0]; else if (o.x > r[2]) o.x = r[2];
        if (o.z < r[1]) o.z = r[1]; else if (o.z > r[3]) o.z = r[3];
    } else if (mode == 2) {
        const Vec3 d{o.x - r[0], o.y - r[1], o.z - r[2]};
        const float L = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (L > r[3]) { const float k = r[3] / L; o = {r[0] + d.x * k, r[1] + d.y * k, r[2] + d.z * k}; }
    }
}

AiOut enemy_ai_tick(EnemyAi& a, const EnemyAiParams& p, const AiEnv& env, EnemyBrain brain) {
    const int idx = env.actor ? env.actor->ai_index : -1;
    // M850's handler tests g_flags[130] before anything else (0x4d1c99): a script puppet.
    if (enemy_handler_puppet(idx, env)) return {};
    // The handler's first branch: an actor not yet engaged that is off-screen does nothing.
    if (a.phase() < 2 && env.culled) { ai::stop(a, 1); return {}; }
    // ... then g_flags[131] (0x4d2146): the plain mover only, no brain, no action player.
    if (enemy_handler_held(idx, env)) { AiOut held; held.plain_mover = true; return held; }
    AiOut out = brain ? brain(a, p, env) : AiOut{};
    // What a death hook asked for last tick (EnemyAi::hook_section / hook_kill_tags).
    if (a.hook_section >= 0) {
        if (env.section != a.hook_section && out.section < 0) out.section = a.hook_section;
        a.hook_section = -1;
    }
    for (int t = 0; a.hook_kill_tags != 0; ++t, a.hook_kill_tags >>= 1)
        if (a.hook_kill_tags & 1u) { AiEffect k; k.kill_tag = t; out.effects.push_back(k); }
    if (out.skip_action_player) return out;
    const int phase_after_brain = a.phase();
    AiOut ap;
    if (out.consume_hit) {                             // M160: +0x3bc &= ~4 before FUN_00479b00
        AiEnv e2 = env;
        e2.was_hit = false;
        ai::action_player(a, p, e2, ap);
    } else {
        ai::action_player(a, p, env, ap);
    }
    enemy_handler_post(idx, a, phase_after_brain);
    // The action player's own requests come after the brain's in the engine; it wins a tie.
    if (ap.section >= 0) { out.section = ap.section; out.section_force = ap.section_force; }
    for (const AiEffect& f : ap.effects) out.effects.push_back(f);
    for (int s : ap.sounds) out.sounds.push_back(s);
    if (ap.hit_spark) { out.hit_spark = true; out.hit_spark_dx = ap.hit_spark_dx; out.hit_spark_dz = ap.hit_spark_dz; }
    out.launch_clear |= ap.launch_clear;
    if (ap.popup >= 0) { out.popup = ap.popup; out.popup_rgb = ap.popup_rgb; }
    out.floor_death |= ap.floor_death;
    return out;
}

}  // namespace cleria
