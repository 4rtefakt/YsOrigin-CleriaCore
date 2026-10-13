#include "cleria/player_actions.hpp"

#include <cmath>

namespace cleria {

namespace {
// 0x732664[char] -> u16 lists, lengths 0x732678 = {6, 4, 2, 5} (COMBO_CHAIN.md §1).
const int kGround[4][7] = {{0x100, 0x103, 0x104, 0x101, 0x102, 0x105, 0x109},
                           {0x100, 0x103, 0x104, 0x101, 0x106, 0x107, 0x108},
                           {0x100, 0x103, 0, 0, 0, 0, 0},
                           {0x100, 0x103, 0x104, 0x101, 0x102, 0, 0}};
const int kGroundLen[4] = {6, 4, 2, 5};
const int kAir[3] = {0x106, 0x107, 0x108};   // 0x732674, length 0x73267c = 3: the AIR combo
// Napishtim Adol's ground combos by bracelet slot [BYTES: 0x68a4fc[k] -> 0x68af88 / 0x68af78 / 0x68af80 / 0x68af98,
// lengths u8 0x68af70; selection 0x46ef69..0x46ef8f, length 0x46f361..]; his air combo is the common one.
const int kNapGround[4][8] = {{0x113, 0x114, 0x115, 0x116, 0x117, 0x118, 0x119, 0x11a},
                              {0x121, 0x122, 0x123, 0, 0, 0, 0, 0},
                              {0x11b, 0x11d, 0x11f, 0, 0, 0, 0, 0},
                              {0x100, 0x103, 0x104, 0, 0, 0, 0, 0}};
const int kNapLen[4] = {8, 3, 3, 3};
constexpr float kPi = 3.14159265f, k2Pi = 6.2831853f;
float wrap(float a) { a = std::fmod(a + kPi, k2Pi); if (a < 0) a += k2Pi; return a - kPi; }
}  // namespace

float dash_char_const(int character, bool blessing_2000, bool hugo_7bc) {
    switch (character) {   // 0x6bd4b4/0x6bd4d4, 0x6bd4a0/0x6bd4b0 (Hugo +0x7bc 0x6bd4ac), 0x6bd4dc, 0x6bd4d0
        case 1: return blessing_2000 ? 1.5f : 1.4f;
        case 2: return hugo_7bc ? (blessing_2000 ? 1.45f : 1.35f) : (blessing_2000 ? 1.375f : 1.275f);
        case 3: return 1.55f;
        default: return 1.45f;
    }
}

PlayerActions::Out PlayerActions::tick(const In& in) {
    Out o;
    const int ch = (in.character >= 0 && in.character < 4) ? in.character : 1;
    const bool press_edge = in.stick_active && !stick_was_active;

    // Counters: +0x780 (0x4680a8), +0x400 (FUN_0045c490), the lunge +0x730.
    if (dj_cool > 0) --dj_cool;
    if (step_t > 0) --step_t;
    if (lunge_t > 0 && --lunge_t == 0) {
        // The mover WRITES knock channel 1 = (cos, sin)(facing) * +0x72c (0x468041..0x4680a0).
        o.lunge = true;
        o.lunge_x = std::cos(in.facing) * lunge_v;
        o.lunge_z = std::sin(in.facing) * lunge_v;
    }
    if (in.landed) {                       // 0046fba0.c:118-197
        air_combo = false; thrust = false;
        dj_count = 0; dj_cool = 6;
    }

    // ---- the dash machine +0x720 / +0x724 / +0x728 (0x4651f3.., 0x4654a0..0x465641) ----
    // Mover state 0xd: a latched dash (DAT_0076c1c4) or AlwaysDashOK re-arms it every tick.
    if (in.flag181 && ((dash_latch && !dash) || in.always_dash)) { dash_state = 4; dash = true; }
    // DashControl ("Double-Tap to Dash", 0x6d606e): neutral -> a direction (its angle kept) -> neutral within
    // 12 ticks -> a direction again within 12 ticks and 2.243995 rad (0x6bd5cc) of the first = the dash, kept
    // while the direction is held; back at neutral it ends unless attack or skill is held.
    if (in.flag181 && in.dash_control) {
        if (!dash) {
            switch (dash_state) {
                case 0: if (!in.stick_active) dash_state = 1; break;
                case 1: if (in.stick_active) { dash_state = 2; dash_angle = in.stick_angle; } break;
                case 2:
                    ++dash_t;
                    if (!in.stick_active) { dash_t = 0; dash_state = 3; }
                    else if (dash_t > 12) { dash_t = 0; dash_state = 0; }
                    break;
                case 3:
                    ++dash_t;
                    if (!in.stick_active) { if (dash_t > 12) { dash_t = 0; dash_state = 0; } }
                    else if (std::fabs(wrap(dash_angle - in.stick_angle)) < 2.243995f) {
                        dash_state = 4; dash_t = 0; dash = true; dash_latch = true;
                    }
                    break;
                default: break;
            }
        } else if (!in.stick_active) {
            dash_t = 0; dash_state = 0;
            if (!in.attack_held && !in.skill_held) { dash = false; dash_latch = false; }
        }
    }

    // ---- the tap machine +0x710 (0x465643..0x465819): a short tap opens a 12-tick window ----
    if (in.mouse_mode) {
        // Mouse mode [BYTES 0x465643..0x4656d6]: +0x70c = 0; state 4 holds the window for 12 more
        // ticks (+0x718 past 12 ends it); a double click (pressed 0x800) arms it toward the cursor.
        tap_window = 0;
        if (tap_state == 4) { ++tap_t; tap_window = 1; if (tap_t > 12) tap_state = 0; }
        else tap_t = 0;
        if (in.double_click) { tap_angle = in.mouse_angle; tap_window = 1; tap_state = 4; tap_t = 0; }
    } else switch (tap_state) {
        case 0:
            if (press_edge && std::fabs(wrap(in.stick_angle - in.facing)) < 2.243995f) {   // 0x6bd5cc
                tap_state = 1; tap_t = 0; tap_angle = in.stick_angle;
            }
            break;
        case 1:
            ++tap_t;
            if (!in.stick_active) { tap_state = 2; tap_window = 12; }
            else if (tap_t > 10) tap_state = 0;
            break;
        case 2:
            if (tap_window > 0) --tap_window;
            if (tap_window == 0 || press_edge) { tap_state = 0; tap_window = 0; }
            break;
        default:                           // a mouse window left over: the keyboard path ignores it
            tap_window = 0;
            break;
    }

    // ---- the dash +0xa00 (flag 181), its W1 / W2 writes: held attack or skill sets it [BYTES 0x466afb],
    // a release clears it unless the machine above holds it (+0x720 == 4) [BYTES 0x466596..0x4665b5] ----
    if (!in.flag181) dash = false;
    else if (in.attack_held || in.skill_held) dash = true;
    else if (dash_state != 4) { dash = false; dash_latch = false; }
    o.locomotion_dash = dash && in.grounded && in.stick_active;

    // ---- the jump (0x465388.., 0x468ba7..) ----
    // The air test is the locomotion state +0x3a0 == 3 or not grounded [EXPORT 00465170.c:243], NOT the section:
    // the jump section 3 plays its landing frames on for some ticks, +0x3a0 is back on the ground state after one.
    // [not modelled] the floor sub-mesh's no-jump byte (+0x25c record +0x89) [:234-240].
    if (in.jump_pressed && !in.busy && in.jump_enable && tap_window == 0) {
        if (in.grounded && !loco_air) {
            o.jump = true;
        } else if (!in.submerged && in.kind != 2 && dj_count < (in.flag163 ? 1 : 0) + in.extra_air_jumps &&   // retail: flag163, +0x77c == 0
                   dj_cool == 0 && in.t <= 0.15f) {                                  // 0x6bd1d8
            o.double_jump = true;
            ++dj_count; dj_cool = 6;
        }
    }
    if (o.jump || o.double_jump) loco_air = true;                  // +0x3a0 = 3 [:1918]
    else if (in.movable) loco_air = !in.grounded && !in.own_jump;  // [:441-443, 1939-1940]

    // ---- the attack request (0x465e68..0x465f9d) ----
    if (in.attack_pressed && !in.busy) {
        if (in.actable) { stage = 0; request = true; break_latch = false; }
        else if (!break_latch && in.reserve) request = true;
        else if (in.attack_break) { request = false; break_latch = true; }
    }
    if (request && (in.actable || in.cancelable)) dispatch(in, o, ch);
    stick_was_active = in.stick_active;
    return o;
}

// FUN_0046ea00(0): choose the swing.
void PlayerActions::dispatch(const In& in, Out& o, int ch) {
    bool advance = false;   // bVar2: the stage advances after this swing
    const bool nap = ch == 0 && in.adol_slot >= 0 && in.adol_slot < 4;
    bool done = false;
    if (stage == 0) {
        if (in.grounded) {
            if (tap_window > 0 && in.section != 0x112) {      // the tap-step attack
                o.face = true; o.face_angle = tap_angle;
                o.section = 0x112;
                if (ch == 0 || ch == 1 || ch == 3) {
                    step_t = 13;                                // +0x400
                    o.impulse = true;                           // +0.1 along the tap (0x6bd19c)
                    o.impulse_x = std::cos(tap_angle) * 0.1f;
                    o.impulse_z = std::sin(tap_angle) * 0.1f;
                }
                advance = true; done = true;
            }
        } else if (!in.submerged) {
            if (in.t > -1e-5f || dj_cool != 0) {               // rising, or just double-jumped
                if (in.stick_active) air_combo = true;          // -> the air combo below
                else { o.section = 0x109; done = true; }        // the jump attack
            } else if (in.section == 3) {
                o.section = 0x10b; thrust = true; done = true;  // the down thrust
            }
        }
    }
    if (!done) {
        // The per-swing lunge: +0x730 = 4, +0x72c = 0.1275 - stage x 1e-4 (Hugo 0.015).
        lunge_t = 4;
        lunge_v = (ch == 2 ? 0.015f : 0.1275f) - float(stage) * 1e-4f;
        if (!air_combo) {
            o.section = nap ? kNapGround[in.adol_slot][stage] : kGround[ch][stage];
            advance = true;
        } else {
            static const float kHang[4][3] = {{0.003f, 0.0375f, 0.22f}, {0.025f, 0.038f, 0.28f},
                                              {0.005f, 0.025f, 0.18f}, {0.003f, 0.0375f, 0.22f}};
            if (stage < 3) o.t_add = (ch == 1 && in.yunica_form) ? (stage == 0 ? 0.015f : stage == 1 ? 0.0375f : 0.28f)
                                                                 : kHang[ch][stage];
            if (ch == 1 && !in.yunica_form) dj_count = 1;      // Yunica's air combo spends the double jump
            if (stage == 0 || in.stick_active) { o.section = kAir[stage < 3 ? stage : 2]; advance = true; }
            else { o.section = 0x10b; thrust = true; }
        }
    }
    if (in.stick_active && o.section != 0x112) { o.face = true; o.face_angle = in.stick_angle; }
    o.new_attack = o.section >= 0;
    if (advance) {
        if (tap_window == 0) ++stage;
        const int len = air_combo ? 3 : nap ? kNapLen[in.adol_slot] : kGroundLen[ch];
        if (stage >= len) stage = 0;
    } else {
        stage = 0;
    }
    request = false;
}

}  // namespace cleria
