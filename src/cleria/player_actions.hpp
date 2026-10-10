// The player's action input — the attack request / combo, the air attacks, the 0x112 tap-step
// attack, the dash state and the double jump — as pure state, from the player mover
// FUN_00465170 and the attack dispatcher FUN_0046ea00 (build/audit/PLAYER_POLISH_AUDIT.md
// movement items 1-9; build/combat_re/COMBO_CHAIN.md). The host feeds the input and the ASO
// flags each tick and applies the requests (SetSection, the timer adds, the impulses).
//
// Offsets are the player actor's: +0x6f0 stage, +0x6f4 request, +0x76c air combo, +0x6f7
// thrust, +0x730/+0x72c the per-swing lunge, +0x710/+0x714/+0x718 the tap machine and +0x70c
// its window, +0x400 the step timer, +0xa00 the dash, +0x77c/+0x780 the double jump.
#pragma once
#include <cstdint>

namespace cleria {

struct PlayerActions {
    // ---- state ----
    int stage = 0;               // +0x6f0
    bool request = false;        // +0x6f4
    bool air_combo = false;      // +0x76c
    bool thrust = false;         // +0x6f7
    int lunge_t = 0;             // +0x730 (counts down; at 0 the lunge is written)
    float lunge_v = 0;           // +0x72c
    float lunge_angle = 0;
    int tap_state = 0, tap_t = 0;   // +0x710 / +0x718 (state 4 = the mouse's double-click window)
    float tap_angle = 0;            // +0x714
    int tap_window = 0;             // +0x70c (12 ticks)
    int step_t = 0;                 // +0x400 (the 0x112 lunge timer, 13 ticks)
    bool dash = false;              // +0xa00
    int dash_state = 0, dash_t = 0; // +0x720 / +0x728: the double-tap dash machine (4 = dashing by it)
    float dash_angle = 0;           // +0x724: the first tap's stick angle
    bool dash_latch = false;        // DAT_0076c1c4: a double-tap dash is on
    int dj_count = 0, dj_cool = 0;  // +0x77c / +0x780
    bool stick_was_active = false;
    bool break_latch = false;       // +0x6f6: an AttackBreak cancelled the buffer

    struct In {
        int character = 1;           // 0 Adol, 1 Yunica, 2 Hugo, 3 Toal
        bool attack_pressed = false; // +0x50c & 1 (this tick's edge)
        bool attack_held = false;    // +0x510 & 1
        bool skill_held = false;     // +0x510 & 4
        bool jump_pressed = false;   // +0x50c & 2
        bool stick_active = false;   // a direction is held (magnitude past 0.25)
        float stick_angle = 0;       // its world angle (the engine heading basis)
        float facing = 0;            // +0x314 in the same basis
        int section = 0;             // +0x994
        bool actable = false, cancelable = false, reserve = false, attack_break = false;   // ASO
        bool jump_enable = true;     // ASO +0x9d1
        bool grounded = true, submerged = false;
        float t = 0;                 // the vertical timer +0x330
        bool landed = false;         // this tick's touch-down
        bool flag181 = false;        // the dash (Silver Bracelet)
        bool flag163 = false;        // the double jump
        bool always_dash = false;    // option AlwaysDashOK (0x6d6070)
        bool dash_control = false;   // option DashControl (0x6d606e, "Double-Tap to Dash"; retail's default is on)
        int kind = 0;                // +0x404
        bool busy = false;           // g_flags[161] / 151 == -1
        bool yunica_form = true;     // g_flags[149] (DAT_0076bb70) != 0: her air-combo timer adds
        // The mouse (build/shell_re/MOUSE.md §4.2): mouse mode 0x7325d4, the double-click edge
        // (pressed 0x800) and the player -> cursor angle 0x7325fc, here in the stick_angle basis.
        bool mouse_mode = false;
        bool double_click = false;
        float mouse_angle = 0;
    };
    struct Out {
        int section = -1;            // a SetSection request (-1 none)
        bool new_attack = false;     // a new swing: a fresh hit serial, atk_dir = facing
        bool face = false; float face_angle = 0;   // snap the facing (+0x308 / +0x314)
        float t_add = 0;             // hang time added to the vertical timer (air combo)
        bool impulse = false; float impulse_x = 0, impulse_z = 0;   // added to knock channel 1
        bool lunge = false; float lunge_x = 0, lunge_z = 0;         // WRITTEN to knock channel 1
        bool jump = false;           // a ground jump (t = v0, origin += t)
        bool double_jump = false;    // section 3 restarts, t = v0, origin += t
        bool locomotion_dash = false;  // play 0x10a instead of the run section 1
    };

    // One mover tick (before the integrator). `ground` / `air` combo tables per character.
    Out tick(const In& in);
    // The Options rows that change DashControl, turn AlwaysDashOK off or reset to Default clear the dash
    // (FUN_005ba2b0 [EXPORT 005ba2b0.c:225-269]: +0xa00, +0x728, +0x720 and DAT_0076c1c4 = 0).
    void dash_reset() { dash = false; dash_state = 0; dash_t = 0; dash_latch = false; }

  private:
    void dispatch(const In& in, Out& o, int ch);   // FUN_0046ea00(0)
};

// The dash run constant (the run x2.2 multiplier's per-character factor in 0x10a).
float dash_char_const(int character, bool blessing_2000, bool hugo_7bc = false);

}  // namespace cleria
