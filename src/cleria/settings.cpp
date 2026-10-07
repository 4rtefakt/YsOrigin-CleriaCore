// Settings: retail's yso_win.ini dialect, the typed model, cleria.ini and the store. See settings.hpp
// and build/shell_re/CONFIG.md. Reader semantics re-checked in the export: FUN_005d07f0 (int),
// FUN_005d0590 (string), FUN_0040bf50 ([input] / the Assign{} loop), FUN_0040b8e0 (the gated pass).
#include "cleria/settings.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>
#endif

namespace fs = std::filesystem;

namespace cleria {

namespace {

bool ieq(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}
std::string_view trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}
bool header_of(std::string_view line, std::string_view* name) {
    const std::string_view t = trim(line);
    if (t.size() < 2 || t.front() != '[' || t.back() != ']') return false;
    if (name) *name = trim(t.substr(1, t.size() - 2));
    return true;
}
// A "key=value" line (not a header, not a ; / # comment). [H] spaces around the key / value are
// trimmed; retail's own files never have any.
bool kv_of(std::string_view line, std::string_view* key, std::string_view* val) {
    const std::string_view t = trim(line);
    if (t.empty() || t.front() == ';' || t.front() == '#' || t.front() == '[') return false;
    const std::size_t eq = t.find('=');
    if (eq == std::string_view::npos || eq == 0) return false;
    if (key) *key = trim(t.substr(0, eq));
    if (val) *val = trim(t.substr(eq + 1));
    return true;
}

}  // namespace

std::optional<std::int32_t> parse_ini_int(std::string_view v) {
    // FUN_005d07f0: a leading "0x"/"0X" -> base 16, "0b"/"0B" -> base 2 (both skipped), else base 10;
    // then strtol (leading blanks and a sign only in the base-10 case; garbage reads as 0).
    std::string s(v);
    int base = 10;
    std::size_t at = 0;
    if (s.size() >= 2 && s[0] == '0') {
        if (s[1] == 'x' || s[1] == 'X') { base = 16; at = 2; }
        else if (s[1] == 'b' || s[1] == 'B') { base = 2; at = 2; }
    }
    return std::int32_t(std::strtol(s.c_str() + at, nullptr, base));
}

// ---- Ini -----------------------------------------------------------------------------------------
void Ini::parse(std::string_view text) {
    lines.clear();
    std::size_t p = 0;
    while (p < text.size()) {
        std::size_t e = text.find('\n', p);
        if (e == std::string_view::npos) e = text.size();
        std::string_view l = text.substr(p, e - p);
        if (!l.empty() && l.back() == '\r') l.remove_suffix(1);
        lines.emplace_back(l);
        p = e + 1;
    }
}

bool Ini::load(const fs::path& p) {
    lines.clear();
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    parse(ss.str());
    return true;
}

std::string Ini::text() const {
    std::string o;
    for (const std::string& l : lines) { o += l; o += "\r\n"; }
    return o;
}

bool Ini::save(const fs::path& p) const {
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    const std::string t = text();
    f.write(t.data(), std::streamsize(t.size()));
    return bool(f);
}

namespace {
int find_line(const std::vector<std::string>& lines, std::string_view sec, std::string_view key) {
    std::string_view cur;   // the root
    for (int i = 0; i < int(lines.size()); ++i) {
        std::string_view h, k;
        if (header_of(lines[std::size_t(i)], &h)) { cur = h; continue; }
        if (ieq(cur, sec) && kv_of(lines[std::size_t(i)], &k, nullptr) && ieq(k, key)) return i;
    }
    return -1;
}
}  // namespace

bool Ini::has(std::string_view sec, std::string_view key) const { return find_line(lines, sec, key) >= 0; }

std::optional<std::string> Ini::raw(std::string_view sec, std::string_view key) const {
    const int i = find_line(lines, sec, key);
    if (i < 0) return std::nullopt;
    std::string_view v;
    kv_of(lines[std::size_t(i)], nullptr, &v);
    return std::string(v);
}

std::optional<std::int32_t> Ini::get_int(std::string_view sec, std::string_view key) const {
    const auto r = raw(sec, key);
    if (!r) return std::nullopt;   // 0x80ff0008: every caller keeps its default
    return parse_ini_int(*r);
}

std::optional<float> Ini::get_float(std::string_view sec, std::string_view key) const {
    const auto r = raw(sec, key);
    if (!r) return std::nullopt;
    return float(std::atof(r->c_str()));
}

std::optional<std::string> Ini::get_string(std::string_view sec, std::string_view key) const {
    // FUN_005d0590: the value from its 2nd byte (the opening quote is dropped unconditionally), cut
    // at the LAST '"'. [not modelled] the Shift-JIS lead-byte copy (byte-identical for ASCII).
    const auto r = raw(sec, key);
    if (!r) return std::nullopt;
    std::string v = r->empty() ? std::string() : r->substr(1);
    if (const std::size_t q = v.rfind('"'); q != std::string::npos) v.resize(q);
    return v;
}

void Ini::set_raw(std::string_view sec, std::string_view key, std::string_view value) {
    if (const int i = find_line(lines, sec, key); i >= 0) {
        std::string_view k;
        kv_of(lines[std::size_t(i)], &k, nullptr);
        lines[std::size_t(i)] = std::string(k) + "=" + std::string(value);
        return;
    }
    const std::string kv = std::string(key) + "=" + std::string(value);
    // The section's span [body, end).
    int hdr = -1, end = int(lines.size());
    if (sec.empty()) {
        for (int i = 0; i < int(lines.size()); ++i)
            if (header_of(lines[std::size_t(i)], nullptr)) { end = i; break; }
    } else {
        for (int i = 0; i < int(lines.size()); ++i) {
            std::string_view h;
            if (!header_of(lines[std::size_t(i)], &h)) continue;
            if (hdr < 0 && ieq(h, sec)) hdr = i;
            else if (hdr >= 0) { end = i; break; }
        }
        if (hdr < 0) {
            if (!lines.empty() && !trim(lines.back()).empty()) lines.emplace_back();
            lines.push_back("[" + std::string(sec) + "]");
            lines.push_back(kv);
            return;
        }
    }
    // After the section's last non-blank line (blank separator lines stay before the next header).
    int at = hdr + 1;
    for (int i = end - 1; i > hdr; --i)
        if (!trim(lines[std::size_t(i)]).empty()) { at = i + 1; break; }
    lines.insert(lines.begin() + at, kv);
}

void Ini::set_int(std::string_view sec, std::string_view key, std::int32_t v, bool hex) {
    char b[32];
    if (hex) std::snprintf(b, sizeof b, "0x%x", unsigned(v));
    else std::snprintf(b, sizeof b, "%d", int(v));
    set_raw(sec, key, b);
}

void Ini::set_float(std::string_view sec, std::string_view key, float v) {
    char b[64];
    std::snprintf(b, sizeof b, "%0.8f", double(v));
    set_raw(sec, key, b);
}

void Ini::set_string(std::string_view sec, std::string_view key, std::string_view v) {
    std::string q = "\"";
    for (char c : v) if ((unsigned char)c >= 0x20) q.push_back(c);   // bytes < 0x20 are dropped
    q.push_back('"');
    set_raw(sec, key, q);
}

bool Ini::erase(std::string_view sec, std::string_view key) {
    const int i = find_line(lines, sec, key);
    if (i < 0) return false;
    lines.erase(lines.begin() + i);
    return true;
}

// ---- the typed model -----------------------------------------------------------------------------
bool gated_key(std::string_view sec, std::string_view key) {
    if (ieq(sec, "kernel")) return ieq(key, "HighResoText");
    if (ieq(sec, "graphics")) return ieq(key, "Windowed") || ieq(key, "ShowFPS");
    if (ieq(sec, "game")) return !ieq(key, "Language");
    if (ieq(sec, "sound")) return ieq(key, "PlayBgm") || ieq(key, "PlayEffect");
    if (ieq(sec, "input")) return ieq(key, "DashControl") || ieq(key, "AtkBtnMagic") || ieq(key, "AlwaysDashOK");
    return false;
}

namespace {

void rdi(const Ini& ini, bool ok, const char* sec, const char* key, std::int32_t& v) {
    if (!ok && gated_key(sec, key)) return;
    if (auto x = ini.get_int(sec, key)) v = *x;
}
void rdf(const Ini& ini, const char* sec, const char* key, float& v) {
    if (auto x = ini.get_float(sec, key)) v = *x;
}
void rds(const Ini& ini, bool ok, const char* sec, const char* key, std::string& v, std::size_t buf) {
    if (!ok && gated_key(sec, key)) return;
    if (auto x = ini.get_string(sec, key)) v = x->substr(0, buf - 1);
}

}  // namespace

void read_retail(const Ini& ini, Settings& s) {
    if (auto v = ini.get_int("", "IniVersion")) s.ini_version = *v;
    else s.ini_version = 0;
    const bool ok = s.ini_version == 0x100;   // FUN_0040d1d0: `if (DVar2 == 0x100)`

    auto& k = s.kernel;
    rdi(ini, ok, "kernel", "WindowPosX", k.window_pos_x);
    rdi(ini, ok, "kernel", "WindowPosY", k.window_pos_y);
    rdi(ini, ok, "kernel", "EnglishRoonic", k.english_roonic);
    rdi(ini, ok, "kernel", "ClampInternalFrameRateMin", k.clamp_fr_min);
    rdi(ini, ok, "kernel", "ClampInternalFrameRateMax", k.clamp_fr_max);
    rdi(ini, ok, "kernel", "AccessArchiveViaMemoryMappedFile", k.access_archive_mmf);
    rdi(ini, ok, "kernel", "HighResoText", k.high_reso_text);

    auto& g = s.game;
    rdi(ini, ok, "game", "Language", g.language);
    rdi(ini, ok, "game", "BloodyEffectLevel", g.bloody_effect_level);
    rdi(ini, ok, "game", "SaveFileNo", g.save_file_no);
    rdi(ini, ok, "game", "TitleCursorIndex", g.title_cursor_index);
    rdi(ini, ok, "game", "TitleGameDifficulty", g.title_game_difficulty);
    rdi(ini, ok, "game", "TitleTimeAttackIndex", g.title_time_attack_index);
    rdi(ini, ok, "game", "GameFirstPlay", g.game_first_play);
    rdi(ini, ok, "game", "GameSD", g.game_sd);
    rds(ini, ok, "game", "PlayerName", g.player_name, 0x32);
    rds(ini, ok, "game", "AlternateNameSeparator", g.alternate_name_separator, 10);

    auto& x = s.graphics;
    rdi(ini, ok, "graphics", "ShowFPS", x.show_fps);
    rdf(ini, "graphics", "GammaValue", x.gamma_value);
    if (auto v = ini.get_string("graphics", "DeviceIdentifier")) x.device_identifier = *v;
    rdi(ini, ok, "graphics", "Device", x.device);
    rdi(ini, ok, "graphics", "Adapter", x.adapter);
    rdi(ini, ok, "graphics", "BackBufferWidth", x.back_buffer_width);
    rdi(ini, ok, "graphics", "BackBufferHeight", x.back_buffer_height);
    rdi(ini, ok, "graphics", "BackBufferFormat32", x.back_buffer_format32);
    rdi(ini, ok, "graphics", "StretchAspect", x.stretch_aspect);
    rdi(ini, ok, "graphics", "RefreshRate", x.refresh_rate);
    rdi(ini, ok, "graphics", "MultiSampleType", x.multi_sample_type);
    rdi(ini, ok, "graphics", "TextureFilter", x.texture_filter);
    rdi(ini, ok, "graphics", "Anisotropy", x.anisotropy);
    rdi(ini, ok, "graphics", "Windowed", x.windowed);
    rdi(ini, ok, "graphics", "WaitVSync", x.wait_vsync);
    rdi(ini, ok, "graphics", "ForceSoftwareVP", x.force_software_vp);
    rdi(ini, ok, "graphics", "DisablePixelShader", x.disable_pixel_shader);
    rdi(ini, ok, "graphics", "TripleBuffer", x.triple_buffer);
    rdi(ini, ok, "graphics", "LowResoTexture", x.low_reso_texture);
    rdi(ini, ok, "graphics", "CompressedTexture", x.compressed_texture);
    rdi(ini, ok, "graphics", "OtherLightWeightMode", x.other_light_weight_mode);
    rdi(ini, ok, "graphics", "Mipmap", x.mipmap);
    rdi(ini, ok, "graphics", "GammaEnable", x.gamma_enable);
    rdi(ini, ok, "graphics", "MaskOfEyes", x.mask_of_eyes);
    rdi(ini, ok, "graphics", "WaterCaustics", x.water_caustics);
    rdi(ini, ok, "graphics", "DisableQPC", x.disable_qpc);
    rdi(ini, ok, "graphics", "DisableMovies", x.disable_movies);
    rdi(ini, ok, "graphics", "WaterEffectLevel", x.water_effect_level);
    rdi(ini, ok, "graphics", "ShadowEffectLevel", x.shadow_effect_level);
    rdi(ini, ok, "graphics", "GlareEffectLevel", x.glare_effect_level);

    auto& so = s.sound;
    rdi(ini, ok, "sound", "BgmVolume", so.bgm_volume);
    rdi(ini, ok, "sound", "EffectVolume", so.effect_volume);
    rdi(ini, ok, "sound", "Device", so.device);
    rdi(ini, ok, "sound", "PlayBgm", so.play_bgm);
    rdi(ini, ok, "sound", "PlayEffect", so.play_effect);
    rdi(ini, ok, "sound", "Reverb", so.reverb);
    rdi(ini, ok, "sound", "ForceSoftwareSoundBuffer", so.force_software_sound_buffer);

    auto& in = s.input;
    rdi(ini, ok, "input", "PadAnalog", in.pad_analog);
    rdi(ini, ok, "input", "DashControl", in.dash_control);
    rdi(ini, ok, "input", "AtkBtnMagic", in.atk_btn_magic);
    rdi(ini, ok, "input", "AlwaysDashOK", in.always_dash_ok);
    rdi(ini, ok, "input", "MouseButtonAlwaysOkCancel", in.mouse_button_always_ok_cancel);
    rdi(ini, ok, "input", "ForceFeedback", in.force_feedback);
    rdi(ini, ok, "input", "GamePad", in.game_pad);
    rdi(ini, ok, "input", "GamePadID", in.game_pad_id);
    in.file_index = ini.get_int("input", "FileIndex");
    // FUN_0040bf50's Assign{} loop: the default table first; a missing or empty value is UNBOUND when
    // FileIndex is present (else the default stays); an unknown name keeps the default.
    for (int i = 0; i < input::kSlots; ++i) {
        const std::string key = std::string("Assign{") + input::slot_name(i) + "}";
        const std::optional<std::string> v = ini.get_string("input", key);
        const std::string name = v ? v->substr(0, 0x1f) : std::string();
        if (name.empty()) {
            if (in.file_index) in.assign[std::size_t(i)] = input::kUnbound;
        } else if (auto c = input::code_from_name(name)) {
            in.assign[std::size_t(i)] = *c;
        }
    }
    rdf(ini, "input", "DeadZone", in.dead_zone);
    rdf(ini, "input", "RunWalkThreshold", in.run_walk_threshold);
    rdf(ini, "input", "MouseSensitivity", in.mouse_sensitivity);
    rdi(ini, ok, "input", "DoubleClickFrame", in.double_click_frame);

    if (auto v = ini.get_int("debug", "NoInitializeGameCtrl")) s.debug.no_initialize_game_ctrl = *v;
}

void write_retail(Ini& ini, const Settings& s) {
    // The key order of a config-tool-written file (CONFIG.md §1.4), so a fresh file reads like retail's.
    ini.set_int("", "IniVersion", 0x100, /*hex*/true);

    const auto& k = s.kernel;
    ini.set_int("kernel", "WindowPosX", k.window_pos_x);
    ini.set_int("kernel", "WindowPosY", k.window_pos_y);
    ini.set_int("kernel", "EnglishRoonic", k.english_roonic);
    ini.set_int("kernel", "ClampInternalFrameRateMin", k.clamp_fr_min);
    ini.set_int("kernel", "ClampInternalFrameRateMax", k.clamp_fr_max);
    ini.set_int("kernel", "AccessArchiveViaMemoryMappedFile", k.access_archive_mmf);
    ini.set_int("kernel", "HighResoText", k.high_reso_text);

    const auto& g = s.game;
    ini.set_int("game", "Language", g.language);
    ini.set_int("game", "BloodyEffectLevel", g.bloody_effect_level);
    ini.set_int("game", "SaveFileNo", g.save_file_no);
    ini.set_int("game", "TitleCursorIndex", g.title_cursor_index);
    ini.set_int("game", "TitleGameDifficulty", g.title_game_difficulty);
    ini.set_int("game", "GameSD", g.game_sd);
    ini.set_int("game", "TitleTimeAttackIndex", g.title_time_attack_index);
    ini.set_string("game", "PlayerName", g.player_name);
    ini.set_string("game", "AlternateNameSeparator", g.alternate_name_separator);
    // FUN_0040d7e0 writes GameFirstPlay only once it is set (the ending's FUN_0058f210).
    if (g.game_first_play != 0 || ini.has("game", "GameFirstPlay"))
        ini.set_int("game", "GameFirstPlay", g.game_first_play);

    const auto& x = s.graphics;
    ini.set_int("graphics", "ShowFPS", x.show_fps);
    ini.set_float("graphics", "GammaValue", x.gamma_value);
    if (x.device_identifier) ini.set_string("graphics", "DeviceIdentifier", *x.device_identifier);
    ini.set_int("graphics", "Device", x.device);
    ini.set_int("graphics", "Adapter", x.adapter);
    ini.set_int("graphics", "BackBufferWidth", x.back_buffer_width);
    ini.set_int("graphics", "BackBufferHeight", x.back_buffer_height);
    ini.set_int("graphics", "BackBufferFormat32", x.back_buffer_format32);
    ini.set_int("graphics", "StretchAspect", x.stretch_aspect);
    ini.set_int("graphics", "RefreshRate", x.refresh_rate);
    ini.set_int("graphics", "MultiSampleType", x.multi_sample_type);
    ini.set_int("graphics", "TextureFilter", x.texture_filter);
    ini.set_int("graphics", "Anisotropy", x.anisotropy);
    ini.set_int("graphics", "Windowed", x.windowed);
    ini.set_int("graphics", "WaitVSync", x.wait_vsync);
    ini.set_int("graphics", "ForceSoftwareVP", x.force_software_vp);
    ini.set_int("graphics", "DisablePixelShader", x.disable_pixel_shader);
    ini.set_int("graphics", "TripleBuffer", x.triple_buffer);
    ini.set_int("graphics", "LowResoTexture", x.low_reso_texture);
    ini.set_int("graphics", "CompressedTexture", x.compressed_texture);
    ini.set_int("graphics", "OtherLightWeightMode", x.other_light_weight_mode);
    ini.set_int("graphics", "Mipmap", x.mipmap);
    ini.set_int("graphics", "GammaEnable", x.gamma_enable);
    ini.set_int("graphics", "MaskOfEyes", x.mask_of_eyes);
    ini.set_int("graphics", "WaterCaustics", x.water_caustics);
    ini.set_int("graphics", "DisableQPC", x.disable_qpc);
    ini.set_int("graphics", "DisableMovies", x.disable_movies);
    ini.set_int("graphics", "WaterEffectLevel", x.water_effect_level);
    ini.set_int("graphics", "ShadowEffectLevel", x.shadow_effect_level);
    ini.set_int("graphics", "GlareEffectLevel", x.glare_effect_level);

    const auto& so = s.sound;
    ini.set_int("sound", "BgmVolume", so.bgm_volume);
    ini.set_int("sound", "EffectVolume", so.effect_volume);
    ini.set_int("sound", "Device", so.device);
    ini.set_int("sound", "PlayBgm", so.play_bgm);
    ini.set_int("sound", "PlayEffect", so.play_effect);
    ini.set_int("sound", "Reverb", so.reverb);
    ini.set_int("sound", "ForceSoftwareSoundBuffer", so.force_software_sound_buffer);

    const auto& in = s.input;
    ini.set_int("input", "PadAnalog", in.pad_analog);
    ini.set_int("input", "DashControl", in.dash_control);
    ini.set_int("input", "AtkBtnMagic", in.atk_btn_magic);
    ini.set_int("input", "AlwaysDashOK", in.always_dash_ok);
    ini.set_int("input", "MouseButtonAlwaysOkCancel", in.mouse_button_always_ok_cancel);
    ini.set_int("input", "ForceFeedback", in.force_feedback);
    ini.set_int("input", "GamePad", in.game_pad);
    ini.set_int("input", "GamePadID", in.game_pad_id);
    // An unbound slot only survives a reload with FileIndex present (an empty Assign{} otherwise
    // keeps the default): write it whenever a slot is unbound. [H] 0 = the config tool's "Preset 1".
    bool any_unbound = false;
    for (input::Code c : in.assign) any_unbound |= c == input::kUnbound;
    if (in.file_index || any_unbound) ini.set_int("input", "FileIndex", in.file_index.value_or(0));
    for (int i = 0; i < input::kSlots; ++i)   // FUN_004133e0: the table name, spaces stripped
        ini.set_string("input", std::string("Assign{") + input::slot_name(i) + "}",
                       input::code_ini_name(in.assign[std::size_t(i)]));
    ini.set_float("input", "DeadZone", in.dead_zone);
    ini.set_float("input", "RunWalkThreshold", in.run_walk_threshold);
    ini.set_float("input", "MouseSensitivity", in.mouse_sensitivity);
    ini.set_int("input", "DoubleClickFrame", in.double_click_frame);

    if (s.debug.no_initialize_game_ctrl) ini.set_int("debug", "NoInitializeGameCtrl", *s.debug.no_initialize_game_ctrl);
}

namespace {
const char* const kPadSlotNames[8] = {"PAD_ACTION", "PAD_JUMP", "PAD_SHOT", "PAD_USE",
                                      "PAD_MENU", "PAD_WALK", "PAD_SWORD_REVD", "PAD_SWORD_REVU"};
}

void read_cleria(const Ini& ini, Settings& s) {
    auto& c = s.cleria;
    auto rb = [&](const char* sec, const char* key, bool& v) { if (auto x = ini.get_int(sec, key)) v = *x != 0; };
    rb("cleria.mods", "UseMods", c.use_mods);
    if (auto v = ini.get_string("cleria.mods", "Order")) c.mod_order = *v;
    if (auto v = ini.get_string("cleria.mods", "Active")) c.mod_active = *v;
    rb("cleria.video", "DebugHud", c.debug_hud);
    rb("cleria.qol", "UnlockMovieSkip", c.unlock_movie_skip);
    rb("cleria.qol", "SkipLogoMovie", c.skip_logo_movie);
    rb("cleria.video", "VSync", c.vsync);
    rb("cleria.video", "BorderlessFullscreen", c.borderless_fullscreen);
    rb("cleria.input", "LinearStickMagnitude", c.linear_stick_magnitude);
    rb("cleria.input", "NativePadBinds", c.native_pad_binds);
    for (int i = 0; i < 8; ++i)
        if (auto v = ini.get_string("cleria.input", std::string("PadBind{") + kPadSlotNames[i] + "}"))
            c.pad_bind[std::size_t(i)] = *v;
    access::read(ini, s.access);
    read_pad(ini, s.pad);
    // [cleria.speedrun] (SPEEDRUN.md)
    auto& r = s.speedrun;
    const char* sr = "cleria.speedrun";
    rb(sr, "Enabled", r.enabled);
    rb(sr, "ShowRTA", r.show_rta);
    rb(sr, "ShowIGT", r.show_igt);
    rb(sr, "ShowSplits", r.show_splits);
    if (auto x = ini.get_int(sr, "Corner")) r.corner = std::clamp(*x, 0, 3);
    if (auto x = ini.get_float(sr, "Scale")) r.scale = std::clamp(*x, 0.5f, 2.0f);
    rb(sr, "AutoStart", r.auto_start);
    rb(sr, "AutoSplit", r.auto_split);
    if (auto x = ini.get_int(sr, "Timing")) r.timing = std::clamp(*x, 0, 1);
    if (auto x = ini.get_int(sr, "Compare")) r.compare = std::clamp(*x, 0, 1);
    rb(sr, "LiveSplit", r.livesplit);
    if (auto x = ini.get_string(sr, "LiveSplitHost")) r.livesplit_host = *x;
    if (auto x = ini.get_int(sr, "LiveSplitPort")) r.livesplit_port = std::clamp(*x, 1, 65535);
    video::read(ini, s.video);
    apcfg::read(ini, s.archipelago);
}

void write_cleria(Ini& ini, const Settings& s) {
    const auto& c = s.cleria;
    ini.set_int("cleria.mods", "UseMods", c.use_mods);
    ini.set_string("cleria.mods", "Order", c.mod_order);
    ini.set_string("cleria.mods", "Active", c.mod_active);
    ini.set_int("cleria.video", "DebugHud", c.debug_hud);
    ini.set_int("cleria.qol", "UnlockMovieSkip", c.unlock_movie_skip);
    ini.set_int("cleria.qol", "SkipLogoMovie", c.skip_logo_movie);
    ini.set_int("cleria.video", "VSync", c.vsync);
    ini.set_int("cleria.video", "BorderlessFullscreen", c.borderless_fullscreen);
    ini.set_int("cleria.input", "LinearStickMagnitude", c.linear_stick_magnitude);
    ini.set_int("cleria.input", "NativePadBinds", c.native_pad_binds);
    for (int i = 0; i < 8; ++i)
        ini.set_string("cleria.input", std::string("PadBind{") + kPadSlotNames[i] + "}", c.pad_bind[std::size_t(i)]);
    access::write(ini, s.access);
    write_pad(ini, s.pad);
    const auto& r = s.speedrun;
    const char* sr = "cleria.speedrun";
    ini.set_int(sr, "Enabled", r.enabled);
    ini.set_int(sr, "ShowRTA", r.show_rta);
    ini.set_int(sr, "ShowIGT", r.show_igt);
    ini.set_int(sr, "ShowSplits", r.show_splits);
    ini.set_int(sr, "Corner", r.corner);
    ini.set_float(sr, "Scale", r.scale);
    ini.set_int(sr, "AutoStart", r.auto_start);
    ini.set_int(sr, "AutoSplit", r.auto_split);
    ini.set_int(sr, "Timing", r.timing);
    ini.set_int(sr, "Compare", r.compare);
    ini.set_int(sr, "LiveSplit", r.livesplit);
    ini.set_string(sr, "LiveSplitHost", r.livesplit_host);
    ini.set_int(sr, "LiveSplitPort", r.livesplit_port);
    video::write(ini, s.video);
    apcfg::write(ini, s.archipelago);
}

input::Config mapper_config(const Settings& s, bool book_mode) {
    input::Config c;
    c.assign = s.input.assign;
    if (s.cleria.native_pad_binds)
        for (int i = 0; i < 8; ++i) {
            const auto code = input::pad_code_from_sdl_name(s.cleria.pad_bind[std::size_t(i)]);
            c.assign[std::size_t(input::PAD_ACTION + i)] = code ? *code : input::kUnbound;
        }
    c.mouse_ok_cancel = s.input.mouse_button_always_ok_cancel != 0;
    c.pad_analog = s.input.pad_analog != 0;
    c.game_pad = s.input.game_pad != 0;
    c.dead_zone = s.input.dead_zone;
    c.double_click_frame = std::uint32_t(s.input.double_click_frame);
    c.book_mode = book_mode;
    c.linear_magnitude = s.cleria.linear_stick_magnitude;
    c.outer_dead_zone = s.pad.outer_dead_zone;           // [cleria.pad] (analog walk only)
    c.response_curve = s.pad.response_curve;
    c.swap_ok_cancel = s.pad.swap_confirm_cancel;
    return c;
}

// ---- locations -----------------------------------------------------------------------------------
namespace {
fs::path saved_games_dir() {
#ifdef _WIN32
    PWSTR w = nullptr;
    fs::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &w)) && w) out = fs::path(w);
    if (w) CoTaskMemFree(w);
    return out;
#else
    const char* h = std::getenv("HOME");
    return h ? fs::path(h) / "Saved Games" : fs::path();
#endif
}
std::string user_name() {
#ifdef _WIN32
    char b[257] = {};
    DWORD n = sizeof b;
    if (GetUserNameA(b, &n)) return b;
#endif
    return {};
}
}  // namespace

fs::path default_ini_path() {
    const fs::path sg = saved_games_dir();
    return sg.empty() ? fs::path() : sg / "CleriaCore" / "yso_win.ini";
}

fs::path retail_ini_path_from(std::string_view common_ini, const fs::path& install_dir, const fs::path& saved_games,
                              std::string_view user) {
    Ini c;
    c.parse(common_ini);
    // UserProfileHere: 0 = the profile folder, 1 = the exe folder; missing = 0 [EXPORT FUN_0040d1d0].
    const bool here = c.get_int("", "UserProfileHere").value_or(0) != 0;
    if (!here) return saved_games.empty() ? fs::path() : saved_games / "FALCOM" / "yso_win" / "yso_win.ini";
    // user\<GetUserNameA>\ relative to BaseDir (the chdir target) or the exe folder. [H] a relative
    // BaseDir is taken against the install folder.
    fs::path base = install_dir;
    if (auto b = c.get_string("", "BaseDir"); b && !b->empty()) {
        const fs::path bp(*b);
        base = bp.is_absolute() ? bp : install_dir / bp;
    }
    return base / "user" / std::string(user) / "yso_win.ini";
}

fs::path retail_ini_path(const fs::path& install_dir) {
    std::string common;
    if (!install_dir.empty()) {
        std::ifstream f(install_dir / "common.ini", std::ios::binary);
        if (f) { std::stringstream ss; ss << f.rdbuf(); common = ss.str(); }
    }
    return retail_ini_path_from(common, install_dir, saved_games_dir(), user_name());
}

// ---- the store -----------------------------------------------------------------------------------
SettingsStore::Origin SettingsStore::load(const fs::path& ini, const fs::path& import_from) {
    s = Settings{};
    retail_.lines.clear();
    cleria_.lines.clear();
    path_ = ini;
    memory_ = false;
    std::error_code ec;
    if (retail_.load(ini)) {
        origin_ = Origin::Loaded;
    } else if (!import_from.empty() && fs::exists(import_from, ec) && retail_.load(import_from)) {
        origin_ = Origin::Imported;   // the retail file's lines verbatim; it is never written
    } else {
        retail_.lines.clear();
        origin_ = Origin::Defaults;
    }
    read_retail(retail_, s);
    if (origin_ == Origin::Defaults) s.ini_version = 0x100;
    cleria_.load(cleria_ini_path(ini));
    read_cleria(cleria_, s);
    ++revision_;
    dirty_ = false;
    if (origin_ != Origin::Loaded) save();   // the import happens once; a fresh install gets a file
    return origin_;
}

void SettingsStore::use_memory_defaults() {
    s = Settings{};
    retail_.lines.clear();
    cleria_.lines.clear();
    path_.clear();
    memory_ = true;
    origin_ = Origin::Memory;
    ++revision_;
    dirty_ = false;
}

bool SettingsStore::save() {
    if (memory_ || path_.empty()) return false;
    write_retail(retail_, s);
    write_cleria(cleria_, s);
    const bool ok = retail_.save(path_) && cleria_.save(cleria_ini_path(path_));
    if (ok) dirty_ = false;
    return ok;
}

}  // namespace cleria
