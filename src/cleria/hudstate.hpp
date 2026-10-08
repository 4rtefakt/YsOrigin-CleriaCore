// The state-driven parts of the status HUD that the playtest of 2026-10-07 found missing
// (build/audit/PLAYTEST_1_HUD.md): builder B's "skill ready" overlays, the buff / combo timer
// bars of builder A, builder D's buff and combo lines, and the buff expiry of FUN_00462c40.
// Pure functions; the host (apps/cleria-view/view_hud.inl, view_hudstate.inl) draws.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "cleria/inventory.hpp"
#include "cleria/skillmp.hpp"

namespace cleria::hudstate {

// Builder B FUN_0043e800 0x43e815..0x43e898: the alpha byte of the two additive "ready" quads.
// `counter` is DAT_0076c184, or DAT_0076c188 while the skill is charged (c138 == 2); FUN_0043a540
// adds 3 / 6 to them for widget 0 (0x43a558 / 0x43a55f). idx = (trunc(c * 0.7111) - 64) & 0xff
// (0x6be028 = -0.7111111), alpha = 0xcc - trunc(LUT[idx] * -51) (0x6be120 = -51.0), LUT =
// DAT_0068f350, the 256-entry cosine table (computed here, not read: [approx] in the last bit).
// So a 153..255 sine pulse: 120 ticks a cycle, 60 when charged.
inline std::uint8_t pulse_alpha(std::int32_t counter) {
    const int k = int(float(counter) * -0.71111113f);          // cvttss2si
    const unsigned idx = unsigned(-64 - k) & 0xffu;
    const float lut = float(std::cos(double(idx) * 6.283185307179586 / 256.0));
    return std::uint8_t((0xcc - int(lut * -51.0f)) & 0xff);
}
// The colour the quads take (0x43e87f..0x43e898): the HUD tint DAT_0074e68c's RGB, its alpha
// scaled by the pulse, (A * pulse) >> 8.
inline std::uint8_t pulse_scaled(std::uint8_t hud_a, std::int32_t counter) {
    return std::uint8_t((unsigned(hud_a) * pulse_alpha(counter)) >> 8);
}

// Builder B's affordability test (0x43e89a..0x43edb3). With a "Reduce MP usage" blessing
// (DAT_0076bc80 & 0x80000 / 0x40000 / 0x40) or the Ring of Ease (0x76a624 & 0x200000) the code
// takes cost x 0.80 / 0.88 / 0.94 and jumps straight to the "!= 0" test (0x43e933 .. 0x43eca4:
// jmp 0x43edb3, past the comiss at 0x43eda6), so the overlays are on whatever the MP — a quirk
// of the retail code, kept. Otherwise it is `cost <= MP` (0x43eda6: comiss MP, cost; setae), the
// cost being the per-character table (+ Nightmare's), halved for Yunica in Boost.
// [not modelled] Adol's DAT_0074c0cc table drops the Nightmare add here (skillmp adds it).
inline bool skill_ready(const skillmp::CostIn& in, float mp) {
    if (in.slot < 0 || in.slot > 2) return false;              // FUN_0046d560 == 3: nothing drawn
    if ((in.bc80 & (0x80000u | 0x40000u | 0x40u)) != 0 || in.acc_mp) return true;
    skillmp::CostIn plain = in;
    plain.bc80 = 0; plain.acc_mp = false;
    return skillmp::cost_f(plain) <= mp;
}

// FUN_00462c40 0x462c86..0x462d6b, once a logic tick outside a Fix: each active buff counts its
// timer down; at 0 the buff ends (the bit off, the value 0, its aura effect +0x74c/+0x750/+0x754
// killed) and the stats are recomputed (FUN_00420c40 with 0x75, 0x462e0a..0x462e16). The engine's
// active bits DAT_0076c198 & 1 / 8 / 0x10 are set together with a non-zero value by the pickup
// (FUN_00463f40), so "value != 0" stands for the bit here. Returns true when a buff ended.
inline bool tick_buffs(InvState& inv) {
    bool ended = false;
    auto one = [&](float& v, std::int32_t& t) {
        if (v == 0.0f) return;
        if (t != 0) { --t; return; }
        v = 0.0f;
        ended = true;
    };
    one(inv.atk_buff, inv.timer_atk);
    one(inv.def_buff, inv.timer_def);
    one(inv.mp_buff, inv.timer_mp);
    return ended;
}

// The timer the buff bars are a fraction of (builder A 0x43d9b1.., the same choice as the pickup's
// FUN_00463f40): 2200 with blessing 0x800000, 2000 with 0x10, else 1800.
inline float buff_full(std::uint32_t bc80) {
    return (bc80 & 0x800000u) ? 2200.0f : (bc80 & 0x10u) ? 2000.0f : 1800.0f;
}
// Builder A's four timer bars at x 0, 12 px rows y 620 / 632 / 644 / 656, width t / full x 168
// (0x6bdb80), from STATUS_512 U 272 (0x6bd31c), rows V 464 / 476 / 488 / 500: str / def / mp
// buff, then the combo meter (full 720, 0x6bddb0). Drawn while no Fix holds (0x43d9a5).
inline float bar_width(std::int32_t timer, float full) {
    return float(std::uint32_t(timer)) / full * 168.0f;
}

// Builder D FUN_0043fe00 0x440420..0x4407a0: the buff lines, font_A at x 0 (xmm3 = 0), y 618 /
// 630 / 642, colour ff6060 / 6060ff / ffff60 (OR'd into the HUD tint), alpha min(timer, 255)
// scaled by the tint's — they fade over the last 255 ticks. English strings 0x6995d8 / 0x699630 /
// 0x6996d4 (the four other languages are in the exe, [not modelled]).
inline std::string buff_line(int which, float value) {
    char b[48];
    if (which == 0) std::snprintf(b, sizeof b, "str add + %2.1f", double(value));
    else if (which == 1) std::snprintf(b, sizeof b, "def add + %2.1f", double(value));
    else std::snprintf(b, sizeof b, "mp speed x %1.1f", double(value));
    return b;
}
inline std::uint8_t buff_line_alpha(std::int32_t timer) {
    return std::uint8_t(std::uint32_t(timer) > 0xffu ? 0xffu : std::uint32_t(timer));
}

// Builder D 0x440a25..0x440d42: the combo line, font_A at (0, 654), while the HUD is up, no Fix
// holds and the count 0x76c1a8 is non-zero. "Hit %2d -> exp x %1.2f" (0x6999dc) in f19040, or at
// 99 "Hit Max -> exp x %1.2f" (0x6998d8) cycling through the hues; x = count x 0.01 + 1 (0x6bd084).
// Alpha min(trunc(timer x 2), 255) (0x440a7c: addss xmm0, xmm0).
inline std::string combo_line(int count) {
    char b[48];
    const double x = double(float(count) * 0.01f + 1.0f);
    if (count == 99) std::snprintf(b, sizeof b, "Hit Max -> exp x %1.2f", x);
    else std::snprintf(b, sizeof b, "Hit %2d -> exp x %1.2f", count, x);
    return b;
}
inline std::uint8_t combo_line_alpha(std::int32_t timer) {
    const int a = int(float(std::uint32_t(timer)) * 2.0f);
    return std::uint8_t(a > 0xff ? 0xff : a);
}

// FUN_004fbb40 0x4fd05e..0x4fd0a0: the number the white damage popup shows. A damage class
// other than melee (3) shows dmg x (g_flags[idx] x 0.1 + 1), floored (FUN_004217f0 -> floor
// 0x671000) and truncated: idx 0xb7 / 0xb6 / 0xb8 for classes 0 / 4, 1 / 5, 2 / 6 (the bracelet
// levels; jump table 0x4fd328), 0 for the no-class 0xff hitboxes. HP took the unscaled dmg
// (0x4fc805) — only the popup is scaled.
inline int dealt_popup_value(int dmg, int cls, const std::int32_t* flags) {
    if (cls == 3) return dmg;
    int idx = 0;
    switch (cls) {
        case 0: case 4: idx = 0xb7; break;
        case 1: case 5: idx = 0xb6; break;
        case 2: case 6: idx = 0xb8; break;
        default: idx = 0; break;
    }
    const float f = float(flags ? flags[idx] : 0) * 0.1f + 1.0f;
    return int(std::floor(f * float(dmg)));
}

}  // namespace cleria::hudstate
