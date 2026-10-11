// The test list: plain data read from retail_probe_tests.ini (next to the DLL).
#pragma once
#include <string>
#include <utility>
#include <vector>

struct CfgButton {            // "shadows:0|2" -> one row of buttons for a config byte
    std::string name;         // shadows / reflections / glare / other / caustics
    std::vector<int> values;
};

struct Test {
    std::string id, group, question, todo, watch, status, note;
    int character = -1;       // -1 any, 0 Adol, 1 Yunica, 2 Hugo, 3 Toal
    int difficulty = -1;      // -1 keep, 0 Very Easy, 1 Easy, 2 Normal, 3 Hard, 4 Nightmare
    std::string room;         // "S_1004"; empty = stay in the current room
    bool has_pos = false;
    float pos[3] = {0, 0, 0};
    bool has_facing = false;
    float facing_deg = 0;     // the game's own door-arrival degrees (SEN +0x238)
    int level = 0;            // 0 = keep
    int weapon_tier = -1;     // -1 keep, 0..10 (0 Lv1, 1 Lv2, 2 Lv3, 4 Lv4, 6 Lv5, 8 Lv6, 10 Max)
    std::vector<std::pair<int, int>> items;   // item id (0..127) -> count
    std::vector<std::pair<int, int>> flags;   // g_flags index -> value
    std::vector<std::pair<int, int>> gear;    // gear cell (item id 0..53) -> value
    int skill_lv[3] = {-1, -1, -1};           // wind, thunder, fire: -1 keep, 0..3
    int equip_skill = -2;     // -2 keep, -1 none, 0 wind, 1 thunder, 2 fire
    bool hp_full = true, mp_full = true;
    int boost = -1;           // -1 keep, 0 empty, 1 full
    int sp = -1;              // -1 keep
    std::vector<std::pair<std::string, int>> config;   // applied with the room load
    std::vector<CfgButton> cfg_buttons;
    std::vector<std::string> helpers;         // boss_hp_1, monsters_hp_1, invincible, hp_lock
    std::vector<std::string> answers;         // answer buttons (default Yes / No / Unclear)
    std::vector<int> show_flags;              // extra flags shown in the status line
};

struct TestFile {
    std::vector<Test> tests;
    std::vector<std::string> errors;   // parse problems, shown in the panel
    int toggle_vk = 0x24;              // VK_HOME
    std::string path;
};

bool load_tests(const char* path, TestFile& out);
const char* char_name(int c);
const char* diff_name(int d);
