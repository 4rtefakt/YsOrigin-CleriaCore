// Settings: retail's yso_win.ini dialect, the typed model, cleria.ini and the store. See settings.hpp
// and build/shell_re/CONFIG.md. Reader semantics re-checked in the export: FUN_005d07f0 (int),
// FUN_005d0590 (string), FUN_0040bf50 ([input] / the Assign{} loop), FUN_0040b8e0 (the gated pass).
#include "cleria/settings.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <algorithm>

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
#else
#include <pwd.h>
#include <unistd.h>

#include "cleria/gamedata.hpp"   // kSteamAppId: the Proton prefix of retail's settings
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

}  // namespace

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
    rb("cleria.mods", "AssociateFiles", c.mod_associate);
    rb("cleria.video", "DebugHud", c.debug_hud);
    rb("cleria.dev", "DebugKeys", c.dev_keys);
    rb("cleria.qol", "UnlockMovieSkip", c.unlock_movie_skip);
    rb("cleria.qol", "UnlockCharacters", c.unlock_characters);
    rb("cleria.qol", "SkipLogoMovie", c.skip_logo_movie);
    if (auto x = ini.get_int("cleria.qol", "EnemyHealthBars")) c.enemy_health_bars = *x < 0 ? 0 : (*x > 2 ? 2 : std::int32_t(*x));
    rb("cleria.shell", "LauncherMusic", c.launcher_music);
    if (auto v = ini.get_string("cleria.shell", "LauncherRoom")) c.launcher_room = *v;
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
}

void write_cleria(Ini& ini, const Settings& s) {
    const auto& c = s.cleria;
    ini.set_int("cleria.mods", "UseMods", c.use_mods);
    ini.set_string("cleria.mods", "Order", c.mod_order);
    ini.set_string("cleria.mods", "Active", c.mod_active);
    ini.set_int("cleria.mods", "AssociateFiles", c.mod_associate);
    ini.set_int("cleria.video", "DebugHud", c.debug_hud);
    ini.set_int("cleria.dev", "DebugKeys", c.dev_keys);
    ini.set_int("cleria.qol", "UnlockMovieSkip", c.unlock_movie_skip);
    ini.set_int("cleria.qol", "UnlockCharacters", c.unlock_characters);
    ini.set_int("cleria.qol", "SkipLogoMovie", c.skip_logo_movie);
    ini.set_int("cleria.qol", "EnemyHealthBars", c.enemy_health_bars);
    ini.set_int("cleria.shell", "LauncherMusic", c.launcher_music);
    ini.set_string("cleria.shell", "LauncherRoom", c.launcher_room);
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
}

const char* launcher_backdrop(std::string_view scene) {
    if (scene.size() == 6 && (scene[0] == 's' || scene[0] == 'S') && scene[1] == '_') switch (scene[2]) {
        case '1': return "S_1009";   // the goddess statue
        case '2': return "S_2007";   // the pools
        case '3': return "S_3005";   // the stairs over the lava
        case '4': return "S_4000";   // the sand floor's entrance
        case '5': return "S_5005";   // floor 5: the arch
        case '6': return "S_6000";   // the entrance: the statue, the violet lamps
        case '7': return "S_7000";   // the summit
        default: break;
    }
    return "S_0002";                 // the prologue's Roda tree
}

void graphics_preset(int p, Settings& s) {
    auto& gr = s.graphics;
    gr.texture_filter = p >= 2 ? 2 : 1;                  // trilinear with the mip caps (Normal / HD), else bilinear
    gr.anisotropy = p == 3 ? 4 : p == 2 ? 2 : 1;         // Normal: min(caps, 2); HD: min(caps, 4) [BYTES 0x411f65..0x411f89]
    // FUN_005d3640 zeroes the whole struct first [EXPORT 005d3640.c:19]: every switch not set below goes off.
    gr.stretch_aspect = gr.disable_movies = gr.mask_of_eyes = gr.water_caustics = 0;
    gr.force_software_vp = gr.disable_pixel_shader = 0;
    gr.refresh_rate = 0;
    gr.multi_sample_type = p == 3 ? 2 : 0;               // HD: the first available count >= 2
    gr.mipmap = p == 0 ? 0 : 1;
    gr.gamma_value = 1.0f;
    gr.wait_vsync = 1;
    gr.triple_buffer = 0;
    gr.back_buffer_format32 = p == 0 ? 0 : 1;
    gr.compressed_texture = p == 3 ? 0 : 1;              // per caps; High-Definition clears it
    gr.low_reso_texture = p == 0 ? 1 : 0;
    gr.other_light_weight_mode = p == 0 ? 1 : 0;
    gr.shadow_effect_level = gr.water_effect_level = p == 0 ? 0 : p == 1 ? 1 : 2;
    gr.glare_effect_level = p == 0 ? 0 : p == 1 ? 1 : 2;
    if (p < 2) s.kernel.high_reso_text = 0;              // Light / Lightest clear it; the others leave it
    s.cleria.vsync = true;                               // CleriaCore's own VSync follows WaitVSync
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
#ifdef _WIN32
fs::path saved_games_dir() {
    PWSTR w = nullptr;
    fs::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &w)) && w) out = fs::path(w);
    if (w) CoTaskMemFree(w);
    return out;
}
#else
// Retail runs under Proton here: its Saved Games is in the game's prefix, next to its library's
// steamapps/common (or ~/.steam/steam's when the install is not known). [H] Proton's layout.
fs::path home_dir() {
    if (const char* h = std::getenv("HOME"); h && *h) return fs::path(h);
    if (const passwd* pw = getpwuid(getuid()); pw && pw->pw_dir) return fs::path(pw->pw_dir);
    return {};
}
// The Proton prefix's Saved Games for `install_dir` (<lib>/steamapps/common/<game>).
fs::path proton_saved_games(const fs::path& install_dir) {
    const fs::path tail = fs::path("compatdata") / std::to_string(gamedata::kSteamAppId) / "pfx" / "drive_c" / "users" /
                          "steamuser" / "Saved Games";
    if (!install_dir.empty()) return install_dir.parent_path().parent_path() / tail;
    const fs::path h = home_dir();
    return h.empty() ? fs::path() : h / ".steam" / "steam" / "steamapps" / tail;
}
#endif
std::string user_name() {
#ifdef _WIN32
    char b[257] = {};
    DWORD n = sizeof b;
    if (GetUserNameA(b, &n)) return b;
#else
    return "steamuser";   // GetUserNameA under Proton (retail's user\<name>\ when UserProfileHere=1) [H]
#endif
    return {};
}
}  // namespace

fs::path default_ini_path() {
#ifdef _WIN32
    const fs::path sg = saved_games_dir();
    return sg.empty() ? fs::path() : sg / "CleriaCore" / "yso_win.ini";
#else
    // Linux: $XDG_CONFIG_HOME/CleriaCore (default ~/.config); macOS: ~/Library/Application Support/CleriaCore.
    fs::path base;
#ifdef __APPLE__
    if (const fs::path h = home_dir(); !h.empty()) base = h / "Library" / "Application Support";
#else
    if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x == '/') base = fs::path(x);
    else if (const fs::path h = home_dir(); !h.empty()) base = h / ".config";
#endif
    return base.empty() ? fs::path() : base / "CleriaCore" / "yso_win.ini";
#endif
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
#ifdef _WIN32
    return retail_ini_path_from(common, install_dir, saved_games_dir(), user_name());
#else
    return retail_ini_path_from(common, install_dir, proton_saved_games(install_dir), user_name());
#endif
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
