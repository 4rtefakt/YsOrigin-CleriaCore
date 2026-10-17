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
    EftTex96,      // 0x74a6d8 = EFTTEX [96], YsF_used/naga_g/ring_l.dds (a pickup's sparkle, FUN_00584860)
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

// One GoXx spark streak (type 3; build/mech_re/GOXX_PARTICLES.md §2.3): a point under gravity that bounces off
// the terrain and drags an 11-point history, drawn as a ribbon through it (FUN_00588450 init, FUN_005886b0
// collision + history, FUN_00588a50 step, LAB_00588c20 draw).
struct Spark {
    std::uint8_t delay = 0;          // +0x130: ticks before it starts (not stepped, not drawn)
    float age = 0, life = 0;         // +0x134 / +0x138
    Vec3 pos{}, vel{};               // +0x50 / +0x220
    Vec3 hist[11]{};                 // +0x144, stride 0xc: [0] the collision anchor, [1..10] the tail
    std::uint32_t skip = 0;          // +0x12c: collision-less calls left (the crowd throttle)
    float half = 0;                  // +0x13c = width * 0.4
    Vec3 prev{};                     // +0x1d4 (the render interpolation's "previous")
};
// LAB_00588c20 [BYTES 0x588c20..0x589129]: the ribbon is four quads through hist[1], [3], [5], [7], [9]. Quad q
// runs from P = hist[1 + 2q] to A = hist[3 + 2q]; its side vector is normalize(normalize(A - B) x F) * half, with
// F the camera forward 0x795f50 and B = P, except B = the current position (+0x1c8) for q = 0; the corners are
// P +- side (v = kV[q]) and A +- side (v = kV[q + 1]), u = 0 / 1 across. White, alpha 1 fading over the last 20
// ticks ((life - age) / 20), render state 0x146c1 (additive), IMAGE\SPARKLE.DDS; 16 vertices, 8 triangles.
inline constexpr float kSparkV[5] = {0.1f, 0.4f, 0.5f, 0.7f, 0.9f};   // 0x6a2b48..0x6a2b58
struct SparkQuad { Vec3 p[4]; float v[2]; };          // p: P + side, P - side, A + side, A - side
void spark_ribbon(const Spark& s, const Vec3& cam_fwd, SparkQuad out[4]);
inline float spark_alpha(const Spark& s) { return s.age > s.life - 20.0f ? (s.life - s.age) / 20.0f : 1.0f; }

// One GoXx floor decal (type 4; GOXX_PARTICLES.md §2.4): a blood splat or a slime puddle -- the record, its ray
// to the floor, its mesh (decal_mesh), its delay / age / life and the footprint trigger (FUN_00589a60 init,
// FUN_00589f60 step). The view draws it (view_weather.inl's draw_decals).
struct DecalVert { Vec3 p; float u = 0, v = 0, w = 0; };   // +0x174: a world vertex, its UV; +0x17c: its weight
inline constexpr std::size_t kDecalMaxVerts = 0x87;        // +0x1a4: 135 vertices = 45 triangles
struct Decal {
    std::vector<DecalVert> verts;    // the surface triangles the decal lies on, whole (3 each)
    std::uint8_t variant = 0;        // +0x160: the texture variant 0..2 (the batch index)
    std::uint32_t argb = 0;          // +0x168 = rgb, +0x188 = the alpha byte / 255
    std::uint8_t delay = 0;          // +0x16c
    bool keep = false;               // +0x16e (flag A): never expires
    bool slime = false;              // +0x16f (flag B)
    bool foot = false;               // +0x170: the footprint / slime trigger is armed
    Vec3 pos{}, normal{};            // the floor point under the spawn point, its normal (+0x190)
    float angle = 0, size = 0;       // the turn about the normal, +0x18c
    float life = 0, age = 0;         // +0x184, +0x180
};

// FUN_00589a60's frame + FUN_00589690 [BYTES 0x589c43..0x589e4e, 0x589690..0x589a55] (GOXX_PARTICLES.md §2.4):
// `tris` (3 points each) are the candidate surface triangles; the ones that pass go into d.verts WHOLE -- nothing
// is clipped, the texture is drawn clamped -- until the 45-triangle cap. Needs d.pos / normal / angle / size.
void decal_mesh(Decal& d, const std::vector<Vec3>& tris);
// FUN_00589f60's draw half [BYTES 0x58a0dc..0x58a1b0]: this frame's alpha (the colour's alpha byte, the 6-tick
// fade in, the 50-tick fade out), and a vertex's alpha byte = the top 8 mantissa bits of w x a + 1.
float decal_alpha(const Decal& d);
inline std::uint8_t decal_alpha8(float w, float a) {
    const float x = 256.0f * w * a;
    return x >= 255.0f ? 255 : x <= 0.0f ? 0 : std::uint8_t(x);
}

struct Obj {
    enum Kind : std::uint8_t { Ember, Smoke, Child, Streak, Splat } kind = Child;
    bool dead = false;               // +0x11 & 2 (destroy requested; freed by the sweep)
    // emitters
    std::uint32_t cap = 0;           // +0x12c, the op's operand: the life in ticks
    std::uint32_t n = 0;             // ember +0x134 / smoke +0x130, the tick counter
    Obj* child = nullptr;            // +0x13c: the last child made
    Vec3 off{};                      // smoke +0x140.. : the child's offset from the camera target
    std::uint32_t f138 = 0;          // smoke +0x138 = (*0x730224)+0x268 [not read anywhere traced]
    // the type-0 child
    weather::Sprite s;
    weather::Spark sp;               // Streak (type 3)
    weather::Decal dc;               // Splat (type 4)
};

// The world the update reads. Camera = the last LookAt (FUN_005ec270): eye 0x7b33f4, at 0x7b3400.
struct Env {
    Vec3 eye{}, at{};
    bool noise = false;              // flag 290 (0x76bda4), set on entry to S_3009
    bool map = true;                 // *0x730224 != 0: a room's render geometry is loaded
    // The player (0x74c09c), for a decal's footprint trigger: the origin's x / z, the ground y +0x318, +0x388.
    bool player = false;
    Vec3 player_pos{};
    float player_ground = 0;
    bool player_grounded = false;
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
    // One type-0 sprite from a descriptor (FUN_0040f700 + FUN_00583d90), for the other emitters of the same
    // sprite: a pickup's sparkle (FUN_00584860). [approx] it shares this pool's cap; nullptr when full.
    Obj* sprite(const Desc& d) { return alloc(Obj::Child) ? emit(d) : nullptr; }
    // FUN_00584550 (build/mech_re/GOXX_PARTICLES.md §3.1 [BYTES 0x584550..0x584857]): `count` dust puffs
    // (EFTTEX[34]) on a ring of `radius` about `pos`, drifting outward at `speed`, each starting 2 ticks after
    // the one before. 4 draws per puff (direction, distance, the quad's turn, the life 24 / 25); a full pool
    // ends the loop before that puff's draws.
    void dust(int count, std::uint32_t argb, bool additive, const Vec3& pos, float size, float radius,
              float speed, float drag = 0.0f, bool layer9 = false);
    // The room's nearest terrain hit on the segment a -> b (FUN_0041e420, set 2): a spark's bounce. None = no
    // terrain (no bounce, no bounce draw).
    std::function<bool(const Vec3& a, const Vec3& b, Vec3* point, Vec3* normal)> hit;
    // The surface triangles around a decal's floor point (FUN_00589a60's collection: the room model's opaque
    // render triangles within `size` of the 1.5 above `pos`), 3 points each. None = the decals keep no mesh
    // and never die of an empty one (the sim alone, as before 2026-10-11).
    std::function<void(const Vec3& pos, float size, std::vector<Vec3>& out)> surface;
    // FUN_00589130 (GOXX_PARTICLES.md §3.7 [BYTES 0x589130..0x589374]): `count` spark streaks from `pos`, each
    // starting `delay_step` ticks after the one before. 4 draws per spark (life 35 + 0..life_range, angle, speed,
    // vy), then FUN_00588450's ten pre-rolled quarter-steps, each of which may bounce (1 draw a bounce).
    void sparks(int count, int delay_step, int life_range, float width, const Vec3& pos, float speed0, float speed1,
                float vy0, float vy1, float ang0, float ang1);
    int live_sparks() const { return sparks_; }      // 0x75ae6c
    // FUN_0058a5f0 (GOXX_PARTICLES.md §3.3 [BYTES 0x58a5f0..0x58a8fd]): `count` blood splats / slime puddles on
    // the floor around `pos`. 6 draws per splat (variant, life, the offset's distance and direction, the turn,
    // the size), taken whether or not there is a floor within 5 below; in a water room FUN_0058a210's drifting
    // clouds instead (type-0 sprites, 5 draws each). Returns nothing: the decals are kept (the view draws them).
    void blood(int count, std::uint32_t argb, int delay_step, int life_range, float size0, float size1,
               const Vec3& pos, float radius, bool flag_a, bool flag_b);
    // FUN_0058a900: the one small drop a debris piece leaves at a bounce (4 draws; 5 as a cloud in water).
    void blood_drop(std::uint32_t argb, const Vec3& pos, float size0, float size1);
    int bloody = 2;                  // byte 0x6d5d92 (BloodyEffectLevel)
    // [0x76a544]: 0 a dry room, 2 whole-room water. [not modelled] 1 (a room with a water surface: the two
    // probes of FUN_0058a210) is treated as dry; [H] byte 0x7494d7 (unknown U10) is taken as 0.
    int water_mode = 0;
    // What a walked-through decal wrote (FUN_00589f60): P+0x39e = 45 with the colour in 0x76a5b4 (footprints), or
    // 0x76a5b0 = 55 with 0x76a5b4 = white (slime). [not modelled] their consumers in FUN_0045c490 / FUN_00427c90
    // (unknown U5): the values are kept, nothing reads them.
    int foot_ticks = 0, slime_ticks = 0;
    std::uint32_t foot_rgb = 0;
    std::size_t live_decals() const;
    // The per-frame sweep FUN_00428b10: destroyed objects leave the pool.
    void sweep();
    // Room teardown FUN_00428ec0 (every GoXx goes) and the scene entry's 0x76a558 = 0 (FUN_00431260).
    void clear() { objs_.clear(); sparks_ = 0; blur.level = 0; blur.out = 0; }
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
    void spark(Obj& o);
    void spark_collide(Spark& s);
    void decal(Obj& o, const Env& e);
    void decal_init(Obj& o, const Decal& d);
    bool water_cloud(int count, std::uint32_t argb, int delay_step, int life_range, float size0, float size1,
                     const Vec3& pos, float radius);
    void offset(float lo, float hi, float* dx, float* dz);
    Obj* emit(const Desc& d);
    int sparks_ = 0;
    std::vector<std::unique_ptr<Obj>> objs_;
    std::vector<Desc> emits_;
};

// The [1,2) float of a draw: (w & 0x7fffff) | 0x3f800000.
float unit12(std::uint32_t w);

}  // namespace cleria::weather
