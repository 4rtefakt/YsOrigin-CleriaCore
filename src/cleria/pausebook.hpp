// The in-game pause book (go_wndmain2), ported from build/menu_re/PAUSEBOOK_DRAW.md: the shell
// and tab navigator (FUN_005a6230 / FUN_005a68f0 / FUN_005a73c0), the Equip page (FUN_005a7580
// / FUN_005a8030 / FUN_005a9140), the Item page with its Use popup (FUN_005a9d10 / FUN_005aa370
// / FUN_005aad90 / FUN_005ab0d0 / FUN_005abd10), the status panel (FUN_005b05c0 /
// FUN_005b1680) and the caption + item-detail panel (FUN_005ae280 / FUN_005ae970 /
// FUN_005b0130). Every widget is a MenuSprite; draw() emits the engine's draw order.
//
// Character Select (page 8, ctx 4) and the New-Game popups follow build/menu_re/TITLE_DRAW.md
// §4-§6, as do the direct contexts' plaques (ctx 1 statue Save, ctx 2 / 3 title Load / Option).
// The Load / Save lists, their Load / Save / Delete dialogs and the Option page follow
// build/menu_re/PAUSEBOOK_DRAW_SVLD_OPT.md (pausebook_lists.cpp).
//
// The Topic page (category menu, index sheets, datacard viewer, manual) and the Crystal's
// warp map follow build/menu_re/PAUSEBOOK_DRAW_TOPIC_MAP.md (pausebook_topic.cpp).
//
// The mouse follows build/shell_re/MOUSE.md §5.3-§5.4: hover on cursor motion over the tab plaques,
// the Equip / Item tiles, the Use popup, the Load / Save plates (+ the tabs on a click, the wheel and
// its warp), the Load / Save / Delete buttons, the Option rows, its message boxes and the sliders'
// drag, the warp map (+ the wheel and its warp) and Character Select (+ the wheel and its warp); a
// click confirms only inside the selected item.
// [not modelled] the title-side contexts (the title Load list's crash-backup box); the Topic pages'
// mouse; the page-transition highlight 0x7440e0 (its input block and its warp); languages other than
// English.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "cleria/inventory.hpp"
#include "cleria/menusprite.hpp"
#include "cleria/mouse.hpp"
#include "cleria/wnddialog.hpp"

namespace cleria {

struct GameInfo;
struct ItemTable;
struct CastTable;
struct EnemyTable;

// Character Select's gates (TITLE_DRAW.md §5-§6): the clear bytes, data.nya, the unlocked
// versions (0x74ac00 / 04 / 08 Yunica / Hugo / Toal Extra, 0x74abf8 / 0x74abfc Adol's Felghana /
// Napishtim, 0x74ac24 Yunica's outfit) and the mode 0x74e25e (0 New Game, 1 Time Attack, 2 Arena).
struct CharSelectGates {
    bool clear_yunica = false, clear_hugo = false, clear_toal = false;
    bool nya = false;
    bool extra_yunica = false, extra_hugo = false, extra_toal = false;
    bool adol_felghana = false, adol_napishtim = false, yunica_outfit = false;
    // The cleared-tier bytes 0x74ab88.. / 8c.. / 94.. / 98.. / 9c.. (Very Easy .. Nightmare) of Yunica / Hugo / Toal:
    // the Difficulty popup's "(Clear)" labels in a New Game (FUN_005c4950).
    std::array<std::array<bool, 3>, 5> tier{};
    // CleriaCore's (not retail's): Toal's bar is open whatever the clear bytes say -- [cleria.qol]
    // UnlockCharacters, or a mod's cleria.game.unlock_character(3) (build/shell_re/LUA_API.md).
    bool open_toal = false;
    int mode = 0;
};
// Character Select in Time Attack (0x74e25e == 1), FUN_005c3790 state 1: every tick, after the keys / wheel / hover
// moved the cursor, a bar whose character has not cleared the game is stepped over [EXPORT 005c3790.c:225-236]:
// Yunica's -> Hugo's; Hugo's -> Yunica's, or Toal's when Toal has cleared and the cursor came from Yunica's (prev 0).
// The order is retail's: with only Toal cleared, a step from Toal's ends on Yunica's (her test ran first).
inline int charsel_ta_cursor(int cur, int prev, bool clear_yunica, bool clear_hugo, bool clear_toal) {
    if (!clear_yunica && cur == 0) cur = 1;
    if (!clear_hugo && cur == 1) cur = (!clear_toal || prev > 0) ? 0 : 2;
    return cur;
}
// The first bar the mouse hover tests (local_30, [EXPORT 005c3790.c:119-121]): 1 in Time Attack when Hugo has
// cleared and Yunica has not, else 0.
inline int charsel_hover_first(int mode, bool clear_yunica, bool clear_hugo) {
    return mode == 1 && clear_hugo && !clear_yunica ? 1 : 0;
}
// What Character Select and Difficulty chose (the callbacks' writes).
struct NewGameInfo {
    int character = 1;                      // 0x76bb74: 1 Yunica, 2 Hugo, 3 Toal, 0 Adol
    int type = 0;                           // 0x74c0dc / d8 / d4 / cc: 0 Normal, 1 Extra (Adol: Felghana / Napishtim)
    int outfit = 0;                         // 0x74c0d0
    bool outfit_set = false;                // Outfit Select answered (its two callbacks are the flag's only writers
                                            // besides a load; otherwise the session's value stands, EXTRA_VERSIONS §1.4)
    int difficulty = 0;                     // 0x74ac3c: 0 Normal, 1 Hard, 2 Nightmare
    bool easy = false, very_easy = false;   // 0x74e685 / 0x74e684
};

// A save slot as the header scanner FUN_00430140 leaves it (PAUSEBOOK_DRAW_SVLD_OPT.md §1.2).
struct SlotCard {
    int status = 0;                         // rec+0x90: 1 card, 0 empty, anything else broken
    int level = 0;                          // +0x04
    int difficulty = 0;                     // +0x20: 0 Normal, 1 Hard, 2 Nightmare
    bool easy = false, very_easy = false, inferno = false;   // +0x28 / +0x2c / +0x30
    std::uint32_t play_frames = 0;          // +0x34 (1/60 s)
    int character = 0;                      // +0x38 (the icon cell)
    std::string date, time, area;           // +0x4c / +0x57 / +0x94
};

// What the book reads and writes of the game (FUN_00420c40 recomputes the stats every frame
// the book is open, so the host refreshes these before each update).
struct BookModel {
    std::int32_t* flags = nullptr;          // g_flags: counts 0..127, slots 151..153, 228 ...
    InvState* inv = nullptr;
    const ItemTable* items = nullptr;
    const GameInfo* gi = nullptr;
    int character = 1;                      // 0x76bb74 = g_flags[150]
    int level = 1;
    float hp = 0, max_hp = 0, atk = 0, def = 0, exp = 0;
    float next_exp = 0;                     // 0x76b910: the EXP total for the next level
    // FUN_00420880: the stats with a candidate item swapped into its slot (the Equip preview).
    std::function<PlayerStats(int item)> preview;
    bool player_can_act = true;             // grounded and not hurt/dead (else "@UseItemBoo")
    bool underwater = false;                // the Harmonica's "@UseHarmonica2"
    // The Load / Save list: the scanner for slot 0..63 (yso_NN.bin) and the last-used slot
    // DAT_0074ac3e (ini SaveFileNo).
    std::function<SlotCard(int slot)> scan;
    int* save_file_no = nullptr;
    // The Option page's live values: BGM / SFX / brightness 0..1024 and the four switches
    // (analog, dash, hold-attack, always-dash; bytes 0x6d606d..0x6d6070).
    int* sliders = nullptr;
    bool* checks = nullptr;
    // The Topic page: the collection records 0x74ac40 + id*0x1c (seen byte, kill count; by cast
    // id, null = nothing seen), the cleared-difficulty bytes 0x74ab88.. (the boss stats; null =
    // none), the manual pages enabled 0x76b8d8[13], and CASTINFO / ENEMYINFO.
    const std::uint8_t* topic_seen = nullptr;
    const std::int32_t* topic_kills = nullptr;
    const std::uint8_t* clear_flags = nullptr;
    std::uint8_t* manual_on = nullptr;
    const CastTable* casts = nullptr;
    const EnemyTable* enemies = nullptr;
    // The warp map: points enabled 0x76c11c[24]; the host starts an S_COMMON script and runs
    // its first burst at once (FUN_00434970), so the book reads flag 198 straight after.
    const std::uint8_t* warp_on = nullptr;
    std::function<void(const std::string&)> start_script;
    // Character Select (ctx 4) and the GDI metrics its popups lay out with.
    CharSelectGates charsel;
    RichMetrics metrics;
};

// The input record (PAUSEBOOK_DRAW.md §0.7), with the host's auto-repeat already applied.
struct BookInput {
    int hstep = 0, vstep = 0;               // -1 / +1 (vstep +1 = down)
    bool confirm = false, cancel = false;   // pressed edges 0x2000 / 0x4000
    bool held_left = false, held_right = false;   // d-pad bits 2 / 4 held (the Option sliders)
    // The mouse (MOUSE.md §5.3): the record's RAW cursor ints (+0x00 / +0x04; the hit point is the
    // arrow tip, -16), the click edge and the held 0x10000 (the slider grab), the held MOUSE_DIR
    // 0x1000, the cursor manager's moved 0x7325d8 (every hover gate) and mouse mode 0x7325d4, the
    // wheel (+0x08; the dispatcher debounces it) and the display (the x-anchor-2 unprojection).
    int mx = 0, my = 0;
    bool click = false, click_held = false, dir_held = false;
    bool moved = false, mouse_mode = false;
    int wheel = 0;
    mouse::Display display;
};

struct BookEvents {
    std::vector<int> sounds;                // FUN_0043a040 ids (0 confirm, 1 cancel, 2 buzzer, 3 move, ...)
    std::string script;                     // an S_COMMON script to start ("@UseHammer" ...)
    int use_item = -1;                      // FUN_00443390 + count-- (the Panacea)
    bool clamp_hp = false;                  // the equip changed: curHP = min(curHP, maxHP)
    bool closed = false;                    // subId = -1
    int load_slot = -1, save_slot = -1, delete_slot = -1;   // FUN_0042f5e0 / FUN_0042e9a0 / FUN_0042f510
    bool options_changed = false;           // a slider / switch moved (apply + write the ini)
    int option_quit = -1;                   // Yes in a message box: 0 Return to Title, 1 Exit
    bool new_game = false;                  // Difficulty chosen: start a game with new_game_info
    NewGameInfo new_game_info;
    // A cursor warp FUN_005d33f0(warp_x, warp_y, warp_mode) (record units; the host moves the pointer).
    bool warp = false;
    int warp_x = 0, warp_y = 0, warp_mode = 0;
};

class PauseBook {
  public:
    // FUN_005a4fd0(ctx): 0 = the pause book, 1 = straight into the Save list (the statue).
    void open(const BookModel& m, int ctx = 0);
    bool is_open() const { return open_; }
    void close() { open_ = false; }
    void update(const BookInput& in, BookModel& m, BookEvents& ev);   // FUN_005a5130 (a real step)
    void draw(DrawList& out, const BookModel& m) const;              // FUN_005a5d50
    int page() const { return page_; }
    int sub_id() const { return sub_; }
    const std::vector<std::string>& popup_rows() const { return cspop_rows_; }   // the last New-Game popup's rows (tests)

  private:
    // shell
    void build_shell();
    void update_nav(const BookInput& in, BookEvents& ev);
    // Equip
    void build_equip(const BookModel& m);
    void equip_colours(bool focused, bool animate);
    void update_equip(const BookInput& in, BookModel& m, BookEvents& ev);
    void draw_equip(DrawList& out, const BookModel& m) const;
    // Item + the Use popup
    void build_item(const BookModel& m);
    void item_colours(bool bright, bool animate);
    void update_item(const BookInput& in, BookModel& m, BookEvents& ev);
    void build_popup();
    void update_popup(const BookInput& in, BookModel& m, BookEvents& ev);
    void draw_item(DrawList& out, const BookModel& m) const;
    // status, caption, detail
    void draw_status(DrawList& out, const BookModel& m) const;
    void update_caption(const BookModel& m);
    void set_detail(int id);
    void draw_caption_detail(DrawList& out, const BookModel& m) const;
    // the Load / Save list and its dialogs
    void build_svld(const BookModel& m, bool from_open);
    void svld_colours(bool lit, bool animate);
    void rescan(const BookModel& m);
    void update_svld(const BookInput& in, BookModel& m, BookEvents& ev);
    void draw_svld(DrawList& out) const;
    struct Dialog {
        int state = 0, cur = 0, ret = 0;
        MenuSprite bg, btn[3], cursor;
    };
    void build_dialog(int k);               // 0 Load, 1 Save, 2 Delete
    void update_dialog(int k, const BookInput& in, BookModel& m, BookEvents& ev);
    void draw_dialog(DrawList& out, int k) const;
    // the Topic page
    void build_category();
    void update_category(const BookInput& in, BookModel& m, BookEvents& ev);
    void build_sheet(const BookModel& m);
    void enter_set(const BookModel& m, int set);
    void build_bar();
    void bar_uvs();
    void pager(const BookModel& m, int d);
    void update_bar(const BookInput& in, BookModel& m, BookEvents& ev);
    void update_grid(const BookInput& in, BookModel& m, BookEvents& ev);
    void place_grid_cursor(const BookModel& m);
    void build_viewer();
    void build_card(const BookModel& m);
    void step_card(const BookModel& m, int d);
    void update_viewer(const BookInput& in, BookModel& m, BookEvents& ev);
    void build_manual(const BookModel& m);
    void manual_targets(const BookModel& m);
    void update_manual(const BookInput& in, BookModel& m, BookEvents& ev);
    void draw_topic(DrawList& out, const BookModel& m) const;
    int topic_count(int page) const;
    int topic_base() const;
    int topic_entry(int slot) const;          // the global entry index of a sheet slot, -1 if none
    bool topic_seen(const BookModel& m, int slot) const;
    // Character Select (page 8)
    bool cs_locked(const BookModel& m, int bar) const;
    void build_charsel(const BookModel& m);
    void cs_pop_chibi();
    void cs_colours(const BookModel& m, bool lit);
    void cs_popup(const BookModel& m, int which);   // 0 Type, 1 Outfit, 2 Difficulty
    void update_charsel(const BookInput& in, BookModel& m, BookEvents& ev);
    void draw_charsel(DrawList& out) const;
    // the warp map
    void build_map();
    void update_map(const BookInput& in, BookModel& m, BookEvents& ev);
    void draw_map(DrawList& out, const BookModel& m) const;
    // the Option page
    void build_option(const BookModel& m);
    void option_colours(bool lit, bool animate);
    void option_checks(const BookModel& m);
    void place_knob(int i, int value);
    void update_option(const BookInput& in, BookModel& m, BookEvents& ev);
    void draw_option(DrawList& out) const;

    bool open_ = false;
    int sub_ = 0;                           // menuObj+0x130
    int frames_ = 0;                        // menuObj+0x134
    // the shell (0x732c50.. )
    int nav_ = 0, page_ = 0, last_page_ = 0;
    MenuSprite back_, plaque_[9], cursor_, gold_;
    // Equip (0x7335c8 ..)
    int estate_ = 0, ecur_ = 0;
    std::array<std::int32_t, 22> erec_count_{};
    MenuSprite epanel_, ecursor_, etile_[22], eicon_[22], emarker_[5];
    // Item (0x735e28 ..)
    int istate_ = 0, icur_ = 0;
    std::array<std::int32_t, 30> irec_count_{};
    MenuSprite ipanel_, icursor_, itile_[30], iicon_[30], ihi_;
    // the Use popup (0x738b40 ..)
    int pstate_ = 0, psel_ = 0, pitem_ = -1;
    MenuSprite pframe_, pyes_, pno_, pcur_;
    float pbx_ = 0, pby_ = 0;
    // the caption / detail panel
    std::string caption_;
    int detail_id_ = -1;
    MenuSprite dplate_, dicon_;
    // the Load / Save list (0x73bc30 ..) and the dialogs (0x73cb08 / 0x73ce78 / 0x73d1e8)
    int svstate_ = 0, svpage_ = 0, svrow_ = 0;
    std::array<SlotCard, 8> cards_{};
    MenuSprite svpanel_, svcursor_, svtab_[8], svplate_[8], svicon_[4];
    Dialog dlg_[3];
    // the Option page (0x73d4b0 ..)
    int ostate_ = 0, orow_ = 0, oenter_ = 0x0b;
    bool odrag_[3] = {};                    // slider +0x14: the knob is grabbed
    int odrag_off_[3] = {};                 // slider +0x18: knob.x - the grab's raw x
    int wheel_db_ = 0;                      // 0x7d9be0: the dispatcher's wheel debounce
    MenuSprite orow_spr_[10], ocursor_, ocheck_[4], otrack_[3], oknob_[3];
    Dialog omsg_[2];                        // Return to Title / Exit (w_MsgBox)
    // Topic: the category menu (0x73eab8 ..)
    int tstate_ = 0, tcur_ = 0, tset_ = 0;
    MenuSprite tbar_[3], tglow_;
    // the index sheet (0x73f038 ..) and its Prev / Select / Next bar (0x73ed78 ..)
    int gstate_ = 0, gcur_ = 0, gpage_ = 0, bstate_ = 0, bcur_ = 0;
    MenuSprite tbook_, tcell_[32], gcursor_, gglow_, tbtn_[3], bglow_;
    // the datacard viewer (0x7409e8 ..)
    int vstate_ = 0, vcur_ = 0, vfirst_ = 0;
    bool vboss_ = false;
    MenuSprite vbg_, vbtn_[2], vglow_, vport_[2], vboss_pic_;
    std::string vtext_[2][2];               // [entry][block]
    std::string vstat_[2][6];               // HP STR EXP LV DEF GOLD
    // the manual (0x794680 ..)
    int mstate_ = 0, mcur_ = 0, mshown_ = 0;
    MenuSprite mbook_, mpage_, mtab_[13], mlabel_[13], mglow_;
    // the warp map (popup state 3; 0x738e00 ..)
    int mapcur_ = 0;
    float pan_ = 0;
    bool close_next_ = false;
    MenuSprite mtower_, mplaque_, mcrystal_, mplate_[22], mbar_;
    std::uint32_t mpillar_diffuse_ = 0, mpillar_specular_ = 0x808080;
    // Character Select (0x742f78 ..)
    int csn_ = 3, csstate_ = 0, cssel_ = 0, cschibi_c_ = 0;
    MenuSprite cspanel_, csbar_[4], csface_[4], csport_[4], cshi_, cschibi_, csstrip_, csbanner_, cstext_;
    WndDialog cspop_;
    std::vector<int> cspop_codes_;
    std::vector<std::string> cspop_rows_;
    int cspop_kind_ = 0, cs_next_ = -1;
    bool cspop_done_ = false, cs_cancelled_ = false;
    NewGameInfo cs_result_;
};

// The level badges the HUD draws on the equipped weapon / armor / footwear (builder D FUN_0043fe00), by the
// book's level rules: the weapon's tier 0x76a634[character] and a gear's "strengthened" cell 0x76a654[id].
std::string hud_weapon_label(int tier);
std::string hud_gear_label(int strengthened);

}  // namespace cleria
