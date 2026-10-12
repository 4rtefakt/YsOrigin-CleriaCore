// The event windows: build/event_re/WNDDIALOG_DRAW.md.
//  - WndText (go_wndtext.cpp, type 0x16): the Window_Msg message box. Handler FUN_00573e60,
//    text submit FUN_0057b030, update FUN_0057a0e0, draw FUN_0057a6d0, prompt FUN_0057a410.
//  - WndDialog (go_wnddialog.cpp, type 0x15): the Window_Treasure item-get box (FUN_005781f0)
//    and the Window_Menu / Menu_* choice box (FUN_00578440 / FUN_005785d0 / FUN_00578710).
//    Update FUN_005789a0, draw FUN_00578f80.
// Both emit draw lists (menusprite.hpp) in authored 1024x768 units, x-anchor 2.
//
// The mouse (build/shell_re/MOUSE.md §5.1): GoWndDialog's hover (path A on the first interactive
// tick in mouse mode, path B on cursor motion) and the click that confirms only over a row.
// [not modelled] the "hidden" toggle (dead in this build), focus between several dialogs (one
// dialog at a time is always focused), the language-0 indent.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "cleria/menusprite.hpp"
#include "cleria/mouse.hpp"
#include "cleria/richtext.hpp"

namespace cleria {

// The input record bits the windows read (PAUSEBOOK_DRAW.md §0.7): Confirm 0x2000 (with
// 0x10000 = the mouse), Cancel 0x4000; held (+0x20), pressed edge (+0x24), auto-repeat
// (+0x28), the vertical step (+0x30, +1 = down).
struct WndInput {
    bool confirm_held = false, confirm_pressed = false;
    bool cancel_held = false, cancel_pressed = false, cancel_repeat = false;
    int vstep = 0;
    // The mouse (MOUSE.md §5.1): the click edge 0x10000 (it confirms only with the hover flag), the
    // cursor manager's moved 0x7325d8 / mouse-mode 0x7325d4, the record's RAW cursor ints and the
    // display X() maps through.
    bool click_pressed = false;
    bool mouse_moved = false, mouse_mode = false;
    int mx = 0, my = 0;
    mouse::Display display;
};

class WndText {
  public:
    // FUN_00573e60 after the host resolved the speaker: `cls` = the entity class (0 = none);
    // `character` = 0x76bb74 for the player's class 0xd; `frame` = op1 (-1 = no face).
    // `body` is the raw script string (escapes and markup unprocessed).
    void message(int cls, int character, int frame, const std::string& name, const std::string& body,
                 const RichMetrics& m);
    void close() { close_ = true; }                       // Window_Close: +0x274
    void arm() { armed_ = true; }                         // Wait_Prompt: +0x271
    bool accepted() const { return accepted_; }           // +0x275
    bool alive() const { return alive_; }
    void set_speed(std::uint32_t glyphs_16_16) { speed_ = glyphs_16_16; }   // SetMsgSpd 0xd6
    // FUN_0057a0e0. `dialog_alive` hides the prompt while a GoWndDialog exists.
    void update(const WndInput& in, std::vector<int>& sounds);
    void draw(DrawList& out, bool dialog_alive) const;
    int face() const { return face_; }                    // +0x258: f_(200 + 10*face) (-1 none)
    int face_frame() const { return frame_; }             // +0x25c
    float box_alpha() const { return box_; }              // +0x248
    const RichLayout& layout() const { return layout_; }  // the body as laid out (drive / tests)
    std::uint32_t reveal() const { return reveal_; }      // +0x250 (16.16 glyphs)
    // CleriaCore's text-speed assist (access::TextSpeed): 0 retail, 1 always the fast rate, 2 the
    // whole message at once. The host sets it; 0 is the faithful window.
    int text_speed = 0;

  private:
    bool alive_ = false, close_ = false, armed_ = false, accepted_ = false;
    bool released_ = false, fast_ = false, revealed_ = false;
    float box_ = 0, text_ = 1;                            // +0x248, +0x24c
    std::uint32_t reveal_ = 0, reveal_end_ = 0;           // +0x250, +0x254
    std::uint32_t speed_ = 0x12000;
    int prompt_ = 0, tick_ = 0;                           // +0x268, +0x26c
    int face_ = -1, frame_ = -1;
    RichLayout layout_;
};

class WndDialog {
  public:
    // FUN_005781f0: the item-get box. (x, y) = the box's centre x and TOP y. item -1 = no icon.
    void treasure(float x, float y, int item, const std::string& text, const RichMetrics& m);
    // FUN_00578440 + FUN_005785d0 + FUN_00578710: a choice box. `labels[i]` is the script run
    // when item i is chosen. Menu_Init uses (512, 282); Menu_Select centres it vertically.
    // `tick` = the header's typewriter tick SE (+0x1fda; FUN_005784b0 menus clear it).
    void menu(float x, float y, const std::string& title, const std::vector<std::string>& items,
              const std::vector<std::string>& labels, const RichMetrics& m, bool centre_y, bool tick = true);
    void set_no_frame(bool v) { no_frame_ = v; }          // +0x1fdb (the title lists)
    void set_cancel(int item) { cancel_ = item; }         // Menu_AssignCancel (1-based, -1 none)
    void set_cursor(int item) { cursor_ = item; }         // MoveCursorLastScript (1-based)
    void set_fast() { fast_ = true; }                     // +0x1fd4: the caption reveals at 0x30000 a tick
    void close() { close_ = true; }                       // +0x1fd5
    void arm() { armed_ = true; }                         // +0x1fd1
    bool accepted() const { return accepted_; }           // +0x1fd9
    bool alive() const { return alive_; }
    // The chosen record's label after a menu accept (copied to 0x75ad98), else empty.
    const std::string& chosen() const { return chosen_; }
    int chosen_index() const { return chosen_index_; }    // the accepted record (1-based), else -1
    bool hover() const { return hover_; }                 // +0x1fd7: the pointer is over the cursor row
    int cursor() const { return cursor_; }
    void update(const WndInput& in, std::vector<int>& sounds);
    void draw(DrawList& out) const;
    bool is_menu() const { return !treasure_; }
    int text_speed = 0;                                   // as WndText::text_speed

  private:
    void layout(const RichMetrics& m);
    bool alive_ = false, treasure_ = false, close_ = false, armed_ = false, accepted_ = false;
    bool released_ = false, revealed_ = false, tick_sound_ = false, no_frame_ = false, fast_ = false;
    int chosen_index_ = -1;
    float alpha_ = 0;                                      // +0x1fb4
    std::uint32_t reveal_ = 0, reveal_end_ = 0;            // +0x1fb8, +0x1fbc
    int prompt_ = 0, tick_ = 0;                            // +0x1fc8, +0x1fcc
    float x_ = 512, y_ = 282, w_ = 352, h_ = 160;          // +0x1fe0.. (x = centre, y = top)
    float widest_ = 0;
    int item_ = -1;                                        // +0x1ff4
    int cursor_ = 1, cancel_ = -1;                         // +0x1ffc, +0x2004
    bool hover_ = false;                                   // +0x1fd7
    bool was_interactive_ = false;                         // +0x2008
    std::vector<std::string> texts_, labels_;
    std::vector<RichLayout> lay_;
    std::vector<float> rx_, ry_;                           // record offsets
    std::string chosen_;
};

// FACEWIN0 / DIALOG0 / MENUCURSOR as named textures (data\image\, the image table 4 / 5 / 7).
inline constexpr const char* kFaceWin = "FACEWIN0.DDS";
inline constexpr const char* kDialog0 = "DIALOG0.DDS";
inline constexpr const char* kMenuCursor = "MENUCURSOR.DDS";

}  // namespace cleria
