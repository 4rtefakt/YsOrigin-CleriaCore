// M510 Mobre — brain FUN_00488a90 (handler FUN_00489680 = M100's minus the arena clamp,
// AI index 44). Spec: build/ai_spec/M510.md.
//
// It runs at the player; inside 4.25 u it stands and faces it, inside 2.25 (or after an
// attack) it runs away until beyond 4.25 (cornered past 30 ticks, its cooldown drops 5 extra
// a tick). Off cooldown, grounded and level (|dy| < 2.5) it turns on the spot (action -1,
// rate x max(1, n/20)) until within +/-90 deg, then rolls 15% a tick for section 7 (a
// refused request just re-rolls): hold 110 (125 Easy), cooldown + 30..59 above Normal or
// + 90..119. In section 7, every 7th marker-1 tick a disc (effect 317) sweeps from -40 deg to
// +60 deg across its heading (speed 0.06, 0.09 above Normal); above Normal its heading
// tracks the target at 0.012 rad/tick. Durable in sections 7/8 (the VM's section start
// clears it again). The jump helper's section-0x13a launch: K 0.1, no horizontal push.
// [not modelled] water (+0x38a/+0x38b: the flailing, the HP drain and popups — the room
// collision has no water set); the keep-out box restore (FUN_0042dc90).
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m510(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;
    bool can_atk = true;
    if (e.target_grounded && std::fabs(e.target.y - e.self.y) >= 2.5f) can_atk = false;
    const int d = difficulty(e);
    if (a.phase() != 0) a.f(0x820) = a.f(0x7b0);                  // restore the run multiplier
    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            a.f(0x7ac) = a.f(0x814); a.f(0x7b0) = a.f(0x820);
            return out;
        case 1:
            ai::idle(a, p, e);
            return out;
        case 2: break;
        default: return out;
    }
    const float dist = a.dist();
    // TAIL (0x489586), every phase-2 tick: the jump launch, K 0.1, no horizontal push.
    Actor* A = e.actor;
    auto tail = [&]() -> AiOut {
        if (A && launch_gate(e)) launch(*A, a.f(0x7dc) * 0.1f, out);
        return out;
    };
    if (dist > 20.0f) ai::disengage(a);
    a.steer() = 0;
    a.action() = 0x400;
    int S = e.section;
    if (e.diff_index >= 0 && (S == 7 || S == 8)) out.set_durable = 1;   // sticky super-armour
    if (S == 7) {                                                  // B: the attack running
        a.steer() = 1; a.action() = 1;
        if (d >= 1) {
            if (a.ncrumb() != 0) a.set_v(0x748, a.v(0x758) - e.self);
            else a.set_v(0x748, a.v(0x734));
            const int s = a.turn_hold(); a.turn_hold() = 0;
            ai::turn(a, a.f(0x814) * 0.2f);
            a.turn_hold() = s;
        }
        if (e.aso_marker == 1) {
            const std::uint32_t n = std::uint32_t(++a.i(0x7a0));
            if (n % 7u == 1u) {
                const float th = (2.0f * float(n)) * 0.0174533f + (a.heading - 0.6981317f);
                const float c = float(std::cos(double(th))), s = float(std::sin(double(th)));
                AiBullet b;
                b.effect = 0x13d;                                  // m_510_attack01
                b.pos = {e.self.x + c * 0.75f, e.self.y - 0.35f + 0.0f, e.self.z + s * 0.75f};
                b.dir = {c, 0, s};
                b.speed = d > 0 ? 0.09f : 0.06f;
                b.accel = -0.0008f;
                out.bullets.push_back(b);
            }
            return tail();
        }
        a.i(0x7a0) = 0;
        return tail();
    }
    if (a.i(0x728) == 100) {
        if ((S != 0x13a || std::uint32_t(e.aso_marker) >= 2u) && e.grounded) a.i(0x728) = 0;
        return tail();
    }
    switch (a.i(0x72c)) {
        case 0:                                                    // approach / hold / retreat
            if (dist > 4.25f) { a.i(0x79c) = 0; break; }
            if (a.cool() == 0 && e.grounded && can_atk) { a.i(0x72c) = 1; break; }
            if (dist < 2.25f || a.i(0x79c) != 0) {
                a.steer() = 3;
                const std::uint32_t n = std::uint32_t(++a.i(0x79c));
                if (dist < 2.25f && n > 30u && std::uint32_t(a.cool()) >= 5u) a.cool() -= 5;
            } else {
                a.i(0x79c) = 0; a.action() = 2;
            }
            break;
        case 1: {                                                  // turn to face, then attack
            a.steer() = 0;
            const int n = ++a.i(0x79c);
            a.action() = -1;
            ai::stop(a, 0);
            ai::steer(a, e);
            ai::turn(a, std::max(1.0f, float(n) / 20.0f) * a.f(0x814));
            a.mode() = 1;                                          // walk in place
            const Vec3 to = a.v(0x734);
            const float ang = float(std::atan2(double(to.z), double(to.x)));
            if (float(std::fabs(ai::angle_diff(a.heading, ang))) < 1.5707964f) { a.i(0x72c) = 2; a.i(0x79c) = 0; }
            else if (dist > 4.25f) { a.i(0x72c) = 0; a.i(0x79c) = 0; }
            break;
        }
        case 2:                                                    // the fire roll
            if (a.i(0x79c) != 0) { a.i(0x79c) = 1; a.i(0x72c) = 0; break; }
            if (r15(e) % 1000u >= 150u) break;
            if (!ai::request(S, 7, false, e)) break;               // refused: re-roll next tick
            out.section = 7; out.attack = true;
            a.i(0x79c) = 1;
            a.action() = 1;
            a.dur() = e.diff_easy ? 125 : 110;
            a.cool() = int(r15(e) % 30u) + (d > 0 ? 30 : 90) + a.dur();
            a.i(0x7a0) = 0;
            break;
        default:
            break;
    }
    return tail();
}

}  // namespace cleria::brains
