// The water rings and wakes (pool 16, build/effect_re/WATER_RINGS.md): flat quads on a water surface that grow and
// fade, spawned by the actors wading (FUN_0045c490), Napishtim Adol's kind-0x19 follower (anime_Special a0 = 3,
// FUN_005914b0), the kind-0x17 runner's pulse in water (FUN_00591850), bullets (kinds 8, 0x29, 3 / 0xc), pickups and
// one floating platform. This is the pure part: the pool, its ageing, the quads Water_DrawRings builds, the random
// ring offset the spawners share and the kind-0x19 follower. The host casts the water rays, owns the texture and
// draws.
//
//   Water_SpawnRing  FUN_00454f40 [EXPORT 00454f40.c]          a round ring: a square on the water plane
//   Water_SpawnWake  FUN_00454ff0 [EXPORT 00454ff0.c]          an oriented wake: the square turned to a heading
//   the ageing       World_Update FUN_004281c0, its inline loop (the same code as the caller-less FUN_004550c0)
//   Water_DrawRings  FUN_00455250 [BYTES 0x455250..0x4556e2]   two passes, one per kind (texture)
//
// No RNG in the pool, the ageing or the draw: the draws are the spawners' (Rng_RingOffset below, and their own).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "cleria/cos_table.hpp"
#include "cleria/mathx.hpp"

namespace cleria::water {

// One pool-16 node (0x28 bytes, no vtable; include/go_*.h WaterRing).
struct Ring {
    Vec3 pos{};                 // +0x00
    float draw_age = 0;         // +0x0c: lerp(prev_age, age, the frame blend), what the draw reads
    float prev_age = 0;         // +0x10
    float age = 0;              // +0x14
    float scale = 0;            // +0x18: the spawn's XMM1 x 0.5 (0x6bd304)
    float dc = 1, ds = 0;       // +0x1c / +0x20: a wake's turn (cos, sin); left stale on a round ring
    bool round = true;          // +0x24
    std::uint8_t kind = 0;      // +0x25: the texture (0 ripple.dds, 1 ripple_r.dds on lava / blood)
};

inline constexpr int kCap = 256;             // the pool's capacity (OBJECT_MODEL.md pool 16; the draw's 0x7000-B buffer)
inline constexpr float kLife = 50.0f;        // 0x6bda60: removed when the age reaches it
inline constexpr float kFadeFrom = 35.0f;    // 0x6bda10: opaque until then, (50 - age) x 17 (0x6bd980) after
inline constexpr float kSurfaceLift = 0.15f; // 0x6bd1d8: every spawner puts its point this far over the surface

class Rings {
  public:
    // FUN_00454f40(ecx = &pos, dl = kind, xmm1 = scale): false when the pool is full (Pool_AllocWaterRing returns 0;
    // the pool's throw bit is clear [EXPORT 00455710.c]).
    bool spawn_ring(const Vec3& pos, int kind, float scale) {
        Ring* r = alloc();
        if (!r) return false;
        r->pos = pos;
        r->draw_age = r->prev_age = r->age = 0.0f;
        r->scale = scale * 0.5f;
        r->round = true;
        r->kind = std::uint8_t(kind);
        return true;
    }
    // FUN_00454ff0(ecx = &pos, dl = kind, xmm1 = scale, xmm2 = heading): the same node, not round, turned by the
    // cosine table at i = trunc((heading + pi/2) x 40.743664) [EXPORT 00454ff0.c]: (T[i], T[i - 64]).
    bool spawn_wake(const Vec3& pos, int kind, float scale, float heading) {
        Ring* r = alloc();
        if (!r) return false;
        r->pos = pos;
        r->draw_age = r->prev_age = r->age = 0.0f;
        r->scale = scale * 0.5f;
        r->round = false;
        r->kind = std::uint8_t(kind);
        const int i = int((heading + 1.5707964f) * 40.743664f);     // kf_1_5707964, kf_40_743664
        r->dc = cos_table(i & 0xff);
        r->ds = cos_table((i - 0x40) & 0xff);
        return true;
    }
    // One step (not paused, a real step): prev = age, age += 1, removed at 50 with the live list kept in order.
    // World_Update runs it after the actors, GoXx and the effects, so a ring made this step is drawn at age 1.
    void step() {
        std::size_t o = 0;
        for (std::size_t i = 0; i < live_.size(); ++i) {
            Ring r = live_[i];
            r.prev_age = r.age;
            r.age += 1.0f;
            if (r.age >= kLife) continue;
            live_[o++] = r;
        }
        live_.resize(o);
    }
    // A drawn frame: draw_age = (age - prev) x blend + prev (g_RenderInterp 0x74c0e0).
    void blend(float f) {
        for (Ring& r : live_) r.draw_age = (r.age - r.prev_age) * f + r.prev_age;
    }
    void clear() { live_.clear(); }
    const std::vector<Ring>& live() const { return live_; }
    std::size_t size() const { return live_.size(); }

    // Water_DrawRings' alpha byte (the colour is white): 0xff under 35, (int)((50 - age) x 17) from it.
    static std::uint8_t alpha(float age) {
        return age < kFadeFrom ? std::uint8_t(0xff) : std::uint8_t(int((kLife - age) * 17.0f));
    }
    // The half-size: (age x 0.02 + 0.5) x scale (0x6bd0ec, 0x6bd304).
    static float half(const Ring& r) { return (r.draw_age * 0.02f + 0.5f) * r.scale; }

    // The quads of one pass (pass = the kind: two passes, ring textures table 0x69b47c[pass]) in the live order,
    // each as four corners in strip order with (u, v) and the alpha [BYTES 0x455294..0x45559b]:
    //   round: (x-s, z-s) (x-s, z+s) (x+s, z-s) (x+s, z+s), u 0 0 1 1, v 0 0.5 0 0.5  — the texture's top half;
    //   wake:  the same square turned to (c, s), A = (c - s) x s, B = (c + s) x s:
    //          (x-A, z-B) (x-B, z+A) (x+B, z-A) (x+A, z+B), u 0 0 1 1, v 0.5 1 0.5 1 — its bottom half.
    // All at the node's y. emit(const Vec3 p[4], const float u[4], const float v[4], std::uint8_t alpha).
    template <class Emit>
    void quads(int pass, Emit&& emit) const {
        for (const Ring& r : live_) {
            if (r.kind != pass) continue;
            const float s = half(r), y = r.pos.y;
            Vec3 p[4];
            const float u[4] = {0.0f, 0.0f, 1.0f, 1.0f};
            if (r.round) {
                p[0] = {r.pos.x - s, y, r.pos.z - s};
                p[1] = {r.pos.x - s, y, r.pos.z + s};
                p[2] = {r.pos.x + s, y, r.pos.z - s};
                p[3] = {r.pos.x + s, y, r.pos.z + s};
                const float v[4] = {0.0f, 0.5f, 0.0f, 0.5f};
                emit(p, u, v, alpha(r.draw_age));
            } else {
                const float A = (r.dc - r.ds) * s, B = (r.ds + r.dc) * s;
                p[0] = {r.pos.x - A, y, r.pos.z - B};
                p[1] = {r.pos.x - B, y, r.pos.z + A};
                p[2] = {r.pos.x + B, y, r.pos.z - A};
                p[3] = {r.pos.x + A, y, r.pos.z + B};
                const float v[4] = {0.5f, 1.0f, 0.5f, 1.0f};
                emit(p, u, v, alpha(r.draw_age));
            }
        }
    }

  private:
    Ring* alloc() {
        if (live_.size() >= std::size_t(kCap)) return nullptr;
        live_.emplace_back();
        return &live_.back();
    }
    std::vector<Ring> live_;
};

// FUN_0045e390 [BYTES 0x45e390..0x45e459] (Rng_RingOffset): two steps of the shared xorshift — the first word's
// mantissa gives a radius in [lo, hi), the second word's top byte an angle index: dx = T[i] r, dz = T[i - 64] r.
template <class Rnd>
void ring_offset(Rnd&& rnd, float lo, float hi, float* dx, float* dz) {
    const std::uint32_t bits = (rnd() & 0x7fffffu) | 0x3f800000u;
    float u;
    std::memcpy(&u, &bits, 4);
    const float r = (hi - lo) * (u - 1.0f) + lo;
    const int i = int(rnd() >> 24);
    *dx = cos_table(i) * r;
    *dz = cos_table((i - 0x40) & 0xff) * r;
}

// ---- The kind-0x19 GoXx (anime_Special a0 = 3; GOXX_17_19.md §4) ------------------------------------------------
// Made inline by FUN_00449ed0 [BYTES 0x449f3e..0x449fb3]: age +0x124 = 0, life +0x12c = 110, probe height +0x130 =
// 10.0, owner +0x120 = the actor, pos = the actor's. Its update FUN_005914b0 [BYTES 0x5914b0..0x5916d1], per step:
// dies on the step its age is already over 110 (it lives 111 steps); age += 1; from age 7 on it follows the actor
// and, every 8th step (cd +0x128: 0 fires, then 7), if the actor is in water (+0x38a) and the water ray from 10.0
// over the actor down 10.0 (FUN_0041f130, set 1) hits more than 1.0 over the actor's origin: a ring 2.5 at the
// hit + a ring offset of 0..0.6 + 0.15 up, then a wake 3.25 at the same point with a random heading. Three draws of
// the shared xorshift a spawn (the offset's two, then U x 2 pi); none out of water. It hits nothing.
// [not modelled] the second blank GoXx the creator makes and drops (one pool slot for one step, nothing drawn).
struct Follower {
    Vec3 pos{};
    int age = 0, cd = 0;          // +0x124, +0x128
    bool dead = false;

    // `surface(at, &y)`: the water ray from (at.x, at.y + 10, at.z) down 10.0; true with the hit's y.
    // `rnd()`: one step of the shared xorshift. Returns true when it spawned a pair (out: where).
    template <class Surface, class Rnd>
    bool tick(const Vec3& actor_pos, bool actor_submerged, Surface&& surface, Rnd&& rnd, Rings& rings, Vec3* where = nullptr) {
        if (dead) return false;
        if (age > 110) { dead = true; return false; }
        ++age;
        if (age < 7) return false;
        pos = actor_pos;
        if (cd != 0) { --cd; return false; }
        cd = 7;
        if (!actor_submerged) return false;
        float y = 0.0f;
        if (!surface(pos, &y) || !(y - 1.0f > pos.y)) return false;       // 0x6bd438
        float dx = 0.0f, dz = 0.0f;
        ring_offset(rnd, 0.0f, 0.6f, &dx, &dz);                           // 0x6bd334
        const Vec3 w{pos.x + dx, y + kSurfaceLift, pos.z + dz};
        rings.spawn_ring(w, 0, 2.5f);                                     // 0x6bd624
        const std::uint32_t bits = (rnd() & 0x7fffffu) | 0x3f800000u;
        float u;
        std::memcpy(&u, &bits, 4);
        rings.spawn_wake(w, 0, 3.25f, (u - 1.0f) * 6.2831855f);           // 0x6bd6c0, 0x6bd898
        if (where) *where = w;
        return true;
    }
};

// ---- An actor wading (FUN_0045c490 [BYTES 0x45cbb1..0x45cec6]) -------------------------------------------------
// The counter +0x39b steps while the actor is wading or submerged (0 otherwise). With HP >= 1, wading (+0x389),
// not culled (+0x250), !(+0x12c & 0x400): on the steps where (counter & mask) == 0 — mask 3 for the player (cast
// 13), 7 for any other — `moved` = the xz move since the last step (+0x50 - +0x3d4) squared > 0.000625 (0x6bcfe4);
// a still player only every 8th. Then a ring offset 0..0.3 (0x6bd27c: two draws, also on sand2), W = (x + dx,
// surface +0x320 + 0.15, z + dz); unless the floor ray's surface +0x4ba is sand2 (0x12): a ring 1.5 (0x6bd4d4) at W,
// and when moved a wake 2.0 (0x6bd580) at (x, W.y, z) turned to atan2(dz, dx) of the move (FUN_0066f80a). The
// kind (texture) is 1 on a lava / blood floor (+0x4b9 0x0d / 0x11), else 0.
struct Wade {
    std::uint8_t ticks = 0;       // +0x39b
};
struct WadeIn {
    bool player = false;          // cast 13
    bool alive = true;            // HP >= 1.0
    bool wading = false, submerged = false;   // +0x389, +0x38a (this step's)
    bool hidden = false;          // +0x250 culled, or +0x12c & 0x400
    bool sand2 = false;           // +0x4ba == 0x12
    bool lava = false;            // +0x4b9 0x0d / 0x11
    Vec3 pos{}, prev{};           // +0x50, +0x3d4
    float surface = 0;            // +0x320
};
struct WadeOut {
    bool fired = false;           // the offset was drawn (two xorshift steps)
    bool moved = false;           // > 0.025 across this step
};
template <class Rnd>
WadeOut wade_step(Wade& w, const WadeIn& in, Rnd&& rnd, Rings& rings) {
    WadeOut o;
    if (in.submerged || in.wading) ++w.ticks;
    else w.ticks = 0;
    if (!in.alive || !in.wading || in.hidden) return o;
    if (w.ticks & (in.player ? 3 : 7)) return o;
    const float dz = in.pos.z - in.prev.z, dx = in.pos.x - in.prev.x;
    o.moved = dz * dz + dx * dx > 0.000625f;
    if (!o.moved && (w.ticks & 7)) return o;
    o.fired = true;
    float ox = 0.0f, oz = 0.0f;
    ring_offset(rnd, 0.0f, 0.3f, &ox, &oz);
    const int kind = in.lava ? 1 : 0;
    const Vec3 at{in.pos.x + ox, in.surface + kSurfaceLift, in.pos.z + oz};
    if (in.sand2) return o;
    rings.spawn_ring(at, kind, 1.5f);
    if (o.moved) rings.spawn_wake({in.pos.x, at.y, in.pos.z}, kind, 2.0f, float(std::atan2(double(dz), double(dx))));
    return o;
}

}  // namespace cleria::water
