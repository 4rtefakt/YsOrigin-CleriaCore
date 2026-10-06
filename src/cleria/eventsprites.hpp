// The cutscene overlay sprites (build/event_re/WNDDIALOG_DRAW.md §7-§8), both GoXx objects:
//  - CaptionCard (type 8): op 0xe2, the area-name card data\image\<s>.dds, 150 frames
//    (45 in / 60 hold / 45 out), drawn additively 1:1 at (16, 16), pinned to the left edge.
//  - Busts (type 6): ops 0xe3 Portrait_Open, 0xe4 Close, 0xe5 Move, 0xe6 Color, 0xe7 Priority,
//    0xe8 FadeSpeed; data\image\<s>.dds 1:1 with y the bottom edge, 50-frame fades, tweened moves.
//    The V_ story cards' per-name anchoring and side-pillar fills (FUN_0058b410, §8.1).
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "cleria/menusprite.hpp"

namespace cleria {

class CaptionCard {
  public:
    // `hold` = +0x124 (the timer stops at 45: fade in, then stay up), `alt` = +0x128 (drawn at
    // (0, 192) in the 4:3 field instead of (16, 16) on the left edge). The op's card has neither;
    // the game over's gameover.dds (FUN_00427820 -> FUN_005a15d0) has both.
    void open(const std::string& name, bool hold = false, bool alt = false);   // +0x120 = 150
    void step();                                     // FUN_0058fcb0 case 8
    void draw(DrawList& out) const;                  // FUN_005a16c0
    bool alive() const { return timer_ > 0; }

  private:
    std::string tex_;                                // "<NAME>.DDS"
    int timer_ = 0;
    bool hold_ = false, alt_ = false;                // +0x124, +0x128
};

// FUN_005d3be0, the authored -> pixel mapper (build/menu_re/PAUSEBOOK_DRAW.md §0.4, WNDDIALOG_DRAW.md
// §8.1) for a W x H backbuffer: `axis` 0 = x (1024 wide), 1 = y (768 tall); `mode` 1/2/3 = the 4:3
// field pinned left / centred / right, else a plain stretch; `fit` (its byte argument) scales the
// 4:3 field to the full width on a wide screen, cropping top and bottom. Returns `v` untouched
// when the screen is 4:3 on that axis (A / B == 1.0) and `fit` is off. [BYTES 0x5d3be0..0x5d3e20]
double map_coord(int axis, int mode, double v, bool fit, int W, int H);

// How FUN_0058b410 draws a bust, chosen by its name (and x for `t_` busts) [BYTES 0x58b5f5..0x58ba67].
struct CardPath {
    enum Kind : std::uint8_t {
        Field,      // x through the mapper in `mode` (1/2/3), y plain
        Stretch,    // V_109: no mapping, x stretched to the full width like y
        Cover,      // V_30x: both axes mapped with `fit` (full width, cropped vertically)
    } kind = Field;
    int mode = 2;
    // The side-pillar fill drawn after the card on a wide screen: 1 = opaque black (V_15x, V_17x,
    // V_2xx, V_32x, V_400..402, V_45x), 2 = HugoPillar art at the card's alpha (V_160).
    int pillars = 0;
};
CardPath card_path(const std::string& name, float x);

class Busts {
  public:
    // The texture size of a bust (the host loads data\image\<NAME>.DDS); false if missing.
    std::function<bool(const std::string& tex, int* w, int* h)> size_of;
    void open(const std::string& name, float x, float y);                       // 0xe3
    void close(const std::string& name);                                        // 0xe4
    void move(const std::string& name, float x, float y, int frames, int curve); // 0xe5
    void colour(const std::string& name, const std::uint32_t c[4]);             // 0xe6
    void priority(const std::string& name, int p);                              // 0xe7
    void fade_speed(const std::string& name, float s);                          // 0xe8
    void step();
    // FUN_0058b410 for each bust on a W x H backbuffer (the V_ paths and pillars need the size).
    void draw(DrawList& out, int W, int H) const;
    bool empty() const { return list_.empty(); }
    void clear() { list_.clear(); }

  private:
    struct Bust {
        std::string name, tex;
        int w = 0, h = 0;
        float x = 0, y = 0, sx = 0, sy = 0, tx = 0, ty = 0, t = 1, dt = 1;
        int curve = 0;
        float fade = 0, speed = 1;
        bool closing = false, dead = false;
        std::uint32_t corner[4] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
        int prio = 0;
    };
    Bust* find(const std::string& name);
    std::vector<Bust> list_;
};

}  // namespace cleria
