// The retail input model and the per-frame mapper FUN_005d2ba0. See input_map.hpp and
// build/shell_re/CONFIG.md §3. Every table here was dumped from yso_win.exe (smallfix 2024-01-08) with
// pefile; the mapper was re-read instruction by instruction at 0x5d2ba0..0x5d3376.
#include "cleria/input_map.hpp"

#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>

namespace cleria::input {

namespace {

// [BYTES 0x68f210] slot names; labels from the EN/default table 0x703b20 (+4, wchar*).
struct SlotInfo { const char* name; const char* label; };
constexpr SlotInfo kSlotInfo[kSlots] = {
    {"KEY_ACTION", "Attack/OK"}, {"KEY_JUMP", "Jump/Cancel"}, {"KEY_SHOT", "Use Magic"},
    {"KEY_USE", "Use Boost"}, {"KEY_MENU", "Camp Menu"}, {"KEY_WALK", "Walk/Run"},
    {"KEY_SWORD_REVD", "Previous Skill"}, {"KEY_SWORD_REVU", "Next Skill"}, {"KEY_SWORD0", "Skill 1"},
    {"KEY_SWORD1", "Skill 2"}, {"KEY_SWORD2", "Skill 3"}, {"KEY_UP", "Move Up"}, {"KEY_DOWN", "Move Down"},
    {"KEY_LEFT", "Move Left"}, {"KEY_RIGHT", "Move Right"}, {"KEY_UPLEFT", "Move Up-Left"},
    {"KEY_UPRIGHT", "Move Up-Right"}, {"KEY_DOWNLEFT", "Move Down-Left"},
    {"KEY_DOWNRIGHT", "Move Down-Right"}, {"MOUSE_DIR", "Mouse Movement"},
    {"PAD_ACTION", "Attack/OK"}, {"PAD_JUMP", "Jump/Cancel"}, {"PAD_SHOT", "Use Magic"},
    {"PAD_USE", "Use Boost"}, {"PAD_MENU", "Camp Menu"}, {"PAD_WALK", "Walk/Run"},
    {"PAD_SWORD_REVD", "Previous Skill"}, {"PAD_SWORD_REVU", "Next Skill"},
    {"MOUSE_ACTION", "Attack/OK"}, {"MOUSE_JUMP", "Jump/Cancel"}, {"MOUSE_SHOT", "Use Magic"},
    {"MOUSE_USE", "Use Boost"}, {"MOUSE_MENU", "Camp Menu"}, {"MOUSE_WALK", "Walk/Run"},
};

// [BYTES 0x68edf8] 131 x {code, name}, in table order (the parser takes the FIRST match, so "---"
// reads back as 0x103). The JP-keyboard names (0x2b "_", 0x7d "\", 0x90..0x92) and Falcom's swapped
// Page Up / Page Down (0xc9 DIK_PRIOR = "Page Down", 0xd1 DIK_NEXT = "Page Up") are kept verbatim.
struct CodeName { Code code; const char* name; };
constexpr CodeName kCodes[] = {
    {0x01, "Esc"}, {0x02, "1"}, {0x03, "2"}, {0x04, "3"}, {0x05, "4"}, {0x06, "5"}, {0x07, "6"},
    {0x08, "7"}, {0x09, "8"}, {0x0a, "9"}, {0x0b, "0"}, {0x0c, "-"}, {0x0d, "="}, {0x0e, "BackSpace"},
    {0x0f, "Tab"}, {0x10, "Q"}, {0x11, "W"}, {0x12, "E"}, {0x13, "R"}, {0x14, "T"}, {0x15, "Y"},
    {0x16, "U"}, {0x17, "I"}, {0x18, "O"}, {0x19, "P"}, {0x1a, "["}, {0x1b, "]"}, {0x1c, "Enter"},
    {0x1d, "L-Ctrl"}, {0x1e, "A"}, {0x1f, "S"}, {0x20, "D"}, {0x21, "F"}, {0x22, "G"}, {0x23, "H"},
    {0x24, "J"}, {0x25, "K"}, {0x26, "L"}, {0x27, ";"}, {0x28, "'"}, {0x29, "`"}, {0x2a, "L-Shift"},
    {0x2b, "_"}, {0x2c, "Z"}, {0x2d, "X"}, {0x2e, "C"}, {0x2f, "V"}, {0x30, "B"}, {0x31, "N"},
    {0x32, "M"}, {0x33, ","}, {0x34, "."}, {0x35, "/"}, {0x36, "R-Shift"}, {0x37, "*"}, {0x38, "L-Alt"},
    {0x39, "Space"}, {0x3a, "CapsLock"}, {0x3b, "F1"}, {0x3c, "F2"}, {0x3d, "F3"}, {0x3e, "F4"},
    {0x3f, "F5"}, {0x40, "F6"}, {0x41, "F7"}, {0x42, "F8"}, {0x43, "F9"}, {0x44, "F10"},
    {0x45, "NumLock"}, {0x46, "ScrollLock"}, {0x47, "Numpad 7"}, {0x48, "Numpad 8"},
    {0x49, "Numpad 9"}, {0x4a, "Numpad -"}, {0x4b, "Numpad 4"}, {0x4c, "Numpad 5"}, {0x4d, "Numpad 6"},
    {0x4e, "Numpad +"}, {0x4f, "Numpad 1"}, {0x50, "Numpad 2"}, {0x51, "Numpad 3"}, {0x52, "Numpad 0"},
    {0x53, "Numpad ."}, {0x57, "F11"}, {0x58, "F12"}, {0x7d, "\\"}, {0x90, "^"}, {0x91, "@"},
    {0x92, ":"}, {0x9c, "Numpad Enter"}, {0x9d, "R-Ctrl"}, {0xb5, "Numpad /"}, {0xb7, "Print Screen"},
    {0xb8, "R-Alt"}, {0xc5, "Pause"}, {0xc7, "Home"}, {0xc8, "Up"}, {0xc9, "Page Down"}, {0xcb, "Left"},
    {0xcd, "Right"}, {0xcf, "End"}, {0xd0, "Down"}, {0xd1, "Page Up"}, {0xd2, "Insert"},
    {0xd3, "Delete"}, {0xdb, "L-Win"}, {0xdc, "R-Win"}, {0xdd, "Apps"},
    {0x100, "L-Button"}, {0x101, "R-Button"}, {0x102, "M-Button"},
    {0x103, "---"}, {0x104, "---"}, {0x105, "---"}, {0x106, "---"},
    {0x107, "Button 1"}, {0x108, "Button 2"}, {0x109, "Button 3"}, {0x10a, "Button 4"},
    {0x10b, "Button 5"}, {0x10c, "Button 6"}, {0x10d, "Button 7"}, {0x10e, "Button 8"},
    {0x10f, "Button 9"}, {0x110, "Button 10"}, {0x111, "Button 11"}, {0x112, "Button 12"},
    {0x113, "Button 13"}, {0x114, "Button 14"}, {0x115, "Button 15"}, {0x116, "Button 16"},
};
static_assert(sizeof(kCodes) / sizeof(kCodes[0]) == 131, "the retail key-name table has 131 entries");

// [BYTES 0x703b20] the default (EN) table = Preset 1; [BYTES 0x703c30] Preset 2.
constexpr Bindings kPreset1 = {
    0x2c, 0x2d, 0x2e, 0x2f, 0x39, 0x2a, 0x1f, 0x20, 0x02, 0x03, 0x04,   // Z X C V Space LShift S D 1 2 3
    0x48, 0x50, 0x4b, 0x4d, 0x47, 0x49, 0x4f, 0x51,                     // Numpad 8 2 4 6 7 9 1 3
    0x100,                                                              // MOUSE_DIR = L-Button
    0x107, 0x108, 0x109, 0x10a, 0x110, 0x10f, 0x10b, 0x10e,             // Button 1 2 3 4 10 9 5 8
    0x103, 0x101, 0x104, 0x102, 0x105, 0x106,                           // ---, R, ---, M, ---, ---
};
constexpr Bindings kPreset2 = {
    0x34, 0x35, 0x2b, 0x39, 0x0f, 0x2a, 0x1a, 0x1b, 0x02, 0x03, 0x04,   // . / _ Space Tab LShift [ ] 1 2 3
    0x11, 0x1f, 0x1e, 0x20, 0x10, 0x12, 0x2c, 0x2d,                     // W S A D Q E Z X
    0x106,                                                              // MOUSE_DIR = ---
    0x107, 0x108, 0x109, 0x10a, 0x110, 0x10f, 0x10b, 0x10e,
    0x100, 0x101, 0x102, 0x103, 0x104, 0x105,                           // L, R, M, ---, ---, ---
};

// [BYTES 0x6b734c] the angle per direction-bit combination (16 floats).
constexpr float kPi = 3.1415927410125732f;           // 0x6bd6ac
constexpr float kAngle[16] = {
    0.0f, -1.57079637050628662f, kPi, -2.35619449615478516f, 0.0f, -0.785398185253143311f, 0.0f, 0.0f,
    1.57079637050628662f, 0.0f, 2.35619449615478516f, 0.0f, 0.785398185253143311f, 0.0f, 0.0f, 0.0f,
};
// [BYTES 0x6b7380] 4-way and [BYTES 0x6b7384] 8-way quantisation tables.
constexpr std::uint8_t kQuant4[4] = {2, 1, 4, 8};
constexpr std::uint8_t kQuant8[8] = {2, 3, 1, 5, 4, 0xc, 8, 0xa};
// [BYTES 0x6b7344 / 0x6b7348] the three slot groups {count, first}: keyboard, pad, mouse.
constexpr int kGroupCount[3] = {11, 8, 6};
constexpr int kGroupFirst[3] = {0, 20, 28};

std::string strip_ws(std::string_view s) {   // FUN_005cc390: drop spaces and tabs
    std::string o;
    for (char c : s) if (c != ' ' && c != '\t') o.push_back(c);
    return o;
}
bool ieq(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

// SDL_Scancode (USB HID usage) -> DIK. Both are physical-position codes, so this is a fixed table.
struct ScanPair { int sdl; Code dik; };
constexpr ScanPair kScan[] = {
    {41, 0x01},                                                         // ESCAPE
    {30, 0x02}, {31, 0x03}, {32, 0x04}, {33, 0x05}, {34, 0x06}, {35, 0x07}, {36, 0x08}, {37, 0x09},
    {38, 0x0a}, {39, 0x0b},                                             // 1..9, 0
    {45, 0x0c}, {46, 0x0d}, {42, 0x0e}, {43, 0x0f},                     // - = BACKSPACE TAB
    {20, 0x10}, {26, 0x11}, {8, 0x12}, {21, 0x13}, {23, 0x14}, {28, 0x15}, {24, 0x16}, {12, 0x17},
    {18, 0x18}, {19, 0x19},                                             // Q W E R T Y U I O P
    {47, 0x1a}, {48, 0x1b}, {40, 0x1c}, {224, 0x1d},                    // [ ] RETURN LCTRL
    {4, 0x1e}, {22, 0x1f}, {7, 0x20}, {9, 0x21}, {10, 0x22}, {11, 0x23}, {13, 0x24}, {14, 0x25},
    {15, 0x26},                                                         // A S D F G H J K L
    {51, 0x27}, {52, 0x28}, {53, 0x29}, {225, 0x2a}, {49, 0x2b},        // ; ' ` LSHIFT BACKSLASH
    {29, 0x2c}, {27, 0x2d}, {6, 0x2e}, {25, 0x2f}, {5, 0x30}, {17, 0x31}, {16, 0x32},   // Z X C V B N M
    {54, 0x33}, {55, 0x34}, {56, 0x35}, {229, 0x36}, {85, 0x37}, {226, 0x38}, {44, 0x39}, {57, 0x3a},
    {58, 0x3b}, {59, 0x3c}, {60, 0x3d}, {61, 0x3e}, {62, 0x3f}, {63, 0x40}, {64, 0x41}, {65, 0x42},
    {66, 0x43}, {67, 0x44},                                             // F1..F10
    {83, 0x45}, {71, 0x46},                                             // NUMLOCKCLEAR SCROLLLOCK
    {95, 0x47}, {96, 0x48}, {97, 0x49}, {86, 0x4a}, {92, 0x4b}, {93, 0x4c}, {94, 0x4d}, {87, 0x4e},
    {89, 0x4f}, {90, 0x50}, {91, 0x51}, {98, 0x52}, {99, 0x53},         // the keypad
    {100, 0x56}, {68, 0x57}, {69, 0x58},                                // NONUSBACKSLASH F11 F12
    {135, 0x73}, {137, 0x7d},                                           // INTERNATIONAL1 (ro) / 3 (yen)
    {88, 0x9c}, {228, 0x9d}, {84, 0xb5}, {70, 0xb7}, {230, 0xb8}, {72, 0xc5},
    {74, 0xc7}, {82, 0xc8}, {75, 0xc9}, {80, 0xcb}, {79, 0xcd}, {77, 0xcf}, {81, 0xd0}, {78, 0xd1},
    {73, 0xd2}, {76, 0xd3}, {227, 0xdb}, {231, 0xdc}, {101, 0xdd},
    {50, 0x2b},   // NONUSHASH: the ISO key DInput also reports as 0x2b (after BACKSLASH: dik->sdl = 49)
};
// [not modelled] DIK 0x90..0x92 (^ @ :) are NEC PC-98 codes with no SDL scancode.

// SDL_GamepadButton -> the DInput button index [H]; see input_map.hpp.
struct PadName { int sdl; const char* name; int dinput; };
constexpr PadName kPad[] = {
    {0, "a", 0}, {1, "b", 1}, {2, "x", 2}, {3, "y", 3}, {4, "back", 6}, {5, "guide", 12},
    {6, "start", 7}, {7, "leftstick", 8}, {8, "rightstick", 9}, {9, "leftshoulder", 4},
    {10, "rightshoulder", 5}, {11, "dpup", -1}, {12, "dpdown", -1}, {13, "dpleft", -1},
    {14, "dpright", -1}, {15, "misc1", 13}, {16, "paddle1", 14}, {17, "paddle2", 15},
    {kSdlLeftTrigger, "lefttrigger", 10}, {kSdlRightTrigger, "righttrigger", 11},
};

}  // namespace

const char* slot_name(int slot) { return slot >= 0 && slot < kSlots ? kSlotInfo[slot].name : ""; }
const char* slot_label(int slot) { return slot >= 0 && slot < kSlots ? kSlotInfo[slot].label : ""; }

const char* code_name(Code c) {
    for (const CodeName& e : kCodes) if (e.code == c) return e.name;
    return nullptr;
}
std::string code_ini_name(Code c) {
    const char* n = code_name(c);
    return n ? strip_ws(n) : std::string();
}
std::optional<Code> code_from_name(std::string_view s) {
    const std::string a = strip_ws(s);
    if (a.empty()) return std::nullopt;
    for (const CodeName& e : kCodes)
        if (ieq(a, strip_ws(e.name))) return e.code;
    return std::nullopt;
}
int code_table_size() { return int(sizeof(kCodes) / sizeof(kCodes[0])); }
Code code_table_entry(int i) { return i >= 0 && i < code_table_size() ? kCodes[i].code : kUnbound; }
bool bindable(Code c) { return c != kDikF1; }

const Bindings& preset1() { return kPreset1; }
const Bindings& preset2() { return kPreset2; }

float dir_angle(std::uint32_t dir) { return kAngle[dir & 15]; }

Code dik_from_sdl_scancode(int sc) {
    for (const ScanPair& p : kScan) if (p.sdl == sc) return p.dik;
    return 0;
}
int sdl_scancode_from_dik(Code dik) {
    for (const ScanPair& p : kScan) if (p.dik == dik) return p.sdl;
    return 0;
}

int pad_button_from_sdl(int b) {
    for (const PadName& p : kPad) if (p.sdl == b) return p.dinput;
    return -1;
}
std::optional<int> sdl_pad_from_name(std::string_view name) {
    for (const PadName& p : kPad) if (ieq(name, p.name)) return p.sdl;
    return std::nullopt;
}
const char* sdl_pad_name(int b) {
    for (const PadName& p : kPad) if (p.sdl == b) return p.name;
    return "";
}
std::optional<Code> pad_code_from_sdl_name(std::string_view name) {
    const std::optional<int> b = sdl_pad_from_name(name);
    if (!b) return std::nullopt;
    const int i = pad_button_from_sdl(*b);
    if (i < 0) return std::nullopt;
    return kPadButton1 + Code(i);
}

// ---- the mapper ----------------------------------------------------------------------------------
void Mapper::configure(const Config& c) {
    cfg_ = c;
    // FUN_005d68d0: dz < 0 -> 0.05; else min(dz, 1), then [0.05, 0.95]; truncated x 1024 [EXPORT].
    float f = 0.05f;
    if (c.dead_zone >= 0.0f) {
        const float d = c.dead_zone <= 1.0f ? c.dead_zone : 1.0f;
        f = d < 0.05f ? 0.05f : d > 0.95f ? 0.95f : d;
    }
    dz_ = c.book_mode ? 0x333 : int(f * 1024.0f);
}

void Mapper::reset() {
    cur_ = {};
    held_prev2_ = latch_ = dbl_count_ = repeat_count_ = 0;
}

const Frame& Mapper::step(const Snapshot& s) {
    const std::uint32_t prev = cur_.held;   // the previous block's +0x20 (the edge reference)
    const std::uint32_t prev2 = held_prev2_; // the block being overwritten (two frames back)
    Frame f;
    f.wheel = s.wheel;

    // ---- the digital direction: slots 11..18, then the fixed arrows [BYTES 0x5d2c..0x5d2d] -------
    // Retail tests keyboard[code] != 0 for these slots whatever the code; a code >= 0x100 reads the
    // mouse / joystick bytes behind the keyboard array [not modelled: never pressed here].
    auto key = [&](Code c) { return c < 0x100 && s.key[c]; };
    static constexpr std::uint32_t kSlotDir[8] = {8, 1, 2, 4, 10, 12, 3, 5};   // UP..DOWNRIGHT
    std::uint32_t dir = 0;
    for (int i = 0; i < 8; ++i)
        if (key(cfg_.assign[std::size_t(KEY_UP + i)])) dir |= kSlotDir[i];
    if (s.key[kDikUp]) dir |= 8;
    if (s.key[kDikDown]) dir |= 1;
    if (s.key[kDikLeft]) dir |= 2;
    if (s.key[kDikRight]) dir |= 4;
    // Opposite pairs: the latch 0x7647f4 keeps which one was held alone last; the newer press wins.
    if ((dir & 9) == 9) dir &= (latch_ & 1) ? ~8u : ~1u;
    else { latch_ &= ~1u; if (dir & 8) latch_ |= 1; }
    if ((dir & 6) == 6) dir &= (latch_ & 2) ? ~2u : ~4u;
    else { latch_ &= ~2u; if (dir & 2) latch_ |= 2; }

    float mag = dir != 0 ? 1.0f : 0.0f;   // 0x6bd438 = 1.0
    int x = 0, y = 0;
    float angle;
    if (!cfg_.game_pad || !s.pad_present) {
        angle = kAngle[dir & 15];
    } else {
        // The stick: (lX, lY) outside the deadzone, else (lRx, lRy), else the first POV hat
        // [BYTES 0x5d2df5..0x5d2f1d]. The deadzone test is on the squared int length.
        const int dz2 = dz_ * dz_;
        int sy = 0, sx = 0;
        if (s.lx * s.lx + s.ly * s.ly >= dz2) { sy = s.ly; sx = s.lx; }
        if (sx == 0 && sy == 0) { sy = s.ry; sx = s.rx; }
        if (sx * sx + sy * sy >= dz2) { x = sx; y = sy; }
        bool hat = false;
        if (x == 0 && y == 0 && s.pov != -1) {
            hat = true;
            // (pi/2 - pov * 1.745329e-4) in float, then cos / sin in double, x 1024 / x -1024, truncated.
            const float a = 1.5707963705062866f - float(s.pov) * 0.0001745329354889691f;
            x = int(float(std::cos(double(a))) * 1024.0f);
            y = int(float(std::sin(double(a))) * -1024.0f);
        }
        if (dir != 0 || (x == 0 && y == 0)) {
            angle = kAngle[dir & 15];       // digital keys win over the stick
        } else {
            const float fx = float(x), fy = float(y);
            if (cfg_.linear_magnitude) {    // [cleria.input] LinearStickMagnitude [H, CleriaCore QoL]
                mag = float(std::sqrt(double(fx * fx + fy * fy))) * 0.0009765625f;
                if (mag > 1.0f) mag = 1.0f;
            } else {                        // retail: sqrt(x^4 + y^4) / 1024 [BYTES 0x5d2f37..0x5d2f6f]
                const float x2 = fx * fx, y2 = fy * fy;
                mag = float(std::sqrt(double(y2 * y2 + x2 * x2))) * 0.0009765625f;
            }
            angle = float(std::atan2(double(-fy), double(fx)));   // _CIatan2(-y, x)
            // Quantise: 8-way (angle + pi + pi/8) * 4/pi, 4-way (angle + pi + pi/4) * 2/pi
            // [BYTES 0x5d2fb2..0x5d300d]; the operand is always positive, so truncation = floor.
            if (!cfg_.book_mode) {
                const float q = ((angle + kPi) + 0.39269909262657166f) * 1.2732394933700562f;
                dir = kQuant8[int(q) & 7];
            } else {
                const float q = ((angle + kPi) + 0.7853981852531433f) * 0.6366197466850281f;
                dir = kQuant4[int(q) & 3];
            }
            if (hat) mag = 1.0f;
            if (cfg_.pad_analog) { mag = 1.0f; angle = kAngle[dir & 15]; }
            else if (angle == std::numeric_limits<float>::max()) angle = kAngle[dir & 15];   // 0x6bdf90
            else f.analog = true;
        }
    }
    f.stick_x = x; f.stick_y = y;
    f.dir = dir; f.angle = angle; f.magnitude = mag;

    // ---- the buttons: three slot groups, bit = index within the group [BYTES 0x5d30a0..0x5d31c1] ----
    std::uint32_t held = 0, pressed = 0;
    const bool aoc = cfg_.mouse_ok_cancel;
    for (int g = 0; g < 3; ++g) {
        for (int i = 0; i < kGroupCount[g]; ++i) {
            const Code c = cfg_.assign[std::size_t(kGroupFirst[g] + i)];
            if (c == kUnbound) continue;
            bool v = false;
            bool mouse_aoc = aoc;            // bl: the "mouse code under AlwaysOkCancel" flag
            if (c < 0x100) { v = s.key[c]; mouse_aoc = false; }
            else if (c >= 0x107) {
                if (cfg_.game_pad) { mouse_aoc = false; v = (c - 0x107) < 32 && s.pad[c - 0x107]; }
            } else if (c - 0x100 < 3) {
                v = s.mouse[c - 0x100];
            }
            const std::uint32_t bit = v ? (1u << i) : 0u;
            held |= bit;
            if (bit && !(prev & bit)) pressed |= bit;
            if (bit == 1) {
                if (c - 0x100 <= 6) {        // a mouse code (incl. the "---" placeholders)
                    if (mouse_aoc) continue;
                    held |= kClick;
                    pressed |= (pressed & 1) << 15;   // 0x8000
                } else {
                    held |= kOk;
                    pressed |= (pressed & 1) << 13;   // 0x2000
                }
            } else if (bit == 2 && !mouse_aoc) {
                held |= kCancel;
                pressed |= (pressed & 2) << 13;       // 0x4000
            }
        }
    }
    if (aoc) {                                // MouseButtonAlwaysOkCancel: L = click, R = cancel
        if (s.mouse[0]) { held |= kClick; if (!(prev & kClick)) pressed |= kClick; }
        if (s.mouse[1]) { held |= kCancel; if (!(prev & kCancel)) pressed |= kCancel; }
    }
    // MOUSE_DIR double click: a release (seen one frame late) within DoubleClickFrame of the last
    // [BYTES 0x5d3215..0x5d3253].
    if (!(prev & kMouseDir) && (prev2 & kMouseDir)) {
        if (dbl_count_ <= cfg_.double_click_frame) pressed |= kDoubleClick;
        dbl_count_ = 0;
    } else {
        ++dbl_count_;
    }
    // MOUSE_DIR held: a key, or the mouse byte at code - 0x100 (0x103 = rgbButtons[3] = X1; 0x104..0x106
    // read the joystick's lX bytes behind DIMOUSESTATE [not modelled: never pressed]).
    if (const Code c = cfg_.assign[MOUSE_DIR]; c != kUnbound) {
        bool v = false;
        if (c < 0x100) v = s.key[c];
        else if (c <= 0x103) v = s.mouse[c - 0x100];
        if (v) { held |= kMouseDir; if (!(prev & kMouseDir)) pressed |= kMouseDir; }
    }
    // Esc is fixed to the menu bit (DIK 1) [BYTES 0x5d3294].
    if (s.key[kDikEscape]) { held |= kMenu; if (!(prev & kMenu)) pressed |= kMenu; }
    // [not modelled] the per-bit press timestamps 0x7647b0[] (timeGetTime) and the cursor transform.
    f.held = held;
    f.pressed = pressed;

    // ---- menu auto-repeat: frame 1, 28, then every 5 [BYTES 0x5d32d8..0x5d336e] ---------------------
    if (dir == 0 && held == 0) {
        repeat_count_ = 0;
    } else {
        ++repeat_count_;
        bool fire;
        if (repeat_count_ < 0x21) fire = repeat_count_ == 1 || repeat_count_ == 0x1c;
        else { repeat_count_ = 0x1c; fire = true; }
        if (fire) {
            if (held != 0) f.repeat = held;
            else {
                if (dir & 2) f.dx = -1;
                if (dir & 4) f.dx = 1;
                if (dir & 1) f.dy = 1;
                if (dir & 8) f.dy = -1;
            }
        }
    }
    held_prev2_ = prev;
    cur_ = f;
    return cur_;
}

}  // namespace cleria::input
