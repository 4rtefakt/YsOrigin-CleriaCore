// CleriaCore's accessibility assists and cheats: the settings, the multipliers, the cutscene-skip
// gate, the flash limiter. See access.hpp and build/shell_re/ACCESS.md.
#include "cleria/access.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

#include "cleria/settings.hpp"

namespace cleria::access {

namespace {

constexpr const char* kAccess = "cleria.access";
constexpr const char* kCheats = "cleria.cheats";

// The keys, their section and their field: one table drives read / write / set_by_key.
struct Key {
    const char* sec;
    const char* name;
    bool Settings::* b;
    float Settings::* f;
    int Settings::* i;
    float lo, hi;
};
const Key kKeys[] = {
    {kAccess, "CutsceneSkip", nullptr, nullptr, &Settings::cutscene_skip, 0, 4},
    {kAccess, "TextSpeed", nullptr, nullptr, &Settings::text_speed, 0, 2},
    {kAccess, "ReduceFlashes", &Settings::reduce_flashes, nullptr, nullptr, 0, 1},
    {kAccess, "ScreenShake", nullptr, &Settings::shake, nullptr, 0.0f, 1.0f},
    {kAccess, "GameSpeed", nullptr, &Settings::game_speed, nullptr, kSpeedMin, kSpeedMax},
    {kCheats, "InfiniteHP", &Settings::inf_hp, nullptr, nullptr, 0, 1},
    {kCheats, "InfiniteMP", &Settings::inf_mp, nullptr, nullptr, 0, 1},
    {kCheats, "InfiniteBoost", &Settings::inf_boost, nullptr, nullptr, 0, 1},
    {kCheats, "InfiniteOxygen", &Settings::inf_o2, nullptr, nullptr, 0, 1},
    {kCheats, "OneHitKill", &Settings::one_hit_kill, nullptr, nullptr, 0, 1},
    {kCheats, "ExpRate", nullptr, &Settings::exp_rate, nullptr, kRateMin, kRateMax},
    {kCheats, "SpRate", nullptr, &Settings::sp_rate, nullptr, kRateMin, kRateMax},
    {kCheats, "DropRate", nullptr, &Settings::drop_rate, nullptr, kRateMin, kRateMax},
    {kCheats, "DamageDealt", nullptr, &Settings::damage_dealt, nullptr, kDealtMin, kDealtMax},
    {kCheats, "DamageTaken", nullptr, &Settings::damage_taken, nullptr, kTakenMin, kTakenMax},
};

bool ieq(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

void assign(Settings& s, const Key& k, float v) {
    if (!std::isfinite(v)) return;
    v = std::clamp(v, k.lo, k.hi);
    if (k.b) s.*k.b = v != 0.0f;
    else if (k.f) s.*k.f = v;
    else s.*k.i = int(std::lround(v));
}

}  // namespace

void Settings::reset_cheats() {
    const Settings d;
    inf_hp = d.inf_hp; inf_mp = d.inf_mp; inf_boost = d.inf_boost; inf_o2 = d.inf_o2;
    one_hit_kill = d.one_hit_kill;
    exp_rate = d.exp_rate; sp_rate = d.sp_rate; drop_rate = d.drop_rate;
    damage_dealt = d.damage_dealt; damage_taken = d.damage_taken;
}

bool Settings::cheats_active() const {
    return inf_hp || inf_mp || inf_boost || inf_o2 || one_hit_kill || exp_rate != 1.0f || sp_rate != 1.0f ||
           drop_rate != 1.0f || damage_dealt != 1.0f || damage_taken != 1.0f;
}

void read(const Ini& ini, Settings& s) {
    for (const Key& k : kKeys)
        if (auto v = ini.get_float(k.sec, k.name)) assign(s, k, *v);
}

void write(Ini& ini, const Settings& s) {
    for (const Key& k : kKeys) {
        if (k.b) ini.set_int(k.sec, k.name, s.*k.b ? 1 : 0);
        else if (k.f) ini.set_float(k.sec, k.name, s.*k.f);
        else ini.set_int(k.sec, k.name, s.*k.i);
    }
}

bool set_by_key(Settings& s, std::string_view key, std::string_view value) {
    for (const Key& k : kKeys)
        if (ieq(key, k.name)) {
            assign(s, k, float(std::atof(std::string(value).c_str())));
            return true;
        }
    return false;
}

int damage_dealt(int dmg, float mult) {
    if (dmg <= 0 || mult == 1.0f) return dmg;
    return std::max(1, int(std::lround(float(dmg) * mult)));
}

int damage_taken(int dmg, float mult) {
    if (dmg <= 0 || mult == 1.0f) return dmg;
    if (mult <= 0.0f) return 0;
    return std::max(1, int(std::lround(float(dmg) * mult)));
}

bool SkipGate::step(SkipMode m, bool pressed, bool held) {
    switch (m) {
        case SkipMode::Single:
            return pressed;
        case SkipMode::Off:
            reset();
            return false;
        case SkipMode::Always:
            reset();
            return true;
        case SkipMode::Double:
            if (pressed && window > 0) { reset(); return true; }
            if (pressed) window = kDoubleWindow;
            else if (window > 0) --window;
            return false;
        case SkipMode::Hold:
            // Any hold counts, also one carried in from before the cutscene or from the scene just
            // skipped: holding the button skips scene after scene, one second each (PLAYTEST_1_STORY.md
            // §3; the edge rule made a held button skip only the first scene of a chain).
            (void)pressed;
            if (held) {
                holding = true;
                if (++hold >= kHoldTicks) { reset(); return true; }
                window = kHintTicks;               // the hint stays up a while after a release
                return false;
            }
            holding = false; hold = 0;
            if (window > 0) --window;
            return false;
    }
    return false;
}

SkipGate::Prompt SkipGate::prompt(SkipMode m) const {
    if (m == SkipMode::Double && window > 0) return Prompt::PressAgain;
    if (m == SkipMode::Hold && (holding || window > 0)) return Prompt::Hold;
    return Prompt::None;
}

float luminance(std::uint32_t rgb) {
    const float r = float((rgb >> 16) & 0xff) / 255.0f, g = float((rgb >> 8) & 0xff) / 255.0f,
                b = float(rgb & 0xff) / 255.0f;
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

void FlashLimiter::step(bool enabled, std::uint32_t rgb, float alpha, std::uint32_t* out_rgb, float* out_alpha) {
    const float lum = luminance(rgb);
    if (!enabled || lum < kBrightLum) {
        shown = alpha;
        *out_rgb = rgb;
        *out_alpha = alpha;
        return;
    }
    const float k = std::min(1.0f, kFlashLum / lum);
    auto ch = [&](int sh) { return std::uint32_t(std::lround(float((rgb >> sh) & 0xff) * k)) << sh; };
    *out_rgb = ch(16) | ch(8) | ch(0);
    shown = std::min(alpha, shown + kFlashRise);    // a rise is limited, a fall follows at once
    *out_alpha = shown;
}

}  // namespace cleria::access
