// cleria-actiontest — the player's action input (src/cleria/player_actions): the attack
// request and combo, the air attacks, the 0x112 tap-step attack, the dash and the double jump,
// against FUN_00465170 / FUN_0046ea00 (build/audit/PLAYER_POLISH_AUDIT.md, COMBO_CHAIN.md).
#include <cmath>
#include <cstdio>

#include "cleria/player_actions.hpp"

using namespace cleria;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[act] %-72s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}
static bool near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e; }

int main(int, char**) {
    {   // the ground combo: a press while actable starts it; mid-swing presses buffer on Reserve
        PlayerActions pa;
        PlayerActions::In in; in.character = 1; in.actable = true;
        in.attack_pressed = true;
        PlayerActions::Out o = pa.tick(in);
        check(o.section == 0x100 && o.new_attack && pa.stage == 1, "press while actable: 0x100, stage 1");
        in.attack_pressed = false; in.actable = false; in.section = 0x100;
        o = pa.tick(in);
        in.attack_pressed = true; in.reserve = true;
        o = pa.tick(in);
        check(o.section == -1 && pa.request, "a press mid-swing with AttackReserve is buffered");
        in.attack_pressed = false; in.cancelable = true;
        o = pa.tick(in);
        check(o.section == 0x103 && pa.stage == 2, "the buffer fires at Cancelable: 0x103");
        PlayerActions pb;
        PlayerActions::In ib; ib.character = 1; ib.actable = true; ib.attack_pressed = true;
        pb.tick(ib); pb.tick(ib);
        check(pb.stage == 1, "a press while actable again restarts the chain (stage 0 -> 1)");
        {   // anime_AttackBreak (vm+0xe5; ASO_NOOPS.md §6 [BYTES 0x465f7f]): a press that cannot be buffered while it
            // is on drops the request and latches +0x6f6 until the next Actable press.
            PlayerActions pk;
            PlayerActions::In ik; ik.character = 0; ik.actable = true; ik.attack_pressed = true;
            pk.tick(ik);
            ik.actable = false; ik.section = 0x116; ik.reserve = false; ik.attack_break = true;
            pk.tick(ik);
            const bool latched = pk.break_latch && !pk.request;
            ik.reserve = true;                                  // AttackReserve comes on ~10 ticks later: too late
            pk.tick(ik);
            const bool refused = !pk.request;
            ik.actable = true; ik.attack_break = false;
            pk.tick(ik);
            check(latched && refused && !pk.break_latch && pk.request == false,
                  "AttackBreak: an early press forfeits the follow-up (latch +0x6f6), cleared by an Actable press");
        }
        {   // Napishtim Adol: the ground combo by bracelet [BYTES 0x68a4fc / 0x68af70]; the air combo is the common one.
            static const int kWant[4][9] = {{0x113, 0x114, 0x115, 0x116, 0x117, 0x118, 0x119, 0x11a, 0x113},
                                            {0x121, 0x122, 0x123, 0x121, 0, 0, 0, 0, 0},
                                            {0x11b, 0x11d, 0x11f, 0x11b, 0, 0, 0, 0, 0},
                                            {0x100, 0x103, 0x104, 0x100, 0, 0, 0, 0, 0}};
            bool ok = true;
            for (int k = 0; k < 4; ++k) {
                PlayerActions pn;
                PlayerActions::In in2; in2.character = 0; in2.adol_slot = k; in2.reserve = true; in2.cancelable = true;
                for (int s = 0; s < 9 && kWant[k][s]; ++s) {
                    in2.actable = (s == 0); in2.attack_pressed = true;
                    ok = ok && pn.tick(in2).section == kWant[k][s];
                }
            }
            PlayerActions pf;
            PlayerActions::In inf; inf.character = 0; inf.reserve = true; inf.cancelable = true;
            int last = 0;
            for (int s = 0; s < 6; ++s) { inf.actable = (s == 0); inf.attack_pressed = true; last = pf.tick(inf).section; }
            PlayerActions py;
            PlayerActions::In iy; iy.character = 1; iy.adol_slot = 0; iy.actable = true; iy.attack_pressed = true;
            check(ok && last == 0x105 && py.tick(iy).section == 0x100,
                  "Napishtim Adol: 8 / 3 / 3 / 3 steps by bracelet; Felghana keeps his six; the slot is character 0's only");
        }
        PlayerActions pc;
        PlayerActions::In ic; ic.character = 1; ic.reserve = true; ic.cancelable = true;
        int secs[5] = {};
        for (int k = 0; k < 5; ++k) {
            ic.actable = (k == 0); ic.attack_pressed = true;
            secs[k] = pc.tick(ic).section;
        }
        check(secs[0] == 0x100 && secs[1] == 0x103 && secs[2] == 0x104 && secs[3] == 0x101 && secs[4] == 0x100,
              "Yunica's chain 100 103 104 101, then wraps");
    }
    {   // the per-swing lunge: 4 ticks later the mover writes channel 1 = facing x 0.1275
        PlayerActions pa;
        PlayerActions::In in; in.character = 3; in.actable = true; in.attack_pressed = true; in.facing = 0.0f;
        pa.tick(in);
        in.attack_pressed = false;
        PlayerActions::Out o;
        int at = 0;
        for (int k = 1; k <= 6 && !at; ++k) { o = pa.tick(in); if (o.lunge) at = k; }
        check(at == 4 && near(o.lunge_x, 0.1275f) && near(o.lunge_z, 0.0f), "the lunge lands 4 ticks after the swing, 0.1275");
    }
    {   // air attacks
        PlayerActions pa;
        PlayerActions::In in; in.character = 1; in.actable = true; in.grounded = false; in.t = 0.2f;
        in.attack_pressed = true;
        PlayerActions::Out o = pa.tick(in);
        check(o.section == 0x109 && pa.stage == 0, "rising, stick neutral: the jump attack 0x109");
        PlayerActions pb;
        in.stick_active = true; in.stick_angle = 0.0f;
        o = pb.tick(in);
        check(o.section == 0x106 && pb.air_combo && near(o.t_add, 0.015f), "rising, stick held: the air combo 0x106 (+0.015)");
        PlayerActions pc;
        PlayerActions::In ic; ic.character = 1; ic.actable = true; ic.grounded = false; ic.t = -0.2f;
        ic.section = 3; ic.attack_pressed = true;
        o = pc.tick(ic);
        check(o.section == 0x10b && pc.thrust, "falling in section 3: the down thrust 0x10b");
    }
    {   // the tap-step attack 0x112: a short tap toward the facing opens a 12-tick window
        PlayerActions pa;
        PlayerActions::In in; in.character = 3; in.actable = true; in.facing = 0.0f;
        pa.tick(in);
        in.stick_active = true; in.stick_angle = 0.1f;
        for (int k = 0; k < 4; ++k) pa.tick(in);
        in.stick_active = false;
        pa.tick(in);
        check(pa.tap_window == 12, "a 4-tick tap then release opens the window");
        in.attack_pressed = true;
        PlayerActions::Out o = pa.tick(in);
        check(o.section == 0x112 && o.impulse && near(o.impulse_x, std::cos(0.1f) * 0.1f, 1e-4f) && pa.step_t == 13,
              "attack in the window: 0x112, +0.1 along the tap, step timer 13");
        PlayerActions pb;
        PlayerActions::In ib; ib.character = 3; ib.actable = true; ib.stick_active = true; ib.stick_angle = 0.0f;
        for (int k = 0; k < 15; ++k) pb.tick(ib);
        ib.stick_active = false;
        pb.tick(ib);
        check(pb.tap_window == 0, "a held direction (> 10 ticks) opens no window");
    }
    {   // the dash and the double jump
        PlayerActions pa;
        PlayerActions::In in; in.character = 1; in.stick_active = true; in.attack_held = true;
        check(!pa.tick(in).locomotion_dash, "no flag 181: no dash");
        in.flag181 = true;
        check(pa.tick(in).locomotion_dash, "flag 181 + attack held + running: section 0x10a");
        in.attack_held = false;
        check(!pa.tick(in).locomotion_dash, "attack released: the dash ends");
        // DashControl ("Double-Tap to Dash"): neutral, a tap, neutral, the same direction again.
        auto taps = [](bool control, int gap, float second) {
            PlayerActions p;
            PlayerActions::In i; i.character = 1; i.flag181 = true; i.dash_control = control;
            p.tick(i);                                   // neutral
            i.stick_active = true; i.stick_angle = 0.5f;
            for (int k = 0; k < 3; ++k) p.tick(i);       // the first tap
            i.stick_active = false;
            for (int k = 0; k < gap; ++k) p.tick(i);     // released
            i.stick_active = true; i.stick_angle = second;
            return p.tick(i).locomotion_dash;
        };
        check(taps(true, 4, 0.5f), "DashControl: a double tap dashes");
        check(!taps(false, 4, 0.5f), "DashControl off: a double tap does not");
        check(!taps(true, 14, 0.5f), "the second tap later than 12 ticks: no dash");
        check(!taps(true, 4, 0.5f + 2.5f), "the second tap more than 2.243995 rad away: no dash");
        PlayerActions pd;
        PlayerActions::In id; id.character = 1; id.flag181 = true; id.dash_control = true;
        pd.tick(id);
        id.stick_active = true; pd.tick(id);
        id.stick_active = false; pd.tick(id);
        id.stick_active = true;
        check(pd.tick(id).locomotion_dash && pd.dash_state == 4 && pd.dash_latch, "the dash: state 4, latched");
        for (int k = 0; k < 30; ++k) pd.tick(id);
        check(pd.tick(id).locomotion_dash, "...kept while the direction is held");
        id.stick_active = false; id.attack_held = true;
        pd.tick(id);
        check(pd.dash && pd.dash_state == 0, "neutral with attack held: the dash flag stays, the machine restarts");
        id.attack_held = false;
        pd.tick(id);
        check(!pd.dash && !pd.dash_latch, "neutral, nothing held: the dash ends");
        PlayerActions pe;
        PlayerActions::In ie; ie.character = 1; ie.flag181 = true; ie.always_dash = true; ie.stick_active = true;
        check(pe.tick(ie).locomotion_dash && pe.dash_state == 4, "AlwaysDashOK: state 4 every tick");
        ie.always_dash = false; pe.dash_reset();
        check(!pe.tick(ie).locomotion_dash, "AlwaysDashOK turned off (the Options row clears it): no dash");
        PlayerActions pb;
        PlayerActions::In ib; ib.character = 1; ib.jump_pressed = true;
        check(pb.tick(ib).jump, "a ground jump");
        ib.grounded = false; ib.t = 0.1f; ib.flag163 = true;
        PlayerActions::Out o = pb.tick(ib);
        check(o.double_jump, "airborne, flag 163, t <= 0.15: the double jump");
        for (int k = 0; k < 8; ++k) o = pb.tick(ib);
        check(!o.double_jump, "only once per airtime");
        ib.landed = true; ib.grounded = true; ib.jump_pressed = false;
        pb.tick(ib);
        check(pb.dj_count == 0 && pb.dj_cool == 6, "landing resets it, with the 6-tick cool-down");
        {   // The jump gate after a landing reads +0x3a0, not the section [EXPORT 00465170.c:243, 1939-1940]: the
            // first grounded tick still counts as air (and the double jump is cooling), the second jumps — while
            // the jump section 3 is still on its landing frames.
            PlayerActions pj;
            PlayerActions::In ij; ij.character = 3; ij.flag163 = true; ij.jump_pressed = true;
            const bool j0 = pj.tick(ij).jump;                        // off the ground
            ij.jump_pressed = false; ij.grounded = false; ij.section = 3; ij.t = -0.3f;
            for (int k = 0; k < 20; ++k) pj.tick(ij);
            ij.grounded = true; ij.landed = true; ij.jump_pressed = true;   // the first grounded tick, section 3
            const PlayerActions::Out a1 = pj.tick(ij);
            ij.landed = false;                                       // the second, still section 3
            const PlayerActions::Out a2 = pj.tick(ij);
            check(j0 && !a1.jump && !a1.double_jump && a2.jump,
                  "a press on the landing tick is refused, on the next it jumps (section 3 still playing)");
            PlayerActions pk;
            PlayerActions::In ik; ik.character = 3; ik.jump_pressed = true;
            pk.tick(ik);
            ik.jump_pressed = false; ik.grounded = false; ik.section = 3; ik.movable = false;
            pk.tick(ik);
            ik.grounded = true; ik.jump_pressed = true;
            check(!pk.tick(ik).jump && pk.loco_air, "Movable 0 keeps +0x3a0: the state is not rewritten");
        }
        check(near(dash_char_const(1, false), 1.4f) && near(dash_char_const(3, false), 1.55f),
              "the dash constants (Yunica 1.4, Toal 1.55)");
    }
    std::printf("[act] %s (%d failed)\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
