#pragma once
// Retail input model: the 34 binding slots, the key-code space and names, the default tables, and the
// per-frame mapper FUN_005d2ba0 that turns a raw input snapshot into the held / pressed bit masks the
// player (+0x510 / +0x50c), the menus and the windows read. Pure: no SDL, no OS. The SDL side (events,
// keyboard state, the gamepad) builds a Snapshot each logic tick (apps/cleria-view/view_input.inl).
//
// Spec: build/shell_re/CONFIG.md §3 (re-checked against the bytes, see input_map.cpp). Provenance tags:
// [BYTES] read from yso_win.exe, [EXPORT] the Ghidra export, [H] hand-chosen, [not modelled].
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "cleria/mouse.hpp"

namespace cleria::input {

// ---- the slots [BYTES names 0x68f210, stride 8] ------------------------------------------------
constexpr int kSlots = 34;
enum Slot : int {
    KEY_ACTION = 0, KEY_JUMP, KEY_SHOT, KEY_USE, KEY_MENU, KEY_WALK, KEY_SWORD_REVD, KEY_SWORD_REVU,
    KEY_SWORD0, KEY_SWORD1, KEY_SWORD2,
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_UPLEFT, KEY_UPRIGHT, KEY_DOWNLEFT, KEY_DOWNRIGHT,
    MOUSE_DIR,
    PAD_ACTION, PAD_JUMP, PAD_SHOT, PAD_USE, PAD_MENU, PAD_WALK, PAD_SWORD_REVD, PAD_SWORD_REVU,
    MOUSE_ACTION, MOUSE_JUMP, MOUSE_SHOT, MOUSE_USE, MOUSE_MENU, MOUSE_WALK,
};
const char* slot_name(int slot);    // "KEY_ACTION" (the ini key is "Assign{" + this + "}")
const char* slot_label(int slot);   // the English label of the default table, "Attack/OK"

// ---- the code space [BYTES table 0x68edf8, 131 x {u32 code, char* name}] -----------------------
// 0x01..0xdd DirectInput DIK codes (PC set-1 scancodes), 0x100/0x101/0x102 the L/R/M mouse buttons,
// 0x103..0x106 the "---" placeholders, 0x107 + n = joystick rgbButtons[n] ("Button n+1").
using Code = std::uint32_t;
constexpr Code kUnbound = 0xffffffffu;
constexpr Code kMouseL = 0x100, kMouseR = 0x101, kMouseM = 0x102;
constexpr Code kPadButton1 = 0x107;
constexpr Code kDikEscape = 0x01, kDikF1 = 0x3b;
constexpr Code kDikUp = 0xc8, kDikDown = 0xd0, kDikLeft = 0xcb, kDikRight = 0xcd;

const char* code_name(Code c);                          // the table's spelling ("Numpad 8"); null if absent
std::string code_ini_name(Code c);                      // FUN_004133e0: spaces stripped; "" if unknown
std::optional<Code> code_from_name(std::string_view s); // FUN_005cc390 + _stricmp, first table match
int code_table_size();
Code code_table_entry(int i);                           // for UIs listing every bindable code
// F1 is reserved for the CleriaCore shell overlay: the shell refuses it and the SDL layer never
// reports it to the mapper [H, CleriaCore].
bool bindable(Code c);

// ---- the default tables [BYTES 0x703b20 = default/EN = Preset 1, 0x703c30 = Preset 2] ---------
// The six per-language tables (0x7035d0..0x703b20) carry identical codes; only labels differ.
using Bindings = std::array<Code, kSlots>;
const Bindings& preset1();   // "Preset Type 1 [Arrows/ZXCV]" — the game default
const Bindings& preset2();   // "Preset Type 2 [WASD/Mouse]" (FUN_005d51d0)

// ---- the mask bits (CONFIG.md §3.4) --------------------------------------------------------------
enum Bit : std::uint32_t {
    kAttack = 0x1, kJump = 0x2, kMagic = 0x4, kBoost = 0x8, kMenu = 0x10, kWalk = 0x20,
    kPrevSkill = 0x40, kNextSkill = 0x80, kSkill1 = 0x100, kSkill2 = 0x200, kSkill3 = 0x400,
    kDoubleClick = 0x800,   // pressed only: MOUSE_DIR released twice within DoubleClickFrame
    kMouseDir = 0x1000,     // MOUSE_DIR held
    kOk = 0x2000,           // menu OK: a non-mouse slot-0 binding
    kCancel = 0x4000,       // menu Cancel: a slot-1 binding (not an AlwaysOkCancel mouse one) / R-button
    kMouseAttack = 0x8000,  // pressed only: a mouse-bound Attack edge (AlwaysOkCancel = 0)
    kClick = 0x10000,       // mouse click: a mouse-bound Attack (AOC 0) or the L-button (AOC 1)
};
// Direction bits (+0x14): 1 down (+y), 2 left (-x), 4 right (+x), 8 up (-y).
enum Dir : std::uint32_t { kDown = 1, kLeft = 2, kRight = 4, kUp = 8 };
float dir_angle(std::uint32_t dir);   // the angle table 0x6b734c [BYTES]: right 0, up +pi/2

// ---- SDL translation (SDL_Scancode values are USB HID usage ids; no SDL header needed) --------
Code dik_from_sdl_scancode(int sdl_scancode);   // 0 = no DIK equivalent
int sdl_scancode_from_dik(Code dik);            // 0 = none

// The gamepad. Retail is DirectInput: an XInput pad seen through DInput numbers its buttons
// A, B, X, Y, LB, RB, Back, Start, LS, RS = Button 1..10 [H: the standard XInput->DInput HID order],
// the d-pad is POV 0 and the sticks are (lX, lY) / (lRx, lRy) in the +-1024 range retail sets
// (DIPROP_RANGE, [BYTES 0x5d5ad7..0x5d5aff]). CleriaCore adds LT / RT / Guide / Misc1 / paddle 1-2 as
// Button 11..16 [H, CleriaCore] so the native binds can use them; retail never sees triggers (DInput
// reports them as the Z axis, which the mapper does not read).
// `sdl_button` is the SDL_GamepadButton value; the two triggers are kSdlLeftTrigger / kSdlRightTrigger.
constexpr int kSdlLeftTrigger = 1000, kSdlRightTrigger = 1001;
int pad_button_from_sdl(int sdl_button);     // DInput button index 0..15, or -1 (the d-pad: POV)
// The SDL gamepad names (SDL_GetGamepadStringForButton / ForAxis spelling) for cleria.ini binds.
std::optional<int> sdl_pad_from_name(std::string_view name);   // case-insensitive
const char* sdl_pad_name(int sdl_button);
// "a" -> 0x107 etc.; nullopt for an unknown or d-pad name.
std::optional<Code> pad_code_from_sdl_name(std::string_view name);

// ---- the mapper FUN_005d2ba0 -------------------------------------------------------------------
// One logic tick's raw state. Key codes index `key` directly (DIK). Mouse buttons are LOGICAL (the
// caller already applied SM_SWAPBUTTON, which retail does itself at 0x75aed7; SDL reports logical).
struct Snapshot {
    std::array<bool, 256> key{};    // DIK-indexed, true = down (retail tests the 0x80 bit)
    bool mouse[5] = {};             // L, R, M, X1, X2 (DIMOUSESTATE.rgbButtons[0..3] + X2)
    // The raw WM_MOUSEWHEEL delta (short)HIWORD(wParam): +-120 per notch, the LAST message of the
    // frame (0x76479c), not a sum [BYTES 0x5d2c58, EXPORT FUN_0040efb0 msg 0x20a]. The skill switch
    // needs |wheel| >= 120 (mouse.hpp SkillSwitch); the menus read only its sign.
    int wheel = 0;
    // The cursor: GetCursorPos. CleriaCore passes the client-area position in back-buffer pixels and
    // the display it maps through (mouse::cursor_from_client, MOUSE.md §1.1).
    int cursor_x = 0, cursor_y = 0;
    mouse::Display display;
    bool pad_present = false;       // 0x764b94: a joystick was opened
    int lx = 0, ly = 0, rx = 0, ry = 0;   // DIJOYSTATE lX/lY/lRx/lRy, +-1024, y down positive
    std::int32_t pov = -1;          // POV[0] in hundredths of a degree (0 = up), -1 = centred
    std::array<bool, 32> pad{};     // rgbButtons
};

struct Config {
    Bindings assign = {};           // 0x7647f8[34]
    bool mouse_ok_cancel = true;    // 0x764880 [input] MouseButtonAlwaysOkCancel
    bool pad_analog = false;        // 0x75aed6 [input] PadAnalog ("Digitize Analog Input")
    bool game_pad = true;           // 0x6d602b [input] GamePad
    float dead_zone = 0.35f;        // [input] DeadZone (clamped like FUN_005d68d0)
    std::uint32_t double_click_frame = 12;   // 0x7647a0 [input] DoubleClickFrame
    // The pause book (GoWndMain2 ctor FUN_005a4ab0) sets the deadzone to 0x333 and 4-way
    // quantisation while it is up; FUN_005a4b60 / FUN_005a4de0 restore both [EXPORT].
    bool book_mode = false;
    // [cleria.input] LinearStickMagnitude (QoL, default off): magnitude = |v| / 1024 clamped to 1, so a
    // half tilt walks. Retail's is sqrt(x^4 + y^4) / 1024 (>= ~125 past any deadzone: the stick
    // always runs) [BYTES 0x5d2f37..0x5d2f6f].
    bool linear_magnitude = false;
    // [cleria.pad] (CleriaCore, build/shell_re/PAD.md): the analog-walk shaping (pad::shaped_magnitude;
    // read only with linear_magnitude), and the pad group's OK / Cancel swapped (Nintendo-style menus;
    // the Attack / Jump bits themselves are unchanged).
    float outer_dead_zone = 1.0f;
    int response_curve = 0;
    bool swap_ok_cancel = false;
};

// The 0x34-byte output record (CONFIG.md §3.4).
struct Frame {
    int x = 0, y = 0;            // +0x00 / +0x04: the cursor in the over-scanned 1024x768 space
    std::uint32_t held = 0;      // +0x20
    std::uint32_t pressed = 0;   // +0x24 (rising edge vs the previous frame's held)
    std::uint32_t repeat = 0;    // +0x28 (= held on an auto-repeat frame)
    int dx = 0, dy = 0;          // +0x2c / +0x30 menu steps on an auto-repeat frame with nothing held
    std::uint32_t dir = 0;       // +0x14 direction bits
    float angle = 0.0f;          // +0x18 (right 0, up +pi/2)
    float magnitude = 0.0f;      // +0x1c (1.0 for a digital direction)
    int stick_x = 0, stick_y = 0;   // +0x0c / +0x10
    int wheel = 0;               // +0x08
    bool analog = false;         // CleriaCore: `angle` is the stick's own (not the table's)
};

class Mapper {
  public:
    void configure(const Config& c);
    const Config& config() const { return cfg_; }
    const Frame& step(const Snapshot& s);   // one call per logic tick
    const Frame& frame() const { return cur_; }
    void reset();                           // forget the history (edges, latches, counters)
    int dead_zone_int() const { return dz_; }   // 0x7647a8

  private:
    Config cfg_{};
    int dz_ = 358;                  // 0x7647a8 = (int)(clamp(dz, 0.05, 0.95) * 1024)
    Frame cur_{};                   // the block just written
    std::uint32_t held_prev2_ = 0;  // the held mask two frames back (the block being overwritten)
    std::uint32_t latch_ = 0;       // 0x7647f4: the opposite-direction resolution
    std::uint32_t dbl_count_ = 0;   // 0x7647ac
    std::uint32_t repeat_count_ = 0;   // 0x764884
};

}  // namespace cleria::input
