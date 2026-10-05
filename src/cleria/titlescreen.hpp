// The title screen: build/menu_re/TITLE_DRAW.md §1 and §3. GoWndDemo's background (setup
// FUN_00576020, draw FUN_00576c90) and the title tick FUN_005765c0, which builds the main list
// or the Bonus sub-list as a frameless GoWndDialog (wnddialog.hpp) whenever no window and no
// book is up. The callbacks (FUN_00575c10 / 0x575ea0 / 0x575f30 / 0x575d70 / 0x575f50 /
// 0x576000 / 0x575ca0 / 0x575dd0 / 0x575f20) become actions for the host: New Game opens the
// book with ctx 4, Load Game ctx 2, Options ctx 3.
//
// [not modelled] the Time Attack / Arena overlays and their shade channels (the logo stays
// white), the Steam achievements overlay, the mouse.
#pragma once
#include <vector>

#include "cleria/menusprite.hpp"
#include "cleria/wnddialog.hpp"

namespace cleria {

// The gates (§3.2): the clear bytes 0x74ab79..0x74ab7b (Toal / Hugo / Yunica), the Steam
// achievements, and Release\data.nya.
struct TitleGates {
    bool clear_toal = false, clear_hugo = false, clear_yunica = false;
    bool achievements = false;
    bool nya = false;
    bool any_clear() const { return clear_toal || clear_hugo || clear_yunica; }
    bool all_clear() const { return clear_toal && clear_hugo && clear_yunica; }
};

enum class TitleAction { None, NewGame, LoadGame, Options, Exit, Achievements, TimeAttack, Arena, BonusShop };

class TitleScreen {
  public:
    // FUN_005765c0: (re)build the list when nothing is up, step it, run the chosen callback.
    TitleAction update(const WndInput& in, bool book_open, const TitleGates& gates, const RichMetrics& m,
                       std::vector<int>& sounds);
    // FUN_00576c90 (pillars + logo), then the list window.
    void draw(DrawList& out) const;
    bool list_alive() const { return list_.alive(); }

  private:
    WndDialog list_;
    std::vector<TitleAction> actions_;
    int kind_ = 0;                        // 0x74e259: 0 main, 1 bonus
    int cursor_mem_ = 0;                  // 0x74ac3d (the list seeds slot cursor_mem + 1)
    bool fired_ = false;
};

}  // namespace cleria
