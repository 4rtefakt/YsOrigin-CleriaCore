// The cutscene overlay sprites (build/event_re/WNDDIALOG_DRAW.md §7-§8), both GoXx objects:
//  - CaptionCard (type 8): op 0xe2, the area-name card data\image\<s>.dds, 150 frames
//    (45 in / 60 hold / 45 out), drawn additively 1:1 at (16, 16), pinned to the left edge.
//  - Busts (type 6): ops 0xe3 Portrait_Open, 0xe4 Close, 0xe5 Move, 0xe6 Color, 0xe7 Priority,
//    0xe8 FadeSpeed; data\image\<s>.dds 1:1 with y the bottom edge, 50-frame fades, tweened moves.
// [not modelled] the V_ full-screen illustrations' special anchoring and pillar fills (§8).
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "cleria/menusprite.hpp"

namespace cleria {

class CaptionCard {
  public:
    void open(const std::string& name);              // +0x120 = 150
    void step();                                     // FUN_0058fcb0 case 8
    void draw(DrawList& out) const;                  // FUN_005a16c0
    bool alive() const { return timer_ > 0; }

  private:
    std::string tex_;                                // "<NAME>.DDS"
    int timer_ = 0;
};

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
    void draw(DrawList& out) const;
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
