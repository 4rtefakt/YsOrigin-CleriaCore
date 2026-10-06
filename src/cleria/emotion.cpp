// The emotion bubbles: build/event_re/CHR_OPS.md §1. See emotion.hpp.
#include "cleria/emotion.hpp"

#include <cmath>

namespace cleria {

namespace {

// DAT_0068b278: the hold length per kind [BYTES].
constexpr int kHold[kEmotionKinds] = {60, 60, 50, 60, 60, 180, 60, 60, 90};
// DAT_0069bf0c: the quad centre X per kind [BYTES].
constexpr float kCentre[kEmotionKinds] = {0.375f, 0.375f, 0, 0, 0, 0.375f, 0.375f, 0.375f, 0.375f};
// DAT_0069bf30 + kind*0x60: 12 {cell, duration} pairs; -1 = hold [BYTES].
struct CellDur { int cell, dur; };
constexpr CellDur kCells[kEmotionKinds][12] = {
    {{2, 10}, {3, 5}, {4, 5}, {5, -1}},
    {{0, 12}, {1, 12}, {0, 12}, {1, 12}, {0, 12}, {1, 12}, {0, 12}, {1, 12}, {0, -1}},
    {{8, 10}, {9, 10}, {8, 10}, {9, 10}, {8, 10}, {9, 10}, {8, 10}, {9, 10}, {8, -1}},
    {{10, 5}, {11, 5}, {12, 5}, {13, 10}, {10, 5}, {11, 5}, {12, 5}, {13, 10}, {10, 5}, {11, 5}, {12, 5}, {13, -1}},
    {{14, 10}, {15, 10}, {14, 10}, {15, 10}, {14, 10}, {15, 10}, {14, 10}, {15, 10}, {14, -1}},
    {{16, 24}, {17, 24}, {18, 24}, {19, 24}, {16, 24}, {17, 24}, {18, 24}, {19, 24}, {16, 24}, {17, 24}, {18, 24}, {19, -1}},
    {{20, 24}, {21, 24}, {22, 24}, {23, 24}, {20, 24}, {21, 24}, {22, 24}, {23, 24}, {20, 24}, {21, 24}, {22, 24}, {23, -1}},
    {{24, 10}, {25, 10}, {24, 10}, {25, 10}, {24, 10}, {25, 10}, {24, 10}, {25, 10}, {24, -1}},
    {{16, 30}, {17, 30}, {18, 30}, {19, -1}},
};

int kind_of(const Emotion& e) { return (e.kind >= 0 && e.kind < kEmotionKinds) ? e.kind : 0; }

// 0x4ffb1e..0x4ffb4c: walk the durations; a -1 entry (the signed minimum) always enters and holds.
int cell_at(int kind, int age) {
    int a = age;
    for (int i = 0; i < 12; ++i) {
        const CellDur& c = kCells[kind][i];
        if (c.dur == -1 || a < c.dur) return c.cell;
        a -= c.dur;
    }
    return kCells[kind][11].cell;
}

}  // namespace

void spawn_emotion(std::vector<Emotion>& list, int owner, int kind, float yoff) {
    for (Emotion& g : list)
        if (!g.dead && g.owner == owner && g.age < kHold[kind_of(g)]) g.age = kHold[kind_of(g)];
    int live = 0;
    for (const Emotion& g : list) live += !g.dead;
    if (live >= kEmotionCap) return;                  // FUN_0045e1b0: the pool is full
    Emotion e;
    e.owner = owner;
    e.kind = kind;
    e.yoff = yoff;
    list.push_back(e);
}

void step_emotion(Emotion& e) {
    if (e.dead) return;
    const int k = kind_of(e);
    e.age += 1;
    e.cell = cell_at(k, e.age);
    // The vertex alpha from the alpha as it stood: (alpha+1.0) float-bits trick = floor(alpha*256).
    e.a8 = e.alpha >= 1.0f ? 255 : std::uint8_t(std::floor(e.alpha * 256.0f));
    const int past = e.age - kHold[k];
    if (e.age < 15) e.alpha = float(e.age) / 15.0f;              // 0x6bd964
    else if (past <= 0) e.alpha = 1.0f;
    else if (past < 25) e.alpha = float(25 - past) / 25.0f;     // 0x6bd9c0
    else e.dead = true;                                          // destroy, no draw this tick
}

EmotionQuad emotion_quad(const Emotion& e) {
    const float c = kCentre[kind_of(e)];
    const int col = e.cell & 7, row = e.cell >> 3;
    EmotionQuad q;
    q.x0 = c - 0.375f; q.x1 = c + 0.375f;
    q.y0 = 0.0f; q.y1 = 0.75f;
    q.u0 = float(col) * 0.125f; q.u1 = q.u0 + 0.125f;
    q.v0 = float(row) * 0.25f; q.v1 = q.v0 + 0.25f;
    q.a8 = e.a8;
    return q;
}

}  // namespace cleria
