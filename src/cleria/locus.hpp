// GoLocusSword — the sword-sweep hit arm of `anime_GenerateLocus` (ASO funcid 0x24).
// Spec: build/ability_re/LOCUS.md (+ ABILITIES_VERIFIED.md §3.1, HITVOLUME.md §1.3/§2).
//
//   ctor   FUN_00464960 (vtable 0x69c2a4, object type 0xb, pool DAT_00730104)
//   update FUN_005002e0 (vtable slot 2), run by the master update FUN_004281c0 AFTER the player
//          (DAT_0074ab90) and the actor list — so a locus spawned by the player's ASO this tick
//          gets its first update in the same tick [EXPORT 0x4281c0].
//   SL2    FUN_00453e90 loads data\effect\sword0..5.sl2 into 6 x 0x34-byte headers at 0x76efc8.
//
// A locus is a weapon trail whose tip follows one vertex of an SL2 frame animation. While its
// hit life lasts it carries THREE r 0.45 spheres along the segment floor-point -> tip, and it is
// scanned for hits by the same mode-0 scan as the player's GenerateCollision box (FUN_004fed50 ->
// FUN_004fbb40), with the swing's token, so the two volumes share the victims' re-hit banks.
//
// Pure: no view, no world. The host supplies the owner's origin / facing every tick and runs the
// spheres through its own victim path (hit_volume.hpp overlap + RehitBank, hit.hpp damage).
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "cleria/hit_volume.hpp"
#include "cleria/mathx.hpp"

namespace cleria {

// One SWORD%d.SL2 file, as FUN_00453e90 leaves it in memory [BYTES 0x453e90, DATA]:
//   header 0x1c: magic, nframes, nverts, ntris, scale (f32, -> 0x76efd8), ?, nrows
//   nverts x 0x1c template vertices (render only), nrows x 0x10 rows {i32 start, i32 end, f32 scale, pad},
//   nframes x (8 + nverts*12) frames {f32 advance, f32 (alpha, render only), nverts x vec3}.
//   The loader's buffer has ntris x 6 more bytes, not in the file (render only).
struct Sl2 {
    struct Row { std::int32_t start = 0, end = 0; float scale = 1.0f; };
    std::int32_t nframes = 0, nverts = 0, ntris = 0;
    float scale = 1.0f;                 // header +0x10 = 0x76efd8 + i*0x34: the D3DXMatrixScaling factor
    std::vector<Row> rows;              // 0x76efe8 + i*0x34
    std::vector<float> advance;         // frame +0: the per-tick frame step while in this frame
    std::vector<Vec3> tip;              // frame +8: vertex 0, the only vertex the hit arm reads

    static Sl2 parse(const std::vector<std::uint8_t>& bytes);
    bool ok() const { return nframes > 0 && !rows.empty(); }
};

// The six files sword0..sword5 (string table 0x68acc4) [BYTES].
struct Sl2Set {
    std::array<Sl2, 6> sword;
    bool load(const std::filesystem::path& asset_root);   // assets/EFFECT/SWORDn.SL2
};

// FUN_00464960 0x464a5f..0x464b97: which SWORD file the owner's locus uses [BYTES].
//   cast 0xd (the player), by DAT_0076bb74 = g_flags[150] (Adol 0, Yunica 1, Hugo 2, Toal 3):
//     Adol   -> 0 (5 when DAT_0074c0cc, the Adol Extra-mode flag, is set)
//     Yunica -> 4 when g_flags[149] != 0, else 1      (jump table 0x464ed8 -> 0x464b2d)
//     Hugo   -> 2 ; Toal -> 3 ; anything else -> 0
//   cast 0xa6 -> the Yunica rule ; cast 0xa8 -> 3 ; any other cast -> 0.
int locus_sword_index(int cast, int character, int flag149, bool adol_extra = false);

// GenerateLocus's 10 script args (type string `iiiiiiffii`, ASOVM.md).
struct LocusArgs {
    std::int32_t row = 0x100;           // [0] SL2 row + 0x100
    std::int32_t a1 = 0, a2 = 0, a3 = 0;// [1..3] -> +0x15c (int), +0x160/+0x164 (float): only the
                                        //   player-boost trail effect 0x500dd0 reads them [not modelled]
    std::int32_t clank_lo = 0, clank_hi = 0;   // [4],[5] -> +0x170/+0x174: the wall-clank frame window
    float bonus = 0.0f;                 // [6] -> +0xfc  power (hit.hpp bonus)
    float knock = 0.0f;                 // [7] -> +0x104 knockback scale
    std::int32_t life = 0;              // [8] -> +0x118 hit life (0 -> the re-hit value)
    std::int32_t rehit = 0;             // [9] -> +0xf4  re-hit timer (0 -> 15)
};

class LocusSword {
public:
    static constexpr float kRadius = 0.45f;   // 0x464d40: 0x3ee66666, each sphere

    // What the update reads from its owner (+0x120) each tick.
    struct Owner {
        Vec3 origin{};          // +0x50: the owner's ORIGIN (not its feet)
        float floor_off = 0;    // +0x324: origin height above the floor (player 1.075)
        float facing = 0;       // +0x314: the visible facing, radians, heading 0 = +X (x = cos, z = sin)
        bool dead = false;      // +0x98 (HP) < 1.0 -> start the 0.15/tick fade
    };

    // FUN_00464960. `token` = the owner's +0x3c4 (the swing's hit serial). `flag215` =
    // DAT_0076bc78 (g_flags[215]) and `flag217` = DAT_0076bc80 (g_flags[217]) scale the row
    // (meanings unknown); `character` = g_flags[150]. False (nothing spawned) when the file is
    // missing or the row is past the file's row table — the engine would read past it [H].
    bool spawn(const Sl2& sl2, const LocusArgs& a, std::uint32_t token,
               int flag215 = 0, int flag217 = 0, int character = 0);

    // One logic tick of FUN_005002e0. False once the locus has died (this tick or before).
    // When it returns true and armed() is true, spheres() hold this tick's three hit spheres,
    // which the host scans exactly once (FUN_004fed50 mode 0, origin NULL: no re-placement).
    bool tick(const Owner& owner);

    // A landed hit by the owner (FUN_004fbb40 0x4fbcb2..0x4fbcd1): +0x180 += 1, so the next
    // update holds the frame instead of advancing it.
    void freeze() { ++freeze_; }

    bool alive() const { return alive_; }
    bool armed() const { return armed_; }                // scanned this tick (+0x118 was non-zero)
    const std::array<HitShape, 3>& spheres() const { return sph_; }   // +0xd0, +0x184, +0x1b0
    Vec3 tip() const { return tip_; }                    // this tick's rotated tip offset (from the floor point)

    float frame() const { return frame_; }               // +0x128
    float end_frame() const { return end_; }             // +0x12c = row.end - 1.0
    float row_scale() const { return scale_; }           // +0x140
    std::int32_t life() const { return life_; }          // +0x118
    std::int32_t rehit() const { return rehit_; }        // +0xf4
    float bonus() const { return bonus_; }               // +0xfc
    float knock() const { return knock_; }              // +0x104
    std::uint32_t token() const { return token_; }       // +0xf0
    std::int32_t clank_lo() const { return clank_lo_; }
    std::int32_t clank_hi() const { return clank_hi_; }
    int frame_at_tick_start() const { return fi_start_; }   // the [4],[5] window is tested on this

private:
    const Sl2* sl2_ = nullptr;
    bool alive_ = false, armed_ = false;
    float frame_ = 0, end_ = 0, scale_ = 1.0f;
    float alpha_ = 1.0f, fade_ = 0.0f;     // +0x138 / +0x13c
    int freeze_ = 0;                       // +0x180
    int fi_start_ = 0;
    std::int32_t life_ = 0, rehit_ = 0, clank_lo_ = 0, clank_hi_ = 0;
    float bonus_ = 0, knock_ = 0;
    std::uint32_t token_ = 0;
    Vec3 tip_{};
    std::array<HitShape, 3> sph_{};
};

}  // namespace cleria
