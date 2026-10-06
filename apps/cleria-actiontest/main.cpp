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
        check(near(dash_char_const(1, false), 1.4f) && near(dash_char_const(3, false), 1.55f),
              "the dash constants (Yunica 1.4, Toal 1.55)");
    }
    std::printf("[act] %s (%d failed)\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
