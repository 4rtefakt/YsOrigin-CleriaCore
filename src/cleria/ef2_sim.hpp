// The EF2 effect / particle simulation, ported from build/ai_spec/EF2SIM.md (the GoEffect /
// GoParticle step, read from the disassembly) with the particle-borne hit records of
// build/ai_spec/EFFECTHIT.md.
//
// An effect is a transform; it spawns its root emitters' particles once, at creation, and
// lives while it owns a particle. Each 60 Hz step a particle ages, samples its 17 tracks,
// moves in the effect's local space, rebuilds its matrix and fires its child emitters. The
// RNG is the engine's global xorshift128 (injectable, so the worked examples replay).
// Matrices follow D3DX: row vectors, v' = v * M.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "cleria/ef2.hpp"
#include "cleria/mathx.hpp"

namespace cleria::ef2sim {

// The engine's xorshift128 at 0x74622c (EF2SIM.md §3.2). `bind` makes it draw from shared
// state (the host's one game RNG: brains, scripts and effects all advance the same words).
struct Rng {
    std::uint32_t x = 123456789, y = 362436069, z = 521288629, w = 916191069;   // the .data words
    std::uint64_t calls = 0;
    std::uint32_t* ext = nullptr;
    void bind(std::uint32_t* state4) { ext = state4; }
    std::uint32_t next() {
        ++calls;
        std::uint32_t* s = ext;
        std::uint32_t own[4] = {x, y, z, w};
        if (!s) s = own;
        const std::uint32_t t = s[0] ^ (s[0] << 11);
        s[0] = s[1]; s[1] = s[2]; s[2] = s[3];
        s[3] = s[3] ^ (s[3] >> 19) ^ t ^ (t >> 8);
        if (!ext) { x = own[0]; y = own[1]; z = own[2]; w = own[3]; }
        return s[3];
    }
};

struct M4 {
    float m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
};
M4 mul(const M4& a, const M4& b);                       // a then b (row vectors)
M4 scaling(float x, float y, float z);
M4 rot_ypr(float yaw, float pitch, float roll);         // D3DXMatrixRotationYawPitchRoll
M4 translation(float x, float y, float z);
Vec3 transform_coord(const Vec3& v, const M4& m);

// The hit record a particle carries while its emitter's B2+0x894 is non-zero
// (EFFECTHIT.md §3.3), refreshed every live step (§4.2).
struct HitRecord {
    bool armed = false;
    int mode = 0;                 // 1 hit repeatedly, 2 hit once then die
    Vec3 centre{};                // world position + (0, voff, 0)
    float radius = 0, half_h = 0; // half_h > 0: a vertical cylinder
    int rehit = 0;
    float power = 0, knock = 0;
    int rtype = 0, status = 0, cls = -1, side = 1;
    float atk = 0;                // the owner's ATK copied at spawn (the stat block's +0xa4)
};

struct Effect;

struct Particle {
    Effect* fx = nullptr;
    int em = 0;                   // emitter index
    Particle* parent = nullptr;
    bool dead = false, dying = false;
    // sub (= the B1 copy), the fields the step uses
    std::uint8_t attach = 0, frame_counter = 0, frame = 0;
    bool colour_lock = false;
    int delay = 0, life = 0, remaining = 0;
    Vec3 scale{}, mult{}, vel{}, pos{}, rot{};
    float speed = 1;
    int fade_rate = 0, fade_acc = 0;
    float uv_u = 0, uv_v = 0, mesh_time = 0;
    std::uint32_t diffuse = 0, specular = 0;
    M4 world;                     // p+0x120; row 3 = the world position
    Vec3 prev_world{};            // p+0x290
    bool collide = false;         // p+0x2b0: die on world contact (B2+0x895 & 1)
    bool latched = false;         // p+0x2b1
    Vec3 latch{};                 // p+0x2b4: the position the contact trace starts from
    HitRecord hit;
    Vec3 world_pos() const { return {world.m[3][0], world.m[3][1], world.m[3][2]}; }
};

struct Effect {
    int id = 0;
    const Ef2* ef = nullptr;
    M4 M;                         // fx+0x20, the world matrix every particle multiplies by
    M4 local;                     // fx+0x120 (attach)
    const M4* attach_parent = nullptr;   // fx+0x1a4
    int attach_mode = 0;          // fx+0x1d5
    float scale_x = 1, scale_y = 1, scale_z = 1;   // fx+0x1ac.. (1.0 on every spawn path)
    float alpha_mul = 1, alpha_target = 1, alpha_step = 0;
    bool kill = false;            // fx+0x1d4
    int fade_rate = 0;            // fx+0x1d0
    int live = 0;                 // fx+0x1b8
    bool dead = false;
    bool held = false;            // a carrier (a GoXx bullet) still points at it: not freed
    // hit parameters (EFFECTHIT.md §2)
    float power = 0, knock = 1, atk = 0;
    int rtype = 0, status = 0, cls = -1, side = 1;
    int owner = 0;                // an actor instance id (bookkeeping only)
    int tag = -1;                 // a brain's private tag (M530 beams, M140 charge)
    // fx+0x201: the world-contact trace of its particles runs on collision set 1 (the water list;
    // FUN_005645b0 0x5645f9 passes it as FUN_0041e420's ecx) instead of set 0. CleriaCore has no
    // water collision, so such particles never touch the world (Velagunder's laser beam).
    bool set1 = false;
    std::string name;             // fx+0x20c, set by the script placements (FUN_00451da0)
    // fx+0x1f0: the draw depth bias, emitter 0's B2+0x0c, copied by every spawn path (FUN_00450410,
    // FUN_00478510, FUN_00463f40: `+0x1f0 = *(*(slot + 8) + 0xc)`). FUN_00451e60 turns it into
    // fx+0x1f4 = normalize(fx pos - camera eye) * (it + fx+0x204), and FUN_0044f2e0 draws a
    // billboard / world quad at p+0x150 - fx+0x1f4 [EXPORT 0044f2e0.c:188-191, 217-219]: pulled
    // that far toward the eye. Rings (p+0x120 as is, 0x44f931) and meshes do not take it.
    float depth_bias = 0;
    float bias_extra = 0;         // fx+0x204: op 0x11b EftAll_AddCameraOffset's addition to the bias
};

// The draw-time offset FUN_0044f2e0 subtracts (fx+0x1f4): `bias` along the unit vector from the
// eye to the effect origin; zero when the bias is 0 or the two coincide. EF2SIM.md §1.5.
Vec3 depth_bias_offset(const Vec3& fx_pos, const Vec3& eye, float bias);

struct SpawnParams {
    Vec3 pos{}, rot{};            // rot = (pitch, yaw, roll) as FUN_00451a40 reads it
    Vec3 scale{1, 1, 1};
    float power = 0, knock = 1, atk = 0;
    int rtype = 0, status = 0, cls = -1, side = 1;
    int owner = 0, tag = -1;
};

class System {
  public:
    explicit System(Rng* rng) : rng_(rng) {}
    // FUN_00450410 / the brains' inlined spawn: the effect, its root particles, one update.
    Effect* spawn(int id, const Ef2& ef, const SpawnParams& sp);
    // One 60 Hz step: every effect, then every particle (counts snapshot first; EF2SIM §1.1).
    // `on_hit(p)` is the hit tick of each armed, live particle (EFFECTHIT §4.2); it returns
    // true when the hit landed, so a mode-2 particle disarms and fades (§5.5).
    void step(const std::function<bool(Particle&)>& on_hit = {});
    // The per-render-frame sweep: destroy dead particles (their effects' counts drop).
    void sweep();
    void clear() { parts_.clear(); fx_.clear(); }
    // M140's KILLFX over every effect an owner tagged: each particle loses its hit and fades
    // over 16 steps with its clock reset to 16 (FUN_00564480 -> FUN_0044ebb0 force 1).
    void kill_owned(int owner, int tag);
    // FUN_0047a7c0 (M640.md §7, M620.md): every live particle of `fx` starts dying (p+0x1b4 = 1) and its
    // kill flag fx+0x1d4 rises. Unlike kill_owned, hits and clocks are left alone.
    void kill_effect(Effect* fx);
    // Re-place every effect an owner tagged (FUN_00451a40 on M530's beams).
    void move_owned(int owner, int tag, const Vec3& pos, const Vec3& rot, const Vec3& scale);
    // FUN_00451a40: the world matrix S * RotYPR * T, and the effect scale +0x1ac.. = scale.
    static void place(Effect& fx, const Vec3& pos, const Vec3& rot, const Vec3& scale);
    // The first live particle of `fx` in the pool (the bullets' `p+0x1a0 == fx` scans).
    Particle* first_particle(const Effect* fx);
    // FUN_00564480(ecx = p, force 1): FUN_0044ebb0(force 1) — the particle dies, its effect's
    // kill flag rises, the impact child spawns — then the hit and the world collide go off.
    bool strike(Particle& p);
    // The EF2 of an EFTLIST id, for impact children (B2+0x16); none = no impact effects.
    void set_lookup(std::function<const Ef2*(int)> f) { lookup_ = std::move(f); }
    // The terrain segment test (FUN_0041e420, set 2) for particles that die on world contact:
    // true on a hit, with the contact point. None = no world contact.
    void set_world(std::function<bool(const Vec3& from, const Vec3& d, Vec3* at)> f) { world_ = std::move(f); }
    const std::vector<std::unique_ptr<Particle>>& particles() const { return parts_; }
    const std::vector<std::unique_ptr<Effect>>& effects() const { return fx_; }
    std::vector<std::unique_ptr<Effect>>& effects() { return fx_; }

  private:
    void emit(Effect& fx, int em, int delay, Particle* parent);
    void spawn_init(Particle& p, Effect& fx, int em, int delay, Particle* parent);
    bool update_particle(Particle& p, bool moving);
    void full_step(Particle& p, bool moving);
    void simple_step(Particle& p, bool moving);
    void hit_tick(Particle& p, const std::function<bool(Particle&)>& on_hit);
    void collide_tick(Particle& p);
    bool impact(Particle& p, bool force);
    Rng* rng_;
    std::function<const Ef2*(int)> lookup_;
    std::function<bool(const Vec3&, const Vec3&, Vec3*)> world_;
    std::vector<std::unique_ptr<Effect>> fx_;
    std::vector<std::unique_ptr<Particle>> parts_;
};

}  // namespace cleria::ef2sim
