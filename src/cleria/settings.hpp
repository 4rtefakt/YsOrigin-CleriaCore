#pragma once
// CleriaCore's settings: retail's yso_win.ini in retail's own dialect, plus cleria.ini for the
// CleriaCore-only / QoL keys. Spec: build/shell_re/CONFIG.md (§1.3 the dialect, §2 the key table,
// §7 the recommendations). API notes for the shell: build/shell_re/SHELL_CORE.md.
//
//  - Ini (ini.hpp): the line-preserving document (Falcom ini library 0x5ce9d0..0x5d0f70): case-insensitive section
//    and key names, ints with 0x / 0b prefixes, "quoted" strings, %0.8f floats, CRLF. Lines the model
//    does not know (retail-only keys, comments, unknown sections) round-trip untouched.
//  - Settings: a typed struct for every retail key, defaulted to the GAME's missing-key defaults
//    (FUN_0040bf50 locals / .data), plus the CleriaCore extras.
//  - SettingsStore: load / import / save of both files, a revision counter for change notification.
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cleria/access.hpp"
#include "cleria/ini.hpp"
#include "cleria/input_map.hpp"
#include "cleria/pad.hpp"
#include "cleria/video.hpp"

namespace cleria {

// ---- the typed model -----------------------------------------------------------------------------
// Ints keep retail's int range even where retail stores a byte. Every default is the game's own
// missing-key default (CONFIG.md §2) unless marked otherwise.
struct Settings {
    std::int32_t ini_version = 0x100;   // root IniVersion: must be 0x100 or the gated keys are skipped

    struct Kernel {
        std::int32_t window_pos_x = 64, window_pos_y = 64;
        std::int32_t english_roonic = 0;
        std::int32_t clamp_fr_min = 5, clamp_fr_max = 10000;   // ClampInternalFrameRateMin/Max
        std::int32_t access_archive_mmf = 1;                    // AccessArchiveViaMemoryMappedFile
        std::int32_t high_reso_text = 1;                        // HighResoText (0x6d606c init 1) [gated]
    } kernel;
    struct Game {   // [game]: every key but Language is gated on IniVersion (FUN_0040b8e0)
        std::int32_t language = 1;          // 0 JP 1 EN 2 ES 3 FR 4 DE 5 IT
        std::int32_t bloody_effect_level = 2;
        std::int32_t save_file_no = 0, title_cursor_index = 0, title_game_difficulty = 0;
        std::int32_t title_time_attack_index = 0;
        std::int32_t game_first_play = 0;   // written only when != 0 (or already in the file)
        std::int32_t game_sd = 0;
        std::string player_name = "Adol Christin";   // <= 0x32 bytes
        std::string alternate_name_separator = " ";   // <= 10 bytes
    } game;
    struct Graphics {
        std::int32_t show_fps = 0;                   // [gated]
        float gamma_value = 1.0f;                    // 0.5 .. 2.0
        std::optional<std::string> device_identifier;   // round-trip only (written only if read)
        std::int32_t device = -1, adapter = 0;
        std::int32_t back_buffer_width = 800, back_buffer_height = 600;
        std::int32_t back_buffer_format32 = 0, stretch_aspect = 0, refresh_rate = 0;
        std::int32_t multi_sample_type = 0, texture_filter = 1, anisotropy = 1;
        std::int32_t windowed = 0;                   // [gated] (bss default 0 = fullscreen)
        std::int32_t wait_vsync = 0, force_software_vp = 0, disable_pixel_shader = 0;
        std::int32_t triple_buffer = 0, low_reso_texture = 0, compressed_texture = 0;
        std::int32_t other_light_weight_mode = 0, mipmap = 1, gamma_enable = 0;
        std::int32_t mask_of_eyes = 0, water_caustics = 0, disable_qpc = 0, disable_movies = 0;
        std::int32_t water_effect_level = 2, shadow_effect_level = 2, glare_effect_level = 2;
    } graphics;
    struct Sound {
        std::int32_t bgm_volume = 512, effect_volume = 512;   // 0..1024
        std::int32_t device = 0;
        std::int32_t play_bgm = 1, play_effect = 1;           // [gated]
        std::int32_t reverb = 0, force_software_sound_buffer = 0;
    } sound;
    struct Input {
        std::int32_t pad_analog = 0, dash_control = 1, atk_btn_magic = 0, always_dash_ok = 0;
        std::int32_t mouse_button_always_ok_cancel = 1;
        std::int32_t force_feedback = 0, game_pad = 1;
        std::int32_t game_pad_id = 1;       // 1-based; retail clamps < 1 to 1 (FUN_005d5b20)
        // FileIndex: its PRESENCE makes an empty / missing Assign{} unbound (FUN_0040bf50).
        std::optional<std::int32_t> file_index;
        input::Bindings assign = input::preset1();   // the EN/default table (Language 1)
        float dead_zone = 0.35f, run_walk_threshold = 0.5f, mouse_sensitivity = 1.0f;
        std::int32_t double_click_frame = 12;
    } input;
    struct Debug {
        std::optional<std::int32_t> no_initialize_game_ctrl;   // [debug] NoInitializeGameCtrl
    } debug;

    // ---- cleria.ini (CleriaCore-only). QoL defaults OFF = retail behaviour. ----
    struct Cleria {
        bool use_mods = true;                // [cleria.mods] UseMods (0 = unmodded; no mod is active until turned on)
        std::string mod_order, mod_active;   // [cleria.mods] Order / Active: mod ids, comma-separated (src/cleria/mods)
        bool mod_associate = true;           // [cleria.mods] AssociateFiles: .cleriamod opens with CleriaCore (MODFILE.md)
        bool debug_hud = false;             // [cleria.video] DebugHud (start with the F3 overlay up)
        bool dev_keys = false;               // [cleria.dev] DebugKeys: the fixed debug keys (F/R/T/K/L/B, Ctrl+Tab, Ctrl+1-9)
        bool unlock_movie_skip = false;      // [cleria.qol] UnlockMovieSkip (skip a demo never seen)
        bool unlock_characters = false;      // [cleria.qol] UnlockCharacters (Toal selectable before a clear)
        bool skip_logo_movie = false;        // [cleria.qol] SkipLogoMovie (boot straight to the title)
        std::int32_t enemy_health_bars = 0;  // [cleria.qol] EnemyHealthBars: 0 off (retail), 1 after a hit, 2 always
                                             // (CleriaCore's own, not retail: src/cleria/enemybar.hpp)
        bool launcher_music = true;          // [cleria.shell] LauncherMusic (the title theme on CleriaCore's launcher)
        std::int32_t ui_scale = 0;           // [cleria.shell] UiScale: the shell's size in percent (75..200); 0 = automatic
                                             // (130 on a Steam Deck, else 100)
        std::string launcher_room = "auto";  // [cleria.shell] LauncherRoom (the launcher's backdrop: "auto" (or empty) =
                                             // by the newest save's floor, launcher_backdrop; else a room id)
        bool vsync = true;                   // [cleria.video] VSync (CleriaCore's own; retail WaitVSync round-trips)
        bool borderless_fullscreen = false;  // [cleria.video] BorderlessFullscreen
        bool linear_stick_magnitude = false; // [cleria.input] LinearStickMagnitude (analog walk)
        bool native_pad_binds = true;        // [cleria.input] NativePadBinds: PAD_* slots from PadBind{} (default since 2026-10-07)
        std::array<std::string, 8> pad_bind = {   // [cleria.input] PadBind{PAD_ACTION..PAD_SWORD_REVU}
            "a", "b", "x", "y", "start", "lefttrigger", "leftshoulder", "rightshoulder"};   // [H]
    } cleria;
    access::Settings access;   // [cleria.access] / [cleria.cheats] (src/cleria/access.hpp)
    PadSettings pad;                         // cleria.ini [cleria.pad] (src/cleria/pad.hpp, PAD.md)

    // ---- [cleria.speedrun] (build/shell_re/SPEEDRUN.md): a display layer, off by default. [H] ----
    struct Speedrun {
        bool enabled = false;                 // Enabled
        bool show_rta = true, show_igt = true, show_splits = true;   // ShowRTA / ShowIGT / ShowSplits
        std::int32_t corner = 1;              // Corner: 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right
        float scale = 1.0f;                   // Scale (0.5..2)
        bool auto_start = true, auto_split = true;   // AutoStart / AutoSplit
        std::int32_t timing = 0;              // Timing: 0 RTA, 1 IGT (decides PBs and deltas)
        std::int32_t compare = 0;             // Compare: 0 personal best, 1 best segments
        bool livesplit = false;               // LiveSplit (connect to LiveSplit Server)
        std::string livesplit_host = "127.0.0.1";   // LiveSplitHost
        std::int32_t livesplit_port = 16834;  // LiveSplitPort (LiveSplit's default)
    } speedrun;
    video::Settings video;                   // cleria.ini [cleria.video] extras (src/cleria/video.hpp, VIDEO.md)
};

// The keys skipped when IniVersion != 0x100 (FUN_0040d1d0's gate around the early HighResoText /
// Windowed reads and the FUN_0040b8e0 pass). [H] PadAnalog and Reverb are read again ungated by
// FUN_0040bf50 (the mapper / sound-struct copies), so this model reads them ungated.
bool gated_key(std::string_view sec, std::string_view key);

void read_retail(const Ini& ini, Settings& s);    // FUN_0040bf50 + FUN_0040b8e0 semantics
void write_retail(Ini& ini, const Settings& s);   // the full key set, retail spelling and order
void read_cleria(const Ini& ini, Settings& s);
void write_cleria(Ini& ini, const Settings& s);

// The launcher's backdrop for LauncherRoom="auto" (a CleriaCore addition: retail has no launcher): the room
// shown for a save whose scene is `scene` ("s_3012"; empty = no save). The floor is the scene id's first digit,
// the game's own grouping (S_10xx / S_11xx = the first tower floor group, ... S_70xx = the summit); the
// prologue / interlude scenes (S_0xxx), anything else and no save get the Roda tree.
const char* launcher_backdrop(std::string_view scene);

// The config tool's four "Basic Settings" buttons (CONFIG_PARITY.md §5) [EXPORT FUN_005d3640, FUN_005d3900,
// FUN_00411f50, FUN_004192c0 case 0x3f5]: 0 Lightest, 1 Light, 2 Normal, 3 High-Definition. The quality keys
// are set as retail sets them: FUN_005d3640 zeroes the whole 0x88-byte struct first, so Stretch widescreen,
// Disable movies, Disable Mask of Eyes and Disable water caustics go off and the refresh rate to its default.
// CleriaCore's deviation: the window mode (retail: fullscreen), the resolution (retail: the desktop mode,
// 640x480 for the light ones, 1024x768 for High-Definition) and the display are left alone -- a click here
// should not throw the window into another mode. The caps-dependent picks take the best case (a modern GPU
// has them all).
void graphics_preset(int p, Settings& s);

// The mapper's configuration from the settings (the PAD_* slots from cleria.ini when NativePadBinds).
input::Config mapper_config(const Settings& s, bool book_mode = false);

// ---- locations -----------------------------------------------------------------------------------
// %USERPROFILE%\Saved Games\CleriaCore\yso_win.ini (FOLDERID_SavedGames); empty if unresolvable.
// Linux: $XDG_CONFIG_HOME/CleriaCore/yso_win.ini (~/.config); macOS: ~/Library/Application Support/CleriaCore.
// Off Windows retail's own file is looked for in its Proton prefix (compatdata/207350/pfx/.../Saved Games).
std::filesystem::path default_ini_path();
inline std::filesystem::path cleria_ini_path(const std::filesystem::path& ini) {
    return ini.parent_path() / "cleria.ini";
}
// Retail's own yso_win.ini, resolved like FUN_005de1d0 / FUN_005ce950: common.ini in `install_dir`
// (UserProfileHere / BaseDir); UserProfileHere=1 -> <BaseDir or install>\user\<user name>\, else (and
// with no common.ini) FOLDERID_SavedGames\FALCOM\yso_win\. `install_dir` may be empty.
std::filesystem::path retail_ini_path(const std::filesystem::path& install_dir);
// The same rules on given inputs (for tests): `common_ini` the file's text ("" = none).
std::filesystem::path retail_ini_path_from(std::string_view common_ini, const std::filesystem::path& install_dir,
                                           const std::filesystem::path& saved_games, std::string_view user_name);

// ---- the store -----------------------------------------------------------------------------------
class SettingsStore {
  public:
    enum class Origin { Memory, Loaded, Imported, Defaults };
    Settings s;   // edit, then touch() (and save() to persist)

    // Read `ini` (and cleria.ini beside it). If `ini` is absent: import `import_from` once when it
    // exists (its lines are copied verbatim, never written), else the defaults; the result is saved
    // to `ini` so the import never repeats. Retail's file is only ever read.
    Origin load(const std::filesystem::path& ini, const std::filesystem::path& import_from = {});
    void use_memory_defaults();   // no file I/O at all (headless / CLERIA_DRIVE runs)
    bool save();                  // read-modify-write both files; false on a write error / memory mode
    void touch() { ++revision_; dirty_ = true; }   // the change notification
    std::uint64_t revision() const { return revision_; }
    bool dirty() const { return dirty_; }
    bool memory_only() const { return memory_; }
    Origin origin() const { return origin_; }
    const std::filesystem::path& path() const { return path_; }
    const Ini& retail_doc() const { return retail_; }
    const Ini& cleria_doc() const { return cleria_; }
    Ini& cleria_doc_mut() { return cleria_; }   // sections the typed model does not know ([cleria.mod.<id>], LUA_API.md)

  private:
    Ini retail_, cleria_;
    std::filesystem::path path_;
    bool memory_ = true, dirty_ = false;
    Origin origin_ = Origin::Memory;
    std::uint64_t revision_ = 1;
};

}  // namespace cleria
