#include "cleria/enemy_ai.hpp"

#include "cleria/actor.hpp"

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

// FUN_00477450: a wall or a ledge ahead along the heading. Grounded actors only. The
// probe reaches radius + 0.5 (min 1.0), x1.5 while unaware and again while stopped.
// A wall hit counts only for a non-engaged actor; then a down-ray of 2 + radius from the
// probe point must find floor. (Water and the +0x130/+0x12c probe variants not modelled.)
bool blocked_ahead(const EnemyAi& a, const AiEnv& e) {
    // FUN_00477450 probes only grounded walkers: not +0x12c & 0x100000, not a flyer (+0x398).
    if (!e.grounded || (e.actor && e.actor->flyer)) return false;
    float L = e.radius > 0.5f ? e.radius + 0.5f : 1.0f;
    if (a.phase() == 1) L *= 1.5f;
    if (a.mode() == 0) L *= 1.5f;
    const Vec3 ahead{e.self.x + std::cos(a.heading) * L, e.self.y, e.self.z + std::sin(a.heading) * L};
    if (e.blocked && e.blocked(e.self, ahead)) return a.phase() != 2;
    if (e.has_floor && !e.has_floor(ahead.x, ahead.z, e.self.y + L - e.height, 2.0f + e.radius))
        return true;
    return false;
}

// FUN_00477720(approach, walk). approach=0: always move — walk (walk=1) or run.
// approach=1: move only while the target is >= 0.75 away; runs unless the heading is
// locked (+0x704) or walk is asked. speed = +0x818 x (+0x81c walk | +0x820 run): the
// actor's copy of EI+0x94/+0x98/+0x9c, which a brain may rewrite (M910's speed ramp).
// [not modelled] the stuck-escape fan (+0x130 & 0x800), speed ramp (+0x78c), edge probe.
void move(EnemyAi& a, const EnemyAiParams&, const AiEnv& e, int approach, int walk) {
    if (blocked_ahead(a, e)) { stop(a, 0); return; }
    int m;
    if (!approach) {
        a.speed = a.f(0x818);
        m = walk ? 1 : 2;
    } else {
        a.speed = a.dist() >= 0.75f ? a.f(0x818) : 0.0f;   // DAT_006bd390
        m = a.speed == 0.0f ? 0 : (a.turn_hold() == 0 && !walk) ? 2 : 1;
    }
    a.mode() = m;
    a.speed *= m == 2 ? a.f(0x820) : m == 1 ? a.f(0x81c) : 0.0f;
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
void act(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    switch (a.action()) {
        case 1: stop(a, 1); break;
        case 2: stop(a, 0); break;
        case 0x10: move(a, p, e, 1, 1); break;
        case 0x20: move(a, p, e, 0, 1); break;
        case 0x400: move(a, p, e, 1, 0); break;
        case 0x800: move(a, p, e, 0, 0); break;
        default: break;
    }
}

void acquire(EnemyAi& a) {   // FUN_004758a0
    if (a.leash() != 0) return;
    a.i(0x6fc) = 0; a.phase() = 2; a.action() = 0; a.dur() = 0; a.cool() = 0; a.ncrumb() = 0;
}
void disengage(EnemyAi& a) {   // FUN_00475860
    a.phase() = 1; a.i(0x6fc) = 0; a.dur() = 0; a.cool() = 0; a.ncrumb() = 0;
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

// FUN_00479b00: the action player.
void action_player(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    a.set_v(0x734, e.target - e.self);
    const Vec3 to = a.v(0x734);
    a.dist() = std::sqrt(dot(to, to));
    if (a.seen_phase() != a.phase()) { a.set_v(0x790, e.self); a.leash() >>= 2; }
    a.seen_phase() = a.phase();
    const float max_dy = a.f(0x834) * 0.5f + 10.0f;
    if (a.phase() == 1) {
        // The "every 16 / 32 ticks" gates test +0x6fc & 0xf / 0x1f, and nothing
        // increments +0x6fc for an enemy — so in practice they run every tick.
        if (e.target_alive) {
            if (sees(a, e, max_dy, 225.0f, 0.785398f)) acquire(a);          // 15 u, +/-45 deg
            if (e.was_hit) acquire(a);
            if (a.dist() < 8.0f && sees(a, e, max_dy, 64.0f, kPi)) acquire(a); // 8 u, all round
        }
    } else if (a.phase() == 2) {
        if (a.action() != -1) {
            // [not modelled] reactive dodge (+0x3bc & 0x6480 -> state 3), home leash.
            if (e.target_alive) {
                if (a.steer() == 0 && !los_keep(a, e, max_dy, a.f(0x834))) disengage(a);
                steer(a, e);
                turn(a, a.f(0x814));
                if (e.actor && e.actor->flyer) hover(a, *e.actor, e, nullptr, a.i(0x720));   // 0x47a45b
                act(a, p, e);
            } else if (a.cool() == 0) {
                a.phase() = 1;
            }
        }
    }
    if (a.cool() != 0 && a.cool() != -1) --a.cool();
    a.i(0x87c) += 1;                                   // per-tick counter (M910's lunge gate)
    if (a.action() != -1) {
        if (a.dur() == 0) { a.action() = 0; a.steer() = 0; a.i(0x824) = 0; }
        else if (a.dur() != -1) --a.dur();
    }
    if (a.leash() != 0) --a.leash();
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

}  // namespace ai

AiOut enemy_ai_tick(EnemyAi& a, const EnemyAiParams& p, const AiEnv& env, EnemyBrain brain) {
    // The handler's first branch: an actor not yet engaged that is off-screen does nothing.
    if (a.phase() < 2 && env.culled) { ai::stop(a, 1); return {}; }
    AiOut out = brain ? brain(a, p, env) : AiOut{};
    ai::action_player(a, p, env);
    return out;
}

}  // namespace cleria
