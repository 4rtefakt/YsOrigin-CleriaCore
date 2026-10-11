// The part of the mod that touches the game. Everything here runs on the game's
// main thread (from the EndScene hook or from the scene-loader hook).
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "tests.h"

void plog(const char* fmt, ...);             // retail_probe.log next to the DLL
const char* module_dir();                    // folder of this DLL, with a trailing backslash

namespace probe {

struct Status {
    bool supported = false;      // the exe is the build the addresses were read from
    bool faulted = false;        // an access violation was caught: game writes are off
    bool in_room = false;
    char room[32] = "";          // "S_1004" ("" outside a room)
    int scene_id = 0, room_number = 0;
    float pos[3] = {0, 0, 0}, heading = 0;
    int character = -1, difficulty = -1, level = 0, sp = 0;
    float hp = 0, max_hp = 0, mp = 0, max_mp = 0, boost = 0;
    int weapon_tier = 0;
    bool busy = false;           // a staged room load has not finished
    bool dirty = false;          // the mod has written game state this session
    int saves_blocked = 0;
    unsigned char cfg_shadows = 0, cfg_water = 0, cfg_glare = 0, cfg_other = 0, cfg_caustics = 0;
    unsigned char boot_shadows = 0, boot_water = 0, boot_glare = 0;
};

void install();                               // version check + the two game hooks
void frame();                                 // once per EndScene
const Status& status();
// nullptr when a room change may be staged now, else the reason in plain words.
const char* why_not();

// Why this test cannot be staged from the loaded save (character), or nullptr.
const char* test_blocker(const Test& t);
// Stage a test. cfg_extra = config values picked with the test's config buttons.
bool go(const Test& t, const std::vector<std::pair<std::string, int>>* cfg_extra, std::string& msg);
bool go_room(const char* room, bool has_pos, const float* pos, std::string& msg);

// Helpers (all refuse outside a room; each logs).
bool heal(std::string& msg);
bool refill_boost(std::string& msg);
bool refill_mp(std::string& msg);
bool boss_hp_1(std::string& msg);
bool monsters_hp_1(std::string& msg);
bool save_pos(std::string& msg);
bool restore_pos(std::string& msg);
bool set_flag(int idx, int value, std::string& msg);
int get_flag(int idx);
bool run_helper(const std::string& name, std::string& msg);

// The engine reads a config level in three ways; `live` says what a change can do
// without a restart, in words the panel shows.
const char* config_note(const std::string& name, int value);

extern bool g_invincible, g_hp_lock, g_allow_save, g_allow_char_switch;

}  // namespace probe
