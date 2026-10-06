// cleria-inputtest — the binding tables, the SDL translation and the per-frame mapper FUN_005d2ba0
// (src/cleria/input_map.hpp) against build/shell_re/CONFIG.md §3.
// Usage: cleria-inputtest [ignored]
#include <cmath>
#include <cstdio>
#include <string>

#include "cleria/input_map.hpp"

using namespace cleria::input;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[input] %-80s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}
static bool near(float a, float b, float e = 1e-5f) { return std::fabs(a - b) < e; }

static Mapper fresh(const Bindings& b, bool aoc = true) {
    Config c;
    c.assign = b;
    c.mouse_ok_cancel = aoc;
    Mapper m;
    m.configure(c);
    return m;
}
// One key held for one frame from a clean history; returns that frame.
static Frame tap(Code dik, const Bindings& b = preset1()) {
    Mapper m = fresh(b);
    Snapshot s;
    s.key[dik] = true;
    return m.step(s);
}

int main() {
    // ---- the tables [BYTES 0x68edf8 / 0x68f210 / 0x703b20 / 0x703c30] ----
    check(code_table_size() == 131, "131 key names");
    check(std::string(code_name(0x01)) == "Esc" && std::string(code_name(0x2b)) == "_" &&
          std::string(code_name(0xc9)) == "Page Down" && std::string(code_name(0xd1)) == "Page Up",
          "JP names and Falcom's swapped Page Up / Page Down kept");
    check(code_from_name("Button1") == 0x107u && code_from_name("button 10") == 0x110u &&
          code_from_name("Numpad8") == 0x48u && code_from_name("l-shift") == 0x2au,
          "parse: spaces stripped, case-insensitive");
    check(code_from_name("---") == 0x103u && !code_from_name("NoSuchKey") && !code_from_name(""),
          "\"---\" = the first match 0x103; unknown / empty = none");
    check(code_ini_name(0x48) == "Numpad8" && code_ini_name(0x110) == "Button10" && code_ini_name(0x99).empty() &&
          code_ini_name(kUnbound).empty(), "write: the table name with spaces stripped, \"\" if unknown");
    check(std::string(slot_name(0)) == "KEY_ACTION" && std::string(slot_name(19)) == "MOUSE_DIR" &&
          std::string(slot_name(33)) == "MOUSE_WALK" && std::string(slot_label(KEY_SWORD_REVU)) == "Next Skill",
          "slot names / labels");
    const Bindings& p1 = preset1();
    const Bindings& p2 = preset2();
    check(p1[KEY_ACTION] == 0x2c && p1[KEY_JUMP] == 0x2d && p1[KEY_MENU] == 0x39 && p1[KEY_UP] == 0x48 &&
          p1[MOUSE_DIR] == 0x100 && p1[PAD_MENU] == 0x110 && p1[PAD_WALK] == 0x10f && p1[MOUSE_ACTION] == 0x103 &&
          p1[MOUSE_JUMP] == 0x101 && p1[MOUSE_USE] == 0x102, "Preset 1: Z X C V / Space / numpad / L-Button dir");
    check(p2[KEY_ACTION] == 0x34 && p2[KEY_USE] == 0x39 && p2[KEY_MENU] == 0x0f && p2[KEY_UP] == 0x11 &&
          p2[KEY_DOWNRIGHT] == 0x2d && p2[MOUSE_DIR] == 0x106 && p2[MOUSE_ACTION] == 0x100 && p2[MOUSE_SHOT] == 0x102,
          "Preset 2: . / _ Space Tab / WASD QEZX / L R M mouse");
    check(!bindable(kDikF1) && bindable(0x3c), "F1 is reserved for the shell");

    // ---- SDL scancode (USB HID usage) <-> DIK ----
    check(dik_from_sdl_scancode(4) == 0x1e && dik_from_sdl_scancode(41) == 0x01 && dik_from_sdl_scancode(82) == 0xc8 &&
          dik_from_sdl_scancode(96) == 0x48 && dik_from_sdl_scancode(225) == 0x2a && dik_from_sdl_scancode(58) == 0x3b &&
          dik_from_sdl_scancode(55) == 0x34 && dik_from_sdl_scancode(49) == 0x2b && dik_from_sdl_scancode(75) == 0xc9,
          "A Esc Up KP8 LShift F1 . \\ PageUp -> DIK");
    bool rt = true;
    int named = 0;
    for (int i = 0; i < code_table_size(); ++i) {
        const Code c = code_table_entry(i);
        if (c >= 0x100) continue;
        const int sc = sdl_scancode_from_dik(c);
        if (sc == 0) continue;
        ++named;
        rt &= dik_from_sdl_scancode(sc) == c;
    }
    check(rt && named == 105, "105 of the 108 named DIK codes have an SDL scancode and round-trip (^ @ : have none)");
    check(dik_from_sdl_scancode(50) == 0x2b && sdl_scancode_from_dik(0x2b) == 49, "ISO NONUSHASH also reads as 0x2b");

    // ---- the gamepad [H: XInput -> DInput order] ----
    check(pad_button_from_sdl(0) == 0 && pad_button_from_sdl(1) == 1 && pad_button_from_sdl(2) == 2 &&
          pad_button_from_sdl(3) == 3 && pad_button_from_sdl(9) == 4 && pad_button_from_sdl(10) == 5 &&
          pad_button_from_sdl(4) == 6 && pad_button_from_sdl(6) == 7 && pad_button_from_sdl(7) == 8 &&
          pad_button_from_sdl(8) == 9, "A B X Y LB RB Back Start LS RS = Button 1..10");
    check(pad_button_from_sdl(11) == -1 && pad_button_from_sdl(14) == -1, "the d-pad is the POV hat, not buttons");
    check(pad_button_from_sdl(kSdlLeftTrigger) == 10 && pad_button_from_sdl(kSdlRightTrigger) == 11,
          "CleriaCore: LT / RT = Button 11 / 12");
    check(pad_code_from_sdl_name("Start") == 0x10eu && pad_code_from_sdl_name("a") == 0x107u &&
          !pad_code_from_sdl_name("dpup") && !pad_code_from_sdl_name("nope") &&
          std::string(sdl_pad_name(9)) == "leftshoulder", "SDL names -> pad codes");

    // ---- the mapper: buttons ----
    {
        Mapper m = fresh(p1);
        Snapshot s;
        s.key[0x2c] = true;   // Z = KEY_ACTION
        Frame f = m.step(s);
        check(f.held == (kAttack | kOk) && f.pressed == (kAttack | kOk), "Z: held/pressed Attack + menu OK (0x2001)");
        f = m.step(s);
        check(f.held == (kAttack | kOk) && f.pressed == 0, "held on: no second edge");
        s.key[0x2c] = false;
        f = m.step(s);
        check(f.held == 0 && f.pressed == 0, "released");
        s.key[0x2c] = true;
        check(m.step(s).pressed == (kAttack | kOk), "a new press, a new edge");
    }
    check(tap(0x2d).held == (kJump | kCancel), "X: Jump + menu Cancel (0x4002)");
    check(tap(0x2e).held == kMagic && tap(0x2f).held == kBoost && tap(0x39).held == kMenu && tap(0x2a).held == kWalk,
          "C Magic 4, V Boost 8, Space Menu 0x10, LShift Walk 0x20");
    check(tap(0x1f).held == kPrevSkill && tap(0x20).held == kNextSkill && tap(0x02).held == kSkill1 &&
          tap(0x03).held == kSkill2 && tap(0x04).held == kSkill3, "S / D prev / next skill, 1 2 3 skills");
    {
        Bindings b = p1;
        b[KEY_MENU] = kUnbound;
        check(tap(0x01, b).pressed == kMenu && tap(0x01, b).held == kMenu, "Esc = Menu 0x10 fixed (KEY_MENU unbound)");
        for (int i = KEY_UP; i <= KEY_DOWNRIGHT; ++i) b[std::size_t(i)] = kUnbound;
        const Frame u = tap(0xc8, b);
        check(u.dir == kUp && near(u.angle, 1.5707964f) && u.magnitude == 1.0f && !u.analog,
              "the arrows are fixed: Up = dir 8, angle +pi/2, magnitude 1");
        check(tap(0xd0, b).dir == kDown && tap(0xcb, b).dir == kLeft && tap(0xcd, b).dir == kRight, "Down / Left / Right");
    }
    check(tap(0x49).dir == (kUp | kRight) && near(tap(0x49).angle, 0.78539819f) && tap(0x4f).dir == (kDown | kLeft) &&
          near(tap(0x4f).angle, -2.3561945f), "diagonal slots set two bits; the angle table");
    {   // opposite pairs: the newer press wins (latch 0x7647f4)
        Mapper m = fresh(p1);
        Snapshot s;
        s.key[0xc8] = true;
        check(m.step(s).dir == kUp, "Up");
        s.key[0xd0] = true;
        check(m.step(s).dir == kDown, "Up held, Down pressed: Down wins");
        s.key[0xc8] = false;
        check(m.step(s).dir == kDown, "Down alone");
        s.key[0xc8] = true;
        check(m.step(s).dir == kUp, "Down held, Up pressed: Up wins");
        Mapper n = fresh(p1);
        Snapshot t;
        t.key[0xcb] = t.key[0xcd] = true;
        check(n.step(t).dir == kLeft, "Left + Right from nothing: Left (latch clear)");
    }
    {   // the pad buttons and the OK / Cancel derivation
        Mapper m = fresh(p1);
        Snapshot s;
        s.pad_present = true;
        s.pad[0] = true;    // Button 1 = PAD_ACTION
        s.pad[1] = true;    // Button 2 = PAD_JUMP
        const Frame f = m.step(s);
        check(f.held == (kAttack | kJump | kOk | kCancel) && f.pressed == f.held, "pad Button 1 / 2: OK / Cancel too");
        Config c = m.config();
        c.game_pad = false;
        Mapper n;
        n.configure(c);
        check(n.step(s).held == 0, "GamePad = 0: the pad's buttons are ignored");
    }
    {   // the mouse: AlwaysOkCancel = 1 (the default)
        Mapper m = fresh(p1, true);
        Snapshot s;
        s.mouse[0] = true;   // L = MOUSE_DIR in Preset 1
        Frame f = m.step(s);
        check(f.held == (kMouseDir | kClick) && f.pressed == (kMouseDir | kClick), "AOC 1, Preset 1: L = mouse-dir + click");
        s.mouse[0] = false;
        s.mouse[1] = true;   // R = MOUSE_JUMP
        f = m.step(s);
        check(f.held == (kJump | kCancel) && f.pressed == (kJump | kCancel), "AOC 1: R = Jump + Cancel (the AOC path)");
        Mapper p = fresh(p2, true);
        Snapshot t;
        t.mouse[0] = true;   // L = MOUSE_ACTION in Preset 2
        f = p.step(t);
        check(f.held == (kAttack | kClick) && f.pressed == (kAttack | kClick), "AOC 1, Preset 2: L = Attack + click, no OK");
    }
    {   // the mouse: AlwaysOkCancel = 0
        Mapper m = fresh(p2, false);
        Snapshot s;
        s.mouse[0] = true;
        Frame f = m.step(s);
        check(f.held == (kAttack | kClick) && f.pressed == (kAttack | kMouseAttack), "AOC 0: L Attack -> held click, pressed 0x8000");
        Mapper n = fresh(p2, false);
        Snapshot t;
        t.mouse[1] = true;
        f = n.step(t);
        check(f.held == (kJump | kCancel), "AOC 0: R Jump -> Cancel through the slot");
        Mapper q = fresh(p1, false);
        check(q.step(s).held == kMouseDir, "AOC 0, Preset 1: L = mouse-dir only (MOUSE_ACTION is ---)");
    }
    {   // MOUSE_DIR double click within DoubleClickFrame (12)
        Mapper m = fresh(p1);
        Snapshot idle, down;
        down.mouse[0] = true;
        for (int i = 0; i < 30; ++i) m.step(idle);
        m.step(down);
        Frame f = m.step(idle);
        f = m.step(idle);   // the first release is seen here: too long since the last -> no double
        check(!(f.pressed & kDoubleClick), "a single click: no 0x800");
        for (int i = 0; i < 3; ++i) m.step(idle);
        m.step(down);
        m.step(idle);
        f = m.step(idle);
        check((f.pressed & kDoubleClick) != 0 && !(f.held & kDoubleClick), "a second click within 12 frames: pressed 0x800");
        for (int i = 0; i < 20; ++i) m.step(idle);
        m.step(down);
        m.step(idle);
        check(!(m.step(idle).pressed & kDoubleClick), "a click 20+ frames later: none");
    }

    // ---- the auto-repeat: frame 1, 28, then every 5 ----
    {
        Mapper m = fresh(p1);
        Snapshot s;
        s.key[0xd0] = true;   // Down arrow, no buttons
        std::string fired;
        for (int i = 1; i <= 45; ++i) {
            const Frame f = m.step(s);
            if (f.dy != 0) fired += std::to_string(i) + (f.dy == 1 ? "+ " : "- ");
        }
        check(fired == "1+ 28+ 33+ 38+ 43+ ", "a held direction steps dy +1 on frames 1, 28, 33, 38, 43");
        Mapper n = fresh(p1);
        Snapshot b;
        b.key[0x2c] = true;   // Z held
        b.key[0xcb] = true;   // with Left
        std::string rep;
        for (int i = 1; i <= 34; ++i) {
            const Frame f = n.step(b);
            if (f.repeat) rep += std::to_string(i) + " ";
            if (f.dx || f.dy) rep += "step! ";
        }
        check(rep == "1 28 33 ", "buttons held: repeat = held on 1, 28, 33; no steps while a button is held");
        Snapshot none;
        n.step(none);
        b.key[0x2c] = false;
        check(n.step(b).dx == -1, "released and pressed again: the counter restarts (Left steps dx -1 at once)");
    }

    // ---- the stick ----
    {
        Mapper m = fresh(p1);
        check(m.dead_zone_int() == 358, "DeadZone 0.35 -> 358 (truncated x 1024)");
        Config c = m.config();
        c.dead_zone = 0.01f; m.configure(c);
        const int lo = m.dead_zone_int();
        c.dead_zone = 2.0f; m.configure(c);
        const int hi = m.dead_zone_int();
        c.dead_zone = -1.0f; m.configure(c);
        const int neg = m.dead_zone_int();
        c.dead_zone = 0.35f; c.book_mode = true; m.configure(c);
        check(lo == 51 && hi == 972 && neg == 51 && m.dead_zone_int() == 0x333, "clamp 0.05..0.95; the book's 0x333");
    }
    {
        Mapper m = fresh(p1);
        Snapshot s;
        s.pad_present = true;
        s.lx = 300;
        check(m.step(s).dir == 0, "inside the deadzone: no direction");
        s.lx = 400;
        Frame f = m.step(s);
        check(f.dir == kRight && near(f.angle, 0.0f) && f.analog && near(f.magnitude, 156.25f) && f.stick_x == 400,
              "past it: right; magnitude = sqrt(x^4 + y^4) / 1024 = 156.25 (retail: the stick always runs)");
        s.lx = -1000; s.ly = -1000;
        f = m.step(s);
        check(f.dir == (kUp | kLeft) && near(f.angle, 2.3561945f, 1e-4f), "up-left (DInput y down): dir 10, angle 3pi/4");
        s.key[0xcd] = true;   // the Right arrow with the stick still left
        f = m.step(s);
        check(f.dir == kRight && f.magnitude == 1.0f && !f.analog && f.stick_x == -1000, "a digital key wins over the stick");
        s.key[0xcd] = false;
        s.lx = s.ly = 0; s.rx = 1024;
        check(m.step(s).dir == kRight, "the right stick when the left is neutral");
        s.rx = 0; s.pov = 9000;
        f = m.step(s);
        check(f.dir == kRight && f.magnitude == 1.0f && f.stick_x == 1024 && f.stick_y == 0, "POV 90 deg = right, magnitude 1");
        s.pov = 0;
        f = m.step(s);
        check(f.dir == kUp && f.stick_y == -1024 && f.stick_x == 0, "POV 0 = up");
        s.pov = -1;
        s.lx = -1000; s.ly = -500;
        Config c = m.config();
        c.book_mode = true;
        m.configure(c);
        check(m.step(s).dir == kLeft, "the book: 4-way quantisation");
        c.book_mode = false;
        c.pad_analog = true;
        m.configure(c);
        f = m.step(s);
        check(f.magnitude == 1.0f && near(f.angle, dir_angle(f.dir)) && !f.analog, "PadAnalog: magnitude 1, the table angle");
        c.pad_analog = false;
        c.linear_magnitude = true;
        m.configure(c);
        s.lx = 512; s.ly = 0;
        check(near(m.step(s).magnitude, 0.5f), "QoL LinearStickMagnitude: |v| / 1024");
        c.game_pad = false;
        m.configure(c);
        check(m.step(s).dir == 0, "GamePad = 0: no stick");
        s.pad_present = false;
        c.game_pad = true;
        m.configure(c);
        check(m.step(s).dir == 0, "no pad opened: no stick");
    }

    // ---- the cursor (+0x00 / +0x04) and the wheel (+0x08) [BYTES 0x5d2be8..0x5d2c58; MOUSE.md §1.1] ----
    {
        Mapper m = fresh(preset1());
        Snapshot s;
        s.display.bb_w = 1024; s.display.bb_h = 768;
        s.cursor_x = 0; s.cursor_y = 0;
        Frame f = m.step(s);
        check(f.x == -48 && f.y == 0, "the cursor: client (0, 0) -> record (-48, 0) (the over-scanned x)");
        s.cursor_x = 1024; s.cursor_y = 768;
        f = m.step(s);
        check(f.x == 1072 && f.y == 864, "client (1024, 768) -> (1072, 864): gains 1.09375 / 1.125");
        s.display.windowed = true; s.display.origin_x = 64; s.display.origin_y = 32;
        s.cursor_x = 512; s.cursor_y = 384;
        f = m.step(s);
        check(f.x == 506 && f.y == 432, "windowed at x 64: the centre reads 6 units left (retail's quirk)");
        s = Snapshot{};
        s.wheel = 120;
        check(m.step(s).wheel == 120, "the wheel is the raw delta (+120 a notch away), not notches");
        s.wheel = -240;
        check(m.step(s).wheel == -240, "two notches toward you in one message: -240 (the skill switch takes the sign)");
        s.wheel = 0;
        check(m.step(s).wheel == 0, "no message: 0 (the caller zeroes it after each snapshot)");
    }

    std::printf("%s (%d failure%s)\n", fails ? "FAILED" : "all passed", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
