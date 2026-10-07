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

    // ---- cleria.ini ----
    {
        Settings s;
        s.cleria.unlock_movie_skip = true;
        s.cleria.native_pad_binds = true;
        s.cleria.pad_bind[0] = "rightshoulder";
        Ini c; write_cleria(c, s);
        Settings t; read_cleria(c, t);
        check(t.cleria.unlock_movie_skip && t.cleria.native_pad_binds && !t.cleria.debug_hud &&
              t.cleria.pad_bind[0] == "rightshoulder" && c.has("cleria.qol", "UnlockMovieSkip") &&
              c.has("cleria.video", "DebugHud") && c.has("cleria.input", "PadBind{PAD_ACTION}"),
              "cleria.ini round trip: [cleria.video] / [cleria.qol] / [cleria.input]");
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

    // ---- [cleria.archipelago] (AP_GAME.md): no slot, no auto-connect, DeathLink off by default; a round trip ----
    {
        Settings s;
        check(s.archipelago.slot.empty() && !s.archipelago.auto_connect && !s.archipelago.death_link &&
              s.archipelago.uuid.empty() && s.archipelago.show_feed && !s.archipelago.show_tracker,
              "[cleria.archipelago] defaults: nothing set, nothing on");
        s.archipelago.server = "wss://archipelago.gg:51234"; s.archipelago.slot = "Hugo \"the\" Doll";
        s.archipelago.password = "p@ss word"; s.archipelago.uuid = "u-1"; s.archipelago.death_link = true;
        s.archipelago.auto_connect = true; s.archipelago.show_feed = false; s.archipelago.show_tracker = true;
        s.archipelago.export_state = false;
        Ini c; write_cleria(c, s);
        Settings t; read_cleria(c, t);
        check(t.archipelago.server == s.archipelago.server && t.archipelago.slot == s.archipelago.slot &&
              t.archipelago.password == "p@ss word" && t.archipelago.uuid == "u-1" && t.archipelago.death_link &&
              t.archipelago.auto_connect && !t.archipelago.show_feed && t.archipelago.show_tracker &&
              !t.archipelago.export_state, "[cleria.archipelago] round trip");
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
        check(retail_ini_path_from("BaseDir=\"E:/Data\"\r\nUserProfileHere=1\r\n", inst, sg, "bob") ==
                  fs::path("E:/Data") / "user" / "bob" / "yso_win.ini", "UserProfileHere=1 + BaseDir");
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
