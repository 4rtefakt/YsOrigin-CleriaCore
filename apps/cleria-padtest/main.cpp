// cleria-padtest — the Controller page's logic (src/cleria/pad.hpp) and its mapper hooks
// (src/cleria/input_map.cpp) against build/shell_re/PAD.md: glyph sets, the PAD_* bindings as SDL
// buttons with conflict swapping, the deadzone and the analog-walk curve, the swapped OK / Cancel,
// retail's force-feedback table and the [cleria.pad] ini keys. No SDL, no pad needed.
// Usage: cleria-padtest [ignored]
#include <cmath>
#include <cstdio>
#include <string>

#include "cleria/input_map.hpp"
#include "cleria/pad.hpp"
#include "cleria/settings.hpp"

using namespace cleria;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[pad] %-84s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}
static bool near(float a, float b, float e = 1e-5f) { return std::fabs(a - b) <= e; }

// One mapper step on a pad-only snapshot.
static input::Frame step(input::Mapper& m, int lx, int ly, int button = -1) {
    input::Snapshot s;
    s.pad_present = true;
    s.lx = lx; s.ly = ly;
    if (button >= 0) s.pad[std::size_t(button)] = true;
    return m.step(s);
}

int main() {
    // ---- glyph sets ----
    check(pad::glyphs_for_sdl_type(3) == pad::kXbox && pad::glyphs_for_sdl_type(5) == pad::kPlayStation &&
              pad::glyphs_for_sdl_type(7) == pad::kNintendo && pad::glyphs_for_sdl_type(1) == pad::kGeneric &&
              pad::glyphs_for_sdl_type(0) == pad::kGeneric,
          "SDL_GamepadType -> Xbox / PlayStation / Nintendo / generic");
    check(pad::resolve_glyphs(pad::kAuto, 6, true) == pad::kPlayStation && pad::resolve_glyphs(pad::kAuto, 6, false) == pad::kGeneric &&
              pad::resolve_glyphs(pad::kNintendo, 3, true) == pad::kNintendo,
          "Glyphs=auto follows the pad (generic without one); an explicit set overrides it");
    check(std::string(pad::button_short(pad::kNintendo, pad::kSouth)) == "B" &&
              std::string(pad::button_short(pad::kNintendo, pad::kEast)) == "A" &&
              std::string(pad::button_name(pad::kPlayStation, pad::kEast)) == "Circle" &&
              std::string(pad::button_short(pad::kXbox, pad::kLeftTrigger)) == "LT" &&
              std::string(pad::button_short(pad::kNintendo, pad::kRightTrigger)) == "ZR" &&
              std::string(pad::button_short(pad::kGeneric, pad::kSouth)) == "1",
          "labels are positional: Nintendo SOUTH = B, PS EAST = Circle, LT / ZR, generic = Button n");
    check(pad::offset_sticks(pad::kXbox) && !pad::offset_sticks(pad::kPlayStation), "Xbox offset sticks, PS symmetric");
    check(pad::glyphs_from_name("PlayStation") == pad::kPlayStation && pad::glyphs_from_name("bogus") == pad::kAuto,
          "Glyphs ini names (case-insensitive; unknown = auto)");

    // ---- SDL <-> DInput ----
    bool inv = true;
    for (int d = 0; d < 16; ++d) inv &= input::pad_button_from_sdl(pad::sdl_from_dinput(d)) == d;
    check(inv && pad::sdl_from_dinput(-1) == -1, "sdl_from_dinput inverts input::pad_button_from_sdl for Button 1..16");

    // ---- bindings ----
    {
        Settings s;   // retail Preset 1: Button 1 2 3 4 10 9 5 8
        const pad::PadButtons b = pad::pad_buttons(s);
        const pad::PadButtons want = {pad::kSouth, pad::kEast, pad::kWest, pad::kNorth, pad::kRightStick,
                                      pad::kLeftStick, pad::kLeftShoulder, pad::kStart};
        check(b == want, "retail pad defaults as an Xbox pad sees them: A B X Y RS LS LB Menu");
        s.cleria.native_pad_binds = true;
        const pad::PadButtons n = pad::pad_buttons(s);
        const pad::PadButtons nw = {pad::kSouth, pad::kEast, pad::kWest, pad::kNorth, pad::kStart,
                                    pad::kLeftTrigger, pad::kLeftShoulder, pad::kRightShoulder};
        check(n == nw, "NativePadBinds defaults: A B X Y Start LT LB RB");
    }
    {
        pad::PadButtons b = {0, 1, 2, 3, 8, 7, 9, 6};
        int r = pad::bind_swap(b, 0, pad::kEast);
        check(r == 1 && b[0] == pad::kEast && b[1] == pad::kSouth, "Attack -> B while Jump has B: they swap");
        r = pad::bind_swap(b, 2, pad::kRightShoulder);
        check(r == -1 && b[2] == pad::kRightShoulder, "a free button: no conflict");
        const pad::PadButtons before = b;
        r = pad::bind_swap(b, 3, pad::kDpadUp);
        check(r == -2 && b == before, "the d-pad is refused (it always moves)");
        b = {0, 1, 2, 3, -1, 7, 9, 6};
        r = pad::bind_swap(b, 4, pad::kNorth);
        check(r == 3 && b[4] == pad::kNorth && b[3] == -1, "an unbound action takes a used button: the other is unbound");
        check(pad::action_on(b, pad::kNorth) == 4 && pad::action_on(b, pad::kGuide) == -1, "action_on");
        r = pad::bind_swap(b, 0, pad::kLeftTrigger);
        check(r == -1 && b[0] == pad::kLeftTrigger, "a trigger binds like a button");
    }
    {
        Settings s;
        pad::PadButtons b = pad::pad_buttons(s);
        pad::bind_swap(b, 5, pad::kLeftTrigger);   // Walk -> LT
        b[7] = -1;                                  // Next Skill unbound
        pad::set_pad_buttons(s, b);
        const input::Config mc = mapper_config(s);
        check(s.input.assign[input::PAD_WALK] == input::kPadButton1 + 10 && s.input.assign[input::PAD_SWORD_REVU] == 0x103 &&
                  mc.assign[input::PAD_WALK] == input::kPadButton1 + 10,
              "retail storage: Walk = Button 11 (LT), unbound = \"---\" (0x103)");
        check(pad::pad_buttons(s) == b, "retail storage round trip");
        pad::reset_pad_bindings(s);
        check(s.input.assign == input::preset1(), "Reset: the retail Preset 1 pad slots");

        Settings t;
        t.cleria.native_pad_binds = true;
        pad::PadButtons c = pad::pad_buttons(t);
        pad::bind_swap(c, 0, pad::kRightShoulder);   // Attack -> RB (Next Skill had RB: takes A)
        c[2] = -1;
        pad::set_pad_buttons(t, c);
        check(t.cleria.pad_bind[0] == "rightshoulder" && t.cleria.pad_bind[7] == "a" && t.cleria.pad_bind[2].empty(),
              "native storage: names, swap, unbound = \"\"");
        const input::Config nc = mapper_config(t);
        check(nc.assign[input::PAD_ACTION] == input::kPadButton1 + 5 && nc.assign[input::PAD_SHOT] == input::kUnbound,
              "native storage reaches the mapper (RB = Button 6, unbound = none)");
        pad::reset_pad_bindings(t);
        check(t.cleria.pad_bind == Settings{}.cleria.pad_bind, "Reset: the native defaults");
    }

    // ---- the stick ----
    {
        check(near(pad::shaped_magnitude(0.5f, 0.35f, 1.0f, 0), 0.5f) && near(pad::shaped_magnitude(1.3f, 0.35f, 1.0f, 0), 1.0f),
              "linear, outer 100%: min(r, 1) (the original LinearStickMagnitude)");
        check(near(pad::shaped_magnitude(0.4f, 0.35f, 0.8f, 0), 0.5f) && near(pad::shaped_magnitude(0.8f, 0.35f, 0.8f, 0), 1.0f),
              "linear, outer 80%: r / 0.8, saturating at the outer ring");
        check(near(pad::shaped_magnitude(0.35f, 0.35f, 1.0f, 1), 0.0f) && near(pad::shaped_magnitude(1.0f, 0.35f, 1.0f, 1), 1.0f) &&
                  near(pad::shaped_magnitude(0.675f, 0.35f, 1.0f, 1), 0.25f),
              "smooth: 0 at the deadzone edge, 1 at the outer ring, a quarter halfway");
        bool mono = true;
        float last = -1.0f;
        for (int i = 0; i <= 150; ++i) {
            const float m = pad::shaped_magnitude(float(i) / 100.0f, 0.2f, 0.9f, 1);
            mono &= m >= last && m >= 0.0f && m <= 1.0f;
            last = m;
        }
        check(mono, "smooth is monotonic within [0, 1]");

        // the retail deadzone: (int)(0.35 * 1024) = 358, compared on the squared int length
        Settings s;
        input::Mapper m;
        m.configure(mapper_config(s));
        check(m.dead_zone_int() == 358, "retail [input] DeadZone 0.35 -> 358 (FUN_005d68d0)");
        check(step(m, 357, 0).dir == 0, "stick (357, 0): inside the deadzone, no movement");
        const input::Frame f = step(m, 358, 0);
        check(f.dir == input::kRight && f.magnitude > 100.0f && f.analog,
              "stick (358, 0): right; retail magnitude x^2/1024 (always past RunWalkThreshold), analog angle");
        check(step(m, 250, -250).dir == 0 && step(m, 300, -300).dir == (input::kRight | input::kUp),
              "radial deadzone: (250, -250) |v| 354 stays, (300, -300) |v| 424 moves up-right (no axis is past 358)");
        // analog walk with the defaults = the original LinearStickMagnitude
        s.cleria.linear_stick_magnitude = true;
        m.configure(mapper_config(s));
        check(near(step(m, 700, 0).magnitude, 700.0f / 1024.0f), "analog walk, defaults: |v| / 1024 (unchanged)");
        s.pad.outer_dead_zone = 0.8f;
        m.configure(mapper_config(s));
        check(near(step(m, 700, 0).magnitude, (700.0f / 1024.0f) / 0.8f, 1e-4f), "analog walk, outer 80%: |v| / 1024 / 0.8");
        s.pad.response_curve = 1;
        m.configure(mapper_config(s));
        const float u = (700.0f / 1024.0f - 358.0f / 1024.0f) / (0.8f - 358.0f / 1024.0f);
        check(near(step(m, 700, 0).magnitude, u * u, 1e-4f), "analog walk, smooth: rescaled from the deadzone edge, squared");
        s.cleria.linear_stick_magnitude = false;
        m.configure(mapper_config(s));
        check(step(m, 700, 0).magnitude > 100.0f, "without analog walk the curve settings change nothing (retail magnitude)");
    }

    // ---- swap confirm / cancel ----
    {
        Settings s;
        input::Mapper m;
        m.configure(mapper_config(s));
        step(m, 0, 0);
        input::Frame f = step(m, 0, 0, 0);   // Button 1 (A) pressed
        check((f.pressed & input::kOk) && !(f.pressed & input::kCancel) && (f.pressed & input::kAttack), "default: A = Attack + OK");
        s.pad.swap_confirm_cancel = true;
        m.configure(mapper_config(s));
        m.reset();
        step(m, 0, 0);
        f = step(m, 0, 0, 0);
        check((f.pressed & input::kCancel) && !(f.pressed & input::kOk) && (f.pressed & input::kAttack) &&
                  (f.held & input::kCancel),
              "swapped: A = Attack + Cancel (the Attack bit itself unchanged)");
        step(m, 0, 0);
        f = step(m, 0, 0, 1);                // Button 2 (B)
        check((f.pressed & input::kOk) && !(f.pressed & input::kCancel) && (f.pressed & input::kJump), "swapped: B = Jump + OK");
        input::Snapshot k;
        k.key[0x2c] = true;                  // Z, the keyboard's Attack
        m.reset();
        m.step(input::Snapshot{});
        f = m.step(k);
        check((f.pressed & input::kOk) && !(f.pressed & input::kCancel), "swapped: the keyboard is unaffected (Z = OK)");
    }

    // ---- force feedback ----
    {
        const pad::Rumble r5 = pad::retail_effect(5, 1.0f), r0 = pad::retail_effect(0, 1.0f), r3h = pad::retail_effect(3, 0.5f);
        check(r5.low == 65535 && r5.high == 65535 && r5.ms == 1000, "effect 5: full strength, 1 s (the vibration test)");
        check(r0.low == 32768 && r0.ms == 200, "effect 0: half strength, 0.2 s");
        check(r3h.low == 32768 && r3h.ms == 400, "effect 3 at 50% intensity: half strength, 0.4 s");
        check(pad::retail_effect(6, 1.0f).ms == 0 && pad::retail_effect(-1, 1.0f).ms == 0, "out of range: nothing");
        check(pad::effect_ms(1) == 200 && pad::effect_ms(2) == 400 && pad::effect_ms(4) == 1000 &&
                  near(pad::effect_magnitude(2), 0.5f) && near(pad::effect_magnitude(3), 1.0f),
              "the table at 0x6b8df8: {0.5, 1.0} x {200, 400, 1000} ms");
        check(pad::hit_effect(4) == 0 && pad::hit_effect(0xb) == 1 && pad::hit_effect(9) == 3 && pad::hit_effect(5) == 3 &&
                  pad::hit_effect(-1) == -1,
              "player hit: small 0, medium 1, knockdown 3, death 3, none -1 (FUN_004fd350)");
    }

    // ---- [cleria.pad] ----
    {
        Settings d;
        check(d.pad.glyphs == 0 && d.pad.outer_dead_zone == 1.0f && d.pad.response_curve == 0 && !d.pad.swap_confirm_cancel &&
                  d.pad.vibration_intensity == 1.0f && near(d.pad.trigger_threshold, 0.25f) && d.input.force_feedback == 0 &&
                  near(d.input.dead_zone, 0.35f),
              "defaults: auto glyphs, no outer deadzone, linear, no swap, full strength; retail FF off, DZ 35%");
        Settings s;
        s.pad.glyphs = pad::kNintendo;
        s.pad.outer_dead_zone = 0.85f;
        s.pad.response_curve = 1;
        s.pad.trigger_threshold = 0.4f;
        s.pad.swap_confirm_cancel = true;
        s.pad.vibration_intensity = 0.6f;
        Ini c;
        write_cleria(c, s);
        Settings t;
        read_cleria(c, t);
        check(t.pad.glyphs == pad::kNintendo && near(t.pad.outer_dead_zone, 0.85f) && t.pad.response_curve == 1 &&
                  near(t.pad.trigger_threshold, 0.4f) && t.pad.swap_confirm_cancel && near(t.pad.vibration_intensity, 0.6f) &&
                  c.get_string("cleria.pad", "Glyphs").value_or("") == "nintendo" &&
                  c.get_string("cleria.pad", "ResponseCurve").value_or("") == "smooth",
              "[cleria.pad] round trip (Glyphs / ResponseCurve as names)");
        Ini bad;
        bad.parse("[cleria.pad]\r\nOuterDeadZone=0.1\r\nTriggerThreshold=2\r\nVibrationIntensity=-1\r\n");
        Settings b;
        read_cleria(bad, b);
        check(near(b.pad.outer_dead_zone, 0.5f) && near(b.pad.trigger_threshold, 0.95f) && near(b.pad.vibration_intensity, 0.0f),
              "out-of-range values are clamped on read");
    }

    std::printf("[pad] %s (%d failed)\n", fails ? "FAILED" : "all passed", fails);
    return fails ? 1 : 0;
}
