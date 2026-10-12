// The Time Attack / Arena stage select and its records panel (build/mode_re/STAGE_SELECT.md): the panel the
// title draw calls while its overlay fade is non-zero (FUN_0057d2a0 Time Attack, FUN_005802d0 Arena) and the
// window GoWndZzz over it (tick FUN_0057bb20, cursor quad FUN_0057cee0) — a bare object with no csprites and no
// go_wnddialog: everything is hand-drawn in F_MIDDLE. The stage tables are the exe's (.rdata 0x68c460..0x68cba4).
//
// [not modelled] the Steam leaderboard rows and views (sub-states 1 / 2: nothing of them exists with
// 0x75ae5c == 0); the languages other than English.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "cleria/menusprite.hpp"
#include "cleria/records.hpp"
#include "cleria/wnddialog.hpp"

namespace cleria {

namespace stages {
// Time Attack [BYTES 0x68c460, 0x68c4cc, 0x68cba4]: the row count of a character (0 Adol, 1 Yunica, 2 Hugo,
// 3 Toal: 14 / 13 / 13 / 14), a row's English name and its scene ("S_1080"). Row 0 is the Boss Rush; its scene
// entry repeats row 1's and is never entered as row 0 (the new-game init turns stage 0 into 1).
int time_attack_count(int character);
const char* time_attack_name(int character, int row);
const char* time_attack_scene(int character, int row);
// Arena [BYTES 0x68c57c, 0x68c604]: rows 0..6 (row 6 "???" is S_9159 again).
const char* arena_name(int row);
const char* arena_scene(int row);
// FUN_0057d1e0: a record's text — "--:--:---" (with the font's "#>n" shifts) for 0, "%d:%02d:%02d:%03d" from
// an hour on (hours capped at 9999), else "%02d:%02d:%03d".
std::string time_text(std::uint32_t ms);
}  // namespace stages

struct StageModel {
    int mode = 1;                         // 0x74e25e: 1 Time Attack, 2 Arena
    int character = 1;                    // 0x76bb74
    int column = 1;                       // the chosen difficulty's column: 0 Easy, 1 Normal, 2 Hard, 3 Nightmare
    bool nya = false;                     // 0x74c106: the Nightmare column exists
    int arena_rows = 1;                   // 1 + the six stage dwords 0x74ac0c..0x74ac20
    bool picking = false;                 // 0x74e25a >= 2: a difficulty is chosen (the cursor / dim colours, Cancel)
    // The tables FUN_00583960 picks (the normal or the "e" one): Time Attack's, the Arena's.
    const records::RecordTable* ta = nullptr;
    const records::RecordTable* ar = nullptr;
    std::uint32_t ms = 0;                 // 0x767c20, timeGetTime(): the new-record pulse
    int rows() const { return mode == 2 ? arena_rows : stages::time_attack_count(character); }
};

struct StageEvents {
    std::vector<int> sounds;
    int launch = -1;                      // a stage row was confirmed (0x74ab9f = it, 0x74e25a = 3)
    bool back = false;                    // Cancel: 0x74e25a = 0, the host saves the records
};

class StageSelect {
  public:
    // The title's overlay channels, `mode` 1 Time Attack (0x74e25d -> 0x74e26c) / 2 Arena (0x74e25c -> 0x74e268):
    // each fade steps 0.05 a title tick toward its target. A new object = FUN_005755c0 (bytes and floats = 0).
    void set_target(int mode, bool on) { target_[mode == 2] = on; }
    void tick_fade();
    float fade(int mode) const { return fade_[mode == 2]; }
    // The title background's shade (FUN_00576c90): white -> 0xFFAFAFAF at full fade; the Arena's fade wins.
    std::uint32_t title_tint() const;

    void open(int cursor) { open_ = true; cursor_ = cursor; hover_ = -1; }   // GoWndZzz: +0x124 = (s8)0x74ab9f
    bool is_open() const { return open_; }
    int cursor() const { return cursor_; }
    void update(const WndInput& in, const StageModel& m, StageEvents& ev);   // FUN_0057bb20
    // FUN_0057d2a0 / FUN_005802d0 while their fade is non-zero, then FUN_0057cee0 while the window is up.
    void draw(DrawList& out, const StageModel& m) const;

  private:
    void panel(DrawList& out, const StageModel& m, bool arena) const;
    bool target_[2] = {false, false}, open_ = false;
    float fade_[2] = {0, 0};
    int cursor_ = -1;                     // +0x124: a row, rows() = Cancel, -1 = none
    int hover_ = -1;                      // 0x7de0f4 / 0x7de0fc: the mouse's last row
};

}  // namespace cleria
