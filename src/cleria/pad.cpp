// The gamepad layer of the shell. See pad.hpp and build/shell_re/PAD.md.
#include "cleria/pad.hpp"

#include <algorithm>
#include <cctype>

#include "cleria/input_map.hpp"
#include "cleria/settings.hpp"

namespace cleria {

namespace {
constexpr const char* kSec = "cleria.pad";
const char* const kCurve[2] = {"linear", "smooth"};
std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}
}  // namespace

void read_pad(const Ini& ini, PadSettings& p) {
    if (auto v = ini.get_string(kSec, "Glyphs")) p.glyphs = pad::glyphs_from_name(*v);
    if (auto v = ini.get_float(kSec, "OuterDeadZone")) p.outer_dead_zone = std::clamp(*v, 0.5f, 1.0f);
    if (auto v = ini.get_string(kSec, "ResponseCurve")) p.response_curve = lower(*v) == "smooth" ? 1 : 0;
    if (auto v = ini.get_float(kSec, "TriggerThreshold")) p.trigger_threshold = std::clamp(*v, 0.05f, 0.95f);
    if (auto v = ini.get_int(kSec, "SwapConfirmCancel")) p.swap_confirm_cancel = *v != 0;
    if (auto v = ini.get_float(kSec, "VibrationIntensity")) p.vibration_intensity = std::clamp(*v, 0.0f, 1.0f);
}

void write_pad(Ini& ini, const PadSettings& p) {
    ini.set_string(kSec, "Glyphs", pad::glyphs_name(p.glyphs));
    ini.set_float(kSec, "OuterDeadZone", p.outer_dead_zone);
    ini.set_string(kSec, "ResponseCurve", kCurve[p.response_curve == 1 ? 1 : 0]);
    ini.set_float(kSec, "TriggerThreshold", p.trigger_threshold);
    ini.set_int(kSec, "SwapConfirmCancel", p.swap_confirm_cancel);
    ini.set_float(kSec, "VibrationIntensity", p.vibration_intensity);
}

namespace pad {

// ---- glyphs ------------------------------------------------------------------------------------
namespace {
const char* const kGlyphNames[kGlyphCount] = {"auto", "xbox", "playstation", "nintendo", "generic"};

struct Label { const char* s; const char* n; };   // short (diagram), name (list)
// Indexed [glyph set 1..4][SDL button 0..15]; the triggers separately.
const Label kLabels[4][16] = {
    {   // Xbox
        {"A", "A"}, {"B", "B"}, {"X", "X"}, {"Y", "Y"}, {"View", "View"}, {"Xbox", "Xbox button"},
        {"Menu", "Menu"}, {"LS", "Left Stick (press)"}, {"RS", "Right Stick (press)"}, {"LB", "Left Bumper"},
        {"RB", "Right Bumper"}, {"Up", "D-pad Up"}, {"Down", "D-pad Down"}, {"Left", "D-pad Left"},
        {"Right", "D-pad Right"}, {"Share", "Share"},
    },
    {   // PlayStation (the face buttons are drawn as their shapes)
        {"", "Cross"}, {"", "Circle"}, {"", "Square"}, {"", "Triangle"}, {"Share", "Share / Create"},
        {"PS", "PS button"}, {"Options", "Options"}, {"L3", "L3"}, {"R3", "R3"}, {"L1", "L1"}, {"R1", "R1"},
        {"Up", "D-pad Up"}, {"Down", "D-pad Down"}, {"Left", "D-pad Left"}, {"Right", "D-pad Right"},
        {"Mic", "Mute"},
    },
    {   // Nintendo: positional SOUTH is the B button, EAST is A, WEST is Y, NORTH is X
        {"B", "B"}, {"A", "A"}, {"Y", "Y"}, {"X", "X"}, {"-", "Minus"}, {"Home", "Home"}, {"+", "Plus"},
        {"LS", "Left Stick (press)"}, {"RS", "Right Stick (press)"}, {"L", "L"}, {"R", "R"}, {"Up", "D-pad Up"},
        {"Down", "D-pad Down"}, {"Left", "D-pad Left"}, {"Right", "D-pad Right"}, {"Cap", "Capture"},
    },
    {   // generic DirectInput pad: the face buttons by the retail "Button n" they become
        {"1", "Button 1 (bottom)"}, {"2", "Button 2 (right)"}, {"3", "Button 3 (left)"}, {"4", "Button 4 (top)"},
        {"Back", "Back / Select"}, {"Guide", "Guide"}, {"Start", "Start"}, {"L3", "L3"}, {"R3", "R3"},
        {"L1", "L1"}, {"R1", "R1"}, {"Up", "D-pad Up"}, {"Down", "D-pad Down"}, {"Left", "D-pad Left"},
        {"Right", "D-pad Right"}, {"Misc", "Misc"},
    },
};
const Label kTriggers[4][2] = {
    {{"LT", "Left Trigger"}, {"RT", "Right Trigger"}},
    {{"L2", "L2"}, {"R2", "R2"}},
    {{"ZL", "ZL"}, {"ZR", "ZR"}},
    {{"L2", "L2"}, {"R2", "R2"}},
};
const Label* label_of(Glyphs g, int b) {
    const int k = std::clamp(int(g), 1, 4) - 1;
    if (b == kLeftTrigger) return &kTriggers[k][0];
    if (b == kRightTrigger) return &kTriggers[k][1];
    if (b >= 0 && b < 16) return &kLabels[k][b];
    return nullptr;
}
}  // namespace

const char* glyphs_name(int g) { return kGlyphNames[std::clamp(g, 0, kGlyphCount - 1)]; }
int glyphs_from_name(const std::string& s) {
    const std::string l = lower(s);
    for (int i = 0; i < kGlyphCount; ++i) if (l == kGlyphNames[i]) return i;
    return 0;
}
Glyphs glyphs_for_sdl_type(int t) {
    switch (t) {
        case 2: case 3: return kXbox;                          // XBOX360, XBOXONE
        case 4: case 5: case 6: return kPlayStation;           // PS3, PS4, PS5
        case 7: case 8: case 9: case 10: case 11: return kNintendo;   // Switch Pro / Joy-Cons / GameCube
        default: return kGeneric;                              // UNKNOWN, STANDARD
    }
}
Glyphs resolve_glyphs(int opt, int t, bool have_pad) {
    if (opt >= kXbox && opt <= kGeneric) return Glyphs(opt);
    return have_pad ? glyphs_for_sdl_type(t) : kGeneric;
}
bool offset_sticks(Glyphs g) { return g == kXbox || g == kNintendo; }

const char* button_short(Glyphs g, int b) {
    const Label* l = label_of(g, b);
    if (b >= 16 && b < 1000) return "P";   // paddles / touchpad
    return l ? l->s : "?";
}
const char* button_name(Glyphs g, int b) {
    if (b >= 16 && b < 1000) {
        static const char* const kExtra[] = {"Right Paddle 1", "Left Paddle 1", "Right Paddle 2", "Left Paddle 2"};
        return b - 16 < 4 ? kExtra[b - 16] : "Extra button";
    }
    const Label* l = label_of(g, b);
    return l ? l->n : "---";
}
bool is_dpad(int b) { return b >= kDpadUp && b <= kDpadRight; }

// ---- bindings ----------------------------------------------------------------------------------
const char* action_short(int a) {
    static const char* const k[kPadActions] = {"Attack", "Jump", "Magic", "Boost", "Menu", "Walk", "Prev Skill",
                                               "Next Skill"};
    return a >= 0 && a < kPadActions ? k[a] : "";
}

int sdl_from_dinput(int d) {
    if (d < 0) return -1;
    for (int b = 0; b < 32; ++b) if (input::pad_button_from_sdl(b) == d) return b;
    if (input::pad_button_from_sdl(kLeftTrigger) == d) return kLeftTrigger;
    if (input::pad_button_from_sdl(kRightTrigger) == d) return kRightTrigger;
    return -1;
}

PadButtons pad_buttons(const Settings& s) {
    PadButtons b{};
    for (int i = 0; i < kPadActions; ++i) {
        b[std::size_t(i)] = -1;
        if (s.cleria.native_pad_binds) {
            if (auto x = input::sdl_pad_from_name(s.cleria.pad_bind[std::size_t(i)]))
                if (input::pad_button_from_sdl(*x) >= 0) b[std::size_t(i)] = *x;
        } else {
            const input::Code c = s.input.assign[std::size_t(input::PAD_ACTION + i)];
            if (c >= input::kPadButton1 && c < input::kPadButton1 + 32)
                b[std::size_t(i)] = sdl_from_dinput(int(c - input::kPadButton1));
        }
    }
    return b;
}

void set_pad_buttons(Settings& s, const PadButtons& b) {
    for (int i = 0; i < kPadActions; ++i) {
        const int x = b[std::size_t(i)];
        const int d = x >= 0 ? input::pad_button_from_sdl(x) : -1;
        if (s.cleria.native_pad_binds) {
            s.cleria.pad_bind[std::size_t(i)] = d >= 0 ? input::sdl_pad_name(x) : "";
        } else {
            // "---" (0x103) is retail's own unbound spelling; it survives a reload without FileIndex.
            s.input.assign[std::size_t(input::PAD_ACTION + i)] = d >= 0 ? input::kPadButton1 + input::Code(d) : 0x103;
        }
    }
}

int action_on(const PadButtons& b, int button) {
    if (button < 0) return -1;
    for (int i = 0; i < kPadActions; ++i) if (b[std::size_t(i)] == button) return i;
    return -1;
}

int bind_swap(PadButtons& b, int action, int button) {
    if (action < 0 || action >= kPadActions) return -1;
    if (is_dpad(button) || (button >= 0 && input::pad_button_from_sdl(button) < 0)) return -2;
    int other = -1;
    for (int i = 0; i < kPadActions; ++i)
        if (i != action && b[std::size_t(i)] == button) other = i;
    if (other >= 0) b[std::size_t(other)] = b[std::size_t(action)];
    b[std::size_t(action)] = button;
    return other;
}

void reset_pad_bindings(Settings& s) {
    if (s.cleria.native_pad_binds) {
        s.cleria.pad_bind = Settings{}.cleria.pad_bind;
    } else {
        for (int i = 0; i < kPadActions; ++i)
            s.input.assign[std::size_t(input::PAD_ACTION + i)] = input::preset1()[std::size_t(input::PAD_ACTION + i)];
    }
}

// ---- the stick ---------------------------------------------------------------------------------
float shaped_magnitude(float r, float dz, float outer, int curve) {
    outer = std::clamp(outer, 0.05f, 1.0f);
    if (curve != 1) {
        const float m = r / outer;
        return m > 1.0f ? 1.0f : m;
    }
    const float span = std::max(outer - dz, 0.01f);
    const float u = std::clamp((r - dz) / span, 0.0f, 1.0f);
    return u * u;
}

// ---- force feedback ----------------------------------------------------------------------------
namespace {
// [BYTES 0x6b8df8] {dwMagnitude (of 10000), dwDuration (us)} x 6.
constexpr std::uint32_t kEffect[kEffects][2] = {
    {5000, 200000}, {10000, 200000}, {5000, 400000}, {10000, 400000}, {5000, 1000000}, {10000, 1000000},
};
}  // namespace

float effect_magnitude(int id) { return id >= 0 && id < kEffects ? float(kEffect[id][0]) / 10000.0f : 0.0f; }
std::uint32_t effect_ms(int id) { return id >= 0 && id < kEffects ? kEffect[id][1] / 1000u : 0u; }

Rumble retail_effect(int id, float intensity) {
    Rumble r;
    if (id < 0 || id >= kEffects) return r;
    // [H] the sine's magnitude as both motors' speed (SDL rumble has no waveform).
    const float m = std::clamp(effect_magnitude(id) * std::clamp(intensity, 0.0f, 1.0f), 0.0f, 1.0f);
    r.low = r.high = std::uint16_t(m * 65535.0f + 0.5f);
    r.ms = effect_ms(id);
    return r;
}

int hit_effect(int motion_state) {
    switch (motion_state) {
        case 4: return 0;      // 0x4fdf58 (and rtype 10, 0x4fdf02)
        case 0xb: return 1;    // 0x4fdf7d: esi 0xb - 0xa
        case 9: return 3;      // 0x4fdfb5: esi 9 - 6
        case 5: return 3;      // 0x4fde93: the lethal hit (no revive item)
        default: return -1;
    }
}

}  // namespace pad
}  // namespace cleria
