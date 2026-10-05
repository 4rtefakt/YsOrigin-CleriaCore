#include "cleria/enemy_ai.hpp"

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
std::uint32_t r15(const AiEnv& e) { return e.rng() >> 17; }   // the engine's (w >> 17)

// FUN_004776e0(hold): stop, and lock the heading for `hold` ticks.
void stop(EnemyAi& ai, int hold) {
    ai.turn_hold = hold;
    ai.mode = 0;
    ai.speed = 0;
}

// FUN_00477450: a wall or a ledge ahead along the heading. Grounded actors only. The
// probe reaches radius + 0.5 (min 1.0), x1.5 while unaware and again while stopped.
// A wall hit counts only for a non-engaged actor; then a down-ray of 2 + radius from the
// probe point must find floor. (Water and the +0x130/+0x12c probe variants not modelled.)
bool blocked_ahead(const EnemyAi& ai, const AiEnv& e) {
    if (!e.grounded) return false;
    float L = e.radius > 0.5f ? e.radius + 0.5f : 1.0f;
    if (ai.phase == 1) L *= 1.5f;
    if (ai.mode == 0) L *= 1.5f;
    const float c = std::cos(ai.heading), s = std::sin(ai.heading);
    const Vec3 ahead{e.self.x + c * L, e.self.y, e.self.z + s * L};
    if (e.blocked && e.blocked(e.self, ahead)) return ai.phase != 2;
    if (e.has_floor && !e.has_floor(ahead.x, ahead.z, e.self.y + L - e.height, 2.0f + e.radius))
        return true;
    return false;
}

// FUN_00477720(approach, walk): the move primitive.
//   approach=0: always move — walk (walk=1) or run (walk=0).
//   approach=1: move only while the target is >= 0.75 away; runs unless the heading is
//               locked (+0x704) or walk is asked, then walks.
// speed = EI+0x94 x (EI+0x98 walk | EI+0x9c run). [not modelled] the stuck-escape fan
// search (+0x130 & 0x800), the speed ramp (+0x78c), the edge probe (+0x130 & 0x80).
void move(EnemyAi& ai, const EnemyAiParams& p, const AiEnv& e, int approach, int walk) {
    if (blocked_ahead(ai, e)) { stop(ai, 0); return; }
    int m;
    if (!approach) {
        ai.speed = p.base;
        m = walk ? 1 : 2;
    } else {
        ai.speed = ai.dist >= 0.75f ? p.base : 0.0f;   // DAT_006bd390
        if (ai.speed == 0.0f) m = 0;
        else m = (ai.turn_hold == 0 && !walk) ? 2 : 1;
    }
    ai.mode = m;
    ai.speed *= m == 2 ? p.run_f : m == 1 ? p.walk_f : 0.0f;
}

// FUN_004758f0(mode): what the heading steers toward (+0x748).
void steer(EnemyAi& ai, const AiEnv& e) {
    switch (ai.steer) {
        case 0:
            if (ai.ncrumb != 0) { ai.steer_vec = ai.crumbs[0] - e.self; break; }
            [[fallthrough]];
        case 2: ai.steer_vec = ai.to_target; break;                       // at the target
        case 1: ai.steer_vec = {std::cos(ai.heading), 0, std::sin(ai.heading)}; break;
        case 3: ai.steer_vec = {-ai.to_target.x, -ai.to_target.y, -ai.to_target.z}; break;
        case 4: ai.steer_vec = ai.waypoint - e.self; break;
        case 6: ai.steer_vec = {e.target_vel.x * 30.0f + ai.to_target.x,       // lead the target
                                e.target_vel.y * 30.0f + ai.to_target.y,
                                e.target_vel.z * 30.0f + ai.to_target.z}; break;
        default: break;
    }
}

// FUN_00476900: turn the heading toward +0x748 at EI+0x90 rad/tick, unless locked.
void turn(EnemyAi& ai, const EnemyAiParams& p) {
    if (ai.turn_hold == 0)
        ai.heading = rotate_toward(ai.heading, std::atan2(ai.steer_vec.z, ai.steer_vec.x), p.turn);
    else
        --ai.turn_hold;
}

// FUN_00479a70: the action vocabulary. 0 / unknown ids leave the mover as it was.
void act(EnemyAi& ai, const EnemyAiParams& p, const AiEnv& e) {
    switch (ai.action) {
        case 1: stop(ai, 1); break;           // stand, heading locked
        case 2: stop(ai, 0); break;           // stand, keep turning
        case 0x10: move(ai, p, e, 1, 1); break;
        case 0x20: move(ai, p, e, 0, 1); break;
        case 0x400: move(ai, p, e, 1, 0); break;   // approach (runs)
        case 0x800: move(ai, p, e, 0, 0); break;
        default: break;
    }
}

void acquire(EnemyAi& ai) {   // FUN_004758a0
    if (ai.leash != 0) return;
    ai.phase = 2; ai.action = 0; ai.dur = 0; ai.cool = 0; ai.ncrumb = 0;
}
void disengage(EnemyAi& ai) {   // FUN_00475860
    ai.phase = 1; ai.dur = 0; ai.cool = 0; ai.ncrumb = 0;
}

// FUN_00475a80: sight. Vertical gap <= max_dy, distance^2 <= d2max, inside +/-cone of
// the heading, and a clear segment.
bool sees(const EnemyAi& ai, const AiEnv& e, float max_dy, float d2max, float cone) {
    const float dy = e.target.y - e.self.y;
    if (std::fabs(dy) > max_dy) return false;
    const float dx = e.target.x - e.self.x, dz = e.target.z - e.self.z;
    if (dx * dx + dy * dy + dz * dz > d2max) return false;
    if (std::fabs(wrap(std::atan2(dz, dx) - ai.heading)) > cone) return false;
    return !(e.blocked && e.blocked(e.self, e.target));
}

// FUN_00475c00: keep line of sight, leaving up to 4 breadcrumbs (the target's last-seen
// positions) to follow round corners. False = lost.
bool los_keep(EnemyAi& ai, const AiEnv& e, float max_dy, float sight) {
    if (std::fabs(e.target.y - e.self.y) > max_dy) return false;
    auto clear = [&](const Vec3& a) { return !(e.blocked && e.blocked(a, e.target)); };
    Vec3 from = e.self;
    if (ai.ncrumb != 0) {
        if (clear(e.self)) { ai.ncrumb = 0; return true; }
        from = ai.crumbs[ai.ncrumb - 1];
    }
    if (clear(from)) {
        if (ai.ncrumb < 4) ai.crumbs[ai.ncrumb] = e.target;   // where it was last seen
    } else {
        if (ai.ncrumb > 3) { ai.ncrumb = 0; return false; }
        ++ai.ncrumb;
    }
    if (ai.ncrumb != 0) {
        const Vec3 d = ai.crumbs[0] - Vec3{e.self.x, e.self.y - sight, e.self.z};
        if (d.x * d.x + d.y * d.y + d.z * d.z < 1.5625f) {        // DAT_006bd4e0: reached it
            --ai.ncrumb;
            for (int i = 0; i < 3; ++i) ai.crumbs[i] = ai.crumbs[i + 1];
        }
    }
    return true;
}

// FUN_00476f40: idle wander. Alternating phases of (r15>>8)%240 + 90 ticks; a "go"
// phase picks a point within +/-3 of home that has floor under it (up to 10 tries) and
// re-aims at it every 8 ticks until within 1 unit. The walk step runs every tick.
void wander(EnemyAi& ai, const EnemyAiParams& p, const AiEnv& e) {
    if (ai.w_type == 1) { stop(ai, 1); return; }
    if (ai.w_timer == 0) {
        ai.w_phase ^= 1;
        ai.w_timer = int((r15(e) >> 8) % 240u) + 90;
        if (ai.w_phase == 1 && ai.w_type == 0) {
            for (int i = 0; i < 10; ++i) {
                auto unit = [&] {   // (float)(w & 0x7fffff | 0x3f800000) - 1.0f
                    const std::uint32_t w = e.rng();
                    const std::uint32_t bits = (w & 0x7fffffu) | 0x3f800000u;
                    float f; std::memcpy(&f, &bits, 4); return f - 1.0f;
                };
                ai.w_target = {ai.home.x + 6.0f * unit() - 3.0f, ai.home.y,
                               ai.home.z + 6.0f * unit() - 3.0f};
                Vec3 d = ai.w_target - e.self;
                const float l = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                const Vec3 n = l > 0 ? Vec3{d.x / l, d.y / l, d.z / l} : Vec3{};
                const Vec3 probe{ai.w_target.x + n.x, ai.w_target.y + n.y, ai.w_target.z + n.z};
                if (!e.has_floor || e.has_floor(probe.x, probe.z, probe.y, 7.5f)) break;
            }
        }
    } else {
        --ai.w_timer;
        if (ai.w_phase == 1 && ai.w_type == 0 && (ai.w_timer & 7) == 0) {
            const float dx = ai.w_target.x - e.self.x, dz = ai.w_target.z - e.self.z;
            if (dx * dx + dz * dz < 1.0f) ai.w_timer = 0;
            else ai.heading = std::atan2(dz, dx);
        }
    }
    if (!blocked_ahead(ai, e)) { move(ai, p, e, 1, 1); return; }
    stop(ai, 1);
}

// FUN_00479b00: the action player.
void action_player(EnemyAi& ai, const EnemyAiParams& p, const AiEnv& e) {
    ai.to_target = e.target - e.self;
    ai.dist = std::sqrt(dot(ai.to_target, ai.to_target));
    if (ai.seen_phase != ai.phase) { ai.home = e.self; ai.leash >>= 2; }
    ai.seen_phase = ai.phase;
    const float max_dy = p.sight * 0.5f + 10.0f;
    if (ai.phase == 1) {
        // The "every 16 / 32 ticks" gates test +0x6fc & 0xf / 0x1f, and nothing increments
        // +0x6fc for an enemy (export-wide grep) — so in practice they run every tick.
        if (e.target_alive) {
            if (sees(ai, e, max_dy, 225.0f, 0.785398f)) acquire(ai);          // 15u, +/-45 deg
            if (e.was_hit) acquire(ai);
            if (ai.dist < 8.0f && sees(ai, e, max_dy, 64.0f, kPi)) acquire(ai); // 8u, all round
        }
    } else if (ai.phase == 2) {
        if (ai.action != -1) {
            // [not modelled] the reactive dodge (+0x3bc & 0x6480 -> state 3) and the
            // home leash (+0x12c & 0x800000).
            if (e.target_alive) {
                if (ai.steer == 0 && !los_keep(ai, e, max_dy, p.sight)) disengage(ai);
                steer(ai, e);
                turn(ai, p);
                act(ai, p, e);
            } else if (ai.cool == 0) {
                ai.phase = 1;
            }
        }
    }
    if (ai.cool != 0 && ai.cool != -1) --ai.cool;
    if (ai.action != -1) {
        if (ai.dur == 0) { ai.action = 0; ai.steer = 0; }
        else if (ai.dur != -1) --ai.dur;
    }
    if (ai.leash != 0) --ai.leash;
}

// FUN_0047adf0: the M100 (Riffligan) brain. Runs BEFORE the action player, so it reads
// the previous tick's +0x734/+0x740.
AiOut brain_m100(EnemyAi& ai, const EnemyAiParams& p, const AiEnv& e) {
    AiOut out;
    const int d = e.difficulty;
    const float d2 = ai.to_target.x * ai.to_target.x + ai.to_target.z * ai.to_target.z;  // planar
    bool can_attack = true;
    if (std::fabs(e.target.y - e.self.y) >= 2.0f && e.target_grounded) can_attack = false;  // 0x47af98
    const std::uint32_t R = r15(e);
    int pmax = int(R % 3u) - d + 4;          // the "patience" budget
    if (d < 0) pmax -= d;
    if (ai.phase == 0) {                      // brain init (FUN_004782f0 sets phase 1)
        // FUN_004782f0 memsets +0x6f4..+0x8ac; the heading (+0x308) and the wander type
        // (+0x424) lie outside it and survive — an enemy keeps its placed facing.
        const float heading = ai.heading;
        const int w_type = ai.w_type;
        ai = EnemyAi{};
        ai.heading = heading;
        ai.w_type = w_type;
        ai.phase = 1;
        ai.home = e.self;
        ai.patience = 1;
        ai.initialised = true;
        return out;
    }
    if (ai.phase == 1) { wander(ai, p, e); ai.sub = 0; return out; }
    if (ai.phase != 2) return out;

    if (ai.dist > 20.0f) disengage(ai);       // DAT_006bd994
    const float l = std::sqrt(dot(ai.to_target, ai.to_target));
    const Vec3 dir = l > 0 ? Vec3{ai.to_target.x / l, ai.to_target.y / l, ai.to_target.z / l} : Vec3{};
    const bool not_front = std::cos(ai.heading) * dir.x + std::sin(ai.heading) * dir.z < 0.001f;
    if (d2 > 2.5f || not_front) can_attack = false;   // DAT_006bd624 reach, DAT_006bcff0 cone

    if (ai.sub == 0) {
        if (!can_attack) {
            if (ai.t7a0 == 0 && ai.t79c == 0) {
                if (!not_front) {                       // in front, out of reach: approach
                    ai.action = 0x400; ai.steer = 0; ai.dur = 60; ai.t7a0 = 60; ai.patience = 0;
                } else {
                    ai.facing = 0;
                    const std::uint32_t R2 = r15(e);
                    if ((d == -2 || int(R2) % (d + 4) == 0) && ai.patience < pmax / 2) {
                        ai.action = 1; ai.steer = 1;           // hold, heading locked
                        ai.dur = ai.t7a0 = int(r15(e) % 15u) + 30;
                        ++ai.patience;
                    } else {
                        ai.action = 2;                         // stand, turning
                        if (ai.patience < pmax) {              // ... toward a +/-45 deg waypoint
                            ai.steer = 4;
                            Vec3 v = ai.steer_vec;
                            if (dot(v, v) < 1e-6f) v = {std::cos(ai.heading), 0, std::sin(ai.heading)};
                            const float a = float(int(r15(e) % 90u) - 45) * kPi / 180.0f;
                            const float ca = std::cos(a), sa = std::sin(a);
                            const Vec3 r{v.x * ca + sa * v.z, v.y, ca * v.z - v.x * sa};
                            ai.waypoint = {e.self.x + r.x * 30.0f, e.self.y + r.y * 30.0f,
                                           e.self.z + r.z * 30.0f};
                            ai.dur = ai.t7a0 = int(r15(e) % 15u) + 30;
                            ++ai.patience;
                        } else {
                            ai.steer = 0;
                        }
                    }
                }
            }
        } else {
            if (ai.t79c == 0) {                       // in reach and in front: pre-attack hold
                ai.action = 2; ai.steer = 0; ai.t79c = 10; ai.dur = 10; ai.sub = 10;
            }
            ai.t7a0 = 0;
            ai.patience = 0;
        }
    } else if (ai.sub == 10) {
        if (ai.t79c == 0) {
            if (ai.facing < 30) {                     // aim until in front 30 ticks running
                const int w = 30 - ai.facing;
                ai.t79c = w; ai.action = 1; ai.steer = 1; ai.dur = w;
            }
            ai.sub = 11;
        }
    }
    if (ai.sub == 11 && ai.t79c == 0) {                // strike: section 7, then cool down
        out.attack = true;
        ai.action = 1;
        const int cd = int(r15(e) % 40u) + 100 - 10 * d;
        ai.dur = ai.cool = ai.t79c = cd;
        ai.sub = 0;
    }
    if (ai.t79c != 0) --ai.t79c;
    if (ai.t7a0 != 0) --ai.t7a0;
    if (!not_front) ++ai.facing; else ai.facing = 0;
    return out;
}

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

AiOut enemy_ai_tick_m100(EnemyAi& ai, const EnemyAiParams& p, const AiEnv& env) {
    // FUN_0047b7f0: an actor not yet engaged that is off-screen does nothing at all.
    if (ai.phase < 2 && env.culled) { stop(ai, 1); return {}; }
    const AiOut out = brain_m100(ai, p, env);
    action_player(ai, p, env);
    return out;
}

}  // namespace cleria
