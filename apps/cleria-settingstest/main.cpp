// cleria-settingstest — the yso_win.ini dialect, the typed settings, cleria.ini and the retail import
// (src/cleria/settings.hpp) against build/shell_re/CONFIG.md §1.3 / §2 / §7.
// Usage: cleria-settingstest [ignored]   (writes only under the temp folder)
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "cleria/settings.hpp"
#include "cleria/enemybar.hpp"

using namespace cleria;
namespace fs = std::filesystem;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[settings] %-78s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}

// A config-tool-written file (the shape of CONFIG.md §1.4, Preset 2, a made-up DeviceIdentifier).
static const char* kRetail =
    "IniVersion=0x100\r\n\r\n[kernel]\r\nWindowPosX=64\r\nWindowPosY=64\r\nEnglishRoonic=0\r\n"
    "ClampInternalFrameRateMin=12\r\nClampInternalFrameRateMax=10000\r\nAccessArchiveViaMemoryMappedFile=1\r\n"
    "HighResoText=1\r\n\r\n[game]\r\nLanguage=1\r\nBloodyEffectLevel=1\r\nSaveFileNo=5\r\nTitleCursorIndex=1\r\n"
    "TitleGameDifficulty=0\r\nGameSD=1\r\nTitleTimeAttackIndex=0\r\nPlayerName=\"Adol Christin\"\r\n"
    "AlternateNameSeparator=\" \"\r\n\r\n[graphics]\r\nShowFPS=0\r\nGammaValue=1.00000000\r\n"
    "DeviceIdentifier=\"<01234567-89AB-CDEF-0123-456789ABCDEF>\"\r\nDevice=0\r\nAdapter=0\r\n"
    "BackBufferWidth=2560\r\nBackBufferHeight=1440\r\nBackBufferFormat32=1\r\nStretchAspect=0\r\nRefreshRate=60\r\n"
    "MultiSampleType=8\r\nTextureFilter=2\r\nAnisotropy=16\r\nWindowed=1\r\nWaitVSync=1\r\nForceSoftwareVP=0\r\n"
    "DisablePixelShader=0\r\nTripleBuffer=0\r\nLowResoTexture=0\r\nCompressedTexture=0\r\nOtherLightWeightMode=0\r\n"
    "Mipmap=1\r\nGammaEnable=1\r\nMaskOfEyes=0\r\nWaterCaustics=0\r\nDisableQPC=0\r\nDisableMovies=0\r\n"
    "WaterEffectLevel=2\r\nShadowEffectLevel=2\r\nGlareEffectLevel=0\r\n\r\n[sound]\r\nBgmVolume=22\r\n"
    "EffectVolume=129\r\nDevice=0\r\nPlayBgm=1\r\nPlayEffect=1\r\nReverb=0\r\nForceSoftwareSoundBuffer=0\r\n\r\n"
    "[input]\r\nPadAnalog=0\r\nDashControl=1\r\nAtkBtnMagic=0\r\nAlwaysDashOK=1\r\nMouseButtonAlwaysOkCancel=1\r\n"
    "ForceFeedback=1\r\nGamePad=1\r\nGamePadID=1\r\nFileIndex=1\r\nAssign{KEY_ACTION}=\".\"\r\n"
    "Assign{KEY_JUMP}=\"/\"\r\nAssign{KEY_SHOT}=\"_\"\r\nAssign{KEY_USE}=\"Space\"\r\nAssign{KEY_MENU}=\"Tab\"\r\n"
    "Assign{KEY_WALK}=\"L-Shift\"\r\nAssign{KEY_SWORD_REVD}=\"[\"\r\nAssign{KEY_SWORD_REVU}=\"]\"\r\n"
    "Assign{KEY_SWORD0}=\"1\"\r\nAssign{KEY_SWORD1}=\"2\"\r\nAssign{KEY_SWORD2}=\"3\"\r\nAssign{KEY_UP}=\"W\"\r\n"
    "Assign{KEY_DOWN}=\"S\"\r\nAssign{KEY_LEFT}=\"A\"\r\nAssign{KEY_RIGHT}=\"D\"\r\nAssign{KEY_UPLEFT}=\"Q\"\r\n"
    "Assign{KEY_UPRIGHT}=\"E\"\r\nAssign{KEY_DOWNLEFT}=\"Z\"\r\nAssign{KEY_DOWNRIGHT}=\"X\"\r\n"
    "Assign{MOUSE_DIR}=\"---\"\r\nAssign{PAD_ACTION}=\"Button1\"\r\nAssign{PAD_JUMP}=\"Button2\"\r\n"
    "Assign{PAD_SHOT}=\"Button3\"\r\nAssign{PAD_USE}=\"Button4\"\r\nAssign{PAD_MENU}=\"Button10\"\r\n"
    "Assign{PAD_WALK}=\"Button9\"\r\nAssign{PAD_SWORD_REVD}=\"Button5\"\r\nAssign{PAD_SWORD_REVU}=\"Button8\"\r\n"
    "Assign{MOUSE_ACTION}=\"L-Button\"\r\nAssign{MOUSE_JUMP}=\"R-Button\"\r\nAssign{MOUSE_SHOT}=\"M-Button\"\r\n"
    "Assign{MOUSE_USE}=\"---\"\r\nAssign{MOUSE_MENU}=\"---\"\r\nAssign{MOUSE_WALK}=\"---\"\r\nDeadZone=0.34999999\r\n"
    "RunWalkThreshold=0.50000000\r\nMouseSensitivity=1.00000000\r\nDoubleClickFrame=12\r\n";

static std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}
static void spit(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary); f << s;
}

int main() {
    // ---- the int dialect (FUN_005d07f0) ----
    check(parse_ini_int("0x100") == 0x100 && parse_ini_int("0X1f") == 0x1f, "0x / 0X -> hex");
    check(parse_ini_int("0b101") == 5 && parse_ini_int("0B11") == 3, "0b / 0B -> binary");
    check(parse_ini_int("012") == 12 && parse_ini_int("-7") == -7 && parse_ini_int("42abc") == 42,
          "else decimal (a leading 0 is not octal; strtol stops at a non-digit)");
    check(parse_ini_int("abc") == 0, "garbage reads as 0 (the key is present: the default is NOT kept)");

    // ---- the document ----
    {
        Ini d;
        d.parse("Root=1\n\n[Sec]\nKey=\"quoted value\"\nf=1.5\n; comment\n[other]\nk=2\n");
        check(d.get_int("", "root") == 1 && d.has("SEC", "KEY") && d.get_int("sec", "nokey") == std::nullopt,
              "case-insensitive names; ints; a missing key = nullopt (the caller keeps its default)");
        check(d.get_string("sec", "key") == std::string("quoted value"), "quotes stripped");
        check(d.get_float("sec", "F") && std::fabs(*d.get_float("sec", "F") - 1.5f) < 1e-6f, "floats (atof)");
        check(!d.has("sec", "k") && d.has("other", "k"), "keys are per section");
        d.set_int("sec", "f", 7);                 // in place, the key's spelling kept
        d.set_int("sec", "New", 3);               // appended to its section
        d.set_string("third", "s", std::string("a\x01" "b"));   // a new section; bytes < 0x20 dropped
        d.set_int("", "R2", 0x100, true);         // a root key before the first section
        const std::string t = d.text();
        check(t == "Root=1\r\nR2=0x100\r\n\r\n[Sec]\r\nKey=\"quoted value\"\r\nf=7\r\n; comment\r\nNew=3\r\n[other]\r\n"
                   "k=2\r\n\r\n[third]\r\ns=\"ab\"\r\n",
              "line-preserving writes, CRLF, comments kept, hex 0x100");
        Ini e; e.parse("v=x\n");
        check(e.get_string("", "v") == std::string(""), "FUN_005d0590 drops the first byte even unquoted");
        e.set_float("", "g", 0.35f);
        check(e.raw("", "g") == std::string("0.34999999"), "floats as %0.8f (0.35f -> 0.34999999)");
    }

    // ---- the typed model: defaults ----
    {
        Settings s;
        check(s.ini_version == 0x100 && s.kernel.high_reso_text == 1 && s.graphics.windowed == 0 &&
              s.graphics.texture_filter == 1 && s.graphics.mipmap == 1 && s.graphics.glare_effect_level == 2 &&
              s.sound.bgm_volume == 512 && s.sound.play_bgm == 1 && s.game.player_name == "Adol Christin" &&
              s.game.bloody_effect_level == 2 && s.game.language == 1,
              "retail game defaults (CONFIG.md §2)");
        check(s.input.mouse_button_always_ok_cancel == 1 && s.input.dash_control == 1 && s.input.pad_analog == 0 &&
              s.input.always_dash_ok == 0 && s.input.game_pad == 1 && std::fabs(s.input.dead_zone - 0.35f) < 1e-6f &&
              std::fabs(s.input.run_walk_threshold - 0.5f) < 1e-6f && s.input.double_click_frame == 12,
              "[input] defaults: AOC 1, DashControl 1, DeadZone 0.35, RunWalk 0.5, DoubleClick 12");
        check(s.input.assign == input::preset1(), "bindings default to Preset 1 (the EN table)");
        check(!s.cleria.debug_hud && !s.cleria.unlock_movie_skip && !s.cleria.linear_stick_magnitude,
              "every CleriaCore QoL key defaults OFF");
        check(s.cleria.native_pad_binds, "the pad binds by button name by default (A/B/X/Y layout)");
        check(!s.cleria.dev_keys, "the developer debug keys default OFF (players never trip F / K / L / B ...)");
        Ini fresh;
        write_retail(fresh, s);
        const std::string t = fresh.text();
        check(t.rfind("IniVersion=0x100\r\n\r\n[kernel]\r\nWindowPosX=64\r\n", 0) == 0, "a fresh file starts IniVersion=0x100, [kernel]");
        check(t.find("Assign{MOUSE_ACTION}=\"---\"") != std::string::npos &&
              t.find("Assign{KEY_UP}=\"Numpad8\"") != std::string::npos && t.find("FileIndex") == std::string::npos &&
              t.find("GameFirstPlay") == std::string::npos && t.find("DeviceIdentifier") == std::string::npos,
              "names spaces-stripped; FileIndex / GameFirstPlay / DeviceIdentifier not invented");
        Settings back;
        read_retail(fresh, back);
        bool same = back.ini_version == 0x100;
        for (int i = 0; i < input::kSlots; ++i) {
            const input::Code c = s.input.assign[std::size_t(i)];
            same &= back.input.assign[std::size_t(i)] == (c >= 0x103 && c <= 0x106 ? 0x103u : c);
        }
        check(same, "a fresh file reads back the same (every \"---\" as 0x103, like retail)");
    }

    // ---- the retail file: values, then an exact round trip with retail-only keys untouched ----
    {
        Ini d;
        d.parse(std::string(kRetail) + "\r\n[mystery]\r\nKeepMe=1\r\n");
        // A retail-only key the model does not know, mid-section.
        d.lines.insert(d.lines.begin() + 3, "SomeRetailOnlyKey=0x2A");
        const std::string before = d.text();
        Settings s;
        read_retail(d, s);
        check(s.sound.bgm_volume == 22 && s.sound.effect_volume == 129 && s.graphics.back_buffer_width == 2560 &&
              s.graphics.windowed == 1 && s.input.always_dash_ok == 1 && s.game.save_file_no == 5,
              "values read: BGM 22, SFX 129, 2560 wide, windowed, AlwaysDashOK 1, SaveFileNo 5");
        bool same = s.input.file_index == 1;
        for (int i = 0; i < input::kSlots; ++i) {
            const bool dashes = input::preset2()[std::size_t(i)] >= 0x103 && input::preset2()[std::size_t(i)] <= 0x106;
            same &= s.input.assign[std::size_t(i)] == (dashes ? 0x103u : input::preset2()[std::size_t(i)]);
        }
        check(same, "\"Button1\" / \"L-Shift\" spelling parses to Preset 2; every \"---\" to the first match 0x103");
        check(s.graphics.device_identifier == std::string("<01234567-89AB-CDEF-0123-456789ABCDEF>"),
              "DeviceIdentifier kept as a string");
        write_retail(d, s);
        check(d.text() == before, "write after read is byte-identical (incl. an unknown key and section)");
    }

    // ---- FileIndex semantics and the IniVersion gate ----
    {
        Ini d;
        d.parse("IniVersion=0x100\n[input]\nAssign{KEY_JUMP}=\"\"\nAssign{KEY_SHOT}=\"NoSuchKey\"\n");
        Settings s; read_retail(d, s);
        check(s.input.assign[input::KEY_JUMP] == input::preset1()[input::KEY_JUMP] &&
              s.input.assign[input::KEY_SHOT] == input::preset1()[input::KEY_SHOT],
              "no FileIndex: an empty or unknown Assign{} keeps the default");
        d.set_int("input", "FileIndex", 0);
        Settings t; read_retail(d, t);
        check(t.input.assign[input::KEY_JUMP] == input::kUnbound && t.input.assign[input::KEY_ACTION] == input::kUnbound &&
              t.input.assign[input::KEY_SHOT] == input::preset1()[input::KEY_SHOT],
              "FileIndex present: empty AND missing = unbound; unknown keeps the default");
        Settings u; u.input.assign[input::KEY_WALK] = input::kUnbound;
        Ini w; write_retail(w, u);
        Settings v; read_retail(w, v);
        check(w.has("input", "FileIndex") && v.input.assign[input::KEY_WALK] == input::kUnbound,
              "an unbound slot writes FileIndex so it survives the reload");
        Ini g;
        g.parse("[game]\nSaveFileNo=3\nLanguage=4\n[input]\nAlwaysDashOK=1\nPadAnalog=1\n[sound]\nBgmVolume=7\nPlayBgm=0\n");
        Settings x; read_retail(g, x);
        check(x.game.save_file_no == 0 && x.input.always_dash_ok == 0 && x.sound.play_bgm == 1,
              "IniVersion missing: the gated [game] / option / PlayBgm keys are skipped");
        check(x.game.language == 4 && x.input.pad_analog == 1 && x.sound.bgm_volume == 7,
              "...but Language, PadAnalog (the mapper copy) and the volumes are read");
    }

    // ---- config.exe parity (CONFIG_PARITY.md): the presets, and the keys the shell's new rows write ----
    {
        Settings n; n.graphics.gamma_value = 1.6f; n.graphics.windowed = 1; n.graphics.back_buffer_width = 1920;
        n.graphics.stretch_aspect = 1; n.cleria.vsync = false;
        graphics_preset(2, n);
        check(n.graphics.texture_filter == 2 && n.graphics.anisotropy == 2 && n.graphics.multi_sample_type == 0 &&
              n.graphics.mipmap == 1 && n.graphics.gamma_value == 1.0f && n.graphics.wait_vsync == 1 && n.cleria.vsync &&
              n.graphics.shadow_effect_level == 2 && n.graphics.water_effect_level == 2 && n.graphics.glare_effect_level == 2 &&
              n.graphics.low_reso_texture == 0 && n.kernel.high_reso_text == 1,
              "preset Normal: trilinear, 2x, no MSAA, mipmaps, gamma 1, VSync, effects 2 (FUN_005d3640)");
        check(n.graphics.windowed == 1 && n.graphics.back_buffer_width == 1920,
              "a preset leaves the window mode and the resolution alone (CleriaCore's deviation)");
        Settings h; h.graphics.disable_movies = h.graphics.mask_of_eyes = h.graphics.water_caustics = 1;
        h.graphics.refresh_rate = 75;
        graphics_preset(3, h);
        check(n.graphics.stretch_aspect == 0 && h.graphics.disable_movies == 0 && h.graphics.mask_of_eyes == 0 &&
              h.graphics.water_caustics == 0 && h.graphics.refresh_rate == 0,
              "a preset clears Stretch widescreen, Disable movies, Mask of Eyes, caustics, the refresh rate (the memset)");
        check(h.graphics.texture_filter == 2 && h.graphics.anisotropy == 4 && h.graphics.multi_sample_type == 2 &&
              h.graphics.compressed_texture == 0 && h.graphics.glare_effect_level == 2,
              "preset High-Definition: trilinear, 4x (min(caps, 4)), MSAA 2, no compressed textures (FUN_00411f50)");
        Settings l; graphics_preset(1, l);
        check(l.graphics.texture_filter == 1 && l.graphics.anisotropy == 1 && l.graphics.shadow_effect_level == 1 &&
              l.graphics.water_effect_level == 1 && l.graphics.glare_effect_level == 1 && l.graphics.low_reso_texture == 0 &&
              l.graphics.back_buffer_format32 == 1 && l.kernel.high_reso_text == 0,
              "preset Light: bilinear, 1x, effects 1, 32-bit, HighResoText 0 (FUN_005d3900)");
        Settings z; graphics_preset(0, z);
        check(z.graphics.texture_filter == 1 && z.graphics.low_reso_texture == 1 && z.graphics.other_light_weight_mode == 1 &&
              z.graphics.mipmap == 0 && z.graphics.shadow_effect_level == 0 && z.graphics.water_effect_level == 0 &&
              z.graphics.glare_effect_level == 0 && z.graphics.back_buffer_format32 == 0 && z.kernel.high_reso_text == 0,
              "preset Lightest: low-res textures, simplification, no mipmaps, effects 0, 16-bit (FUN_004192c0 0x3f5)");
        // the rows added to the shell write these retail keys, in yso_win.ini
        Settings e; e.sound.play_bgm = 0; e.sound.play_effect = 0; e.sound.device = 2; e.game.language = 3;
        e.game.bloody_effect_level = 0; e.input.mouse_button_always_ok_cancel = 0; e.input.dash_control = 0;
        e.input.atk_btn_magic = 1; e.graphics.wait_vsync = 1;
        Ini w; write_retail(w, e);
        Settings r; read_retail(w, r);
        check(r.sound.play_bgm == 0 && r.sound.play_effect == 0 && r.sound.device == 2 && r.game.language == 3 &&
              r.game.bloody_effect_level == 0 && r.input.mouse_button_always_ok_cancel == 0 && r.input.dash_control == 0 &&
              r.input.atk_btn_magic == 1 && r.graphics.wait_vsync == 1,
              "PlayBgm / PlayEffect / Device / Language / Blood / mouse OK-Cancel / DashControl / AtkBtnMagic / WaitVSync round-trip");
        check(mapper_config(e).mouse_ok_cancel == false && mapper_config(Settings{}).mouse_ok_cancel == true,
              "MouseButtonAlwaysOkCancel reaches the mapper (default on)");
        check(Settings{}.sound.play_bgm == 1 && Settings{}.sound.play_effect == 1 && Settings{}.sound.device == 0 &&
              Settings{}.game.language == 1 && Settings{}.game.bloody_effect_level == 2 && Settings{}.input.dash_control == 1 &&
              Settings{}.input.atk_btn_magic == 0, "their defaults are the game's");
    }

    // ---- cleria.ini ----
    {
        Settings s;
        s.cleria.unlock_movie_skip = true;
        s.cleria.enemy_health_bars = 7;                    // written as is, read back clamped to 0..2
        s.cleria.native_pad_binds = true;
        s.cleria.pad_bind[0] = "rightshoulder";
        Ini c; write_cleria(c, s);
        Settings t; read_cleria(c, t);
        check(Settings{}.cleria.enemy_health_bars == 0 && t.cleria.enemy_health_bars == 2 && c.has("cleria.qol", "EnemyHealthBars"),
              "[cleria.qol] EnemyHealthBars: off by default (retail), clamped to 0..2");
        {   // the pool (src/cleria/enemybar.hpp): a hit turns a bar on, kHold + 255 steps later it is off; a kill drops it
            EnemyBars eb; int on = 0, faded = 0, gone = 0;
            auto ev = [&](int, EnemyBars::Event e) { (e == EnemyBars::kFaded ? faded : gone)++; };
            auto step = [&](float hp1, bool two) {
                eb.begin();
                if (eb.see(1, hp1, 10, {}, 1.0f) == EnemyBars::kOn) ++on;
                if (two && eb.see(2, 5, 5, {}, 4.0f) == EnemyBars::kOn) ++on;
                eb.end(ev);
            };
            step(10, true);
            const bool quiet = on == 0 && eb.alpha(0, 1) == 0 && eb.alpha(0, 2) == 255 && eb.alpha(1, 2) == 255;
            step(7, true);
            const bool hit = on == 1 && eb.alpha(0, 1) == 255 && eb.alpha(1, 1) == 0;
            for (int i = 0; i < EnemyBars::kHold; ++i) step(7, true);
            const bool held = eb.alpha(0, 1) == 254;
            for (int i = 0; i < 253; ++i) step(7, true);
            const bool last = eb.alpha(0, 1) == 1 && faded == 0;
            step(7, true);
            const bool off = eb.alpha(0, 1) == 0 && faded == 1;
            step(3, true); step(3, false);                  // hit again; then enemy 2 is gone (no bar: no event)
            const bool again = on == 2 && gone == 0;
            eb.begin(); eb.end(ev);                         // enemy 1 killed with its bar up
            EnemyBarQuad q;
            const bool quad = enemy_bar_quad(5, 10, 512, 384, &q) && q.x0 == 470 && q.x1 == 512 && q.y0 == 350 && q.y1 == 362 &&
                              q.sw == 42 && !enemy_bar_quad(0, 10, 0, 0, &q) && !enemy_bar_quad(1, 0, 0, 0, &q) &&
                              enemy_bar_quad(20, 10, 0, 0, &q) && q.sw == 84;
            check(quiet && hit && held && last && off && again && gone == 1 && quad,
                  "enemy bars: on at a hit, full kHold steps, the 255-step fade, gone at the kill; the quad");
        }
        check(t.cleria.unlock_movie_skip && t.cleria.native_pad_binds && !t.cleria.debug_hud &&
              t.cleria.pad_bind[0] == "rightshoulder" && c.has("cleria.qol", "UnlockMovieSkip") &&
              c.has("cleria.video", "DebugHud") && c.has("cleria.input", "PadBind{PAD_ACTION}"),
              "cleria.ini round trip: [cleria.video] / [cleria.qol] / [cleria.input]");
        Settings dv; dv.cleria.dev_keys = true;
        Ini dc; write_cleria(dc, dv);
        Settings dr; read_cleria(dc, dr);
        check(dr.cleria.dev_keys && dc.get_int("cleria.dev", "DebugKeys") == 1 && !t.cleria.dev_keys,
              "cleria.ini round trip: [cleria.dev] DebugKeys");
        const input::Config mc = mapper_config(t);
        check(mc.assign[input::PAD_ACTION] == input::kPadButton1 + 5 && mc.assign[input::KEY_ACTION] == 0x2c,
              "NativePadBinds: PAD_ACTION = rightshoulder = virtual Button 6; keyboard slots untouched");
        Settings r;
        r.cleria.native_pad_binds = false;
        check(mapper_config(r).assign == r.input.assign, "NativePadBinds off: the retail Assign{} codes");
    }
    {   // [cleria.mods] (src/cleria/mods): the load order and the enabled ids
        Settings s;
        check(s.cleria.use_mods && s.cleria.mod_order.empty() && s.cleria.mod_active.empty(),
              "[cleria.mods] defaults: UseMods on, no mod active");
        s.cleria.use_mods = false;
        s.cleria.mod_order = "a.one,b.two";
        s.cleria.mod_active = "b.two";
        Ini c; write_cleria(c, s);
        Settings t; read_cleria(c, t);
        check(!t.cleria.use_mods && t.cleria.mod_order == "a.one,b.two" && t.cleria.mod_active == "b.two" &&
              c.raw("cleria.mods", "Order") == std::optional<std::string>("\"a.one,b.two\""),
              "[cleria.mods] UseMods / Order / Active round trip (quoted strings)");
        check(t.cleria.mod_associate, "[cleria.mods] AssociateFiles defaults on");
        s.cleria.mod_associate = false;
        Ini c2; write_cleria(c2, s);
        Settings t2; read_cleria(c2, t2);
        check(!t2.cleria.mod_associate && c2.raw("cleria.mods", "AssociateFiles") == std::optional<std::string>("0"),
              "[cleria.mods] AssociateFiles round trip");
    }

    // ---- [cleria.pad] (the Controller page, build/shell_re/PAD.md) ----
    {
        Settings s;
        Ini c; write_cleria(c, s);
        check(c.has("cleria.pad", "Glyphs") && c.has("cleria.pad", "OuterDeadZone") && c.has("cleria.pad", "ResponseCurve") &&
              c.has("cleria.pad", "TriggerThreshold") && c.has("cleria.pad", "SwapConfirmCancel") &&
              c.has("cleria.pad", "VibrationIntensity"), "cleria.ini writes the six [cleria.pad] keys");
        const input::Config mc = mapper_config(s);
        check(mc.outer_dead_zone == 1.0f && mc.response_curve == 0 && !mc.swap_ok_cancel && s.input.force_feedback == 0,
              "[cleria.pad] defaults = retail: no outer deadzone, linear, no OK/Cancel swap; ForceFeedback off");
        s.pad.swap_confirm_cancel = true;
        s.pad.outer_dead_zone = 0.9f;
        Ini c2; write_cleria(c2, s);
        Settings t; read_cleria(c2, t);
        check(mapper_config(t).swap_ok_cancel && std::fabs(mapper_config(t).outer_dead_zone - 0.9f) < 1e-6f,
              "[cleria.pad] round trip reaches the mapper config");
    }

    // ---- [cleria.speedrun] (SPEEDRUN.md): off by default; a round trip; clamped ----
    {
        Settings s;
        check(!s.speedrun.enabled && !s.speedrun.livesplit && s.speedrun.livesplit_port == 16834 &&
              s.speedrun.auto_start && s.speedrun.auto_split && s.speedrun.timing == 0,
              "[cleria.speedrun] defaults: off, LiveSplit off on port 16834, RTA");
        s.speedrun.enabled = true; s.speedrun.corner = 2; s.speedrun.scale = 1.5f; s.speedrun.timing = 1;
        s.speedrun.livesplit = true; s.speedrun.livesplit_host = "192.168.1.5"; s.speedrun.show_igt = false;
        Ini c; write_cleria(c, s);
        Settings t; read_cleria(c, t);
        check(t.speedrun.enabled && t.speedrun.corner == 2 && std::fabs(t.speedrun.scale - 1.5f) < 1e-6f &&
              t.speedrun.timing == 1 && t.speedrun.livesplit && t.speedrun.livesplit_host == "192.168.1.5" &&
              !t.speedrun.show_igt && c.has("cleria.speedrun", "LiveSplitPort"), "[cleria.speedrun] round trip");
        Ini bad; bad.parse("[cleria.speedrun]\r\nCorner=9\r\nScale=7\r\nLiveSplitPort=0\r\n");
        Settings u; read_cleria(bad, u);
        check(u.speedrun.corner == 3 && u.speedrun.scale == 2.0f && u.speedrun.livesplit_port == 1,
              "[cleria.speedrun] out-of-range values are clamped");
    }

    // ---- [cleria.archipelago]: the removed built-in client's section (AP_GAME.md, superseded by the Lua mod).
    // An old cleria.ini that has it still loads: the section is ignored, the rest reads as before. ----
    {
        Ini c;
        c.parse("[cleria.archipelago]\r\nServer=\"archipelago.gg:38281\"\r\nSlot=\"Hugo\"\r\nAutoConnect=1\r\n"
                "[cleria.mods]\r\nUseMods=0\r\n[cleria.qol]\r\nSkipLogoMovie=1\r\n");
        Settings t; read_cleria(c, t);
        check(!t.cleria.use_mods && t.cleria.skip_logo_movie, "an old [cleria.archipelago] section is ignored; the rest loads");
        Ini w; write_cleria(w, t);
        check(!w.has("cleria.archipelago", "Slot") && !w.has("cleria.archipelago", "Server"),
              "write_cleria writes no [cleria.archipelago]");
    }

    // ---- the launcher's backdrop by the newest save's floor (LauncherRoom="auto", a CleriaCore addition) ----
    {
        check(Settings{}.cleria.launcher_room == "auto", "LauncherRoom defaults to auto");
        const auto bd = [](const char* scene) { return std::string(launcher_backdrop(scene)); };
        check(bd("") == "S_0002", "no save: the Roda tree");
        check(bd("s_1000") == "S_1009" && bd("S_1104") == "S_1009", "floor 1 (S_10xx / S_11xx)");
        check(bd("s_2012") == "S_2007", "floor 2");
        check(bd("s_3099") == "S_3005", "floor 3");
        check(bd("s_4013") == "S_4000" && bd("s_4100") == "S_4000", "floor 4");
        check(bd("s_5001") == "S_5005", "floor 5");
        check(bd("s_6014") == "S_6000", "floor 6");
        check(bd("s_7000") == "S_7000", "the summit");
        check(bd("s_0002") == "S_0002" && bd("s_9105") == "S_0002" && bd("title") == "S_0002" && bd("s_1") == "S_0002",
              "the prologue, another scene, a malformed name: the Roda tree");
    }

    // ---- retail's location rules (FUN_005de1d0) ----
    {
        const fs::path sg = "C:/SG", inst = "D:/Game";
        check(retail_ini_path_from("", inst, sg, "bob") == sg / "FALCOM" / "yso_win" / "yso_win.ini",
              "no common.ini: Saved Games\\FALCOM\\yso_win");
        check(retail_ini_path_from("BaseDir=\"\"\r\nUserProfileHere=0\r\n", inst, sg, "bob") ==
                  sg / "FALCOM" / "yso_win" / "yso_win.ini", "UserProfileHere=0: Saved Games");
        check(retail_ini_path_from("BaseDir=\"\"\r\nUserProfileHere=1\r\n", inst, sg, "bob") ==
                  inst / "user" / "bob" / "yso_win.ini", "UserProfileHere=1: <exe dir>\\user\\<name>");
#ifdef _WIN32
        const char* const abs_base = "E:/Data";
#else
        const char* const abs_base = "/data";   // an absolute BaseDir in this OS's terms
#endif
        check(retail_ini_path_from("BaseDir=\"" + std::string(abs_base) + "\"\r\nUserProfileHere=1\r\n", inst, sg, "bob") ==
                  fs::path(abs_base) / "user" / "bob" / "yso_win.ini", "UserProfileHere=1 + BaseDir");
        const fs::path d = default_ini_path();
        check(!d.empty() && d.filename() == "yso_win.ini" && d.parent_path().filename() == "CleriaCore",
              "the default location: <Saved Games>\\CleriaCore\\yso_win.ini");
    }

    // ---- the store: import once, never write retail, then load ----
    {
        std::error_code ec;
        const fs::path root = fs::temp_directory_path() / "cleria-settingstest";
        fs::remove_all(root, ec);
        const fs::path retail = root / "FALCOM" / "yso_win" / "yso_win.ini";
        const fs::path mine = root / "CleriaCore" / "yso_win.ini";
        spit(retail, kRetail);
        const std::string retail_before = slurp(retail);
        SettingsStore st;
        check(st.load(mine, retail) == SettingsStore::Origin::Imported && st.s.sound.bgm_volume == 22 &&
              st.s.input.assign[input::KEY_UP] == 0x11, "absent: imported from the retail file");
        check(fs::exists(mine) && fs::exists(root / "CleriaCore" / "cleria.ini") && slurp(retail) == retail_before,
              "the import was saved (yso_win.ini + cleria.ini); retail's file untouched");
        check(slurp(mine) == retail_before, "the imported copy is byte-identical to retail's");
        check(!st.s.access.cheats_active() && st.s.access.game_speed == 1.0f,
              "imported: the access / cheat keys at their retail defaults (retail has none)");
        st.s.sound.bgm_volume = 300;
        st.s.cleria.debug_hud = true;
        st.s.access.exp_rate = 2.0f;                       // [cleria.cheats] ExpRate (src/cleria/access)
        st.s.access.cutscene_skip = int(access::SkipMode::Double);   // [cleria.access] CutsceneSkip
        const auto rev = st.revision();
        st.touch();
        check(st.dirty() && st.revision() == rev + 1, "touch(): dirty + a revision bump");
        check(st.save() && !st.dirty(), "save()");
        SettingsStore st2;
        check(st2.load(mine, retail) == SettingsStore::Origin::Loaded && st2.s.sound.bgm_volume == 300 &&
              st2.s.cleria.debug_hud, "second run: loaded (no re-import), the edits kept");
        check(st2.s.access.exp_rate == 2.0f && st2.s.access.cutscene_skip == int(access::SkipMode::Double) &&
              st2.cleria_doc().has("cleria.cheats", "ExpRate") && !st2.retail_doc().has("cleria.cheats", "ExpRate"),
              "the cheats / assists persist in cleria.ini, never in yso_win.ini");
        check(slurp(retail) == retail_before, "retail's file still untouched");
        {   // the Video page (src/cleria/video): retail keys in yso_win.ini, the extras in cleria.ini
            st2.s.graphics.gamma_value = 1.4f;
            st2.s.graphics.texture_filter = 0;
            st2.s.graphics.windowed = video::retail_windowed(video::Exclusive);
            st2.s.video.window_mode = video::Exclusive;
            st2.s.video.render_scale = 1.5f;
            st2.s.video.fps_cap = 120;
            st2.touch();
            SettingsStore st4;
            check(st2.save() && st4.load(mine, retail) == SettingsStore::Origin::Loaded &&
                  std::fabs(st4.s.graphics.gamma_value - 1.4f) < 1e-6f && st4.s.graphics.texture_filter == 0 &&
                  st4.s.graphics.windowed == 0 && st4.s.video.window_mode == video::Exclusive &&
                  st4.s.video.render_scale == 1.5f && st4.s.video.fps_cap == 120,
                  "Video: GammaValue / TextureFilter / Windowed and [cleria.video] persist");
            check(st4.retail_doc().has("graphics", "GammaValue") && !st4.retail_doc().has("cleria.video", "RenderScale") &&
                  st4.cleria_doc().has("cleria.video", "RenderScale") && st4.cleria_doc().has("cleria.video", "WindowMode") &&
                  !st4.cleria_doc().has("graphics", "GammaValue"),
                  "Video: retail keys in yso_win.ini only, the extras in cleria.ini only");
        }
        const fs::path other = root / "fresh" / "yso_win.ini";
        SettingsStore st3;
        check(st3.load(other) == SettingsStore::Origin::Defaults && fs::exists(other) &&
              slurp(other).rfind("IniVersion=0x100\r\n", 0) == 0, "no file, no retail: defaults, written with IniVersion");
        SettingsStore m;
        m.use_memory_defaults();
        check(m.memory_only() && !m.save() && m.s.sound.bgm_volume == 512, "memory mode: defaults, never writes");
        fs::remove_all(root, ec);
    }

    std::printf("%s (%d failure%s)\n", fails ? "FAILED" : "all passed", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
