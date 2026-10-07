#pragma once
// CleriaCore's accessibility assists and cheats (build/shell_re/ACCESS.md). They are NOT part of the
// faithful game: the shell's Gameplay / Cheats / Interface pages edit them, cleria.ini keeps them
// ([cleria.access] / [cleria.cheats]), and every default is retail behaviour (1x, off) -- except the
// cutscene skip, whose default is CleriaCore's own single-press skip that predates this file (retail
// has no cutscene skip at all: ACCESS.md §1).
//
// This header is the pure part: the settings model, the multiplier math at the game-code hook sites,
// the cutscene-skip state machine and the flash limiter. The view (apps/cleria-view/view_access.inl)
// owns the pages, the prompt and the hooks. Every tuned value here is [H].
#include <cstdint>
#include <string_view>

namespace cleria {
class Ini;

namespace access {

// How the cutscene skip button (the mapper's menu bit 0x10: Esc / Camp Menu / the pad's menu button)
// skips a cutscene that holds the world.
enum class SkipMode : int {
    Single = 0,   // one press skips (CleriaCore's skip since 2026-07-23; the default)
    Double = 1,   // the first press shows "Press again to skip"; a second within the window skips
    Hold = 2,     // hold the button for kHoldTicks
    Off = 3,      // never (retail: no cutscene skip exists)
};
// The message box's typewriter (WndText / WndDialog reveal rate, WNDDIALOG_DRAW.md §2.4).
enum class TextSpeed : int {
    Retail = 0,   // 1.125 glyphs / tick (0x12000), 5 while Confirm / Cancel is held
    Fast = 1,     // always retail's own fast rate (0x50000 = 5 glyphs / tick)
    Instant = 2,  // the whole message at once
};

struct Settings {
    // [cleria.access]
    int cutscene_skip = int(SkipMode::Double);   // CutsceneSkip (the user's choice, 2026-10-07)
    int text_speed = int(TextSpeed::Retail);     // TextSpeed
    bool reduce_flashes = false;                 // ReduceFlashes
    float shake = 1.0f;                          // ScreenShake: 0..1 of the camera shake's offset
    float game_speed = 1.0f;                     // GameSpeed: x the logic ticks per real second
    // [cleria.cheats]
    bool inf_hp = false, inf_mp = false, inf_boost = false, inf_o2 = false;   // InfiniteHP / MP / Boost / Oxygen
    bool one_hit_kill = false;                   // OneHitKill
    float exp_rate = 1.0f;                       // ExpRate: x the kill EXP (retail EXP rate 0x76a5fc = 1.0)
    float sp_rate = 1.0f;                        // SpRate: x the gold -> SP pickups (retail SP rate 0x76a5f8)
    float drop_rate = 1.0f;                      // DropRate: x every enemy drop chance (FUN_00478510)
    float damage_dealt = 1.0f;                   // DamageDealt: x the damage the player's hits do
    float damage_taken = 1.0f;                   // DamageTaken: x the damage the player takes (0 = none)

    void reset_cheats();                         // the [cleria.cheats] block back to retail
    bool cheats_active() const;                  // any cheat differs from retail
};

// The value ranges the shell offers (and the reader clamps to).
inline constexpr float kSpeedMin = 0.25f, kSpeedMax = 2.0f;
inline constexpr float kRateMin = 0.0f, kRateMax = 10.0f;      // EXP / SP / drop rates
inline constexpr float kDealtMin = 0.25f, kDealtMax = 10.0f;
inline constexpr float kTakenMin = 0.0f, kTakenMax = 4.0f;

void read(const Ini& ini, Settings& s);
void write(Ini& ini, const Settings& s);
// One key of either section by its cleria.ini name ("ExpRate", "2"); false for an unknown key.
// (CLERIA_DRIVE `opt=Key=value`.)
bool set_by_key(Settings& s, std::string_view key, std::string_view value);

// ---- the multipliers at the hook sites --------------------------------------------------------------
// A hit the player lands (FUN_004fa550 / FUN_004faf20's result): 0 stays 0 (a guard, an immune
// element); otherwise at least 1, like the engine's own min-1 chip.
int damage_dealt(int dmg, float mult);
// A hit the player takes (FUN_004fa740 after the Boost halving): 0 at mult 0 (invincible);
// otherwise at least 1 for a hit that did damage.
int damage_taken(int dmg, float mult);

// ---- the cutscene skip --------------------------------------------------------------------------------
inline constexpr int kDoubleWindow = 120;   // [H] ticks (2 s) for the second press
inline constexpr int kHoldTicks = 60;       // [H] ticks (1 s) to hold
inline constexpr int kHintTicks = 120;      // [H] how long a Hold-mode tap shows "Hold to skip"

// One per game. step() runs once per logic tick while a cutscene holds the world (a Fix, not over a
// script menu); reset() on every other tick. `pressed` / `held` are the mapper record's menu bit.
struct SkipGate {
    int window = 0;          // Double: ticks left for the second press; Hold: ticks left of the hint
    int hold = 0;            // Hold: ticks the button has been held while the gate was stepping
    bool holding = false;    // Hold: the button is held (the fill shows)

    bool step(SkipMode m, bool pressed, bool held);
    void reset() { *this = SkipGate{}; }
    // What the prompt shows (0 = nothing).
    enum class Prompt { None, PressAgain, Hold };
    Prompt prompt(SkipMode m) const;
    float hold_fraction() const { return float(hold) / float(kHoldTicks); }
    float window_fraction() const { return float(window) / float(kDoubleWindow); }
};

// ---- reduce flashes --------------------------------------------------------------------------------------
// The script fader (ops 0xc8..0xca Fader_Current / Target / Color) can fill the screen with a bright
// colour in one frame -- a white flash. With ReduceFlashes the overlay shown is limited: a bright
// fade's colour is darkened to at most kFlashLum and its alpha rises at most kFlashRise a frame, so a
// 0 -> 1 white flash becomes a soft grey swell over ~8 frames. Dark fades (black transitions) are
// untouched. Pure presentation: the fader's own state (and every script wait on it) is unchanged.
inline constexpr float kFlashLum = 0.55f;   // [H] the brightest colour a reduced fade shows
inline constexpr float kFlashRise = 0.12f;  // [H] the most a bright fade's alpha rises per frame
inline constexpr float kBrightLum = 0.35f;  // [H] a fade colour at or above this luminance is "bright"

struct FlashLimiter {
    float shown = 0.0f;      // the alpha shown last frame
    // In: the fader's colour 0xRRGGBB and alpha. Out: the colour / alpha to draw.
    void step(bool enabled, std::uint32_t rgb, float alpha, std::uint32_t* out_rgb, float* out_alpha);
};
float luminance(std::uint32_t rgb);   // Rec. 709 luma of 0xRRGGBB, 0..1

}  // namespace access
}  // namespace cleria
