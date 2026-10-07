// The floor-3 script weather (build/effect_re/WEATHER.md): XSO ops 0x125 FireDrop (embers),
// 0x122 FrameSmoke (smoke puffs) and 0x123 FireBlur (the heat-haze timer).
//
// 0x125 / 0x122 each allocate a GoXx from pool 9 (FUN_0040b2d0 -> FUN_0040f700, cap 2048) and make
// it an emitter: type 0x12 (FUN_0058e790, update FUN_0058e7c0) or type 0x10 (FUN_0058da80, update
// FUN_0058daa0). The emitters draw from the game's ONE xorshift128 (0x74622c..38):
//   ember  every 16th tick of its life: 2 draws, then (when the pool has a slot) 8 more and one
//          child sprite; flag 290 (the S_3009 noise) adds one draw before the alloc.
//   smoke  on its first tick: 3 draws (5 with flag 290), then (slot) 7 more and one child sprite;
//          then every tick: 3 draws (the child's drift), unless the alloc failed this tick.
// The children are GoXx type 0 sprites (FUN_00583d90 init, FUN_00584030 step, FUN_00584270 draw):
// a yaw billboard that drifts and fades by key frames; they draw nothing from the RNG.
// The update order is the pool's: emitters and sprites in creation order, the count snapshot at
// the pass start (a child made this tick first moves next tick), FUN_004281c0 step 11 — after
// the actors, before pool 2 (the GoXx bullets) and pool 6 (the EF2 particles).
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "cleria/mathx.hpp"

namespace cleria::weather {

// The texture handles the emitters hand their sprites (+0x120).
enum class Tex : std::uint32_t {
    Sparkle = 1,   // 0x749dc4 = image table entry 30, data\image\sparkle.dds (the embers)
    Sparkle2,      // 0x749dc8 = entry 31, sparkle2.dds (the ember emitter's +0x130 != 0; 0x125 writes 0)
    Kao00,         // 0x749e3c = entry 60, kao_00.dds (the smoke under flag 290)
    EftTex34,      // 0x74a5e0 = the EFTTEX table 0x74a558 [34] (the smoke)
};

// FUN_00583d90's argument: the 27-dword child descriptor, field for field [EXPORT 00583d90].
struct Desc {
    Tex tex{};                       // [0]  -> +0x120
    std::uint32_t rstate = 0x46c1;   // [1]  -> +0x124 render state (0x46c1 = additive)
    std::uint32_t kind = 0xc;        // [2]  -> +0x128
    std::uint32_t argb = 0;          // [3]  -> +0x12c = argb & 0xffffff; argb >> 24 scales [19..22]
    std::uint32_t delay = 0, pad = 0;   // [4] -> +0x130 (a start delay), [5] unused
    Vec3 pos{};                      // [6..8]  -> +0x50
    Vec3 vel{};                      // [9..11] -> +0x220
    std::uint32_t uv = 0;            // [12] UV-rect table pointer (0 = 0x7461c4 = {0, 0, 1, 1})
    float angle = 0;                 // [13] -> the cos/sin pair +0x144/+0x148 (256-entry LUT)
    float drag = 0;                  // [14] -> +0x13c: vel *= 1 - drag per tick when != 0
    float depth_k = 0;               // [15] -> +0x140: the draw offset along 0x795f50
    float life = 0, t1 = 0, t2 = 0;  // [16..18] -> +0x14c / +0x150 / +0x154
    float alpha[4] = {};             // [19..22] -> +0x158.. (x the argb alpha byte)
    float size[4] = {};              // [23..26] -> +0x168..
};
static_assert(sizeof(Desc) == 27 * 4, "FUN_00583d90 reads 27 dwords");

// One GoXx sprite (type 0) as FUN_00583d90 leaves it.
struct Sprite {
    Desc d;
    Vec3 pos{}, prev{}, vel{};       // +0x50, +0x1f8, +0x220
    float age = 0;                   // +0x138
    float cs = 1, sn = 0;            // +0x144 / +0x148: cos_table(angle * 256/2pi), its -0x40 neighbour
    float alpha[4] = {}, size[4] = {};
};

// The draw values of a sprite for its current age (FUN_00584270, without the render
// interpolation): alpha 0..255 and the half-size.
struct Look { float alpha = 0, half = 0; };
Look look(const Sprite& s);

struct Obj {
    enum Kind : std::uint8_t { Ember, Smoke, Child } kind = Child;
    bool dead = false;               // +0x11 & 2 (destroy requested; freed by the sweep)
    // emitters
    std::uint32_t cap = 0;           // +0x12c, the op's operand: the life in ticks
    std::uint32_t n = 0;             // ember +0x134 / smoke +0x130, the tick counter
    Obj* child = nullptr;            // +0x13c: the last child made
    Vec3 off{};                      // smoke +0x140.. : the child's offset from the camera target
    std::uint32_t f138 = 0;          // smoke +0x138 = (*0x730224)+0x268 [not read anywhere traced]
    // the type-0 child
    weather::Sprite s;
};

// The world the update reads. Camera = the last LookAt (FUN_005ec270): eye 0x7b33f4, at 0x7b3400.
struct Env {
    Vec3 eye{}, at{};
    bool noise = false;              // flag 290 (0x76bda4), set on entry to S_3009
    bool map = true;                 // *0x730224 != 0: a room's render geometry is loaded
    std::uint32_t map268 = 0;        // (*0x730224)+0x268
};

// FireBlur's wave grid (WEATHER.md §5): 36 pretransformed vertices at 0x7b251c (stride 0x1c: x, y,
// z, rhw, colour, u, v), built once by FUN_005e44c0 (renderer init, from FUN_005e2670) over a
// 4 x 6 lattice of the back buffer, as one triangle strip of three column bands (down, up, down;
// the joins are degenerate). The draw pass FUN_005fed30 copies the back buffer into a texture
// (StretchRect) and redraws it through this mesh; each vertex keeps its lattice UV, so moving the
// vertex warps the picture. The step (FUN_00427c90, 0x427e66..0x427f22) restores the base copy
// (0x7b290c) and adds  x += amp[i] cos(a[i] deg),  y += amp[i] cos(b[i] deg) * 1.25  after
// a[i] += 1, b[i] += 6 (0x7b2cfc / 0x7b2d8c, never reset). amp = 0x6983d8: 7 / 5.25 / 4 / 4 /
// 5.25 / 7 back-buffer pixels by lattice row.
struct BlurGrid {
    static constexpr int kN = 36;
    std::int32_t a[kN], b[kN];       // 0x7b2cfc / 0x7b2d8c, degrees (FUN_005e44c0 0x5e549b.. tables)
    float dx[kN] = {}, dy[kN] = {};  // the vertex's offset from its base (0 until the first step)
    bool drawn = false;              // 0x7b2e1c: set by the draw pass, consumed by the next step
    BlurGrid();
    void step();
    // Vertex i of the strip for a W x H back buffer copied into a TW x TH texture: the position in
    // D3D9 pretransformed pixels (pixel centres on integers) and the copy's UV.
    struct V { float x, y, u, v; };
    static V base(int i, int W, int H, float TW, float TH);
    V vertex(int i, int W, int H, float TW, float TH) const {
        V v = base(i, W, H, TW, TH); v.x += dx[i]; v.y += dy[i]; return v;
    }
};

// 0x123 FireBlur(level, delta, strength) -> 0x76a558 / 0x76a55c / 0x76a560, stepped by
// FUN_00427c90 (master update step 9): while level != 0, level += delta and 0x7b24a0 = 1; at <= 0
// it stops and 0x7b249c is 0, else level is clamped to 1, 0x7b249c = strength and (when the pass
// drew since the last step) the wave grid steps. The pass draws while 0x7b24a0 && 0x7b249c != 0:
// the strength is only a gate (0x7b249c has no other reader [BYTES]).
struct Blur {
    float level = 0, delta = 0, strength = 0;   // 0x76a558 / 0x76a55c / 0x76a560
    float out = 0;                              // 0x7b249c
    bool latch = false;                         // 0x7b24a0 (= the .data byte 0x6d6071 = 1)
    BlurGrid grid;                              // 0x7b251c..
    void step();
    bool active() const { return latch && out != 0.0f; }   // FUN_005fed30's gate
};

class Weather {
  public:
    // One xorshift128 advance of the game's shared state, returning the new w.
    std::function<std::uint32_t()> rng;
    std::size_t pool_cap = 2048;     // pool 9's capacity [approx: its UI GoXx are not counted]

    void fire_drop(std::uint32_t life);    // 0x125: FUN_0058e790 (+0x12c = life, +0x130 = 0)
    void frame_smoke(std::uint32_t life);  // 0x122: FUN_0058da80 (+0x12c = life)
    void fire_blur(float level, float delta, float strength) { blur.level = level; blur.delta = delta; blur.strength = strength; }
    // One 60 Hz pass of pool 9's weather objects (run gate 0x74ab8f == 0 && 0x74c0f0 != 0 is the
    // caller's: a logic tick, under a Fix too).
    void step(const Env& e);
    void step_blur() { blur.step(); }
    // The per-frame sweep FUN_00428b10: destroyed objects leave the pool.
    void sweep();
    // Room teardown FUN_00428ec0 (every GoXx goes) and the scene entry's 0x76a558 = 0 (FUN_00431260).
    void clear() { objs_.clear(); blur.level = 0; blur.out = 0; }
    const std::vector<std::unique_ptr<Obj>>& objects() const { return objs_; }
    std::size_t live_emitters() const;
    // Every descriptor handed to FUN_00583d90 since the last call (tests / logs).
    std::vector<Desc> take_emits() { std::vector<Desc> v; v.swap(emits_); return v; }
    Blur blur;

  private:
    Obj* alloc(Obj::Kind k);
    void ember(Obj& o, const Env& e);
    void smoke(Obj& o, const Env& e);
    void sprite(Obj& o);
    Obj* emit(const Desc& d);
    std::vector<std::unique_ptr<Obj>> objs_;
    std::vector<Desc> emits_;
};

// The [1,2) float of a draw: (w & 0x7fffff) | 0x3f800000.
float unit12(std::uint32_t w);

}  // namespace cleria::weather
