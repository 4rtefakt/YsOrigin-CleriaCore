// Health bars over ordinary enemies: a CleriaCore addition, NOT retail ([cleria.qol] EnemyHealthBars, default 0 =
// the retail look). The bar is the boss mini gauge's quad (worldgauge.hpp: the oxygen bar's cell in red) drawn over
// an enemy's head. Its own small pool, keyed by the actor's instance id: the three retail gauge records
// (0x76c1cc..) stay the bosses'. Pure and read-only on the sim: the host feeds it what it sees each sim step and
// draws the result; no RNG, no allocation.
//   mode 1 "After a hit": a bar comes on when the enemy's HP drops, stays full kHold steps after the last hit, then
//                          fades over the mini gauge's 255 steps.
//   mode 2 "Always":      every enemy the host shows it.
// [approx: CleriaCore's own] every number here (kHold, the caps, the quad's place over the head).
#pragma once
#include <array>
#include <cstdint>

#include "cleria/mathx.hpp"

namespace cleria {

struct EnemyBars {
    static constexpr int kTrack = 64;        // enemies followed (their last HP, to see a hit)
    static constexpr int kDraw = 24;         // bars drawn at most: the nearest
    static constexpr int kHold = 120;        // steps at full alpha after the last hit (mode 1)
    static constexpr int kFade = 255;        // then the mini gauge's countdown (alpha = timer)
    enum Event { kNone = 0, kOn = 1, kFaded = 2, kGone = 3 };

    struct Track { int id = 0; float hp = 0, max = 0; int timer = 0; Vec3 anchor{}; float d2 = 0; bool live = false, seen = false; };
    std::array<Track, kTrack> t{};

    void clear() { t = {}; }
    void begin() { for (Track& k : t) k.seen = false; }
    // One eligible enemy this step: `anchor` = where its bar hangs (world), `d2` = its squared distance to the
    // player. kOn when its bar comes on by a hit (mode 1's log).
    Event see(int id, float hp, float max, const Vec3& anchor, float d2) {
        Track* k = nullptr;
        for (Track& c : t) if (c.live && c.id == id) { k = &c; break; }
        bool fresh = false;
        if (!k) {
            for (Track& c : t) if (!c.live) { k = &c; break; }
            if (!k) return kNone;            // more than kTrack enemies: the rest get no bar
            *k = Track{}; k->id = id; k->live = true; k->hp = hp; fresh = true;
        }
        Event e = kNone;
        if (!fresh && hp < k->hp) { if (k->timer == 0) e = kOn; k->timer = kHold + kFade; }
        k->hp = hp; k->max = max; k->anchor = anchor; k->d2 = d2; k->seen = true;
        return e;
    }
    // The end of the step: an enemy not seen (killed, removed, hidden) loses its bar at once; a timer counts down.
    // `ev(id, event)` for a bar that was on and went off.
    template <class F> void end(F&& ev) {
        for (Track& k : t) {
            if (!k.live) continue;
            if (!k.seen) { if (k.timer != 0) ev(k.id, kGone); k = Track{}; continue; }
            if (k.timer != 0 && --k.timer == 0) ev(k.id, kFaded);
        }
    }
    // The bar of track i in `mode` (1 / 2): its alpha byte, 0 = none. At most kDraw, the nearest ones.
    std::uint8_t alpha(int i, int mode) const {
        const Track& k = t[std::size_t(i)];
        auto on = [&](const Track& c) { return c.live && c.max > 0.0f && (mode == 2 || c.timer != 0); };
        if (!on(k)) return 0;
        int nearer = 0;
        for (int j = 0; j < kTrack; ++j) {
            const Track& c = t[std::size_t(j)];
            if (j != i && on(c) && (c.d2 < k.d2 || (c.d2 == k.d2 && j < i))) ++nearer;
        }
        if (nearer >= kDraw) return 0;
        return mode == 2 ? std::uint8_t(255) : std::uint8_t(k.timer < kFade ? k.timer : kFade);
    }
};

// The bar's quad in the AUTHORED 1024x768 frame, from the anchor projected into it: the mini gauge's size (84 x 12)
// and STATUS_512 cell, centred over the anchor and 22 px clear of it (a sprite's crest reaches over its collision
// height) [approx: CleriaCore's own]; the ratio clamped.
struct EnemyBarQuad { float x0, y0, x1, y1, sx, sy, sw, sh; };
inline bool enemy_bar_quad(float hp, float max, float ax, float ay, EnemyBarQuad* q) {
    if (max <= 0.0f) return false;
    const float r = hp <= 0.0f ? 0.0f : (hp >= max ? 1.0f : hp / max);
    q->x0 = ax - 42.0f; q->y0 = ay - 34.0f;
    q->x1 = q->x0 + r * 84.0f; q->y1 = q->y0 + 12.0f;
    q->sx = 288.0f; q->sy = 24.0f; q->sw = 84.0f * r; q->sh = 12.0f;
    return r > 0.0f;
}

}  // namespace cleria
