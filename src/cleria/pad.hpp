#pragma once
// The gamepad layer of CleriaCore's shell (the Input > Controller page): the [cleria.pad] settings,
// the button glyph sets, the PAD_* bindings as SDL buttons with conflict swapping, the analog-walk
// magnitude shaping, and retail's six force-feedback effects. Pure: no SDL header (the SDL enum
// values it relies on are static_assert-ed in apps/cleria-view/view_pad.inl).
//
// Spec: build/shell_re/PAD.md. Provenance tags: [BYTES] read from yso_win.exe, [EXPORT] the Ghidra
// export, [H] hand-chosen, [not modelled].
#include <array>
#include <cstdint>
#include <string>

namespace cleria {

class Ini;
struct Settings;

// ---- [cleria.pad] (CleriaCore-only; every default = retail behaviour) ---------------------------
// The retail keys the Controller page also edits stay where retail keeps them: [input] DeadZone
// (inner deadzone), RunWalkThreshold, ForceFeedback (vibration), GamePad, GamePadID; and
// [cleria.input] LinearStickMagnitude (analog walk) / NativePadBinds / PadBind{}.
struct PadSettings {
    int glyphs = 0;                    // Glyphs: 0 auto (from the pad), 1 xbox, 2 playstation, 3 nintendo, 4 generic
    float outer_dead_zone = 1.0f;      // OuterDeadZone: |v| at which analog walk saturates (1.0 = none, as retail) [H]
    int response_curve = 0;            // ResponseCurve: 0 linear (as LinearStickMagnitude always was), 1 smooth [H]
    float trigger_threshold = 0.25f;   // TriggerThreshold: LT / RT read as a button past this pull [H, CleriaCore]
    bool swap_confirm_cancel = false;  // SwapConfirmCancel: the pad's OK / Cancel swapped in menus (Nintendo style)
    float vibration_intensity = 1.0f;  // VibrationIntensity: x retail's effect magnitude [H]
};
void read_pad(const Ini& ini, PadSettings& p);
void write_pad(Ini& ini, const PadSettings& p);

namespace pad {

// ---- glyph sets -------------------------------------------------------------------------------
enum Glyphs : int { kAuto = 0, kXbox = 1, kPlayStation = 2, kNintendo = 3, kGeneric = 4, kGlyphCount = 5 };
const char* glyphs_name(int g);           // "auto" / "xbox" / ... (the ini spelling)
int glyphs_from_name(const std::string& s);   // -> 0..4, unknown = 0
// SDL_GamepadType -> the glyph set (UNKNOWN / STANDARD -> generic).
Glyphs glyphs_for_sdl_type(int sdl_gamepad_type);
// `opt` = PadSettings::glyphs; auto takes the pad's type (generic without a pad).
Glyphs resolve_glyphs(int opt, int sdl_gamepad_type, bool have_pad);
// Xbox and Nintendo pads put the left stick above the d-pad; PlayStation and generic DInput pads
// have the d-pad on top and the sticks side by side.
bool offset_sticks(Glyphs g);

// SDL_GamepadButton values (positional: SOUTH is the bottom face button on every pad) plus the
// two triggers as input::kSdlLeftTrigger / kSdlRightTrigger (1000 / 1001).
enum Button : int {
    kSouth = 0, kEast, kWest, kNorth, kBack, kGuide, kStart, kLeftStick, kRightStick, kLeftShoulder,
    kRightShoulder, kDpadUp, kDpadDown, kDpadLeft, kDpadRight, kMisc1,
    kLeftTrigger = 1000, kRightTrigger = 1001,
};
const char* button_short(Glyphs g, int b);   // the diagram's label: "A", "L1", "ZR", "+" ("" = drawn as a shape)
const char* button_name(Glyphs g, int b);    // the list's name: "A", "Cross", "Left Bumper", "ZR"
bool is_dpad(int b);

// ---- the PAD_* bindings as SDL buttons ---------------------------------------------------------
// The eight pad slots (PAD_ACTION..PAD_SWORD_REVU); -1 = unbound. In both storage modes the mapper
// sees a "Button n" code (input::pad_code_from_sdl_name / the XInput->DInput order), so one SDL
// button per slot describes either.
constexpr int kPadActions = 8;
using PadButtons = std::array<int, kPadActions>;
const char* action_short(int a);             // "Attack", "Jump", "Magic", "Boost", "Menu", "Walk", ...
int sdl_from_dinput(int dinput_index);       // the inverse of input::pad_button_from_sdl; -1 if none
PadButtons pad_buttons(const Settings& s);
void set_pad_buttons(Settings& s, const PadButtons& b);   // into Assign{PAD_*} or PadBind{} per NativePadBinds
// Bind `action` to `button`. If another action already uses it, the two swap (that action takes
// `action`'s old button, or none). Returns the displaced action or -1. A d-pad direction is refused
// (returns -2, nothing changes): the d-pad always moves (the POV hat, CONFIG.md §3.4).
int bind_swap(PadButtons& b, int action, int button);
int action_on(const PadButtons& b, int button);   // the action bound to `button`, or -1
// "Reset to default": the retail Preset 1 pad slots, or with NativePadBinds the CleriaCore names.
void reset_pad_bindings(Settings& s);

// ---- the stick ---------------------------------------------------------------------------------
// Analog walk ([cleria.input] LinearStickMagnitude) magnitude from r = |v| / 1024 and the inner
// deadzone fraction (the mapper's 0x7647a8 / 1024):
//   linear: min(r / outer, 1)                       (outer 1.0 = the original LinearStickMagnitude)
//   smooth: u = clamp((r - dz) / (outer - dz)), u^2  (rescaled from the deadzone edge, fine near it)
// Retail's own magnitude (sqrt(x^4 + y^4) / 1024 >= ~125 past any deadzone) never uses this.
float shaped_magnitude(float r, float dz, float outer, int curve);

// ---- force feedback [EXPORT FUN_005d5b20 / FUN_005d3500, BYTES 0x6b8df8] ----------------------
// Retail creates six GUID_Sine effects (0x13541c23, period 50 ms, gain 10000) from the table at
// 0x6b8df8 = {magnitude, duration us} x 6, and FUN_005d3500(n) stops the last effect and starts
// effect n — only with [input] GamePad and ForceFeedback on.
constexpr int kEffects = 6;
struct Rumble { std::uint16_t low = 0, high = 0; std::uint32_t ms = 0; };
Rumble retail_effect(int id, float intensity);   // id out of range -> all zero
float effect_magnitude(int id);                  // 0.5 / 1.0
std::uint32_t effect_ms(int id);                 // 200 / 400 / 1000
// The player's hit reaction (FUN_004fd350 0x4fde25..0x4fdfb5): the effect for the section the hit
// put the player in (4 small -> 0, 0xb medium -> 1, 9 knockdown -> 3, 5 death -> 3), -1 none.
int hit_effect(int motion_state);
// The vibration test button (config dialog 3006, FUN_0041bf10): effect 5.
constexpr int kTestEffect = 5;

}  // namespace pad
}  // namespace cleria
